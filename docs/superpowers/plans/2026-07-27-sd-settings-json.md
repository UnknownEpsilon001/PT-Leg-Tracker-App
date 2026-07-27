# SD-Backed Device Settings (`settings.json`) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Store device settings as human-editable JSON on the CYD's microSD card so a Wi-Fi password can be typed on a PC instead of a resistive touchscreen, and so a machine's configuration survives a controller board swap.

**Architecture:** The card is the master copy; the controller keeps its NVS copy so the machine still runs with the screen unplugged or the card absent. The controller cannot read the card — it is on the CYD's SPI bus — so the CYD pushes settings to the controller rather than the controller pulling. Reconciliation reuses the existing `hello` and `settings` UART messages, so the link protocol does not change.

**Tech Stack:** Arduino ESP32 core 3.3.10, ArduinoJson 7.4.3 (already a display dependency via `link.cpp`), SD/FS (already mounted by `store.cpp`), LVGL 8.4.0, TFT_eSPI 2.5.43.

**Spec:** `docs/superpowers/specs/2026-07-21-sd-settings-json-design.md` (status: approved)

## Global Constraints

- Build both sketches with `--fqbn "esp32:esp32:esp32:PartitionScheme=huge_app"`. The default scheme leaves 1.31 MB and the display sketch does not fit.
- Every non-obvious comment is bilingual: English first, then a `[TH]` line. Match the density of the surrounding file.
- **Every LVGL screen and dialog is built once and re-filled.** The pool is 48 KB and `LV_ASSERT_HANDLER` is `while(1)`, so a rebuilt-per-open dialog is a silent hard freeze after a few opens. This bug shipped once already, on 2026-07-21.
- No change to the UART link message schema (`firmware/link-protocol.md`). Reconciliation uses the existing `hello` and `settings` messages only.
- The machine never refuses to run because of a settings, card, or link problem. Motion and the phone path always work; configuration convenience degrades on its own.
- `LINK_OVER_USB` in `pt-leg-display.ino` must be `0` in every commit. Flip it locally for bench runs, never commit it as `1`.
- SD mount logic stays in exactly one place: `store.cpp`. `settingsfile.cpp` calls `storeCardPresent()` and never mounts.
- Settings are JSON; session and patient data stay CSV. Different shapes, different formats.
- The Wi-Fi password is plaintext on removable media. Accepted deliberately for a clinic/contest LAN — do not add encryption in this plan.

## File Structure

| File | Responsibility |
|---|---|
| `firmware/pt-leg-display/settingsfile.h` (create) | Public API: defaults, load, save, present |
| `firmware/pt-leg-display/settingsfile.cpp` (create) | `/settings.json` read/write, atomic temp+rename, malformed-file quarantine |
| `firmware/pt-leg-display/pt-leg-display.ino` (modify) | Boot reconciliation; write card on save |
| `firmware/pt-leg-display/ui.h` / `ui.cpp` (modify) | SD status line, RESET DEFAULTS button, confirm dialog |
| `firmware/pt-leg-controller/config.h` (modify) | `DEFAULT_DEVICE_CODE` |
| `firmware/pt-leg-controller/settings.cpp` (modify) | Use `DEFAULT_DEVICE_CODE` instead of an inline `""` |
| `firmware/README.md` (modify) | Document the file, precedence, and recovery |

Task order matters: Task 1 is independent, Task 2 has no dependencies on the UI, Tasks 3-6 build on Task 2, Task 7 verifies the lot.

---

### Task 1: Controller — `deviceCode` defaults to `"default"`

**Files:**
- Modify: `firmware/pt-leg-controller/config.h`
- Modify: `firmware/pt-leg-controller/settings.cpp:16`

**Interfaces:**
- Produces: `DEFAULT_DEVICE_CODE` macro, consumed by Task 2's `settingsFileDefaults` (which hardcodes the same string on the display side, since the two sketches share no headers).

Why: the phone app's zero-setup flow pairs with `"default"`. The controller currently defaults the code to `""`, so a fresh machine and a fresh app disagree until someone types a code.

- [ ] **Step 1: Add the macro to `config.h`** — beside the other defaults near line 63:

```cpp
// Pairing code a machine answers to before anyone configures one. Matches the
// phone app's zero-setup default, so a fresh machine and a fresh app find each
// other with no typing. An empty code would pair with nothing.
// [TH] รหัสจับคู่ตั้งต้นของเครื่องก่อนตั้งค่าใดๆ ตรงกับค่าเริ่มต้นของแอปบนมือถือ
//      เครื่องใหม่กับแอปใหม่จึงจับคู่กันได้เลยโดยไม่ต้องพิมพ์ ถ้าเป็นค่าว่างจะจับคู่ไม่ได้เลย
#define DEFAULT_DEVICE_CODE "default"
```

