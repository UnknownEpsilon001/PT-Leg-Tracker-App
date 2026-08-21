# Start Confirmation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Pressing START from idle opens a confirmation screen on the CYD showing the sets the machine is about to run, adjustable there, committed with ตกลง or a second START press — and the phone app asks the same question before queuing its start.

**Architecture:** The controller gains one appended `Phase::Confirm` and owns a *pending target* separate from the stored default. The display renders a confirm screen whenever it sees that phase, steps the number locally for instant feedback, and mirrors each change to the controller with a `sets` message so both commit paths (touchscreen ตกลง, second panel START) agree on what runs. The app reuses `queueStart(url, code, targetReps?)`, which already exists.

**Tech Stack:** Arduino-framework C++ on two ESP32s (arduino-cli, `esp32:esp32:esp32:PartitionScheme=huge_app`), LVGL 8.4.0 on the CYD, ArduinoJson 7.4.1 over newline-delimited JSON UART, Vue 3 + TypeScript + Vitest for the app, PowerShell bench harnesses.

**Spec:** `docs/superpowers/specs/2026-08-17-start-confirmation-design.md`

## Global Constraints

- `Phase` is wire protocol. **Append only** — `Confirm` goes last, after `Fault`. Both copies (`pt-leg-controller/session.h`, `pt-leg-display/session.h`) must stay byte-identical in enumerator order.
- **Both boards must be flashed together** after any task that changes the enum or the link protocol.
- `link.h`, `link.cpp` and `settings.h` exist in both sketch directories. The controller's copies are the source of truth; run `.\firmware\sync-shared.ps1` to copy and `-Check` to verify. **`session.h` is NOT synced** — the display's copy is a deliberately reduced one.
- `CONFIRM_TIMEOUT_MS = 30000`.
- Target range is `0..99` everywhere. **`0` is a legal value meaning "run until STOP"** and must never be treated as unset. On screen it renders as `ไม่จำกัด`.
- LVGL does no text shaping: Thai wording must avoid vowel+tone double stacks, and every glyph used must be inside the generated font subset or it renders blank.
- Comments follow the existing bilingual style: English first, then a `[TH]` block.
- Release flags stay at `BENCH_DEBUG 0`, `LINK_OVER_USB 0`, `UI_TEST_HOOK 0` in anything committed.
- The app has **no component-test harness** (no `@vue/test-utils`, no jsdom). Testable logic goes in a pure module under `src/lib/` with a sibling `.test.ts`, following `src/lib/target-step.ts`. Do not add a component test framework.
- Firmware has no unit-test framework. A firmware task's test step is `arduino-cli compile` plus the named bench-harness check.
- No server change in this plan.

---

## File Structure

**Controller firmware** (`firmware/pt-leg-controller/`)
- `session.h` — append `Phase::Confirm`; declare `requestStart`, `setPendingTarget`, `confirmStart`, `cancelConfirm`, `pendingTarget()`; add `pendingTargetReps` member.
- `session.cpp` — implement the four transitions and the expiry in `tick()`.
- `link.h` / `link.cpp` — add `onSets`/`onConfirm` callbacks, `sendSets`/`sendConfirm` senders, and parse the two new types.
- `pt-leg-controller.ino` — route panel START/STOP through the new transitions; scope a start-carried target to the session.
- `panel_lamps.cpp` — treat Confirm as a non-running phase (yellow).

**Display firmware** (`firmware/pt-leg-display/`)
- `session.h` — append `Phase::Confirm` (mirror).
- `link.h` / `link.cpp` — synced copies.
- `ui.h` / `ui.cpp` — new `scrConfirm`, `uiShowConfirm()`, `uiSetConfirmCallbacks()`.
- `pt-leg-display.ino` — show/hide the screen on phase, wire callbacks to link sends.

**App** (`PT-Leg-Tracker/`)
- `src/lib/start-confirm.ts` (new) — pure sheet logic.
- `src/lib/start-confirm.test.ts` (new).
- `src/views/SessionView.vue` — sheet markup + wiring.

**Tooling / docs**
- `firmware/tools/fake-controller.ps1`, `firmware/tools/button-bench.ps1`
- `firmware/link-protocol.md`, `firmware/README.md`, `firmware/pin-layout.html`

---

### Task 1: Append `Phase::Confirm` to both enum copies

**Files:**
- Modify: `firmware/pt-leg-controller/session.h:22`
- Modify: `firmware/pt-leg-display/session.h:13`
- Modify: `firmware/link-protocol.md:121`

**Interfaces:**
- Consumes: nothing.
- Produces: `Phase::Confirm` with ordinal `7` in both copies.

- [ ] **Step 1: Append the enumerator in the controller's copy**

```c++
enum class Phase { Idle, Lift, Hold, Lower, Rest, SafeLower, Fault, Confirm };
```

- [ ] **Step 2: Append it identically in the display's copy**

Same line, in `firmware/pt-leg-display/session.h`. The order must match byte for byte.

- [ ] **Step 3: Document the new ordinal**

In `firmware/link-protocol.md`, extend the phase list to `0=Idle, 1=Lift, 2=Hold, 3=Lower, 4=Rest, 5=SafeLower, 6=Fault, 7=Confirm`, and note that Confirm means "waiting for a human to confirm the sets; relays off, nothing committed".

