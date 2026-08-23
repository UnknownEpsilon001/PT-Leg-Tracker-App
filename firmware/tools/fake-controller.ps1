# [TH] สคริปต์สวมรอยเป็นบอร์ดคอนโทรลเลอร์ เพื่อทดสอบ UI บนจอ CYD โดยไม่ต้องมีบอร์ดที่สอง
#      พูดโปรโตคอล JSON คั่นด้วยขึ้นบรรทัดใหม่แบบเดียวกับ firmware/link-protocol.md
#      ตอบ hello ด้วยค่าตั้งค่า แล้วจำลองรอบการทำงาน ยก -> ค้าง -> ลง -> พัก วนไป
#
# Impersonates the controller so the CYD's UI can be exercised with no second
# board on the desk. Speaks the same newline-delimited JSON as
# firmware/link-protocol.md.
#
# Flash the display with the link routed to USB first. Flip LINK_OVER_USB to 1 in
# the sketch source — NOT with --build-property, which leaves cached objects
# compiled without the flag and has produced broken images (see firmware/README.md):
#   arduino-cli compile --fqbn "esp32:esp32:esp32:PartitionScheme=huge_app" `
#     --clean --upload -p COM4 firmware/pt-leg-display
#
# Then (port auto-detected from the USB-serial chip; pass -Port COMx to override):
#   .\firmware\tools\fake-controller.ps1
#
# Settings the display saves are kept for the rest of the run, standing in for the
# controller's NVS, so an edit sticks instead of snapping back. They are not
# written to disk: restart the script and the defaults below are back.
# [TH] ค่าตั้งค่าที่จอบันทึกจะถูกจำไว้จนกว่าสคริปต์จะปิด แทนหน่วยความจำ NVS ของคอนโทรลเลอร์
#      แต่ไม่ได้บันทึกลงไฟล์ ถ้าเริ่มสคริปต์ใหม่จะกลับเป็นค่าตั้งต้น
#
# It answers `hello` with a settings reply, then runs the device's real cycle:
# LIFT -> HOLD 10 s -> LOWER -> REST 10 s, repeating until the rep count reaches
# the target (0 = until stopped). Ctrl+C stops, and the display should fall back
# to ไม่พบตัวควบคุม (no link) about 2.5 s later. Shorten a phase for a quicker
# watch with -LiftSec / -HoldSec / -LowerSec / -RestSec.
#
# There are no limit switches any more — travel is timed. LIFT always drives for
# LiftSec and LOWER (including a safe-lower) always drives for LowerSec, matching
# SessionSM::tick(): a timed stroke runs its full duration, it does not end early.
#
# Ending a session (x, the target being reached, or -Seconds running out mid-stop)
# sends `done`, the same message the real controller emits, so the finish screen
# and the SD row can be exercised with only the CYD attached. With no card in the
# slot the summary should read "ยังไม่ได้บันทึก รอการ์ด" and the row waits in RAM;
# -Epoch 0 impersonates a controller with no RTC, which logs its datetime as "?".
# [TH] ตอนจบรอบจะส่งข้อความ done เหมือนคอนโทรลเลอร์จริง ใช้ทดสอบหน้าสรุปและการบันทึกลงการ์ดได้
#      ด้วยจอ CYD ตัวเดียว

