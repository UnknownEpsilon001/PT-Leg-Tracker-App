# Physical NC buttons + output-only display — design

Date: 2026-07-24
Branch: feature/esp32-firmware

## Motivation

The CYD touchscreen currently drives the session: touch GO/STOP buttons on the
main screen, plus a gear → settings screen for Wi-Fi/server config. Field
feedback: run control should be **physical panel buttons** (normally-closed,
like the limit switches), and the screen should be a **status display only** —
show the session status, Wi-Fi/server connection state, and a large logo.

Settings stay on the touchscreen. Only run control (start/stop) and a reset
move to physical buttons.

## Scope

In scope:
- Three NC momentary panel buttons on the controller: START, STOP, RESET-all.
- Controller reads them (debounced, edge-triggered) and drives `SessionSM`.
- Display main screen becomes output-only: big idle logo, running stats,
  Wi-Fi/server indicators. Touch GO/STOP removed. Gear → settings kept.
- UART protocol: drop the display→controller `cmd` message.

Out of scope (unchanged):
- Settings screen (touch keyboard, Wi-Fi/server/timing config).
- Limit-switch travel termination and FAULT safety.
- Server relay, NVS persistence, Wi-Fi provisioning path.

## Button behavior

| Button | GPIO | Gate | Action |
|--------|------|------|--------|
| START  | 25   | idle only          | `SessionSM` start |
| STOP   | 26   | any state          | `SessionSM` stop → safe-lower → idle |
| RESET  | 4    | idle only (not running) | zero reps + elapsed, clear FAULT → Idle |

- All NC, wired to GND, read `INPUT_PULLUP`. Same polarity convention as the
  limit switches: with `SWITCH_NC`, pressed (or broken wire) reads open/HIGH.
  A button module mirrors that so a cut wire cannot silently mean "held".
- **Edge-triggered**: each accessor returns true for exactly one loop on the
  press transition (not-pressed → pressed), so a held button fires once.
- STOP is ungated and always locally reachable on the controller — it is now
  the primary stop. There is no touch STOP fallback.
- RESET is idle-gated: pressing it mid-session does nothing (STOP first).

Chosen GPIOs 25/26/4 avoid the used pins (16/17 UART, 22/27 relays, 32/33
limit switches) and the input-only pins (34/35/36/39, no pullup), and carry no
boot-strapping role.

## Components

### Controller: `buttons.h` / `buttons.cpp` (new)
Mirrors the `switches.cpp` debounce pattern but adds rising-edge latch.

```
void buttonsBegin();                 // pinMode INPUT_PULLUP on the 3 pins
void buttonsPoll(uint32_t nowMs);    // call every loop, before SessionSM::tick
bool buttonStartPressed();           // true once on the press edge
bool buttonStopPressed();
bool buttonResetPressed();
```

Kept separate from `switches.cpp` on purpose: limit switches are
safety-critical and report a debounced *level*; buttons are UI and report an
*edge*. Different concerns, different files.

### Controller: `config.h`
Add `PIN_BTN_START 25`, `PIN_BTN_STOP 26`, `PIN_BTN_RESET 4`, and a
`BUTTON_DEBOUNCE_MS` (reuse 25 ms). Buttons follow the existing `SWITCH_NC`
define for polarity.

### Controller: `pt-leg-controller.ino`
- `buttonsBegin()` in setup, `buttonsPoll(now)` each loop before `SessionSM::tick`.
- Wire edges: START → `session.start()` when idle; STOP → `session.stop()`
  always; RESET → `session.reset()` when idle (new SessionSM method that zeroes
  reps/elapsed and clears FAULT back to Idle; no-op if running).

### Controller: `link.cpp`
Remove handling of `{"t":"cmd","v":"start"|"stop"}` — the display no longer
sends it. Keep `hello`, `ping`, `settings`. The UART-loss safe-lower watchdog
stays as defense-in-depth even though STOP is now local.

### Display: `ui.cpp` / `ui.h`
- Main screen: remove GO and STOP buttons and their callbacks (`cbStart`,
  `cbStop`, `goClicked`). Keep the gear button and its callback.
- Add Wi-Fi + server status indicators driven by the `state` message's `wifi`
  and `server` fields (small labeled dots: connected / not).
- Logo: enlarge to fill the idle screen. When `state.running` (or phase != Idle)
  the logo hides and the session stats (phase name, elapsed, reps) show; on
  return to idle the big logo comes back.
- `pt-leg-display.ino`: drop the start/stop send path over the link; keep
  settings-save and `hello`/`ping`.

### Protocol: `link-protocol.md`
- Remove the `cmd` line from Display → Controller.
- Note that START/STOP/RESET are physical NC buttons on the controller, so run
  control no longer travels over UART; the watchdog safe-lower is retained.

## Data flow

```
Panel NC buttons ──GPIO──> buttonsPoll (edge) ──> SessionSM (start/stop/reset)
                                                        │
SessionSM state ──> link (state msg, 4 Hz) ──UART──> CYD ui: logo/stats + wifi/server
Gear touch ──> settings screen ──settings msg──UART──> controller NVS + Wi-Fi
```

## Testing

Native firmware has no unit harness; verification is by build + bench.
- `arduino-cli compile` both sketches clean.
- Bench: `LINK_OVER_USB` fake-controller drives phases → confirm logo hides when
  running, stats show, logo returns idle, wifi/server dots reflect flags.
- Manual: press physical START/STOP/RESET, confirm gating (RESET no-op while
  running; START no-op while running; STOP always acts).

## Risks

- NC momentary buttons idle-closed (LOW) hold the pin low continuously; only the
  press (open/HIGH) is the event. Confirm wiring matches `SWITCH_NC` polarity or
  the edge inverts. Bench-test before trusting.
- Removing touch STOP means the physical STOP wiring is now safety-load-bearing;
  the UART watchdog remains the backstop if the controller itself is fine but
  the button wire fails open (reads as pressed → stops, fail-safe).
- NC wire-break fails as a single spurious press edge. For STOP/RESET this is
  safe (stop / idle-reset). For **START** a cut wire while idle looks like one
  press → spurious session start. Mitigation: START edge fires a session only
  from a stable idle state and the stroke is still limit-switch + max-travel
  bounded, so a spurious start cannot over-drive; acceptable, but note it. If
  stronger protection is wanted later, require START to be a fresh closed→open→
  closed cycle (seen closed at least once since boot) before honoring an edge.