- [ ] **Step 4: Compile both sketches**

Run:
```bash
arduino-cli compile --fqbn "esp32:esp32:esp32:PartitionScheme=huge_app" firmware/pt-leg-controller
```
Then the same for `firmware/pt-leg-display`. Expected: both succeed. `phaseName()` switches in both sketches may now warn about an unhandled enumerator — that is expected and is fixed in Tasks 2 and 6.

- [ ] **Step 5: Commit**

```bash
git add firmware/pt-leg-controller/session.h firmware/pt-leg-display/session.h firmware/link-protocol.md
git commit -m "feat(firmware): add the Confirm phase ordinal to both enum copies"
```

---

### Task 2: `SessionSM` confirm transitions and expiry

**Files:**
- Modify: `firmware/pt-leg-controller/session.h` (public API, `running()`, `pendingTargetReps` member)
- Modify: `firmware/pt-leg-controller/session.cpp` (the four transitions, `tick()` expiry)
- Modify: `firmware/pt-leg-controller/pt-leg-controller.ino` (`phaseName()` under `BENCH_DEBUG`, and the session-end target restore)

**Interfaces:**
- Consumes: `Phase::Confirm` from Task 1.
- Produces:
  - `void requestStart(uint32_t nowMs)`
  - `void setPendingTarget(uint16_t reps)`
  - `void confirmStart(uint32_t nowMs)`
  - `void cancelConfirm(uint32_t nowMs)`
  - `uint16_t pendingTarget() const`
  - `bool confirming() const`

- [ ] **Step 1: Add the constant and members to `session.h`**

```c++
// How long a confirmation waits for a human before giving up. Nothing is
// committed while it waits, so expiry is always safe — it returns to Idle
// rather than starting.
// [TH] เวลาที่หน้ายืนยันจะรอคนกดก่อนเลิกรอ ระหว่างรอยังไม่มีอะไรถูกยืนยัน
//      การหมดเวลาจึงปลอดภัยเสมอ คือกลับไปสถานะว่าง ไม่ใช่เริ่มทำงาน
static const uint32_t CONFIRM_TIMEOUT_MS = 30000;
```

Add to the private section, next to `repCount`:

```c++
  uint16_t pendingTargetReps = 0;  // เป้าหมายที่รอการยืนยัน ใช้เฉพาะรอบนี้
```

- [ ] **Step 2: Declare the public API in `session.h`**

```c++
  // Idle -> Confirm. Commits the machine to nothing: the actuator is untouched
  // and both relays stay off. The pending target seeds from the stored default,
  // which is the prescription; editing it on the confirm screen changes only
  // this session.
  // [TH] ว่าง -> รอยืนยัน ยังไม่ผูกมัดอะไรทั้งสิ้น ไม่แตะชุดขับเคลื่อนและรีเลย์ดับทั้งคู่
  //      ค่าเป้าหมายที่รอยืนยันเริ่มจากค่าตั้งต้นที่บันทึกไว้ ซึ่งเป็นค่าที่แพทย์สั่ง
  //      การแก้ที่หน้ายืนยันมีผลเฉพาะรอบนี้เท่านั้น
  void requestStart(uint32_t nowMs);

  // Mirrors what the operator is looking at on the display. Ignored outside
  // Confirm, so a late or stray message can never retarget a running session.
  // [TH] สะท้อนตัวเลขที่ผู้ใช้กำลังดูอยู่บนจอ จะถูกละเว้นถ้าไม่ได้อยู่สถานะรอยืนยัน
  //      ข้อความที่มาช้าหรือหลงมาจึงเปลี่ยนเป้าหมายของรอบที่กำลังทำงานอยู่ไม่ได้
  void setPendingTarget(uint16_t reps);

  void confirmStart(uint32_t nowMs);   // Confirm -> Lift, running the pending target
  void cancelConfirm(uint32_t nowMs);  // Confirm -> Idle, discarding it
  uint16_t pendingTarget() const { return pendingTargetReps; }
  bool confirming() const { return ph == Phase::Confirm; }
```

In the same file, `running()` must stop counting Confirm as a session. It is
currently `ph != Phase::Idle && ph != Phase::Fault`, which reports **true** for
Confirm:

```c++
  bool running() const {
    return ph != Phase::Idle && ph != Phase::Fault && ph != Phase::Confirm;
  }
```

**This is load-bearing well beyond the lamps.** `running()` also gates the
heartbeat interval, the settings-deferral logic, and the display's gear button
and logo screen. Confirm is not a session — nothing is moving and nothing is
committed — and every one of those consumers wants that answer.

- [ ] **Step 3: Implement the transitions in `session.cpp`**

