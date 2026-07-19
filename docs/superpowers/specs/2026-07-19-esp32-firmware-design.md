# ESP32 Firmware (Device Side) — Design

Date: 2026-07-19
Status: approved by user (brainstorming session 2026-07-19)
Implements the device half of: `2026-07-13-server-mediated-device-control-design.md`
(v3 server contract — unchanged by this spec).

## Goal

Firmware for the therapy machine: drives the pneumatic leg-motion cycle,
provides an on-device touch UI, and speaks the v3 server contract — so the
Android app, server, and physical machine form one linked system. Lives in
`firmware/` in this repo (monorepo, same pattern as `server/`).

## Hardware

- **Board:** ESP32-2432S028R "Cheap Yellow Display" (CYD): ESP32-WROOM-32,
  2.8" ILI9341 320×240 TFT, XPT2046 resistive touch.
- **Actuator:** pneumatic motor via **2 relays** — UP valve → GPIO 22 (P3),
  DOWN valve → GPIO 27 (CN1). Both free GPIOs on the CYD. Relay polarity
  assumed active-HIGH; single `#define RELAY_ACTIVE_LOW` flips it.
- **Fallback:** if pneumatics fail, stepper motor drive replaces the relay
  module. The actuator abstraction (below) confines that change to one module.
- **No physical button:** start/stop is the touch screen. (The v3 contract's
  "physical button" = the on-device touch button; contract semantics
  identical.)

## Architecture

