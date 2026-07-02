param(
    [string]$SourceDll = "G:\mingw32\OleDB_MonetDB\build\Release\monetdb_oledb_137.dll",
    [string]$BuildDll = "G:\mingw32\OleDB_MonetDB\build\Release\monetdb_oledb.dll",
    [string]$InstallDll = "C:\monetdb_oledb\monetdb_oledb_135.dll",
    [string]$InstallSideBySideDll = "C:\monetdb_oledb\monetdb_oledb_137.dll"
)

$ErrorActionPreference = "Stop"
$log = "C:\monetdb_oledb\deploy_137_admin.log"

function Write-Step {
    param([string]$Message)
    $line = "[{0:yyyy-MM-dd HH:mm:ss}] {1}" -f (Get-Date), $Message
    Write-Host $line
    Add-Content -LiteralPath $log -Value $line
}

New-Item -ItemType Directory -Path (Split-Path -Parent $log) -Force | Out-Null
Set-Content -LiteralPath $log -Value ("[{0:yyyy-MM-dd HH:mm:ss}] deploy start" -f (Get-Date))

$stoppedServices = New-Object System.Collections.Generic.List[string]

try {
    if (-not (Test-Path -LiteralPath $SourceDll)) {
        throw "DLL sorgente non trovata: $SourceDll"
    }

    Write-Step "Arresto SQL Server se sta usando il provider..."
    $sqlServices = Get-Service | Where-Object { $_.Name -eq "MSSQLSERVER" -or $_.Name -like "MSSQL`$*" }
    foreach ($svc in $sqlServices) {
        if ($svc.Status -eq "Running") {
            Write-Step "Stop servizio $($svc.Name)"
            Stop-Service -Name $svc.Name -Force
            $svc.WaitForStatus("Stopped", "00:01:00")
            $stoppedServices.Add($svc.Name)
        }
    }

    Write-Step "Chiudo solo eventuali DllHost che hanno caricato monetdb_oledb..."
    Get-Process dllhost -ErrorAction SilentlyContinue | ForEach-Object {
        $process = $_
        try {
            $loaded = $process.Modules | Where-Object { $_.FileName -like "*monetdb_oledb*" }
            if ($loaded) {
                Write-Step "Stop DllHost PID=$($process.Id)"
                Stop-Process -Id $process.Id -Force
            }
        }
        catch {
            Write-Step "Ignoro DllHost PID=$($process.Id): $($_.Exception.Message)"
        }
    }

    Write-Step "Copio DLL nuova su build ufficiale: $BuildDll"
    Copy-Item -LiteralPath $SourceDll -Destination $BuildDll -Force

    Write-Step "Copio DLL nuova su installazione: $InstallDll"
    Copy-Item -LiteralPath $SourceDll -Destination $InstallDll -Force
    Copy-Item -LiteralPath $SourceDll -Destination $InstallSideBySideDll -Force

    Write-Step "Registro COM dal path installato: $InstallDll"
    $regsvr = Join-Path $env:WINDIR "System32\regsvr32.exe"
    $p = Start-Process -FilePath $regsvr -ArgumentList "/s", $InstallDll -Wait -PassThru
    if ($p.ExitCode -ne 0) {
        throw "regsvr32 fallito con exit code $($p.ExitCode)"
    }

    Write-Step "Verifico InprocServer32"
    & reg.exe query "HKCR\CLSID\{A3F2D8E1-7B4C-4E9A-B5D6-1C8F3E2A9D07}\InprocServer32" /ve | ForEach-Object {
        Write-Step $_
    }

    Write-Step "Deploy completato"
}
finally {
    foreach ($name in $stoppedServices) {
        try {
            Write-Step "Start servizio $name"
            Start-Service -Name $name
            (Get-Service -Name $name).WaitForStatus("Running", "00:01:00")
        }
        catch {
            Write-Step "ERRORE start servizio $name: $($_.Exception.Message)"
            throw
        }
    }
}
