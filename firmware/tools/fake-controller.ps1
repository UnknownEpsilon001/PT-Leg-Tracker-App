# [TH] สคริปต์สวมรอยเป็นบอร์ดคอนโทรลเลอร์ เพื่อทดสอบ UI บนจอ CYD โดยไม่ต้องมีบอร์ดที่สอง
#      พูดโปรโตคอล JSON คั่นด้วยขึ้นบรรทัดใหม่แบบเดียวกับ firmware/link-protocol.md
#      ตอบ hello ด้วยค่าตั้งค่า แล้วจำลองรอบการทำงาน ยก -> ค้าง -> ลง -> พัก วนไป
#
# Impersonates the controller so the CYD's UI can be exercised with no second
# board on the desk. Speaks the same newline-delimited JSON as
# firmware/link-protocol.md.
#
# Flash the display with the link routed to USB first:
#   arduino-cli compile --fqbn "esp32:esp32:esp32:PartitionScheme=huge_app" `
#     --build-property compiler.cpp.extra_flags=-DLINK_OVER_USB=1 `
#     --upload -p COM4 firmware/pt-leg-display
#
# Then (port auto-detected from the USB-serial chip; pass -Port COMx to override):
#   .\firmware\tools\fake-controller.ps1
#
# It answers `hello` with a settings reply, then runs the device's real cycle:
# LIFT -> HOLD 10 s -> LOWER -> REST 10 s, repeating. Ctrl+C stops, and the display
# should fall back to NO LINK about 2.5 s later.
# Shorten a phase for a quicker watch with -HoldSec / -RestSec.

param(
    [string]$Port = "",     # empty = auto-detect the ESP32's USB-serial port
    [int]$HoldSec = 10,     # matches the real device (lift -> hold 10 s -> lower -> rest 10 s)
    [int]$RestSec = 10,
    [int]$TravelSec = 2,
    [int]$Seconds = 0,      # 0 = run until Ctrl+C
    [switch]$AutoStart      # begin a session immediately, without waiting for a touch
)

$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot 'esp-port.ps1')   # Resolve-EspPort — ตัวช่วยหาพอร์ต

if (-not $Port) {
    $Port = Resolve-EspPort
    Write-Host "auto-detected ESP32 on $Port" -ForegroundColor Cyan
}

$sp = New-Object System.IO.Ports.SerialPort $Port, 115200, None, 8, one
$sp.DtrEnable = $false
$sp.RtsEnable = $false
$sp.Open()
Write-Host "fake controller on $Port. Ctrl+C to stop." -ForegroundColor Green

function Send-Line([string]$json) {
    $sp.WriteLine($json)
    Write-Host "-> $json" -ForegroundColor DarkGray
}

$settings = '{"t":"settings","ssid":"ClinicWiFi","pass":"secret123",' +
            '"server":"http://192.168.0.12:8000","code":"KNEE-01",' +
            '"maxtravel":8,"hold":10,"rest":10}'

# phase ordinals per link-protocol.md: 0 Idle 1 Lift 2 Hold 3 Lower 4 Rest 5 SafeLower 6 Fault
$running = $false
$phase = 0
$elapsed = 0
$reps = 0
$phaseLeft = 0
$sessionStart = $null

Send-Line $settings

if ($AutoStart) {
    $running = $true; $phase = 1; $phaseLeft = $TravelSec; $sessionStart = Get-Date
    Write-Host "   AUTOSTART" -ForegroundColor Green
}

# [TH] คุมด้วยแป้นพิมพ์ จำลองปุ่มหน้าปัดของคอนโทรลเลอร์ เพื่อทดสอบการตอบสนองของจอโดยไม่ต้องมีบอร์ดคอนโทรลเลอร์
# Live keyboard control stands in for the controller's panel buttons so the
# display's reaction to each session action can be exercised with only the CYD on
# the desk. s=START (idle only), x=STOP, r=RESET reps (idle only), q=quit. This
# drives the display SIDE of the button feature; it does not test the real button
# GPIO/debounce, which lives in the controller firmware.
# Skipped when input is redirected (e.g. piped/non-interactive) so the loop still
# runs headless with -AutoStart -Seconds.
$interactive = -not [Console]::IsInputRedirected
if ($interactive) {
    Write-Host "keys: s=start  x=stop  r=reset  q=quit" -ForegroundColor Cyan
}

