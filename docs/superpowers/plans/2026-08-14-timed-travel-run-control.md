# Timed Travel, START/STOP Panel, Two Lamps, Rep Target — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace limit-switch-terminated travel with editable timed travel, replace the RUN/RESET panel with START/STOP (both normally-open), replace the single status lamp with a green/yellow pair, and add a rep target that the controller owns and both the CYD and the phone app can edit.

**Architecture:** The controller's `SessionSM` stops reading switches and runs every phase off a clock from `CycleCfg`. Two dedicated button pins replace the toggle, so the interrupt latch goes back to STOP-only. The rep target lives in the controller (NVS); the CYD pushes changes over UART and the app pushes them through the server, which hands them to the device on the command poll it already makes. Both UIs render the target the controller reports back, so neither holds an authoritative copy.

**Tech Stack:** Arduino/ESP32 C++ (controller + CYD/LVGL), FastAPI + pytest, Vue 3 + TypeScript + Vitest, static HTML/JS for `pin-layout.html`, PowerShell for bench tools.

**Spec:** `docs/superpowers/specs/2026-08-14-timed-travel-run-control-design.md`

## Global Constraints

- **No test framework on the firmware.** Firmware tasks are verified by a compile and a documented bench check, not by unit tests. Do not add a firmware test framework.
- **Comments are bilingual.** Every non-trivial comment in `firmware/`, `server/` and `PT-Leg-Tracker/src/` is English followed by a `[TH]` Thai line, matching the surrounding file. Follow the file you are editing.
- **Never round-trip a Thai or bilingual file through PowerShell `Get-Content`/`Set-Content`** — PS 5.1 double-encodes it and adds a BOM. Use the Write/Edit tools.
- **All npm/npx commands run from `PT-Leg-Tracker/`.** All pytest/uvicorn commands run from `server/` with its own venv (`server\.venv`).
- **Clamps (authoritative, copy exactly):** `maxTravelSec` 1–20 default 8; `liftSec` 1–`maxTravelSec` default 4; `lowerSec` 1–`maxTravelSec` default 4; `holdSec` 1–60 default 10; `restSec` 1–60 default 10; `targetReps` 0–99 default 10, where **0 means run until STOP**.
- **Pins (authoritative):** relay UP 22, relay DOWN 27, START 25, STOP 26, green lamp 23, yellow lamp 32, RTC SDA 21 / SCL 19, UART RX 16 / TX 17. GPIO 4 and 33 become free.
- **Both panel buttons are normally-open:** `BTN_START_NC 0`, `BTN_STOP_NC 0`.
- **No blinking lamps, ever.** Both lamps are relay-driven; pins are written only on change.
- **LVGL is pinned at 8.4.0.** Do not port display code to v9. Thai labels must avoid vowel+tone double stacks (no shaping engine).
- **Commit after every task**, conventional-commit style (`feat(firmware):`, `fix(app):`, `docs(firmware):`). No AI attribution in any commit message.

---

## File Structure

**Controller (`firmware/pt-leg-controller/`)**
- `config.h` — pins, contact types, clamps' defaults. Loses the switch block, gains the yellow lamp and the lift/lower/target defaults.
- `switches.cpp` / `switches.h` — **deleted.**
- `session.h` / `session.cpp` — timed state machine. Loses ~200 lines of switch-defence.
- `buttons.h` / `buttons.cpp` — two NO buttons, latch on STOP only.
- `status_led.h` / `status_led.cpp` — **renamed** to `panel_lamps.h` / `panel_lamps.cpp`, drives two pins.
- `settings.h` / `settings.cpp` — three new persisted fields.
- `link.h` / `link.cpp` — `LinkSettings` gains three fields, `LinkState` gains `target`, new `target` message.
- `net.h` / `net.cpp` — command poll returns a target, heartbeat sends one.
- `pt-leg-controller.ino` — wiring, boot banner, debug prints, target plumbing.

**Display (`firmware/pt-leg-display/`)**
- `settings.h`, `settingsfile.cpp`, `store.cpp` — three new fields, `/settings.json` keys.
- `ui.h` / `ui.cpp` — main-screen target stepper, three new settings spinboxes.
- `pt-leg-display.ino` — field copying and the new callback.

**Server (`server/app/`)**
- `state.py` — `desired_target`, `live_target`.
- `service.py` — target set/fetch/ack, `current()` reports it.
- `routes_app.py` — `POST /api/sessions/target`, start body.
- `routes_device.py` — command response and heartbeat body.
- `server/tests/test_state.py`, `test_service.py`, `test_app_routes.py`, `test_device_routes.py`.

**App (`PT-Leg-Tracker/src/`)**
- `types.ts` — `CurrentSession.targetReps`.
- `lib/device.ts` + `lib/device.test.ts` — `setTarget`, start with target.
- `views/SessionView.vue` — target stepper + `reps / target`.
- `views/SettingsView.vue` — device timings group.

**Docs and tools**
- `firmware/link-protocol.md`, `firmware/README.md`, `firmware/pin-layout.html`
- `firmware/tools/button-bench.ps1`, `firmware/tools/fake-controller.ps1`

---

## Task 1: Controller config — pins, contact types, new defaults

**Files:**
- Modify: `firmware/pt-leg-controller/config.h`
- Delete: `firmware/pt-leg-controller/switches.h`, `firmware/pt-leg-controller/switches.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces: `PIN_BTN_START`, `PIN_BTN_STOP`, `BTN_START_NC`, `BTN_STOP_NC`, `PIN_LAMP_GREEN`, `PIN_LAMP_YELLOW`, `DEFAULT_LIFT_SEC`, `DEFAULT_LOWER_SEC`, `DEFAULT_TARGET_REPS`. Removes `PIN_SWITCH_TOP`, `PIN_SWITCH_BOTTOM`, `SWITCH_NC`, `SWITCH_DEBOUNCE_MS`, `PIN_BTN_RUN`, `PIN_BTN_RESET`, `BTN_RUN_NC`, `BTN_RESET_NC`, `PIN_STATUS_LED`.

This task leaves the sketch not compiling. That is expected — Task 5 closes it. Do not try to keep the build green in tasks 1–4; verify each by reading, and compile once at the end of Task 5.

- [ ] **Step 1: Delete the limit-switch module**

```bash
git rm firmware/pt-leg-controller/switches.h firmware/pt-leg-controller/switches.cpp
```

- [ ] **Step 2: Replace the limit-switch block in `config.h`**

Delete the whole block from the `// limit switches ...` comment through `#define SWITCH_DEBOUNCE_MS 25` (currently lines 14–20) and put this in its place:

```c++
// The top and bottom limit switches are gone. Travel is timed now: LIFT drives
// the UP relay for liftSec and LOWER drives DOWN for lowerSec, both editable.
// GPIO 32 became the yellow lamp; GPIO 33 is free.
//
// What this costs, stated once so nobody rediscovers it the hard way: the board
// has no measurement of where the leg is. A slipping belt, a jammed actuator and
// a clean stroke are the same event to this firmware — "the relay was energized
// for lowerSec". The actuator energize cap is the only backstop left, so the
// machine must not run unattended with a leg loaded on it.
// [TH] ลิมิตสวิตช์บนและล่างถูกยกเลิกแล้ว ตอนนี้การเคลื่อนที่ใช้เวลาเป็นตัวกำหนด
//      สถานะยกขาจ่ายไฟรีเลย์ขึ้นเป็นเวลา liftSec และสถานะลดลงจ่ายไฟรีเลย์ลงเป็นเวลา lowerSec ปรับได้ทั้งคู่
//      ขา 32 กลายเป็นไฟเหลือง ส่วนขา 33 ว่าง
//      สิ่งที่ต้องแลก บอกไว้ครั้งเดียวเพื่อไม่ให้ต้องไปค้นพบเองตอนเกิดเรื่อง
//      บอร์ดไม่มีการวัดตำแหน่งขาเลย สายพานลื่น ชุดขับเคลื่อนติดขัด และการเคลื่อนที่ปกติ
//      เป็นเหตุการณ์เดียวกันหมดสำหรับเฟิร์มแวร์นี้ คือ "รีเลย์จ่ายไฟครบ lowerSec"
//      เหลือเพียงเพดานเวลาจ่ายไฟของชุดขับเคลื่อนเป็นด่านสุดท้าย
//      จึงต้องไม่ปล่อยให้เครื่องทำงานโดยไม่มีคนดูขณะที่มีขาวางอยู่
```

- [ ] **Step 3: Replace the panel-button block in `config.h`**

Delete the block from `// panel buttons (momentary, INPUT_PULLUP) — two of them` through `#define BUTTON_DEBOUNCE_MS 25` (currently lines 22–65) and put this in its place:

```c++
// panel buttons (momentary, INPUT_PULLUP) — two of them, one job each
// START begins a session; STOP ends one, and when the machine is already idle it
// clears the rep count and any latched FAULT. Separate pins rather than one
// toggle, so a press means the same thing in every phase and the interrupt latch
// can go back to being wired to STOP alone.
// [TH] ปุ่มหน้าปัด (กดชั่วขณะ) ใช้ INPUT_PULLUP มีสองปุ่ม ปุ่มละหน้าที่
//      START เริ่มรอบ STOP จบรอบ และถ้าเครื่องว่างอยู่แล้วจะล้างจำนวนครั้งและสถานะ FAULT ที่ค้างอยู่
//      ใช้ขาแยกกันแทนปุ่มสลับปุ่มเดียว การกดจึงมีความหมายเดียวกันในทุกสถานะ
//      และกลไกจับค้างของอินเทอร์รัปต์กลับไปผูกกับปุ่ม STOP เพียงปุ่มเดียวได้เหมือนเดิม
#define PIN_BTN_START 25
#define PIN_BTN_STOP 26

// Per-button contact type: 1 = normally-closed, 0 = normally-open.
// Both are NO on this panel. An NO button that loses its wire is simply dead —
// it cannot raise an edge, and it cannot read as held down either. That kills the
// old fail-safe argument for NC in both directions: a broken STOP wire no longer
// stops the machine by failing, and a broken START wire no longer jams the panel.
// The stuck-line guard in buttons.cpp covers what is left, which on an NO button
// is a SHORT rather than a break.
// [TH] ชนิดหน้าสัมผัสของแต่ละปุ่ม: 1 = ปกติปิด (NC), 0 = ปกติเปิด (NO)
//      หน้าปัดชุดนี้เป็น NO ทั้งคู่ ปุ่มแบบ NO ที่สายขาดจะตายเงียบ
//      คือไม่เกิดขอบสัญญาณ และไม่อ่านได้ว่ากดค้างด้วย
//      เหตุผลเรื่องความปลอดภัยของแบบ NC จึงหมดไปทั้งสองทาง
//      สาย STOP ที่ขาดไม่ได้ทำให้เครื่องหยุดอีกต่อไป และสาย START ที่ขาดก็ไม่ได้ทำให้หน้าปัดใช้ไม่ได้
//      ส่วนที่เหลือคือกลไกตรวจสายค้างใน buttons.cpp ซึ่งบนปุ่ม NO คือสายลัดวงจร ไม่ใช่สายขาด
// Guarded so a bench build can flip one with
// --build-property compiler.cpp.extra_flags=-DBTN_START_NC=1 --clean
// [TH] ครอบด้วย #ifndef เพื่อให้สลับขั้วผ่าน build flag ตอนทดสอบได้
#ifndef BTN_START_NC
#define BTN_START_NC 0
#endif
#ifndef BTN_STOP_NC
#define BTN_STOP_NC 0
#endif
#define BUTTON_DEBOUNCE_MS 25 // เวลากันเด้งของปุ่ม (มิลลิวินาที)
```

- [ ] **Step 4: Replace the status-LED block in `config.h`**

Delete the block from `// Panel status LED — 12 V, switched by a mechanical relay off this pin.` through `// #define LED_ACTIVE_LOW` (currently lines 67–99) and put this in its place:

```c++
// Panel lamps — 12 V, each switched by a mechanical relay off its pin. Exactly
// one is lit at any moment:
//   green   a session is in progress and the actuator may move — keep clear
//   yellow  idle, or a latched FAULT; nothing is going to move on its own
// A FAULT reads yellow because it is not running and the relays really are off.
// The lamp answers "is it about to move", nothing else; a fault is diagnosed on
// the CYD, the serial log and the card row.
//
// No blinking and no heartbeat, on either lamp. Each change of state is a contact
// operation on a mechanical relay, so a 5 Hz blink is 18,000 operations an hour
// and eats a ~100k-operation relay in a few hours of one latched fault. Solid
// states cost four operations per session (two lamps, on and off).
// [TH] ไฟหน้าปัด 12 โวลต์ แต่ละดวงสั่งผ่านรีเลย์ของตัวเอง ติดครั้งละดวงเดียวเสมอ
//      เขียว = อยู่ในรอบการทำงาน ชุดขับเคลื่อนอาจเคลื่อนที่ ให้อยู่ห่างไว้
//      เหลือง = ว่าง หรือค้างสถานะผิดพลาด จะไม่มีอะไรเคลื่อนที่เอง
//      สถานะผิดพลาดแสดงเป็นสีเหลือง เพราะไม่ได้กำลังทำงานและรีเลย์ดับจริงๆ
//      หลอดตอบแค่คำถามเดียวว่า "กำลังจะเคลื่อนที่ไหม" การวินิจฉัยให้ดูที่จอ พอร์ตอนุกรม และแถวข้อมูลบนการ์ด
//      ห้ามกะพริบและห้ามวาบเป็นจังหวะทั้งสองดวง ทุกการเปลี่ยนสถานะคือการตัดต่อหน้าสัมผัสของรีเลย์กลไก
//      การกะพริบ 5 ครั้งต่อวินาทีคือ 18,000 ครั้งต่อชั่วโมง
//      กินอายุรีเลย์ที่ทนได้ราวแสนครั้งจนหมดในไม่กี่ชั่วโมงจากความผิดพลาดที่ค้างอยู่ครั้งเดียว
//      การใช้สถานะติดค้างหรือดับสนิทเสียแค่สี่ครั้งต่อรอบ (สองดวง ติดและดับ)
//
// 23 was already the status lamp. 32 is the pin the TOP limit switch freed; it is
// not a strapping pin and does not glitch at boot the way 0/2/5/12/15 can.
// [TH] ขา 23 เคยเป็นไฟแสดงสถานะอยู่แล้ว ส่วนขา 32 คือขาที่ว่างจากลิมิตสวิตช์บน
//      ไม่ใช่ขา strapping และไม่มีสัญญาณแกว่งตอนบูตแบบขา 0/2/5/12/15
#define PIN_LAMP_GREEN 23
#define PIN_LAMP_YELLOW 32
// Uncomment for an active-low relay board — most cheap opto-isolated modules are.
// Covers BOTH lamps, on the assumption they are two channels of one board. Check
// before first power-up: backwards, green is lit whenever the machine is idle,
// which is the dangerous way round.
// [TH] เปิดบรรทัดนี้ถ้าบอร์ดรีเลย์เป็นแบบสั่งงานด้วยลอจิกต่ำ ซึ่งโมดูลออปโตราคาถูกส่วนใหญ่เป็นแบบนั้น
//      ใช้กับหลอดทั้งสองดวง โดยถือว่าเป็นสองช่องของบอร์ดเดียวกัน
//      ต้องตรวจก่อนเปิดเครื่องครั้งแรก ถ้ากลับด้าน ไฟเขียวจะติดตอนเครื่องว่าง ซึ่งเป็นด้านที่อันตราย
// #define LED_ACTIVE_LOW
```

