# [TH] แฟลชเฟิร์มแวร์ที่คอมไพล์ไว้แล้วในโฟลเดอร์รีลีสนี้ลงบอร์ดคอนโทรลเลอร์และจอ CYD
#      หาบอร์ดเองจากชิป USB-serial ไม่ต้องติดตั้ง Arduino
#
# Flashes the prebuilt controller and CYD firmware in this release folder. No
# Arduino install needed: esptool.exe ships next to this script. Each board is
# found by its USB-serial chip, the same rule firmware/tools/flash.ps1 uses:
# controller = CP210x (VID 10C4), CYD display = CH340/CH9102 (VID 1A86).
#
#   upload.cmd                                   # flash every board plugged in
#   upload.cmd -Board controller
#   upload.cmd -ControllerPort COM4 -DisplayPort COM5
#   upload.cmd -Erase                            # wipe the chip first (loses NVS)
#
# Offsets come from each board's flash_args, written by the compiler, so NVS at
# 0x9000 is never touched and the Wi-Fi settings survive an upgrade. The 4 MB
# merged image is not used for exactly that reason.
# [TH] ตำแหน่งที่เขียนอ่านจากไฟล์ flash_args ที่คอมไพเลอร์สร้าง จึงไม่แตะ NVS ที่ 0x9000
#      ค่า WiFi จึงยังอยู่หลังอัปเกรด และไม่ใช้ไฟล์ merged ขนาด 4 MB ด้วยเหตุผลนี้
param(
    [ValidateSet("both", "controller", "display")][string]$Board = "both",
    [string]$ControllerPort = "",
    [string]$DisplayPort = "",
    [switch]$Erase,
    [switch]$Yes
)
$ErrorActionPreference = "Stop"

$esptool = Join-Path $PSScriptRoot "esptool.exe"
if (-not (Test-Path $esptool)) {
    # Run straight from the repo, this is only the template: build-release.ps1
    # copies it into the release zip next to esptool.exe and the firmware.
    if (Test-Path (Join-Path $PSScriptRoot "..\build-release.ps1")) {
        throw ("This is the source copy of the uploader, not a release. Unzip release\SmartOAKnee-*.zip " +
               "and run upload.cmd from there, or flash from source with firmware\tools\flash.ps1 -Action auto -Board both.")
    }
    throw "esptool.exe is missing from $PSScriptRoot"
}

$vids = @{ controller = "VID_10C4"; display = "VID_1A86" }
$explicit = @{ controller = $ControllerPort; display = $DisplayPort }

$serialPorts = @(Get-CimInstance Win32_PnPEntity -Filter "PNPClass = 'Ports'" |
    Where-Object { $_.Name -match '\((COM\d+)\)' })

function Find-Ports([string]$vid) {
    @($serialPorts | Where-Object { $_.PNPDeviceID -like "*$vid*" } |
        ForEach-Object { [regex]::Match($_.Name, '\((COM\d+)\)').Groups[1].Value } |
        Sort-Object -Unique)
}

# An explicit port is still checked: one that belongs to the other board's USB
# chip would write the wrong firmware. An unknown chip is allowed through.
# [TH] พอร์ตที่ระบุเองก็ยังตรวจ ถ้าเป็นชิปของอีกบอร์ดจะเขียนเฟิร์มแวร์ผิดตัว ชิปที่ไม่รู้จักยอมให้ผ่าน
function Assert-PortRole([string]$role, [string]$port) {
    $other = if ($role -eq "controller") { "display" } else { "controller" }
    $dev = $serialPorts | Where-Object { $_.Name -match "\($([regex]::Escape($port))\)" } | Select-Object -First 1
    if ($dev -and $dev.PNPDeviceID -like "*$($vids[$other])*") {
        throw "$port is a $other board ($($vids[$other])), not the $role. Check the port numbers."
    }
}

# Two bridges of one kind means the board cannot be told apart from a stranger
# on the same chip: stop rather than flash a guess.
# [TH] ถ้าเจอชิปชนิดเดียวกันสองตัว แยกไม่ออกว่าตัวไหนคือบอร์ดจริง จึงหยุดแทนการเดา
function Resolve-Port([string]$role) {
    if ($explicit[$role]) { Assert-PortRole $role $explicit[$role]; return $explicit[$role] }
    # @() again here: a one-item result comes back from the function as a bare
    # string, and "COM8"[0] is "C"
    # [TH] ต้องครอบ @() อีกครั้ง ผลลัพธ์รายการเดียวจะกลับมาเป็นสตริงเปล่า และ "COM8"[0] คือ "C"
    $found = @(Find-Ports $vids[$role])
    if ($found.Count -gt 1) {
        $flag = if ($role -eq "controller") { "-ControllerPort" } else { "-DisplayPort" }
        throw "More than one $role board found ($($found -join ', ')). Unplug the extra one or pass $flag."
    }
    if ($found.Count -eq 1) { return $found[0] }
    return ""
}