```c++
void SessionSM::requestStart(uint32_t nowMs) {
  if (ph != Phase::Idle) return;
  pendingTargetReps = cfg.targetReps;
  enter(Phase::Confirm, nowMs);
}

void SessionSM::setPendingTarget(uint16_t reps) {
  if (ph != Phase::Confirm) return;
  pendingTargetReps = reps > 99 ? 99 : reps;
}

void SessionSM::confirmStart(uint32_t nowMs) {
  if (ph != Phase::Confirm) return;
  // Deliberately NOT written to cfg.targetReps: that is the stored prescription
  // and the confirm screen must not move it. setTarget() is what start() reads,
  // so the pending value is applied to the run only.
  // [TH] จงใจไม่เขียนลง cfg.targetReps เพราะนั่นคือค่าที่แพทย์สั่งและบันทึกไว้
  //      หน้ายืนยันต้องไม่ไปขยับค่านั้น ค่าที่รอยืนยันจึงถูกใช้กับรอบนี้เท่านั้น
  uint16_t reps = pendingTargetReps;
  ph = Phase::Idle;  // start() refuses from Confirm; hand it the Idle it expects
  start(nowMs);
  cfg.targetReps = reps;
}

void SessionSM::cancelConfirm(uint32_t nowMs) {
  if (ph != Phase::Confirm) return;
  enter(Phase::Idle, nowMs);
}
```

- [ ] **Step 3b: Restore the stored default at session end — REQUIRED**

`cfg.targetReps` is both the stored default *and* what a running session counts
against, which is why `confirmStart` writes it after `start()`. That makes the
restore mandatory, not optional.

Find the stopped-edge handling in `pt-leg-controller.ino` (where
`takeStoppedEdge()` is consumed). If it does not already re-seed the cycle
config from settings, add:

```c++
  // The session just ended may have run a confirm-screen target, which lives in
  // cfg.targetReps because that is what tick() counts against. Put the stored
  // prescription back, or one patient's one-off becomes the next patient's
  // default — the exact thing the confirmation screen exists to prevent.
  // [TH] รอบที่เพิ่งจบอาจใช้ค่าเป้าหมายจากหน้ายืนยัน ซึ่งเก็บอยู่ใน cfg.targetReps
  //      เพราะเป็นค่าที่ tick() ใช้นับ ต้องคืนค่าที่แพทย์สั่งกลับไป
  //      ไม่งั้นค่าครั้งเดียวของคนไข้คนหนึ่งจะกลายเป็นค่าตั้งต้นของคนถัดไป
  //      ซึ่งเป็นสิ่งที่หน้ายืนยันมีไว้เพื่อป้องกันโดยตรง
  gSession.setTarget(gSettings.targetReps);
```

This must also cover a session that ends in a fault and one cancelled from
Confirm. State in your report which path you attached it to and why.

- [ ] **Step 4: Expire the phase in `tick()`**

At the top of `tick()`'s phase switch, add:

```c++
    case Phase::Confirm:
      // Nobody answered. Nothing was committed, so this returns to Idle and
      // never starts — see the spec's decision table.
      // [TH] ไม่มีใครตอบ และยังไม่มีอะไรถูกยืนยัน จึงกลับไปสถานะว่าง ไม่เริ่มทำงาน
      if (nowMs - phaseStart >= CONFIRM_TIMEOUT_MS) enter(Phase::Idle, nowMs);
      return;
```

- [ ] **Step 5: Add the phase name for bench output**

In `pt-leg-controller.ino`'s `phaseName()` (inside `#if BENCH_DEBUG`), add `case Phase::Confirm: return "CONFIRM";`.

- [ ] **Step 6: Compile**

Run:
```bash
arduino-cli compile --fqbn "esp32:esp32:esp32:PartitionScheme=huge_app" firmware/pt-leg-controller
```
Expected: success, no unhandled-enumerator warnings.

- [ ] **Step 7: Commit**

```bash
git add firmware/pt-leg-controller/session.h firmware/pt-leg-controller/session.cpp firmware/pt-leg-controller/pt-leg-controller.ino
git commit -m "feat(firmware): confirm transitions and 30 s expiry in the session state machine"
```

---

### Task 3: `sets` and `confirm` link messages

**Files:**
- Modify: `firmware/pt-leg-controller/link.h`, `firmware/pt-leg-controller/link.cpp`
- Modify: `firmware/link-protocol.md`
- Run: `firmware/sync-shared.ps1`

**Interfaces:**
- Consumes: nothing from earlier tasks.
- Produces:
  - `void Link::sendSets(uint16_t reps)` (display side)
  - `void Link::sendConfirm(bool ok)` (display side)
  - `void (*onSets)(uint16_t)` (controller side)
  - `void (*onConfirm)(bool)` (controller side)

- [ ] **Step 1: Declare the senders and callbacks in `link.h`**

Next to `sendTarget`:

```c++
  void sendSets(uint16_t reps);   // display -> controller, pending target
  void sendConfirm(bool ok);      // display -> controller, commit or cancel
```

Next to `onTarget`:

```c++
  void (*onSets)(uint16_t) = nullptr;   // controller side
  void (*onConfirm)(bool) = nullptr;    // controller side
```

- [ ] **Step 2: Implement the senders in `link.cpp`**

Follow the shape of the existing `sendTarget`, including the id/ack path — both are one-shot events whose loss is silent, so they belong in the retransmit queue exactly like `target`:

```c++
void Link::sendSets(uint16_t reps) {
  JsonDocument d;
  d["t"] = "sets";
  d["reps"] = reps;
  sendTracked(d);
}

void Link::sendConfirm(bool ok) {
  JsonDocument d;
  d["t"] = "confirm";
  d["ok"] = ok;
  sendTracked(d);
}
```

