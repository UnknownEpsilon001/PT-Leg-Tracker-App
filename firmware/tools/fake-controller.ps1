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
# LIFT -> HOLD 10 s -> LOWER -> REST 10 s, repeating. Ctrl+C stops, and the display
# should fall back to ไม่พบตัวควบคุม (no link) about 2.5 s later.
# Shorten a phase for a quicker watch with -HoldSec / -RestSec.
#
# Ending a session (x, or -Seconds running out mid-stop) sends `done`, the same
# message the real controller emits, so the finish screen and the SD row can be
# exercised with only the CYD attached. With no card in the slot the summary
# should read "ยังไม่ได้บันทึก รอการ์ด" and the row waits in RAM; -Epoch 0
# impersonates a controller with no RTC, which logs its datetime as "?".
# [TH] ตอนจบรอบจะส่งข้อความ done เหมือนคอนโทรลเลอร์จริง ใช้ทดสอบหน้าสรุปและการบันทึกลงการ์ดได้
#      ด้วยจอ CYD ตัวเดียว

param(
    [string]$Port = "",     # empty = auto-detect the ESP32's USB-serial port
    [int]$HoldSec = 10,     # matches the real device (lift -> hold 10 s -> lower -> rest 10 s)
    [int]$RestSec = 10,
    [int]$TravelSec = 2,
    [int]$Seconds = 0,      # 0 = run until Ctrl+C
    [switch]$AutoStart,     # begin a session immediately, without waiting for a touch
    # Epoch put in the `done` message at session end. 0 impersonates a controller
    # whose RTC is missing or unset, which is the case that logs a datetime of "?".
    # -Epoch -1 means "use the PC's clock", i.e. a controller with a good RTC.
    # [TH] เวลาที่ใส่ในข้อความ done ตอนจบรอบ: 0 = เหมือนไม่มีนาฬิกา, -1 = ใช้เวลาจริงของเครื่องพีซี
    [long]$Epoch = -1,
    # Ends the session with fault=true, the way the controller reports a session
    # that hit a safety net: a stroke that ran past maxTravelSec with no limit
    # closing, both limits reading pressed at once, an actuator cap trip, or a
    # safe-lower that never reached the bottom switch. A limit switch closing is
    # the normal path, not a fault. The finish screen should read the fault
    # wording and the SD row should carry fault=1.
    # [TH] จบรอบด้วย fault=true เหมือนตอนสวิตช์จำกัดระยะทำงาน หน้าสรุปต้องขึ้นข้อความเตือน
    #      และแถวในไฟล์ต้องมี fault=1
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
                $phase = 5; $phaseLeft = $TravelSec
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
                    5 {
                        # [TH] จบรอบ: ส่งข้อความ done เหมือนคอนโทรลเลอร์จริง จอจะได้บันทึกและขึ้นหน้าสรุป
                        # SafeLower -> Idle. The real controller emits `done` on this
                        # edge and the display logs it, so the bench must too -
                        # otherwise the finish screen and the SD row never appear here.
                        # epoch: -Epoch 0 stands in for a controller with no RTC, which
                        # is the case that logs a datetime of "?".
                        $running = $false; $phase = 0
                        $ep = if ($Epoch -lt 0) { [DateTimeOffset]::UtcNow.ToUnixTimeSeconds() }
                              else { $Epoch }
                        $done = '{{"t":"done","elapsed":{0},"reps":{1},"epoch":{2},"fault":{3}}}'
                        Send-Line ([string]::Format($done, $elapsed, $reps, $ep,
                                   $Fault.ToString().ToLower()))
                        $elapsed = 0
                    }
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
