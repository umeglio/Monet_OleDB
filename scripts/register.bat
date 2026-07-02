@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "PROVIDER_NAME=MonetDB.OleDb"
set "PROVIDER_CLSID={A3F2D8E1-7B4C-4E9A-B5D6-1C8F3E2A9D07}"
set "SCRIPT_DIR=%~dp0"
set "INSTALL_DIR=%~dp0"
if "%INSTALL_DIR:~-1%"=="\" set "INSTALL_DIR=%INSTALL_DIR:~0,-1%"

if /I "%~1"=="/check" goto :check
if /I "%~1"=="/u" goto :unregister
if /I "%~1"=="/unregister" goto :unregister

echo [INFO] Configuro le chiavi provider per SQL Server...
call :set_provider_key "HKLM\SOFTWARE\Microsoft\MSSQLServer\Providers\%PROVIDER_NAME%"

for /f "skip=2 tokens=1,2,*" %%A in ('reg query "HKLM\SOFTWARE\Microsoft\Microsoft SQL Server\Instance Names\SQL" 2^>nul') do (
    if not "%%C"=="" (
        echo [INFO] Istanza SQL trovata: %%A ^(%%C^)
        call :set_provider_key "HKLM\SOFTWARE\Microsoft\Microsoft SQL Server\%%C\Providers\%PROVIDER_NAME%"
    )
)

echo [INFO] Concedo Modify a NT SERVICE\ALL SERVICES sulla directory di installazione...
icacls "%INSTALL_DIR%" /grant *S-1-5-80-0:(OI)(CI)M >nul 2>nul

echo [OK] Registrazione SQL Server completata.
exit /b 0

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

:unregister
echo [INFO] Rimuovo le chiavi provider per SQL Server...
call :delete_provider_key "HKLM\SOFTWARE\Microsoft\MSSQLServer\Providers\%PROVIDER_NAME%"

for /f "skip=2 tokens=1,2,*" %%A in ('reg query "HKLM\SOFTWARE\Microsoft\Microsoft SQL Server\Instance Names\SQL" 2^>nul') do (
    if not "%%C"=="" (
        echo [INFO] Istanza SQL trovata: %%A ^(%%C^)
        call :delete_provider_key "HKLM\SOFTWARE\Microsoft\Microsoft SQL Server\%%C\Providers\%PROVIDER_NAME%"
    )
)

echo [OK] Rimozione chiavi SQL Server completata.
exit /b 0

:check
echo [CHECK] CLSID provider...
reg query "HKCR\CLSID\%PROVIDER_CLSID%" /s
echo.
echo [CHECK] ProgID provider...
reg query "HKCR\%PROVIDER_NAME%" /s
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
echo [CHECK] ACL directory installazione...
icacls "%INSTALL_DIR%"
exit /b 0
