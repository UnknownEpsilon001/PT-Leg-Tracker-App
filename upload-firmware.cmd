@echo off
rem Double-click to upload the current code to the machine.
rem Compiles only the boards whose code changed, finds the controller and the
rem CYD by their USB chips, then uploads both. Pull the UART jumpers first.
rem   upload-firmware.cmd                 both boards
rem   upload-firmware.cmd -Board display  just the CYD
cd /d "%~dp0"
rem No arguments means both boards; any argument is passed through as-is.
set "BOARD=-Board both"
if not "%~1"=="" set "BOARD="
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0firmware\tools\flash.ps1" -Action auto %BOARD% %*
echo.
pause
