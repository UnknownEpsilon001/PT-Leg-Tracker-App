# Physical NC Buttons + Output-Only Display Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move session run control (START / STOP / RESET) from the CYD touchscreen to three physical normally-closed panel buttons on the controller, and turn the display main screen into an output-only status view with a large idle logo.

**Architecture:** The controller MCU gains a small edge-triggered button module that mirrors the existing limit-switch debounce and drives `SessionSM`. The display drops its touch GO/STOP button (settings screen stays), shows a bigger idle logo, and the Display→Controller `cmd` UART message is removed. Shared files (`link.*`, `session.h`, `settings.h`) are synced controller→display via `sync-shared.ps1`.

**Tech Stack:** Arduino/ESP32 C++, LVGL 8 (CYD touchscreen), ArduinoJson over UART, PowerShell tooling. Build with `arduino-cli`.

## Global Constraints

- Firmware has **no unit-test harness**. Verification per task = clean `arduino-cli compile` of the affected sketch(es), `sync-shared.ps1 -Check` for shared-file drift, and bench check via `LINK_OVER_USB` + `tools/fake-controller.ps1` where behavior is observable. State this honestly; do not invent test files.
- Controller sketch FQBN: `esp32:esp32:esp32` — compile with `--fqbn "esp32:esp32:esp32:PartitionScheme=huge_app"`.
- All firmware paths are under `firmware/`. Run `arduino-cli`/PowerShell from the repo root.
- Buttons are **normally-closed (NC)**, read `INPUT_PULLUP`, and follow the existing `SWITCH_NC` polarity convention: pressed (or broken wire) reads open/HIGH.
- Chosen GPIOs: **START=25, STOP=26, RESET=4**. Do not reuse 16/17 (UART), 22/27 (relays), 32/33 (limit switches), or input-only 34/35/36/39.
- Shared files list (kept byte-identical across both sketches): `link.h`, `link.cpp`, `session.h`, `settings.h`. Any edit to these on the controller MUST be followed by `sync-shared.ps1` and re-verified with `-Check`.
- Never commit `LINK_OVER_USB 1` — it is a bench-only switch.
- Bilingual comment convention: this repo comments in English with a `// [TH]` Thai line alongside. Match it for any non-trivial new comment.
- No `Co-Authored-By` / AI attribution in commit messages.

---

### Task 1: Controller — RESET action + button pins in config

Adds the `SessionSM::reset()` used by the RESET button and declares the three button GPIOs. No behavior wired yet; this task just makes the pieces compile.

**Files:**
- Modify: `firmware/pt-leg-controller/config.h`
- Modify: `firmware/pt-leg-controller/session.h`
- Modify: `firmware/pt-leg-controller/session.cpp`

**Interfaces:**
- Produces: `void SessionSM::reset(uint32_t nowMs)` — when idle, zero reps/elapsed and clear a latched FAULT back to Idle; **no-op while running**. Consumed by Task 3.
- Produces: `PIN_BTN_START` (25), `PIN_BTN_STOP` (26), `PIN_BTN_RESET` (4), `BUTTON_DEBOUNCE_MS` (25). Consumed by Task 2.

- [ ] **Step 1: Add button pins + debounce to `config.h`**

Insert after the limit-switch block (after `#define SWITCH_DEBOUNCE_MS 25`):

```cpp
// panel buttons (NC momentary, INPUT_PULLUP, same polarity as SWITCH_NC)
// [TH] ปุ่มหน้าปัด (แบบ NC กดชั่วขณะ) ใช้ INPUT_PULLUP ขั้วเดียวกับ SWITCH_NC
#define PIN_BTN_START 25   // start a session — เริ่มรอบ (เฉพาะตอนว่าง)
#define PIN_BTN_STOP 26    // stop / safe-lower — หยุดและลดขาลงอย่างปลอดภัย (ตลอดเวลา)
#define PIN_BTN_RESET 4    // clear reps + FAULT when idle — ล้างจำนวนครั้งและสถานะ FAULT ตอนว่าง
#define BUTTON_DEBOUNCE_MS 25 // เวลากันเด้งของปุ่ม (มิลลิวินาที)
```

