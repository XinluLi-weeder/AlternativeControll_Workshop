@echo off
setlocal
cd /d "%~dp0"

where python >nul 2>nul
if errorlevel 1 (
  echo Python 3 was not found on PATH. Install Python, then run this file again.
  pause
  exit /b 1
)

echo Starting OSC bridge. Enter Play in the Hallyway level first.
echo Press Ctrl+C to stop.
echo.
python -u Tools\osc_bridge.py %*
echo.
echo Bridge stopped. Read the messages above if this was unexpected.
pause