- [ ] **Step 2: Use it in `settings.cpp`** — replace line 16:

```cpp
  s.deviceCode = p.getString("code", DEFAULT_DEVICE_CODE);
```

- [ ] **Step 3: Compile**

Run: `arduino-cli compile --fqbn "esp32:esp32:esp32:PartitionScheme=huge_app" firmware/pt-leg-controller`
Expected: success, ~34% program storage.

- [ ] **Step 4: Commit**

```bash
git add firmware/pt-leg-controller
git commit -m "fix(firmware): controller pairing code defaults to \"default\"

An unconfigured controller defaulted its device code to an empty string, which
pairs with nothing, while the phone app's zero-setup flow looks for \"default\".
A fresh machine and a fresh app could not find each other until someone typed a
code on the touchscreen."
```

---

### Task 2: Display — `settingsfile` module

**Files:**
- Create: `firmware/pt-leg-display/settingsfile.h`
- Create: `firmware/pt-leg-display/settingsfile.cpp`

**Interfaces:**
- Consumes: `DeviceSettings` (from `settings.h`), `storeCardPresent()` (from `store.h`).
- Produces, consumed by Tasks 3-6:
  - `void settingsFileDefaults(DeviceSettings& s)`
  - `bool settingsFileLoad(DeviceSettings& s)` — false when no card, no file, or malformed
  - `bool settingsFilePresent()` — true when a valid file was read or written this boot
  - `bool settingsFileSave(const DeviceSettings& s)` — false on write failure

- [ ] **Step 1: Write `settingsfile.h`**

```cpp
#pragma once
#include "settings.h"  // DeviceSettings

// ค่าตั้งค่าของเครื่องที่เก็บเป็นไฟล์ JSON บนการ์ด SD ของบอร์ดจอ (/settings.json)
// Device settings stored as JSON on the CYD's microSD card (/settings.json).
//
// The card is the master copy: a readable file wins over the controller's NVS,
// because editing the file on a PC has to actually take effect. The controller
// keeps its NVS copy so the machine still runs with no card and no screen.
// [TH] การ์ดคือค่าหลัก ถ้าอ่านไฟล์ได้จะใช้ค่าจากการ์ดแทนค่าใน NVS ของคอนโทรลเลอร์
//      เพราะการแก้ไฟล์จากคอมพิวเตอร์ต้องมีผลจริง ส่วนคอนโทรลเลอร์ยังเก็บสำเนาไว้ใน NVS
//      เพื่อให้เครื่องทำงานได้แม้ไม่มีการ์ดและไม่มีจอ
//
// Everything here is safe to call with no card: load returns false and save
// reports failure, and the caller carries on.
// [TH] ทุกฟังก์ชันเรียกได้แม้ไม่มีการ์ด โหลดจะคืน false และบันทึกจะรายงานว่าไม่สำเร็จ

void settingsFileDefaults(DeviceSettings& s);   // ค่าตั้งต้นตามเอกสารออกแบบ
bool settingsFileLoad(DeviceSettings& s);       // false: no card / no file / malformed
bool settingsFilePresent();                     // a valid file was read or written this boot
bool settingsFileSave(const DeviceSettings& s); // temp + rename; false on write failure
```

- [ ] **Step 2: Write `settingsfile.cpp`**

