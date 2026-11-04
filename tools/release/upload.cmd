@echo off
rem Double-click to flash every plugged-in board. Arguments pass through to upload.ps1.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0upload.ps1" %*
pause