Use whatever the existing `sendTarget` calls to enqueue — read it first and match it exactly rather than assuming the helper is named `sendTracked`.

- [ ] **Step 3: Parse both types**

In `link.cpp`'s type dispatch, after the `"target"` branch:

```c++
  } else if (!strcmp(t, "sets")) {
    if (onSets) onSets((uint16_t)(d["reps"] | 0));
    return;
  } else if (!strcmp(t, "confirm")) {
    if (onConfirm) onConfirm(d["ok"] | false);
    return;
```

`d["reps"] | 0` is correct here even though 0 is a legal target: `sets` always carries the field, and an absent one meaning "unlimited" is the safe reading for a pending value that is about to be confirmed on screen anyway.

- [ ] **Step 4: Document both messages**

Add them to `firmware/link-protocol.md` alongside `target`, stating: `sets` is valid only while the controller is in Confirm, applies to the pending session, and never touches stored settings; `confirm` commits or cancels and carries no reps because both boards already agree by then.

- [ ] **Step 5: Sync the shared copies and verify**

```bash
.\firmware\sync-shared.ps1
```
Then:
```bash
.\firmware\sync-shared.ps1 -Check
```
Expected: `shared files in sync`, exit 0.

- [ ] **Step 6: Compile both sketches**

Both must build. Expected: success.

- [ ] **Step 7: Commit**

```bash
git add firmware/pt-leg-controller/link.h firmware/pt-leg-controller/link.cpp firmware/pt-leg-display/link.h firmware/pt-leg-display/link.cpp firmware/link-protocol.md
git commit -m "feat(firmware): sets and confirm link messages"
```

---

### Task 4: Route the panel buttons through Confirm

**Files:**
- Modify: `firmware/pt-leg-controller/pt-leg-controller.ino` (button handling near `:500`, callback wiring near `:411`)

**Interfaces:**
- Consumes: `requestStart`, `confirmStart`, `cancelConfirm`, `setPendingTarget`, `confirming()` (Task 2); `onSets`, `onConfirm` (Task 3).
- Produces: nothing for later tasks.

- [ ] **Step 1: Replace the START/STOP dispatch**

The current line is `else if (pressStart) gSession.start(now);`. Replace the start/stop dispatch with:

```c++
  // START from idle opens the confirmation instead of running, but only when a
  // display is actually there to show it. With the link down there is nothing to
  // confirm on, and a machine that cannot be started is worse than one that
  // starts the way it always has.
  // [TH] ปุ่มเริ่มจากสถานะว่างจะเปิดหน้ายืนยันแทนการเริ่มทำงาน แต่เฉพาะตอนที่มีจอเชื่อมอยู่จริง
  //      ถ้าลิงก์หลุดก็ไม่มีที่ให้ยืนยัน และเครื่องที่เริ่มไม่ได้เลยแย่กว่าเครื่องที่เริ่มแบบเดิม
  else if (pressStart && gSession.confirming()) gSession.confirmStart(now);
  else if (pressStart && linkUp) gSession.requestStart(now);
  else if (pressStart) gSession.start(now);
```

And for STOP, before the existing stop handling:

```c++
  if (pressStop && gSession.confirming()) gSession.cancelConfirm(now);
```

**Read the surrounding code before editing.** The existing branch already guards on `buttonStartStuck()`; `requestStart` must sit behind that same guard, so that a START line reading pressed since power-up cannot put the machine on the confirm screen where the next press commits it. If the guard is expressed as an earlier `if`, leave it in front of all three branches rather than duplicating it.

- [ ] **Step 2: Define `linkUp` the way the safe-stop already does**

Use the existing UART-liveness expression in this file (built on `gLink.lastRxMs()`), not a new one. If it is currently inline, extract it to a local `bool linkUp` above the dispatch so both the safe-stop and this dispatch read the same value in the same iteration.

- [ ] **Step 3: Wire the two new callbacks**

Next to `gLink.onTarget = onLinkTarget;`:

```c++
  gLink.onSets = onLinkSets;
  gLink.onConfirm = onLinkConfirm;
```

With, next to `onLinkTarget`:

```c++
// Pending target from the confirm screen. Session-scoped by construction:
// SessionSM ignores it outside Confirm and never writes it to gSettings.
// [TH] ค่าเป้าหมายที่รอยืนยันจากหน้าจอ ผูกกับรอบนี้โดยตัวมันเอง
//      เครื่องสถานะจะละเว้นถ้าไม่ได้อยู่สถานะรอยืนยัน และไม่เขียนลง gSettings เลย
static void onLinkSets(uint16_t reps) { gSession.setPendingTarget(reps); }

static void onLinkConfirm(bool ok) {
  uint32_t now = millis();
  if (ok) gSession.confirmStart(now);
  else gSession.cancelConfirm(now);
}
```

- [ ] **Step 4: Compile**

Expected: success.

- [ ] **Step 5: Bench-check with the dry run**

```bash
.\firmware\tools\button-bench.ps1 -DryRun
```
Expected: the existing checklist still passes. It does not yet cover Confirm — Task 8 adds those steps.

