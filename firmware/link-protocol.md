# UART link protocol (controller ↔ CYD display)

Newline-delimited JSON, one message per line, over a dedicated UART (controller
`Serial2` on GPIO 16 RX / 17 TX ↔ CYD `Serial2` on GPIO 22 RX / 27 TX,
cross-wired RX↔TX with a common GND). Both ends 115200 8N1.

`println(json)` to send; the reader accumulates until `\n` and parses with
ArduinoJson. Malformed lines are dropped. Lines over 512 chars reset the buffer.

## Display → Controller

- `{"t":"cmd","v":"start"|"stop"}` — touch button press.
- `{"t":"settings","ssid":..,"pass":..,"server":..,"code":..,"maxtravel":..,"hold":..,"rest":..}`
  — save from the settings screen. The controller persists it to NVS and
  reconnects Wi-Fi.
- `{"t":"hello"}` — CYD boot; asks the controller for current settings + state.
- `{"t":"ping"}` — 1 Hz keepalive that feeds the controller's UART watchdog.

## Controller → Display

- `{"t":"state","running":bool,"phase":int,"elapsed":int,"reps":int,"wifi":bool,"server":bool}`
  — ~4 Hz.
- `{"t":"settings", ...current...}` — reply to `hello`, so the CYD settings
  screen pre-fills and the main screen can show the device code.

## Notes

- `phase` is the `Phase` enum ordinal: 0=Idle, 1=Lift, 2=Hold, 3=Lower, 4=Rest,
  5=SafeLower, 6=Fault.
- `maxtravel` replaced the old `lift`/`lower` timings when travel became
  limit-switch terminated; it is the per-stroke safety cap in seconds.
- UART loss is a safety event: if the controller sees no line for ~2 s during a
  session it safe-lowers and stops (the touch STOP would otherwise be
  unreachable).
