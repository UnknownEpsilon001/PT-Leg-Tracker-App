# [TH] คอมไพล์ทุกส่วนแล้วรวมเป็นไฟล์รีลีสเดียว: แอป APK, เฟิร์มแวร์สองบอร์ด, เซิร์ฟเวอร์, ไฟล์การ์ด SD และตัวอัปโหลด
#      แสดงแถบความคืบหน้า เวลาที่ใช้แต่ละขั้น และแจ้งเมื่อเสร็จหรือล้มเหลว
#
# Compiles everything and packs one release zip: the APK, both firmware images,
# the server, the SD card files and the self-contained uploader. Shows a progress
# bar with the current step, the time each step took, and a clear finished or
# failed banner with a beep.
#
#   .\tools\build-release.ps1                    # -> release\MedFlexAutoSmart-<date>-<commit>.zip
#   .\tools\build-release.ps1 -Version 1.0.0
#   .\tools\build-release.ps1 -Open              # open the folder when done
#
# Compiling goes through the existing tools, so their guards still run:
# flash.ps1 refuses a bench switch left on, app-build.ps1 keeps the last good APK
# if Gradle fails. Both print their own progress inside each step.
# [TH] การคอมไพล์ผ่านเครื่องมือเดิม ตัวตรวจของมันจึงยังทำงานครบ
#      flash.ps1 ไม่ยอมคอมไพล์ถ้ายังเปิดสวิตช์โหมดทดสอบไว้ app-build.ps1 เก็บ APK ที่ดีล่าสุดไว้ถ้า Gradle ล้มเหลว
# LEO WAS HERE ;3
param(
    [string]$Version = "",
    [switch]$Open
)
$ErrorActionPreference = "Stop"

$root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$commit = (git -C $root rev-parse --short HEAD).Trim()
if (-not $Version) { $Version = "$(Get-Date -Format yyyy.MM.dd)-$commit" }
$dirty = [bool](git -C $root status --porcelain --untracked-files=no)
$name = "MedFlexAutoSmart-$Version"
$stage = Join-Path $root "release\$name"
$zip = "$stage.zip"
$originalTitle = $Host.UI.RawUI.WindowTitle

function Format-Span([TimeSpan]$t) {
    if ($t.TotalMinutes -ge 1) { return "{0}m{1:00}s" -f [int][math]::Floor($t.TotalMinutes), $t.Seconds }
    return "{0}s" -f [int]$t.TotalSeconds
}

function Format-Bar([int]$pct) {
    $filled = [int]($pct / 5)
    return ("[" + ("#" * $filled) + ("-" * (20 - $filled)) + "] {0,3}%") -f $pct
}

function Invoke-Script([string]$script, [string[]]$argList) {
    & powershell -NoProfile -ExecutionPolicy Bypass -File $script @argList
    if ($LASTEXITCODE -ne 0) { throw "$(Split-Path -Leaf $script) failed (exit $LASTEXITCODE)" }
}

function Copy-Into([string]$from, [string]$to) {
    New-Item -ItemType Directory -Force (Split-Path -Parent $to) | Out-Null
    Copy-Item $from $to -Recurse -Force
}

