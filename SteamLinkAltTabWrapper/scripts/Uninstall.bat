@echo off
setlocal

set IFEO_KEY="HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\SteamLink.exe"

echo Removing IFEO debugger for SteamLink.exe...
echo.

net session >nul 2>&1
if %errorLevel% neq 0 (
    echo ERROR: Run as Administrator.
    pause
    exit /b 1
)

reg delete %IFEO_KEY% /v Debugger /f
if %errorLevel% equ 0 (
    echo Done. IFEO debugger removed.
) else (
    echo Nothing to remove (key may not exist).
)

pause
