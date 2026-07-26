# [TH] สคริปต์ทดสอบปุ่มหน้าปัดของบอร์ดคอนโทรลเลอร์ บนโต๊ะ ไม่ต้องต่อจอ CYD
#      อ่านบรรทัด [dbg] จากพอร์ต USB แล้วพาทำทีละขั้น พร้อมบอกผ่าน/ไม่ผ่านทุกขั้น
#
# Walks the controller's panel buttons through a pass/fail checklist using the
# DEBUG_SERIAL prints on USB. No display board and no UART jumpers needed: the
# display link is on Serial2, and the UART-loss safe-stop only arms once a
# display has actually been heard from.
#
# Flash the controller with the debug prints on first:
#   arduino-cli compile --fqbn "esp32:esp32:esp32:PartitionScheme=huge_app" `
#     --build-property compiler.cpp.extra_flags=-DDEBUG_SERIAL=1 --clean `
#     --upload -p COM4 firmware/pt-leg-controller
#
# Then (port auto-detected; pass -Port COMx to override):
#   .\firmware\tools\button-bench.ps1
#
# Wiring: the buttons are normally-closed on INPUT_PULLUP, so a *grounded* pin is
# the at-rest state and lifting the jumper is a press. Three jumpers:
#   GPIO 25 -> GND (START)   GPIO 26 -> GND (STOP)   GPIO 4 -> GND (RESET)
# Leave the limit switches (32, 33) floating unless a step says otherwise.
#
# -Tail skips the checklist and just streams the debug output.

param(
    [string]$Port = "",        # empty = auto-detect the ESP32's USB-serial port
    [int]$StepTimeoutSec = 30, # per-step patience before a step is failed
    [switch]$NoReset,          # skip the DTR/RTS reboot pulse at start
    [switch]$Tail,             # stream the port, run no checks
    [switch]$DryRun            # no board: replay a canned transcript through the same checks
)

$ErrorActionPreference = "Stop"

# [TH] บทสนทนาจำลองสำหรับ -DryRun เขียนตามรูปแบบ printf ใน pt-leg-controller.ino เป๊ะๆ
# Canned transcript for -DryRun, copied from the printf formats in
# pt-leg-controller.ino. It exists so the checklist itself can be exercised with
# no board attached: a pattern that stops matching the firmware's output fails
# here first, instead of on the bench with jumpers in hand.
$script:dryTranscript = @(
    'pt-leg-controller 0.1.0 ssid=ClinicWiFi server=http://192.168.0.12:8000 code=KNEE-01 cycle max/hold/rest=8/10/10'
    'debug prints ON - btn pins start=25 stop=26 reset=4, NC=yes (LOW = at rest, HIGH = pressed)'
    '[dbg 812] btn levels start=HIGH/pressed stop=HIGH/pressed reset=HIGH/pressed'
    '[dbg 812] limits top=AT-LIMIT bottom=AT-LIMIT'
    '[dbg 812] phase=IDLE reps=0 elapsed=0s'
    '[dbg 2400] btn levels start=LOW/rest stop=LOW/rest reset=LOW/rest'
    '[dbg 5100] EDGE START'
    '[dbg 5100] phase=LIFT reps=0 elapsed=0s'
    '[dbg 5140] btn levels start=HIGH/pressed stop=LOW/rest reset=LOW/rest'
    '[dbg 5260] btn levels start=LOW/rest stop=LOW/rest reset=LOW/rest'
    '[dbg 9000] EDGE STOP'
    '[dbg 9000] phase=SAFE-LOWER reps=0 elapsed=4s'
    '[dbg 9300] phase=IDLE reps=0 elapsed=0s'
    '[dbg 12000] EDGE RESET'
    '[dbg 12000] phase=IDLE reps=0 elapsed=0s'
    '[dbg 15000] limits top=clear bottom=clear'
    '[dbg 18000] limits top=AT-LIMIT bottom=AT-LIMIT'
)
$script:dryIndex = 0

