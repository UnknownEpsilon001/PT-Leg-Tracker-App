# Start Confirmation — Sets Prompt Before A Session Runs — Design

**Date:** 2026-08-17
**Status:** approved, not implemented
**Builds on:** `2026-08-14-timed-travel-run-control-design.md` (the rep target
and the START/STOP panel this gates)
**Touches:** controller firmware, CYD display firmware, UART link protocol, Vue
app, bench tooling. **No server change.**

## Problem

Pressing START runs the machine immediately —
`pt-leg-controller.ino:500` for the panel button, `:678` for a server command.
Nothing states what is about to happen, and the rep target is only visible on a
screen nobody has to look at.

Three consequences:

1. **The prescription is invisible at the moment it matters.** A patient
   stepping up to the machine has no way to see how many reps it will run, or
   with what stroke and hold, without opening the settings screen.
2. **The one adjustment that is per-patient is buried.** Rep count is the number
   that changes between people. Changing it means the settings screen or the
   mid-session stepper, both of which write the stored default.
3. **The target stepper on the running screen reads as broken.** It sends the
   change and waits for the controller to echo it back, so with the link down it
   moves nothing and gives no feedback. Observed on the bench 2026-08-17.

## Solution

START from idle opens a confirmation screen on the CYD showing the sets the
machine is about to run, adjustable there, committed with ตกลง or a second START
press. The phone app asks the same question before it queues its start.

Sets chosen at confirmation apply to **that session only**. The stored default
is a prescription and is changed on the settings screen, nowhere else.

### Accepted tradeoff

START stops being a single action and becomes a two-step one. For a patient
repeating a daily programme this is one extra tap for no new information. It is
accepted because the machine loads a person's leg, and the cost of the machine
running a different programme than the operator assumed is higher than the cost
of a tap.

The mitigation is that the second step accepts the *same button* — press START
twice and nothing else has to be learned.

## Decisions

| Question | Decision | Why |
|---|---|---|
| START with the link down | Starts immediately, as today | A dead CYD must never make the machine unstartable with a patient loaded. |
| Sets changed at confirmation | Session only | Otherwise the last patient's number silently becomes the next patient's starting point. |
| Abandoned confirmation | STOP cancels; 30 s expiry cancels | Nothing moves either way, and the display must not sit on a half-finished action. |
| Expiry starting the session | No | A machine that begins moving with nobody having confirmed is the wrong default on a therapy device. |
| App-initiated start | No CYD confirmation | The app asks its own question; a second gate would hang a phone start until someone walked to the machine. |

## Controller state machine

`Phase` gains one value, **appended**:

```c++
enum class Phase { Idle, Lift, Hold, Lower, Rest, SafeLower, Fault, Confirm };
```

`session.h` already documents that the enumerator order is wire protocol and
that appending is the only safe extension. Every existing ordinal keeps its
meaning; only a display that has also been flashed understands the new one.

`SessionSM` gains three transitions and one timer:

- `requestStart(nowMs)` — Idle → Confirm, seeding the pending target from
  `gSettings.targetReps`. Actuator untouched, both relays off.
- `setPendingTarget(reps)` — updates the pending target while in Confirm. Fed by
  the `sets` message; ignored in any other phase.
- `confirmStart(nowMs)` — Confirm → Lift, running with the pending target.
- `cancelConfirm(nowMs)` — Confirm → Idle, discarding it.
- `tick()` expires Confirm to Idle after `CONFIRM_TIMEOUT_MS` (30000).

The pending target is the single value both commit paths read, so ตกลง on the
touchscreen and a second START press on the panel cannot disagree.

Nothing about the safety path moves earlier. `legPositionUnknown` and the
fault-recovery lowering still happen at the real start, so entering Confirm
commits the machine to nothing.

### Entry points

| Trigger | Behaviour |
|---|---|
| Panel START, Idle, link up | → Confirm |
| Panel START, Idle, link down | `gSession.start()` immediately, as today |
| Panel START, in Confirm | commits with the sets currently shown |
| Panel STOP, in Confirm | → Idle |
| 30 s in Confirm | → Idle |
| Server `cmd == "start"` | `gSession.start()` directly, no Confirm |

"Link down" is the condition the UART safe-stop already uses, so no new liveness
tracking is introduced.

### The stuck-START guard gates Confirm too

`buttonStartStuck()` refuses a start when START has read pressed since power-up
or far longer than a real press, on the theory that it is a short. Entering
Confirm must honour the same check. Without it a shorted line puts the machine
on the confirm screen and the *next* press commits — routing a suspected wiring
fault into a start rather than away from one.

### Session target vs stored default

Today both target paths persist: `onLinkTarget()` and `pt-leg-controller.ino:669`
each write `gSettings.targetReps` and set `gTargetDirty`.

`SessionSM` gains its own target for the run. `gSettings.targetReps` remains the
prescribed default and is written only by the settings screen and the app's
`/target` route.

## Link protocol

Two new display → controller messages:

