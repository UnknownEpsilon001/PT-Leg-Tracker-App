# Thai On-Screen Keyboard Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let someone type Thai on the CYD, so a patient's real name can be recorded on the add-patient form.

**Architecture:** Two Thai key pages live in LVGL's `USER_1` / `USER_2` keyboard modes, defined once in a new `thai_kb` module and applied to both existing keyboards. LVGL's built-in keyboard event handler is replaced, because it types unknown control keys into the textarea instead of acting on them.

**Tech Stack:** Arduino ESP32 core 3.3.11, LVGL 8.4.0, TFT_eSPI 2.5.43, Kanit fonts already generated at 14/20/28 px covering `0x0E00-0x0E7F`.

**Spec:** `docs/superpowers/specs/2026-07-28-thai-keyboard-design.md` (status: approved)

## Global Constraints

- Build with `--fqbn "esp32:esp32:esp32:PartitionScheme=huge_app"`. The default scheme leaves 1.31 MB and the display sketch does not fit.
- Every non-obvious comment is bilingual: English first, then a `[TH]` line. Match the density of the surrounding file.
- **Every LVGL screen and dialog is built once and re-filled.** The pool is 48 KB and `LV_ASSERT_HANDLER` is `while(1)`, so a rebuilt-per-open widget is a silent hard freeze after a few opens. Both keyboards are already built once inside their screen builders; keep it that way.
- `LINK_OVER_USB` in `pt-leg-display.ino` must be `0` in every commit. Flip it locally for bench runs only.
- **Never edit a file containing Thai through PowerShell's `Get-Content` / `Set-Content`.** On 5.1 that reads UTF-8 as ANSI and writes a BOM, double-encoding every Thai line. Use an editor, or `[System.IO.File]::ReadAllText($p, [System.Text.Encoding]::UTF8)` with `WriteAllText` and `New-Object System.Text.UTF8Encoding($false)`.
- Thai numerals (๐-๙) are deliberately omitted; Arabic numerals are on the `?123` page.
- No change to `patients.csv` format, the link protocol, or any screen layout.

## File Structure

| File | Responsibility |
|---|---|
| `firmware/pt-leg-display/thai_kb.h` (create) | One entry point: `void thaiKbApply(lv_obj_t* kb, bool startThai)` |
| `firmware/pt-leg-display/thai_kb.cpp` (create) | Both key maps, both control arrays, the replacement event handler |
| `firmware/pt-leg-display/ui.cpp` (modify) | Two call sites plus the Thai font on each keyboard |
| `firmware/README.md` (modify) | Document the layout and the handler substitution |

Task 1 is self-contained and compiles alone. Task 2 wires it in and is the first point where anything is visible on the panel. Task 3 verifies and documents.

---

### Task 1: The `thai_kb` module

**Files:**
- Create: `firmware/pt-leg-display/thai_kb.h`
- Create: `firmware/pt-leg-display/thai_kb.cpp`

**Interfaces:**
- Consumes: LVGL only. No project headers, no SD, no fonts — the caller owns the font.
- Produces, consumed by Task 2: `void thaiKbApply(lv_obj_t* kb, bool startThai)`.

- [x] **Step 1: Write `thai_kb.h`**

```cpp
#pragma once
#include <lvgl.h>

// แป้นพิมพ์ภาษาไทยแบบโทรศัพท์สำหรับจอ CYD
// Phone-style Thai keyboard pages for the CYD's on-screen keyboards.
//
// Two Thai pages are installed into LVGL's USER_1 (unshifted) and USER_2
// (shifted) keyboard modes; the Latin pages LVGL ships with are left alone.
// [TH] ติดตั้งแป้นไทยสองหน้าในโหมด USER_1 และ USER_2 ของ LVGL
//      ส่วนแป้นภาษาอังกฤษที่มากับ LVGL ยังใช้ได้ตามเดิม
//
// Call once per keyboard, right after lv_keyboard_create. startThai decides
// which script the keyboard opens on: true for patient names, false for
// settings, where the fields are ASCII by protocol.
// [TH] เรียกครั้งเดียวต่อแป้นพิมพ์หนึ่งตัว หลังสร้างแป้นพิมพ์เสร็จ
//      startThai กำหนดว่าจะเปิดมาเป็นภาษาไทยหรืออังกฤษ ชื่อคนไข้ใช้ไทย ส่วนหน้าตั้งค่าใช้อังกฤษ
void thaiKbApply(lv_obj_t* kb, bool startThai);
```

