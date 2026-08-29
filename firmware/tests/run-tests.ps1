# [TH] คอมไพล์และรันชุดทดสอบที่รันบนคอมพิวเตอร์ได้ ไม่ต้องใช้บอร์ด
#
# Builds and runs the host-side firmware tests.
#
# Only hardware-free code is testable this way. wav.cpp parses bytes and knows
# nothing about the DAC, the card or LVGL, so it compiles for a PC. Everything
# else in pt-leg-display needs the board and is covered by the bench checklist
# in firmware/README.md instead — do not pretend otherwise by adding stubs.
# [TH] ทดสอบแบบนี้ได้เฉพาะโค้ดที่ไม่ยุ่งกับฮาร์ดแวร์ ไฟล์ wav.cpp อ่านแต่ไบต์
#      ไม่รู้จัก DAC การ์ด หรือ LVGL จึงคอมไพล์บนคอมพิวเตอร์ได้
#      ส่วนที่เหลือต้องใช้บอร์ดจริง และใช้รายการตรวจบนโต๊ะใน firmware/README.md แทน
#      อย่าสร้างตัวจำลองขึ้นมาเพื่อให้ดูเหมือนทดสอบได้
#
# Run from the repository root, so the optional scratch/sounds/start.wav check
# resolves:
#
#   .\firmware\tests\run-tests.ps1
param([string]$Compiler = "g++")
$ErrorActionPreference = "Stop"

$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$outDir = Join-Path $here "..\.build\test-bin"
New-Item -ItemType Directory -Force $outDir | Out-Null
$exe = Join-Path $outDir "wav_test.exe"

$sources = @(
    (Join-Path $here "wav_test.cpp"),
    (Join-Path $here "..\pt-leg-display\wav.cpp")
)

# -Werror because the parser's whole job is to be strict about bytes. A warning
# here is the compiler noticing something about that logic, which is exactly the
# kind of thing this test binary exists to catch.
# [TH] ใช้ -Werror เพราะหน้าที่ของตัวแยกคือเข้มงวดกับข้อมูลไบต์
#      คำเตือนตรงนี้คือคอมไพเลอร์สังเกตเห็นบางอย่างในตรรกะนั้น
#      ซึ่งเป็นสิ่งที่ไฟล์ทดสอบนี้มีไว้เพื่อจับพอดี
$compilerArgs = @("-std=c++17", "-Wall", "-Wextra", "-Werror", "-O1") + $sources + @("-o", $exe)

& $Compiler $compilerArgs
if ($LASTEXITCODE -ne 0) { throw "compile failed" }

& $exe
if ($LASTEXITCODE -ne 0) { throw "tests failed" }
Write-Host "tests passed" -ForegroundColor Green
