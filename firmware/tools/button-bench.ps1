# [TH] สคริปต์ทดสอบปุ่มหน้าปัดของบอร์ดคอนโทรลเลอร์ บนโต๊ะ ไม่ต้องต่อจอ CYD
#      อ่านบรรทัด [dbg] จากพอร์ต USB แล้วพาทำทีละขั้น พร้อมบอกผ่าน/ไม่ผ่านทุกขั้น
#
# Walks the controller's panel buttons through a pass/fail checklist using the
# BENCH_DEBUG prints on USB. No display board and no UART jumpers needed: the
# display link is on Serial2, and the UART-loss safe-stop only arms once a
# display has actually been heard from.
#
# Flash the controller with the debug prints on first:
#   arduino-cli compile --fqbn "esp32:esp32:esp32:PartitionScheme=huge_app" `
#     --build-property compiler.cpp.extra_flags=-DBENCH_DEBUG=1 --clean `
#     --upload -p COM4 firmware/pt-leg-controller
#
# Then (port auto-detected; pass -Port COMx to override):
#   .\firmware\tools\button-bench.ps1
#
# The panel lamps are the out-of-band check throughout: one changeover relay
# on GPIO 23, green on NO lit whenever a session is in progress (including the
# safe-lower), yellow on NC lit while idle, and blinking once a second for jog
# mode and a latched FAULT. Outside those two the relay only ever switches at a
# phase boundary, and you should hear a click each time. If a step's serial output disagrees
# with the lamps, trust neither and check wiring.
#
# Wiring: both buttons are normally-open on INPUT_PULLUP, so an open pin is
# the at-rest state and touching it to GND is a press. Two connections:
#   GPIO 25 -> GND (START, momentary)   GPIO 26 -> GND (STOP, momentary)
# There is no RESET button any more - STOP does its job: it always ends a
# running session, and it zeroes the rep count when the machine is already
# idle. GPIO 4 and GPIO 33 are free pins the firmware does not read.
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
    'pt-leg-controller 0.1.0 ssid=ClinicWiFi server=http://192.168.0.12:8000 code=default cycle lift/hold/lower/rest=7/10/7/10 target=10'
    'debug prints ON - btn start=25(NO) stop=26(NO), lamp relay=23'
    '[dbg 210] btn levels start=HIGH/rest stop=HIGH/rest'
    '[dbg 210] phase=IDLE reps=0 elapsed=0s'
    '[dbg 5100] EDGE START'
    '[dbg 5100] phase=LIFT reps=0 elapsed=0s'
    '[dbg 5140] btn levels start=LOW/pressed stop=HIGH/rest'
    '[dbg 5260] btn levels start=HIGH/rest stop=HIGH/rest'
    '[dbg 6000] EDGE STOP'
    '[dbg 6000] phase=SAFE-LOWER reps=0 elapsed=1s'
    '[dbg 7400] phase=IDLE reps=0 elapsed=0s'    # 0.9 s into LIFT, so the safe-lower is that 0.9 s plus the 0.5 s margin, not a full 7 s lowerSec
    '[dbg 12000] EDGE STOP'
    '[dbg 12000] phase=IDLE reps=0 elapsed=0s'
    '[dbg 15000] EDGE START'
    '[dbg 15000] phase=LIFT reps=0 elapsed=0s'
    '[dbg 22000] phase=HOLD reps=0 elapsed=7s'
    '[dbg 23000] EDGE STOP'
    '[dbg 23000] phase=SAFE-LOWER reps=0 elapsed=8s'
    '[dbg 30000] phase=IDLE reps=0 elapsed=0s'   # stopped from HOLD, so this one is the full 7 s lowerSec
    '[dbg 33000] EDGE START'
    '[dbg 33000] phase=LIFT reps=0 elapsed=0s'
    'actuator: energize cap cut the motor drive'
    '[dbg 34500] phase=FAULT reps=0 elapsed=1s'
    # [TH] จากตรงนี้เป็นบทสนทนาจำลองของสถานะรอยืนยัน (CONFIRM) เริ่มด้วยการล้างสถานะผิดพลาดค้างจากขั้นก่อนหน้า
    # From here on: the CONFIRM lines, exercising the five steps below. Opens
    # by clearing the FAULT the previous step left latched, the same STOP a
    # human would touch first per that step's own Hint. Recovering out of a
    # latched Fault always drives the full lowerSec (enter()'s Fault/Idle
    # branch in session.cpp), not the proportional partial descent a STOP
    # mid-LIFT gets elsewhere in this transcript - hence the full 7000 ms gap
    # and elapsed pinned at 0s throughout (elapsedSec() returns 0 while
    # recovering, same as it does for Confirm itself just below).
    '[dbg 40000] EDGE STOP'
    '[dbg 40000] phase=SAFE-LOWER reps=0 elapsed=0s'
    '[dbg 47000] phase=IDLE reps=0 elapsed=0s'   # 7000 ms later: full lowerSec, a fault recovery is never proportional
    '[dbg 50000] EDGE START'
    '[dbg 50000] phase=CONFIRM reps=0 elapsed=0s'   # elapsed pinned at 0 the whole time Confirm is open (session.cpp)
    '[dbg 51000] EDGE START'                        # a second START while CONFIRM is open always commits it
    '[dbg 51000] phase=LIFT reps=0 elapsed=0s'
    # Settles the LIFT just committed back to IDLE so the next CONFIRM opens
    # from a clean idle, the same way the checklist's own steps ask for it.
    # [TH] คืนสถานะจากการยกที่เพิ่งยืนยันกลับไปว่าง เพื่อให้รอบยืนยันครั้งถัดไปเริ่มจากสถานะว่างที่สะอาด
    '[dbg 52000] EDGE STOP'
    '[dbg 52000] phase=SAFE-LOWER reps=0 elapsed=1s'
    '[dbg 53500] phase=IDLE reps=0 elapsed=0s'   # 1 s into a 7 s/7 s lift/lower, so 1 s down + 0.5 s margin
    '[dbg 55000] EDGE START'
    '[dbg 55000] phase=CONFIRM reps=0 elapsed=0s'
    '[dbg 56000] EDGE STOP'                      # panel STOP while CONFIRM is open cancels it, reps untouched
    '[dbg 56000] phase=IDLE reps=0 elapsed=0s'
    '[dbg 58000] EDGE START'
    '[dbg 58000] phase=CONFIRM reps=0 elapsed=0s'
    '[dbg 88000] phase=IDLE reps=0 elapsed=0s'   # +30000 ms = CONFIRM_TIMEOUT_MS exactly; no EDGE line, tick() alone drives it
    # Link down (jumpers out): requestStart() only opens Confirm when linkUp
    # is true, so this START behaves exactly like the very first START step
    # earlier in this transcript - straight to LIFT, no CONFIRM at all.
    # [TH] ลิงก์หลุด (ถอดจัมเปอร์): requestStart() จะเปิดหน้ายืนยันก็ต่อเมื่อ linkUp เป็นจริงเท่านั้น
    #      ปุ่ม START นี้จึงทำตัวเหมือนขั้น START ตัวแรกสุดในบทสนทนานี้ทุกประการ คือไปที่ LIFT ตรงๆ ไม่ผ่าน CONFIRM เลย
    '[dbg 91000] EDGE START'
    '[dbg 91000] phase=LIFT reps=0 elapsed=0s'
)
$script:dryIndex = 0
$script:seenLines = [System.Collections.ArrayList]::new()

