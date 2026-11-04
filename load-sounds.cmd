@echo off
rem Double-click with the CYD SD card in this PC: copies the spoken cues
rem onto it as \sounds and checks each copy. Other files on the card are left alone.
rem   load-sounds.cmd            find the card (asks if there are several)
rem   load-sounds.cmd -Drive E:  use drive E:
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0firmware\tools\load-sounds.ps1" %*
echo.
pause