```cpp
#include "settingsfile.h"
#include <SD.h>
#include <ArduinoJson.h>
#include "store.h"  // storeCardPresent — การ์ดถูกเมานต์ที่ store.cpp ที่เดียว

static const char* PATH = "/settings.json";
static const char* TMP = "/settings.tmp";
static const char* BAD = "/settings.bad.json";

static bool filePresent = false;

void settingsFileDefaults(DeviceSettings& s) {
  // Blank network fields, because there is no sensible guess for someone else's
  // clinic Wi-Fi. The pairing code is not blank: "default" is what the phone app
  // looks for before anyone configures anything.
  // [TH] ช่องเครือข่ายเว้นว่างไว้ เพราะเดาค่า WiFi ของคลินิกแต่ละแห่งไม่ได้
  //      ส่วนรหัสจับคู่ไม่เว้นว่าง ใช้ "default" ซึ่งเป็นค่าที่แอปบนมือถือมองหา
  s.wifiSsid = "";
  s.wifiPass = "";
  s.serverUrl = "";
  s.deviceCode = "default";
  s.maxTravelSec = 8;
  s.holdSec = 10;
  s.restSec = 10;
}

bool settingsFileLoad(DeviceSettings& s) {
  filePresent = false;
  if (!storeCardPresent()) return false;
  File f = SD.open(PATH, FILE_READ);
  if (!f) return false;

  JsonDocument d;
  DeserializationError err = deserializeJson(d, f);
  f.close();

  if (err) {
    // A half-written or hand-mangled file is renamed rather than deleted: the
    // person who broke it may want to see it, and the caller writes a fresh one
    // straight after. Deleting would destroy the only copy of their password.
    // [TH] ไฟล์ที่เสียหรือถูกแก้จนผิดรูปแบบจะถูกเปลี่ยนชื่อเก็บไว้ ไม่ลบทิ้ง
    //      เพราะเจ้าของอาจอยากดูของเดิม และตัวเรียกจะเขียนไฟล์ใหม่ให้ทันที
    SD.remove(BAD);
    SD.rename(PATH, BAD);
    return false;
  }

  // Missing keys take the documented default, unknown keys are ignored, so a
  // half-filled file someone typed by hand still boots the machine.
  // [TH] คีย์ที่ขาดไปจะใช้ค่าตั้งต้น คีย์แปลกปลอมจะถูกข้าม ไฟล์ที่พิมพ์มาไม่ครบจึงยังใช้งานได้
  DeviceSettings out;
  settingsFileDefaults(out);
  out.wifiSsid = d["wifiSsid"] | "";
  out.wifiPass = d["wifiPass"] | "";
  out.serverUrl = d["serverUrl"] | "";
  out.deviceCode = d["deviceCode"] | "default";
  out.maxTravelSec = d["maxTravelSec"] | (uint16_t)8;
  out.holdSec = d["holdSec"] | (uint16_t)10;
  out.restSec = d["restSec"] | (uint16_t)10;

  s = out;
  filePresent = true;
  return true;
}

bool settingsFilePresent() { return filePresent; }

bool settingsFileSave(const DeviceSettings& s) {
  if (!storeCardPresent()) return false;

  JsonDocument d;
  d["wifiSsid"] = s.wifiSsid;
  d["wifiPass"] = s.wifiPass;
  d["serverUrl"] = s.serverUrl;
  d["deviceCode"] = s.deviceCode;
  d["maxTravelSec"] = s.maxTravelSec;
  d["holdSec"] = s.holdSec;
  d["restSec"] = s.restSec;

  // Written to a temp file and renamed over the real one. A card pulled during
  // the write then costs the new settings, never the old ones — the alternative
  // leaves a truncated file that reads as malformed on the next boot.
  // [TH] เขียนลงไฟล์ชั่วคราวก่อนแล้วค่อยเปลี่ยนชื่อทับไฟล์จริง ถ้าการ์ดถูกดึงระหว่างเขียน
  //      จะเสียแค่ค่าที่เพิ่งตั้ง ไม่ใช่ค่าเดิม ถ้าเขียนทับตรงๆ จะได้ไฟล์ครึ่งๆ กลางๆ ที่อ่านไม่ออก
  SD.remove(TMP);
  File f = SD.open(TMP, FILE_WRITE);
  if (!f) return false;
  size_t written = serializeJsonPretty(d, f);
  f.close();
  if (written == 0) {
    SD.remove(TMP);
    return false;
  }

  SD.remove(PATH);
  if (!SD.rename(TMP, PATH)) return false;
  filePresent = true;
  return true;
}
```

- [ ] **Step 3: Compile the display sketch**

Run: `arduino-cli compile --fqbn "esp32:esp32:esp32:PartitionScheme=huge_app" firmware/pt-leg-display`
Expected: success. The module is unused until Task 3, so only compilation is proven here.

- [ ] **Step 4: Commit**

```bash
git add firmware/pt-leg-display/settingsfile.h firmware/pt-leg-display/settingsfile.cpp
git commit -m "feat(firmware): CYD settings.json read/write module

Settings live only in the controller's NVS today, so a Wi-Fi password has to be
typed on a resistive touchscreen and a controller swap loses the machine's whole
configuration. This reads and writes a human-editable /settings.json on the card
the display already mounts. Writes go via a temp file and a rename so a card
pulled mid-write costs the new values rather than corrupting the old ones, and a
malformed file is quarantined as settings.bad.json instead of deleted."
```

---

### Task 3: Display — boot reconciliation

**Files:**
- Modify: `firmware/pt-leg-display/pt-leg-display.ino`

**Interfaces:**
- Consumes: `settingsFileLoad`, `settingsFileSave`, `settingsFilePresent` (Task 2); existing `gLast`, `onLinkSettingsReply`, `gLink.sendSettings`.
- Produces: `gCardSettings` (file-was-read flag) consumed by Task 5's status line.

The precedence rule: a readable card wins over controller NVS. A card with no file seeds itself from the controller's reply, so inserting a blank card into a configured machine does not wipe it.

- [ ] **Step 1: Include the module and add the boot flags** — after the `sdlog.h` include, and beside `gSettingsKnown`:

```cpp
#include "settingsfile.h"  // /settings.json on the card — ค่าตั้งค่าบนการ์ด
```

```cpp
// Did the card hold a readable settings file at boot? Decides which way the
// first reconciliation runs: card wins if it did, controller wins if it did not.
// [TH] ตอนบูตอ่านไฟล์ตั้งค่าจากการ์ดได้หรือไม่ ตัวแปรนี้ตัดสินว่ารอบแรกจะยึดค่าจากฝั่งไหน
//      ถ้าอ่านได้ให้ยึดการ์ด ถ้าไม่ได้ให้ยึดค่าจากคอนโทรลเลอร์
static bool gCardSettings = false;
static bool gReconciled = false;  // the first controller reply has been handled
```

- [ ] **Step 2: Read the card in `setup()`** — immediately after the `storeBegin()` line that prints the SD status:

```cpp
  // Read before the link comes up: the controller's reply is what triggers
  // reconciliation, and this decides which side of it wins.
  // [TH] อ่านไฟล์ก่อนที่ลิงก์จะพร้อม เพราะการตอบกลับของคอนโทรลเลอร์คือจุดที่เริ่มเทียบค่า
  //      และค่าที่อ่านได้ตรงนี้เป็นตัวตัดสินว่าจะยึดฝั่งใด
  gCardSettings = settingsFileLoad(gLast);
  Serial.printf("settings source: %s\n", gCardSettings ? "sd card" : "controller nvs");
  if (gCardSettings) uiSetDeviceCode(gLast.deviceCode.c_str());
```

- [ ] **Step 3: Reconcile inside `onLinkSettingsReply`** — replace the whole function:

```cpp
static void onLinkSettingsReply(const LinkSettings& s) {
  // The card wins when it had a file, so a password edited on a PC actually
  // takes effect. Push it once, then let later replies flow in normally — the
  // controller echoes what it just stored, and fighting that would loop.
  // [TH] ถ้าการ์ดมีไฟล์อยู่ให้ยึดค่าจากการ์ด รหัสผ่านที่แก้จากคอมพิวเตอร์จะได้มีผลจริง
  //      ส่งให้คอนโทรลเลอร์ครั้งเดียว จากนั้นปล่อยให้การตอบกลับครั้งถัดไปทำงานตามปกติ
  //      เพราะคอนโทรลเลอร์จะสะท้อนค่าที่เพิ่งบันทึกกลับมา ถ้าดันกันไปมาจะวนไม่จบ
  if (gCardSettings && !gReconciled) {
    gReconciled = true;
    LinkSettings ls;
    ls.ssid = gLast.wifiSsid; ls.pass = gLast.wifiPass; ls.server = gLast.serverUrl;
    ls.code = gLast.deviceCode;
    ls.maxTravel = gLast.maxTravelSec; ls.hold = gLast.holdSec; ls.rest = gLast.restSec;
    gLink.sendSettings(ls);
    Serial.println("settings: pushed the card's values to the controller");
    gSettingsKnown = true;
    uiSetSettingsKnown(true);
    return;
  }

  gLast.wifiSsid = s.ssid; gLast.wifiPass = s.pass; gLast.serverUrl = s.server;
  gLast.deviceCode = s.code;
  gLast.maxTravelSec = s.maxTravel; gLast.holdSec = s.hold; gLast.restSec = s.rest;
  uiSetDeviceCode(s.code.c_str());
  gSettingsKnown = true;
  uiSetSettingsKnown(true);  // the controller's real values are on screen now

  // A card with no file seeds itself from the machine's current configuration,
  // so a blank card put into a configured machine does not blank it.
  // [TH] การ์ดที่ยังไม่มีไฟล์จะถูกสร้างไฟล์จากค่าที่เครื่องใช้อยู่จริง
  //      การ์ดเปล่าที่เสียบเข้าเครื่องที่ตั้งค่าไว้แล้วจึงไม่ทำให้ค่าหาย
  if (!gReconciled) {
    gReconciled = true;
    if (storeCardPresent() && !settingsFilePresent()) {
      Serial.printf("settings: seeding the card from the controller: %s\n",
                    settingsFileSave(gLast) ? "written" : "write failed");
    }
  }
}
```

- [ ] **Step 4: Compile**

Run: `arduino-cli compile --fqbn "esp32:esp32:esp32:PartitionScheme=huge_app" firmware/pt-leg-display`
Expected: success.

- [ ] **Step 5: Bench check with the fake controller** — flash with `LINK_OVER_USB` flipped to `1` locally, then:

```powershell
.\firmware\tools\fake-controller.ps1 -Seconds 30 -Port COM4
```

Expected on a card with no `/settings.json`: `settings source: controller nvs`, then `settings: seeding the card from the controller: written`. Pull the card and confirm `/settings.json` exists and matches. Put it back, reboot, expect `settings source: sd card` and `settings: pushed the card's values to the controller`.

