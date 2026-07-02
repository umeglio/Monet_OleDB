@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "PROVIDER_NAME=MonetDB.OleDb"
set "PROVIDER_NAME_VER=MonetDB.OleDb.1"
set "PROVIDER_FRIENDLY_NAME=MonetDB OLE DB Provider for SQL Server"
set "PROVIDER_CLSID={A3F2D8E1-7B4C-4E9A-B5D6-1C8F3E2A9D07}"
set "MSDAINITIALIZE_APPID={2206CDB0-19C1-11D1-89E0-00C04FD7A829}"
set "REGSVR32_EXE=%SystemRoot%\System32\regsvr32.exe"
if exist "%SystemRoot%\Sysnative\regsvr32.exe" set "REGSVR32_EXE=%SystemRoot%\Sysnative\regsvr32.exe"
set "SCRIPT_DIR=%~dp0"
if "%SCRIPT_DIR:~-1%"=="\" set "SCRIPT_DIR=%SCRIPT_DIR:~0,-1%"

set "LATEST_DLL="
set "REGISTERED_DLL="
set "STOPPED_SQL_SERVICES="
set "SERVICE_ERRORS=0"

if /I "%~1"=="/check" goto dispatch_check
if /I "%~1"=="/u" goto dispatch_unregister
if /I "%~1"=="/unregister" goto dispatch_unregister

goto main

:dispatch_check
call :check
goto :eof

:dispatch_unregister
call :unregister
goto :eof

:main

call :require_admin
if errorlevel 1 exit /b 1

call :find_latest_dll
if not defined LATEST_DLL (
    echo [ERRORE] Nessuna DLL monetdb_oledb*.dll trovata in "%SCRIPT_DIR%".
    exit /b 1
)

call :read_registered_dll

echo [INFO] Cartella batch: "%SCRIPT_DIR%"
echo [INFO] DLL selezionata: "%LATEST_DLL%"
echo [INFO] Regsvr32 usato: "%REGSVR32_EXE%"
if defined REGISTERED_DLL (
    echo [INFO] DLL attualmente registrata: "%REGISTERED_DLL%"
) else (
    echo [INFO] Nessuna DLL provider registrata al momento.
)

echo [INFO] Arresto i servizi SQL che possono avere caricato il provider...
call :stop_sql_services

echo [INFO] Chiudo eventuali surrogate COM DllHost.exe del provider...
call :stop_provider_surrogates

echo [INFO] Rimuovo le chiavi provider per SQL Server...
call :delete_all_provider_keys

if defined REGISTERED_DLL (
    if exist "%REGISTERED_DLL%" (
        echo [INFO] Sregistro la DLL attualmente registrata...
        call :unregister_dll "%REGISTERED_DLL%"
    ) else (
        echo [WARN] La DLL registrata non esiste piu': "%REGISTERED_DLL%"
    )
)

if /I not "%REGISTERED_DLL%"=="%LATEST_DLL%" (
    echo [INFO] Sregistro in sicurezza anche la DLL selezionata...
    call :unregister_dll_quiet "%LATEST_DLL%"
)

echo [INFO] Rimuovo tutti i riferimenti COM/OLE DB di MonetDB...
call :delete_all_com_keys

echo [INFO] Registro la DLL selezionata...
call :register_dll "%LATEST_DLL%"
if errorlevel 1 (
    call :start_stopped_services
    exit /b 1
)

echo [INFO] Riparo le chiavi COM/OLE DB del provider...
call :repair_com_registration "%LATEST_DLL%"

echo [INFO] Configuro le chiavi provider per SQL Server...
call :set_all_provider_keys

echo [INFO] Concedo Modify a NT SERVICE\ALL SERVICES sulla directory...
icacls "%SCRIPT_DIR%" /grant *S-1-5-80-0:(OI)(CI)M >nul 2>nul

echo [INFO] Riavvio i servizi SQL fermati dallo script...
call :start_stopped_services
if not "%SERVICE_ERRORS%"=="0" exit /b 1

echo [OK] Provider registrato correttamente dalla cartella "%SCRIPT_DIR%".
exit /b 0

:check
call :find_latest_dll
call :read_registered_dll