- [ ] **Step 6: Commit**

```bash
git add firmware/pt-leg-controller/pt-leg-controller.ino
git commit -m "feat(firmware): panel START opens the confirmation when a display is linked"
```

---

### Task 5: Scope a start-carried target to the session

**Files:**
- Modify: `firmware/pt-leg-controller/pt-leg-controller.ino:668-679`

**Interfaces:**
- Consumes: `gSession.setTarget` (existing).
- Produces: nothing for later tasks.

**Rewritten 2026-08-19.** Ruling F removed the `cfg.targetReps` overloading this
task was originally written against, and Task 4's review found the server start
path bypasses the confirmation. Both land in this same block, so both are here.

- [ ] **Step 1: Add a session-only target setter**

`setTarget()` now writes *both* `cfg.targetReps` (the prescription) and
`sessionTargetReps` (what the running session counts toward), so it is the wrong
tool for a one-off. Add to `session.h`, beside `setTarget()`:

```c++
  // A target for this session only, leaving the stored prescription alone —
  // the app's confirmation sheet, the remote twin of what confirmStart() does
  // for the CYD's confirm screen. Call it after start(), which seeds
  // sessionTargetReps from the prescription.
  // [TH] เป้าหมายเฉพาะรอบนี้ ไม่แตะค่าที่แพทย์สั่ง คือฝั่งแอปของสิ่งที่ confirmStart()
  //      ทำให้หน้ายืนยันบนจอ ต้องเรียกหลัง start() เพราะ start() จะตั้งค่าเริ่มต้นจากค่าที่แพทย์สั่ง
  void setSessionTarget(uint16_t reps) { sessionTargetReps = reps > 99 ? 99 : reps; }
```

- [ ] **Step 2: Split the two meanings of an incoming target**

```c++
    if (wantTarget > 99) wantTarget = 99;
    // A target arriving WITH a start is the app's confirmation sheet: this run
    // only, applied after start() so it overrides the prescription start()
    // seeded. A target arriving alone is the app's /target route — an explicit
    // prescription change, which persists exactly as it did before.
    // [TH] ค่าเป้าหมายที่มาพร้อมคำสั่งเริ่ม คือค่าที่ผู้ใช้ยืนยันในแอป ใช้เฉพาะรอบนี้
    //      ใส่หลัง start() เพื่อทับค่าที่แพทย์สั่งซึ่ง start() ตั้งไว้
    //      ส่วนค่าที่มาลำพัง คือการเปลี่ยนค่าที่แพทย์สั่งผ่านเส้นทาง /target จึงบันทึกเหมือนเดิม
    if (wantTarget >= 0 && cmd != "start" && (uint16_t)wantTarget != gSettings.targetReps) {
      gSettings.targetReps = (uint16_t)wantTarget;
      gSession.setTarget((uint16_t)wantTarget);
      gTargetDirty = true;
      gLink.sendSettings(currentAsLink());
    }
```

- [ ] **Step 3: Stop the server start from bypassing the confirmation**

Task 4's review found this, and no other task covers it. `start()`'s only guard
is `if (running()) return;`, and `running()` excludes `Confirm` — so an app start
arriving on the 1 s poll while the confirm screen is up runs immediately and
silently discards the operator's pending edit.

The spec's decision table is deliberate that an app start does **not** need CYD
confirmation — the app asks its own question. But that is about a start from
*Idle*. Arriving mid-confirmation, the remote intent supersedes what is on
screen, and it must do so explicitly rather than by racing it:

```c++
    if (cmd == "start" && buttonStartStuck()) {
      Serial.println("server start refused: START reads pressed, check its wiring");
    } else if (cmd == "start") {
      // A confirmation on screen is superseded, not raced: cancel it first so
      // the pending edit is discarded deliberately and the display leaves
      // Confirm, then start on the app's own terms. Without this the start
      // runs while the CYD still shows a confirm screen for a session that is
      // already moving.
      // [TH] ถ้ามีหน้ายืนยันค้างอยู่ ให้ยกเลิกก่อน ไม่ใช่แย่งกัน เพื่อให้ค่าที่ค้างถูกทิ้งอย่างตั้งใจ
      //      และจอออกจากสถานะรอยืนยัน แล้วค่อยเริ่มตามคำสั่งของแอป
      //      ถ้าไม่ทำแบบนี้ เครื่องจะเริ่มเดินทั้งที่จอยังแสดงหน้ายืนยันของรอบที่กำลังทำงานอยู่แล้ว
      if (gSession.confirming()) gSession.cancelConfirm(now);
      gSession.start(now);
      if (wantTarget >= 0) gSession.setSessionTarget((uint16_t)wantTarget);
    } else if (cmd == "stop") {
      gSession.requestStop(now);
    }
```

Note the ordering: `setSessionTarget()` must come **after** `start()`, because
`start()` seeds `sessionTargetReps` from the prescription and would otherwise
overwrite the app's number.

- [ ] **Step 2: Compile**

Expected: success.

- [ ] **Step 3: Commit**

```bash
git add firmware/pt-leg-controller/pt-leg-controller.ino
git commit -m "feat(firmware): a target arriving with a start applies to that session only"
```

---

### Task 6: Lamps and phase naming for Confirm