- [ ] **Step 5: Add the new cycle defaults in `config.h`**

Replace the `// cycle defaults (seconds)` block at the end of the file with:

```c++
// cycle defaults (seconds) — user-adjustable in the settings UI
// [TH] ค่าตั้งต้นของรอบ (วินาที) ผู้ใช้ปรับได้ในหน้าตั้งค่า
#define DEFAULT_LIFT_SEC 4  // เวลาที่รีเลย์ยกขึ้นจ่ายไฟ
#define DEFAULT_LOWER_SEC 4 // เวลาที่รีเลย์ลดลงจ่ายไฟ
#define DEFAULT_HOLD_SEC 10 // เวลาค้างตอนยกสุด
#define DEFAULT_REST_SEC 10 // เวลาพักตอนลงสุด

// How many reps a session runs before it stops itself. 0 = run until STOP.
// [TH] จำนวนครั้งที่รอบหนึ่งทำก่อนหยุดเอง ค่า 0 = ทำไปเรื่อยๆ จนกว่าจะกด STOP
#define DEFAULT_TARGET_REPS 10
```

- [ ] **Step 6: Commit**

```bash
git add firmware/pt-leg-controller/config.h firmware/pt-leg-controller/switches.h firmware/pt-leg-controller/switches.cpp
git commit -m "feat(firmware): retire limit switches, split START/STOP, add second lamp pin"
```

---

## Task 2: Controller session state machine — timed phases and the rep target

**Files:**
- Modify: `firmware/pt-leg-controller/session.h`, `firmware/pt-leg-controller/session.cpp`

**Interfaces:**
- Consumes: `DEFAULT_*` from Task 1.
- Produces: `CycleCfg{liftMs, lowerMs, holdMs, restMs, maxTravelMs, targetReps}`; `SessionSM::start(uint32_t)`, `requestStop(uint32_t)`, `tick(uint32_t)`, `setTarget(uint16_t)`, `target() const -> uint16_t`, `forceFault(uint32_t)`, `running()`, `faulted()`, `phase()`, `elapsedSec()`, `reps()`, `takeStartedEdge()`, `takeStoppedEdge()`, `takeStrokeRenewEdge()`, `finalElapsedSec()`, `finalReps()`, `finalFaulted()`. **Removed:** `toggle()`, `toggleWouldStart()`, `reset()`, `seedLegPosition()`, `downConfirmed()`.

- [ ] **Step 1: Rewrite `session.h`**

Replace the whole file with:

```c++
#pragma once
#include <stdint.h>

// เครื่องสถานะของหนึ่งรอบการทำงาน (session state machine)
// ลำดับ: ยกขึ้น -> ค้าง -> ลดลง -> พัก วนซ้ำ ทุกสถานะจบด้วยเวลา
// Session state machine: Lift -> Hold -> Lower -> Rest, every phase ended by a
// clock. There are no limit switches: travel length IS liftMs / lowerMs.

// [TH] สถานะของรอบ: ว่าง / ยกขึ้น / ค้าง / ลดลง / พัก / กำลังลงเพื่อหยุด / ผิดพลาด
enum class Phase { Idle, Lift, Hold, Lower, Rest, SafeLower, Fault };

struct CycleCfg {
  uint32_t liftMs;       // เวลาที่รีเลย์ยกขึ้นจ่ายไฟต่อหนึ่งจังหวะ
  uint32_t lowerMs;      // เวลาที่รีเลย์ลดลงจ่ายไฟต่อหนึ่งจังหวะ
  uint32_t holdMs;       // เวลาค้างตอนยกสุด
  uint32_t restMs;       // เวลาพักตอนลงสุด
  uint32_t maxTravelMs;  // ceiling liftMs/lowerMs are clamped to — เพดานที่ใช้จำกัดสองค่าแรก
  uint16_t targetReps;   // stop after this many reps; 0 = until STOP — 0 = ไม่จำกัด
};

class SessionSM {
 public:
  CycleCfg cfg{4000, 4000, 10000, 10000, 8000, 10};

  // Idle/Fault -> Lift. From Fault the latch is cleared and the leg is lowered
  // first: with no limit switches there is no way to know where it is, and
  // lowering a leg that is already down costs one stroke against the bottom stop
  // while lifting one that is already up drives it into the top.
  // [TH] จากสถานะว่างหรือผิดพลาดไปสถานะยกขา ถ้ามาจากสถานะผิดพลาดจะล้างสถานะและลดขาลงก่อน
  //      เพราะไม่มีลิมิตสวิตช์แล้วจึงไม่รู้ว่าขาอยู่ตรงไหน
  //      การลดขาที่อยู่ล่างสุดอยู่แล้วเสียแค่การเคลื่อนที่หนึ่งครั้งชนกลไกด้านล่าง
  //      แต่การยกขาที่อยู่บนสุดอยู่แล้วคือการดันเข้าหากลไกด้านบน
  void start(uint32_t nowMs);

  // Running -> SafeLower -> Idle, reporting the session.
  // Fault -> SafeLower -> Idle, quiet: that session was reported when it faulted,
  // and a second stopped edge would file it on the card twice.
  // Idle -> zero the rep count. This is what replaces the retired RESET button.
  // [TH] กำลังทำงาน -> ลดขาเพื่อหยุด -> ว่าง พร้อมรายงานรอบนั้น
  //      ผิดพลาด -> ลดขาเพื่อหยุด -> ว่าง แบบเงียบ เพราะรอบนั้นถูกรายงานไปแล้วตอนที่ผิดพลาด
  //      ถ้าตั้งสัญญาณหยุดซ้ำ รอบเดียวกันจะลงการ์ดสองครั้ง
  //      ว่าง -> ล้างจำนวนครั้ง ซึ่งเป็นสิ่งที่มาแทนปุ่ม RESET ที่เลิกใช้แล้ว
  void requestStop(uint32_t nowMs);

  void forceFault(uint32_t nowMs);   // external safety trip (actuator cap)
                                     // [TH] บังคับเข้าสถานะผิดพลาดจากภายนอก (เพดานเวลารีเลย์)

  // Change the target mid-session as well as at rest, because both UIs can edit
  // it while the machine runs. Lowering it below the reps already done ends the
  // session at the end of the current Rest, which is where the check lives.
  // [TH] เปลี่ยนเป้าหมายได้ทั้งตอนว่างและตอนกำลังทำงาน เพราะทั้งสองหน้าจอแก้ได้ระหว่างเครื่องเดิน
  //      ถ้าตั้งต่ำกว่าจำนวนครั้งที่ทำไปแล้ว รอบจะจบเมื่อสิ้นสุดสถานะพักครั้งปัจจุบัน ซึ่งเป็นจุดที่ตรวจค่านี้
  void setTarget(uint16_t reps) { cfg.targetReps = reps; }
  uint16_t target() const { return cfg.targetReps; }

  void tick(uint32_t nowMs);
  bool running() const { return ph != Phase::Idle && ph != Phase::Fault; }
  bool faulted() const { return ph == Phase::Fault; }
  Phase phase() const { return ph; }
  uint32_t elapsedSec(uint32_t nowMs) const;
  uint16_t reps() const { return repCount; }
  bool takeStartedEdge();  // อ่าน+ล้างสัญญาณ "เพิ่งเริ่มรอบ" (edge)
  // Read+clear "a session just ended". Raised only by finish(), i.e. exactly once
  // per session that is reported — so NOT by a lowering started from Fault, which
  // is not a session.
  // [TH] อ่านและล้างสัญญาณ "รอบเพิ่งจบ" ยกโดย finish() เท่านั้น คือครั้งเดียวต่อรอบที่ถูกรายงาน
  //      จึงไม่ยกให้การลดขาที่เริ่มจากสถานะผิดพลาด เพราะนั่นไม่ใช่รอบการทำงาน
  bool takeStoppedEdge();
  uint32_t finalElapsedSec() const { return finalElapsed; }
  uint16_t finalReps() const { return finalRepCount; }
  // Whether the session that just ended ended in a fault. Use this and not
  // faulted() when reporting a finished session: faulted() reads the live phase.
  // [TH] รอบที่เพิ่งจบจบเพราะความผิดพลาดหรือไม่ ตอนรายงานรอบที่จบแล้วให้ใช้ค่านี้ ไม่ใช่ faulted()
  bool finalFaulted() const { return finalFault; }
  // One-shot: a new stroke has begun that may drive the same direction as the
  // last one, so the actuator's energize clock has to be restarted. Read once per
  // loop, before the relays are commanded.
  // [TH] อ่านได้ครั้งเดียว: มีการเคลื่อนที่ครั้งใหม่ที่อาจสั่งไปทางเดิม ต้องเริ่มจับเวลาการจ่ายไฟใหม่
  //      อ่านหนึ่งครั้งต่อลูป ก่อนสั่งรีเลย์
  bool takeStrokeRenewEdge();

 private:
  void enter(Phase p, uint32_t nowMs);
  void finish(uint32_t nowMs, Phase to, bool fault);
  Phase ph = Phase::Idle;
  uint32_t phaseStart = 0, sessionStart = 0;
  uint16_t repCount = 0;
  uint32_t finalElapsed = 0;
  uint16_t finalRepCount = 0;
  bool finalFault = false;
  // This SafeLower is bringing the leg down rather than ending a session — set
  // from Fault (that session was reported when it faulted) and from a start out
  // of Fault (the session has not begun yet). Either way it must not raise a
  // stopped edge. Invariant: recovering implies SafeLower.
  // [TH] การลดขาครั้งนี้เป็นการเอาขาลง ไม่ใช่การจบรอบ
  //      ตั้งจากสถานะผิดพลาด ซึ่งรอบนั้นถูกรายงานไปแล้ว และตั้งจากการกดเริ่มขณะอยู่ในสถานะผิดพลาด
  //      ซึ่งรอบยังไม่เริ่ม ทั้งสองกรณีต้องไม่ตั้งสัญญาณหยุด
  //      ข้อกำหนด: ถ้า recovering เป็นจริง สถานะต้องเป็นลดขาเพื่อหยุดเสมอ
  bool recovering = false;
  // A start is waiting for the recovery lowering above to finish. Consumed by the
  // SafeLower case, which then lifts instead of going to Idle.
  // [TH] มีคำสั่งเริ่มรออยู่ระหว่างที่การลดขาข้างต้นทำงาน
  //      สถานะลดขาเพื่อหยุดจะอ่านค่านี้แล้วยกขาต่อแทนที่จะกลับไปสถานะว่าง
  bool startAfterRecovery = false;
  bool strokeRenewEdge = false;
  bool startedEdge = false, stoppedEdge = false;
};
```

- [ ] **Step 2: Rewrite `session.cpp`**

Replace the whole file with:

```c++
// ตรรกะเครื่องสถานะของรอบการทำงาน: ทุกสถานะเปลี่ยนด้วยเวลา ไม่มีลิมิตสวิตช์แล้ว
// Session state-machine logic. Every transition is a clock; there are no switches.
#include "session.h"

void SessionSM::enter(Phase p, uint32_t nowMs) {
  // Entering a descending phase begins a stroke that may keep driving the same
  // direction as the phase before it, which the actuator cannot see for itself.
  // Raised here, the one place every entry passes through, rather than inferred
  // by the sketch from a phase it happened to observe.
  // [TH] การเข้าสู่สถานะที่สั่งลงคือการเริ่มเคลื่อนที่ครั้งใหม่ ซึ่งอาจสั่งไปทางเดิมกับสถานะก่อนหน้า
  //      ตัวขับเคลื่อนมองไม่เห็นเรื่องนี้เอง จึงยกสัญญาณตรงนี้ซึ่งเป็นจุดเดียวที่การเข้าสถานะทุกครั้งต้องผ่าน
  //      แทนที่จะให้สเก็ตช์เดาจากสถานะที่มันบังเอิญเห็น
  if (p == Phase::SafeLower) strokeRenewEdge = true;
  ph = p;
  phaseStart = nowMs;  // จับเวลาเริ่มสถานะใหม่
}

bool SessionSM::takeStrokeRenewEdge() {
  bool v = strokeRenewEdge;
  strokeRenewEdge = false;
  return v;
}

void SessionSM::finish(uint32_t nowMs, Phase to, bool fault) {
  // [TH] ปิดรอบ: เก็บเวลารวมและจำนวนครั้งไว้เป็นค่าสุดท้าย แล้วตั้งสัญญาณ "หยุด"
  finalElapsed = (ph == Phase::Idle) ? 0 : (nowMs - sessionStart) / 1000;
  finalRepCount = repCount;
  // Latched with the other totals, not read back off the live phase later: a stop
  // press taken in the same loop iteration as the fault that ended the session
  // would otherwise let faulted() answer "no" for a session that tripped a net.
  // [TH] เก็บค่าพร้อมยอดรวมอื่นๆ ไม่ได้ไปอ่านจากสถานะปัจจุบันทีหลัง
  //      ถ้ามีการกดหยุดในลูปรอบเดียวกับความผิดพลาดที่จบรอบนั้น faulted() จะตอบว่าไม่ผิดพลาด
  //      ทั้งที่รอบนั้นชนกลไกความปลอดภัย
  finalFault = fault;
  stoppedEdge = true;
  enter(to, nowMs);
}

void SessionSM::start(uint32_t nowMs) {
  if (running()) return;  // ignore while a session is under way — ถ้ากำลังทำงานอยู่ให้ข้าม
  // Out of a latched Fault, lower before lifting. The fault stopped the relays
  // wherever the leg happened to be, and this firmware has no way to find out
  // where that was. The lowering runs first and the start is queued behind it.
  // [TH] ถ้ามาจากสถานะผิดพลาดที่ค้างอยู่ ให้ลดขาก่อนแล้วค่อยยก
  //      ความผิดพลาดตัดรีเลย์ตรงจุดไหนก็ได้ที่ขาอยู่ และเฟิร์มแวร์นี้ไม่มีทางรู้ว่าตรงไหน
  //      จึงให้การลดขาทำงานก่อน แล้วเข้าคิวเริ่มรอบไว้ข้างหลัง
  if (ph == Phase::Fault) {
    recovering = true;
    startAfterRecovery = true;
    enter(Phase::SafeLower, nowMs);
    return;
  }
  sessionStart = nowMs;
  repCount = 0;
  startedEdge = true;
  recovering = false;
  enter(Phase::Lift, nowMs);
}

void SessionSM::requestStop(uint32_t nowMs) {
  // [TH] คำขอหยุด: ตอบสนองต่างกันตามสถานะปัจจุบัน เพื่อให้ขาลงถึงพื้นก่อนหยุดเสมอ
  switch (ph) {
    case Phase::Idle:
      // Nothing is moving and nothing is latched, so STOP is the "clear" button:
      // it zeroes the count the panel and the screen are showing. This is what
      // the retired RESET did.
      // [TH] ไม่มีอะไรเคลื่อนที่และไม่มีสถานะค้าง ปุ่ม STOP จึงทำหน้าที่ล้างค่า
      //      คือล้างจำนวนครั้งที่หน้าปัดและหน้าจอแสดงอยู่ ซึ่งเป็นสิ่งที่ปุ่ม RESET ที่เลิกใช้เคยทำ
      repCount = 0;
      // The edges and the finished session's totals are deliberately left alone. A
      // session can finish and a STOP edge be taken inside the same loop iteration
      // — tick() runs before the buttons, and the edges are consumed after them —
      // and clearing them here would throw away both the server "stopped" event
      // and the done message, which is the session's only row on the card.
      // [TH] จงใจไม่ล้างขอบสัญญาณและยอดรวมของรอบที่จบไปแล้ว
      //      รอบหนึ่งจบและมีการกด STOP ในลูปรอบเดียวกันได้ เพราะ tick() ทำงานก่อนปุ่ม
      //      และการอ่านขอบสัญญาณอยู่หลังปุ่ม การล้างตรงนี้จะทิ้งทั้งเหตุการณ์ฝั่งเซิร์ฟเวอร์
      //      และข้อความ done ซึ่งเป็นแถวข้อมูลเดียวของรอบนั้นบนการ์ด
      return;
    case Phase::Fault:
      // A fault stops the relays wherever the leg is, and it can be halfway up.
      // STOP is the button an operator reaches for, so it brings the leg down and
      // clears the latch when it gets there. The fault was already recorded when
      // it happened, so this lowering reports nothing — see `recovering`.
      // [TH] ความผิดพลาดตัดรีเลย์ไม่ว่าขาจะอยู่ตรงไหน และอาจค้างกลางทาง
      //      ปุ่ม STOP คือปุ่มที่ผู้ใช้เอื้อมไปกด จึงพาขาลงมาและล้างสถานะเมื่อถึงพื้น
      //      ความผิดพลาดถูกบันทึกไปแล้วตอนเกิดเหตุ การลดขาครั้งนี้จึงไม่รายงานอะไร ดูตัวแปร recovering
      recovering = true;
      startAfterRecovery = false;
      enter(Phase::SafeLower, nowMs);
      return;
    case Phase::Lift:
    case Phase::Hold:
    case Phase::Lower:
      enter(Phase::SafeLower, nowMs);  // bring the leg down, then stop — ลดขาลงก่อนแล้วค่อยหยุด
      return;
    case Phase::Rest:
      finish(nowMs, Phase::Idle, false);  // already down — ขาลงอยู่แล้ว หยุดได้เลย
      return;
    case Phase::SafeLower:
      // Already lowering. A STOP here cancels a start that was queued behind a
      // recovery, which is the operator changing their mind mid-descent.
      // [TH] กำลังลดขาอยู่แล้ว การกด STOP ตรงนี้คือการยกเลิกคำสั่งเริ่มที่เข้าคิวรออยู่หลังการลดขา
      //      ซึ่งคือผู้ใช้เปลี่ยนใจระหว่างที่ขากำลังลง
      startAfterRecovery = false;
      return;
  }
}

void SessionSM::forceFault(uint32_t nowMs) {
  if (ph == Phase::Idle || ph == Phase::Fault) return;
  // A recovery lower has no session of its own to end — the fault that preceded
  // it already sent one. Latch back to Fault without raising a second stopped
  // edge, which would put a duplicate row on the card for one session.
  // [TH] การลดขาเพื่อกู้สถานการณ์ไม่มีรอบของตัวเองให้จบ เพราะความผิดพลาดก่อนหน้าส่งไปแล้ว
  //      จึงกลับไปสถานะผิดพลาดโดยไม่ตั้งสัญญาณหยุดซ้ำ ซึ่งจะทำให้มีแถวซ้ำในการ์ดสำหรับรอบเดียว
  if (recovering) {
    recovering = false;
    startAfterRecovery = false;
    enter(Phase::Fault, nowMs);
    return;
  }
  finish(nowMs, Phase::Fault, true);
}

void SessionSM::tick(uint32_t nowMs) {
  uint32_t dt = nowMs - phaseStart;  // เวลาที่อยู่ในสถานะปัจจุบัน
  switch (ph) {
    case Phase::Idle:
    case Phase::Fault:
      return;
    case Phase::Lift:
      if (dt >= cfg.liftMs) enter(Phase::Hold, nowMs);
      return;
    case Phase::Hold:
      if (dt >= cfg.holdMs) enter(Phase::Lower, nowMs);
      return;
    case Phase::Lower:
      if (dt >= cfg.lowerMs) enter(Phase::Rest, nowMs);
      return;
    case Phase::Rest:
      if (dt < cfg.restMs) return;
      repCount++;  // พักครบ -> นับ 1 ครั้ง
      // The target is checked here and nowhere else, because here is the one
      // point in the cycle where the leg is already down: the session can end on
      // the spot with no safe-lower and no fault. A target of 0 means "until
      // STOP", and a target already passed (someone lowered it mid-session) ends
      // the session at the end of this rest for the same reason.
      // [TH] ตรวจเป้าหมายที่นี่ที่เดียว เพราะเป็นจุดเดียวในรอบที่ขาลงถึงพื้นอยู่แล้ว
      //      รอบจึงจบได้ทันทีโดยไม่ต้องลดขาและไม่เป็นความผิดพลาด
      //      ค่า 0 แปลว่าทำจนกว่าจะกด STOP ส่วนเป้าหมายที่เลยมาแล้ว (มีคนปรับลดกลางรอบ)
      //      ก็จบรอบเมื่อสิ้นสุดการพักครั้งนี้ด้วยเหตุผลเดียวกัน
      if (cfg.targetReps != 0 && repCount >= cfg.targetReps) {
        finish(nowMs, Phase::Idle, false);
        return;
      }
      enter(Phase::Lift, nowMs);
      return;
    case Phase::SafeLower:
      if (dt < cfg.lowerMs) return;
      if (recovering) {
        // Lowering after a fault, or ahead of a start that was pressed in one.
        // Either way the session that faulted was already reported, so this ends
        // quietly; a second stopped edge would file it on the card twice.
        // [TH] เป็นการลดขาหลังความผิดพลาด หรือลดขาก่อนคำสั่งเริ่มที่กดตอนอยู่ในสถานะผิดพลาด
        //      ทั้งสองกรณีรอบที่ผิดพลาดถูกรายงานไปแล้ว ตรงนี้จึงจบแบบเงียบ
        //      ถ้าตั้งสัญญาณหยุดซ้ำ รอบเดียวกันจะลงการ์ดสองครั้ง
        recovering = false;
        if (startAfterRecovery) {
          startAfterRecovery = false;
          sessionStart = nowMs;
          repCount = 0;
          startedEdge = true;
          enter(Phase::Lift, nowMs);
          return;
        }
        enter(Phase::Idle, nowMs);
        return;
      }
      finish(nowMs, Phase::Idle, false);  // a clean stop — หยุดตามปกติ
      return;
  }
}

uint32_t SessionSM::elapsedSec(uint32_t nowMs) const {
  // [TH] เวลาที่ผ่านไปของรอบ (วินาที): ว่าง=0, ผิดพลาด=ค่าที่แช่ไว้ตอน fault, อื่นๆ=นับสด
  if (ph == Phase::Idle) return 0;
  // A recovery lowering has no session of its own, so it has no elapsed time
  // either. Measuring from sessionStart would report the age of whatever ran
  // before it — after a power-cut reboot that is the uptime — and running() is
  // true during a recovery, so that number went out on every heartbeat.
  // [TH] การลดขาเพื่อกู้สถานการณ์ไม่มีรอบของตัวเอง จึงไม่มีเวลาที่ผ่านไปของตัวเอง
  //      ถ้าวัดจาก sessionStart จะได้อายุของสิ่งที่ทำงานก่อนหน้า
  //      หลังไฟดับแล้วเปิดใหม่ ค่านั้นคือเวลาที่เครื่องเปิดมา
  //      และระหว่างกู้สถานการณ์ running() เป็นจริง ตัวเลขนั้นจึงถูกส่งออกทุกครั้งที่ส่งสัญญาณชีพ
  if (recovering) return 0;
  if (ph == Phase::Fault) return finalElapsed;  // frozen at fault time — แช่ค่าไว้ ณ เวลาที่ผิดพลาด
  return (nowMs - sessionStart) / 1000;
}

bool SessionSM::takeStartedEdge() {
  bool v = startedEdge;
  startedEdge = false;
  return v;
}

bool SessionSM::takeStoppedEdge() {
  bool v = stoppedEdge;
  stoppedEdge = false;
  return v;
}
```

- [ ] **Step 3: Check the invariants by reading**

Confirm, by reading the file you just wrote:
1. `recovering == true` only ever coexists with `ph == Phase::SafeLower`.
2. `startAfterRecovery` is cleared on every path that leaves `SafeLower`, and by `requestStop` and `forceFault`.
3. `finish()` is called exactly once per reported session: the target path, the Rest path in `requestStop`, the clean `SafeLower` path, and `forceFault`'s non-recovering branch.
4. Nothing references `switchTop`, `switchBottom`, `topPressed`, `bottomPressed`, `liftedSinceBottom` or `maxTravelMs` as a phase terminator.

- [ ] **Step 4: Commit**

```bash
git add firmware/pt-leg-controller/session.h firmware/pt-leg-controller/session.cpp
git commit -m "feat(firmware): time every session phase and stop at a rep target"
```

---

## Task 3: Controller buttons — two normally-open pins, latch on STOP only

**Files:**
- Modify: `firmware/pt-leg-controller/buttons.h`, `firmware/pt-leg-controller/buttons.cpp`

**Interfaces:**
- Consumes: `PIN_BTN_START`, `PIN_BTN_STOP`, `BTN_START_NC`, `BTN_STOP_NC`, `BUTTON_DEBOUNCE_MS` (Task 1).
- Produces: `buttonsBegin()`, `buttonsPoll(uint32_t)`, `buttonStartPressed() -> bool`, `buttonStopPressed() -> bool`, `buttonStartStuck() -> bool`. **Removed:** `buttonRunPressed`, `buttonResetPressed`, `buttonRunWireSuspect`.

- [ ] **Step 1: Rewrite `buttons.h`**

```c++
#pragma once
#include <stdint.h>

// อ่านปุ่มหน้าปัดสองปุ่ม (START/STOP) แบบกันเด้งและจับ "ขอบขากด" ครั้งเดียว
// Debounced, edge-triggered reads of the two panel buttons. Each accessor returns
// true for exactly ONE loop on the press edge, so a held button fires once.
// Contact type is per button: BTN_START_NC / BTN_STOP_NC in config.h, because a
// panel can mix NO and NC parts even when both are specified NO.
// [TH] คืนค่า true เพียงลูปเดียวตอนกด เพื่อให้กดค้างก็ทำงานครั้งเดียว
//      ชนิดหน้าสัมผัสแยกรายปุ่มที่ config.h เพราะหน้าปัดอาจมีปุ่มคนละแบบปนกัน
void buttonsBegin();
void buttonsPoll(uint32_t nowMs);  // call every loop, before SessionSM::tick
                                   // [TH] เรียกทุกลูปก่อน SessionSM::tick

// One-shot press edges.
// STOP alone carries the interrupt latch: a press lost inside a loop iteration
// that blocked on the network must still be able to stop the machine, and a
// latched press can inflate an electrical transient into a full-width press. That
// trade is worth taking for a stop and never for a start, and with separate pins
// it is settled by the wiring rather than by the phase.
// [TH] ขอบกดครั้งเดียว
//      มีเพียงปุ่ม STOP ที่ใช้กลไกจับค้างของอินเทอร์รัปต์
//      การกดที่หายไปในลูปรอบที่ค้างรอเครือข่ายต้องยังสั่งหยุดเครื่องได้
//      และการจับค้างอาจทำให้สัญญาณรบกวนกลายเป็นการกดเต็มเกณฑ์
//      ข้อแลกเปลี่ยนนี้คุ้มสำหรับการหยุดแต่ไม่คุ้มสำหรับการเริ่ม
//      พอแยกขากันแล้วเรื่องนี้จบด้วยการต่อสาย ไม่ต้องดูสถานะ
bool buttonStartPressed();
bool buttonStopPressed();

// True while the START line reads pressed with no plausible person pressing it:
// since power-up, or held longer than anyone holds a button. On an NO button that
// is a SHORT — a pinched loom, a jammed button, a screwdriver left across the
// terminals — and it would restart the machine the moment each session ended. No
// session starts while it lasts, including one commanded from the server. Clears
// as soon as the line moves, which a real held button does on release.
// [TH] เป็นจริงเมื่อสาย START อ่านได้ว่ากดโดยไม่น่าจะมีคนกดจริง
//      คืออ่านได้แบบนั้นตั้งแต่เปิดเครื่อง หรือค้างนานเกินกว่าที่คนจะกดค้าง
//      สำหรับปุ่มแบบ NO นั่นคือสายลัดวงจร เช่นสายถูกหนีบ ปุ่มค้าง หรือไขควงพาดขั้วอยู่
//      และจะทำให้เครื่องเริ่มรอบใหม่ทันทีทุกครั้งที่รอบก่อนหน้าจบ
//      จึงไม่ให้เริ่มรอบใดๆ ตราบที่ยังเป็นแบบนั้น รวมถึงรอบที่สั่งจากเซิร์ฟเวอร์
//      ค่านี้ล้างทันทีที่ระดับขาเปลี่ยน ซึ่งปุ่มที่ถูกกดค้างจริงจะเปลี่ยนเมื่อปล่อย
bool buttonStartStuck();
```

- [ ] **Step 2: Edit `buttons.cpp` — the struct table and its comment**

Replace the comment block and the two `static Btn` definitions (currently lines 35–50) with:

```c++
// One struct per button again, and the latch belongs to STOP alone. When RUN was
// a single toggle the latch had to fire for it and the origin travelled with the
// edge (edgeFromIsr) so the sketch could drop a latched press that would start.
// Separate pins make that unnecessary: a START edge is always a start, so it is
// simply never latched.
// [TH] กลับมาเป็นโครงสร้างละปุ่ม และกลไกจับค้างเป็นของปุ่ม STOP เท่านั้น
//      สมัยที่ RUN เป็นปุ่มสลับปุ่มเดียว กลไกนี้ต้องทำงานให้มันด้วย
//      และที่มาของขอบต้องติดไปกับตัวขอบ (edgeFromIsr) เพื่อให้สเก็ตช์ทิ้งการกดที่จับค้างไว้ซึ่งจะกลายเป็นการเริ่ม
//      พอแยกขากันแล้วไม่ต้องทำแบบนั้น ขอบของปุ่ม START คือการเริ่มเสมอ จึงไม่ให้จับค้างเลย
//                pin, nc, stable, lastRaw, changed, edge, edgeFromIsr, isrPressed, isrDown,
//                isrDownMs, isrMayEdge, bootPressed
static Btn bStart{PIN_BTN_START, BTN_START_NC, false, false, 0, false, false, false, false, 0,
                  false, false};
static Btn bStop{PIN_BTN_STOP, BTN_STOP_NC, false, false, 0, false, false, false, false, 0,
                 true, false};
```

- [ ] **Step 3: Edit `buttons.cpp` — the ISR comment**

In `btnIsr`'s comment block (currently lines 63–81), replace the sentence about both buttons being NC. The mechanism is unchanged; only the reason changes:

```c++
// Runs on a pin change, and TIMES the press rather than just noticing one.
//
// Latching on the level alone would be a debounce bypass. Both buttons are NO
// with a pullup, so "pressed" is the line being pulled to GND — a loose lug that
// touches the frame for microseconds reads as a press. Only STOP is latched, so
// the worst such a glitch can do is command a safe-lower, but a spurious stop
// mid-session is still a wasted session. So the interrupt measures the gap
// between the make and the break and hands the loop only a press that lasted the
// full debounce window — the same bar the polling applies, measured by hardware
// timestamps instead of by sampling.
// [TH] ทำงานเมื่อระดับขาเปลี่ยน และจับเวลาความยาวของการกด ไม่ใช่แค่รู้ว่ามีการกด
//      ถ้าตัดสินจากระดับสัญญาณอย่างเดียวจะเป็นการข้ามกลไกกันเด้ง
//      ปุ่มทั้งสองเป็นแบบ NO และมีตัวต้านทานดึงขึ้น สถานะ "กด" คือขาถูกดึงลง GND
//      ขั้วที่หลวมและแตะโครงเครื่องเพียงไมโครวินาทีจะถูกอ่านว่าเป็นการกด
//      มีแค่ปุ่ม STOP ที่ถูกจับค้าง สิ่งที่แย่ที่สุดที่สัญญาณรบกวนทำได้จึงเป็นการสั่งลดขาเพื่อหยุด
//      แต่การหยุดที่ไม่ได้ตั้งใจกลางรอบก็ยังทำให้เสียรอบนั้นไป
//      อินเทอร์รัปต์จึงวัดช่วงเวลาระหว่างจังหวะกดกับจังหวะปล่อย
//      และส่งให้ลูปหลักเฉพาะการกดที่ยาวครบเกณฑ์กันเด้ง
//      ซึ่งเป็นเกณฑ์เดียวกับการอ่านตามรอบ ต่างกันแค่วัดด้วยเวลาจากฮาร์ดแวร์แทนการสุ่มอ่าน
```

- [ ] **Step 4: Edit `buttons.cpp` — the latch-consumption comment inside `poll()`**

Replace the long comment block that currently begins `// RESET is excluded from the latch (isrMayEdge), and the asymmetry is the point.` and ends `// preserved is identical — a latched press can only ever stop or lower.` with:

```c++
  // START is excluded from the latch (isrMayEdge), and the asymmetry is the point.
  // The failure the latch exists for is a stop press lost inside a blocked loop
  // iteration; a lost start just means pressing the button again, while a FALSE
  // start lifts a patient's leg with nobody at the panel. The interrupt is not
  // IRAM-registered on this core, so it is masked while the flash cache is off and
  // one pending edge is delivered late, timed against the level at delivery —
  // which can inflate a glitch into a full-width "press". A spurious stop costs a
  // safe-lower; that is the trade, and it is taken on STOP alone.
  // [TH] ปุ่ม START ถูกกันออกจากกลไกจับค้าง (isrMayEdge) และความไม่สมมาตรนี้คือประเด็นสำคัญ
  //      สิ่งที่กลไกนี้มีไว้แก้คือการกดเพื่อหยุดที่หายไปในลูปรอบที่ถูกบล็อก
  //      การกดเพื่อเริ่มที่หายไปแค่กดใหม่ก็จบ แต่การเริ่มทำงานผิดพลาดคือการยกขาผู้ป่วยโดยไม่มีใครอยู่ที่หน้าปัด
  //      อินเทอร์รัปต์บนคอร์นี้ไม่ได้ลงทะเบียนใน IRAM จึงถูกปิดกั้นระหว่างที่แคชแฟลชถูกปิด
  //      ขอบที่ค้างอยู่หนึ่งครั้งจะถูกส่งช้าและจับเวลาเทียบกับระดับสัญญาณ ณ เวลาที่ส่ง
  //      ซึ่งอาจทำให้สัญญาณรบกวนกลายเป็นการกดเต็มเกณฑ์
  //      การหยุดที่เกินมาแลกด้วยการลดขาลงหนึ่งครั้ง นั่นคือสิ่งที่ยอมแลก และยอมแลกเฉพาะกับปุ่ม STOP
```

- [ ] **Step 5: Edit `buttons.cpp` — the seed comment, `buttonsBegin`, the stuck guard and the accessors**

Replace the `seed()` doc comment (currently lines 186–218) with:

```c++
// Seeded from the pins, not from `false`. The structs used to start as
// not-pressed, so a line already reading pressed at power-up looked like a
// transition and raised a press edge about one debounce later — on START that is
// a session beginning roughly 25 ms after power-up with nobody touching the panel.
// Taking the boot state as the baseline means a press has to be a release-then-
// press seen after startup, which a shorted line can never produce.
// The flip side: a line that is already pressed at power-up seeds as pressed and
// can never raise an edge again, because an edge needs not-pressed -> pressed.
// Which buttons came up that way is recorded, and the sketch refuses to start a
// session — including one commanded over the link — while START is one of them.
// Releasing a genuinely held button clears it (see poll); a short never does.
// [TH] ตั้งค่าเริ่มต้นจากค่าที่อ่านได้จริงจากขา ไม่ใช่กำหนดเป็น "ไม่ได้กด"
//      เดิมโครงสร้างเริ่มที่ไม่ได้กด ถ้าตอนเปิดเครื่องขาใดอ่านได้ว่ากดอยู่ จะถูกมองว่าเพิ่งเปลี่ยนสถานะ
//      แล้วเกิดขอบกดขึ้นหลังพ้นเวลากันเด้ง ซึ่งบนปุ่ม START คือรอบที่เริ่มเองราว 25 มิลลิวินาทีหลังเปิดเครื่อง
//      โดยไม่มีใครแตะหน้าปัด
//      การยึดสถานะตอนเปิดเครื่องเป็นค่าตั้งต้นทำให้ต้องมีการปล่อยแล้วกดใหม่เท่านั้นจึงนับเป็นการกด
//      ซึ่งสายที่ลัดวงจรอยู่ทำไม่ได้
//      อีกด้านหนึ่งคือขาที่อ่านได้ว่ากดตั้งแต่เปิดเครื่องจะไม่เกิดขอบสัญญาณอีกเลย
//      เพราะขอบต้องเปลี่ยนจากไม่กดเป็นกด จึงบันทึกไว้ว่าปุ่มใดเป็นแบบนั้น
//      และไม่ยอมให้เริ่มรอบถ้า START เป็นหนึ่งในนั้น รวมถึงรอบที่สั่งมาทางลิงก์
//      ถ้าเป็นปุ่มที่ถูกกดค้างจริง พอปล่อยแล้วข้อสงสัยจะถูกล้าง (ดู poll) แต่สายที่ลัดจะไม่มีวันล้าง
```

Then replace `buttonsBegin()` and everything below it (currently lines 229–269) with:

```c++
void buttonsBegin() {
  pinMode(PIN_BTN_START, INPUT_PULLUP);
  pinMode(PIN_BTN_STOP, INPUT_PULLUP);
  delay(5);  // let the pullups pull the lines up before sampling — รอให้ตัวต้านทานดึงระดับขาขึ้นก่อนอ่าน
  uint32_t now = millis();
  seed(bStart, now);
  seed(bStop, now);
  // Attached after seeding, so the baseline is taken from a quiet pin.
  // [TH] ผูกอินเทอร์รัปต์หลังตั้งค่าตั้งต้นแล้ว ค่าตั้งต้นจะได้มาจากขาที่ยังไม่มีอะไรมารบกวน
  attachInterruptArg(digitalPinToInterrupt(PIN_BTN_START), btnIsr, &bStart, CHANGE);
  attachInterruptArg(digitalPinToInterrupt(PIN_BTN_STOP), btnIsr, &bStop, CHANGE);
}

// Two ways a START line reads pressed with nobody pressing it, and the boot check
// only catches one. A short that appears AFTER power-up is the likelier failure on
// a machine that moves: the line goes to GND, debounces to pressed, fires one
// press — which starts a session — and then reads pressed forever, never raising
// another edge and clearing bootPressed on the way past. So a line that has sat
// pressed far longer than any human press is treated the same as one that came up
// that way.
// [TH] มีสองทางที่สาย START จะอ่านได้ว่ากดโดยไม่มีใครกด แต่การตรวจตอนเปิดเครื่องดักได้ทางเดียว
//      สายที่ลัดวงจรหลังเปิดเครื่องมีโอกาสเกิดมากกว่าบนเครื่องที่มีการเคลื่อนไหว
//      ขาจะถูกดึงลง GND ผ่านกันเด้งกลายเป็นสถานะกด เกิดการกดหนึ่งครั้งซึ่งเริ่มรอบ
//      แล้วอ่านได้ว่ากดค้างตลอดไปโดยไม่เกิดขอบอีก และล้างค่า bootPressed ไปด้วยระหว่างทาง
//      สายที่อยู่ในสถานะกดนานเกินกว่าที่คนจะกดค้างได้ จึงถูกปฏิบัติเหมือนสายที่ผิดมาตั้งแต่เปิดเครื่อง
static const uint32_t STUCK_PRESSED_MS = 30000;

bool buttonStartStuck() {
  if (bStart.bootPressed) return true;
  return bStart.stable && (millis() - bStart.changed) > STUCK_PRESSED_MS;
}

void buttonsPoll(uint32_t nowMs) {
  poll(bStart, nowMs);
  poll(bStop, nowMs);
}

bool buttonStartPressed() { return takeEdge(bStart, nullptr); }
bool buttonStopPressed() { return takeEdge(bStop, nullptr); }
```

- [ ] **Step 6: Commit**

```bash
git add firmware/pt-leg-controller/buttons.h firmware/pt-leg-controller/buttons.cpp
git commit -m "feat(firmware): split the panel toggle into START and STOP"
```

---

## Task 4: Controller lamps — green and yellow

**Files:**
- Create: `firmware/pt-leg-controller/panel_lamps.h`, `firmware/pt-leg-controller/panel_lamps.cpp`
- Delete: `firmware/pt-leg-controller/status_led.h`, `firmware/pt-leg-controller/status_led.cpp`

**Interfaces:**
- Consumes: `PIN_LAMP_GREEN`, `PIN_LAMP_YELLOW`, `LED_ACTIVE_LOW` (Task 1).
- Produces: `panelLampsBegin()`, `panelLampsSet(bool inProgress)`. **Removed:** `statusLedBegin()`, `statusLedSet()`.

- [ ] **Step 1: Create `panel_lamps.h`**

```c++
#pragma once

// ไฟหน้าปัดสองดวง 12V ขับผ่านรีเลย์ ติดครั้งละดวง: เขียว=กำลังทำงาน เหลือง=ว่าง
// The two 12 V panel lamps, each behind a mechanical relay. Exactly one is lit at
// any moment:
//   green   a session is in progress and the actuator may move — keep clear
//   yellow  idle, or a latched FAULT; nothing is going to move on its own
//
// No blinking and no heartbeat. Every change of state is a contact operation and
// an audible click, so a 5 Hz blink is 18,000 operations an hour and would wear a
// ~100k-operation relay out in a few hours of one latched fault. Solid states cost
// four operations per session across the pair, so the relays outlive the machine.
// [TH] ไม่กะพริบและไม่วาบเป็นจังหวะ ทุกการเปลี่ยนสถานะคือการตัดต่อหน้าสัมผัสหนึ่งครั้งและมีเสียงคลิก
//      การกะพริบ 5 ครั้งต่อวินาทีคือ 18,000 ครั้งต่อชั่วโมง
//      จะกินอายุรีเลย์ที่ทนได้ราวแสนครั้งจนหมดภายในไม่กี่ชั่วโมงจากความผิดพลาดที่ค้างอยู่ครั้งเดียว
//      การใช้สถานะติดค้างหรือดับสนิทเสียแค่สี่ครั้งต่อรอบสำหรับหลอดทั้งคู่ รีเลย์จึงอยู่ได้นานกว่าตัวเครื่อง
//
// A latched FAULT reads yellow: it is not running, and the relays really are off.
// The pair answers "is it about to move" and nothing else — a fault is diagnosed
// on the CYD, the serial log and the card row.
// [TH] สถานะผิดพลาดที่ค้างอยู่จะแสดงเป็นสีเหลือง เพราะไม่ได้กำลังทำงานและรีเลย์ดับจริงๆ
//      หลอดคู่นี้ตอบแค่คำถามเดียวว่า "กำลังจะเคลื่อนที่ไหม"
//      การวินิจฉัยความผิดพลาดให้ดูที่จอ CYD บันทึกทางพอร์ตอนุกรม และแถวข้อมูลบนการ์ด

void panelLampsBegin();  // both pins driven dark before anything else can — ตั้งให้ดับทั้งคู่ก่อน

// Call every loop with SessionSM::running(). Writes each pin only when its own
// state changes, so the relays click twice a session each and not once a loop.
// [TH] เรียกทุกลูปด้วยค่าจาก SessionSM::running()
//      เขียนแต่ละขาเฉพาะตอนที่สถานะของขานั้นเปลี่ยนจริงๆ รีเลย์จึงคลิกดวงละสองครั้งต่อรอบ ไม่ใช่ทุกลูป
void panelLampsSet(bool inProgress);
```

- [ ] **Step 2: Create `panel_lamps.cpp`**

