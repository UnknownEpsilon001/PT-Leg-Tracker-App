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

# [TH] คำนวณค่าตรวจสอบแบบ CRC-16/CCITT-FALSE ให้ตรงกับ crc16() ใน link.cpp
# CRC-16/CCITT-FALSE, matching crc16() in link.cpp. The display drops any line
# without a valid checksum, so this script has to speak it too or the bench
# harness goes silent against real firmware.
function Get-LinkCrc([string]$text) {
    $crc = 0xFFFF
    foreach ($b in [System.Text.Encoding]::ASCII.GetBytes($text)) {
        $crc = $crc -bxor ([int]$b -shl 8)
        for ($i = 0; $i -lt 8; $i++) {
            if ($crc -band 0x8000) { $crc = (($crc -shl 1) -bxor 0x1021) -band 0xFFFF }
            else { $crc = ($crc -shl 1) -band 0xFFFF }
        }
    }
    return $crc
}

function Send-Line([string]$json) {
    $line = "{0}*{1:X4}" -f $json, (Get-LinkCrc $json)
    $sp.WriteLine($line)
    Write-Host "-> $line" -ForegroundColor DarkGray
}

$settings = '{"t":"settings","ssid":"ClinicWiFi","pass":"secret123",' +
            '"server":"http://192.168.0.12:8000","code":"KNEE-01",' +
            '"maxtravel":8,"lift":7,"lower":7,"hold":10,"rest":10,"target":10}'

# phase ordinals per link-protocol.md: 0 Idle 1 Lift 2 Hold 3 Lower 4 Rest 5 SafeLower 6 Fault
$running = $false
$phase = 0
$elapsed = 0
$reps = 0
$target = $Target
$phaseLeft = 0
$sessionStart = $null

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
    Write-Host "keys: s=start  x=stop  r=stop-while-idle (clear reps)  q=quit" -ForegroundColor Cyan
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
            # [TH] ข้อความที่มีรหัสต้องตอบรับ ไม่งั้นจอจะส่งซ้ำจนเลิกส่งแล้วนับว่าหาย
            # Anything carrying an id must be acknowledged, or the display retries
            # it eight times and then counts it as dropped — a SAVE would report
            # itself lost even though this script acted on it.
            if ($line -match '"id"\s*:\s*(\d+)') {
                Send-Line ('{{"t":"ack","id":{0}}}' -f $Matches[1])
            }
            # The settings the display keeps re-sending carry an id; strip it so
            # the copy echoed back does not replay the display's own id at it.
            # [TH] ค่าตั้งค่าที่จอส่งมามีรหัสติดมาด้วย ต้องตัดออกก่อนส่งกลับ
            #      ไม่งั้นจะเป็นการส่งรหัสของจอเองย้อนกลับไปหาจอ
            $line = $line -replace ',"id":\d+', '' -replace '\*[0-9A-Fa-f]{4}$', ''
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
                        if (-not $running) {
                            $running = $true; $phase = 1; $phaseLeft = $LiftSec
                            $reps = 0; $sessionStart = Get-Date
                            Write-Host "   START (key)" -ForegroundColor Green
                        } else {
                            Write-Host "   START ignored (already running)" -ForegroundColor DarkYellow
                        }
                    }
                    'x' {
                        if ($running -and $phase -ne 5) {
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

        $state = '{{"t":"state","running":{0},"phase":{1},"elapsed":{2},"reps":{3},"target":{4},' +
                 '"wifi":true,"server":true}}'
        # Through Send-Line, not WriteLine: the state frame needs the checksum
        # like every other message, and it is the one the display acts on most.
        # [TH] ส่งผ่าน Send-Line ไม่ใช่ WriteLine เพราะข้อความสถานะก็ต้องมีค่าตรวจสอบเหมือนข้อความอื่น
        #      และเป็นข้อความที่ฝั่งจอใช้งานมากที่สุด
        Send-Line ([string]::Format($state, $running.ToString().ToLower(), $phase, $elapsed, $reps, $target))
        Start-Sleep -Milliseconds 250
    }
}
finally {
    $sp.Close()
    Write-Host "closed $Port" -ForegroundColor DarkGray
}