- [x] **Step 2: Write the key maps in `thai_kb.cpp`**

```cpp
#include "thai_kb.h"
#include <string.h>

// Ten columns, because 320 px / 10 gives 32 px keys. A PC Kedmanee layout wants
// twelve, and 26 px keys on a resistive panel are hard to hit.
// [TH] ใช้สิบคอลัมน์ เพราะ 320 หารสิบได้ปุ่มกว้าง 32 พิกเซล
//      ถ้าเรียงแบบแป้นพิมพ์คอมพิวเตอร์ต้องใช้สิบสองคอลัมน์ ปุ่มจะเหลือ 26 พิกเซล ซึ่งกดยากบนจอสัมผัสแบบต้านทาน
static const char* MAP_TH1[] = {
  "ๆ", "ไ", "ำ", "พ", "ะ", "ั", "ี", "ร", "น", "ย", "\n",
  "บ", "ล", "ฟ", "ห", "ก", "ด", "เ", "้", "่", "า", "\n",
  "ส", "ว", "ง", "ผ", "ป", "แ", "อ", "ิ", "ื", "ท", "\n",
  LV_SYMBOL_UP, "ม", "ใ", "ฝ", "ถ", "ุ", "ึ", "ค", "ต", LV_SYMBOL_BACKSPACE, "\n",
  "?123", LV_SYMBOL_KEYBOARD, " ", LV_SYMBOL_OK, ""
};

// The characters page 1 has no room for. Every Thai character must be reachable
// from one of the two pages: LVGL's button matrix has no long-press popup, so
// there is no third way to get at a rare consonant.
// [TH] ตัวอักษรที่หน้าแรกใส่ไม่พอ ทุกตัวต้องกดถึงได้จากสองหน้านี้
//      เพราะปุ่มของ LVGL ไม่มีการกดค้างเพื่อเลือกตัวอักษรอื่น
static const char* MAP_TH2[] = {
  "ข", "ช", "ฃ", "ฅ", "ฆ", "จ", "ฉ", "ซ", "ฌ", "ญ", "\n",
  "ฎ", "ฏ", "ฐ", "ฑ", "ฒ", "ณ", "ธ", "ภ", "ศ", "ษ", "\n",
  "ฬ", "ฮ", "ฤ", "ฦ", "โ", "ู", "็", "๊", "๋", "์", "\n",
  LV_SYMBOL_UP, "ํ", "ฯ", "ฺ", "฿", "๏", "๚", "๛", LV_SYMBOL_BACKSPACE, "\n",
  "?123", LV_SYMBOL_KEYBOARD, " ", LV_SYMBOL_OK, ""
};

// The low bits of a control entry are the key's relative width, so the bottom
// row reads 2 + 2 + 4 + 2 = ten units, matching the ten columns above it.
// [TH] บิตล่างของค่าควบคุมคือความกว้างของปุ่ม แถวล่างจึงรวมได้ 2+2+4+2 = สิบหน่วย เท่ากับสิบคอลัมน์ด้านบน
static const lv_btnmatrix_ctrl_t CTRL_TH1[] = {
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  2, 2, 4, 2
};

static const lv_btnmatrix_ctrl_t CTRL_TH2[] = {
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  2, 1, 1, 1, 1, 1, 1, 1, 1,
  2, 2, 4, 2
};
```

- [x] **Step 3: Write the replacement event handler, below the maps**

