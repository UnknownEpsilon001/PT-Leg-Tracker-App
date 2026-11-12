@echo off
rem Double-click to update main from GitHub, set up Python, test, and start the server.
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "scripts\update-server.ps1" %*
if errorlevel 1 pause