function New-ReleasePackage {
    if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
    New-Item -ItemType Directory -Force $stage | Out-Null

    Write-Host "  app\MedFlexAutoSmart.apk"
    Copy-Into (Join-Path $root "PT-Leg-Tracker\output\PT-Leg-Tracker-debug.apk") (Join-Path $stage "app\MedFlexAutoSmart.apk")

    # The four separate images and flash_args, not the merged 4 MB image: the
    # merged one also covers NVS and would wipe the Wi-Fi settings on every upgrade.
    # [TH] ใช้ไฟล์แยกสี่ไฟล์กับ flash_args ไม่ใช้ไฟล์ merged ขนาด 4 MB เพราะไฟล์นั้นทับ NVS และจะลบค่า WiFi ทุกครั้งที่อัปเกรด
    foreach ($role in "controller", "display") {
        Write-Host "  firmware\$role\"
        $sketch = if ($role -eq "controller") { "pt-leg-controller" } else { "pt-leg-display" }
        $build = Join-Path $root "firmware\.build\$role"
        foreach ($file in "$sketch.ino.bin", "$sketch.ino.bootloader.bin", "$sketch.ino.partitions.bin",
                          "boot_app0.bin", "flash_args") {
            Copy-Into (Join-Path $build $file) (Join-Path $stage "firmware\$role\$file")
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
    Write-Host "  esptool.exe ($($esptoolDir.Name))"
    Copy-Into (Join-Path $esptoolDir.FullName "esptool.exe") (Join-Path $stage "esptool.exe")
    Copy-Into (Join-Path $esptoolDir.FullName "LICENSE") (Join-Path $stage "esptool-LICENSE.txt")

    Write-Host "  sd-card\sounds\"
    Copy-Into (Join-Path $root "firmware\sd\sounds") (Join-Path $stage "sd-card\sounds")

    # Code only: no venv, no caches, and never the local database.
    # [TH] เฉพาะโค้ด ไม่เอา venv ไม่เอาแคช และไม่เอาฐานข้อมูลในเครื่องเด็ดขาด
    Write-Host "  server\"
    $server = Join-Path $stage "server"
    Copy-Into (Join-Path $root "server\app") (Join-Path $server "app")
    Get-ChildItem $server -Recurse -Directory -Filter "__pycache__" | Remove-Item -Recurse -Force
    Copy-Into (Join-Path $root "server\requirements.txt") (Join-Path $server "requirements.txt")
    Copy-Into (Join-Path $root "server\README.md") (Join-Path $server "README.md")
    Copy-Into (Join-Path $root "tools\release\start-server.cmd") (Join-Path $server "start-server.cmd")

    Write-Host "  upload.cmd, load-sounds.cmd, README.txt, build-info.txt"
    foreach ($file in "upload.ps1", "upload.cmd", "load-sounds.cmd", "README.txt") {
        Copy-Into (Join-Path $root "tools\release\$file") (Join-Path $stage $file)
    }
    Copy-Into (Join-Path $root "firmware\tools\load-sounds.ps1") (Join-Path $stage "load-sounds.ps1")
    @(
        "version: $Version"
        "commit:  $commit$(if ($dirty) { ' (with uncommitted changes)' })"
        "built:   $(Get-Date -Format 'yyyy-MM-dd HH:mm')"
    ) | Set-Content -Encoding ASCII (Join-Path $stage "build-info.txt")

    Write-Host "  zipping..."
    if (Test-Path $zip) { Remove-Item $zip -Force }
    Compress-Archive -Path $stage -DestinationPath $zip
}

# Weights are rough seconds from a warm build, used only to make the percentage
# move in proportion to the work rather than jump a quarter per step.
# [TH] น้ำหนักคือเวลาโดยประมาณเป็นวินาทีจากการคอมไพล์ที่มีแคชแล้ว ใช้ให้เปอร์เซ็นต์ขยับตามงานจริง
$steps = @(
    @{ Name = "Android app (APK)";   Weight = 60;  Run = { Invoke-Script (Join-Path $root "PT-Leg-Tracker\tools\app-build.ps1") @("-Action", "build") } }
    @{ Name = "Controller firmware"; Weight = 180; Run = { Invoke-Script (Join-Path $root "firmware\tools\flash.ps1") @("-Action", "compile", "-Board", "controller", "-IfChanged") } }
    @{ Name = "CYD display firmware"; Weight = 240; Run = { Invoke-Script (Join-Path $root "firmware\tools\flash.ps1") @("-Action", "compile", "-Board", "display", "-IfChanged") } }
    @{ Name = "Package release zip"; Weight = 20;  Run = { New-ReleasePackage } }
)
$totalWeight = ($steps | ForEach-Object { $_.Weight } | Measure-Object -Sum).Sum
$doneWeight = 0
$clock = [Diagnostics.Stopwatch]::StartNew()
$stepClock = $clock
$timings = @()

Write-Host ""
Write-Host "Building $name" -ForegroundColor White
Write-Host "commit $commit$(if ($dirty) { ' + uncommitted changes' })"

$current = $null
try {
    for ($i = 0; $i -lt $steps.Count; $i++) {
        $current = $steps[$i]
        $pct = [int](100 * $doneWeight / $totalWeight)
        $Host.UI.RawUI.WindowTitle = "Release $pct% - $($current.Name)"
        Write-Host ""
        Write-Host ("{0}  step {1}/{2}: {3}   (elapsed {4})" -f (Format-Bar $pct), ($i + 1), $steps.Count,
            $current.Name, (Format-Span $clock.Elapsed)) -ForegroundColor Cyan
        $stepClock = [Diagnostics.Stopwatch]::StartNew()
        & $current.Run
        $timings += [pscustomobject]@{ Step = $current.Name; Time = Format-Span $stepClock.Elapsed }
        Write-Host ("[OK] {0} finished in {1}" -f $current.Name, (Format-Span $stepClock.Elapsed)) -ForegroundColor Green
        $doneWeight += $current.Weight
    }
} catch {
    $Host.UI.RawUI.WindowTitle = "Release FAILED - $($current.Name)"
    Write-Host ""
    Write-Host ("[FAILED] {0} after {1} (total {2})" -f $current.Name, (Format-Span $stepClock.Elapsed),
        (Format-Span $clock.Elapsed)) -ForegroundColor Red
    Write-Host "  $($_.Exception.Message)" -ForegroundColor Red
    Write-Host "  The output above this line shows what went wrong. Nothing was packaged." -ForegroundColor Red
    try { [console]::Beep(400, 600) } catch {}
    exit 1
}

$Host.UI.RawUI.WindowTitle = "Release 100% - done"
Write-Host ""
Write-Host ("{0}  all {1} steps done in {2}" -f (Format-Bar 100), $steps.Count, (Format-Span $clock.Elapsed)) -ForegroundColor Green
$timings | Format-Table -AutoSize | Out-String | Write-Host
Write-Host "RELEASE READY" -ForegroundColor Green
Write-Host ("  {0}  ({1:N1} MB)" -f $zip, ((Get-Item $zip).Length / 1MB))
if ($dirty) { Write-Host "  warning: built from a working tree with uncommitted changes" -ForegroundColor Yellow }
try { [console]::Beep(880, 200); [console]::Beep(1175, 300) } catch {}
if ($Open) { Start-Process explorer.exe "/select,`"$zip`"" }
$Host.UI.RawUI.WindowTitle = $originalTitle
