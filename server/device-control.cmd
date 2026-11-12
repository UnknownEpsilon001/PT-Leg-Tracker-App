@echo off
rem Double-click this file to open the interactive device control test console.
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "scripts\device-control.ps1" %*
pause