$sp = $null
if ($DryRun) {
    Write-Host "DRY RUN - replaying a canned transcript, no board involved" -ForegroundColor Magenta
} else {
    . (Join-Path $PSScriptRoot 'esp-port.ps1')   # Resolve-EspPort - ตัวช่วยหาพอร์ต

    if (-not $Port) {
        $Port = Resolve-EspPort
        Write-Host "auto-detected ESP32 on $Port" -ForegroundColor Cyan
    }

    $sp = New-Object System.IO.Ports.SerialPort $Port, 115200, None, 8, one
    $sp.ReadTimeout = 200
    $sp.DtrEnable = $false
    $sp.RtsEnable = $false
    $sp.Open()

    # [TH] กระตุก RTS เพื่อรีเซ็ตบอร์ด จะได้เห็นข้อความตอนบูตและเริ่มจากสถานะที่รู้แน่
    # RTS drives EN on a dev board, so pulsing it reboots the controller. Worth
    # doing: the banner proves the running image really has DEBUG_SERIAL on, and
    # the run starts from a known state instead of mid-session.
    if (-not $NoReset) {
        $sp.RtsEnable = $true
        Start-Sleep -Milliseconds 120
        $sp.RtsEnable = $false
        Start-Sleep -Milliseconds 400
    }
}

# [TH] อ่านทีละบรรทัดแบบไม่บล็อค คืนค่า $null ถ้ายังไม่มีข้อมูล
# Non-blocking-ish line read: $null when the port had nothing within ReadTimeout.
function Read-LineOrNull {
    if ($DryRun) {
        if ($script:dryIndex -ge $script:dryTranscript.Count) { return $null }
        $line = $script:dryTranscript[$script:dryIndex]
        $script:dryIndex++
        return $line
    }
    try { return $sp.ReadLine() } catch [System.TimeoutException] { return $null }
}

function Write-SerialLine([string]$line) {
    $color = "DarkGray"
    if ($line -match 'EDGE ') { $color = "Yellow" }
    elseif ($line -match 'phase=') { $color = "Cyan" }
    elseif ($line -match '\[dbg') { $color = "DarkCyan" }
    Write-Host "  | $line" -ForegroundColor $color
}

# [TH] รอให้บรรทัดตรงกับ pattern ตามลำดับที่กำหนด หรือหมดเวลา
# Wait for the patterns strictly in the order given. Order matters: a state line
# printed *before* the press proves nothing about the press, and the firmware
# always prints the edge first and the resulting state right after it. Matching
# in any order let a stale line satisfy a step nobody had performed yet.
function Wait-ForPatterns([string[]]$patterns, [int]$timeoutSec) {
    $pending = [System.Collections.Generic.Queue[string]]::new()
    foreach ($p in $patterns) { $pending.Enqueue($p) }
    $hits = @()
    $deadline = (Get-Date).AddSeconds($timeoutSec)
    while ((Get-Date) -lt $deadline -and $pending.Count -gt 0) {
        $line = Read-LineOrNull
        if (-not $line) { continue }
        $line = $line.Trim()
        if (-not $line) { continue }
        Write-SerialLine $line
        if ($line -match $pending.Peek()) {
            $hits += $line
            [void]$pending.Dequeue()
        }
    }
    return [pscustomobject]@{ Ok = ($pending.Count -eq 0); Missing = @($pending.ToArray()); Hits = $hits }
}

# [TH] ล้างข้อมูลค้างในบัฟเฟอร์ก่อนเริ่มขั้นถัดไป กันบรรทัดเก่าทำให้ผ่านทั้งที่ยังไม่ได้กด
# Drop buffered lines between steps, so a stale line cannot pass the next step.
function Clear-Serial {
    if ($DryRun) { return }   # the canned transcript is consumed in order, never stale
    Start-Sleep -Milliseconds 150
    $sp.DiscardInBuffer()
}

# A dry run drains its transcript instantly, so a failing step would otherwise
# sit spinning on an empty queue for the full bench timeout.
if ($DryRun) { $StepTimeoutSec = [Math]::Min($StepTimeoutSec, 3) }

if ($Tail) {
    Write-Host "tailing $Port - Ctrl+C to stop" -ForegroundColor Green
    try { while ($true) { $l = Read-LineOrNull; if ($l) { Write-SerialLine $l.Trim() } } }
    finally { $sp.Close(); Write-Host "closed $Port" -ForegroundColor DarkGray }
    return
}

