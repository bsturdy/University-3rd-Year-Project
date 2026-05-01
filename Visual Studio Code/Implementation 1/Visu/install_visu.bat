@echo off
setlocal
cd /d "%~dp0"
if "%~1"=="" (
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0install_visu.ps1" -AddFirewallRule -AddStartup
) else (
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0install_visu.ps1" %*
)
if errorlevel 1 (
  echo.
  echo Installation failed. Check the messages above.
  pause
  exit /b %errorlevel%
)
echo.
echo Installation complete.
pause
endlocal