- [ ] **Step 2: Declare `reset()` in `session.h`**

Add to the `public:` section of `class SessionSM`, right after the `forceFault` declaration:

```cpp
  void reset(uint32_t nowMs);  // idle-only: zero reps/elapsed, clear FAULT -> Idle; no-op while running
                               // [TH] เฉพาะตอนว่าง: ล้างจำนวนครั้ง/เวลา และล้าง FAULT กลับสู่ Idle ; ไม่ทำอะไรถ้ากำลังทำงาน
```

- [ ] **Step 3: Implement `reset()` in `session.cpp`**

Add this method (place it next to `forceFault`). It reuses the private `enter(Phase, nowMs)` helper to land in Idle cleanly:

```cpp
void SessionSM::reset(uint32_t nowMs) {
  // [TH] ปุ่ม RESET: ใช้ได้เฉพาะตอนว่าง/ผิดพลาด ไม่ให้ล้างกลางรอบ (ต้องกด STOP ก่อน)
  if (running()) return;         // must STOP first; RESET never interrupts a session
  repCount = 0;
  finalElapsed = 0;
  finalRepCount = 0;
  startedEdge = false;
  stoppedEdge = false;
  enter(Phase::Idle, nowMs);     // clears a latched Fault back to Idle
}
```

Verify against `session.cpp`: `enter(Phase, uint32_t)`, `running()`, and members `repCount`, `finalElapsed`, `finalRepCount`, `startedEdge`, `stoppedEdge` all exist (they are declared in `session.h`). If `enter` resets `repCount` itself, the explicit zeroing above is still correct and harmless.

- [ ] **Step 4: Compile the controller sketch**

Run:
```
arduino-cli compile --fqbn "esp32:esp32:esp32:PartitionScheme=huge_app" firmware/pt-leg-controller
```
Expected: compiles clean (no errors). `reset()` is defined but not yet called — that is fine.

- [ ] **Step 5: Commit**

```
git add firmware/pt-leg-controller/config.h firmware/pt-leg-controller/session.h firmware/pt-leg-controller/session.cpp
git commit -m "feat(firmware): SessionSM.reset() + panel button pins"
```

---

### Task 2: Controller — `buttons` module (debounced, edge-triggered)

New module reading the three NC buttons. Mirrors `switches.cpp` debounce but exposes a one-shot **press edge** (fires once per press), not a level.

**Files:**
- Create: `firmware/pt-leg-controller/buttons.h`
- Create: `firmware/pt-leg-controller/buttons.cpp`

**Interfaces:**
- Consumes: `PIN_BTN_START/STOP/RESET`, `BUTTON_DEBOUNCE_MS` (Task 1); `SWITCH_NC` define (config.h) for polarity.
- Produces: `void buttonsBegin()`, `void buttonsPoll(uint32_t nowMs)`, `bool buttonStartPressed()`, `bool buttonStopPressed()`, `bool buttonResetPressed()`. Each `*Pressed()` returns true for exactly one loop on the not-pressed→pressed transition, then clears itself. Consumed by Task 3.

- [ ] **Step 1: Write `buttons.h`**

