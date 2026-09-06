@echo off
setlocal

set WRAPPER=%~dp0SteamLinkAltTabWrapper.exe
set IFEO_KEY="HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\SteamLink.exe"

echo SteamLink Alt+Tab Capturer - IFEO Setup
echo ========================================
echo.
echo Wrapper: %WRAPPER%
echo.

if not exist "%WRAPPER%" (
    echo ERROR: Wrapper not found at %WRAPPER%
    echo Build the solution first.
    pause
    exit /b 1
)

echo This will register the wrapper as an IFEO debugger for SteamLink.exe.
echo When SteamLink.exe launches, Windows will run the wrapper instead,
echo which starts SteamLink.exe suspended, injects the DLL, then resumes it.
echo.
echo Requires Administrator privileges.
echo.

net session >nul 2>&1
if %errorLevel% neq 0 (
    echo ERROR: Run as Administrator.
    pause
    exit /b 1
)

reg add %IFEO_KEY% /v Debugger /t REG_SZ /d "\"%WRAPPER%\"" /f
if %errorLevel% equ 0 (
    echo.
    echo Done! IFEO debugger registered.
    echo SteamLink.exe will now auto-load the capturer DLL.
    echo.
    echo To remove: reg delete %IFEO_KEY% /v Debugger /f
) else (
    echo.
    echo ERROR: Failed to write registry key.
)

pause