```cpp
// LVGL's own keyboard handler knows only the keys in its own maps. A "?123" or
// globe key defined in a user map is not one of them, so the stock handler types
// the label into the textarea instead of switching pages — a patient called
// "สมชาย?123" is the symptom. It is removed and this one put in its place, which
// means this function owns the Latin pages' control keys too.
// [TH] ตัวจัดการปุ่มของ LVGL รู้จักเฉพาะปุ่มในแป้นของตัวเอง ปุ่ม "?123" หรือปุ่มลูกโลกที่เราสร้างเองมันไม่รู้จัก
//      มันจะพิมพ์ข้อความบนปุ่มลงในช่องกรอกแทนที่จะสลับหน้า จึงต้องถอดออกแล้วใช้ตัวนี้แทน
//      ผลคือฟังก์ชันนี้ต้องดูแลปุ่มควบคุมของแป้นภาษาอังกฤษด้วย
static void thaiKbEvent(lv_event_t* e) {
  lv_obj_t* kb = (lv_obj_t*)lv_event_get_target(e);
  lv_obj_t* ta = lv_keyboard_get_textarea(kb);
  uint16_t id = lv_btnmatrix_get_selected_btn(kb);
  const char* txt = lv_btnmatrix_get_btn_text(kb, id);
  if (!txt) return;

  // Page and script switching.
  // [TH] การสลับหน้าแป้นและสลับภาษา
  if (strcmp(txt, LV_SYMBOL_UP) == 0) {
    lv_keyboard_mode_t m = lv_keyboard_get_mode(kb);
    lv_keyboard_set_mode(kb, m == LV_KEYBOARD_MODE_USER_1 ? LV_KEYBOARD_MODE_USER_2
                                                          : LV_KEYBOARD_MODE_USER_1);
    return;
  }
  if (strcmp(txt, LV_SYMBOL_KEYBOARD) == 0) {
    lv_keyboard_mode_t m = lv_keyboard_get_mode(kb);
    bool onThai = (m == LV_KEYBOARD_MODE_USER_1 || m == LV_KEYBOARD_MODE_USER_2);
    lv_keyboard_set_mode(kb, onThai ? LV_KEYBOARD_MODE_TEXT_LOWER : LV_KEYBOARD_MODE_USER_1);
    return;
  }
  if (strcmp(txt, "?123") == 0 || strcmp(txt, "1#") == 0) {
    lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_SPECIAL);
    return;
  }
  if (strcmp(txt, "ABC") == 0) { lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_UPPER); return; }
  if (strcmp(txt, "abc") == 0) { lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_LOWER); return; }

  // Editing and closing, kept identical to what the stock handler did, so the
  // OK and X keys behave the way every other screen already expects.
  // [TH] ปุ่มแก้ไขและปุ่มปิด ทำงานเหมือนของเดิมทุกอย่าง เพื่อให้ปุ่มตกลงและปุ่มกากบาทยังทำงานตามที่หน้าจออื่นคาดไว้
  if (strcmp(txt, LV_SYMBOL_BACKSPACE) == 0) { if (ta) lv_textarea_del_char(ta); return; }
  if (strcmp(txt, LV_SYMBOL_OK) == 0 || strcmp(txt, LV_SYMBOL_NEW_LINE) == 0) {
    lv_event_send(kb, LV_EVENT_READY, NULL);
    return;
  }
  if (strcmp(txt, LV_SYMBOL_CLOSE) == 0) {
    lv_event_send(kb, LV_EVENT_CANCEL, NULL);
    return;
  }
  if (strcmp(txt, LV_SYMBOL_LEFT) == 0) { if (ta) lv_textarea_cursor_left(ta); return; }
  if (strcmp(txt, LV_SYMBOL_RIGHT) == 0) { if (ta) lv_textarea_cursor_right(ta); return; }

  // Anything else is a character. lv_textarea_add_text takes a UTF-8 string, so
  // a three-byte Thai character arrives intact.
  // [TH] ที่เหลือคือตัวอักษร ฟังก์ชันนี้รับสตริงแบบ UTF-8 อักษรไทยขนาดสามไบต์จึงเข้าไปครบ
  if (ta) lv_textarea_add_text(ta, txt);
}

void thaiKbApply(lv_obj_t* kb, bool startThai) {
  lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_USER_1, MAP_TH1, CTRL_TH1);
  lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_USER_2, MAP_TH2, CTRL_TH2);
  lv_obj_remove_event_cb(kb, lv_keyboard_def_event_cb);
  lv_obj_add_event_cb(kb, thaiKbEvent, LV_EVENT_VALUE_CHANGED, NULL);
  if (startThai) lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_USER_1);
}
```

- [x] **Step 4: Compile**

Run: `arduino-cli compile --fqbn "esp32:esp32:esp32:PartitionScheme=huge_app" firmware/pt-leg-display`
Expected: success. The module is unused until Task 2, so only compilation is proven here. If `lv_keyboard_def_event_cb` is reported undeclared, the LVGL pin has drifted off 8.4.0 — reinstall the pin rather than porting.

- [x] **Step 5: Commit**

```bash
git add firmware/pt-leg-display/thai_kb.h firmware/pt-leg-display/thai_kb.cpp
git commit -m "feat(firmware): Thai keyboard pages for the CYD

Patient names are Thai and the on-screen keyboards were Latin-only, so a name
could only be recorded transliterated. Two Thai pages go into LVGL's USER_1 and
USER_2 modes, ten 32 px columns wide, with every Thai character reachable from
one of the two because a button matrix has no long-press alternates.

LVGL's stock keyboard handler is replaced: it recognises only its own control
keys and types anything else into the textarea, so a page-switch key defined in
a user map would end up inside the patient's name."
```