- [ ] **Step 6: Set `LINK_OVER_USB` back to `0`, then commit**

```bash
git add firmware/pt-leg-display/pt-leg-display.ino
git commit -m "feat(firmware): CYD reconciles settings between card and controller

The card is the master copy, so a file edited on a PC is pushed to the
controller on the first reply after boot and takes effect. A card with no file
is seeded from the controller's current settings instead, so putting a blank
card into a configured machine does not blank it."
```

---

### Task 4: Display — SAVE writes the card

**Files:**
- Modify: `firmware/pt-leg-display/pt-leg-display.ino` (`onSaveFromUi`)
- Modify: `firmware/pt-leg-display/ui.h`, `firmware/pt-leg-display/ui.cpp`

**Interfaces:**
- Consumes: `settingsFileSave` (Task 2).
- Produces: `void uiReportSettingsSaved(bool toCard)` — called by the sketch after a save so the screen can say whether the card was written. Consumed by nothing later; Task 5 places the label it writes to.

- [ ] **Step 1: Declare the reporter in `ui.h`** — after `uiSetSettingsKnown`:

```cpp
// Result of the last SAVE. The controller always receives the values; the card
// may not have been there, and the screen must not imply it was written.
// [TH] ผลของการกดบันทึกครั้งล่าสุด ค่าจะถูกส่งให้คอนโทรลเลอร์เสมอ แต่การ์ดอาจไม่ได้เสียบอยู่
//      หน้าจอต้องไม่ทำให้เข้าใจว่าเขียนลงการ์ดแล้ว
void uiReportSettingsSaved(bool toCard);
```

- [ ] **Step 2: Write the card in `onSaveFromUi`** — replace the function body in the sketch:

```cpp
static void onSaveFromUi(const DeviceSettings& s) {
  gLast = s;
  LinkSettings ls;
  ls.ssid = s.wifiSsid; ls.pass = s.wifiPass; ls.server = s.serverUrl; ls.code = s.deviceCode;
  ls.maxTravel = s.maxTravelSec; ls.hold = s.holdSec; ls.rest = s.restSec;
  gLink.sendSettings(ls);
  uiSetDeviceCode(s.deviceCode.c_str());

  // The controller gets the values either way; the card is best-effort. Settings
  // are never silently dropped, so a failed card write is reported, not hidden.
  // [TH] คอนโทรลเลอร์ได้รับค่าเสมอ ส่วนการ์ดเขียนได้ก็ดี ไม่ได้ก็ไม่เป็นไร
  //      แต่ต้องไม่เงียบ ถ้าเขียนการ์ดไม่สำเร็จต้องบอกผู้ใช้
  bool toCard = settingsFileSave(s);
  Serial.printf("settings saved: controller yes, card %s\n", toCard ? "yes" : "no");
  uiReportSettingsSaved(toCard);
}
```

- [ ] **Step 3: Implement the reporter in `ui.cpp`** — beside `applySaveEnabled`, using the status label Task 5 adds. Until Task 5 lands, guard on null so this compiles and runs on its own:

```cpp
static lv_obj_t* lblSdStatus = nullptr;  // filled in by buildSettings (Task 5)

void uiReportSettingsSaved(bool toCard) {
  if (!lblSdStatus) return;
  lv_label_set_text(lblSdStatus, toCard ? "SD: บันทึกแล้ว" : "SD: เขียนการ์ดไม่ได้");
  lv_obj_set_style_text_color(lblSdStatus, lv_color_hex(toCard ? COL_MUTED : COL_FAULT), 0);
}
```

- [ ] **Step 4: Compile**

Run: `arduino-cli compile --fqbn "esp32:esp32:esp32:PartitionScheme=huge_app" firmware/pt-leg-display`
Expected: success.

- [ ] **Step 5: Commit**

```bash
git add firmware/pt-leg-display
git commit -m "feat(firmware): SAVE writes settings to the card as well as the controller

The controller receives the values either way; the card write is best-effort and
its result is reported rather than assumed, so a save with no card in the slot
does not look like it was stored."
```

---

### Task 5: Display — SD status line on the settings screen

**Files:**
- Modify: `firmware/pt-leg-display/ui.h`, `firmware/pt-leg-display/ui.cpp`
- Modify: `firmware/pt-leg-display/pt-leg-display.ino`

**Interfaces:**
- Consumes: `lblSdStatus` (declared in Task 4), `settingsFilePresent`/`storeCardPresent`.
- Produces: `void uiSetSdStatus(bool cardPresent, bool fileLoaded)` — called by the sketch when opening settings.

The spec's known consequence is that a stale card silently overwrites good NVS settings. This line is the whole mitigation: the state is visible rather than silent.