Arduino IDE sketch (user's toolchain choice), modular:

```
firmware/
  pt-leg-device/
    pt-leg-device.ino   — setup/loop, module wiring, non-blocking scheduler
    config.h            — pins, defaults, RELAY_ACTIVE_LOW, version pins list
    settings.{h,cpp}    — NVS-backed settings (Preferences): wifi ssid/pass,
                          server url, cycle durations
    actuator.{h,cpp}    — relay control: up() / down() / off(); enforces
                          mutual exclusion + off-on-boot
    session.{h,cpp}     — session + motion state machine (pure C++, no
                          Arduino deps beyond millis passed in)
    net.{h,cpp}         — WiFi connect/reconnect + server client (HTTPClient
                          + ArduinoJson): pollCommand / sendEvent / heartbeat
    ui.{h,cpp}          — LVGL screens + bindings
```

Cooperative `loop()` with `millis()` timers — no FreeRTOS tasks beyond what
Arduino/LVGL already use. `lv_timer_handler()` every loop; net calls
scheduled (1 s / 5 s cadences) and bounded by short HTTP timeouts so UI
stays responsive.

## Motion state machine (firmware owns cycle timing)

Top level: `IDLE ⟷ RUNNING`. Inside RUNNING:

```
LIFT  (UP relay ON,  liftSec,  default 3 s)
HOLD  (all OFF,      holdSec,  default 10 s)
LOWER (DOWN relay ON, lowerSec, default 3 s)
REST  (all OFF,      restSec,  default 10 s)  → reps++ → LIFT ...
```

- All four durations are settings (NVS), adjustable on the settings screen.
- Rep increments at cycle completion (end of REST).
- `elapsedSec` counts from session start; reported in heartbeats/events.
- HOLD/REST = both valves closed (traps pressure → position held).
- Stop (any origin) at any sub-phase: **safe-lower first** — if the leg is
  up (LIFT or HOLD), energize DOWN relay for `lowerSec`, then all relays
  OFF, then IDLE + report final elapsed/reps. If already in LOWER/REST,
  finish lowering / go straight to OFF. The session's `stopped` event is
  sent after the safe-lower completes.

### Safety rules (actuator module invariants)

- UP and DOWN relays never energized simultaneously.
- All relays OFF: at boot (before anything else in `setup()`), on stop, on
  any state-machine exit.
- Hardware watchdog stays enabled; a firmware hang → reset → boot-time
  relays-OFF gives a safe failure mode.

## Server communication (v3 contract, device side)

Same behavior the server's pytest suite and `server/scripts/mock-esp32.mjs`
already encode:

- `GET /api/device/command` every 1 s → `start` / `stop` / `null`
  (consume-on-read). Acting on a command drives the same code path as a
  touch press.
- `POST /api/device/heartbeat` `{state, elapsedSec, reps}` — every 1 s
  RUNNING, 5 s IDLE.
- `POST /api/device/event` `{type: started|stopped, elapsedSec, reps}` on
  every transition regardless of origin. Server's returned `sessionId` is
  not needed by firmware (kept for logging only).

**Offline behavior:** device is fully functional standalone — touch
start/stop and the motion cycle never depend on connectivity. The last
unacknowledged event is buffered (one slot, matching the server's one-slot
model) and retried each poll tick until a 2xx. Heartbeats are
fire-and-forget. Wi-Fi drop → background reconnect loop; UI shows status.

## UI (LVGL, 2 screens)

Libraries (Arduino IDE — versions pinned in `firmware/README.md` since
there is no manifest): `lvgl` 8.x, `TFT_eSPI` (CYD `User_Setup.h` checked
into `firmware/`), `XPT2046_Touchscreen`, `ArduinoJson` 7.x.

**Main screen:** large mm:ss timer, rep count, one giant start/stop button
(green start / maroon stop, Thai labels เริ่ม/หยุด), status icons for Wi-Fi
and server reachability, gear icon → settings. Sub-phase indicator text
(ยก/ค้าง/ลง/พัก) while running.

**Settings screen:** Wi-Fi scan list, password entry via LVGL on-screen
keyboard, server URL text field, four numeric fields for cycle durations,
save → NVS + reconnect. First boot with no stored SSID lands here
automatically.

Thai text on device requires an LVGL font with Thai glyphs: generate one
(LVGL font converter, subset to the ~30 Thai strings used) and commit it;
fallback plan if rendering quality is poor on 320×240 is English/numeric
labels with Thai only on the two big buttons.

## Testing

No on-target unit tests (Arduino IDE has no test runner; `session` module is
kept Arduino-free so it *could* be host-tested later). Verification is
integration against the real stack:

1. `server/scripts/dev.ps1 -NoMock` on the staff PC.
2. Dual-control matrix — each must produce ONE correct server session:
   app-start/app-stop, app-start/touch-stop, touch-start/app-stop,
   touch-start/touch-stop.
3. Offline matrix: start with server down (standalone runs, event delivered
   after server returns), Wi-Fi drop mid-session (cycle uninterrupted,
   heartbeat resumes), command expiry (app start with device powered off →
   app shows เครื่องออฟไลน์).
4. Safety: relay states observed at boot, stop, and power-pull mid-LIFT.
5. Parity check: server pytest suite still green with real device attached
   (it encodes the same contract the mock satisfies).

## Out of scope

- Stepper motor drive (abstraction ready; separate future plan).
- HTTPS/cloud server phase (same accepted risk as v3: HTTP on trusted LAN).
- OTA updates. (Multi-device support is now IN scope — see Addendum below,
  superseding the earlier single-device line.)

---

## Addendum 2026-07-19 — two-MCU split + device-code pairing

Two corrections to the design above, from the user: the machine uses **two
ESP32s**, and it must carry a **device code** for multi-machine pairing
(`2026-07-19-multi-device-pairing-design.md`).

### Revised hardware (supersedes the single-board "Hardware" + "Architecture" sections)

Two ESP32 per machine, one UART between them:

- **Controller ESP32** — the brain, and the WiFi owner. Drives the relays
  (UP GPIO 22, DOWN GPIO 27), owns the motion + session state machine, the
  WiFi + v3 server client, and NVS settings (wifi ssid/pass, server url,
  **device code**, cycle durations). No display attached.
- **Display ESP32 = CYD (ESP32-2432S028R)** — LVGL touch UI ONLY. No WiFi,
  no relays, no session logic. Renders state received over UART; forwards
  touch (start/stop, settings entry) over UART.

The relay GPIOs and all logic that the single-board design placed on the CYD
now live on the controller; the CYD is a pure terminal.

### UART link (controller ↔ CYD)

Newline-delimited JSON, one message per line, over a dedicated UART
(controller `Serial2` ↔ a spare CYD UART; exact pins pinned in the display
README). `println(json)` to send; reader accumulates to `\n` and parses with
ArduinoJson; malformed lines dropped.

Display → Controller:
- `{"t":"cmd","v":"start"|"stop"}` — touch button press.
- `{"t":"settings","ssid":..,"pass":..,"server":..,"code":..,"lift":..,"hold":..,"lower":..,"rest":..}` — save from the settings screen.
- `{"t":"hello"}` — CYD boot; asks the controller for current settings + state.

Controller → Display:
- `{"t":"state","running":bool,"phase":int,"elapsed":int,"reps":int,"wifi":bool,"server":bool}` — ~4 Hz.
- `{"t":"settings", ...current...}` — reply to `hello`, so the CYD settings
  screen pre-fills and the main screen can show the device code.

`phase` is the `Phase` enum ordinal (0=Idle … 5=SafeLower).

### Safety under UART loss

Touch lives on the CYD; control lives on the controller. If the UART goes
silent for > 2 s **while running**, the controller **safe-lowers and stops**
(the operator's screen may be dead and could not send stop). The app-stop
path is unaffected — the controller has WiFi and still receives server stop
commands directly. On boot the controller relays are OFF regardless of UART
state (existing invariant).

### Device code (multi-machine pairing)

The controller stores `deviceCode` in NVS and sends it as `?deviceId=<code>`
on `GET /api/device/command`, and as `"deviceId":<code>` in the `/event` and
`/heartbeat` bodies. Blank code → the server's `"default"` device. The code is
typed on the CYD settings screen (a new text field beside server URL), pushed
to the controller over the UART `settings` message, and shown on the CYD main
screen for the app operator to read and enter in the phone.

### Module → board map

| Module (from the single-board plan) | Board |
|---|---|
| `config.h`, `settings.{h,cpp}` (+ `deviceCode`) | Controller |
| `actuator.{h,cpp}` | Controller |
| `session.{h,cpp}` | Controller |
| `net.{h,cpp}` (+ `deviceId` on requests) | Controller |
| `link.{h,cpp}` — new UART protocol, both ends | Both |
| `ui.{h,cpp}` (+ device-code field, now UART-driven, no WiFi/relays) | Display (CYD) |

### Sketch layout (revised)

```
firmware/
  pt-leg-controller/  — .ino + config/settings/actuator/session/net/link
  pt-leg-display/     — .ino + ui + link + lv_conf.h + TFT_eSPI_User_Setup.h
  link-protocol.md    — UART message schema (shared reference)
```

### Testing delta

- UART link check: pull the UART mid-session → controller safe-lowers/stops
  within ~2 s; CYD shows "no link"; app-stop still works over WiFi.
- Device-code check: two machines with codes `KNEE-01`/`KNEE-02`, two phones,
  concurrent sessions stay isolated end-to-end (mirrors the multi-device
  pairing plan's e2e gate, now with real firmware).
- Dual-control and offline matrices from the single-board plan are unchanged
  in intent; "touch" now originates on the CYD and crosses the UART.