```c++
// ขับไฟหน้าปัดสองดวงผ่านรีเลย์ เขียนขาเฉพาะตอนสถานะเปลี่ยน
// Drives the two panel lamps through their relays. The only cleverness here is
// that a pin is written on change and never per loop: behind a mechanical relay a
// redundant write is a contact operation and a click, not a no-op.
#include "panel_lamps.h"
#include <Arduino.h>
#include "config.h"

static int gLastGreen = -1, gLastYellow = -1;  // -1 = ยังไม่เคยเขียนขา

static void writeLamp(uint8_t pin, int& last, bool on) {
#ifdef LED_ACTIVE_LOW
  int level = on ? LOW : HIGH;
#else
  int level = on ? HIGH : LOW;
#endif
  if (level == last) return;
  last = level;
  digitalWrite(pin, level);
}

static void beginLamp(uint8_t pin) {
  // Level before mode, deliberately the opposite order to actuatorBegin(). A pin
  // comes out of reset high-Z; pinMode(OUTPUT) makes it drive whatever the output
  // register already holds, which is LOW. On an active-low relay board LOW is
  // energized, so mode-then-write flicks the lamp on for the microseconds until
  // the next line — a click nobody asked for, at every boot.
  // [TH] ตั้งระดับก่อนตั้งโหมด ซึ่งเป็นลำดับตรงข้ามกับ actuatorBegin() โดยตั้งใจ
  //      ขา GPIO ออกจากรีเซ็ตมาเป็นอินพุตอิมพีแดนซ์สูง
  //      คำสั่ง pinMode(OUTPUT) จะทำให้ขาเริ่มขับค่าที่ค้างอยู่ในรีจิสเตอร์ ซึ่งคือ LOW
  //      บนบอร์ดรีเลย์แบบสั่งงานด้วยลอจิกต่ำ ค่า LOW คือสถานะที่รีเลย์ทำงาน
  //      การตั้งโหมดก่อนจึงทำให้หลอดวาบขึ้นชั่วไมโครวินาที เป็นเสียงคลิกที่ไม่มีใครสั่งทุกครั้งที่เปิดเครื่อง
#ifdef LED_ACTIVE_LOW
  digitalWrite(pin, HIGH);
#else
  digitalWrite(pin, LOW);
#endif
  pinMode(pin, OUTPUT);
}

void panelLampsBegin() {
  beginLamp(PIN_LAMP_GREEN);
  beginLamp(PIN_LAMP_YELLOW);
  gLastGreen = gLastYellow = -1;  // force the first set to write — ให้การเรียกครั้งแรกเขียนขาเสมอ
  // Idle is the boot state, so yellow comes on and stays on. That is not a lamp
  // test and must not become one: the machine is specified to show exactly one
  // lamp at all times, and a green flash at power-up would read as the machine
  // starting by itself, which is the one impression this panel must never give.
  // [TH] สถานะตอนเปิดเครื่องคือว่าง ไฟเหลืองจึงติดและติดค้างไว้
  //      นี่ไม่ใช่การทดสอบหลอดและต้องไม่กลายเป็นการทดสอบหลอด
  //      ข้อกำหนดคือให้มีไฟติดดวงเดียวตลอดเวลา
  //      การวาบไฟเขียวตอนเปิดเครื่องจะถูกอ่านว่าเครื่องเริ่มทำงานเอง
  //      ซึ่งเป็นความรู้สึกเดียวที่หน้าปัดนี้ต้องไม่สร้างขึ้นเลย
  panelLampsSet(false);
}

void panelLampsSet(bool inProgress) {
  writeLamp(PIN_LAMP_GREEN, gLastGreen, inProgress);
  writeLamp(PIN_LAMP_YELLOW, gLastYellow, !inProgress);
}
```

- [ ] **Step 3: Delete the old module**

```bash
git rm firmware/pt-leg-controller/status_led.h firmware/pt-leg-controller/status_led.cpp
```

- [ ] **Step 4: Commit**

```bash
git add firmware/pt-leg-controller/panel_lamps.h firmware/pt-leg-controller/panel_lamps.cpp firmware/pt-leg-controller/status_led.h firmware/pt-leg-controller/status_led.cpp
git commit -m "feat(firmware): replace the status lamp with a green/yellow pair"
```

---

## Task 5: Controller settings, link and sketch — wire it together and compile

**Files:**
- Modify: `firmware/pt-leg-controller/settings.h`, `firmware/pt-leg-controller/settings.cpp`, `firmware/pt-leg-controller/link.h`, `firmware/pt-leg-controller/link.cpp`, `firmware/pt-leg-controller/net.h`, `firmware/pt-leg-controller/net.cpp`, `firmware/pt-leg-controller/pt-leg-controller.ino`

**Interfaces:**
- Consumes: everything from Tasks 1–4.
- Produces: `DeviceSettings{..., maxTravelSec, liftSec, lowerSec, holdSec, restSec, targetReps}`; `LinkSettings{..., maxTravel, lift, lower, hold, rest, target}`; `LinkState{..., target}`; `Link::onTarget(uint16_t)` callback and `Link::sendTarget(uint16_t)`; `NetTimings`; `netPollCommand(int16_t* targetOut, NetTimings* timingsOut)`; `netHeartbeat(state, elapsedSec, reps, target)`.

- [ ] **Step 1: Add the three fields to `settings.h`**

```c++
  uint16_t maxTravelSec, liftSec, lowerSec, holdSec, restSec;  // เพดานต่อจังหวะ / ยก / ลง / ค้าง / พัก (วินาที)
  uint16_t targetReps;  // stop after this many reps; 0 = until STOP — 0 = ไม่จำกัด
```

- [ ] **Step 2: Persist them in `settings.cpp`**

Follow the file's existing `prefs.getUShort` / `prefs.putUShort` pattern exactly, adding:

```c++
  s.liftSec = prefs.getUShort("lift", DEFAULT_LIFT_SEC);
  s.lowerSec = prefs.getUShort("lower", DEFAULT_LOWER_SEC);
  s.targetReps = prefs.getUShort("target", DEFAULT_TARGET_REPS);
```

and the matching puts in `settingsSave`. NVS keys are limited to 15 characters; `lift`, `lower` and `target` are safe.

- [ ] **Step 3: Extend `LinkSettings` and `LinkState` in `link.h`**

```c++
struct LinkState {  // สถานะที่คอนโทรลเลอร์ส่งให้จอแสดงผล
  bool running;
  int phase;
  uint32_t elapsed;
  uint16_t reps;
  uint16_t target;  // 0 = ไม่จำกัด — the display shows reps/target when non-zero
  bool wifi;
  bool server;
};

struct LinkSettings {  // ค่าตั้งค่าที่รับส่งกันสองทางระหว่างจอกับคอนโทรลเลอร์
  String ssid, pass, server, code;
  uint16_t maxTravel, lift, lower, hold, rest, target;
};
```

Add to the `Link` class:

```c++
  void sendTarget(uint16_t reps);
  // display -> controller, applied live. Kept off the `settings` path on purpose:
  // that one commits NVS and restarts Wi-Fi, so it is deferred until a session
  // ends, and a rep target the operator is turning right now cannot wait for that.
  // [TH] จอส่งให้คอนโทรลเลอร์ นำไปใช้ทันที จงใจไม่ใช้เส้นทาง settings
  //      เพราะเส้นทางนั้นบันทึก NVS และรีสตาร์ต WiFi จึงถูกเลื่อนไปจนกว่ารอบจะจบ
  //      แต่เป้าหมายจำนวนครั้งที่ผู้ใช้กำลังหมุนอยู่รอแบบนั้นไม่ได้
  void (*onTarget)(uint16_t) = nullptr;  // controller side
```

- [ ] **Step 4: Handle the new fields and message in `link.cpp`**

In `sendSettings`, add `d["lift"] = s.lift; d["lower"] = s.lower; d["target"] = s.target;` alongside the existing `maxtravel`/`hold`/`rest`. In `sendState`, add `d["target"] = st.target;`. In `dispatch`, where `settings` is parsed, read the three new keys with the same `| (uint16_t)0` defaulting the file already uses, and add a new branch:

```c++
  if (t == "target") {
    if (onTarget) onTarget((uint16_t)(d["reps"] | 0));
    return;
  }
```

Add `void Link::sendTarget(uint16_t reps)` mirroring `sendPing`'s shape, emitting `{"t":"target","reps":n}`.

- [ ] **Step 5: Carry the target over HTTP in `net.h` / `net.cpp`**

`net.h`:

```c++
// Returns "start" | "stop" | "". `targetOut`, when given, receives the target the
// server wants this device to use, or -1 when the server named none. The two ride
// the same poll because the device already makes it once a second; a second
// endpoint would double the traffic to carry one integer.
// [TH] คืนค่า "start" | "stop" | "" ถ้าส่ง targetOut มาด้วยจะได้เป้าหมายจำนวนครั้งที่เซิร์ฟเวอร์ต้องการ
//      หรือ -1 ถ้าเซิร์ฟเวอร์ไม่ได้ระบุ ทั้งสองอย่างมาด้วยกันเพราะอุปกรณ์ถามอยู่แล้ววินาทีละครั้ง
//      ถ้าแยกเอนด์พอยต์จะเพิ่มปริมาณการสื่อสารเป็นสองเท่าเพื่อขนเลขตัวเดียว
// Cycle timings the server wants applied, or all-zero when it named none. Same
// ride-along reasoning as the target. Any field the server omitted arrives 0 and
// means "leave this one alone" — 0 is outside every clamp, so it can never be a
// real value.
// [TH] ค่าเวลาของรอบที่เซิร์ฟเวอร์ต้องการให้ใช้ หรือเป็นศูนย์ทั้งหมดถ้าไม่ได้ระบุ
//      เหตุผลเดียวกับเป้าหมายจำนวนครั้ง คือขนมาพร้อมกัน
//      ฟิลด์ใดที่เซิร์ฟเวอร์ไม่ได้ส่งมาจะได้ค่า 0 ซึ่งแปลว่าไม่ต้องแตะค่านั้น
//      เพราะ 0 อยู่นอกช่วงที่ยอมรับทุกค่า จึงเป็นค่าจริงไม่ได้
struct NetTimings {
  uint16_t liftSec, lowerSec, holdSec, restSec, maxTravelSec;
};
String netPollCommand(int16_t* targetOut = nullptr, NetTimings* timingsOut = nullptr);
void netHeartbeat(const char* state, uint32_t elapsedSec, uint16_t reps, uint16_t target);
```

`net.cpp`: add `filter["targetReps"] = true; filter["timings"] = true;` next to `filter["command"] = true;`, and after parsing set

```c++
  if (targetOut)
    *targetOut = doc["targetReps"].isNull() ? -1 : (int16_t)doc["targetReps"];
  if (timingsOut) {
    JsonVariantConst t = doc["timings"];
    timingsOut->liftSec = t["liftSec"] | (uint16_t)0;
    timingsOut->lowerSec = t["lowerSec"] | (uint16_t)0;
    timingsOut->holdSec = t["holdSec"] | (uint16_t)0;
    timingsOut->restSec = t["restSec"] | (uint16_t)0;
    timingsOut->maxTravelSec = t["maxTravelSec"] | (uint16_t)0;
  }
```

with `-1` and all-zero written on every early-return path too. Add `d["target"] = target;` to the heartbeat body.

- [ ] **Step 6: Update the sketch, `pt-leg-controller.ino`**

Apply all of the following:

1. `#include "status_led.h"` → `#include "panel_lamps.h"`; delete `#include "switches.h"`.
2. `applySettings` clamps — add, next to the existing three:
```c++
  d.liftSec = clampSec(s.lift, 1, d.maxTravelSec, "liftSec");
  d.lowerSec = clampSec(s.lower, 1, d.maxTravelSec, "lowerSec");
  d.targetReps = clampSec(s.target, 0, 99, "targetReps");
```
   `d.maxTravelSec` must be assigned before these two lines so the ceiling is the incoming value.
3. `currentAsLink()` — add `s.lift = gSettings.liftSec; s.lower = gSettings.lowerSec; s.target = gSettings.targetReps;`.
4. The held-settings block (`held.maxTravel = ...`) — add the three new fields.
5. New callback, registered in `setup()` next to `gLink.onSettings`:
```c++
// A live target change from the CYD. Applied at once, including mid-session, and
// remembered so the next NVS write carries it — the settings path is deferred
// until the session ends and this cannot wait for that.
// [TH] การเปลี่ยนเป้าหมายจากจอแบบสด นำไปใช้ทันทีรวมถึงตอนกลางรอบ
//      และจำไว้เพื่อให้การบันทึก NVS ครั้งถัดไปเก็บค่านี้ไปด้วย
//      เพราะเส้นทาง settings ถูกเลื่อนไปจนกว่ารอบจะจบ ซึ่งค่านี้รอไม่ได้
static void onLinkTarget(uint16_t reps) {
  if (reps > 99) reps = 99;
  gSettings.targetReps = reps;
  gSession.setTarget(reps);
  gTargetDirty = true;
}
```
   with `static bool gTargetDirty = false;` beside the other file-scope state, and in the place where the deferred settings are committed at session end, add: if `gTargetDirty` and no settings were held, call `settingsSave(gSettings)` and clear the flag.
6. `setup()` — delete `switchesBegin()`, the 60 ms `switchesPoll` loop, `seedLegPosition` and the `leg at boot` print. `statusLedBegin()` → `panelLampsBegin()`. Config assignment becomes:
```c++
  gSession.cfg = {gSettings.liftSec * 1000u,  gSettings.lowerSec * 1000u,
                  gSettings.holdSec * 1000u,  gSettings.restSec * 1000u,
                  gSettings.maxTravelSec * 1000u, gSettings.targetReps};
```
   (the same assignment appears twice — in `applySettings` and in `setup()`; update both), and the boot banner becomes:
```c++
  Serial.printf("pt-leg-controller %s ssid=%s server=%s code=%s cycle lift/hold/lower/rest=%u/%u/%u/%u target=%u\n",
                FW_VERSION, gSettings.wifiSsid.c_str(), gSettings.serverUrl.c_str(),
                gSettings.deviceCode.c_str(), gSettings.liftSec, gSettings.holdSec,
                gSettings.lowerSec, gSettings.restSec, gSettings.targetReps);
```
7. `BENCH_DEBUG` — `debugTick`'s signature becomes `(uint32_t nowMs, bool startEdge, bool stopEdge)`; delete `runWouldStart`, `runFromIsr`, the limits print and the `lastTop`/`lastBot` statics; print `EDGE START` / `EDGE STOP`; read levels from `PIN_BTN_START`/`PIN_BTN_STOP` with `BTN_START_NC`/`BTN_STOP_NC`; the setup banner becomes:
```c++
  Serial.printf("debug prints ON - btn start=%d(%s) stop=%d(%s), lamps green=%d yellow=%d\n",
                PIN_BTN_START, BTN_START_NC ? "NC" : "NO",
                PIN_BTN_STOP, BTN_STOP_NC ? "NC" : "NO",
                PIN_LAMP_GREEN, PIN_LAMP_YELLOW);
```
8. `loop()` — delete `switchesPoll(now)`; `gSession.tick(now, switchTop(), switchBottom())` → `gSession.tick(now)`; replace the whole button block with:
```c++
  // panel buttons: START begins a session, STOP ends one — and when the machine is
  // already idle, STOP clears the rep count. Read both first: each accessor clears
  // its edge, so the debug build has to see the same values this loop acts on.
  // [TH] ปุ่มหน้าปัด: START เริ่มรอบ STOP จบรอบ และถ้าเครื่องว่างอยู่แล้ว STOP จะล้างจำนวนครั้ง
  //      อ่านทั้งสองก่อน เพราะการอ่านจะล้างขอบทิ้ง โหมดดีบักต้องเห็นค่าชุดเดียวกับที่ลูปใช้
  bool pressStart = buttonStartPressed();
  bool pressStop = buttonStopPressed();
  // A start is refused while the START line looks shorted: it would restart the
  // machine at the end of every session with nobody at the panel. A stop is never
  // refused by anything.
  // [TH] ไม่ให้เริ่มรอบถ้าสาย START ดูเหมือนลัดวงจร เพราะจะทำให้เครื่องเริ่มใหม่ทุกครั้งที่รอบจบ
  //      โดยไม่มีใครอยู่ที่หน้าปัด ส่วนการสั่งหยุดไม่เคยถูกปฏิเสธด้วยอะไรทั้งสิ้น
  if (pressStart && buttonStartStuck())
    Serial.println("start refused: START reads pressed, check its wiring");
  else if (pressStart) gSession.start(now);
  if (pressStop) gSession.requestStop(now);
```
9. `statusLedSet(gSession.running())` → `panelLampsSet(gSession.running())`.
10. The server command poll becomes:
```c++
    int16_t wantTarget = -1;
    NetTimings wantTimings{0, 0, 0, 0, 0};
    String cmd = netPollCommand(&wantTarget, &wantTimings);
    // Timings go through applySettings, which clamps them, persists them and — if
    // a session is running — holds the whole change until it ends. That deferral
    // is the point: the NVS commit and the Wi-Fi restart it performs must not
    // happen with a relay energized, and a stroke length that changed halfway
    // through a stroke would be worse than waiting.
    // [TH] ค่าเวลาของรอบส่งผ่าน applySettings ซึ่งจำกัดช่วง บันทึกค่า
    //      และถ้ากำลังมีรอบทำงานอยู่จะพักการเปลี่ยนแปลงทั้งชุดไว้จนกว่ารอบจะจบ
    //      การเลื่อนนั้นคือประเด็น เพราะการบันทึก NVS และการรีสตาร์ต WiFi ที่ตามมา
    //      ต้องไม่เกิดขึ้นตอนที่รีเลย์กำลังจ่ายไฟ
    //      และความยาวจังหวะที่เปลี่ยนกลางจังหวะก็แย่กว่าการรอ
    if (wantTimings.liftSec || wantTimings.lowerSec || wantTimings.holdSec ||
        wantTimings.restSec || wantTimings.maxTravelSec) {
      LinkSettings s = currentAsLink();
      if (wantTimings.maxTravelSec) s.maxTravel = wantTimings.maxTravelSec;
      if (wantTimings.liftSec) s.lift = wantTimings.liftSec;
      if (wantTimings.lowerSec) s.lower = wantTimings.lowerSec;
      if (wantTimings.holdSec) s.hold = wantTimings.holdSec;
      if (wantTimings.restSec) s.rest = wantTimings.restSec;
      onLinkSettings(s);
    }
    // Applied before the command, so a start that carries a target runs with it
    // rather than with the previous one.
    // [TH] นำไปใช้ก่อนคำสั่ง คำสั่งเริ่มที่แนบเป้าหมายมาจะได้ใช้ค่าใหม่ ไม่ใช่ค่าเดิม
    if (wantTarget >= 0 && (uint16_t)wantTarget != gSettings.targetReps) {
      gSettings.targetReps = (uint16_t)wantTarget;
      gSession.setTarget((uint16_t)wantTarget);
      gTargetDirty = true;
      gLink.sendSettings(currentAsLink());  // keep the CYD's spinbox honest
                                            // [TH] ให้ช่องปรับค่าบนจอตรงกับของจริง
    }
    if (cmd == "start" && buttonStartStuck())
      Serial.println("server start refused: START reads pressed, check its wiring");
    else if (cmd == "start") gSession.start(now);
    else if (cmd == "stop") gSession.requestStop(now);
```
11. The heartbeat call gains the target: `netHeartbeat(..., gSession.reps(), gSession.target())`.
12. Where `LinkState` is filled for `sendState`, add `st.target = gSession.target();`.
13. Anywhere `gSession.reset(now)` is still called, delete the call — `reset()` no longer exists.

- [ ] **Step 7: Compile**

Run from `firmware/`:

```bash
arduino-cli compile --fqbn esp32:esp32:esp32 pt-leg-controller
```

Expected: PASS with no errors. If `arduino-cli` is not installed, open `pt-leg-controller.ino` in the Arduino IDE and use Verify. Fix every error before continuing — this is the first compile since Task 1, so expect a handful of missed renames.

- [ ] **Step 8: Commit**

```bash
git add firmware/pt-leg-controller
git commit -m "feat(firmware): carry lift/lower timings and the rep target end to end"
```

---

## Task 6: CYD display — new settings fields and a live rep target

**Files:**
- Modify: `firmware/pt-leg-display/settings.h`, `firmware/pt-leg-display/settingsfile.cpp`, `firmware/pt-leg-display/store.cpp`, `firmware/pt-leg-display/ui.h`, `firmware/pt-leg-display/ui.cpp`, `firmware/pt-leg-display/pt-leg-display.ino`

**Interfaces:**
- Consumes: `LinkSettings`/`LinkState` with the new fields (Task 5).
- Produces: `uiSetTargetChanged(void (*cb)(uint16_t))`; `uiUpdateMain(bool running, Phase phase, uint32_t elapsedSec, uint16_t reps, uint16_t target, bool wifiUp, bool serverUp, bool linkUp)`.

- [ ] **Step 1: Add the fields to `settings.h` and `settingsfile.cpp`**

`DeviceSettings` on the display side must match the controller's exactly: add `liftSec`, `lowerSec`, `targetReps`. In `settingsfile.cpp` add the defaults (`s.liftSec = 4; s.lowerSec = 4; s.targetReps = 10;`), the parse lines

```c++
  out.liftSec = clampU16(d["liftSec"] | (uint16_t)4, 1, out.maxTravelSec, "liftSec");
  out.lowerSec = clampU16(d["lowerSec"] | (uint16_t)4, 1, out.maxTravelSec, "lowerSec");
  out.targetReps = clampU16(d["targetReps"] | (uint16_t)10, 0, 99, "targetReps");
```

(after `maxTravelSec` is parsed, so the ceiling is right), and the three matching `d["..."] = s....;` writer lines. Update the file's header comment, which currently explains `maxTravelSec` as the cap "without a limit switch closing" — it is now the ceiling for `liftSec`/`lowerSec`.

- [ ] **Step 2: Mirror the fields in `store.cpp` and `pt-leg-display.ino`**

Add the three fields everywhere `maxTravelSec`/`holdSec`/`restSec` are already copied — the two `ls.maxTravel = ...` sites and the `gLast.maxTravelSec = ...` site in the sketch, and the NVS get/put pairs in `store.cpp`.

- [ ] **Step 3: Add the three spinboxes to the settings screen**

In `ui.cpp`, extend the declaration at line 409 to `static lv_obj_t *spinMax, *spinLift, *spinLower, *spinHold, *spinRest, *btnSave, *btnReset;` and add the rows next to the existing three (currently lines 871–873), in cycle order so the screen reads like the machine runs:

```c++
  spinMax = makeSpinRow(col, "เวลาเดินทางสูงสุด (วิ)", 8, 1, 20);
  spinLift = makeSpinRow(col, "ยกขึ้น (วิ)", 4, 1, 20);
  spinHold = makeSpinRow(col, "ค้าง (วิ)", 10, 1, 60);
  spinLower = makeSpinRow(col, "ลดลง (วิ)", 4, 1, 20);
  spinRest = makeSpinRow(col, "พัก (วิ)", 10, 1, 60);
  spinTarget = makeSpinRow(col, "จำนวนครั้ง (0=ไม่จำกัด)", 10, 0, 99);
```

`spinLift`/`spinLower` take the widest range LVGL can offer (1–20); the controller clamps them to the live `maxTravelSec`, which is the only place that knows it. Add the matching `lv_spinbox_set_value` lines at the three sites that currently set the other three (lines 512–514, 655–657 reading them back into `DeviceSettings`, and 921–923).

- [ ] **Step 4: Add the main-screen target stepper**

In `buildMain()`, add a row with `−` and `+` buttons and a label next to the existing reps label, using the same `makeBtn` helper the screen already uses. Both buttons call:

```c++
// The controller owns the target: this sends the change and does NOT update the
// label. The label follows the next state message, so what the screen shows is
// always what the machine will actually do — including a change somebody made in
// the phone app a moment ago.
// [TH] คอนโทรลเลอร์เป็นเจ้าของค่าเป้าหมาย ตรงนี้ส่งการเปลี่ยนแปลงออกไปและไม่แก้ป้ายเอง
//      ป้ายจะเปลี่ยนตามข้อความสถานะครั้งถัดไป สิ่งที่จอแสดงจึงเป็นสิ่งที่เครื่องจะทำจริงเสมอ
//      รวมถึงค่าที่มีคนเพิ่งเปลี่ยนจากแอปบนมือถือด้วย
static void targetStep(int delta) {
  int next = (int)uiTargetShown + delta;
  if (next < 0) next = 0;
  if (next > 99) next = 99;
  if (cbTarget) cbTarget((uint16_t)next);
}
```

with `static uint16_t uiTargetShown = 0;` updated from `uiUpdateMain`, and `cbTarget` set by the new `uiSetTargetChanged`. In `pt-leg-display.ino`, register a callback that calls `gLink.sendTarget(reps)`.

- [ ] **Step 5: Show `reps / target` on the main screen**

`uiUpdateMain` gains a `uint16_t target` parameter (add it to `ui.h` too, and to the `pPrev` change-detection block at line 325 so a target change repaints). The reps label becomes:

```c++
  if (target == 0) lv_label_set_text_fmt(lblReps, "%u รอบ", reps);
  else lv_label_set_text_fmt(lblReps, "%u / %u รอบ", reps, target);
```

Update the one call site in `pt-leg-display.ino` to pass `st.target`.

- [ ] **Step 6: Compile**

```bash
arduino-cli compile --fqbn esp32:esp32:esp32 pt-leg-display
```

Expected: PASS. If it fails with errors about `lv_disp_draw_buf_t` or `LV_IMG_CF_*`, lvgl has drifted off the pinned 8.4.0 — reinstall the pin, do not port to v9.

- [ ] **Step 7: Commit**

```bash
git add firmware/pt-leg-display
git commit -m "feat(firmware): edit lift/lower timings and the rep target on the CYD"
```

---

## Task 7: Protocol and firmware docs

**Files:**
- Modify: `firmware/link-protocol.md`, `firmware/README.md`

- [ ] **Step 1: Update `link-protocol.md`**

- Display → Controller `settings`: add `"lift":..,"lower":..,"target":..` to the message shape and note the clamps (`maxtravel` 1–20, `lift`/`lower` 1–`maxtravel`, `hold`/`rest` 1–60, `target` 0–99).
- Add the new message:
  > - `{"t":"target","reps":n}` — live rep-target change from the main screen. Applied immediately, including mid-session; the NVS write is deferred to the end of the session. Unlike `settings` it is never held, because that path commits NVS and restarts Wi-Fi.
- Controller → Display: `state` gains `"target":int`; `settings` gains `lift`, `lower`, `target`.
- Notes: replace the `maxtravel` bullet with — `maxtravel` is the per-stroke energize cap and the ceiling `lift`/`lower` are clamped to; travel itself is timed by `lift`/`lower`, the limit switches having been removed.
- Notes: replace the run-control bullet with — two physical NO panel buttons, START and STOP; STOP while idle clears the rep count and any latched FAULT. There is no RESET button.
- Notes: add — a session ends by itself when `reps` reaches `target`; `target` 0 means run until STOP.

- [ ] **Step 2: Update `firmware/README.md`**

Search it for `RUN`, `RESET`, `limit`, `switch`, `status LED` and `maxtravel`, and bring each mention in line: the panel is START (GPIO 25) + STOP (GPIO 26), both NO; the lamps are green (23) and yellow (32); GPIO 4 and 33 are free; travel is timed; the rep target exists and can be set on the CYD or in the app. The "Running the controller without a display" section must still be correct — the UART watchdog behaviour is unchanged.

- [ ] **Step 3: Commit**

```bash
git add firmware/link-protocol.md firmware/README.md
git commit -m "docs(firmware): document timed travel, START/STOP and the rep target"
```

---

## Task 8: Server — desired target, device handoff, and acknowledgement

**Files:**
- Modify: `server/app/state.py`, `server/app/service.py`, `server/app/routes_app.py`, `server/app/routes_device.py`
- Test: `server/tests/test_state.py`, `server/tests/test_service.py`, `server/tests/test_app_routes.py`, `server/tests/test_device_routes.py`

**Interfaces:**
- Consumes: nothing from earlier tasks (the wire contract is defined here and consumed by Tasks 5 and 9).
- Produces: `DeviceState.live_target: int`, `DeviceState.set_desired_target(int)`, `DeviceState.fetch_desired_target() -> int | None`, `DeviceState.set_desired_timings(dict)`, `DeviceState.fetch_desired_timings() -> dict | None`, `DeviceState.heartbeat(state, elapsed_sec, reps, target=0)`; `SessionService.set_target(reps, device_id)`, `fetch_target(device_id)`, `set_timings(timings, device_id)`, `fetch_timings(device_id)`, `queue_start(device_id, target_reps=None)`, `handle_heartbeat(state, elapsed_sec, reps, target=0, device_id=...)`; `POST /api/sessions/target`, `POST /api/sessions/timings`, `POST /api/sessions/start` body `{"targetReps": n}`, `GET /api/device/command` → `{"command":…, "targetReps": n|null, "timings": {...}|null}`, `POST /api/device/heartbeat` body field `target`.

- [ ] **Step 1: Write the failing state tests**

Add to `server/tests/test_state.py`:

```python
def test_desired_target_is_none_until_set():
    dev = DeviceState()
    assert dev.fetch_desired_target() is None


def test_desired_target_is_handed_out_until_the_device_echoes_it():
    dev = DeviceState()
    dev.set_desired_target(7)
    # handed out on every poll: one lost response must not lose the target
    # [TH] ส่งให้ทุกครั้งที่ถาม เพราะถ้าคำตอบหายไปครั้งเดียวต้องไม่ทำให้เป้าหมายหายตาม
    assert dev.fetch_desired_target() == 7
    assert dev.fetch_desired_target() == 7
    dev.heartbeat("idle", 0, 0, target=7)
    assert dev.fetch_desired_target() is None
    assert dev.live_target == 7


def test_a_heartbeat_with_a_different_target_does_not_clear_the_desire():
    dev = DeviceState()
    dev.set_desired_target(7)
    dev.heartbeat("idle", 0, 0, target=3)
    assert dev.fetch_desired_target() == 7
```

