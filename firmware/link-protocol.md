# UART link protocol (controller ↔ CYD display)

Newline-delimited JSON, one message per line, over a dedicated UART (controller
`Serial2` on GPIO 16 RX / 17 TX ↔ CYD `Serial2` on GPIO 22 RX / 27 TX,
cross-wired RX↔TX with a common GND). Both ends 115200 8N1.

`println(json)` to send; the reader accumulates until `\n` and parses with
ArduinoJson. Malformed lines are dropped. Lines over 512 chars reset the buffer.

## Display → Controller

- `{"t":"settings","ssid":..,"pass":..,"server":..,"code":..,"maxtravel":..,"hold":..,"rest":..}`
  — save from the settings screen. The controller clamps `maxtravel`/`hold`/`rest`
  to 1-20/1-60/1-60, then persists to NVS and reconnects Wi-Fi. A message that
  arrives mid-session is held until the session ends — the NVS commit and the
  Wi-Fi restart must not happen with a relay energized — so a SAVE during a
  session takes effect once the leg is down.
- `{"t":"hello"}` — CYD boot; asks the controller for current settings + state.
- `{"t":"ping"}` — 1 Hz keepalive that feeds the controller's UART watchdog.

## Controller → Display

- `{"t":"state","running":bool,"phase":int,"elapsed":int,"reps":int,"wifi":bool,"server":bool}`
  — ~4 Hz.
- `{"t":"settings", ...current...}` — reply to `hello`, so the CYD settings
  screen pre-fills and the main screen can show the device code.
- `{"t":"done","elapsed":int,"reps":int,"epoch":unixUTC|0,"fault":bool}` — sent
  once at session end (clean stop or FAULT); the display logs it to SD. `epoch`
  is UTC seconds from the controller's clock — the DS3231 first, the SNTP system
  clock if the RTC is absent or unset — and 0 when neither has a time. See Notes
  for what that means for a datetime read off the card.

## Notes

- `phase` is the `Phase` enum ordinal: 0=Idle, 1=Lift, 2=Hold, 3=Lower, 4=Rest,
  5=SafeLower, 6=Fault.
- `done.epoch` is UTC seconds from the controller's DS3231, falling back to the
  SNTP-synced system clock when the RTC is absent or unset, and 0 when neither
  has a time. A datetime on the card is therefore not proof of battery-backed
  hardware: on a machine with no RTC fitted it came from the network, and it is
  `?` whenever that machine booted offline.
- `done.fault` is latched when the session ends, not read back off the live
  phase, and it is also true for a safe-lower that ran its cap out without the
  leg ever reaching the bottom switch — the phase ends Idle by design, but the
  session was not clean.
- `maxtravel` replaced the old `lift`/`lower` timings when travel became
  limit-switch terminated; it is the per-stroke safety cap in seconds.
- Run control (START/STOP/RESET) is on physical NC panel buttons wired to the
  controller, so it no longer travels over UART.
- UART loss is still a safety event: if the controller sees no line for ~2 s
  during a session it safe-lowers and stops, as defense-in-depth (the physical
  STOP remains the primary stop).
- That watchdog is armed by the first line ever received, not by boot. A
  controller that has never heard from a display is running standalone — the
  bench setup in `firmware/README.md`, or a machine with the CYD unplugged —
  and stops on its panel STOP button instead. Arming it from boot would make
  "silent for 2 s" true from the first instant on those machines, and no session
  could start at all.