echo [CHECK] Cartella batch: "%SCRIPT_DIR%"
echo [CHECK] Regsvr32 usato: "%REGSVR32_EXE%"
if defined LATEST_DLL (
    echo [CHECK] Ultima DLL trovata: "%LATEST_DLL%"
) else (
    echo [CHECK] Ultima DLL trovata: nessuna
)
if defined REGISTERED_DLL (
    echo [CHECK] DLL registrata: "%REGISTERED_DLL%"
    echo.
    reg query "HKCR\CLSID\%PROVIDER_CLSID%\InprocServer32" /s
    echo.
) else (
    echo [CHECK] DLL registrata: nessuna
    echo.
)

echo [CHECK] Chiavi COM/OLE DB del provider...
reg query "HKCR\CLSID\%PROVIDER_CLSID%" /s
echo.
reg query "HKCR\%PROVIDER_NAME%" /s
echo.
reg query "HKCR\%PROVIDER_NAME_VER%" /s
echo.

echo [CHECK] Provider legacy path...
reg query "HKLM\SOFTWARE\Microsoft\MSSQLServer\Providers\%PROVIDER_NAME%" /s
echo.
echo [CHECK] Provider su istanze SQL enumerate...
for /f "skip=2 tokens=1,2,*" %%A in ('reg query "HKLM\SOFTWARE\Microsoft\Microsoft SQL Server\Instance Names\SQL" 2^>nul') do (
    if not "%%C"=="" (
        echo ----- %%A / %%C -----
        reg query "HKLM\SOFTWARE\Microsoft\Microsoft SQL Server\%%C\Providers\%PROVIDER_NAME%" /s
        echo.
    )
)
exit /b 0

:unregister
call :require_admin
if errorlevel 1 exit /b 1
call :find_latest_dll
call :read_registered_dll

echo [INFO] Arresto i servizi SQL che possono avere caricato il provider...
call :stop_sql_services

echo [INFO] Chiudo eventuali surrogate COM DllHost.exe del provider...
call :stop_provider_surrogates

echo [INFO] Rimuovo le chiavi provider per SQL Server...
call :delete_all_provider_keys

if defined REGISTERED_DLL (
    if exist "%REGISTERED_DLL%" (
        echo [INFO] Sregistro la DLL attualmente registrata...
        call :unregister_dll "%REGISTERED_DLL%"
    ) else (
        echo [WARN] La DLL registrata non esiste piu': "%REGISTERED_DLL%"
    )
)

if defined LATEST_DLL if /I not "%REGISTERED_DLL%"=="%LATEST_DLL%" (
    echo [INFO] Sregistro in sicurezza anche la DLL locale...
    call :unregister_dll_quiet "%LATEST_DLL%"
)

echo [INFO] Rimuovo tutti i riferimenti COM/OLE DB di MonetDB...
call :delete_all_com_keys

echo [INFO] Riavvio i servizi SQL fermati dallo script...
call :start_stopped_services
if not "%SERVICE_ERRORS%"=="0" exit /b 1

echo [OK] Provider sregistrato e chiavi SQL Server rimosse.
exit /b 0

:find_latest_dll
set "LATEST_DLL="
for /f "delims=" %%F in ('dir /b /a:-d /o-d "%SCRIPT_DIR%\monetdb_oledb*.dll" 2^>nul') do (
    if not defined LATEST_DLL set "LATEST_DLL=%SCRIPT_DIR%\%%F"
)
goto :eof

:read_registered_dll
set "REGISTERED_DLL="
for /f "skip=2 tokens=1,2,*" %%A in ('reg query "HKCR\CLSID\%PROVIDER_CLSID%\InprocServer32" /ve 2^>nul') do (
    if not "%%C"=="" (
        set "REGISTERED_DLL=%%C"
        goto :eof
    )
)
goto :eof

:register_dll
set "TARGET_DLL=%~1"
if "%TARGET_DLL%"=="" exit /b 1
"%REGSVR32_EXE%" /s "%TARGET_DLL%"
if errorlevel 1 (
    echo [ERRORE] regsvr32 non e' riuscito a registrare "%TARGET_DLL%".
    echo [ERRORE] Verifica che la DLL sia x64, che tutte le dipendenze siano presenti e che il prompt sia elevato.
    echo [ERRORE] Per una diagnosi esplicita prova manualmente:
    echo          "%REGSVR32_EXE%" "%TARGET_DLL%"
    exit /b 1
)
exit /b 0