- [ ] **Step 1: Declare it in `ui.h`**

```cpp
// Where the settings on screen came from. A card carrying stale values wins over
// the controller's NVS by design, so the screen has to say when that happened.
// [TH] บอกว่าค่าที่แสดงอยู่มาจากไหน เพราะค่าจากการ์ดจะถูกใช้แทนค่าใน NVS เสมอ
//      ถ้าการ์ดมีค่าเก่าค้างอยู่ ผู้ใช้ต้องเห็นว่ากำลังใช้ค่าจากการ์ด
void uiSetSdStatus(bool cardPresent, bool fileLoaded);
```

- [ ] **Step 2: Create the label in `buildSettings`** — after the `applySaveEnabled();` line, inside the top bar:

```cpp
  lblSdStatus = lv_label_create(bar);
  lv_obj_set_style_text_font(lblSdStatus, &thai14, 0);
  lv_obj_set_style_text_color(lblSdStatus, lv_color_hex(COL_MUTED), 0);
  lv_obj_align(lblSdStatus, LV_ALIGN_CENTER, 0, 0);
  lv_label_set_text(lblSdStatus, "");
```

- [ ] **Step 3: Implement the setter in `ui.cpp`** — beside `uiReportSettingsSaved`:

```cpp
void uiSetSdStatus(bool cardPresent, bool fileLoaded) {
  if (!lblSdStatus) return;
  const char* text = !cardPresent  ? "SD: ไม่มีการ์ด"
                     : fileLoaded  ? "SD: ใช้ค่าจากการ์ด"
                                   : "SD: การ์ดยังไม่มีไฟล์";
  lv_label_set_text(lblSdStatus, text);
  lv_obj_set_style_text_color(lblSdStatus, lv_color_hex(COL_MUTED), 0);
}
```

- [ ] **Step 4: Call it when the screen opens** — in the sketch's `onOpenSettings`:

```cpp
static void onOpenSettings() {
  Serial.println("touch: settings");
  // A card pushed in after boot is picked up by the retry loop in loop(); this
  // just reports whatever the current state is when the screen opens.
  // [TH] การ์ดที่เพิ่งเสียบหลังบูตจะถูกเมานต์โดยลูปที่ทำงานเป็นระยะอยู่แล้ว
  //      ตรงนี้เพียงรายงานสถานะปัจจุบันตอนเปิดหน้าจอ
  uiSetSdStatus(storeCardPresent(), settingsFilePresent());
  uiOpenSettings(gLast);
}
```

- [ ] **Step 5: Compile**

Run: `arduino-cli compile --fqbn "esp32:esp32:esp32:PartitionScheme=huge_app" firmware/pt-leg-display`
Expected: success.

- [ ] **Step 6: Commit**

```bash
git add firmware/pt-leg-display
git commit -m "feat(firmware): settings screen says where its values came from

A readable card overrides the controller's NVS by design, which means an old
card carrying stale settings silently reconfigures a working machine. The screen
now states which source is in use, so the behaviour is visible rather than
surprising."
```

---

### Task 6: Display — RESET DEFAULTS with confirmation

**Files:**
- Modify: `firmware/pt-leg-display/ui.cpp`

**Interfaces:**
- Consumes: `cbSave` (the existing save callback — reset reuses the save path so the card and the controller both get the defaults), `settingsFileDefaults` via a new callback.
- Produces: nothing consumed later.

- [ ] **Step 1: Add the confirm screen, built once** — near the settings screen code. It is a separate screen rather than a floating dialog because every other screen here is built once and re-filled, and a per-open dialog is the exact bug that froze this device on 2026-07-21:

```cpp
// Built once and reused, like every other screen. A dialog created per open
// leaks ~9 KB of the 48 KB LVGL pool each time and freezes the device silently
// on about the fifth open — this codebase has already shipped that bug once.
// [TH] สร้างครั้งเดียวแล้วใช้ซ้ำเหมือนหน้าจออื่น ถ้าสร้างใหม่ทุกครั้งที่เปิด
//      จะกินหน่วยความจำ LVGL ครั้งละราว 9 KB จาก 48 KB และค้างตายเงียบๆ ราวครั้งที่ห้า
static lv_obj_t* scrConfirmReset;

static void buildConfirmReset() {
  scrConfirmReset = lv_obj_create(nullptr);
  styleScreen(scrConfirmReset);

  lv_obj_t* col = lv_obj_create(scrConfirmReset);
  lv_obj_set_size(col, lv_pct(100), lv_pct(100));
  lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(col, 0, 0);
  lv_obj_set_style_pad_row(col, 10, 0);
  lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_text_color(col, lv_color_hex(COL_TEXT), 0);

  lv_obj_t* q = lv_label_create(col);
  lv_obj_set_style_text_font(q, &thai20, 0);
  lv_label_set_text(q, "คืนค่าเริ่มต้นทั้งหมด");

  lv_obj_t* warn = lv_label_create(col);
  lv_obj_set_style_text_font(warn, &thai14, 0);
  lv_obj_set_style_text_color(warn, lv_color_hex(COL_FAULT), 0);
  lv_label_set_text(warn, "รหัสผ่าน WiFi จะถูกลบ");

  lv_obj_t* row = lv_obj_create(col);
  lv_obj_set_size(row, 300, 48);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(row, 12, 0);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* cancel = makeBtn(row, "ยกเลิก", COL_SURFACE, &thai20,
                             [](lv_event_t*) { lv_scr_load(scrSettings); });
  lv_obj_set_size(cancel, 120, 40);

  lv_obj_t* ok = makeBtn(row, "คืนค่า", COL_FAULT, &thai20, resetConfirmed);
  lv_obj_set_size(ok, 120, 40);
}
```

