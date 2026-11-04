@echo off
rem Double-click with the CYD SD card in this PC: copies sd-card\sounds onto it.
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0load-sounds.ps1" %*
echo.
pause