- [ ] **Step 2: Run them and watch them fail**

```bash
python -m pytest tests/test_state.py -v
```
Expected: FAIL with `AttributeError: 'DeviceState' object has no attribute 'fetch_desired_target'`.

- [ ] **Step 3: Implement in `state.py`**

```python
        self.live_target = 0
        self._desired_target: int | None = None

    def set_desired_target(self, reps: int) -> None:
        self._desired_target = reps

    def fetch_desired_target(self) -> int | None:
        """The target the device should adopt, or None when it already has it.

        Not consume-on-read, unlike commands: a command that is applied twice
        starts two sessions, while a target that is applied twice is idempotent.
        So it is repeated until the device's own heartbeat reports that value back,
        which survives a dropped response.

        [TH] ไม่ใช่อ่านแล้วล้างแบบคำสั่ง เพราะคำสั่งที่ถูกใช้ซ้ำจะเริ่มสองรอบ
             แต่เป้าหมายที่ถูกใช้ซ้ำให้ผลเหมือนเดิม
             จึงส่งซ้ำไปจนกว่าสัญญาณชีพของอุปกรณ์จะรายงานค่านั้นกลับมา ซึ่งทนต่อการที่คำตอบหายได้
        """
        return self._desired_target

    def heartbeat(self, state: str, elapsed_sec: int, reps: int, target: int = 0) -> None:
        self._last_heartbeat_at = self._now()
        self.live_state = state
        self.live_elapsed_sec = elapsed_sec
        self.live_reps = reps
        self.live_target = target
        if self._desired_target is not None and target == self._desired_target:
            self._desired_target = None
```

- [ ] **Step 4: Run the state tests**

```bash
python -m pytest tests/test_state.py -v
```
Expected: PASS.

- [ ] **Step 5: Write the failing service and route tests**

Add to `server/tests/test_service.py`:

```python
def test_queue_start_with_a_target_sets_the_desired_target(service):
    service.queue_start(target_reps=5)
    assert service.fetch_target() == 5
    assert service.fetch_command() == "start"


def test_set_target_works_while_a_session_runs(service):
    service.handle_event("started", 0, 0)
    service.set_target(12)
    assert service.fetch_target() == 12


def test_current_reports_the_live_target(service):
    service.handle_heartbeat("idle", 0, 0, target=8)
    assert service.current()["targetReps"] == 8
```

Add to `server/tests/test_app_routes.py`:

```python
def test_post_target_returns_202_and_queues_it(client):
    assert client.post("/api/sessions/target", json={"targetReps": 6}).status_code == 202
    assert client.get("/api/device/command").json()["targetReps"] == 6


def test_post_target_rejects_out_of_range(client):
    assert client.post("/api/sessions/target", json={"targetReps": 100}).status_code == 422
    assert client.post("/api/sessions/target", json={"targetReps": -1}).status_code == 422


def test_start_accepts_a_target_in_the_body(client):
    assert client.post("/api/sessions/start", json={"targetReps": 4}).status_code == 202
    assert client.get("/api/device/command").json()["targetReps"] == 4


def test_start_without_a_body_still_works(client):
    assert client.post("/api/sessions/start").status_code == 202
```

Add to `server/tests/test_device_routes.py`:

```python
def test_command_reports_no_target_when_none_is_wanted(client):
    assert client.get("/api/device/command").json()["targetReps"] is None


def test_heartbeat_accepts_and_clears_a_target(client):
    client.post("/api/sessions/target", json={"targetReps": 9})
    client.post("/api/device/heartbeat", json={"state": "idle", "target": 9})
    assert client.get("/api/device/command").json()["targetReps"] is None
    assert client.get("/api/sessions/current").json()["targetReps"] == 9
```

Match the fixture names already used in each file (`service`, `client`); do not invent new ones.

- [ ] **Step 6: Run them and watch them fail**

```bash
python -m pytest tests -v
```
Expected: FAIL — `TypeError: queue_start() got an unexpected keyword argument 'target_reps'` and 404s on `/api/sessions/target`.

- [ ] **Step 7: Implement in `service.py`**

```python
    def fetch_target(self, device_id: str = DEFAULT_DEVICE_ID) -> int | None:
        return self._device(device_id).fetch_desired_target()

    def set_target(self, reps: int, device_id: str = DEFAULT_DEVICE_ID) -> None:
        # Never a ConflictError: the target is editable while a session runs, and
        # the controller applies a lowered one at the end of the current rest.
        # [TH] ไม่เคยชนสถานะ เพราะแก้เป้าหมายได้ระหว่างที่รอบกำลังเดิน
        #      และคอนโทรลเลอร์จะใช้ค่าที่ปรับลดลงเมื่อสิ้นสุดการพักครั้งปัจจุบัน
        self._device(device_id).set_desired_target(reps)
```

`queue_start` gains `target_reps: int | None = None` and sets the desired target **before** `dev.queue_command("start")`, so a device that fetches the command on its very next poll already sees the target in the same response. `handle_heartbeat` gains `target: int = 0` and forwards it to `dev.heartbeat(...)`; `handle_event` keeps its own `dev.heartbeat(...)` calls, passing `dev.live_target` so an event does not blank the target. Both `current()` return paths gain `"targetReps": dev.live_target`.

- [ ] **Step 8: Implement in `routes_app.py`**

```python
class StartBody(BaseModel):
    targetReps: int | None = Field(default=None, ge=0, le=99)


class TargetBody(BaseModel):
    targetReps: int = Field(ge=0, le=99)


@router.post("/start", status_code=202)
def start(request: Request, deviceId: str = "default", body: StartBody | None = None):
    try:
        request.app.state.service.queue_start(
            deviceId, target_reps=None if body is None else body.targetReps
        )
    except ConflictError as exc:
        raise HTTPException(status_code=409, detail=str(exc))
    return {"queued": True}


@router.post("/target", status_code=202)
def set_target(body: TargetBody, request: Request, deviceId: str = "default"):
    request.app.state.service.set_target(body.targetReps, deviceId)
    return {"queued": True}
```

Import `Field` from pydantic. Keep `/target` declared **above** `/{session_id}/claim` so the literal path wins the route match.

- [ ] **Step 9: Implement in `routes_device.py`**

`HeartbeatBody` gains `target: int = 0`, `post_heartbeat` forwards it, and:

```python
@router.get("/command")
def get_command(request: Request, deviceId: str = "default"):
    service = request.app.state.service
    return {
        "command": service.fetch_command(deviceId),
        "targetReps": service.fetch_target(deviceId),
    }
```

- [ ] **Step 10: Write the failing timings tests**

The four cycle timings need the same app→device path as the target, because the
spec has `SettingsView` editing them. They differ in one way and it is worth
stating: the device echoes its target in every heartbeat, so a target can be
repeated until it is acknowledged, while nothing echoes the timings. They are
therefore handed out for a bounded window instead — the same `COMMAND_TTL_SEC`
the command queue uses — so a dropped response is retried for 30 s and then
dropped rather than repeated forever.

Add to `server/tests/test_state.py`:

```python
def test_desired_timings_are_handed_out_until_the_ttl_expires():
    clock = [1000.0]
    dev = DeviceState(now=lambda: clock[0])
    dev.set_desired_timings({"liftSec": 3, "lowerSec": 3, "holdSec": 8, "restSec": 8})
    assert dev.fetch_desired_timings()["liftSec"] == 3
    clock[0] += 10
    assert dev.fetch_desired_timings() is not None
    clock[0] += 30
    assert dev.fetch_desired_timings() is None
```

Add to `server/tests/test_app_routes.py`:

```python
def test_post_timings_returns_202_and_reaches_the_device(client):
    body = {"liftSec": 3, "lowerSec": 3, "holdSec": 8, "restSec": 8, "maxTravelSec": 9}
    assert client.post("/api/sessions/timings", json=body).status_code == 202
    assert client.get("/api/device/command").json()["timings"] == body


def test_post_timings_rejects_out_of_range(client):
    assert client.post(
        "/api/sessions/timings", json={"liftSec": 0}
    ).status_code == 422
    assert client.post(
        "/api/sessions/timings", json={"holdSec": 61}
    ).status_code == 422


def test_command_reports_no_timings_when_none_are_wanted(client):
    assert client.get("/api/device/command").json()["timings"] is None
```

- [ ] **Step 11: Run them and watch them fail**

```bash
python -m pytest tests -v
```
Expected: FAIL — `AttributeError: … 'set_desired_timings'` and a 404 on `/api/sessions/timings`.

- [ ] **Step 12: Implement the timings path**

`state.py`:

```python
        self._desired_timings: dict | None = None
        self._timings_queued_at = 0.0

    def set_desired_timings(self, timings: dict) -> None:
        self._desired_timings = timings
        self._timings_queued_at = self._now()

    def fetch_desired_timings(self) -> dict | None:
        """Cycle timings the device should adopt, until COMMAND_TTL_SEC passes.

        Unlike the target, nothing echoes these back — the heartbeat has no room
        for four more fields and there is nothing useful to do with a mismatch —
        so they expire on a clock instead of on an acknowledgement. Repeating
        them forever would re-apply a stale edit to a controller that had since
        been reconfigured at its own screen.

        [TH] ต่างจากเป้าหมายจำนวนครั้ง ตรงที่ไม่มีอะไรส่งค่าเหล่านี้กลับมายืนยัน
             สัญญาณชีพไม่มีที่ให้อีกสี่ฟิลด์ และถึงรู้ว่าไม่ตรงกันก็ทำอะไรไม่ได้
             จึงให้หมดอายุตามเวลาแทนการรอการยืนยัน
             ถ้าส่งซ้ำไปเรื่อยๆ ค่าที่แก้ไว้นานแล้วจะไปทับเครื่องที่ถูกตั้งค่าใหม่จากหน้าจอของตัวเองไปแล้ว
        """
        if (
            self._desired_timings is not None
            and self._now() - self._timings_queued_at > COMMAND_TTL_SEC
        ):
            self._desired_timings = None
        return self._desired_timings
```

`service.py`:

```python
    def set_timings(self, timings: dict, device_id: str = DEFAULT_DEVICE_ID) -> None:
        self._device(device_id).set_desired_timings(timings)

    def fetch_timings(self, device_id: str = DEFAULT_DEVICE_ID) -> dict | None:
        return self._device(device_id).fetch_desired_timings()
```

`routes_app.py` — declared above `/{session_id}/claim` like `/target`:

```python
class TimingsBody(BaseModel):
    liftSec: int | None = Field(default=None, ge=1, le=20)
    lowerSec: int | None = Field(default=None, ge=1, le=20)
    holdSec: int | None = Field(default=None, ge=1, le=60)
    restSec: int | None = Field(default=None, ge=1, le=60)
    maxTravelSec: int | None = Field(default=None, ge=1, le=20)


@router.post("/timings", status_code=202)
def set_timings(body: TimingsBody, request: Request, deviceId: str = "default"):
    # Only the fields that were sent. lift/lower are bounded to 20 here because
    # that is maxTravelSec's own ceiling; the controller clamps them again to the
    # maxTravelSec it actually holds, which is the only place that knows it.
    # [TH] ส่งเฉพาะฟิลด์ที่ถูกระบุมา ค่ายก/ลงจำกัดที่ 20 ตรงนี้เพราะเป็นเพดานของ maxTravelSec เอง
    #      คอนโทรลเลอร์จะจำกัดซ้ำด้วยค่า maxTravelSec ที่ถืออยู่จริง ซึ่งเป็นที่เดียวที่รู้ค่านั้น
    request.app.state.service.set_timings(
        body.model_dump(exclude_none=True), deviceId
    )
    return {"queued": True}
```

`routes_device.py` — `get_command` gains `"timings": service.fetch_timings(deviceId)`.

- [ ] **Step 13: Run the full suite**

```bash
python -m pytest tests -v
```
Expected: PASS, all 54 pre-existing tests plus the new ones.

- [ ] **Step 14: Commit**

```bash
git add server/app server/tests
git commit -m "feat(server): carry the rep target and cycle timings to the device"
```

---

## Task 9: App device client — `setTarget` and start-with-target

**Files:**
- Modify: `PT-Leg-Tracker/src/types.ts`, `PT-Leg-Tracker/src/lib/device.ts`
- Test: `PT-Leg-Tracker/src/lib/device.test.ts`

**Interfaces:**
- Consumes: the server contract from Task 8.
- Produces: `queueStart(serverUrl, deviceCode, targetReps?)`, `setTarget(serverUrl, deviceCode, reps)`, `setTimings(serverUrl, deviceCode, timings)`, `DeviceTimings`, `getCurrent(...) -> CurrentSession` with `targetReps: number`.

- [ ] **Step 1: Write the failing tests**

Add to `device.test.ts`:

```ts
describe('setTarget', () => {
  it('POSTs the target and resolves on 202', async () => {
    const fn = mockFetchOnce(jsonRes(202, { queued: true }))
    await expect(setTarget('http://srv/', 'D1', 6)).resolves.toBeUndefined()
    expect(fn).toHaveBeenCalledWith(
      'http://srv/api/sessions/target?deviceId=D1',
      expect.objectContaining({
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ targetReps: 6 }),
      }),
    )
  })

  it('throws no-device when the device code is blank', async () => {
    await expect(setTarget('http://srv', '  ', 6)).rejects.toMatchObject({ kind: 'no-device' })
  })
})

describe('queueStart with a target', () => {
  it('sends the target in the body', async () => {
    const fn = mockFetchOnce(jsonRes(202, { queued: true }))
    await queueStart('http://srv', 'D1', 4)
    expect(fn).toHaveBeenCalledWith(
      'http://srv/api/sessions/start?deviceId=D1',
      expect.objectContaining({ body: JSON.stringify({ targetReps: 4 }) }),
    )
  })

  it('sends no body when no target is given', async () => {
    const fn = mockFetchOnce(jsonRes(202, { queued: true }))
    await queueStart('http://srv', 'D1')
    expect(fn.mock.calls[0][1]).not.toHaveProperty('body')
  })
})

describe('getCurrent targetReps', () => {
  it('reads the target', async () => {
    mockFetchOnce(jsonRes(200, { state: 'idle', targetReps: 8 }))
    await expect(getCurrent('http://srv', 'D1')).resolves.toMatchObject({ targetReps: 8 })
  })

  it('defaults to 0 when the server omits it', async () => {
    mockFetchOnce(jsonRes(200, { state: 'idle' }))
    await expect(getCurrent('http://srv', 'D1')).resolves.toMatchObject({ targetReps: 0 })
  })
})
```

