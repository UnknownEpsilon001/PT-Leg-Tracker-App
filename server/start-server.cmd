@echo off
rem Double-click to run the server. First run creates server\.venv and installs
rem the requirements; later runs only start it. Close the window to stop.
rem [TH] ดับเบิลคลิกเพื่อเปิดเซิร์ฟเวอร์ ครั้งแรกจะสร้าง .venv และติดตั้งแพ็กเกจให้ ปิดหน้าต่างเพื่อหยุด
cd /d "%~dp0"
if not exist ".venv\Scripts\python.exe" (
  echo First run: setting up Python environment...
  python -m venv .venv || goto :nopython
  ".venv\Scripts\python.exe" -m pip install -r requirements.txt || goto :failed
)
".venv\Scripts\python.exe" -m pip install -q -r requirements.txt
powershell -NoProfile -ExecutionPolicy Bypass -File "scripts\dev.ps1" -NoMock
pause
exit /b

:nopython
echo Python was not found. Install Python 3.13 from python.org and tick "Add python.exe to PATH".
pause
exit /b 1

:failed
echo Installing the requirements failed. Check the internet connection and run this again.
pause
exit /b 1