```cpp
#pragma once
#include <stdint.h>

// อ่านปุ่มหน้าปัดสามปุ่ม (START/STOP/RESET) แบบกันเด้งและจับ "ขอบขากด" ครั้งเดียว
// Debounced, edge-triggered reads of the three NC panel buttons.
// Unlike switches.* (which report a debounced level for the safety limit
// switches), each accessor here returns true for exactly ONE loop on the
// press edge, so a held button fires once. Polarity follows SWITCH_NC.
// [TH] ต่างจาก switches.* ที่รายงานระดับสัญญาณของลิมิตสวิตช์เพื่อความปลอดภัย
//      ที่นี่คืนค่า true เพียงลูปเดียวตอนกด เพื่อให้กดค้างก็ทำงานครั้งเดียว
void buttonsBegin();
void buttonsPoll(uint32_t nowMs);  // call every loop, before SessionSM::tick
                                   // [TH] เรียกทุกลูปก่อน SessionSM::tick
bool buttonStartPressed();         // one-shot press edge — ขอบกดครั้งเดียว
bool buttonStopPressed();
bool buttonResetPressed();
```

- [ ] **Step 2: Write `buttons.cpp`**

```cpp
// กันเด้งปุ่มแบบเดียวกับ switches.cpp แล้วยกธง "เพิ่งกด" หนึ่งครั้งเมื่อค่านิ่งเปลี่ยนจากไม่กด->กด
// Debounce like switches.cpp, then raise a one-shot "just pressed" flag when the
// stable state transitions not-pressed -> pressed.
#include "buttons.h"
#include <Arduino.h>
#include "config.h"

struct Btn {
  uint8_t pin;
  bool stable;      // debounced logical pressed — ค่าที่ผ่านกันเด้งแล้ว
  bool lastRaw;     // last raw logical read — ค่าดิบครั้งก่อน
  uint32_t changed; // ms of last raw change — เวลาที่ค่าดิบเปลี่ยนล่าสุด
  bool edge;        // one-shot press edge, cleared on read — ขอบกดครั้งเดียว อ่านแล้วล้าง
};

static Btn bStart{PIN_BTN_START, false, false, 0, false};
static Btn bStop{PIN_BTN_STOP, false, false, 0, false};
static Btn bReset{PIN_BTN_RESET, false, false, 0, false};

static bool rawPressed(uint8_t pin) {
  int level = digitalRead(pin);
#ifdef SWITCH_NC
  return level == HIGH;  // NC: pressed (or broken wire) reads open/HIGH — แบบ NC กด/สายขาด = HIGH
#else
  return level == LOW;   // NO to GND with pullup: pressed reads LOW — แบบ NO ลง GND = LOW
#endif
}

static void poll(Btn& b, uint32_t nowMs) {
  bool raw = rawPressed(b.pin);
  if (raw != b.lastRaw) {
    b.lastRaw = raw;
    b.changed = nowMs;
  } else if (nowMs - b.changed >= BUTTON_DEBOUNCE_MS && raw != b.stable) {
    b.stable = raw;
    if (raw) b.edge = true;  // rising: not-pressed -> pressed — ขอบขาขึ้น: จากไม่กดเป็นกด
  }
}

static bool takeEdge(Btn& b) {
  bool e = b.edge;
  b.edge = false;
  return e;
}

void buttonsBegin() {
  pinMode(PIN_BTN_START, INPUT_PULLUP);
  pinMode(PIN_BTN_STOP, INPUT_PULLUP);
  pinMode(PIN_BTN_RESET, INPUT_PULLUP);
}

void buttonsPoll(uint32_t nowMs) {
  poll(bStart, nowMs);
  poll(bStop, nowMs);
  poll(bReset, nowMs);
}

bool buttonStartPressed() { return takeEdge(bStart); }
bool buttonStopPressed() { return takeEdge(bStop); }
bool buttonResetPressed() { return takeEdge(bReset); }
```

- [ ] **Step 3: Compile the controller sketch**

Run:
```
arduino-cli compile --fqbn "esp32:esp32:esp32:PartitionScheme=huge_app" firmware/pt-leg-controller
```
Expected: compiles clean. `.ino` picks up `buttons.cpp` automatically (same sketch folder); the accessors are unused for now — no error.

- [ ] **Step 4: Commit**

```
git add firmware/pt-leg-controller/buttons.h firmware/pt-leg-controller/buttons.cpp
git commit -m "feat(firmware): edge-triggered NC panel button module"
```

