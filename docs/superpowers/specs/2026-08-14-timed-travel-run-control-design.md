# Timed Travel, START/STOP Panel, Two Lamps, Rep Target — Design

**Date:** 2026-08-14
**Status:** approved, not implemented
**Supersedes (in part):** `2026-07-24-physical-buttons-output-display-design.md`
(the RUN toggle + single status lamp it landed on) and the limit-switch travel
termination from `2026-07-19-esp32-firmware-design.md`
**Touches:** controller firmware, CYD display firmware, UART link protocol,
FastAPI server, Vue app, `firmware/pin-layout.html`, bench tooling

## Problem

Three separate things, one change because they all land on the same state
machine:

1. **The top and bottom limit switches terminate travel.** That makes stroke
   length a property of the machine's wiring, so a bench board with no switches
   fitted cannot run a cycle at all, and every debug session needs the real
   frame. The switches also carry the machine's whole belief about where the leg
   is, which is why `session.cpp` is 508 lines of which most is defending
   against a lying switch.
2. **Run control is one RUN toggle.** A press means start or stop depending on
   phase, which forced the interrupt latch to become phase-aware
   (`toggleWouldStart`) and made neither contact polarity fail-safe.
3. **A session runs until someone stops it.** There is no way to say "ten reps"
   — not on the CYD, not in the app.

## Solution

Travel becomes timed and every timing is editable. The panel gets two dedicated
buttons and two lamps. The session gets a rep target that the controller owns
and both UIs edit.

### Accepted tradeoff

With the limit switches out of the state machine the controller has no
measurement of where the leg is. A slipping belt, a jammed actuator and a clean
stroke are indistinguishable — each is "the DOWN relay was energized for
`lowerSec`". The only remaining backstop is the actuator's energize cap. This is
deliberate and is the point of the change (bench-runnable, debuggable), but it
means the machine must not be left unattended with a patient's leg loaded.

## Controller state machine

`CycleCfg` becomes:

```c++
struct CycleCfg {
  uint32_t liftMs;       // NEW — how long the UP relay drives
  uint32_t lowerMs;      // NEW — how long the DOWN relay drives
  uint32_t holdMs;
  uint32_t restMs;
  uint32_t maxTravelMs;  // ceiling that liftSec/lowerSec are clamped to
  uint16_t targetReps;   // NEW — 0 = run until STOP
};
```

Cycle:

```
Lift(liftMs) → Hold(holdMs) → Lower(lowerMs) → Rest(restMs) → repCount++
  repCount >= targetReps (and target != 0) ? finish(Idle, clean) : Lift
```

The target is checked at the end of Rest, where the leg is already down, so
reaching it ends the session immediately with no safe-lower and no fault. A
target of 0 is honoured as "unlimited" so a bad value from the wire cannot
strand a session; both UIs clamp their spinboxes to 1–99.

`tick()` loses both switch arguments: `void tick(uint32_t nowMs)`.

Deleted with the switches — `switches.cpp`, `switches.h`, `downConfirmed()`,
`seedLegPosition()`, `liftedSinceBottom`, `strokeFresh`, `bottomWasClear`,
`alreadyDown`, `recoveryFailed`, the net-2 "both switches pressed" fault, and
`LIFT_COMMIT_MS`. `SafeLower` always ends in `Idle`; there is no
unconfirmed-arrival case left to re-latch a fault for, so `recovering`
collapses to "this lowering does not report a session". Its duration is no
longer always the full `lowerMs`: from `Lift` it is computed from how far the
leg actually rose — the fraction of `liftMs` driven, converted to the
equivalent fraction of `lowerMs` — so an early stop does not spend the rest of
`lowerMs` stalled against the bottom stop with the limb loaded. From `Hold`,
`Fault` or `Idle` the position is either fully up or unknown by definition, so
those keep the full stroke.

`Phase` keeps all seven values. `Fault` remains reachable from the actuator
energize cap and the UART watchdog — only the travel-timeout fault goes away.

### Run control

`toggle()` and `toggleWouldStart()` are removed. Back to two entry points:

- `start(nowMs)` — Idle → Lift. Refused while running. From Fault it clears the
  latch and lowers first: with no limit switches there is no way to know whether
  the leg is up, so a start out of a fault always spends one `lowerMs` stroke
  driving DOWN before it lifts. Lowering an already-down leg costs a few seconds
  against the bottom stop; lifting an already-raised one drives it into the top.
- `requestStop(nowMs)` — running → SafeLower → Idle, reporting the session;
  Fault → SafeLower → Idle, quiet (that session was reported when it faulted),
  and the latch is cleared on arrival; Idle → zeroes `repCount`. Those last two
  clauses are what replace the retired RESET button.

## Pins and panel

