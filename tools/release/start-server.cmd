@echo off
rem Starts the Med-Flex Auto Smart server on port 8000, reachable from the LAN.
rem The first run creates .venv and installs requirements.txt (needs Python 3 and internet).
cd /d "%~dp0"
if not exist .venv\Scripts\python.exe (
  py -3 -m venv .venv 2>nul || python -m venv .venv || goto :nopython
  .venv\Scripts\python -m pip install -r requirements.txt || goto :pipfail
)
.venv\Scripts\python -m uvicorn app.main:app --host 0.0.0.0 --port 8000
pause
exit /b

:nopython
echo Python 3 was not found. Install it from python.org, then run this again.
pause
exit /b 1

:pipfail
echo Installing the requirements failed. Check the internet connection and run this again.
rmdir /s /q .venv
pause
exit /b 1