---

### Task 3: Controller — wire buttons into the loop, drop UART `cmd` handling

Buttons now drive the session. The Display→Controller `cmd` handler is removed from the sketch (the message itself is removed from the protocol in Task 4). Server relay start/stop is untouched.

**Files:**
- Modify: `firmware/pt-leg-controller/pt-leg-controller.ino`

**Interfaces:**
- Consumes: `buttonsBegin/Poll`, `buttonStartPressed/StopPressed/ResetPressed` (Task 2); `SessionSM::reset` (Task 1); existing `gSession.start/requestStop`.

- [ ] **Step 1: Include the buttons header**

In `pt-leg-controller.ino`, add after `#include "switches.h"`:

```cpp
#include "buttons.h"
```

- [ ] **Step 2: Remove the link `cmd` handler**

Delete the `onLinkCmd` function (currently lines ~26-31):

```cpp
static void onLinkCmd(const char* v) {
  uint32_t now = millis();
  if (!strcmp(v, "start")) gSession.start(now);
  else if (!strcmp(v, "stop")) gSession.requestStop(now);
}
```

And in `setup()`, delete the line:

```cpp
  gLink.onCmd = onLinkCmd;
```

- [ ] **Step 3: Initialize buttons in `setup()`**

Add right after `switchesBegin();`:

```cpp
  buttonsBegin();
```

- [ ] **Step 4: Poll + act on buttons in `loop()`**

In `loop()`, after `switchesPoll(now);` add `buttonsPoll(now);`. Then, immediately after the existing `gSession.tick(now, switchTop(), switchBottom());` line, add:

```cpp
  // panel buttons: START (idle only), STOP (any state), RESET (idle only)
  // [TH] ปุ่มหน้าปัด: START เริ่ม (เฉพาะว่าง), STOP หยุด (ตลอดเวลา), RESET ล้าง (เฉพาะว่าง)
  if (buttonStartPressed()) gSession.start(now);
  if (buttonStopPressed()) gSession.requestStop(now);
  if (buttonResetPressed()) gSession.reset(now);
```

`gSession.start()` already no-ops usefully mid-session (it only advances from Idle/Fault); `requestStop()` is safe any time; `reset()` self-gates to idle. No extra guards needed.

- [ ] **Step 5: Compile the controller sketch**

Run:
```
arduino-cli compile --fqbn "esp32:esp32:esp32:PartitionScheme=huge_app" firmware/pt-leg-controller
```
Expected: compiles clean. (`gLink.onCmd` is still a member on `Link` until Task 4; leaving it unset is fine.)

- [ ] **Step 6: Commit**

```
git add firmware/pt-leg-controller/pt-leg-controller.ino
git commit -m "feat(firmware): drive session from panel buttons, drop UART cmd"
```

---

### Task 4: Shared link — remove the `cmd` message; update protocol doc

Removes `cmd` from the shared `link` transport on the controller (source of truth) and the protocol doc. Display callers of `sendCmd` are removed in Task 5; this task is sequenced before the sync in Task 6, and the display is not recompiled until Task 5 has landed.

**Files:**
- Modify: `firmware/pt-leg-controller/link.h`
- Modify: `firmware/pt-leg-controller/link.cpp`
- Modify: `firmware/link-protocol.md`

**Interfaces:**
- Removes: `Link::onCmd` callback, `Link::sendCmd(const char*)`, and the `"cmd"` dispatch branch. No task after this may reference them.

- [ ] **Step 1: Remove `onCmd` and `sendCmd` from `link.h`**

Open `firmware/pt-leg-controller/link.h`. Delete the `onCmd` callback member (the `void (*onCmd)(const char* v)` line) and the `void sendCmd(const char* v);` declaration. Leave `onHello`, `onState`, `onSettings`, `onSettingsReply`, `sendHello`, `sendPing`, `sendState`, `sendSettings` intact.

