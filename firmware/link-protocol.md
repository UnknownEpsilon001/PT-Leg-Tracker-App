# UART link protocol (controller ↔ CYD display)

Newline-delimited JSON, one message per line, over a dedicated UART (controller
`Serial2` on GPIO 16 RX / 17 TX ↔ CYD `Serial2` on GPIO 22 RX / 27 TX,
cross-wired RX↔TX with a common GND). Both ends 115200 8N1.

`println(json)` to send; the reader accumulates until `\n` and parses with
ArduinoJson. Malformed lines are dropped. Lines over 512 chars reset the buffer.

## Display → Controller

- `{"t":"settings","ssid":..,"pass":..,"server":..,"code":..,"maxtravel":..,"lift":..,"lower":..,"hold":..,"rest":..,"target":..}`
  — save from the settings screen. The controller clamps `maxtravel` to 1-20,
  `lift`/`lower` to 1-`maxtravel`, `hold`/`rest` to 1-60 and `target` to 0-99,
  then persists to NVS and reconnects Wi-Fi. A message that arrives mid-session
  is held until the session ends — the NVS commit and the Wi-Fi restart must not
  happen with a relay energized — so a SAVE during a session takes effect once
  the leg is down.

  **A field this message omits means "keep whatever the controller already
  has", not "set this to zero" — this matters because the two boards are
  flashed separately, so a display running an image older than a given field
  can send a `settings` message without it.** The two fields use different
  sentinels for "absent" because they have different legal ranges:
  - `maxtravel`, `lift`, `lower`, `hold`, `rest` — none of these has a legal
    value of 0, so a plain `0` (or an omitted key, which ArduinoJson also
    reads as `0`) means "absent, keep current".
  - `target` — 0 is legal (run until STOP), so it cannot double as the
    "absent" sentinel without losing that value. The controller instead
    decodes an omitted key as the out-of-range value `0xFFFF`
    (`LINK_TARGET_ABSENT` in `link.h`) and treats that as "absent, keep
    current". A real `target` is always 0-99, so `0xFFFF` can never collide
    with one.
- `{"t":"target","reps":n}` — live rep-target change from the main screen.
  Applied immediately, including mid-session; the NVS write is deferred to the
  end of the session. Unlike `settings` it is never held, because that path
  commits NVS and restarts Wi-Fi.
- `{"t":"hello"}` — CYD boot; asks the controller for current settings + state.
- `{"t":"ping"}` — 1 Hz keepalive that feeds the controller's UART watchdog.

## Controller → Display

- `{"t":"state","running":bool,"phase":int,"elapsed":int,"reps":int,"target":int,"wifi":bool,"server":bool}`
  — ~4 Hz.
- `{"t":"settings", ...current...}` — reply to `hello`, so the CYD settings
  screen pre-fills and the main screen can show the device code. Carries the
  same fields as the `settings` message above, including `lift`, `lower` and
  `target`.
- `{"t":"done","elapsed":int,"reps":int,"epoch":unixUTC|0,"fault":bool}` — sent
  once at session end (clean stop or FAULT); the display logs it to SD. `epoch`
  is UTC seconds from the controller's clock — the DS3231 first, the SNTP system
  clock if the RTC is absent or unset — and 0 when neither has a time. See Notes
  for what that means for a datetime read off the card.

## Notes

- `phase` is the `Phase` enum ordinal: 0=Idle, 1=Lift, 2=Hold, 3=Lower, 4=Rest,
  5=SafeLower, 6=Fault, 7=Confirm. Confirm means waiting for a human to confirm
  the sets; relays off, nothing committed.
- `done.epoch` is UTC seconds from the controller's DS3231, falling back to the
  SNTP-synced system clock when the RTC is absent or unset, and 0 when neither
  has a time. A datetime on the card is therefore not proof of battery-backed
  hardware: on a machine with no RTC fitted it came from the network, and it is
  `?` whenever that machine booted offline.
- `done.fault` is latched when the session ends, not read back off the live
  phase.
- The top and bottom limit switches are gone. Travel is timed: LIFT drives the
  UP relay for `lift` seconds and LOWER drives DOWN for `lower`, both editable.
  `maxtravel` is no longer a phase terminator — it is the actuator's per-stroke
  energize cap, and the ceiling `lift`/`lower` are clamped to. The travel-timeout
  FAULT this used to produce no longer exists; FAULT now comes only from the
  actuator cap being hit and from the UART watchdog below.
- Run control is on two physical NO panel buttons wired to the controller —
  START (GPIO 25) and STOP (GPIO 26) — so it does not travel over UART. There is
  no RESET button. STOP's effect depends on what the machine is doing: from a
  running session it ends the session (lowering first if the leg might be up);
  from a latched FAULT it lowers the leg and clears the latch once it gets
  there; while the machine is already idle it zeroes the rep count instead.
- A session ends by itself when `reps` reaches `target`. `target` 0 means run
  until STOP.
- UART loss is still a safety event: if the controller sees no line for ~2 s
  during a session it requests a stop — which lowers the leg first unless it is
  already down — as defense-in-depth (the physical STOP button remains the
  primary stop).
- That watchdog is armed by the first line ever received, not by boot. A
  controller that has never heard from a display is running standalone — the
  bench setup in `firmware/README.md`, or a machine with the CYD unplugged —
  and stops on its panel STOP button instead. Arming it from boot would make
  "silent for 2 s" true from the first instant on those machines, and no session
  could start at all.