:repair_com_registration
set "TARGET_DLL=%~1"
if "%TARGET_DLL%"=="" exit /b 1
reg add "HKCR\CLSID\%PROVIDER_CLSID%" /ve /t REG_SZ /d "%PROVIDER_FRIENDLY_NAME%" /f >nul
reg add "HKCR\CLSID\%PROVIDER_CLSID%" /v OLEDB_SERVICES /t REG_DWORD /d 0 /f >nul
reg add "HKCR\CLSID\%PROVIDER_CLSID%\InprocServer32" /ve /t REG_SZ /d "%TARGET_DLL%" /f >nul
reg add "HKCR\CLSID\%PROVIDER_CLSID%\InprocServer32" /v ThreadingModel /t REG_SZ /d Both /f >nul
reg add "HKCR\CLSID\%PROVIDER_CLSID%\OLE DB Provider" /ve /t REG_SZ /d "%PROVIDER_FRIENDLY_NAME%" /f >nul
reg add "HKCR\CLSID\%PROVIDER_CLSID%\ProgID" /ve /t REG_SZ /d "%PROVIDER_NAME_VER%" /f >nul
reg add "HKCR\CLSID\%PROVIDER_CLSID%\VersionIndependentProgID" /ve /t REG_SZ /d "%PROVIDER_NAME%" /f >nul
reg add "HKCR\%PROVIDER_NAME%" /ve /t REG_SZ /d "%PROVIDER_FRIENDLY_NAME%" /f >nul
reg add "HKCR\%PROVIDER_NAME%\CLSID" /ve /t REG_SZ /d "%PROVIDER_CLSID%" /f >nul
reg add "HKCR\%PROVIDER_NAME%\CurVer" /ve /t REG_SZ /d "%PROVIDER_NAME_VER%" /f >nul
reg add "HKCR\%PROVIDER_NAME%\OLE DB Provider" /ve /t REG_SZ /d "%PROVIDER_FRIENDLY_NAME%" /f >nul
reg add "HKCR\%PROVIDER_NAME_VER%" /ve /t REG_SZ /d "%PROVIDER_FRIENDLY_NAME%" /f >nul
reg add "HKCR\%PROVIDER_NAME_VER%\CLSID" /ve /t REG_SZ /d "%PROVIDER_CLSID%" /f >nul
reg add "HKCR\%PROVIDER_NAME_VER%\OLE DB Provider" /ve /t REG_SZ /d "%PROVIDER_FRIENDLY_NAME%" /f >nul
exit /b 0

:unregister_dll
set "TARGET_DLL=%~1"
if "%TARGET_DLL%"=="" exit /b 0
"%REGSVR32_EXE%" /u /s "%TARGET_DLL%"
if errorlevel 1 (
    echo [WARN] regsvr32 /u non e' riuscito su "%TARGET_DLL%".
)
exit /b 0

:unregister_dll_quiet
set "TARGET_DLL=%~1"
if "%TARGET_DLL%"=="" exit /b 0
"%REGSVR32_EXE%" /u /s "%TARGET_DLL%" >nul 2>nul
exit /b 0

:build_service_name
set "INSTANCE_NAME=%~1"
set "SERVICE_NAME="
if /I "%INSTANCE_NAME%"=="MSSQLSERVER" (
    set "SERVICE_NAME=MSSQLSERVER"
) else if not "%INSTANCE_NAME%"=="" (
    set "SERVICE_NAME=MSSQL$%INSTANCE_NAME%"
)
goto :eof

:wait_for_service_state
set "WAIT_SERVICE=%~1"
set "WAIT_TARGET=%~2"
set /a WAIT_COUNT=0
:wait_for_service_state_loop
set /a WAIT_COUNT+=1
sc query "%WAIT_SERVICE%" | findstr /I /C:"%WAIT_TARGET%" >nul 2>nul
if not errorlevel 1 goto :eof
if %WAIT_COUNT% GEQ 30 goto :eof
ping -n 2 127.0.0.1 >nul
goto :wait_for_service_state_loop