- [ ] **Step 2: Add the confirmed handler** — above `buildConfirmReset` (it is referenced there), reusing the save path so card, screen and controller all end up with the defaults:

```cpp
// Reset goes through the ordinary save path on purpose: the defaults must reach
// the card and the controller exactly the way a typed change does, and writing a
// defaults file beats deleting one — the file always exists and is always the
// thing being read.
// [TH] การคืนค่าใช้เส้นทางเดียวกับการบันทึกปกติ เพื่อให้ค่าตั้งต้นไปถึงทั้งการ์ดและคอนโทรลเลอร์
//      เหมือนการแก้ค่าด้วยมือ และเขียนไฟล์ค่าตั้งต้นดีกว่าลบไฟล์ทิ้ง เพราะไฟล์จะมีอยู่เสมอ
static void resetConfirmed(lv_event_t*) {
  DeviceSettings d;
  settingsFileDefaults(d);
  lv_textarea_set_text(taSsid, d.wifiSsid.c_str());
  lv_textarea_set_text(taPass, d.wifiPass.c_str());
  lv_textarea_set_text(taServer, d.serverUrl.c_str());
  lv_textarea_set_text(taCode, d.deviceCode.c_str());
  lv_spinbox_set_value(spinMax, d.maxTravelSec);
  lv_spinbox_set_value(spinHold, d.holdSec);
  lv_spinbox_set_value(spinRest, d.restSec);
  if (cbSave) cbSave(d);
  lv_scr_load(scrSettings);
}
```

Add `#include "settingsfile.h"` at the top of `ui.cpp` for `settingsFileDefaults`.

- [ ] **Step 3: Add the button to the settings screen** — at the end of `buildSettings`, after the last field row, so it scrolls with the fields rather than crowding the fixed top bar:

```cpp
  lv_obj_t* reset = makeBtn(col, "คืนค่าเริ่มต้น", COL_SURFACE, &thai20,
                            [](lv_event_t*) {
                              if (!scrConfirmReset) buildConfirmReset();
                              lv_scr_load(scrConfirmReset);
                            });
  lv_obj_set_size(reset, lv_pct(100), 40);
```

- [ ] **Step 4: Compile**

Run: `arduino-cli compile --fqbn "esp32:esp32:esp32:PartitionScheme=huge_app" firmware/pt-leg-display`
Expected: success.

- [ ] **Step 5: Leak check on the bench** — flash, then open the settings screen and the reset dialog 20+ times, cancelling each time. Add a temporary print in `loop()` if needed:

```cpp
  lv_mem_monitor_t m; lv_mem_monitor(&m);
  Serial.printf("lvgl free %u frag %u%%\n", (unsigned)m.free_size, (unsigned)m.frag_pct);
```

Expected: free bytes flat across all 20 opens, not trending down. Remove the temporary print before committing.

- [ ] **Step 6: Commit**

```bash
git add firmware/pt-leg-display
git commit -m "feat(firmware): RESET DEFAULTS on the settings screen

Restores the documented defaults to the screen, the card and the controller
through the ordinary save path, behind a confirmation because one stray tap
otherwise costs the Wi-Fi password. The confirm screen is built once and reused,
like every other screen here: a per-open dialog leaks the LVGL pool and freezes
the device silently, which this codebase has shipped once already."
```

---

### Task 7: Bench matrix and documentation

**Files:**
- Modify: `firmware/README.md`

**Interfaces:** none — this task verifies Tasks 1-6 and documents the result.

Prereq: CYD flashed, microSD in the slot, a PC card reader. Items 3, 6 and 7 need the controller board; run everything else first and note what was deferred.

- [ ] **Step 1: No card** — boot with an empty slot. Expected: machine runs, `settings source: controller nvs`, settings screen shows `SD: ไม่มีการ์ด`, SAVE still reaches the controller and reports the card was not written.