- [ ] **Step 2: Remove the `cmd` dispatch + sender from `link.cpp`**

In `firmware/pt-leg-controller/link.cpp`, delete the dispatch branch:

```cpp
  if (!strcmp(t, "cmd")) {
    if (onCmd) onCmd(d["v"] | "");
  } else if (!strcmp(t, "hello")) {
```

so it becomes:

```cpp
  if (!strcmp(t, "hello")) {
```

Then delete the `sendCmd` definition:

```cpp
void Link::sendCmd(const char* v) {
  JsonDocument d; d["t"] = "cmd"; d["v"] = v; emit(_io, d);
}
```

- [ ] **Step 3: Update `link-protocol.md`**

In the **Display → Controller** section, delete the line:

```
- `{"t":"cmd","v":"start"|"stop"}` — touch button press.
```

In **Notes**, replace the UART-loss note's parenthetical about the unreachable touch STOP with the new reality. Change:

```
- UART loss is a safety event: if the controller sees no line for ~2 s during a
  session it safe-lowers and stops (the touch STOP would otherwise be
  unreachable).
```

to:

```
- Run control (START/STOP/RESET) is on physical NC panel buttons wired to the
  controller, so it no longer travels over UART.
- UART loss is still a safety event: if the controller sees no line for ~2 s
  during a session it safe-lowers and stops, as defense-in-depth (the physical
  STOP remains the primary stop).
```

- [ ] **Step 4: Compile the controller sketch**

Run:
```
arduino-cli compile --fqbn "esp32:esp32:esp32:PartitionScheme=huge_app" firmware/pt-leg-controller
```
Expected: compiles clean (Task 3 already removed the only controller reference to `onCmd`).

- [ ] **Step 5: Commit**

```
git add firmware/pt-leg-controller/link.h firmware/pt-leg-controller/link.cpp firmware/link-protocol.md
git commit -m "feat(firmware): remove display->controller cmd from UART link"
```

---

### Task 5: Display — output-only main screen + bigger logo

Removes the touch GO/STOP button and its callbacks; keeps the gear→settings path. Enlarges and centers the idle logo. The wifi/server status corner and the running/idle logo-swap already exist and stay.

**Files:**
- Modify: `firmware/pt-leg-display/ui.h`
- Modify: `firmware/pt-leg-display/ui.cpp`
- Modify: `firmware/pt-leg-display/pt-leg-display.ino`
- Regenerate: `firmware/pt-leg-display/logo_img.c` (via `tools/make-logo.ps1`)

**Interfaces:**
- Changes: `uiSetCallbacks` signature from `(onStart, onStop, onOpenSettings)` to `(onOpenSettings)` only.
- Removes (display side): `onTouchStart`, `onTouchStop`, and the `sendCmd` calls in `pt-leg-display.ino`; `btnGo`, `lblGo`, `cbStart`, `cbStop`, `goClicked` in `ui.cpp`.

- [ ] **Step 1: Regenerate a larger logo**

The current `logo_img.c` is 200×109 on a 320×240 panel. Regenerate at width 300, same near-white tint used before:

```
powershell -File firmware/tools/make-logo.ps1 -Width 300 -Tint f2f5f9
```
Expected output line like `generated ... logo_img.c : 300x163, ~143 KB of flash`. This overwrites `firmware/pt-leg-display/logo_img.c`.

- [ ] **Step 2: Narrow `uiSetCallbacks` in `ui.h`**

Change the declaration:

```cpp
void uiSetCallbacks(void (*onStart)(), void (*onStop)(), void (*onOpenSettings)());
```
to:
```cpp
void uiSetCallbacks(void (*onOpenSettings)());  // main screen is output-only; only the gear is interactive
                                                // [TH] หน้าจอหลักแสดงผลอย่างเดียว มีเพียงปุ่มเฟืองที่กดได้
```
Also update the file-top comment "…and START/STOP buttons…" to reflect that the main screen is now status-only with a settings gear.