# [TH] ลายเซ็นของเฟิร์มแวร์ที่ไม่ใช่ของโปรเจกต์นี้ ใช้บอกสาเหตุที่แท้จริงเมื่อขั้นแรกไม่ผ่าน
# Signatures of firmware that is not ours. A board still carrying the factory
# Espressif AT image fails step 1 for a reason no amount of rebuilding fixes, so
# name it instead of blaming the build flag.
$script:foreignFirmware = @(
    @{ Pattern = 'at_customize';         What = "Espressif AT firmware (at_customize partition)" }
    @{ Pattern = 'module_name:WROOM';    What = "Espressif AT firmware (AT banner)" }
    @{ Pattern = 'AT version';           What = "Espressif AT firmware" }
    @{ Pattern = 'No bootable app partitions'; What = "a stale partition table from a different scheme" }
)

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
    # doing: the banner proves the running image really has BENCH_DEBUG on, and
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
        [void]$script:seenLines.Add($line)
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
# Patterns match the BENCH_DEBUG format in pt-leg-controller.ino.
$steps = @(
    [pscustomobject]@{
        Name    = "debug build is running"
        Do      = "nothing, but watch the lamps through boot: yellow should come on and stay on, green must stay dark - neither should flash or click at power-up"
        Expect  = @('debug prints ON')
        Hint    = "No banner: the image was built without the flag, or -NoReset skipped the reboot that prints it. Green LIT while idle instead of yellow: the lamp polarity is backwards - LED_ACTIVE_LOW is set in config.h for the active-low board fitted here, so either the board on the bench energizes on HIGH or that line got lost. Fix it before anything else, because the lamps currently mean the opposite of what they should."
        Fatal   = $true   # every later step reads that same debug output
    },
    [pscustomobject]@{
        Name    = "both buttons at rest"
        # Both buttons are normally-open, so an unconnected pin already reads
        # "rest" - there is nothing to wire first the way an NC panel needed.
        # This still catches a firmware/wiring contact-type mismatch: if either
        # BTN_START_NC or BTN_STOP_NC were compiled as NC while the physical
        # switch is NO (or vice versa), the idle reading comes back "pressed".
        Do      = "start with nothing touching GPIO 25 or 26 - this line prints once at boot regardless of wiring"
        Expect  = @('btn levels start=\S+/rest stop=\S+/rest')
        Hint    = "Either side says /pressed with nothing touched: the firmware and the physical switch disagree on contact type. Flip BTN_START_NC / BTN_STOP_NC in config.h to match the part and reflash - both should be 0 (NO) as committed."
    },
    [pscustomobject]@{
        Name    = "START starts a session"
        Do      = "touch GPIO 25 to GND briefly, then let go - the green lamp should light (one relay click) and stay lit, yellow should go dark"
        Expect  = @('EDGE START', 'phase=LIFT')
        Hint    = "Edge printed but nothing happens, with 'start refused' logged: START has read pressed since power-up or far longer than a real press - the stuck-line guard suspects a short. Touch GPIO 25 to GND and release once to clear it. No EDGE at all: wrong pin, or the wire never reached GND."
    },
    [pscustomobject]@{
        Name    = "STOP lowers and stops"
        # There is no limit switch any more, so SAFE-LOWER is a timed stroke, not
        # one that reaches a physical bottom. It is no longer always the full
        # lowerSec, though: a STOP taken mid-LIFT drives DOWN for roughly the same
        # fraction of lowerSec as the fraction of liftSec already spent lifting,
        # plus a small margin - not a flat "~7 s regardless of when you pressed".
        # With the stock 7 s/7 s config that fraction is subtle to eyeball, so the
        # part of this step that actually proves the arithmetic is right needs
        # liftSec and lowerSec unequal - see the second Do line.
        Do      = "(a) with default timings (7 s/7 s), touch GPIO 26 to GND briefly while phase=LIFT - green stays lit right through the safe-lower and only goes dark, with yellow lighting, once phase reaches IDLE roughly (time already spent in LIFT) later, never more than 7 s. (b) then set DEFAULT_LIFT_SEC=2 and DEFAULT_LOWER_SEC=8 in config.h (both within DEFAULT_MAX_TRAVEL_SEC=8) and erase NVS before reflashing - `esptool --chip esp32 --port COMx erase_flash`, or skip the reflash and set lift=2 / lower=8 from the settings UI instead. A plain reflash alone does NOT do it: any board that has ever taken a settings message from the CYD or the server has its own lift/lower pair saved in NVS, and settingsLoad only falls back to the compiled config.h default when that key is absent, so the board would silently keep running whatever pair was already saved. Confirm this by reading the boot banner before touching STOP - it must print `cycle lift/hold/lower/rest=2/10/8/10`; anything else (7/10/7/10 included) means the NVS erase did not take and the rest of this step proves nothing. Then repeat, pressing STOP right at the end of LIFT: a CORRECT descent drives DOWN for close to the full 8 s - watch the leg, it should be visibly at the bottom by the time phase=IDLE prints. An UNDER-DRIVEN descent (the bug this step exists to catch) reaches phase=IDLE after only ~2.5 s with the leg still visibly raised - that is a FAIL even though the three Expect patterns below still print, because they only prove the phase transitions happened, not that the leg got anywhere"
        Expect  = @('EDGE STOP', 'phase=SAFE-LOWER', 'phase=IDLE')
        Hint    = "STOP also lowers out of a latched FAULT, and zeroes the rep count instead when the machine is already idle - so this step passing alone does not by itself prove a fresh session was running. The three Expect patterns are timing-blind: they pass whether the descent was proportional or under-driven, so part (b)'s visual check of the leg - still raised at phase=IDLE means FAIL - is the actual test, not optional color. Press STOP during LIFT only - the stated ratio only holds there. A press during LOWER instead drives DOWN for (lowerMs - time already spent in LOWER + 0.5 s), not the LIFT-based figure above. A press during REST gives no SAFE-LOWER at all: the leg is already down, so the session finishes straight to phase=IDLE and this step's phase=SAFE-LOWER pattern never shows up - that is a mistimed press, not a bug, so wait for the next LIFT and press again."
    },
    [pscustomobject]@{
        Name    = "STOP clears reps when idle"
        Do      = "with phase=IDLE, touch GPIO 26 to GND briefly again"
        Expect  = @('EDGE STOP', 'phase=IDLE reps=0')
        Hint    = "This is what replaces the retired RESET button: STOP while already idle only zeroes the rep count, nothing else happens. Pressed mid-session it lowers the leg instead (previous step)."
    },
    [pscustomobject]@{
        Name    = "lamps never agree"
        # Not just a serial check: this step's real pass criterion is what you
        # watched, not what matched. The two patterns below only prove a full
        # cycle ran; the lamp invariant has to be confirmed by eye.
        Do      = "run one more full cycle and watch BOTH lamps the whole time: touch GPIO 25 briefly, wait for LIFT, then touch GPIO 26 and watch it through to IDLE. This cycle never faults, so exactly one lamp must be lit at every instant - never blinking, never neither"
        Expect  = @('EDGE START', 'phase=LIFT', 'EDGE STOP', 'phase=IDLE')
        Hint    = "If the lamps blinked, went both dark, or a phase boundary above did not produce a relay click - stop and check LED_ACTIVE_LOW in config.h and the lamp wiring (green on NO, yellow on NC) before doing anything else. Blinking IS the fault and jog indication, so it is only wrong outside those."
    },
    [pscustomobject]@{
        Name    = "actuator cap induces a FAULT"
        # Nothing on the wire, in config.h, or in NVS can make liftSec/lowerSec
        # exceed maxTravelSec any more - every load path clamps them (settings.cpp),
        # which is exactly what stops a low cap from faulting every ordinary lift.
        # So the cap has to be tripped the way it is meant to be: a loop stall that
        # outlasts its fixed 500 ms margin (actuatorSetCap = maxTravelSec*1000+500,
        # see actuator.cpp) while the motor is driving. The heartbeat POST is the
        # practical way to arrange that on the bench - see net.cpp.
        Do      = "reflash once more with BENCH_DEBUG=1 and, this time, a low DEFAULT_MAX_TRAVEL_SEC (say 1) in config.h with NVS erased first, so DEFAULT_LIFT_SEC/DEFAULT_LOWER_SEC auto-clamp down to match - this shrinks the cap's fixed 500 ms margin to a much bigger share of the 1 s stroke. Then set serverUrl to an address nothing answers on the LAN - not a down WiFi, which makes net.cpp's post() return instantly instead of blocking, but a real IP with no listener. Touch GPIO 25 to GND to START: the 1/s heartbeat blocks up to ~400 ms per attempt against that address, and one landing across the LIFT->HOLD boundary is what eats the margin and trips the actuator's own hardware timer. May take a couple of tries to land the timing - each START is an independent attempt"
        Expect  = @('EDGE START', 'phase=LIFT', 'actuator: energize cap cut the motor drive', 'phase=FAULT')
        Hint    = "No trip after several tries: check maxTravelSec really did come out at 1 - a value already stored in NVS from a previous flash beats a lowered config.h default, since settingsLoad only falls back to the compiled default when the key is absent. Also confirm the unreachable address fails SLOW: a WiFi that is actually down, or a host that actively refuses the connection, returns near-instantly and never blocks long enough - it has to be an address that is silently unreachable, packets just dropped. Watch the lamps: they should start blinking green/yellow the instant it faults - that is the latched-FAULT indication. To clear it, touch GPIO 26 (STOP): the leg lowers through the fault's own safe-lower and the latch clears the moment phase reaches IDLE, same as any other STOP-from-FAULT recovery."
    }
    # The five steps below need something every step above deliberately did
    # not: the display link actually up. requestStart() only opens Confirm
    # when linkUp is true (pt-leg-controller.ino) - without a linked display
    # every START here would behave exactly like "START starts a session"
    # above, and the interesting half of this feature, that a bare panel
    # press waits for a human instead of moving the leg, would never be
    # exercised. Fit the UART jumpers per firmware/README.md and power a CYD
    # running pt-leg-display (any settings; only its 1 Hz ping matters here)
    # before running this group. The previous step may still be latched into
    # FAULT - the first step below clears it.
    # [TH] ห้าขั้นด้านล่างนี้ต้องการสิ่งที่ทุกขั้นก่อนหน้าตั้งใจไม่ต้องการเลย: ลิงก์กับจอต้องเชื่อมอยู่จริง
    #      requestStart() จะเปิดหน้ายืนยันก็ต่อเมื่อ linkUp เป็นจริงเท่านั้น (pt-leg-controller.ino)
    #      ถ้าไม่มีจอเชื่อมอยู่ ปุ่ม START ทุกขั้นด้านล่างจะทำตัวเหมือนขั้น "START starts a session" ด้านบนทุกประการ
    #      และครึ่งหนึ่งที่น่าสนใจของฟีเจอร์นี้ คือการกดปุ่มหน้าปัดเปล่าๆ จะรอคนแทนที่จะขยับขา จะไม่เคยถูกทดสอบเลย
    #      ต่อสายจัมเปอร์ UART ตาม firmware/README.md และเปิดจอ CYD ที่ลง pt-leg-display ไว้
    #      (ตั้งค่าใดก็ได้ สนใจแค่สัญญาณ ping 1 เฮิรตซ์ของมัน) ก่อนเริ่มกลุ่มนี้
    #      ขั้นก่อนหน้าอาจยังค้างอยู่ในสถานะผิดพลาด ขั้นแรกด้านล่างจะล้างให้เอง
    [pscustomobject]@{
        Name    = "START opens CONFIRM, no relay click"
        # The whole point of this feature: a bare panel press must NOT move
        # anything. A step that only checked phase=CONFIRM would pass on a
        # machine that opened the confirm screen and started lifting anyway -
        # the exact regression this step exists to catch - so the click (or
        # its absence) is graded by ear, not by the two Expect patterns below,
        # which say nothing about the relay either way.
        Do      = "if the previous step's FAULT is still latched, touch GPIO 26 first and wait for phase=IDLE (full 7 s lowerSec - this is a fault recovery, not a proportional one). With the display linked and phase=IDLE, touch GPIO 25 to GND briefly and let go. Listen: unlike every other START in this checklist, this press must produce NO relay click, and the green lamp must stay dark with yellow still lit - exactly the idle picture, because nothing has been committed yet"
        Expect  = @('EDGE START', 'phase=CONFIRM')
        Hint    = "phase=CONFIRM printing is necessary but not sufficient - it would print just the same on a machine that clicked the relay and started lifting a moment later, since BENCH_DEBUG never reports the actuator, only phase and reps. A click here means requestStart() ran the old direct-start path instead of opening Confirm - check the display is actually linked (a ping every second keeps linkUp alive; a CYD that is powered but never flashed with the current firmware, or unplugged from the UART jumpers, leaves linkUp false and this step silently becomes the last step in this group instead). Green lit or yellow dark here is the same tell as an audible click - something moved that Confirm should have held still."
    }
    [pscustomobject]@{
        Name    = "a second START commits"
        # Any press of START while phase=CONFIRM commits it - confirmStart()
        # does not care whether this is literally the second physical press of
        # the checklist, only that the machine is currently on the confirm
        # screen (session.cpp).
        Do      = "with phase=CONFIRM still open from the previous step, touch GPIO 25 again. This time you should hear the relay click and see green light, yellow go dark - the same picture the earlier START step described, now on the far side of the confirm screen instead of straight from idle"
        Expect  = @('EDGE START', 'phase=LIFT')
        Hint    = "No click and phase stays CONFIRM: confirmStart() never ran, which given the previous step just passed means this press was not read at all - check the button wiring and that START is not reading stuck-pressed since the first touch. CONFIRM reopens instead of LIFT: the machine backed out to IDLE on its own (the 30 s timeout tested two steps from now) in the gap between the two presses, and this one opened a fresh confirm screen rather than committing one - repeat the two presses closer together."
    }
    [pscustomobject]@{
        Name    = "STOP cancels CONFIRM"
        Do      = "settle whatever the previous step left running: touch GPIO 26 and wait for phase=IDLE. Then, with the display still linked, touch GPIO 25 to reopen CONFIRM, and this time touch GPIO 26 instead of GPIO 25 - the confirm screen should cancel straight back to IDLE. Watch and listen through the whole cancel the way the first CONFIRM step asked: no relay click, no LIFT, lamps never leave the idle picture"
        Expect  = @('EDGE START', 'phase=CONFIRM', 'EDGE STOP', 'phase=IDLE')
        Hint    = "The four Expect patterns above pass whether or not a click happened in between - they only prove CONFIRM was entered and IDLE was reached eventually, the same click-blindness the first CONFIRM step's hint describes. The panel STOP button is wired to cancelConfirm() directly while CONFIRM is open (pt-leg-controller.ino) and, unlike an idle STOP, never touches the rep count - so reps in the final phase=IDLE line should read whatever it already was, not necessarily 0."
    }
    [pscustomobject]@{
        Name       = "CONFIRM expires after 30 s"
        # CONFIRM_TIMEOUT_MS is exactly 30000 ms, measured by the firmware's
        # own clock from the moment Confirm was entered. Wait-ForPatterns
        # starts ITS deadline the instant this step begins, which is before
        # the operator has even read the Do line below and pressed START -
        # so against the shared -StepTimeoutSec default (30) this step's own
        # window always closes before the firmware's does. That is not an
        # occasional flake, it is a guaranteed FAIL at the default budget
        # every single time, which is worse: a checklist step that always
        # fails teaches whoever is running it to discount failures - the
        # last thing a hand-run safety checklist should do. TimeoutSec below
        # gives this one step alone a wider budget than the rest of the
        # checklist; every other step keeps the shared default.
        # [TH] CONFIRM_TIMEOUT_MS อยู่ที่ 30000 มิลลิวินาทีพอดี นับจากนาฬิกาของเฟิร์มแวร์เองตอนที่เข้าสถานะ
        #      รอยืนยัน ส่วน Wait-ForPatterns เริ่มนับเวลาของตัวเองตั้งแต่ขั้นนี้เริ่ม ซึ่งเป็นช่วงก่อนที่ผู้ใช้
        #      จะอ่านบรรทัด Do ด้านล่างและกด START ด้วยซ้ำ เทียบกับงบเวลา -StepTimeoutSec ที่ใช้ร่วมกัน (30)
        #      หน้าต่างเวลาของขั้นนี้จึงปิดก่อนของเฟิร์มแวร์เสมอ ไม่ใช่แค่คลาดเคลื่อนเป็นบางครั้ง แต่พังทุกครั้งที่รันด้วย
        #      งบเริ่มต้น ซึ่งแย่กว่านั้น เพราะขั้นตรวจสอบที่พังทุกครั้งจะสอนให้คนที่รันมองข้ามความล้มเหลวไป
        #      ซึ่งเป็นสิ่งสุดท้ายที่รายการตรวจสอบด้านความปลอดภัยที่รันด้วยมือควรทำ TimeoutSec ด้านล่างให้งบเวลา
        #      ที่กว้างกว่าขั้นอื่นในรายการเฉพาะขั้นนี้ขั้นเดียว ขั้นอื่นยังใช้ค่าเริ่มต้นที่ใช้ร่วมกันตามเดิม
        TimeoutSec = 40   # 30 s CONFIRM window + slack for reading this line and pressing START
        Do         = "with phase=IDLE and the display linked, touch GPIO 25 once to open CONFIRM, then touch nothing at all - no STOP, no second START - and wait. The confirm screen should back out to IDLE by itself, with no EDGE line in front of that phase=IDLE: nobody pressed anything, tick() alone drove it"
        Expect     = @('EDGE START', 'phase=CONFIRM', 'phase=IDLE')
        Hint       = "A STOP or a second START anywhere in the wait changes what this step is actually testing - cancelling or committing before the clock runs out only re-proves the two steps already covered above, not the timeout. If phase=CONFIRM never yields to phase=IDLE at all even with this step's own wider budget, the 30 s window itself is not firing - check CONFIRM_TIMEOUT_MS in session.h is still 30000 and that the board is not sitting in a debugger breakpoint or otherwise stalling millis()."
    }
    [pscustomobject]@{
        Name    = "link down: START skips CONFIRM"
        # The failure mode this guards against is the opposite of every step
        # above: a machine stuck behind a confirm screen with no way to
        # answer it, because the display that would show it is not there.
        Do      = "pull the UART jumpers (or power off the CYD) and wait at least 2 s past its last ping - link-protocol.md's UART-loss window, the same 2 s the mid-session safe-stop uses, is what requestStart() reads as linkUp too. Then, with phase=IDLE, touch GPIO 25 to GND briefly. This should behave exactly like the very first START step in this checklist again: one relay click, green lit, yellow dark, and phase=LIFT with no CONFIRM in between - the same as running this whole bench with no display ever attached at all"
        Expect  = @('EDGE START', 'phase=LIFT')
        Hint    = "phase=CONFIRM appears here instead: either the jumpers are still seated, the CYD is still powered and pinging, or GPIO 25 was touched inside that 2 s window before the link actually went stale - wait it out fully and try again. This is the same pair of patterns the very first START step checked with no display ever in the picture; the only thing this step adds is proving a display that WAS linked, and then dropped, does not leave the machine waiting on a confirm screen nobody can answer."
    }
)