```json
{"t":"sets","reps":12}
{"t":"confirm","ok":true}
```

`sets` mirrors the number the operator is currently looking at. It is valid only
while the controller is in Confirm, applies to the pending session, and never
touches `gSettings.targetReps`. `confirm` commits (`ok:false` is ยกเลิก).
Ordinary `link.h` id/ack/CRC handling applies, unchanged.

No new controller → display message: the display already receives `phase` and
`target` in the state message, which is everything the confirm screen needs to
open.

### Stepping renders locally but still mirrors

The display updates its own label the instant a tap lands, without waiting for
the controller to echo anything back. It *also* sends `sets`, fire-and-forget.

Both halves are load-bearing, for different reasons:

- **Rendering locally** is what fixes the complaint that started this. The
  existing running-screen stepper redraws only when the controller's next state
  message arrives, so with the link down it moves nothing at all and reads as a
  dead button. A confirmation screen must never feel dead.
- **Mirroring anyway** is what makes a physical START commit correct. START is
  wired to the controller, so if the number lived only on the display the
  controller would commit whatever stale value it happened to hold — running a
  different programme than the screen was showing at the moment somebody pressed
  the button. That failure is silent and lands on a patient's leg.

So the controller stays authoritative for what actually runs, and the display
stays instant for what the operator sees. `confirm` carries no `reps`: by the
time it is sent, both boards already agree.

If a `sets` message is lost, the ack/retransmit in `link.h` covers it; if the
link fails outright the confirmation expires to Idle and nothing runs.

## CYD confirm screen

New `scrConfirm` in `ui.cpp`:

- The sets number large and centred, `−` and `+` either side.
- ตกลง commits, ยกเลิก cancels.
- The programme read-only underneath: `ยก 7 · ค้าง 10 · ลง 7 · พัก 10 วิ`.
- Remaining seconds of the 30 s window shown; a screen that vanishes on its own
  should say so.
- Seeded from the `target` in the state message, so it opens on the prescription.
- Clamped 0–99. Zero renders as ไม่จำกัด, not a bare `0`.

Two build constraints carried from the existing UI: LVGL does no shaping, so
wording must avoid vowel+tone double stacks; and new glyphs must fall inside the
generated font subset or they render blank.

**Lamps:** Confirm shows yellow, the same as idle. Nothing is moving, and green
must keep meaning "a session is running".

## App

`queueStart(url, code, targetReps?)` and `StartBody.targetReps` already exist and
are covered by tests, so the client and server need no change.

A confirmation sheet goes between the start tap and the `queueStart` call at
`SessionView.vue:225`: seeded from `target.value`, a `−`/`+` stepper, ตกลง passes
the number to `queueStart`, ยกเลิก returns without starting.

The sheet must **not** call `setTarget()`. That is the existing stepper's job and
it writes the default through `/target`. Passing the number only to the start
call is what makes it session-scoped.

The guard at `SessionView.vue:228` stays: with a `stepTarget` edit in flight,
sending a target races the outstanding `setTarget` and would silently revert it.
The sheet is unavailable in that window rather than guessing.

## Controller rule for the server path

`netPollCommand()` returns a command and a target from the same poll response,
which makes the scoping rule fall out with no protocol change:

- target arriving **with** `cmd == "start"` → session only, `gSettings.targetReps`
  untouched
- target arriving **alone** (the app's `/target` route) → an explicit
  prescription change, persisted exactly as today

## Server

No change. `queue_start(target_reps)` already calls `set_desired_target()` before
queuing the command, so the device sees both in one poll response.

## Testing

**App (vitest):** the sheet opens on the start tap rather than starting; ตกลง
calls `queueStart` with the chosen number; ยกเลิก calls nothing; the sheet seeds
from `target.value`; the sheet is unavailable while `targetPending` is non-null.

**Server (pytest):** unchanged.

**Firmware** has no unit-test framework; verification is the existing bench
harness:

- `fake-controller.ps1` learns the Confirm phase and the `confirm` message, so
  the screen can be driven with only the CYD on the desk.
- `button-bench.ps1` gains steps: START → Confirm with **no relay click**; a
  second START commits; STOP in Confirm returns to Idle; the 30 s expiry; and
  START with the jumpers out starting immediately.
- `UI_TEST_HOOK` screenshots the confirm screen over USB. This is the only check
  that catches Thai glyphs falling outside the font subset — serial output
  cannot show it.
- `sync-shared.ps1 -Check` before flashing.

## Risks

**Both boards must be flashed together.** `Phase` is wire protocol. Appending
keeps every existing ordinal stable, so nothing silently changes meaning, but a
new controller against an old display sends a phase the display cannot name.

**Two-step START changes muscle memory** for anyone who has used the machine.
The second step accepting the same button limits what has to be relearned, and
the link-down fallback means the old one-press behaviour is still what happens
when the display is absent.

**The confirm screen is modal on a device whose touch panel has failed before.**
STOP on the panel and the 30 s expiry are both non-touch escapes, so a dead
digitizer cannot strand the machine on this screen.
