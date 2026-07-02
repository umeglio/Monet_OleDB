param(
    [string]$ProviderName = 'MonetDB.OleDb',
    [string]$Clsid = '{A3F2D8E1-7B4C-4E9A-B5D6-1C8F3E2A9D07}',
    [string]$DsnName = 'MonetDB'
)

$ErrorActionPreference = 'Stop'
$script:ScriptDir = Split-Path -Parent $PSCommandPath

function Resolve-InstallRoot {
    $registeredPath = $null
    $inprocKey = "Registry::HKEY_CLASSES_ROOT\CLSID\$Clsid\InprocServer32"
    if (Test-Path $inprocKey) {
        $registeredPath = (Get-Item $inprocKey).GetValue('')
        if ($registeredPath -and (Test-Path $registeredPath)) {
            return (Split-Path -Parent $registeredPath)
        }
    }

    if (Test-Path (Join-Path $script:ScriptDir 'monetdb_oledb.dll')) {
        return $script:ScriptDir
    }

    $parentDir = Split-Path -Parent $script:ScriptDir
    if ($parentDir -and (Test-Path (Join-Path $parentDir 'monetdb_oledb.dll'))) {
        return $parentDir
    }

    return $script:ScriptDir
}

function Get-DllLockingProcesses {
    param([string]$Path)

    if (-not (Test-Path $Path)) {
        return @()
    }

    $moduleName = Split-Path -Leaf $Path
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

function Get-SqlErrorLogFiles {
    $root = 'C:\Program Files\Microsoft SQL Server'
    if (-not (Test-Path $root)) {
        return @()
    }

    return Get-ChildItem $root -Recurse -Filter ERRORLOG -ErrorAction SilentlyContinue |
        Select-Object -ExpandProperty FullName -Unique
}

function Show-SqlErrorLogMatches {
    $patterns = 'MonetDB|OLE DB|7302|7303|7399|sp_testlinkedserver|linked server'
    $logs = Get-SqlErrorLogFiles

    Write-Host '== SQL Server ERRORLOG ==' -ForegroundColor Cyan
    if ($logs.Count -eq 0) {
        Write-Warning 'ERRORLOG non trovato.'
        Write-Host
        return
    }

    foreach ($log in $logs) {
        $matches = Select-String -Path $log -Pattern $patterns -ErrorAction SilentlyContinue |
            Select-Object -Last 20
        if ($matches) {
            Write-Host ("-- {0} --" -f $log) -ForegroundColor DarkCyan
            $matches | ForEach-Object { $_.Line }
            Write-Host
        }
    }
}

function Show-Key {
    param([string]$Path)
    if (Test-Path $Path) {
        Write-Host "== $Path ==" -ForegroundColor Cyan
        Get-ItemProperty -Path $Path | Format-List *
        Write-Host
    } else {
        Write-Warning "Chiave non trovata: $Path"
    }
}

Write-Host 'Diagnostica provider MonetDB OLE DB' -ForegroundColor Green
Write-Host

Show-Key "Registry::HKEY_CLASSES_ROOT\CLSID\$Clsid"
Show-Key "Registry::HKEY_CLASSES_ROOT\$ProviderName"
Show-Key "Registry::HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\MSSQLServer\Providers\$ProviderName"

$instancesKey = 'Registry::HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Microsoft SQL Server\Instance Names\SQL'
if (Test-Path $instancesKey) {
    Write-Host '== Istanze SQL Server ==' -ForegroundColor Cyan
    $instances = Get-ItemProperty -Path $instancesKey
    foreach ($property in $instances.PSObject.Properties | Where-Object { $_.Name -notmatch '^PS' }) {
        Write-Host ("{0} -> {1}" -f $property.Name, $property.Value)
        Show-Key "Registry::HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Microsoft SQL Server\$($property.Value)\Providers\$ProviderName"
    }
}

$dsnRoot = 'Registry::HKEY_LOCAL_MACHINE\SOFTWARE\ODBC\ODBC.INI'
Show-Key "$dsnRoot\ODBC Data Sources"
Show-Key "$dsnRoot\$DsnName"

$dllDir = Resolve-InstallRoot
$dllPath = Join-Path $dllDir 'monetdb_oledb.dll'
$iniPath = Join-Path $dllDir 'monetdb_oledb.ini'

Write-Host '== File installazione ==' -ForegroundColor Cyan
Get-Item $dllPath, $iniPath -ErrorAction SilentlyContinue | Select-Object FullName, Length, LastWriteTime | Format-Table -AutoSize
Write-Host

Write-Host '== Processi che tengono la DLL ==' -ForegroundColor Cyan
$lockers = Get-DllLockingProcesses -Path $dllPath
if ($lockers.Count -eq 0) {
    Write-Host 'Nessun processo sta usando la DLL.'
} else {
    $lockers | Format-Table -AutoSize
}
Write-Host

Write-Host '== ACL directory ==' -ForegroundColor Cyan
icacls $dllDir

Write-Host
Show-SqlErrorLogMatches
