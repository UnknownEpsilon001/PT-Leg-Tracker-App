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

# The buffer cap is stated in two places that must agree: the firmware rejects a
# clip larger than AUDIO_BUF_BYTES, and the tool refuses to write one larger than
# -MaxBytes. Comments in both files asked for that and nothing enforced it, so a
# change to one would only surface on the bench as a file the tool blessed and
# the device rejected.
# [TH] ค่าเพดานขนาดบัฟเฟอร์ถูกเขียนไว้สองที่และต้องตรงกัน
#      เฟิร์มแวร์ปฏิเสธคลิปที่ใหญ่กว่า AUDIO_BUF_BYTES ส่วนเครื่องมือไม่ยอมเขียนไฟล์ที่ใหญ่กว่า -MaxBytes
#      เดิมมีแค่คอมเมนต์ขอไว้ในสองไฟล์โดยไม่มีอะไรบังคับ
#      การแก้ค่าฝั่งเดียวจึงไปโผล่ตอนทดสอบบนโต๊ะ เป็นไฟล์ที่เครื่องมือบอกว่าผ่านแต่เครื่องปฏิเสธ
$audioCpp = Get-Content (Join-Path $here "..\pt-leg-display\audio.cpp") -Raw
$toolPs1 = Get-Content (Join-Path $here "..\tools\make-sounds.ps1") -Raw
if ($audioCpp -notmatch 'AUDIO_BUF_BYTES\s*=\s*(\d+)u?\s*\*\s*(\d+)u?') {
    throw "could not find AUDIO_BUF_BYTES in audio.cpp"
}
$fw = [int]$Matches[1] * [int]$Matches[2]
if ($toolPs1 -notmatch '\$MaxBytes\s*=\s*(\d+)') {
    throw "could not find the -MaxBytes default in make-sounds.ps1"
}
$tool = [int]$Matches[1]
if ($fw -ne $tool) {
    throw "buffer cap mismatch: audio.cpp AUDIO_BUF_BYTES=$fw but make-sounds.ps1 -MaxBytes=$tool"
}
Write-Host "buffer cap agrees: $fw bytes" -ForegroundColor Green

# The repo root is passed so the generated-file check resolves independently of
# where this script was invoked from.
# [TH] ส่งรากของที่เก็บโค้ดเข้าไปด้วย เพื่อให้การตรวจไฟล์ที่สร้างขึ้นไม่ขึ้นกับตำแหน่งที่เรียกสคริปต์นี้
$repoRoot = (Resolve-Path (Join-Path $here "..\..")).Path
& $exe $repoRoot
if ($LASTEXITCODE -ne 0) { throw "tests failed" }
Write-Host "tests passed" -ForegroundColor Green