# ---- the checklist -------------------------------------------------------
# Each step: what to do by hand, then what the firmware must print for a pass.
# Patterns match the DEBUG_SERIAL format in pt-leg-controller.ino.
$steps = @(
    [pscustomobject]@{
        Name    = "debug build is running"
        Do      = "nothing - just confirming the board is flashed with DEBUG_SERIAL=1"
        Expect  = @('debug prints ON')
        Hint    = "No banner: the image was built without the flag, or -NoReset skipped the reboot that prints it."
    },
    [pscustomobject]@{
        Name    = "all three buttons at rest"
        # Levels print on change, so the step has to create one: starting from
        # bare pins guarantees a HIGH -> LOW transition to watch for.
        Do      = "start with all jumpers OFF, then ground GPIO 25, 26 and 4 (START/STOP/RESET)"
        Expect  = @('btn levels start=LOW/rest stop=LOW/rest reset=LOW/rest')
        Hint    = "A pin stuck HIGH is not grounded, or the button wiring is normally-open while config.h says SWITCH_NC."
    },
    [pscustomobject]@{
        Name    = "START begins a session"
        Do      = "lift the START jumper (GPIO 25) briefly, then put it back on GND"
        Expect  = @('EDGE START', 'phase=LIFT')
        Hint    = "EDGE but no LIFT: the session refused to start (already running, or a latched FAULT). No EDGE at all: wrong pin or polarity."
    },
    [pscustomobject]@{
        Name    = "STOP safe-lowers"
        Do      = "lift the STOP jumper (GPIO 26) briefly, then put it back on GND"
        Expect  = @('EDGE STOP', 'phase=(SAFE-LOWER|IDLE)')
        Hint    = "With both limit pins floating the board reads 'already at the bottom', so it can go straight to IDLE - that still counts."
    },
    [pscustomobject]@{
        Name    = "RESET clears reps when idle"
        Do      = "wait for phase=IDLE, then lift the RESET jumper (GPIO 4) briefly"
        Expect  = @('EDGE RESET', 'phase=IDLE reps=0')
        Hint    = "RESET is idle-only by design: pressed mid-session it prints the edge and does nothing."
    },
    [pscustomobject]@{
        Name    = "limit switches read both ways"
        Do      = "ground GPIO 32 and 33 as well (both limits 'clear'), then remove those two jumpers"
        Expect  = @('limits top=clear bottom=clear', 'limits top=AT-LIMIT bottom=AT-LIMIT')
        Hint    = "Floating NC inputs read AT-LIMIT - that is the fail-safe, not a fault. Grounding them must flip both to clear."
    }
)

$results = @()
try {
    Write-Host ""
    Write-Host "controller button bench on $Port" -ForegroundColor Green
    Write-Host "buttons are NC: jumper to GND = at rest, lift = press" -ForegroundColor Green
    Write-Host "$($steps.Count) steps, ${StepTimeoutSec}s each. Ctrl+C to abort." -ForegroundColor Green

    $n = 0
    foreach ($step in $steps) {
        $n++
        Write-Host ""
        Write-Host "[$n/$($steps.Count)] $($step.Name)" -ForegroundColor White
        Write-Host "      do: $($step.Do)" -ForegroundColor Cyan
        if ($n -gt 1) { Clear-Serial }
        $r = Wait-ForPatterns $step.Expect $StepTimeoutSec
        if ($r.Ok) {
            Write-Host "      PASS" -ForegroundColor Green
        } else {
            Write-Host "      FAIL - never saw: $($r.Missing -join ' | ')" -ForegroundColor Red
            Write-Host "      $($step.Hint)" -ForegroundColor DarkYellow
        }
        $results += [pscustomobject]@{ Step = $step.Name; Pass = $r.Ok; Missing = ($r.Missing -join ' | ') }
    }
}
finally {
    if ($sp) { $sp.Close() }
}

Write-Host ""
Write-Host "summary" -ForegroundColor White
foreach ($r in $results) {
    if ($r.Pass) {
        Write-Host ("  PASS  " + $r.Step) -ForegroundColor Green
    } else {
        Write-Host ("  FAIL  " + $r.Step + "  (missing: " + $r.Missing + ")") -ForegroundColor Red
    }
}
$failed = @($results | Where-Object { -not $_.Pass }).Count
Write-Host ""
if ($failed -eq 0) {
    Write-Host "all $($results.Count) steps passed" -ForegroundColor Green
} else {
    Write-Host "$failed of $($results.Count) steps failed" -ForegroundColor Red
}
exit $failed
