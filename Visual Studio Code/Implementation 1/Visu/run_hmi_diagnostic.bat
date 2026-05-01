@echo off
setlocal
cd /d "%~dp0"
echo Starting ESP Mesh HMI diagnostic run...
echo.
if not exist ".venv\Scripts\python.exe" (
  echo Missing .venv\Scripts\python.exe
  echo Run install_visu.bat first.
  pause
  exit /b 1
)
".venv\Scripts\python.exe" web_hmi.py --host 127.0.0.1 --port 8080
echo.
echo HMI stopped or failed. Check the messages above.
pause