---

### Task 2: Wire both keyboards up

**Files:**
- Modify: `firmware/pt-leg-display/ui.cpp` (the `kb` creation inside `buildSettings`, and the `kbAdd` creation inside the add-patient screen builder)

**Interfaces:**
- Consumes: `thaiKbApply(lv_obj_t*, bool)` from Task 1.
- Produces: nothing consumed later.

Both keyboards already exist and are built once inside their screen builders. This task adds two calls and the Thai font; it changes no sizes, positions or show/hide logic.

- [x] **Step 1: Include the module** — beside the other local includes at the top of `ui.cpp`:

```cpp
#include "thai_kb.h"  // Thai key pages for both keyboards — แป้นไทยของทั้งสองแป้นพิมพ์
```

- [x] **Step 2: Apply it to the settings keyboard** — in `buildSettings`, immediately after the `lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);` line:

```cpp
  // Latin on open: this screen's fields are ASCII by protocol. deviceCode is
  // matched byte-for-byte by the server and serverUrl has to resolve, so a Thai
  // character in either means the machine silently never pairs. Thai stays one
  // tap away rather than being where a finger lands first.
  // [TH] เปิดมาเป็นภาษาอังกฤษ เพราะช่องในหน้านี้ต้องเป็นอักษรอังกฤษตามข้อกำหนด
  //      รหัสอุปกรณ์ถูกเทียบแบบตรงตัวที่เซิร์ฟเวอร์ และที่อยู่เซิร์ฟเวอร์ต้องแปลงเป็นที่อยู่จริงได้
  //      ถ้ามีอักษรไทยปนจะจับคู่ไม่ได้โดยไม่มีข้อความเตือน ภาษาไทยจึงอยู่ถัดไปหนึ่งปุ่ม
  lv_obj_set_style_text_font(kb, &thai20, 0);
  thaiKbApply(kb, false);
```

- [x] **Step 3: Apply it to the add-patient keyboard** — immediately after the `lv_obj_add_flag(kbAdd, LV_OBJ_FLAG_HIDDEN);` line:

```cpp
  // Thai on open: this keyboard exists for ชื่อผู้ป่วย, and the code and age
  // fields beside it are short enough to reach Latin in one tap.
  // [TH] เปิดมาเป็นภาษาไทย เพราะแป้นนี้มีไว้พิมพ์ชื่อผู้ป่วย
  //      ส่วนช่องรหัสและอายุกดสลับไปภาษาอังกฤษได้ในปุ่มเดียว
  lv_obj_set_style_text_font(kbAdd, &thai20, 0);
  thaiKbApply(kbAdd, true);
```

- [x] **Step 4: Compile**

Run: `arduino-cli compile --fqbn "esp32:esp32:esp32:PartitionScheme=huge_app" firmware/pt-leg-display`
Expected: success, around 50% program storage.

- [x] **Step 5: Flash and smoke-test**

```bash
arduino-cli compile --fqbn "esp32:esp32:esp32:PartitionScheme=huge_app" --upload -p COM4 firmware/pt-leg-display
```

Open ข้อมูล → ลงทะเบียน, tap the name field. Expected: the keyboard appears showing Thai. Type one character and confirm it lands in the field rather than a `?`. Tap the globe and confirm Latin appears. If a key's label prints into the field instead of switching pages, the default handler was not removed.

- [x] **Step 6: Commit**

```bash
git add firmware/pt-leg-display/ui.cpp
git commit -m "feat(firmware): Thai keyboard on the patient and settings forms

The add-patient keyboard opens on Thai, since it exists to type ชื่อผู้ป่วย. The
settings keyboard opens on Latin, because deviceCode is matched byte-for-byte by
the server and serverUrl has to resolve — a Thai character in either fails
silently. Both keep every existing size, position and event."
```

---

### Task 3: Bench matrix and documentation

**Files:**
- Modify: `firmware/README.md`

**Interfaces:** none — this task verifies Tasks 1-2 and documents the result.

Prereq: CYD flashed, microSD in the slot, a PC card reader for step 2.

- [x] **Step 1: Type a stack-heavy name** — add a patient named `เกิ่น`, age 30, and save. Expected: every character appears as typed, the marks stack the way they do in the roster.

- [x] **Step 2: Check what reached the card** — card into a PC reader, open `patients.csv`. Expected: the row reads `<code>,เกิ่น,30`, the file is UTF-8 with no BOM, and no other row changed.

