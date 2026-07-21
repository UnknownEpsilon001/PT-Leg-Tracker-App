# Impersonates the controller so the CYD's UI can be exercised with no second
# board on the desk. Speaks the same newline-delimited JSON as
# firmware/link-protocol.md.
#
# Flash the display with the link routed to USB first:
#   arduino-cli compile --fqbn "esp32:esp32:esp32:PartitionScheme=huge_app" `
#     --build-property compiler.cpp.extra_flags=-DLINK_OVER_USB=1 `
#     --upload -p COM4 firmware/pt-leg-display
#
# Then:
#   .\firmware\tools\fake-controller.ps1 -Port COM4
#
# It answers `hello` with a settings reply, then runs a compressed session so the
# phases actually move: LIFT -> HOLD -> LOWER -> REST, repeating. Ctrl+C stops,
# and the display should fall back to NO LINK about 2.5 s later.

param(
    [string]$Port = "COM4",
    [int]$HoldSec = 4,      # compressed from the real 10 s so a cycle is watchable
    [int]$RestSec = 4,
    [int]$TravelSec = 2,
    [int]$Seconds = 0,      # 0 = run until Ctrl+C
    [switch]$AutoStart      # begin a session immediately, without waiting for a touch
)

$ErrorActionPreference = "Stop"

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