- [ ] **Step 3: Remove the GO button + start/stop callbacks in `ui.cpp`**

In `ui.cpp`:

a. In the `static lv_obj_t *scrMain, ...` declarations, remove `btnGo` and `lblGo`.

b. Remove the callback pointers and their use:
```cpp
static void (*cbStart)() = nullptr;
static void (*cbStop)() = nullptr;
```
and the `goClicked` function entirely.

c. Replace `uiSetCallbacks`:
```cpp
void uiSetCallbacks(void (*onStart)(), void (*onStop)(), void (*onOpenSettings)()) {
  cbStart = onStart;
  cbStop = onStop;
  cbSettings = onOpenSettings;
}
```
with:
```cpp
void uiSetCallbacks(void (*onOpenSettings)()) {
  cbSettings = onOpenSettings;
}
```

d. In `buildMain()`, delete the block that creates `btnGo`/`lblGo`:
```cpp
  btnGo = makeBtn(scrMain, "START", COL_SURFACE, &lv_font_montserrat_28, goClicked);
  lv_obj_set_size(btnGo, 260, 56);
  lv_obj_align(btnGo, LV_ALIGN_BOTTOM_MID, 0, -24);
  lblGo = lv_obj_get_child(btnGo, 0);
```

e. In `buildMain()`, re-center the logo now that no button sits below it. Change:
```cpp
  lv_obj_align(imgLogo, LV_ALIGN_TOP_MID, 0, 46);  // clears the START button at y=160
```
to:
```cpp
  lv_obj_align(imgLogo, LV_ALIGN_CENTER, 0, -6);  // full-screen idle branding; nothing below it now
                                                  // [TH] โลโก้ตอนว่างเต็มจอ ไม่มีปุ่มอยู่ข้างล่างแล้ว
```

f. In `uiUpdateMain()`, delete the GO-button block:
```cpp
  lv_label_set_text(lblGo, fault ? "RESTART" : (running ? "STOP" : "START"));
  uint32_t goCol = fault ? COL_FAULT : (running ? COL_STOP : COL_SURFACE);
  lv_obj_set_style_bg_color(btnGo, lv_color_hex(goCol), 0);
  if (linkUp) lv_obj_clear_state(btnGo, LV_STATE_DISABLED);
  else lv_obj_add_state(btnGo, LV_STATE_DISABLED);
```
Keep everything else in `uiUpdateMain` (clock, reps, phase, status corner, logo show/hide, the `if (running) ...` gear-disable block, `leaveSettingsForStop()`).

- [ ] **Step 4: Update the display sketch `pt-leg-display.ino`**

a. Delete the touch start/stop callbacks:
```cpp
static void onTouchStart() { Serial.println("touch: start"); gLink.sendCmd("start"); }
static void onTouchStop() { Serial.println("touch: stop"); gLink.sendCmd("stop"); }
```

b. Change the `uiSetCallbacks` call in `setup()`:
```cpp
  uiSetCallbacks(onTouchStart, onTouchStop, onOpenSettings);
```
to:
```cpp
  uiSetCallbacks(onOpenSettings);
```

c. Update the file-top comment: it says the sketch "relays touch commands back" — change to note the main screen is output-only and only settings are sent back.

- [ ] **Step 5: Compile the display sketch**

Run:
```
arduino-cli compile --fqbn "esp32:esp32:esp32:PartitionScheme=huge_app" firmware/pt-leg-display
```
Expected: compiles clean. (At this point `link.*` in the display folder is still the OLD synced copy that still defines `sendCmd`; nothing calls it, so it compiles. Task 6 syncs the new copy.)

- [ ] **Step 6: Commit**

```
git add firmware/pt-leg-display/ui.h firmware/pt-leg-display/ui.cpp firmware/pt-leg-display/pt-leg-display.ino firmware/pt-leg-display/logo_img.c
git commit -m "feat(firmware): output-only display main screen, larger idle logo"
```

