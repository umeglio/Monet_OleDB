param(
    [string]$Path,
    [string]$Clsid = '{A3F2D8E1-7B4C-4E9A-B5D6-1C8F3E2A9D07}',
    [switch]$KillSurrogates = $true
)

$ErrorActionPreference = 'Stop'
$script:ScriptDir = Split-Path -Parent $PSCommandPath

function Resolve-ProviderPath {
    param([string]$RequestedPath)

    if ($RequestedPath) {
        return $RequestedPath
    }

    $inprocKey = "Registry::HKEY_CLASSES_ROOT\CLSID\$Clsid\InprocServer32"
    if (Test-Path $inprocKey) {
        $registeredPath = (Get-Item $inprocKey).GetValue('')
        if ($registeredPath) {
            return $registeredPath
        }
    }

    return (Join-Path $script:ScriptDir 'monetdb_oledb.dll')
}

function Get-DllLockingProcesses {
    param([string]$TargetPath)

    if (-not (Test-Path $TargetPath)) {
        return @()
    }

    $moduleName = Split-Path -Leaf $TargetPath
    $rows = @()
    $lines = & tasklist /m $moduleName /fo csv /nh 2>$null
    if (-not $lines) {
        return @()
    }

    foreach ($line in $lines) {
        if ([string]::IsNullOrWhiteSpace($line) -or $line.StartsWith('INFO:')) {
            continue
        }

        $entry = $line | ConvertFrom-Csv -Header ImageName, PID, SessionName, SessionNumber, MemUsage, Modules
        $processId = 0
        if (-not [int]::TryParse($entry.PID, [ref]$processId)) {
            continue
        }

        $proc = Get-CimInstance Win32_Process -Filter "ProcessId = $processId" -ErrorAction SilentlyContinue
        if (-not $proc) {
            continue
        }

        $service = Get-CimInstance Win32_Service -Filter "ProcessId = $processId" -ErrorAction SilentlyContinue |
            Select-Object -First 1 -ExpandProperty Name

        $rows += [PSCustomObject]@{
            ProcessId   = $processId
            Name        = $proc.Name
            ServiceName = $service
            CommandLine = $proc.CommandLine
        }
    }

    return $rows | Sort-Object ProcessId -Unique
}

function Stop-DllHostLockers {
    param([object[]]$Locks)

    $surrogates = $Locks | Where-Object { $_.Name -ieq 'dllhost.exe' }
    foreach ($surrogate in $surrogates) {
        Write-Host ("[INFO] Termino COM surrogate PID={0} ({1})" -f $surrogate.ProcessId, $surrogate.CommandLine) -ForegroundColor Yellow
        Stop-Process -Id $surrogate.ProcessId -Force -ErrorAction Stop
    }
}

$resolvedPath = Resolve-ProviderPath -RequestedPath $Path
if (-not (Test-Path $resolvedPath)) {
    Write-Host ("[INFO] DLL non trovata, nessun lock da rimuovere: {0}" -f $resolvedPath)
    exit 0
}

Write-Host ("[INFO] Verifico lock su {0}" -f $resolvedPath) -ForegroundColor Cyan
$locks = Get-DllLockingProcesses -TargetPath $resolvedPath

if ($locks.Count -eq 0) {
    Write-Host '[OK] Nessun processo sta usando la DLL.' -ForegroundColor Green
    exit 0
}

Write-Host '[INFO] Processi che hanno caricato la DLL:' -ForegroundColor Cyan
$locks | Format-Table -AutoSize

if ($KillSurrogates) {
    Stop-DllHostLockers -Locks $locks
    Start-Sleep -Seconds 1
    $locks = Get-DllLockingProcesses -TargetPath $resolvedPath
}

if ($locks.Count -eq 0) {
    Write-Host '[OK] Lock rimossi. La DLL puo essere aggiornata.' -ForegroundColor Green
    exit 0
}

Write-Host '[ERROR] La DLL e ancora in uso.' -ForegroundColor Red
foreach ($lock in $locks) {
    if ($lock.Name -ieq 'sqlservr.exe' -and $lock.ServiceName) {
        Write-Host ("  - SQL Server attivo: servizio {0} (PID={1}). Fermalo prima di sovrascrivere la DLL." -f $lock.ServiceName, $lock.ProcessId)
    } elseif ($lock.Name -ieq 'ssms.exe') {
        Write-Host ("  - SSMS e ancora aperto (PID={0}). Chiudi Object Explorer e le finestre di proprieta del linked server." -f $lock.ProcessId)
    } else {
        Write-Host ("  - Processo {0} PID={1}" -f $lock.Name, $lock.ProcessId)
    }
}

exit 1
