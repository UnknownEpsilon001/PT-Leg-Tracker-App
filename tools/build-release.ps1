# [TH] คอมไพล์ทุกส่วนแล้วรวมเป็นไฟล์รีลีสเดียว: แอป APK, เฟิร์มแวร์สองบอร์ด, เซิร์ฟเวอร์, ไฟล์การ์ด SD และตัวอัปโหลด
#
# Compiles everything and packs one release zip: the APK, both firmware images,
# the server, the SD card files and the self-contained uploader.
#
#   .\tools\build-release.ps1                    # -> release\SmartOAKnee-<date>-<commit>.zip
#   .\tools\build-release.ps1 -Version 1.0.0
#
# Compiling goes through the existing tools, so their guards still run:
# flash.ps1 refuses a bench switch left on, app-build.ps1 keeps the last good APK
# if Gradle fails.
# [TH] การคอมไพล์ผ่านเครื่องมือเดิม ตัวตรวจของมันจึงยังทำงานครบ
#      flash.ps1 ไม่ยอมคอมไพล์ถ้ายังเปิดสวิตช์โหมดทดสอบไว้ app-build.ps1 เก็บ APK ที่ดีล่าสุดไว้ถ้า Gradle ล้มเหลว
# LEO WAS HERE ;3
param([string]$Version = "")
$ErrorActionPreference = "Stop"

$root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$commit = (git -C $root rev-parse --short HEAD).Trim()
if (-not $Version) { $Version = "$(Get-Date -Format yyyy.MM.dd)-$commit" }
$dirty = [bool](git -C $root status --porcelain --untracked-files=no)
$name = "SmartOAKnee-$Version"
$stage = Join-Path $root "release\$name"
$zip = "$stage.zip"

function Invoke-Script([string]$script, [string[]]$argList) {
    Write-Host "== $(Split-Path -Leaf $script) $($argList -join ' ') ==" -ForegroundColor Cyan
    & powershell -NoProfile -ExecutionPolicy Bypass -File $script @argList
    if ($LASTEXITCODE -ne 0) { throw "$script failed (exit $LASTEXITCODE)" }
}

function Copy-Into([string]$from, [string]$to) {
    New-Item -ItemType Directory -Force (Split-Path -Parent $to) | Out-Null
    Copy-Item $from $to -Recurse -Force
}

Invoke-Script (Join-Path $root "PT-Leg-Tracker\tools\app-build.ps1") @("-Action", "build")
Invoke-Script (Join-Path $root "firmware\tools\flash.ps1") @("-Action", "compile", "-Board", "both")

if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Force $stage | Out-Null

Copy-Into (Join-Path $root "PT-Leg-Tracker\output\PT-Leg-Tracker-debug.apk") (Join-Path $stage "app\SmartOAKnee.apk")

# The four separate images and flash_args, not the merged 4 MB image: the
# merged one also covers NVS and would wipe the Wi-Fi settings on every upgrade.
# [TH] ใช้ไฟล์แยกสี่ไฟล์กับ flash_args ไม่ใช้ไฟล์ merged ขนาด 4 MB เพราะไฟล์นั้นทับ NVS และจะลบค่า WiFi ทุกครั้งที่อัปเกรด
foreach ($role in "controller", "display") {
    $sketch = if ($role -eq "controller") { "pt-leg-controller" } else { "pt-leg-display" }
    $build = Join-Path $root "firmware\.build\$role"
    $dest = Join-Path $stage "firmware\$role"
    foreach ($file in "$sketch.ino.bin", "$sketch.ino.bootloader.bin", "$sketch.ino.partitions.bin",
                      "boot_app0.bin", "flash_args") {
        Copy-Into (Join-Path $build $file) (Join-Path $dest $file)
    }
}

# Newest esptool from the installed ESP32 core: it matches the flash_args the
# same core wrote (5.x option spelling).
# [TH] ใช้ esptool รุ่นใหม่สุดจากแกน ESP32 ที่ติดตั้งไว้ ให้ตรงกับ flash_args ที่แกนเดียวกันเขียน
$esptoolBase = Join-Path $env:LOCALAPPDATA "Arduino15\packages\esp32\tools\esptool_py"
$esptoolDir = Get-ChildItem $esptoolBase -Directory |
    Sort-Object { try { [version]$_.Name } catch { [version]"0.0" } } | Select-Object -Last 1
if (-not $esptoolDir -or [version]$esptoolDir.Name -lt [version]"5.0") {
    throw "esptool 5.x not found under $esptoolBase. Install or update the esp32 core."
}
Copy-Into (Join-Path $esptoolDir.FullName "esptool.exe") (Join-Path $stage "esptool.exe")
Copy-Into (Join-Path $esptoolDir.FullName "LICENSE") (Join-Path $stage "esptool-LICENSE.txt")

Copy-Into (Join-Path $root "firmware\sd\sounds") (Join-Path $stage "sd-card\sounds")

# Code only: no venv, no caches, and never the local database.
# [TH] เฉพาะโค้ด ไม่เอา venv ไม่เอาแคช และไม่เอาฐานข้อมูลในเครื่องเด็ดขาด
$server = Join-Path $stage "server"
Copy-Into (Join-Path $root "server\app") (Join-Path $server "app")
Get-ChildItem $server -Recurse -Directory -Filter "__pycache__" | Remove-Item -Recurse -Force
Copy-Into (Join-Path $root "server\requirements.txt") (Join-Path $server "requirements.txt")
Copy-Into (Join-Path $root "server\README.md") (Join-Path $server "README.md")
Copy-Into (Join-Path $root "tools\release\start-server.cmd") (Join-Path $server "start-server.cmd")

foreach ($file in "upload.ps1", "upload.cmd", "README.txt") {
    Copy-Into (Join-Path $root "tools\release\$file") (Join-Path $stage $file)
}
@(
    "version: $Version"
    "commit:  $commit$(if ($dirty) { ' (with uncommitted changes)' })"
    "built:   $(Get-Date -Format 'yyyy-MM-dd HH:mm')"
) | Set-Content -Encoding ASCII (Join-Path $stage "build-info.txt")

if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path $stage -DestinationPath $zip
Write-Host ("release: {0} ({1:N1} MB)" -f $zip, ((Get-Item $zip).Length / 1MB)) -ForegroundColor Green
if ($dirty) { Write-Host "warning: built from a working tree with uncommitted changes" -ForegroundColor Yellow }