function Invoke-Esptool([string[]]$argList, [string]$what) {
    & $esptool @argList
    if ($LASTEXITCODE -ne 0) { throw "$what failed (esptool exit $LASTEXITCODE)" }
}

function Publish-Board([string]$role, [string]$port) {
    $dir = Join-Path $PSScriptRoot "firmware\$role"
    $lines = @(Get-Content (Join-Path $dir "flash_args") | Where-Object { $_.Trim() })
    $argList = @("--chip", "esp32", "--port", $port, "--baud", "921600",
        "--before", "default-reset", "--after", "hard-reset", "write-flash") + (-split $lines[0])
    foreach ($line in $lines[1..($lines.Count - 1)]) {
        $offset, $file = -split $line
        $argList += $offset, (Join-Path $dir $file)
    }
    if ($Erase) {
        Write-Host "  erasing the whole chip (about 20 s)..."
        Invoke-Esptool @("--chip", "esp32", "--port", $port, "erase-flash") "erase of $role on $port"
    }
    Write-Host "  writing 4 images; esptool shows the percent for each"
    Invoke-Esptool $argList "upload of $role on $port"
}

function Format-Span([TimeSpan]$t) {
    if ($t.TotalMinutes -ge 1) { return "{0}m{1:00}s" -f [int][math]::Floor($t.TotalMinutes), $t.Seconds }
    return "{0}s" -f [int]$t.TotalSeconds
}

$roles = if ($Board -eq "both") { @("controller", "display") } else { @($Board) }
$plan = [ordered]@{}
foreach ($role in $roles) {
    $port = Resolve-Port $role
    if ($port) { $plan[$role] = $port }
    elseif ($Board -ne "both") { throw "No $role board found (USB $($vids[$role])). Plug it in or pass its port." }
    else { Write-Host "no $role board plugged in - skipped" -ForegroundColor Yellow }
}
if ($plan.Count -eq 0) { throw "No controller (CP210x) or CYD (CH340) board found. Plug one in and retry." }

foreach ($role in $plan.Keys) { Write-Host ("{0,-11} -> {1}" -f $role, $plan[$role]) }
Write-Host "Pull the controller<->CYD UART jumpers first; a connected link fakes flash errors." -ForegroundColor Yellow
if ($Erase) { Write-Host "-Erase wipes NVS: Wi-Fi and device code are lost until the SD card pushes them back." -ForegroundColor Yellow }
if (-not $Yes) { Read-Host "Press Enter to flash, Ctrl+C to cancel" | Out-Null }

$clock = [Diagnostics.Stopwatch]::StartNew()
$done = @()
$i = 0
foreach ($role in @($plan.Keys)) {
    $i++
    $port = $plan[$role]
    $Host.UI.RawUI.WindowTitle = "Flashing $i/$($plan.Count) - $role on $port"
    Write-Host ""
    Write-Host "[$i/$($plan.Count)] $role on $port" -ForegroundColor Cyan
    $boardClock = [Diagnostics.Stopwatch]::StartNew()
    try {
        Publish-Board $role $port
    } catch {
        $Host.UI.RawUI.WindowTitle = "Flashing FAILED - $role"
        Write-Host ""
        Write-Host "[FAILED] $role on $port after $(Format-Span $boardClock.Elapsed)" -ForegroundColor Red
        Write-Host "  $($_.Exception.Message)" -ForegroundColor Red
        Write-Host "  Check the cable and the UART jumpers, then run upload.cmd again." -ForegroundColor Red
        if ($done) { Write-Host "  Already flashed in this run: $($done -join ', ')" -ForegroundColor Yellow }
        try { [console]::Beep(400, 600) } catch {}
        exit 1
    }
    $done += $role
    Write-Host "[OK] $role flashed in $(Format-Span $boardClock.Elapsed)" -ForegroundColor Green
}

$Host.UI.RawUI.WindowTitle = "Flashing done"
Write-Host ""
Write-Host "ALL DONE: $($done -join ' + ') flashed in $(Format-Span $clock.Elapsed)" -ForegroundColor Green
Write-Host "Reconnect the UART jumpers and power both boards." -ForegroundColor Green
try { [console]::Beep(880, 200); [console]::Beep(1175, 300) } catch {}