**Files:**
- Verify (edit only if wrong): `firmware/pt-leg-controller/pt-leg-controller.ino` (lamp call site)
- Modify: `firmware/pt-leg-display/ui.cpp` (`phaseName()`)

**Interfaces:**
- Consumes: `Phase::Confirm`.
- Produces: nothing.

- [ ] **Step 1: Keep green meaning "running"**

`panelLampsSet` is driven by `inProgress`. Confirm must **not** count as in
progress — nothing is moving, so yellow stays lit exactly as at idle.

`running()` was already changed in Task 2 to exclude Confirm, so this step is a
verification, not an edit: confirm the lamp call site in `pt-leg-controller.ino`
passes `gSession.running()` (and not a hand-rolled phase comparison that would
have missed Task 2's change). If it passes something else, make it use
`running()`.

- [ ] **Step 2: Name the phase on the display**

In `ui.cpp`'s `phaseName()`, add a case returning `"ยืนยัน"` for `Phase::Confirm`. Check the glyphs against the generated font before assuming they render.

- [ ] **Step 3: Compile both sketches**

Expected: success.

- [ ] **Step 4: Commit**

```bash
git add firmware/pt-leg-display/ui.cpp
git commit -m "feat(firmware): name the Confirm phase on the display"
```

---

### Task 7: The confirm screen

**Files:**
- Modify: `firmware/pt-leg-display/ui.h`, `firmware/pt-leg-display/ui.cpp`
- Modify: `firmware/pt-leg-display/pt-leg-display.ino`

**Interfaces:**
- Consumes: `Phase::Confirm`; `Link::sendSets`, `Link::sendConfirm`.
- Produces:
  - `void uiShowConfirm(uint16_t reps, const DeviceSettings& s, uint32_t secsLeft)`
  - `void uiSetConfirmCallbacks(void (*onSets)(uint16_t), void (*onConfirm)(bool))`

- [ ] **Step 1: Declare the API in `ui.h`**

```c++
// Confirmation screen: shown while the controller is in Phase::Confirm.
// [TH] หน้ายืนยัน แสดงตอนที่คอนโทรลเลอร์อยู่สถานะรอยืนยัน
void uiShowConfirm(uint16_t reps, const DeviceSettings& s, uint32_t secsLeft);
void uiSetConfirmCallbacks(void (*onSets)(uint16_t), void (*onConfirm)(bool));
```

- [ ] **Step 2: Build `scrConfirm` in `ui.cpp`**

Follow `buildMain()`'s existing shape — `lv_obj_create(nullptr)`, `styleScreen()`, `makeBtn()` for the four buttons. Layout: the sets number large and centred with `−` / `+` either side, ตกลง and ยกเลิก below, a muted programme line under that, and the remaining seconds smallest of all.

Steps update a local `confirmReps` and redraw immediately, then call the sets callback. Clamp `0..99`; render `0` as `ไม่จำกัด`.

```c++
static void confirmStep(int delta) {
  int next = (int)confirmReps + delta;
  if (next < 0) next = 0;
  if (next > 99) next = 99;
  if ((uint16_t)next == confirmReps) return;
  confirmReps = (uint16_t)next;
  // Redraw first, send second. The label must never wait on a round trip — that
  // is precisely what makes the running screen's stepper feel dead when the link
  // is down. The controller still gets told, so a panel START commits the same
  // number this screen is showing.
  // [TH] วาดจอก่อน แล้วค่อยส่ง ป้ายต้องไม่รอการตอบกลับ
  //      เพราะนั่นคือสิ่งที่ทำให้ปุ่มปรับค่าบนหน้าทำงานรู้สึกเหมือนกดไม่ติดตอนลิงก์หลุด
  //      แต่ยังต้องบอกคอนโทรลเลอร์ด้วย เพื่อให้การกดปุ่มเริ่มที่หน้าปัดยืนยันตัวเลขเดียวกับที่จอแสดง
  drawConfirmReps();
  if (cbConfirmSets) cbConfirmSets(confirmReps);
}
```

- [ ] **Step 3: Wire the screen in `pt-leg-display.ino`**

Where the state message is handled, show the screen on entry to Confirm and leave it on exit. Seed `confirmReps` from the state message's `target` **once**, on the transition into Confirm — not on every state message, or an in-flight `sets` would be stomped by an echo of the old value 250 ms later.

Set the callbacks to `gLink.sendSets` / `gLink.sendConfirm`.

- [ ] **Step 4: Compile the display sketch**

Expected: success.

- [ ] **Step 5: Screenshot the screen over USB**

Set `UI_TEST_HOOK 1` in `uitest.h`, flash the display alone, drive it to the confirm screen and capture. Check every Thai glyph rendered — blanks mean the font subset is missing characters and the font must be regenerated per `firmware/README.md` (`--no-compress`, both Kanit woffs). **Set `UI_TEST_HOOK` back to `0` before committing.**

- [ ] **Step 6: Commit**

```bash
git add firmware/pt-leg-display/ui.h firmware/pt-leg-display/ui.cpp firmware/pt-leg-display/pt-leg-display.ino
git commit -m "feat(firmware): confirmation screen on the display"
```

---

### Task 8: Bench harnesses

**Files:**
- Modify: `firmware/tools/fake-controller.ps1`
- Modify: `firmware/tools/button-bench.ps1`

**Interfaces:**
- Consumes: the Confirm phase ordinal (7), `sets`, `confirm`.
- Produces: a harness that drives the confirm screen with only the CYD attached.

- [ ] **Step 1: Teach `fake-controller.ps1` the phase and messages**

Add `7` to the phase-ordinal comment. Add a key (`c`) that puts the fake controller into phase 7 so the screen can be opened on demand, handle an inbound `sets` by storing the number, and handle an inbound `confirm` by either starting the simulated cycle with that number (`ok:true`) or returning to phase 0 (`ok:false`).

- [ ] **Step 2: Add the Confirm steps to `button-bench.ps1`**

Add checklist entries, each in the existing `[pscustomobject]@{ Name; Do; Expect; Hint }` shape:

- START from idle → `phase=CONFIRM` and **no relay click**.
- A second START → `phase=LIFT`.
- STOP while in CONFIRM → `phase=IDLE`, no lift.
- Wait 30 s in CONFIRM → `phase=IDLE` on its own.
- Jumpers out, START → `phase=LIFT` directly, no CONFIRM.

- [ ] **Step 3: Extend the dry transcript**

Add the CONFIRM lines to `$script:dryTranscript` so the new patterns are exercised with no board attached. Keep the timings self-consistent with 7 s strokes.

- [ ] **Step 4: Run the dry run**

```bash
.\firmware\tools\button-bench.ps1 -DryRun
```
Expected: every step passes, including the new ones.

- [ ] **Step 5: Parse-check both scripts**

Expected: no parse errors.

- [ ] **Step 6: Commit**

```bash
git add firmware/tools/fake-controller.ps1 firmware/tools/button-bench.ps1
git commit -m "test(firmware): bench coverage for the confirmation flow"
```

---

### Task 9: App confirm-sheet logic

**Files:**
- Create: `PT-Leg-Tracker/src/lib/start-confirm.ts`
- Create: `PT-Leg-Tracker/src/lib/start-confirm.test.ts`

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `export function planConfirmStep(shown: number, delta: number): number`
  - `export function canOpenConfirm(target: number | null, targetPending: number | null): boolean`
  - `export function confirmSeed(target: number | null): number`

- [ ] **Step 1: Write the failing tests**

```typescript
import { describe, it, expect } from 'vitest'
import { planConfirmStep, canOpenConfirm, confirmSeed } from './start-confirm'

describe('planConfirmStep', () => {
  it('steps by the delta', () => {
    expect(planConfirmStep(10, 1)).toBe(11)
    expect(planConfirmStep(10, -1)).toBe(9)
  })
  it('clamps to 0..99', () => {
    expect(planConfirmStep(99, 1)).toBe(99)
    expect(planConfirmStep(0, -1)).toBe(0)
  })
  it('treats 0 as a real value, not unset', () => {
    expect(planConfirmStep(0, 1)).toBe(1)
  })
})

describe('canOpenConfirm', () => {
  it('is open when the target is known and nothing is in flight', () => {
    expect(canOpenConfirm(10, null)).toBe(true)
  })
  it('is open when the known target is 0', () => {
    expect(canOpenConfirm(0, null)).toBe(true)
  })
  it('is closed while a stepTarget edit is in flight', () => {
    expect(canOpenConfirm(10, 12)).toBe(false)
  })
  it('is closed when the target is not known yet', () => {
    expect(canOpenConfirm(null, null)).toBe(false)
  })
})

describe('confirmSeed', () => {
  it('seeds from the known target', () => {
    expect(confirmSeed(8)).toBe(8)
  })
  it('seeds 0 as 0, not as a default', () => {
    expect(confirmSeed(0)).toBe(0)
  })
})
```

- [ ] **Step 2: Run and watch them fail**

```bash
npm run test -- start-confirm
```
Expected: FAIL, module not found.

- [ ] **Step 3: Implement the module**

```typescript
// ตรรกะของแผ่นยืนยันจำนวนเซ็ตก่อนเริ่มรอบ แยกออกจาก SessionView.vue เพื่อให้ทดสอบได้
// โดยไม่ต้อง mount คอมโพเนนต์ เหมือนกับ target-step.ts
// The start-confirmation sheet's decision logic, kept out of SessionView.vue so it can be
// tested without mounting a component — same reasoning as target-step.ts.

// 0 is a legal target ("run until STOP"), so it is never treated as unset. null is the only
// "unknown" here.
// [TH] ค่า 0 เป็นเป้าหมายที่ถูกต้อง ("ทำไปเรื่อยๆ จนกว่าจะกด STOP") จึงไม่ถือว่ายังไม่ได้ตั้งค่า
//      มีแต่ null เท่านั้นที่แปลว่า "ไม่ทราบ"
export function planConfirmStep(shown: number, delta: number): number {
  return Math.min(99, Math.max(0, shown + delta))
}

// The sheet needs a real baseline to open on, and must stay shut while a stepTarget edit is
// still in flight: `target` is stale until that settles, and sending a number now would race
// the outstanding setTarget and silently revert it.
// [TH] แผ่นยืนยันต้องมีค่าตั้งต้นจริงก่อนจึงจะเปิดได้ และต้องไม่เปิดขณะที่การแก้ค่าจาก stepTarget
//      ยังค้างอยู่ เพราะค่า target ยังเป็นค่าเก่าจนกว่าจะเสร็จ การส่งตอนนี้จะไปแข่งและทับการแก้ไขนั้น
export function canOpenConfirm(target: number | null, targetPending: number | null): boolean {
  return target !== null && targetPending === null
}

export function confirmSeed(target: number | null): number {
  return target ?? 0
}
```

- [ ] **Step 4: Run the tests**

```bash
npm run test -- start-confirm
```
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add PT-Leg-Tracker/src/lib/start-confirm.ts PT-Leg-Tracker/src/lib/start-confirm.test.ts
git commit -m "feat(app): confirmation-sheet logic for the sets prompt"
```

---

### Task 10: Wire the sheet into `SessionView`

**Files:**
- Modify: `PT-Leg-Tracker/src/views/SessionView.vue`

**Interfaces:**
- Consumes: `planConfirmStep`, `canOpenConfirm`, `confirmSeed` (Task 9); `queueStart` (existing).
- Produces: nothing.

- [ ] **Step 1: Open the sheet instead of starting**

The start handler currently calls `queueStart` directly. Split it: the button opens the sheet (when `canOpenConfirm` is true), seeded with `confirmSeed(target)`. When `canOpenConfirm` is false, keep today's behaviour and start without a sheet — an unknown baseline is exactly the case where the existing code deliberately sends no target at all.

- [ ] **Step 2: Add the sheet markup**

`−` / `+` calling `planConfirmStep`, the number with `0` rendered as `ไม่จำกัด`, ตกลง and ยกเลิก. Follow the existing card/button classes in this file; do not introduce a new component library.

- [ ] **Step 3: Commit on ตกลง only**

ตกลง calls `queueStart(serverUrl, deviceCode, chosen)` and then the existing `startPolling()`. ยกเลิก closes the sheet and returns to `phase = 'before'` without calling anything.

**It must not call `setTarget()`.** That writes the stored default through `/target`; passing the number only to `queueStart` is what keeps it session-scoped.

- [ ] **Step 4: Verify the whole suite and the type-check**

```bash
npm run test
```
Expected: all pass. Baseline before this plan is 14 files / 107 tests, so the
suite must end at 107 plus whatever Task 9 added.

```bash
npm run build
```
Expected: `vue-tsc` clean, build succeeds.

- [ ] **Step 5: Commit**

```bash
git add PT-Leg-Tracker/src/views/SessionView.vue
git commit -m "feat(app): ask how many sets before starting a session"
```

---

### Task 11: Documentation

**Files:**
- Modify: `firmware/README.md`, `firmware/pin-layout.html`

**Interfaces:**
- Consumes: everything above.
- Produces: nothing.

- [ ] **Step 1: Document the flow in `firmware/README.md`**

In the run-control section: START from idle opens the confirmation when a display is linked; a second START or ตกลง commits; STOP or 30 s cancels; with the link down START runs immediately. State that sets chosen at confirmation apply to that session only, and that the settings screen is where the stored default changes.

- [ ] **Step 2: Update the simulator caption in `pin-layout.html`**

The caption currently says START begins a session from idle. Update both the `en` and `th` spans to describe the confirmation step. Keep the two languages saying the same thing.

- [ ] **Step 3: Verify the page still renders**

Open `firmware/pin-layout.html` in a browser; the rig and timeline still animate and the caption reads correctly in both languages.

- [ ] **Step 4: Commit**

```bash
git add firmware/README.md firmware/pin-layout.html
git commit -m "docs(firmware): describe the start-confirmation flow"
```

---

### Task 12: Flash both boards and run the bench

**Files:** none — verification only.

- [ ] **Step 1: Check for protocol drift**

```bash
.\firmware\sync-shared.ps1 -Check
```
Expected: `shared files in sync`.

- [ ] **Step 2: Confirm the release flags**

`BENCH_DEBUG 0`, `LINK_OVER_USB 0`, `UI_TEST_HOOK 0`.

- [ ] **Step 3: Pull the UART jumpers, then flash both boards**

Jumpers fitted during a USB flash fake a boot mode and produce checksum errors. Pull them at the **controller** end, then:

```bash
arduino-cli upload -p COM8 --fqbn "esp32:esp32:esp32:PartitionScheme=huge_app" firmware/pt-leg-controller
```
```bash
arduino-cli upload -p COM7 --fqbn "esp32:esp32:esp32:PartitionScheme=huge_app" firmware/pt-leg-display
```

Both boards must be flashed — the Phase enum changed.

- [ ] **Step 4: Refit the jumpers and run the full bench**

```bash
.\firmware\tools\button-bench.ps1
```
Expected: every step passes, including the five Confirm steps from Task 8.

- [ ] **Step 5: Verify the session-only promise by hand**

Set the default to 10 on the settings screen. Press START, step to 12, confirm, let it run. When it ends, reopen the settings screen: it must still read **10**. This is the one behaviour no automated check in this plan covers, and it is the core promise of the spec.