```ts
describe('setTimings', () => {
  it('POSTs only the fields it was given', async () => {
    const fn = mockFetchOnce(jsonRes(202, { queued: true }))
    await expect(setTimings('http://srv', 'D1', { liftSec: 3 })).resolves.toBeUndefined()
    expect(fn).toHaveBeenCalledWith(
      'http://srv/api/sessions/timings?deviceId=D1',
      expect.objectContaining({ body: JSON.stringify({ liftSec: 3 }) }),
    )
  })
})
```

Add `setTarget` and `setTimings` to the import at the top of the file.

- [ ] **Step 2: Run them and watch them fail**

```bash
npx vitest run src/lib/device.test.ts
```
Expected: FAIL with `setTarget is not a function`.

- [ ] **Step 3: Implement**

`types.ts` — add `targetReps: number` to `CurrentSession`.

`device.ts`:

```ts
// [TH] ส่งเป้าหมายจำนวนครั้งไปที่เซิร์ฟเวอร์ ; เครื่องจะรับไปเองตอนถามคำสั่งครั้งถัดไป
export async function setTarget(
  serverUrl: string,
  deviceCode: string,
  reps: number,
): Promise<void> {
  await control(serverUrl, deviceCode, '/api/sessions/target', 'unreachable', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ targetReps: reps }),
  })
}
```

and `queueStart` becomes:

```ts
export async function queueStart(
  serverUrl: string,
  deviceCode: string,
  targetReps?: number,
): Promise<void> {
  // The target rides the start request so a session begins with the number the
  // user just picked, rather than with whatever the device happened to hold.
  // [TH] ส่งเป้าหมายไปพร้อมคำสั่งเริ่ม รอบจะได้เริ่มด้วยจำนวนที่ผู้ใช้เพิ่งเลือก
  //      ไม่ใช่ค่าที่บังเอิญค้างอยู่ในเครื่อง
  const init: RequestInit =
    targetReps === undefined
      ? { method: 'POST' }
      : {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ targetReps }),
        }
  await control(serverUrl, deviceCode, '/api/sessions/start', 'busy', init)
}
```

and `getCurrent` gains `targetReps: typeof d.targetReps === 'number' ? d.targetReps : 0,`.

Also:

```ts
// [TH] ค่าเวลาของรอบที่แก้ได้จากแอป ส่งเฉพาะฟิลด์ที่ต้องการเปลี่ยน
export interface DeviceTimings {
  liftSec?: number
  lowerSec?: number
  holdSec?: number
  restSec?: number
  maxTravelSec?: number
}

export async function setTimings(
  serverUrl: string,
  deviceCode: string,
  timings: DeviceTimings,
): Promise<void> {
  await control(serverUrl, deviceCode, '/api/sessions/timings', 'unreachable', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(timings),
  })
}
```

- [ ] **Step 4: Run the tests**

```bash
npx vitest run src/lib/device.test.ts
```
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add PT-Leg-Tracker/src/types.ts PT-Leg-Tracker/src/lib/device.ts PT-Leg-Tracker/src/lib/device.test.ts
git commit -m "feat(app): set the device rep target from the client"
```

---

## Task 10: App screens — target stepper and device timings

**Files:**
- Modify: `PT-Leg-Tracker/src/views/SessionView.vue`, `PT-Leg-Tracker/src/views/SettingsView.vue`

**Interfaces:**
- Consumes: `setTarget`, `queueStart(…, targetReps)`, `CurrentSession.targetReps` (Task 9).

- [ ] **Step 1: Add the target stepper to `SessionView.vue`**

Add `const target = ref(0)` beside `reps`, set it from `poll()` (`target.value = cur.targetReps`), and pass it on start: `await queueStart(serverUrl.value, deviceCode.value, target.value)`.

The `−` / `+` handlers call the server and let the next poll confirm:

```ts
// [TH] ค่าเป้าหมายเป็นของเครื่อง ตรงนี้แค่ส่งคำขอเปลี่ยน แล้วรอค่าจริงจากการอ่านสถานะรอบถัดไป
//      หน้าจอจะได้ตรงกับเครื่องเสมอ รวมถึงตอนที่มีคนเปลี่ยนค่าที่หน้าจอ CYD
async function stepTarget(delta: number) {
  const next = Math.min(99, Math.max(0, target.value + delta))
  if (next === target.value) return
  try {
    await setTarget(serverUrl.value, deviceCode.value, next)
  } catch {
    // a failed change is simply not applied; the next poll re-reads the truth
    // [TH] ถ้าเปลี่ยนไม่สำเร็จก็ถือว่าไม่เปลี่ยน การอ่านสถานะรอบถัดไปจะดึงค่าจริงกลับมาเอง
  }
}
```

In the template, put the stepper next to the reps display and show progress as `{{ reps }} / {{ target }}` when `target > 0`, falling back to the current bare `{{ reps }}` when it is 0. Follow the styling of the existing `.reps` block and use the app's `BigButton` component for the ± controls if it fits; otherwise plain buttons styled like the ones already on the screen.

- [ ] **Step 2: Add the device timings group to `SettingsView.vue`**

Add a secondary section labelled in Thai (e.g. `ตั้งค่าเครื่อง (สำหรับทดสอบ)`) with number inputs for lift, lower, hold, rest and max-travel seconds, and a save button that calls `setTimings(serverUrl, deviceCode, {...})` with the fields the user actually changed. These are device settings: they go to the machine, not into `stores/settings.ts`.

Two things the section's help text must say, because both will otherwise read as bugs:

```
[TH] การเปลี่ยนค่าจะมีผลเมื่อรอบปัจจุบันจบแล้ว และต้องให้เครื่องออนไลน์ภายใน 30 วินาที
     ถ้าเครื่องออฟไลน์อยู่ ค่าที่ส่งจะหมดอายุและต้องกดบันทึกใหม่
EN:  A change takes effect once the current session ends, and only if the machine
     comes online within 30 seconds. Sent to an offline machine, it expires and
     has to be saved again.
```

The inputs show what the user last typed, not what the machine holds — unlike the rep target, nothing reports these back. Do not add a fake "current value" readout implying otherwise.

- [ ] **Step 3: Verify the build and the whole test suite**

```bash
npm run build
```
Expected: PASS (vue-tsc clean, vite build succeeds).

```bash
npx vitest run
```
Expected: PASS — all 46 pre-existing tests plus the new ones.

- [ ] **Step 4: Commit**

```bash
git add PT-Leg-Tracker/src/views
git commit -m "feat(app): pick the rep target on the session screen"
```

---

## Task 11: `pin-layout.html`

**Files:**
- Modify: `firmware/pin-layout.html`

Every change is bilingual: an `<span class="en">` and a `<span class="th">`, matching the file throughout.

- [ ] **Step 1: Update the pin tables**

- Delete the two `<tr data-pin="32">` / `<tr data-pin="33">` limit-switch rows and the `INPUT_PULLUP` + `SWITCH_NC` paragraph under them.
- Add a row for the yellow lamp at GPIO 32 and relabel the GPIO 23 row "Green lamp / ไฟเขียว".
- Replace the RUN (25) and RESET (4) rows with START (25) and STOP (26), both `INPUT_PULLUP`, and mark the interrupt on STOP only.
- Rewrite the per-button contact paragraph for `BTN_START_NC` / `BTN_STOP_NC`, both `0` (normally-open) as committed, and say that GPIO **4** and **33** are now free pins that nothing reads and nothing reserves.
- In the reference table near line 600, update the 23/25/32 rows, add 26, and delete 33.

- [ ] **Step 2: Update the board diagram's `USE` map**

```js
  var USE={ '22':['var(--relay)','UP'], '27':['var(--relay)','DOWN'],
            '25':['var(--btn)','START'], '26':['var(--btn)','STOP'],
            '23':['var(--btn)','GREEN'], '32':['var(--btn)','YELLOW'],
            '21':['var(--i2c)','SDA'], '19':['var(--i2c)','SCL'],
            '16':['var(--uart)','RX'], '17':['var(--uart)','TX'] };
```

The `--limit` CSS variable becomes unused; delete its declaration too.

- [ ] **Step 3: Retime the simulator**

- Constants: `var LIFT=4, LOWER=4, HOLD=10, REST=10;` replacing `TRAVEL`.
- `enter('lift')` uses `ms=LIFT/speed*1000` and moves the leg to 1 over that time; `enter('lower')`/`enter('safe')` use `LOWER`. The leg's travel is no longer proportional to `pos` — a timed stroke always runs its full duration, which is exactly the behaviour change being illustrated.
- Delete `limit()` and the `ledTop`/`ledBot`/`ps32`/`ps33` wiring; delete the two `.pinstate` blocks for GPIO 32/33 in the markup and add one for the yellow lamp.
- Move the rep increment from the end of `lower` to the end of `rest`, matching the firmware, and stop the session when `reps` reaches the target.
- `statusLamp()` becomes `lamps()`, toggling `ps23` on `phase!=='idle'` and the new yellow element on `phase==='idle'`.
- Phase notes in `TEXT`: `rest` no longer says "Bottom limit closed"; use "Both relays off, 10 s" / "รีเลย์ดับทั้งคู่ 10 วินาที". `lift`/`lower` gain their durations. Add nothing for a `fault` phase — the rig has never had one.

- [ ] **Step 4: Replace the rig's controls**

```html
      <button class="pbtn start" id="cStart">START</button>
      <button class="pbtn stop" id="cStop">STOP</button>
      <label class="tgt">× <input id="cTarget" type="number" min="0" max="99" value="3"></label>
      <button class="pbtn" id="cSpeed">×4</button>
```

`cStart` starts from idle only; `cStop` lowers when running and zeroes the rep count when idle. Delete `runLabel()` and `cReset` entirely. Rewrite the caption paragraph under the rig: two NO panel buttons on GPIO 25 and 26, a target that ends the run by itself, and travel that takes `liftSec`/`lowerSec` rather than ending at a switch.

- [ ] **Step 5: Check it in a browser**

Open `firmware/pin-layout.html`, then:
1. Set the target to 2, press START, and confirm the rig stops itself after 2 reps with the green pin-state going dark and the yellow lighting.
2. Press START then STOP mid-lift; the rig lowers and stops.
3. Press STOP while idle; the rep counter zeroes.
4. Toggle EN/ไทย and confirm no English leaks into the Thai view or vice versa.
5. Hover a pin row and confirm the diagram still cross-highlights, including the new 26 and 32.

- [ ] **Step 6: Commit**

```bash
git add firmware/pin-layout.html
git commit -m "docs(firmware): redraw the pin layout for timed travel and START/STOP"
```

---

## Task 12: Bench tools

**Files:**
- Modify: `firmware/tools/button-bench.ps1`, `firmware/tools/fake-controller.ps1`

- [ ] **Step 1: Update `button-bench.ps1`**

Replace RUN/RESET with START/STOP throughout: the prompts, the expected `EDGE …` lines it matches against the serial log, and the pin numbers it prints (25 and 26). It watches for one lamp; make it watch for two, and add a check that the two are never in the same state. The contact-type prompt now defaults to NO for both.

- [ ] **Step 2: Update `fake-controller.ps1`**

- The emitted `state` line gains `"target"`.
- The emitted `settings` line gains `lift`, `lower`, `target`.
- Accept an incoming `{"t":"target","reps":n}` and apply it to the fake's own target, echoing it back on the next `state`.
- Run the fake cycle off lift/lower seconds instead of jumping between limit-switch events, and end the fake session when the rep count reaches the target.

- [ ] **Step 3: Run the fake controller against the display sketch or by itself**

```bash
pwsh -File firmware/tools/fake-controller.ps1 -WhatIf
```

If the script has no `-WhatIf`, run it against a connected CYD as documented at the top of the file. Expected: the CYD's main screen shows `0 / 3 รอบ` when the fake reports target 3, and the ± buttons on the CYD change the number the fake prints.

- [ ] **Step 4: Commit**

```bash
git add firmware/tools
git commit -m "test(firmware): bench the two-button panel and the rep target"
```

---

## Task 13: Bench verification on real hardware

**Files:** none — this is the acceptance pass. Record the results in the commit message or in `firmware/README.md` if anything surprising turns up.

- [ ] **Step 1: Flash both boards**

```bash
arduino-cli upload --fqbn esp32:esp32:esp32 -p COM_PORT pt-leg-controller
```
(and the same for `pt-leg-display`; `firmware/tools/esp-port.ps1` finds the port).

- [ ] **Step 2: Work the checklist**

1. At power-up: yellow lit, green dark, no click from the green relay.
2. START runs a session: green lit, yellow dark, phases advance on the clock with no switches connected.
3. Target 3 → the session stops itself after 3 reps, lamps swap back, and the app's records list shows one session.
4. STOP mid-lift → the leg lowers for `lowerSec` and the session ends and is reported.
5. STOP while idle → the rep count on the CYD goes to 0 and nothing moves.
6. Force a FAULT (pull the actuator cap low, or set `maxTravelSec` to 1 and let the energize cap trip) → yellow lit, green dark. START then lowers first, then lifts.
7. Change the target on the CYD mid-session → the app's session screen shows the new number within a couple of seconds.
8. Change the target in the app → the CYD shows it within a couple of seconds.
9. Change `liftSec` on the CYD settings screen and save → the next session's lift stroke is visibly longer or shorter.
10. Short the START line to GND and power-cycle → the serial log prints the refusal and no session starts.

- [ ] **Step 3: Report**

Write up which of the ten passed. Anything that failed goes back to the task that owns it — do not patch around it here.

---

## Self-Review Notes

Checked against the spec:

- **Accepted tradeoff** — documented in `config.h` (Task 1 Step 2), not just in the spec, so it survives where an engineer will read it.
- **`SafeLower` always ends Idle** — Task 2's `tick()` has no unconfirmed-arrival branch; `recoveryFailed` is gone from both the header and the source.
- **`maxTravelSec` as ceiling** — enforced in `applySettings` (Task 5 Step 6.2) and in `settingsfile.cpp` (Task 6 Step 1); still fed to `actuatorSetCap` unchanged.
- **Target 0 = unlimited** — honoured in `tick()` (Task 2), allowed by the pydantic bound `ge=0` (Task 8), and offered as `0=ไม่จำกัด` on the CYD spinbox (Task 6).
- **Server target is repeated until acknowledged, not consume-on-read** — Task 8 Step 3, with the reasoning in the docstring; this is the one place the plan deliberately differs in mechanism from the command queue next to it.
- **`SettingsView` device timings** — the spec has the app editing them but named no endpoint, so Task 8 Steps 10–14 add `POST /api/sessions/timings` and the plan carries it through `net.cpp` (Task 5), `device.ts` (Task 9) and the view (Task 10). The timings expire on a 30 s TTL rather than on an acknowledgement, because nothing echoes them; that asymmetry with the target is deliberate and is surfaced in the app's own help text so it does not read as a bug.
- **Spec amended to match** — the spec's Server section now lists the timings endpoint, so the two documents do not disagree.