$deadline = if ($Seconds -gt 0) { (Get-Date).AddSeconds($Seconds) } else { [datetime]::MaxValue }

try {
    while ((Get-Date) -lt $deadline) {
        # answer anything the display asks for, and react to its buttons
        while ($sp.BytesToRead -gt 0) {
            $line = ""
            try { $line = $sp.ReadLine() } catch { break }
            if (-not $line) { continue }
            Write-Host "<- $line" -ForegroundColor DarkCyan
            if ($line -match '"t"\s*:\s*"hello"') { Send-Line $settings }
            elseif ($line -match '"t"\s*:\s*"settings"') {
                Write-Host "   (display saved settings)" -ForegroundColor Yellow
                Send-Line $settings
            }
            elseif ($line -match '"v"\s*:\s*"start"') {
                $running = $true; $phase = 1; $phaseLeft = $TravelSec
                $reps = 0; $sessionStart = Get-Date
                Write-Host "   START" -ForegroundColor Green
            }
            elseif ($line -match '"v"\s*:\s*"stop"') {
                $phase = 5; $phaseLeft = $TravelSec   # SafeLower, then idle
                Write-Host "   STOP" -ForegroundColor Green
            }
        }

        # keyboard: stand in for the controller's START/STOP/RESET panel buttons
        if ($interactive) {
            while ([Console]::KeyAvailable) {
                $key = [Console]::ReadKey($true).KeyChar
                switch -CaseSensitive ([char]::ToLower($key)) {
                    's' {
                        if (-not $running) {
                            $running = $true; $phase = 1; $phaseLeft = $TravelSec
                            $reps = 0; $sessionStart = Get-Date
                            Write-Host "   START (key)" -ForegroundColor Green
                        } else {
                            Write-Host "   START ignored (already running)" -ForegroundColor DarkYellow
                        }
                    }
                    'x' {
                        if ($running -and $phase -ne 5) {
                            $phase = 5; $phaseLeft = $TravelSec   # SafeLower, then idle
                            Write-Host "   STOP (key)" -ForegroundColor Green
                        } else {
                            Write-Host "   STOP ignored (not running)" -ForegroundColor DarkYellow
                        }
                    }
                    'r' {
                        if (-not $running) {                      # controller: RESET only when idle
                            $reps = 0; $elapsed = 0
                            Write-Host "   RESET reps (key)" -ForegroundColor Green
                        } else {
                            Write-Host "   RESET ignored (running)" -ForegroundColor DarkYellow
                        }
                    }
                    'q' { Write-Host "   quit (key)" -ForegroundColor Green; return }
                }
            }
        }

        if ($running) {
            $elapsed = [int]((Get-Date) - $sessionStart).TotalSeconds
            $phaseLeft -= 0.25
            if ($phaseLeft -le 0) {
                switch ($phase) {
                    1 { $phase = 2; $phaseLeft = $HoldSec }        # Lift  -> Hold
                    2 { $phase = 3; $phaseLeft = $TravelSec }      # Hold  -> Lower
                    3 { $phase = 4; $phaseLeft = $RestSec }        # Lower -> Rest
                    4 { $reps++; $phase = 1; $phaseLeft = $TravelSec }  # Rest -> Lift
                    5 { $running = $false; $phase = 0; $elapsed = 0 }   # SafeLower -> Idle
                }
            }
        }

        $state = '{{"t":"state","running":{0},"phase":{1},"elapsed":{2},"reps":{3},' +
                 '"wifi":true,"server":true}}'
        $sp.WriteLine([string]::Format($state, $running.ToString().ToLower(), $phase, $elapsed, $reps))
        Start-Sleep -Milliseconds 250
    }
}
finally {
    $sp.Close()
    Write-Host "closed $Port" -ForegroundColor DarkGray
}