$results = @()
try {
    Write-Host ""
    Write-Host "controller button bench on $Port" -ForegroundColor Green
    Write-Host "buttons are NO: leave open = at rest, touch to GND = press" -ForegroundColor Green
    Write-Host "watch the lamps throughout - only a latched FAULT or jog mode may blink" -ForegroundColor Green
    Write-Host "$($steps.Count) steps, ${StepTimeoutSec}s each. Ctrl+C to abort." -ForegroundColor Green

    $n = 0
    foreach ($step in $steps) {
        $n++
        Write-Host ""
        Write-Host "[$n/$($steps.Count)] $($step.Name)" -ForegroundColor White
        Write-Host "      do: $($step.Do)" -ForegroundColor Cyan
        if ($n -gt 1) { Clear-Serial }
        # Most steps share the one -StepTimeoutSec budget. A step whose own
        # Do instructions require riding out a fixed firmware timer (CONFIRM's
        # 30 s expiry) needs more than that, and needs it every time, not just
        # on a slow reaction - see that step's own comment for why the shared
        # default guarantees a FAIL there. TimeoutSec on the step object
        # overrides the shared budget for exactly that one step; every step
        # without it keeps using $StepTimeoutSec exactly as before. Still run
        # through the same DryRun clamp as the shared budget, so a step that
        # regresses in dry-run mode fails fast instead of spinning for
        # whatever wider timeout it was given.
        # [TH] ส่วนใหญ่ใช้งบเวลา -StepTimeoutSec ร่วมกัน แต่ขั้นที่คำสั่งของมันเองต้องรอให้ครบตัวจับเวลาคงที่ของ
        #      เฟิร์มแวร์ (30 วินาทีของ CONFIRM) ต้องการมากกว่านั้น และต้องการทุกครั้ง ไม่ใช่แค่ตอนตอบสนองช้า
        #      ดูคอมเมนต์ของขั้นนั้นเองว่าทำไมงบเริ่มต้นที่ใช้ร่วมกันถึงพังแน่นอน TimeoutSec บนตัวขั้นเองจะแทนที่
        #      งบเวลาร่วมเฉพาะขั้นนั้นขั้นเดียว ขั้นที่ไม่มีค่านี้ยังใช้ $StepTimeoutSec เหมือนเดิมทุกประการ
        #      ยังผ่านตัวคุมของ DryRun เดียวกันกับงบเวลาร่วม เพื่อให้ขั้นที่พังตอน dry-run ยังพังเร็วเหมือนเดิม
        #      แทนที่จะหมุนรอจนครบเวลาที่กว้างกว่าซึ่งได้รับมา
        $timeout = if ($step.PSObject.Properties['TimeoutSec']) { $step.TimeoutSec } else { $StepTimeoutSec }
        if ($DryRun) { $timeout = [Math]::Min($timeout, 3) }
        $r = Wait-ForPatterns $step.Expect $timeout
        if ($r.Ok) {
            Write-Host "      PASS" -ForegroundColor Green
        } else {
            Write-Host "      FAIL - never saw: $($r.Missing -join ' | ')" -ForegroundColor Red
            # Name the real cause when the board is running somebody else's image:
            # the generic hint would send you off rebuilding a flag that was never
            # the problem.
            $foreign = $null
            foreach ($sig in $script:foreignFirmware) {
                if (@($script:seenLines) -match $sig.Pattern) { $foreign = $sig.What; break }
            }
            if ($foreign) {
                Write-Host "      This board is running $foreign, not pt-leg-controller." -ForegroundColor Yellow
                Write-Host "      Erase it and flash the controller (see firmware/README.md):" -ForegroundColor Yellow
                Write-Host "        esptool --chip esp32 --port $Port --baud 921600 erase_flash" -ForegroundColor Yellow
                Write-Host "        arduino-cli compile --fqbn esp32:esp32:esp32:PartitionScheme=huge_app ``" -ForegroundColor Yellow
                Write-Host "          --build-property compiler.cpp.extra_flags=-DBENCH_DEBUG=1 --clean ``" -ForegroundColor Yellow
                Write-Host "          --upload -p $Port firmware/pt-leg-controller" -ForegroundColor Yellow
            } else {
                Write-Host "      $($step.Hint)" -ForegroundColor DarkYellow
            }
        }
        $results += [pscustomobject]@{ Step = $step.Name; Pass = $r.Ok; Missing = ($r.Missing -join ' | ') }
        # Later steps all read the same debug stream, so once it is missing they
        # would just burn the timeout each and bury the one failure that matters.
        if (-not $r.Ok -and $step.Fatal) {
            Write-Host ""
            Write-Host "      stopping: the remaining $($steps.Count - $n) steps all need that debug output" -ForegroundColor Yellow
            break
        }
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