param(
    [string]$Port = "",     # empty = auto-detect the ESP32's USB-serial port
    [int]$LiftSec = 7,      # matches the real device's default liftSec/lowerSec
    [int]$LowerSec = 7,
    [int]$HoldSec = 10,     # matches the real device (lift -> hold 10 s -> lower -> rest 10 s)
    [int]$RestSec = 10,
    [int]$Target = 10,      # matches DEFAULT_TARGET_REPS; 0 = run until stopped
    [int]$Seconds = 0,      # 0 = run until Ctrl+C
    [switch]$AutoStart,     # begin a session immediately, without waiting for a touch
    # Epoch put in the `done` message at session end. 0 impersonates a controller
    # whose RTC is missing or unset, which is the case that logs a datetime of "?".
    # -Epoch -1 means "use the PC's clock", i.e. a controller with a good RTC.
    # [TH] เวลาที่ใส่ในข้อความ done ตอนจบรอบ: 0 = เหมือนไม่มีนาฬิกา, -1 = ใช้เวลาจริงของเครื่องพีซี
    [long]$Epoch = -1,
    # Ends a STOPPED session (x, or -Seconds running out) with fault=true, the way
    # the controller reports one that hit the actuator's energize cap mid-LIFT or
    # mid-LOWER — the only thing that raises FAULT now that the limit switches are
    # gone. Never applied to a session that ends itself by reaching the rep target:
    # that is always a clean stop in the real firmware too. The finish screen
    # should read the fault wording and the SD row should carry fault=1.
    # [TH] จบรอบด้วย fault=true เหมือนตอนเพดานเวลาของชุดขับเคลื่อนตัดกลางจังหวะยกหรือลง
    #      ซึ่งเป็นสาเหตุเดียวที่เหลืออยู่ของ FAULT เมื่อไม่มีลิมิตสวิตช์แล้ว ไม่ใช้กับรอบที่จบเองเพราะถึงเป้าหมาย
    #      หน้าสรุปต้องขึ้นข้อความเตือนและแถวในไฟล์ต้องมี fault=1
    [switch]$Fault
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
            '"maxtravel":8,"lift":7,"lower":7,"hold":10,"rest":10,"target":10}'

# phase ordinals per link-protocol.md: 0 Idle 1 Lift 2 Hold 3 Lower 4 Rest 5 SafeLower 6 Fault 7 Confirm
$running = $false
$phase = 0
$elapsed = 0
$reps = 0
$target = $Target
$phaseLeft = 0
$sessionStart = $null

# The number on the confirm screen while phase=7 -- seeded from $target when
# Confirm is opened, edited in place by an inbound `sets`, and never written
# back to $target unless the operator actually confirms. Kept separate from
# $target the same way the real controller keeps pendingTargetReps separate
# from cfg.targetReps: a cancelled or expired edit must leave the stored
# number untouched. $confirmDeadline is the wall-clock moment the real
# CONFIRM_TIMEOUT_MS (30 s) elapses; $null outside phase 7.
# [TH] ตัวเลขบนหน้ายืนยันขณะ phase=7 ตั้งต้นจาก $target ตอนเปิดหน้า แล้วแก้ในตัวมันเองเมื่อมีข้อความ
#      sets เข้ามา และจะไม่ถูกเขียนกลับไปที่ $target เว้นแต่ผู้ใช้กดยืนยันจริง แยกจาก $target
#      เหมือนที่คอนโทรลเลอร์จริงแยก pendingTargetReps จาก cfg.targetReps
#      เพื่อไม่ให้การยกเลิกหรือหมดเวลาไปแตะค่าที่บันทึกไว้ $confirmDeadline คือเวลาจริงที่ครบ 30 วินาที
#      ตามค่า CONFIRM_TIMEOUT_MS ของเครื่องจริง เป็น $null นอกเหนือจาก phase 7
$pendingReps = $Target
$confirmDeadline = $null

# One shot at session end, shared by a target-reached stop (never a fault, the
# same as SessionSM::tick()'s Rest-phase check) and an operator/deadline stop
# (may be a fault, when -Fault was passed).
# [TH] จุดเดียวที่ส่งข้อความ done ใช้ร่วมกันระหว่างรอบที่จบเองเพราะถึงเป้าหมาย (ไม่ใช่ fault เหมือนของจริง)
#      กับรอบที่หยุดโดยผู้ใช้หรือหมดเวลา (เป็น fault ได้ถ้าส่ง -Fault มา)
function Send-Done([bool]$isFault) {
    $ep = if ($Epoch -lt 0) { [DateTimeOffset]::UtcNow.ToUnixTimeSeconds() } else { $Epoch }
    $done = '{{"t":"done","elapsed":{0},"reps":{1},"epoch":{2},"fault":{3}}}'
    Send-Line ([string]::Format($done, $elapsed, $reps, $ep, $isFault.ToString().ToLower()))
}

# Shared by both ways the confirm screen can be committed -- the inbound
# `confirm ok:true` message and a press of the panel's own START button (the
# 's' key) once phase=7. $via only labels which one it was, for the log line;
# $script: prefixes are needed here (unlike the rest of this file) because
# this runs inside a function's own scope, not inline in the main loop.
# Bench caveat, and it points the wrong way: this simulator keeps ONE target
# variable where the controller keeps two -- cfg.targetReps, the stored
# prescription, and sessionTargetReps, this session only -- and a real confirm
# writes only the second. Committing here assigns $script:target, so the
# simulator's stored target DOES move on confirm and keeps the confirmed number
# after the session ends. The session-only scoping therefore cannot be
# demonstrated against this script; checking it here answers the opposite of the
# truth. Verify it on real hardware instead: set the default to 10, confirm 12,
# run, and read the settings screen back.
# [TH] ใช้ร่วมกันระหว่างสองทางที่ยืนยันหน้ารอยืนยันได้ คือข้อความ confirm ok:true ที่มาทางลิงก์ และการกดปุ่ม
#      START บนหน้าปัดเอง (ปุ่ม s) เมื่ออยู่ phase=7 $via ใช้แค่บอกที่มาของการยืนยันในบรรทัด log ต้องใส่
#      $script: นำหน้า (ต่างจากส่วนอื่นของไฟล์นี้) เพราะโค้ดนี้ทำงานในสโคปของฟังก์ชันเอง ไม่ใช่แบบแทรกตรงในลูปหลัก
# [TH] ข้อควรระวังบนโต๊ะทดสอบ และมันชี้ไปคนละทางกับความจริง สคริปต์จำลองนี้มีตัวแปรเป้าหมายตัวเดียว
#      ขณะที่คอนโทรลเลอร์จริงมีสองตัว คือ cfg.targetReps ค่าที่แพทย์สั่ง กับ sessionTargetReps
#      ค่าเฉพาะรอบนี้ และการยืนยันจริงเขียนแค่ตัวหลังเท่านั้น การยืนยันตรงนี้เขียน $script:target
#      เป้าหมายที่เก็บไว้ของสคริปต์จำลองจึงขยับตามการยืนยันจริงๆ และค้างค่าที่ยืนยันไว้หลังรอบจบด้วย
#      เรื่องขอบเขตเฉพาะรอบจึงพิสูจน์กับสคริปต์นี้ไม่ได้ ถ้าไปตรวจกับที่นี่จะได้คำตอบตรงข้ามกับความจริง
#      ให้ตรวจกับฮาร์ดแวร์จริงแทน ตั้งค่าเริ่มต้นเป็น 10 ยืนยัน 12 เดินรอบ แล้วกลับไปอ่านหน้าตั้งค่า
function Commit-Confirm([string]$via) {
    $script:running = $true; $script:phase = 1; $script:phaseLeft = $LiftSec
    $script:reps = 0; $script:sessionStart = Get-Date; $script:target = $script:pendingReps
    $script:confirmDeadline = $null
    Write-Host "   CONFIRM committed via $via -> starting with target=$($script:target)" -ForegroundColor Green
}

# Shared by both ways the confirm screen can be cancelled -- the inbound
# `confirm ok:false` message (and a missing "ok", matching link.cpp's
# `d["ok"] | false`) and a press of the panel's own STOP button (the 'x' key)
# once phase=7. Neither touches $pendingReps or $target: nothing was ever
# committed, matching cancelConfirm() on the real controller.
# [TH] ใช้ร่วมกันระหว่างสองทางที่ยกเลิกหน้ารอยืนยันได้ คือข้อความ confirm ok:false ที่มาทางลิงก์ (หรือไม่มี "ok"
#      เลย ตรงกับ d["ok"] | false ใน link.cpp) และการกดปุ่ม STOP บนหน้าปัดเอง (ปุ่ม x) เมื่ออยู่ phase=7
#      ทั้งสองทางไม่แตะ $pendingReps หรือ $target เลย เพราะไม่มีอะไรถูกผูกมัดไว้ตั้งแต่แรก ตรงกับ cancelConfirm()
#      บนคอนโทรลเลอร์จริง
function Cancel-Confirm([string]$via) {
    $script:phase = 0; $script:confirmDeadline = $null
    Write-Host "   CONFIRM cancelled via $via" -ForegroundColor Green
}

Send-Line $settings

if ($AutoStart) {
    $running = $true; $phase = 1; $phaseLeft = $LiftSec; $reps = 0; $sessionStart = Get-Date
    Write-Host "   AUTOSTART" -ForegroundColor Green
}

# [TH] คุมด้วยแป้นพิมพ์ จำลองปุ่มหน้าปัดของคอนโทรลเลอร์ เพื่อทดสอบการตอบสนองของจอโดยไม่ต้องมีบอร์ดคอนโทรลเลอร์
# Live keyboard control stands in for the controller's panel buttons so the
# display's reaction to each session action can be exercised with only the CYD on
# the desk. s=START (idle only), x=STOP, r=STOP-while-idle's rep clear, q=quit.
# The real panel already has separate START/STOP buttons (GPIO 25/26) now, so s
# and x map onto them directly; r stands in for STOP pressed while idle, which is
# what clears the rep count on real hardware — there is no separate RESET any
# more. This drives the display SIDE of the button feature; it does not test the
# real button GPIO/debounce, which lives in the controller firmware.
# Skipped when input is redirected (e.g. piped/non-interactive) so the loop still
# runs headless with -AutoStart -Seconds.
$interactive = -not [Console]::IsInputRedirected
if ($interactive) {
    Write-Host "keys: s=start (commits while CONFIRM open)  x=stop (cancels while CONFIRM open)  r=stop-while-idle (clear reps)  c=open confirm (idle only)  q=quit" -ForegroundColor Cyan
}

$deadline = if ($Seconds -gt 0) { (Get-Date).AddSeconds($Seconds) } else { [datetime]::MaxValue }

try {
    # The deadline stops the session the way a person would, then waits for the
    # cycle to reach idle, so `done` is sent and the finish screen appears. Exiting
    # the moment the clock runs out would leave the display stuck in RUNNING and no
    # row would ever be logged, which is most of what a headless run is checking.
    # [TH] เมื่อครบเวลาจะสั่งหยุดเหมือนคนกด แล้วรอให้รอบเดินจนว่าง ข้อความ done จึงถูกส่ง
    #      ถ้าออกทันทีที่หมดเวลา จอจะค้างที่สถานะกำลังทำงานและไม่มีการบันทึกแถวใดเลย
    $stopping = $false
    while ($true) {
        if (-not $stopping -and (Get-Date) -ge $deadline) {
            $stopping = $true
            if ($running -and $phase -ne 5) {
                $phase = 5; $phaseLeft = $LowerSec
                Write-Host "   STOP (deadline)" -ForegroundColor Green
            }
        }
        if ($stopping -and -not $running) {
            # The display answers `done` with its own log line (whether the row
            # reached the card or went to the RAM ring), which arrives after the
            # message is sent. Read for a moment more or the run ends blind.
            # [TH] จอจะพิมพ์บรรทัดตอบกลับหลังได้รับ done ว่าบันทึกลงการ์ดหรือพักไว้ในหน่วยความจำ
            #      ต้องรออ่านอีกครู่ ไม่งั้นจะไม่เห็นผลของรอบนั้นเลย
            $drain = (Get-Date).AddSeconds(2)
            while ((Get-Date) -lt $drain) {
                while ($sp.BytesToRead -gt 0) {
                    try { $l = $sp.ReadLine() } catch { break }
                    if ($l) { Write-Host "<- $l" -ForegroundColor DarkCyan }
                }
                Start-Sleep -Milliseconds 50
            }
            break
        }

        # answer anything the display asks for, and react to its buttons
        while ($sp.BytesToRead -gt 0) {
            $line = ""
            try { $line = $sp.ReadLine() } catch { break }
            if (-not $line) { continue }
            Write-Host "<- $line" -ForegroundColor DarkCyan
            if ($line -match '"t"\s*:\s*"hello"') { Send-Line $settings }
            elseif ($line -match '"t"\s*:\s*"settings"') {
                # Keep what the display sent, the way the real controller keeps it
                # in NVS, and answer with that from now on. Replying with the
                # canned set instead made every edit snap back a moment after it
                # was saved, which reads exactly like a broken save button.
                # [TH] เก็บค่าที่จอส่งมาไว้ เหมือนที่คอนโทรลเลอร์จริงเก็บลง NVS แล้วตอบด้วยค่านั้นต่อไป
                #      ถ้าตอบด้วยค่าตั้งต้นเดิม ค่าที่เพิ่งบันทึกจะเด้งกลับทันที ดูเหมือนปุ่มบันทึกเสีย
                $settings = $line
                Write-Host "   (kept the display's settings)" -ForegroundColor Yellow
                Send-Line $settings
            }
            elseif ($line -match '"t"\s*:\s*"target"') {
                # Live rep-target change from the main screen's +/- buttons,
                # applied at once and echoed on the next `state` — never held the
                # way a `settings` save is, matching link-protocol.md.
                # [TH] การเปลี่ยนเป้าหมายแบบสดจากปุ่ม +/- บนหน้าหลัก นำไปใช้ทันทีและส่งค่ากลับไปในข้อความ state
                #      ถัดไป ไม่ถูกพักไว้เหมือนการบันทึกค่าตั้งค่า
                if ($line -match '"reps"\s*:\s*(\d+)') {
                    $target = [int]$Matches[1]
                    Write-Host "   (target set to $target via link)" -ForegroundColor Yellow
                }
            }
            elseif ($line -match '"t"\s*:\s*"sets"') {
                # Pending rep-target edit from the confirm screen. Effective only
                # in phase 7, matching link-protocol.md's "silently ignored
                # otherwise" -- a stray or late one outside Confirm changes
                # nothing. Stored, then echoed back as `target` on the next
                # `state` (see the state line below): that echo is the only way
                # the display's mismatch indicator knows the edit arrived, so an
                # accepted `sets` that is never echoed reproduces the exact bug
                # this handler exists to avoid.
                # [TH] ค่าที่แก้ไขบนหน้ายืนยัน มีผลเฉพาะตอน phase 7 เท่านั้น ตรงกับที่ link-protocol.md
                #      ระบุว่า "เพิกเฉยเงียบๆ" นอกเหนือจากนั้น เก็บค่าไว้แล้วส่งกลับเป็น target ในข้อความ
                #      state ถัดไป (ดูบรรทัด state ด้านล่าง) การสะท้อนกลับนี้คือทางเดียวที่ตัวบ่งชี้ความไม่ตรงกัน
                #      บนจอจะรู้ว่าค่าที่แก้ไปถึงจริง
                if ($phase -eq 7) {
                    if ($line -match '"reps"\s*:\s*(\d+)') {
                        $pendingReps = [int]$Matches[1]
                        Write-Host "   (pending sets = $pendingReps, CONFIRM)" -ForegroundColor Yellow
                    } else {
                        # In Confirm, but the field itself did not parse (missing
                        # or non-numeric "reps") -- a different failure than "not
                        # in CONFIRM", and worth saying so: this one means the
                        # message was malformed, not that it arrived at the wrong
                        # time.
                        # [TH] อยู่ในสถานะรอยืนยันแล้ว แต่ตัวฟิลด์เองแปลงค่าไม่ได้ (ไม่มี "reps" หรือไม่ใช่ตัวเลข)
                        #      เป็นความผิดพลาดคนละแบบกับ "ไม่ได้อยู่ในสถานะรอยืนยัน" ควรบอกให้ต่างกันไว้
                        #      กรณีนี้หมายถึงข้อความมีรูปแบบผิด ไม่ใช่ว่ามาถึงผิดจังหวะ
                        Write-Host "   (sets ignored - in CONFIRM but reps did not parse)" -ForegroundColor DarkYellow
                    }
                } else {
                    Write-Host "   (sets ignored - not in CONFIRM)" -ForegroundColor DarkYellow
                }
            }
            elseif ($line -match '"t"\s*:\s*"confirm"') {
                # ok:true commits $pendingReps and starts the cycle exactly like
                # the 's' key below, except the target is the confirmed value,
                # not $Target. ok:false (and a missing "ok", matching
                # link.cpp's `d["ok"] | false`) cancels back to Idle. Both are
                # no-ops outside phase 7, matching cancelConfirm()/confirmStart()
                # each guarding on Phase::Confirm. Commit-Confirm/Cancel-Confirm
                # are the same functions the 's'/'x' keys call below, so a link
                # message and a panel-button press reach an identical state.
                # [TH] ok:true ยืนยัน $pendingReps แล้วเริ่มรอบเหมือนปุ่ม s ด้านล่าง ต่างกันที่เป้าหมายเป็นค่าที่
                #      ยืนยันแล้ว ไม่ใช่ $Target ส่วน ok:false (หรือไม่มี "ok" เลย ตรงกับ d["ok"] | false ใน
                #      link.cpp) ยกเลิกกลับไปสถานะว่าง ทั้งสองไม่มีผลถ้าไม่ได้อยู่ phase 7 Commit-Confirm กับ
                #      Cancel-Confirm เป็นฟังก์ชันเดียวกับที่ปุ่ม s/x ด้านล่างเรียก ข้อความทางลิงก์กับการกดปุ่ม
                #      หน้าปัดจึงพาไปสู่สถานะเดียวกันเป๊ะ
                if ($phase -eq 7) {
                    if ($line -match '"ok"\s*:\s*true') { Commit-Confirm 'link' }
                    else { Cancel-Confirm 'link' }
                } else {
                    Write-Host "   (confirm ignored - not in CONFIRM)" -ForegroundColor DarkYellow
                }
            }
            # Run control used to travel over UART as a `cmd` message. It moved to
            # physical panel buttons on the controller, the display's main screen
            # is output-only, and link-protocol.md no longer defines the message -
            # so nothing can send it and the branches that handled it are gone.
            # The keyboard handler below is the live stand-in for those buttons.
            # [TH] เดิมคำสั่งเริ่ม/หยุดส่งผ่าน UART เป็นข้อความ cmd
            #      ตอนนี้ย้ายไปเป็นปุ่มจริงบนคอนโทรลเลอร์ หน้าจอหลักของจอแสดงผลอย่างเดียว
            #      และเอกสารโพรโทคอลไม่มีข้อความนี้แล้ว จึงไม่มีอะไรส่งมาได้และลบส่วนที่รับไปแล้ว
            #      ตัวรับปุ่มจากคีย์บอร์ดด้านล่างคือตัวแทนของปุ่มเหล่านั้น
        }

        if ($interactive) {
            while ([Console]::KeyAvailable) {
                $key = [Console]::ReadKey($true).KeyChar
                switch -CaseSensitive ([char]::ToLower($key)) {
                    's' {
                        # A press of the panel's own START button while phase=7
                        # commits Confirm every time, unconditionally - matching
                        # requestStart()'s "if confirming(), confirmStart()"
                        # branch in pt-leg-controller.ino, which is checked
                        # before the branch that cares whether the link is up.
                        # Same commit the inbound `confirm ok:true` handler
                        # above performs; only the source of the press differs.
                        # [TH] กดปุ่ม START บนหน้าปัดเองระหว่าง phase=7 ยืนยันเสมอไม่มีเงื่อนไข ตรงกับสาขา
                        #      "if confirming(), confirmStart()" ของ requestStart() ใน pt-leg-controller.ino
                        #      ซึ่งถูกตรวจก่อนสาขาที่สนใจว่าลิงก์เชื่อมอยู่หรือไม่ เป็นการยืนยันแบบเดียวกับตัวจัดการ
                        #      ข้อความ confirm ok:true ด้านบน ต่างกันแค่ที่มาของการกด
                        if ($phase -eq 7) {
                            Commit-Confirm 'START (key)'
                        } elseif (-not $running) {
                            $running = $true; $phase = 1; $phaseLeft = $LiftSec
                            $reps = 0; $sessionStart = Get-Date
                            Write-Host "   START (key)" -ForegroundColor Green
                        } else {
                            Write-Host "   START ignored (already running)" -ForegroundColor DarkYellow
                        }
                    }
                    'x' {
                        # The panel's own STOP button cancels Confirm outright,
                        # never touching reps - see the panel STOP dispatch in
                        # pt-leg-controller.ino, which wires it to
                        # cancelConfirm() separately from requestStop(). Same
                        # cancel the inbound `confirm ok:false` handler above
                        # performs.
                        # [TH] ปุ่ม STOP บนหน้าปัดเองยกเลิกหน้ายืนยันทันที ไม่แตะจำนวนครั้งเลย ดูจุดจ่ายงานปุ่ม STOP
                        #      บนหน้าปัดใน pt-leg-controller.ino ซึ่งเดินสายไปยัง cancelConfirm() แยกจาก
                        #      requestStop() เป็นการยกเลิกแบบเดียวกับตัวจัดการข้อความ confirm ok:false ด้านบน
                        if ($phase -eq 7) {
                            Cancel-Confirm 'STOP (key)'
                        } elseif ($running -and $phase -ne 5) {
                            $phase = 5; $phaseLeft = $LowerSec   # SafeLower, then idle
                            Write-Host "   STOP (key)" -ForegroundColor Green
                        } else {
                            Write-Host "   STOP ignored (not running)" -ForegroundColor DarkYellow
                        }
                    }
                    'r' {
                        if (-not $running) {          # controller: STOP-while-idle clears reps
                            $reps = 0; $elapsed = 0
                            Write-Host "   STOP-while-idle: reps cleared (key)" -ForegroundColor Green
                        } else {
                            Write-Host "   ignored (running)" -ForegroundColor DarkYellow
                        }
                    }
                    'c' {
                        # Stands in for the panel's own START-from-idle-with-a-
                        # link-up opening Confirm rather than running directly --
                        # see requestStart() in session.cpp. A separate key
                        # rather than folding this into 's': 's' already means
                        # "start now" throughout this script and the real panel
                        # button keeps meaning that too whenever the link is
                        # down, so overloading it here would make the one key
                        # do two different things depending on state that is not
                        # visible at the keyboard.
                        # [TH] แทนปุ่ม START บนหน้าปัดตอนกดจากสถานะว่างขณะมีลิงก์เชื่อมอยู่ ซึ่งจะเปิดหน้ายืนยัน
                        #      แทนที่จะเริ่มทำงานทันที ดู requestStart() ใน session.cpp ใช้ปุ่มแยกจาก s เพราะ s
                        #      หมายถึง "เริ่มเดี๋ยวนี้" มาตลอดทั้งสคริปต์ และปุ่มจริงบนหน้าปัดก็ยังหมายความแบบนั้น
                        #      เสมอเมื่อลิงก์หลุด การใช้ปุ่มเดียวกันจะทำให้ปุ่มนั้นทำสองอย่างต่างกันตามสถานะที่
                        #      มองไม่เห็นจากแป้นพิมพ์
                        if (-not $running -and $phase -eq 0) {
                            $phase = 7; $pendingReps = $target
                            $confirmDeadline = (Get-Date).AddSeconds(30)
                            Write-Host "   CONFIRM opened (key), pending=$pendingReps" -ForegroundColor Green
                        } else {
                            Write-Host "   CONFIRM ignored (not idle)" -ForegroundColor DarkYellow
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
                    2 { $phase = 3; $phaseLeft = $LowerSec }       # Hold  -> Lower
                    3 { $phase = 4; $phaseLeft = $RestSec }        # Lower -> Rest
                    4 {
                        # [TH] พักครบ -> นับหนึ่งครั้ง แล้วตรวจเป้าหมายตรงนี้ที่เดียว เหมือน SessionSM::tick()
                        # Rest -> Lift, or Rest -> Idle if the target was just
                        # reached. Counted and checked here and nowhere else,
                        # matching SessionSM::tick(): Rest is the one point in the
                        # cycle where the leg is already down, so the session can
                        # end on the spot with no safe-lower and no fault.
                        $reps++
                        if ($target -ne 0 -and $reps -ge $target) {
                            $running = $false; $phase = 0
                            Send-Done $false
                            $elapsed = 0
                        } else {
                            $phase = 1; $phaseLeft = $LiftSec
                        }
                    }
                    5 {
                        # [TH] จบรอบ: ส่งข้อความ done เหมือนคอนโทรลเลอร์จริง จอจะได้บันทึกและขึ้นหน้าสรุป
                        # SafeLower -> Idle. The real controller emits `done` on this
                        # edge and the display logs it, so the bench must too -
                        # otherwise the finish screen and the SD row never appear here.
                        $running = $false; $phase = 0
                        Send-Done $Fault.IsPresent
                        $elapsed = 0
                    }
                }
            }
        }

        if ($phase -eq 7 -and $confirmDeadline -and (Get-Date) -ge $confirmDeadline) {
            # CONFIRM_TIMEOUT_MS on the real controller: nobody answered in 30 s,
            # so it backs out to Idle on its own -- see SessionSM::tick()'s
            # Phase::Confirm case. Outside the $running block above because
            # Confirm is deliberately not running() on the real firmware either
            # (session.h), so it needs its own clock instead of riding
            # $phaseLeft. Nothing was committed while it waited, matching that
            # nothing here touches $target.
            # [TH] ตรงกับ CONFIRM_TIMEOUT_MS ของคอนโทรลเลอร์จริง: ไม่มีใครตอบภายใน 30 วินาที จึงกลับไปสถานะว่างเอง
            #      ดู case Phase::Confirm ของ SessionSM::tick() อยู่นอกบล็อก $running ด้านบนเพราะสถานะรอยืนยัน
            #      ไม่นับเป็น running() บนเครื่องจริงเช่นกัน (session.h) จึงต้องมีนาฬิกาของตัวเองแทนที่จะอาศัย
            #      $phaseLeft ไม่มีอะไรถูกผูกมัดระหว่างที่รอ ตรงกับที่ตรงนี้ก็ไม่แตะ $target เช่นกัน
            $phase = 0; $confirmDeadline = $null
            Write-Host "   CONFIRM expired (30s)" -ForegroundColor Green
        }

        # target() on the real controller reports pendingTargetReps while
        # phase=Confirm, not cfg.targetReps -- see session.h. Mirrored here so
        # the display's mismatch indicator sees its edit echoed back the same
        # way it would from real hardware.
        # [TH] target() บนคอนโทรลเลอร์จริงรายงาน pendingTargetReps ระหว่าง phase=Confirm ไม่ใช่ cfg.targetReps
        #      ดู session.h ทำตามที่นี่ด้วย เพื่อให้ตัวบ่งชี้ความไม่ตรงกันบนจอเห็นค่าที่แก้สะท้อนกลับมาเหมือนของจริง
        $reportedTarget = if ($phase -eq 7) { $pendingReps } else { $target }
        $state = '{{"t":"state","running":{0},"phase":{1},"elapsed":{2},"reps":{3},"target":{4},' +
                 '"wifi":true,"server":true}}'
        $sp.WriteLine([string]::Format($state, $running.ToString().ToLower(), $phase, $elapsed, $reps, $reportedTarget))
        Start-Sleep -Milliseconds 250
    }
}
finally {
    $sp.Close()
    Write-Host "closed $Port" -ForegroundColor DarkGray
}