- [ ] **Step 3: Check the roster and finish screen** — reinsert, open ข้อมูล. Expected: the name renders correctly in the list. Select the patient, run a session with the fake controller, and confirm the finish screen shows it — that screen draws at a larger font where stacks overlap, so note how it looks.

- [x] **Step 4: Latin still works** — open the settings screen, tap เครือข่าย WiFi. Expected: the keyboard opens Latin, typing works, บันทึก still reaches the controller and the card.

- [x] **Step 5: Page flips do not leak** — flip ⇧, the globe and `?123` 20+ times across both keyboards. Expected: no freeze. Optional temporary print in `loop()`:

```cpp
  lv_mem_monitor_t m; lv_mem_monitor(&m);
  Serial.printf("lvgl free %u frag %u%%\n", (unsigned)m.free_size, (unsigned)m.frag_pct);
```

Free bytes flat across all flips, not trending down. Remove the print before committing.

- [x] **Step 6: Key accuracy** — type all four character rows on both Thai pages. Expected: the character you meant is the character you get. 32 px keys on a resistive panel are the least certain part of this design; if a row misses consistently, note which and stop — that is a calibration question, not a keyboard one.

- [x] **Step 7: Document it in `firmware/README.md`** — add a section after the settings section:

````markdown
## Thai keyboard

Both on-screen keyboards carry Thai on LVGL's `USER_1` (unshifted) and `USER_2`
(shifted) pages, defined in `pt-leg-display/thai_kb.cpp`. Ten columns give 32 px
keys on the 320 px panel; a PC Kedmanee layout would need twelve and 26 px keys,
which are hard to hit on a resistive screen.

The add-patient keyboard opens on Thai, the settings keyboard on Latin —
`deviceCode` is matched byte-for-byte by the server and `serverUrl` must resolve,
so a Thai character in either means the machine silently never pairs.

`⇧` swaps the two Thai pages, the globe key swaps script, `?123` reaches numbers.
There are no long-press alternates: a button matrix has no popup layer, so every
Thai character is on one of the two pages.

**The stock LVGL handler cannot be kept.** `lv_keyboard_def_event_cb` recognises
only the control keys in its own maps and types anything else into the textarea,
so a page-switch key from a user map lands inside the patient's name.
`thaiKbApply` removes it and installs a replacement that owns every control key,
including the Latin pages'.

Combining marks stack acceptably at thai14 and overlap at thai20/28 — measured in
`docs/superpowers/specs/2026-07-28-thai-keyboard-design.md`. Prefer thai14 for
text that may carry a vowel and a tone mark on one consonant.
[TH] แป้นพิมพ์ไทยอยู่ในโหมด USER_1 และ USER_2 ของ LVGL สิบคอลัมน์ ปุ่มกว้าง 32 พิกเซล
     หน้าลงทะเบียนคนไข้เปิดมาเป็นไทย ส่วนหน้าตั้งค่าเปิดมาเป็นอังกฤษ เพราะรหัสอุปกรณ์และที่อยู่เซิร์ฟเวอร์
     ต้องเป็นอักษรอังกฤษ ไม่มีการกดค้างเลือกตัวอักษร ทุกตัวจึงต้องอยู่ในสองหน้านี้
````

- [x] **Step 8: Commit**

```bash
git add firmware/README.md
git commit -m "docs(firmware): document the Thai keyboard"
```

---

## Self-Review

**Spec coverage:** ten-column phone layout → Task 1 step 2; page 2 holding every remaining character → Task 1 step 2; `⇧` / globe / `?123` roles → Task 1 step 3; the handler substitution → Task 1 step 3 and documented in Task 3 step 7; module boundary `thai_kb.h/.cpp` with `thaiKbApply(kb, startThai)` → Task 1; add-patient opens Thai and settings opens Latin → Task 2 steps 2-3; the accepted `deviceCode` / `serverUrl` risk → Task 2 step 2 comment and Task 3 step 7; testing items 1-6 → Task 3 steps 1-6. Thai numerals stay omitted, per the spec.

**Type consistency:** `thaiKbApply(lv_obj_t*, bool)` is declared in Task 1 step 1 and called with the same signature in Task 2 steps 2-3. `MAP_TH1` / `MAP_TH2` pair with `CTRL_TH1` / `CTRL_TH2`, each control array holding one entry per key and none for the `"\n"` separators. `thai20` is the existing static font in `ui.cpp`, used only there.

**Out of scope, per spec:** long-press alternates, Thai in the phone app, Thai numerals, predictive text, roster format changes.