:require_admin
net session >nul 2>nul
if errorlevel 1 (
    echo [ERRORE] Eseguire questo script da Prompt dei comandi avviato come Amministratore.
    exit /b 1
)
goto :eof

:stop_sql_services
set "STOPPED_SQL_SERVICES="
set "SERVICE_ERRORS=0"
for /f "skip=2 tokens=1,2,*" %%A in ('reg query "HKLM\SOFTWARE\Microsoft\Microsoft SQL Server\Instance Names\SQL" 2^>nul') do (
    if not "%%C"=="" (
        call :build_service_name "%%A"
        if defined SERVICE_NAME (
            sc query "!SERVICE_NAME!" | findstr /I /C:"RUNNING" >nul 2>nul
            if not errorlevel 1 (
                call :stop_one_service "!SERVICE_NAME!"
            )
        )
    )
)
goto :eof

:start_stopped_services
if not defined STOPPED_SQL_SERVICES goto :eof
for %%S in (%STOPPED_SQL_SERVICES:;= %) do (
    if not "%%~S"=="" (
        call :start_one_service "%%~S"
    )
)
goto :eof

:stop_one_service
set "CURRENT_SERVICE=%~1"
if "%CURRENT_SERVICE%"=="" goto :eof
echo [INFO] Arresto servizio %CURRENT_SERVICE%...
set "STOP_RESULT="
for /f "usebackq delims=" %%R in (`powershell -NoProfile -ExecutionPolicy Bypass -Command ^
    "& {" ^
    "  $name = '%CURRENT_SERVICE%';" ^
    "  $svc = Get-Service -Name $name -ErrorAction Stop;" ^
    "  $runningDeps = @($svc.DependentServices | Where-Object { $_.Status -eq 'Running' } | Select-Object -ExpandProperty Name);" ^
    "  if ($svc.Status -eq 'Running') {" ^
    "    Stop-Service -Name $name -Force -ErrorAction Stop;" ^
    "    (Get-Service -Name $name).WaitForStatus([System.ServiceProcess.ServiceControllerStatus]::Stopped, [TimeSpan]::FromSeconds(30));" ^
    "  }" ^
    "  $result = @($name) + $runningDeps;" ^
    "  [Console]::Out.Write(($result -join ';'));" ^
    "}"`) do (
    set "STOP_RESULT=%%R"
)
if not defined STOP_RESULT (
    echo [WARN] Il servizio %CURRENT_SERVICE% non risulta fermato o la raccolta dipendenze e' fallita.
    set "SERVICE_ERRORS=1"
) else (
    for %%S in (%STOP_RESULT:;= %) do (
        if not "%%~S"=="" (
            echo [INFO] Fermato: %%~S
            if "!STOPPED_SQL_SERVICES!"=="" (
                set "STOPPED_SQL_SERVICES=%%~S"
            ) else (
                set "STOPPED_SQL_SERVICES=!STOPPED_SQL_SERVICES!;%%~S"
            )
        )
    )
)
goto :eof

:start_one_service
set "CURRENT_SERVICE=%~1"
if "%CURRENT_SERVICE%"=="" goto :eof
echo [INFO] Avvio servizio %CURRENT_SERVICE%...
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
    "$name = '%CURRENT_SERVICE%';" ^
    "Start-Service -Name $name -ErrorAction Stop;" ^
    "(Get-Service -Name $name).WaitForStatus([System.ServiceProcess.ServiceControllerStatus]::Running,[TimeSpan]::FromSeconds(30))"
if errorlevel 1 (
    echo [ERRORE] Il servizio %CURRENT_SERVICE% non e' partito.
    set "SERVICE_ERRORS=1"
)
goto :eof

:stop_provider_surrogates
set "TARGET_FOR_UNLOCK=%LATEST_DLL%"
if not defined TARGET_FOR_UNLOCK set "TARGET_FOR_UNLOCK=%REGISTERED_DLL%"
if not defined TARGET_FOR_UNLOCK goto :eof