---

### Task 6: Sync shared files + full verification

Propagates the Task 4 link changes into the display sketch and verifies both boards compile against identical shared code, plus a bench pass of the display UI.

**Files:**
- Modify (generated copy): `firmware/pt-leg-display/link.h`, `firmware/pt-leg-display/link.cpp` (overwritten by sync)

- [ ] **Step 1: Sync shared files controller→display**

Run:
```
powershell -File firmware/sync-shared.ps1
```
Expected: `synced: link.h` and `synced: link.cpp` (session.h/settings.h unchanged this round → not listed or "nothing to do" for those).

- [ ] **Step 2: Verify no drift**

Run:
```
powershell -File firmware/sync-shared.ps1 -Check
```
Expected: `shared files in sync` (exit 0).

- [ ] **Step 3: Compile BOTH sketches clean**

Run:
```
arduino-cli compile --fqbn "esp32:esp32:esp32:PartitionScheme=huge_app" firmware/pt-leg-controller
arduino-cli compile --fqbn "esp32:esp32:esp32:PartitionScheme=huge_app" firmware/pt-leg-display
```
Expected: both compile clean, no references to `sendCmd`/`onCmd` remain.

- [ ] **Step 4: Bench-verify the display UI (no controller)**

Per `firmware/README.md`: set `LINK_OVER_USB` to `1` in `pt-leg-display.ino`, flash the CYD, and drive it with `firmware/tools/fake-controller.ps1`. Confirm:
- Idle: large logo centered, wifi/server status corner reflects the fake flags, **no START/STOP button** on screen.
- Running (fake drives phases): logo hidden, clock/reps/phase shown and advancing.
- Stop → idle: large logo returns.
- Gear still opens settings when idle.

Then **revert `LINK_OVER_USB` to `0`** before committing anything. Do not commit it as `1`.

If `arduino-cli`/hardware is unavailable in this environment, record that the bench step was not run and leave it for the user; the compile + drift checks above still gate correctness.

- [ ] **Step 5: Commit the synced copies**

```
git add firmware/pt-leg-display/link.h firmware/pt-leg-display/link.cpp
git commit -m "chore(firmware): sync shared link files to display sketch"
```

---

## Self-Review

**Spec coverage:**
- 3 NC buttons START/STOP/RESET on controller → Tasks 1-3. ✓
- START idle-only / STOP anytime / RESET idle-only (reps+fault) → Task 3 gating + Task 1 `reset()`. ✓
- New `buttons.cpp/.h`, edge-triggered, separate from `switches.cpp` → Task 2. ✓
- GPIOs 25/26/4, avoid used/input-only pins → Global Constraints + Task 1. ✓
- Display output-only: remove touch GO/STOP, keep gear→settings → Task 5. ✓
- Big idle logo, hides when running, returns on stop → Task 5 (regen + recenter; hide/return logic already present, preserved). ✓
- wifi/server indicators → already present in `uiUpdateMain`, preserved (Task 5 keeps status corner). ✓
- Protocol drops Display→Controller `cmd`; UART watchdog retained → Task 4. ✓
- Shared-file sync discipline → Task 6. ✓
- NC wire-break START risk → mitigated by limit-switch + max-travel bounds (spec risk section); no code change required, noted in Task 3. ✓

**Placeholder scan:** No TBD/TODO; every code step shows full code. ✓

**Type consistency:** `reset(uint32_t)` defined Task 1, called Task 3. `buttonStartPressed/StopPressed/ResetPressed` defined Task 2, called Task 3. `uiSetCallbacks(void(*)())` changed in ui.h (Task 5 Step 2), ui.cpp (Step 3c), and caller .ino (Step 4b) consistently. `sendCmd`/`onCmd` removed in Task 4 with no later references. ✓