- [ ] **Step 2: Blank card** — boot with a card holding no `/settings.json`. Expected: `settings: seeding the card from the controller: written`; pull the card and confirm the file's contents match what the screen shows.

- [ ] **Step 3: Hand-edited card** — change `wifiSsid` in Notepad, boot. Expected: `settings source: sd card`, `settings: pushed the card's values to the controller`, and the controller connects to the new network. (Needs the controller board.)

- [ ] **Step 4: Malformed JSON** — truncate the file mid-object on the PC, boot. Expected: `/settings.bad.json` appears, a fresh `/settings.json` is written from the controller's values, machine runs normally.

- [ ] **Step 5: Reset** — open settings, tap คืนค่าเริ่มต้น, cancel: nothing changes. Repeat and confirm: screen, card and controller all hold the documented defaults, and `deviceCode` reads `default`.

- [ ] **Step 6: Persistence** — power-cycle both boards. Expected: settings survive. (Needs the controller board.)

- [ ] **Step 7: Cloning** — move the card to a second machine. Expected: it comes up configured and pairs on the card's `deviceCode`. (Needs a second machine; note as deferred if unavailable.)

- [ ] **Step 8: Card pulled mid-save** — pull the card while tapping บันทึก, repeatedly. Expected: `/settings.json` is never truncated — either the previous file or a `/settings.tmp` remains.

- [ ] **Step 9: Document it in `firmware/README.md`** — add a section after the SD logging notes:

````markdown
## Device settings live on the card (`/settings.json`)

Settings are stored twice: as JSON on the CYD's microSD card, and in the
controller's NVS. The card is the master copy — a readable file is pushed to the
controller at boot — so a Wi-Fi password can be typed on a PC rather than on the
touchscreen, and a controller board can be swapped without retyping anything.

```json
{
  "wifiSsid": "ClinicWiFi",
  "wifiPass": "hunter2",
  "serverUrl": "http://192.168.0.12:8000",
  "deviceCode": "default",
  "maxTravelSec": 8,
  "holdSec": 10,
  "restSec": 10
}
```

Missing keys take their default; unknown keys are ignored. The settings screen
states which source is in use, because a stale card reconfigures a working
machine by design.

| Situation | Behaviour |
|---|---|
| No card | Runs from NVS; saves reach the controller only, and say so |
| File absent | Seeded from the controller's current settings, so a blank card does not blank the machine |
| Malformed file | Renamed to `/settings.bad.json`, a fresh one written, machine runs |
| Card pulled mid-save | Writes go to `/settings.tmp` and are renamed, so the old file is never truncated |

The Wi-Fi password is plaintext on removable media. That is inherent to editing
it on a PC, and accepted for a clinic LAN.
[TH] ค่าตั้งค่าเก็บสองที่: ไฟล์ JSON บนการ์ด SD ของจอ และ NVS ของคอนโทรลเลอร์
     โดยยึดค่าจากการ์ดเป็นหลัก แก้รหัสผ่าน WiFi จากคอมพิวเตอร์ได้ และเปลี่ยนบอร์ดคอนโทรลเลอร์
     ได้โดยไม่ต้องพิมพ์ใหม่ รหัสผ่านเก็บเป็นข้อความธรรมดาบนการ์ด ยอมรับได้สำหรับเครือข่ายในคลินิก
````

- [ ] **Step 10: Commit**

```bash
git add firmware/README.md
git commit -m "docs(firmware): document SD-backed device settings"
```

---

## Self-Review

**Spec coverage:** file format → Task 2; boot flow steps 1-4 → Task 3; precedence rule → Task 3; save flow (temp+rename, push always) → Tasks 2 and 4; reset to defaults with confirmation → Task 6; failure handling table → Tasks 2, 3, 4 and documented in Task 7; `deviceCode` default change → Task 1; LVGL memory constraint → Task 6 steps 1 and 5; testing matrix items 1-9 → Task 7 steps 1-8 plus the leak check in Task 6 step 5. The spec's "card inserted after boot: remount when the settings screen opens, no polling loop" is deliberately **not** implemented as written: commit `7f84315` already added a 5-second remount retry in `loop()`, so Task 5 step 4 reports the state instead of adding a second mechanism.

**Type consistency:** `settingsFileDefaults` / `settingsFileLoad` / `settingsFilePresent` / `settingsFileSave` are used with the same signatures in Tasks 3-6 as declared in Task 2. `uiReportSettingsSaved(bool)` and `uiSetSdStatus(bool, bool)` match their declarations. `lblSdStatus` is declared in Task 4 and populated in Task 5, guarded on null so Task 4 compiles and runs alone.

**Out of scope, per spec:** server/app sync, password encryption, patient and session data, link schema changes.