powershell -NoProfile -ExecutionPolicy Bypass -Command ^
    "$dll = [IO.Path]::GetFileName('%TARGET_FOR_UNLOCK%');" ^
    "$locks = tasklist /m $dll /fo csv /nh 2>$null;" ^
    "foreach($line in $locks){" ^
        "if([string]::IsNullOrWhiteSpace($line) -or $line.StartsWith('INFO:')){ continue };" ^
        "$entry = $line | ConvertFrom-Csv -Header ImageName,PID,SessionName,SessionNumber,MemUsage,Modules;" ^
        "$proc = Get-CimInstance Win32_Process -Filter ('ProcessId = ' + $entry.PID) -ErrorAction SilentlyContinue;" ^
        "if($proc -and $proc.Name -ieq 'dllhost.exe'){ Write-Host ('[INFO] Termino DllHost PID=' + $proc.ProcessId); Stop-Process -Id $proc.ProcessId -Force -ErrorAction SilentlyContinue }" ^
    "};" ^
    "Get-CimInstance Win32_Process -Filter ""Name = 'dllhost.exe'"" -ErrorAction SilentlyContinue | Where-Object { $_.CommandLine -match '%MSDAINITIALIZE_APPID%' } | ForEach-Object { Write-Host ('[INFO] Termino DllHost/MSDAINITIALIZE PID=' + $_.ProcessId); Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }" >nul 2>nul
goto :eof

:set_all_provider_keys
call :set_provider_key "HKLM\SOFTWARE\Microsoft\MSSQLServer\Providers\%PROVIDER_NAME%"
for /f "skip=2 tokens=1,2,*" %%A in ('reg query "HKLM\SOFTWARE\Microsoft\Microsoft SQL Server\Instance Names\SQL" 2^>nul') do (
    if not "%%C"=="" (
        echo [INFO] Istanza SQL trovata: %%A ^(%%C^)
        call :set_provider_key "HKLM\SOFTWARE\Microsoft\Microsoft SQL Server\%%C\Providers\%PROVIDER_NAME%"
    )
)
goto :eof

:delete_all_provider_keys
call :delete_provider_key "HKLM\SOFTWARE\Microsoft\MSSQLServer\Providers\%PROVIDER_NAME%"
for /f "skip=2 tokens=1,2,*" %%A in ('reg query "HKLM\SOFTWARE\Microsoft\Microsoft SQL Server\Instance Names\SQL" 2^>nul') do (
    if not "%%C"=="" (
        echo [INFO] Istanza SQL trovata: %%A ^(%%C^)
        call :delete_provider_key "HKLM\SOFTWARE\Microsoft\Microsoft SQL Server\%%C\Providers\%PROVIDER_NAME%"
    )
)
goto :eof

:delete_all_com_keys
call :delete_registry_key "HKCR\CLSID\%PROVIDER_CLSID%"
call :delete_registry_key "HKCR\%PROVIDER_NAME%"
call :delete_registry_key "HKCR\%PROVIDER_NAME_VER%"
call :delete_registry_key "HKLM\SOFTWARE\Classes\CLSID\%PROVIDER_CLSID%"
call :delete_registry_key "HKLM\SOFTWARE\Classes\%PROVIDER_NAME%"
call :delete_registry_key "HKLM\SOFTWARE\Classes\%PROVIDER_NAME_VER%"
call :delete_registry_key "HKLM\SOFTWARE\Classes\WOW6432Node\CLSID\%PROVIDER_CLSID%"
call :delete_registry_key "HKLM\SOFTWARE\Classes\WOW6432Node\%PROVIDER_NAME%"
call :delete_registry_key "HKLM\SOFTWARE\Classes\WOW6432Node\%PROVIDER_NAME_VER%"
goto :eof

:set_provider_key
set "KEY=%~1"
reg add "%KEY%" /f >nul
reg add "%KEY%" /v AllowInProcess /t REG_DWORD /d 1 /f >nul
reg add "%KEY%" /v DynamicParameters /t REG_DWORD /d 1 /f >nul
reg add "%KEY%" /v NestedQueries /t REG_DWORD /d 1 /f >nul
reg add "%KEY%" /v LevelZeroOnly /t REG_DWORD /d 0 /f >nul
goto :eof

:delete_provider_key
set "KEY=%~1"
reg delete "%KEY%" /f >nul 2>nul
goto :eof

:delete_registry_key
set "KEY=%~1"
reg delete "%KEY%" /f >nul 2>nul
goto :eof
