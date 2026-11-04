@echo off
rem Double-click to upload the current code to the machine.
rem Compiles only the boards whose code changed, finds the controller and the
rem CYD by their USB chips, then uploads both. Pull the UART jumpers first.
rem   upload-firmware.cmd                 both boards
rem   upload-firmware.cmd -Board display  just the CYD
cd /d "%~dp0"
set "ARGS=%*"
if "%ARGS%"=="" set "ARGS=-Board both"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0firmware\tools\flash.ps1" -Action auto %ARGS%
echo.
pause