| Function | GPIO | Change |
|---|---|---|
| Relay UP | 22 | unchanged |
| Relay DOWN | 27 | unchanged |
| START button | 25 | was RUN |
| STOP button | 26 | was free (retired STOP's old pin) |
| Green lamp | 23 | was the single status lamp |
| Yellow lamp | 32 | freed by the TOP limit switch |
| RTC SDA / SCL | 21 / 19 | unchanged |
| UART RX / TX | 16 / 17 | unchanged |

Freed and left free: **4** (old RESET), **33** (old BOTTOM limit).

Both buttons are **normally-open**: `BTN_START_NC 0`, `BTN_STOP_NC 0`, still
per-button and still overridable by build flag. A cut wire on an NO button is a
dead button, not a stuck press — so the fail-safe reasoning that kept the old
RUN button NC no longer applies, and `buttonRunWireSuspect()` narrows to a
stuck-START guard: START reading pressed since power-up, or held far longer than
a person holds a button, is a shorted line that would restart the machine
forever, so no session starts while it lasts. STOP needs no such guard — a
stuck STOP just keeps the machine idle.

The interrupt latch returns to **STOP only**, which is where it was before the
panel went to one button. A latched press can be an electrical transient
inflated to full width; that is acceptable for a stop and never acceptable for a
start, and with separate pins the rule needs no knowledge of the phase.

### Lamps

`status_led.*` becomes two pins driven from one call:

```c++
void statusLampsSet(bool inProgress);  // green = inProgress, yellow = !inProgress
```

Exactly one lamp is lit in both normal states (green running, yellow idle).
FAULT is the one exception: it lights **both** lamps rather than reading as
plain yellow, because plain yellow is indistinguishable from ordinary idle,
and on a panel-only machine with no CYD attached the panel is the only
indicator there is — it must not tell an operator a machine that faulted with
the leg raised is "ready". Both are still written only on change — the
mechanical-relay wear argument in `status_led.h` is unchanged and still forbids
blinking; a fault costs two extra contact operations (both lamps instead of
one), not a blink.

`LED_ACTIVE_LOW` stays a single switch covering both lamps, on the assumption
they are two channels of the same relay board.

## Settings

`DeviceSettings` gains three fields:

```c++
uint16_t maxTravelSec, liftSec, lowerSec, holdSec, restSec, targetReps;
```

Clamps, applied in `applySettings()` exactly as the existing ones are:

| field | range | default |
|---|---|---|
| `maxTravelSec` | 1–20 | 8 |
| `liftSec` | 1–`maxTravelSec` | 4 |
| `lowerSec` | 1–`maxTravelSec` | 4 |
| `holdSec` | 1–60 | 10 |
| `restSec` | 1–60 | 10 |
| `targetReps` | 0–99 | 10 |

`liftSec`/`lowerSec` clamp against the *incoming* `maxTravelSec` so a single
settings message that raises both stays consistent. These persist to NVS and to
`/settings.json` on the SD card, through the existing paths.

## UART link protocol

Display → Controller:

- `{"t":"settings", ...}` gains `lift`, `lower`, `target`. Unchanged otherwise,
  including being held until the session ends (NVS commit + Wi-Fi restart must
  not happen with a relay energized).
- `{"t":"target","reps":n}` — **new.** Applied to the live `CycleCfg`
  immediately, including mid-session, and marked dirty for NVS so it is
  persisted when the session ends. This is the message the main screen's ± sends;
  it deliberately does not go through `applySettings` because that path stalls.
- `{"t":"hello"}`, `{"t":"ping"}` unchanged.

Controller → Display:

- `{"t":"state", ...}` gains `"target":int`.
- `{"t":"settings", ...}` gains `lift`, `lower`, `target`.
- `{"t":"done", ...}` unchanged.

`maxtravel` keeps its name and its meaning as the per-stroke safety cap; the
protocol note saying it "replaced the old lift/lower timings when travel became
limit-switch terminated" is reversed — `lift`/`lower` are back, and `maxtravel`
is now only their ceiling.

## Server

State (`state.py`) gains a desired target set by the app and a live target
reported by the device:

- `desired_target: int | None` — set by `POST /api/sessions/target`, cleared
  when the device acknowledges it by reporting that value in a heartbeat, or
  after 30 s regardless, on the same clock as the timings below (a device on
  a pre-target-echo image can never acknowledge, so the clock is the only exit
  for it).
- `live_target: int` — from the heartbeat.

Endpoints:

- `POST /api/sessions/start?deviceId=` accepts an optional body
  `{"targetReps": n}`. When present it sets `desired_target` before queueing the
  start, so a start from the app carries its target in one round trip.
- `POST /api/sessions/target?deviceId=` — body `{"targetReps": n}`, sets
  `desired_target`. 202. Allowed while running.
- `GET /api/device/command?deviceId=` — response gains `"targetReps": n|null`,
  the pending desired target. The device already polls this, so no new polling
  loop exists on the ESP32.
- `POST /api/device/heartbeat` — body gains `target: int`.
- `GET /api/sessions/current` — response gains `targetReps: int` (the device's
  live value, not the desired one) so the app displays what the machine will
  actually do.

- `POST /api/sessions/timings?deviceId=` — body with any subset of `liftSec`,
  `lowerSec`, `holdSec`, `restSec`, `maxTravelSec`, for the app's debug settings
  group. Handed to the device on the same command poll. These expire after
  30 s on the same clock as the target above, because nothing echoes them
  back: repeating them indefinitely would let a stale edit overwrite a
  controller that had since been reconfigured at its own screen. The target
  additionally clears itself early on acknowledgement; timings have no such
  echo to clear on, so the clock is their only exit.

Range validation (targets 0–99, timings to the clamp table above) lives in the
route; the controller clamps again, and only it knows the live `maxTravelSec`
that `liftSec`/`lowerSec` are bounded by.

## Vue app

- `lib/device.ts`: `queueStart(serverUrl, code, targetReps?)`, new
  `setTarget(serverUrl, code, reps)` and `setTimings(serverUrl, code, timings)`,
  `getCurrent` returns `targetReps`.
- `types`: `CurrentSession.targetReps: number`.
- `SessionView.vue`: a rep-target stepper shown before start and while running.
  It displays the value from `/current` — never a local copy that outlives its
  own request — so a change made on the CYD appears here within one poll. The
  one local value it does keep, the pending value of a tap still in flight, is
  held only until that tap's own request resolves and is never sent on to the
  device as if it were confirmed. Progress reads `reps / target`.
- `SettingsView.vue`: a debug/advanced group for `liftSec`, `lowerSec`,
  `holdSec`, `restSec`, `maxTravelSec`. These are device settings, so they are
  sent to the machine, not stored in the app's own settings store.
- `stores/settings.ts`: unchanged except for whatever the new view needs to
  reach the server.

## CYD display

- Main screen: rep target with ± buttons, sending `{"t":"target"}` on each
  change, and a `reps / target` readout. The screen remains output-only for run
  control — START and STOP stay physical.
- Settings screen: `lift`, `lower`, `target` spinboxes alongside the existing
  ones.
- `settings.h` / `settingsfile.cpp` / `store.cpp`: the three new fields, and
  `/settings.json` gains `liftSec`, `lowerSec`, `targetReps`. A card file
  missing them loads the defaults above rather than zeroes.

Thai label wording must avoid vowel+tone double stacks — LVGL does no shaping.

## `firmware/pin-layout.html`

- Pin tables: TOP/BOTTOM limit rows removed, yellow-lamp row added at GPIO 32,
  RUN/RESET rows become START (25) / STOP (26), and the "free pins" note lists 4
  and 33. The `INPUT_PULLUP` + `SWITCH_NC` paragraph goes; the per-button
  contact paragraph is rewritten for two NO buttons.
- Simulator: `cRun`/`cReset` become `cStart`/`cStop`; the pin-state row drops
  `ps32`/`ps33` (limits) and gains the yellow lamp; the phase clock is driven by
  `liftSec`/`lowerSec` instead of ending on a switch; a target field ends the
  run at N reps.
- Both EN and TH copy, as the file already does throughout.

## Bench tooling

- `tools/button-bench.ps1`: two NO buttons and two lamps instead of RUN/RESET
  and one lamp.
- `tools/fake-controller.ps1`: emit `target` in `state` and the three new fields
  in `settings`; accept `{"t":"target"}`; run phases on the new timings.

## Testing

- **pytest** (`server/tests`): target set via start body; target set via
  `/api/sessions/target` while running; `desired_target` handed to the device on
  `/command` and cleared once the heartbeat echoes it; `current` reports the
  live target; out-of-range rejected.
- **vitest** (`PT-Leg-Tracker/src/lib`): `device.test.ts` for `setTarget`, start
  with a target, and `targetReps` parsing/defaulting in `getCurrent`.
- **Bench** (no test framework on the firmware): a session with target 3 stops
  itself after 3 reps; START from Fault clears and runs; STOP while idle zeroes
  the count; green/yellow are never both lit and never both dark; a target
  changed on the CYD mid-session shows up in the app and vice versa; timings from
  the app reach the machine and change stroke length.

## Out of scope

- Re-fitting limit switches as an optional safety input. The pins are freed and
  the code deleted; adding them back later is a new design.
- Any change to session logging, the SD card row format, or the app's records
  and charts. `done` is unchanged, so a target-terminated session logs like any
  other clean stop.
