# Offline Patient Data + SD Session Logging Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the machine device-first — register/pick a patient and log each finished session (with a real timestamp) entirely on the CYD's SD card, with no phone and no internet.

**Architecture:** Two-MCU firmware extension. The **CYD** gets the microSD card (patient store + session log + patient UI); the **controller** gets a DS3231 RTC (NTP-synced when online) and sends the current epoch to the CYD in a new `done` UART message on every session end. App + FastAPI server are untouched.

**Tech Stack:** Arduino core for ESP32, lvgl 8.4.x, `SD` (bundled with the ESP32 core), `RTClib` (Adafruit) for the DS3231, ArduinoJson 7.x (existing). Builds via arduino-cli.

> **PREREQUISITE:** This plan runs AFTER the base firmware plan
> `2026-07-19-esp32-firmware.md` (Tasks 1–13 incl. the 2026-07-20 switch
> addendum). It reuses `session.{h,cpp}` (`finalElapsedSec`/`finalReps`/
> `faulted`/`takeStoppedEdge`, `Phase::Fault`), the `link` protocol, `net`
> (WiFi), and the CYD `ui` screens. Do not start until those exist and compile.

## Global Constraints

- Spec: `docs/superpowers/specs/2026-07-20-offline-patient-sd-logging-design.md`.
- The machine must NEVER refuse to run for lack of patient/SD/RTC — motion and the phone path always work; logging degrades gracefully.
- App (`PT-Leg-Tracker/`) and server (`server/`) are UNCHANGED by this plan.
- Epoch is **UTC** unix seconds end-to-end; the CYD adds `TZ_OFFSET_SEC = 7*3600` (Thailand) only when formatting an ISO datetime for the log.
- Patient `code` and `name` entered on the CYD may not contain a comma or newline (enforced on the add form) → `/patients.csv` and `/sessions.csv` need no quote-escaping; a simple positional split parses them.
- Compile check after every task, for whichever board(s) the task touches:
  `arduino-cli compile --fqbn esp32:esp32:esp32 firmware/pt-leg-controller`
  and/or `... firmware/pt-leg-display`. Must succeed.
- Commit after every task. No AI attribution lines in commits.
- New library versions pinned in `firmware/README.md`.

## File Structure

- `firmware/pt-leg-controller/rtc.{h,cpp}` — DS3231 read + set (new).
- `firmware/pt-leg-controller/config.h` — add RTC pins (modify).
- `firmware/pt-leg-controller/link.{h,cpp}` — `done` message (modify; copied to display).
- `firmware/pt-leg-controller/pt-leg-controller.ino` — RTC + NTP + send `done` (modify).
- `firmware/pt-leg-display/store.{h,cpp}` — SD mount + patient CSV store (new).
- `firmware/pt-leg-display/sdlog.{h,cpp}` — session CSV append + RAM ring (new).
- `firmware/pt-leg-display/ui.{h,cpp}` — data menu + finish screen + data button (modify).
- `firmware/pt-leg-display/pt-leg-display.ino` — wire store/sdlog/ui + `onDone` (modify).

---

### Task 1: Controller — DS3231 RTC module

**Files:**
- Create: `firmware/pt-leg-controller/rtc.h`, `firmware/pt-leg-controller/rtc.cpp`
- Modify: `firmware/pt-leg-controller/config.h`, `firmware/README.md`

**Interfaces:**
- Produces: `void rtcBegin()`, `bool rtcPresent()`, `uint32_t rtcEpoch()` (UTC seconds, 0 if absent/unset), `void rtcSetFromEpoch(uint32_t)`. Consumed by Task 3.

- [ ] **Step 1: Install RTClib** — `arduino-cli lib install "RTClib@2.1.4"`. Add the line to `firmware/README.md` pinned-libraries list.

- [ ] **Step 2: Add RTC pins to `config.h`** — after the switch pins:

```cpp
// DS3231 RTC on controller I2C (free after relays 22/27, UART 16/17, switches 32/33)
#define PIN_RTC_SDA 21
#define PIN_RTC_SCL 25
```

- [ ] **Step 3: `rtc.h`**

```cpp
#pragma once
#include <stdint.h>

// DS3231 real-time clock on the controller I2C bus. Battery-backed: correct
// across power cuts once set. All epochs are UTC unix seconds.
void rtcBegin();                    // Wire.begin + probe; safe if the chip is absent
bool rtcPresent();                  // false if I2C init failed
uint32_t rtcEpoch();                // UTC seconds; 0 if absent or never set (year < 2020)
void rtcSetFromEpoch(uint32_t utc); // write the time (used after an NTP sync)
```

- [ ] **Step 4: `rtc.cpp`**

```cpp
#include "rtc.h"
#include <Wire.h>
#include <RTClib.h>
#include "config.h"

static RTC_DS3231 rtc;
static bool present = false;

void rtcBegin() {
  Wire.begin(PIN_RTC_SDA, PIN_RTC_SCL);
  present = rtc.begin(&Wire);
}

bool rtcPresent() { return present; }

uint32_t rtcEpoch() {
  if (!present) return 0;
  DateTime now = rtc.now();
  if (now.year() < 2020) return 0;  // powered up but never set
  return now.unixtime();
}

void rtcSetFromEpoch(uint32_t utc) {
  if (!present || utc == 0) return;
  rtc.adjust(DateTime(utc));
}
```

- [ ] **Step 5: Compile** — `arduino-cli compile --fqbn esp32:esp32:esp32 firmware/pt-leg-controller` → success (module unused until Task 3).

- [ ] **Step 6: Commit**

```bash
git add firmware
git commit -m "feat(firmware): DS3231 RTC module on controller"
```

---

### Task 2: Link — `done` end-of-session message (both ends)

**Files:**
- Modify: `firmware/pt-leg-controller/link.h`, `firmware/pt-leg-controller/link.cpp`, `firmware/link-protocol.md`
- (Task 11 of the base plan already copies `link.*` into `pt-leg-display/`; re-copy after this change — see Step 4.)

**Interfaces:**
- Produces: `struct LinkDone { uint32_t elapsed; uint16_t reps; uint32_t epoch; bool fault; }`, `void Link::sendDone(const LinkDone&)`, handler `void (*Link::onDone)(const LinkDone&)`. Consumed by Task 3 (controller sends) and Task 7 (display receives).

- [ ] **Step 1: Add `LinkDone` + members to `link.h`** — after the `LinkSettings` struct:

```cpp
struct LinkDone {
  uint32_t elapsed;   // finalElapsedSec
  uint16_t reps;      // finalReps
  uint32_t epoch;     // controller RTC UTC seconds, 0 if unset
  bool fault;         // true if the session ended in FAULT
};
```

In the `Link` class public section, add the sender and handler:

```cpp
  void sendDone(const LinkDone& d);
  void (*onDone)(const LinkDone&) = nullptr;  // display side
```

- [ ] **Step 2: Dispatch `done` in `link.cpp`** — in `Link::dispatch`, add a branch before the trailing `// "ping"` comment:

```cpp
  } else if (!strcmp(t, "done")) {
    if (onDone) {
      LinkDone dn{d["elapsed"] | 0u, (uint16_t)(d["reps"] | 0),
                  d["epoch"] | 0u, d["fault"] | false};
      onDone(dn);
    }
```

(Keep the existing `}` chain intact — this is another `else if` on the same `t` ladder.)

- [ ] **Step 3: `sendDone` in `link.cpp`** — add beside the other senders:

```cpp
void Link::sendDone(const LinkDone& d) {
  JsonDocument doc;
  doc["t"] = "done"; doc["elapsed"] = d.elapsed; doc["reps"] = d.reps;
  doc["epoch"] = d.epoch; doc["fault"] = d.fault;
  emit(_io, doc);
}
```

- [ ] **Step 4: Re-copy `link.*` into the display sketch + update the protocol doc**

```bash
cp firmware/pt-leg-controller/link.h firmware/pt-leg-display/link.h
cp firmware/pt-leg-controller/link.cpp firmware/pt-leg-display/link.cpp
```

In `firmware/link-protocol.md`, add under controller→display:
`{"t":"done","elapsed":int,"reps":int,"epoch":unixUTC|0,"fault":bool}` — sent once at session end (clean stop or FAULT); the display logs it to SD.

- [ ] **Step 5: Compile both** — `arduino-cli compile ... firmware/pt-leg-controller` and `... firmware/pt-leg-display` → both succeed.

- [ ] **Step 6: Commit**

```bash
git add firmware
git commit -m "feat(firmware): UART done message for end-of-session logging"
```

---

### Task 3: Controller — NTP→RTC sync + send `done` on session end

**Files:**
- Modify: `firmware/pt-leg-controller/pt-leg-controller.ino`

**Interfaces:**
- Consumes: `rtc*` (Task 1), `Link::sendDone`/`LinkDone` (Task 2), `SessionSM::faulted/finalElapsedSec/finalReps/takeStoppedEdge` (base switch addendum).

- [ ] **Step 1: Includes + globals** — add near the top of `pt-leg-controller.ino`:

```cpp
#include "rtc.h"
#include <time.h>

static bool gNtpDone = false;
```

- [ ] **Step 2: Init RTC + start SNTP in `setup()`** — after `actuatorBegin();` add `rtcBegin();`. After `netBegin(gSettings);` add:

```cpp
  configTime(0, 0, "pool.ntp.org", "time.google.com");  // UTC; DNS resolves once WiFi is up
```

- [ ] **Step 3: One-shot NTP→RTC sync in `loop()`** — add near the top of `loop()` (after `netLoop(now);`):

```cpp
  if (!gNtpDone && netWifiUp()) {
    time_t t = time(nullptr);
    if (t > 1700000000) {  // ~2023-11: SNTP has a real time
      rtcSetFromEpoch((uint32_t)t);
      gNtpDone = true;
    }
  }
```

- [ ] **Step 4: Send `done` on the stopped edge** — replace the base plan's single-line stopped-edge handler:

```cpp
  if (gSession.takeStoppedEdge()) netQueueEvent("stopped", gSession.finalElapsedSec(), gSession.finalReps());
```

with:

```cpp
  if (gSession.takeStoppedEdge()) {
    netQueueEvent("stopped", gSession.finalElapsedSec(), gSession.finalReps());
    LinkDone d{gSession.finalElapsedSec(), gSession.finalReps(), rtcEpoch(),
               gSession.faulted()};
    gLink.sendDone(d);
  }
```

- [ ] **Step 5: Compile** — `arduino-cli compile ... firmware/pt-leg-controller` → success.

- [ ] **Step 6: On-device check (if controller on desk; else defer to Task 8)** — with WiFi creds set and online, serial-log `rtcEpoch()` once/5 s: it becomes non-zero (a plausible UTC epoch) within ~30 s of boot; run a session, confirm a `done` line is emitted on the UART TX (scope or loopback).

- [ ] **Step 7: Commit**

```bash
git add firmware
git commit -m "feat(firmware): controller NTP-syncs RTC and sends done on session end"
```

---

### Task 4: CYD — SD mount + patient store (with VSPI/touch bus fix)

**Files:**
- Create: `firmware/pt-leg-display/store.h`, `firmware/pt-leg-display/store.cpp`
- Modify: `firmware/pt-leg-display/ui.cpp` (touch → software SPI), `firmware/README.md`

**Interfaces:**
- Produces: `struct Patient { String code, name; uint16_t age; }`, `bool storeBegin()`, `bool storeCardPresent()`, `int storePatientCount()`, `const Patient& storePatient(int i)`, `bool storeAddPatient(const Patient&)`. Consumed by Tasks 5–7.

- [ ] **Step 1: Free the VSPI bus for the SD card** — the SD card needs VSPI (SCK 18 / MISO 19 / MOSI 23 / CS 5), but base-plan Task 6 put the XPT2046 touch on `SPIClass touchSpi(VSPI)` at pins 25/39/32/33. Two hardware SPI buses exist; TFT_eSPI owns the other. Move touch to **software SPI** so VSPI is free for SD:
  - Install: `arduino-cli lib install "XPT2046_Bitbang@2.0.4"` (pinned in README).
  - In `ui.cpp`, replace the `XPT2046_Touchscreen` include/objects with the bitbang driver:

```cpp
#include <XPT2046_Bitbang.h>
// pins unchanged (CYD-fixed): MOSI 32, MISO 39, CLK 25, CS 33
static XPT2046_Bitbang touch(32, 39, 25, 33);
```

  - In `uiBegin()`, remove the `touchSpi.begin(...)` / `touch.setRotation` VSPI setup and call `touch.begin();`.
  - In `touchCb`, replace the read with the bitbang API:

```cpp
static void touchCb(lv_indev_drv_t*, lv_indev_data_t* data) {
  TouchPoint p = touch.getTouch();
  if (p.zRaw > 200) {  // pressed
    data->point.x = map(p.x, 200, 3700, 0, 320);
    data->point.y = map(p.y, 240, 3800, 0, 240);
    data->state = LV_INDEV_STATE_PRESSED;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}
```

  (Calibration `map()` ranges are re-tuned on the bench in Task 8, same as base Task 9.) Document in README that touch is bitbang because SD owns VSPI.

- [ ] **Step 2: `store.h`**

```cpp
#pragma once
#include <Arduino.h>

struct Patient {
  String code;
  String name;
  uint16_t age;
};

// SD card on the CYD (VSPI). storeBegin mounts the card and loads /patients.csv.
bool storeBegin();                       // false if no card / mount failed
bool storeCardPresent();
int storePatientCount();
const Patient& storePatient(int i);      // 0..count-1; returns a static blank if out of range
bool storeAddPatient(const Patient& p);  // append to CSV + RAM; false on write failure
```

- [ ] **Step 3: `store.cpp`**

```cpp
#include "store.h"
#include <vector>
#include <SPI.h>
#include <SD.h>

#define SD_CS 5
#define SD_SCK 18
#define SD_MISO 19
#define SD_MOSI 23

static SPIClass sdSpi(VSPI);
static bool mounted = false;
static std::vector<Patient> patients;
static const Patient kBlank{"", "", 0};

static const char* PATIENTS = "/patients.csv";

static void parseLine(const String& line) {
  if (line.length() == 0) return;
  int first = line.indexOf(',');
  int last = line.lastIndexOf(',');
  if (first < 0 || last <= first) return;  // malformed -> skip
  Patient p;
  p.code = line.substring(0, first);
  p.name = line.substring(first + 1, last);
  p.age = (uint16_t)line.substring(last + 1).toInt();
  if (p.code == "code") return;  // header
  patients.push_back(p);
}

static void load() {
  patients.clear();
  File f = SD.open(PATIENTS, FILE_READ);
  if (!f) return;  // no file yet: first boot writes it on first add
  String line;
  while (f.available()) {
    char c = f.read();
    if (c == '\n') { parseLine(line); line = ""; }
    else if (c != '\r') line += c;
  }
  if (line.length()) parseLine(line);
  f.close();
}

bool storeBegin() {
  sdSpi.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  mounted = SD.begin(SD_CS, sdSpi);
  if (mounted) load();
  return mounted;
}

bool storeCardPresent() { return mounted; }
int storePatientCount() { return (int)patients.size(); }

const Patient& storePatient(int i) {
  if (i < 0 || i >= (int)patients.size()) return kBlank;
  return patients[i];
}

bool storeAddPatient(const Patient& p) {
  if (!mounted) return false;
  bool newFile = !SD.exists(PATIENTS);
  File f = SD.open(PATIENTS, FILE_APPEND);
  if (!f) return false;
  if (newFile) f.println("code,name,age");
  f.printf("%s,%s,%u\n", p.code.c_str(), p.name.c_str(), p.age);
  f.close();
  patients.push_back(p);
  return true;
}
```

- [ ] **Step 4: Compile display** — `arduino-cli compile ... firmware/pt-leg-display` → success. (`store` unused until Task 6; the touch-driver swap must still compile.)

- [ ] **Step 5: On-device check (if CYD on desk; else defer to Task 8)** — flash; serial-log `storeBegin()` result and `storePatientCount()`. Screen still draws and touch still responds (bus fix works). With a card holding a hand-written `/patients.csv`, the count matches.

- [ ] **Step 6: Commit**

```bash
git add firmware
git commit -m "feat(firmware): CYD SD mount + patient CSV store (touch to bitbang for VSPI)"
```

---

### Task 5: CYD — session log to SD (`sdlog`) with RAM-ring fallback

**Files:**
- Create: `firmware/pt-leg-display/sdlog.h`, `firmware/pt-leg-display/sdlog.cpp`

**Interfaces:**
- Consumes: `SD` (mounted by `storeBegin`, Task 4).
- Produces: `struct SessionRow { uint32_t epoch; uint16_t reps; uint32_t durationSec; bool fault; String patientCode, patientName; }`, `void sdlogEnsureHeader()`, `bool sdlogWrite(const SessionRow&)` (false on write failure), `void sdlogFlushRing()` (retry buffered rows), `int sdlogRingDepth()`. Consumed by Task 7.

- [ ] **Step 1: `sdlog.h`**

```cpp
#pragma once
#include <Arduino.h>

struct SessionRow {
  uint32_t epoch;        // UTC seconds, 0 = provisional
  uint16_t reps;
  uint32_t durationSec;
  bool fault;
  String patientCode;    // "-" if none
  String patientName;
};

void sdlogEnsureHeader();          // write /sessions.csv header if the file is new
bool sdlogWrite(const SessionRow&);// append one row; false on failure (caller may ring)
void sdlogRingPush(const SessionRow&);
void sdlogFlushRing();             // retry buffered rows to SD
int sdlogRingDepth();
```

- [ ] **Step 2: `sdlog.cpp`**

```cpp
#include "sdlog.h"
#include <SD.h>
#include <time.h>

#define TZ_OFFSET_SEC (7 * 3600)  // Thailand UTC+7, applied only for display formatting
#define SESSIONS "/sessions.csv"

// small in-RAM ring for rows that could not be written (SD absent / error)
static const int RING = 8;
static SessionRow ring[RING];
static int ringHead = 0, ringCount = 0;

static String isoLocal(uint32_t epochUtc) {
  if (epochUtc == 0) return "?";  // RTC never set -> provisional, greppable
  time_t t = (time_t)epochUtc + TZ_OFFSET_SEC;
  struct tm tmv;
  gmtime_r(&t, &tmv);
  char buf[24];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tmv);
  return String(buf);
}

void sdlogEnsureHeader() {
  if (SD.exists(SESSIONS)) return;
  File f = SD.open(SESSIONS, FILE_WRITE);
  if (!f) return;
  f.println("datetime,patientCode,patientName,durationSec,reps,fault");
  f.close();
}

bool sdlogWrite(const SessionRow& r) {
  sdlogEnsureHeader();
  File f = SD.open(SESSIONS, FILE_APPEND);
  if (!f) return false;
  f.printf("%s,%s,%s,%lu,%u,%u\n", isoLocal(r.epoch).c_str(), r.patientCode.c_str(),
           r.patientName.c_str(), (unsigned long)r.durationSec, r.reps, r.fault ? 1 : 0);
  f.close();
  return true;
}

void sdlogRingPush(const SessionRow& r) {
  ring[ringHead] = r;
  ringHead = (ringHead + 1) % RING;
  if (ringCount < RING) ringCount++;  // oldest overwritten past RING (logged loss)
}

void sdlogFlushRing() {
  while (ringCount > 0) {
    int idx = (ringHead - ringCount + RING) % RING;
    if (!sdlogWrite(ring[idx])) return;  // still failing: keep the ring for later
    ringCount--;
  }
}

int sdlogRingDepth() { return ringCount; }
```

- [ ] **Step 3: Compile display** → success (unused until Task 7).

- [ ] **Step 4: Commit**

```bash
git add firmware
git commit -m "feat(firmware): CYD session CSV logger with RAM-ring fallback"
```

---

### Task 6: CYD — patient data menu (list + add + set active)

**Files:**
- Modify: `firmware/pt-leg-display/ui.h`, `firmware/pt-leg-display/ui.cpp`

**Interfaces:**
- Consumes: `Phase` (existing), font_thai (base Task 8).
- Produces: `void uiSetDataCallbacks(int (*count)(), const Patient* (*get)(int), bool (*add)(const char*, const char*, int), void (*setActive)(int))`, `void uiOpenData()`, and a **"ข้อมูล"** button added to the idle main screen that calls a `cbData` handler set via `uiSetCallbacks` (extended). Consumed by Task 7.

- [ ] **Step 1: Extend `ui.h`** — add the include and declarations:

```cpp
#include "store.h"  // Patient

void uiSetDataCallbacks(int (*count)(), const Patient* (*get)(int),
                        bool (*add)(const char* code, const char* name, int age),
                        void (*setActive)(int));
void uiOpenData();
```

Change `uiSetCallbacks` to carry a fourth callback (the data button):

```cpp
void uiSetCallbacks(void (*onStart)(), void (*onStop)(), void (*onOpenSettings)(),
                    void (*onOpenData)());
```

- [ ] **Step 2: Data-menu state + callbacks in `ui.cpp`** — add file-scope:

```cpp
static int (*cbCount)() = nullptr;
static const Patient* (*cbGet)(int) = nullptr;
static bool (*cbAdd)(const char*, const char*, int) = nullptr;
static void (*cbSetActive)(int) = nullptr;
static void (*cbData)() = nullptr;
static lv_obj_t* scrData = nullptr;

void uiSetDataCallbacks(int (*count)(), const Patient* (*get)(int),
                        bool (*add)(const char*, const char*, int),
                        void (*setActive)(int)) {
  cbCount = count; cbGet = get; cbAdd = add; cbSetActive = setActive;
}
```

Update `uiSetCallbacks` to store `cbData = onOpenData;` (add the parameter and assignment).

- [ ] **Step 3: Add the data button to `buildMain()`** — after the gear button block:

```cpp
  lv_obj_t* dataBtn = lv_btn_create(scrMain);
  lv_obj_set_size(dataBtn, 44, 44);
  lv_obj_align(dataBtn, LV_ALIGN_TOP_RIGHT, -56, 6);  // left of the gear
  lv_obj_add_event_cb(dataBtn, [](lv_event_t*) { if (cbData) cbData(); },
                      LV_EVENT_CLICKED, nullptr);
  lv_obj_t* dl = lv_label_create(dataBtn);
  lv_label_set_text(dl, LV_SYMBOL_LIST);
  lv_obj_center(dl);
```

- [ ] **Step 4: Build the data screen in `ui.cpp`**

```cpp
static void addPatientDialog();  // fwd

static void patientRowClicked(lv_event_t* e) {
  int idx = (int)(intptr_t)lv_event_get_user_data(e);
  if (cbSetActive) cbSetActive(idx);
  lv_scr_load(scrMain);  // active set; back to main to start
}

void uiOpenData() {
  scrData = lv_obj_create(nullptr);
  lv_obj_t* col = lv_obj_create(scrData);
  lv_obj_set_size(col, lv_pct(100), lv_pct(100));
  lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(col, 6, 0);

  lv_obj_t* title = lv_label_create(col);
  lv_obj_set_style_text_font(title, &font_thai, 0);
  lv_label_set_text(title, cbCount && cbCount() >= 0 ? "ผู้ป่วย" : "ไม่พบการ์ด SD");

  lv_obj_t* list = lv_list_create(col);
  lv_obj_set_size(list, lv_pct(100), 150);
  int n = cbCount ? cbCount() : 0;
  for (int i = 0; i < n; i++) {
    const Patient* p = cbGet(i);
    lv_obj_t* b = lv_list_add_btn(list, LV_SYMBOL_RIGHT, "");
    lv_obj_t* lbl = lv_obj_get_child(b, -1);  // the text label of the list button
    lv_obj_set_style_text_font(lbl, &font_thai, 0);
    lv_label_set_text_fmt(lbl, "%s  (%s)", p->name.c_str(), p->code.c_str());
    lv_obj_add_event_cb(b, patientRowClicked, LV_EVENT_CLICKED, (void*)(intptr_t)i);
  }

  lv_obj_t* rowBtns = lv_obj_create(col);
  lv_obj_set_flex_flow(rowBtns, LV_FLEX_FLOW_ROW);
  lv_obj_set_size(rowBtns, lv_pct(100), LV_SIZE_CONTENT);

  lv_obj_t* addBtn = lv_btn_create(rowBtns);
  lv_obj_add_event_cb(addBtn, [](lv_event_t*) { addPatientDialog(); }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* al = lv_label_create(addBtn);
  lv_obj_set_style_text_font(al, &font_thai, 0);
  lv_label_set_text(al, "เพิ่มผู้ป่วย");
  lv_obj_center(al);

  lv_obj_t* backBtn = lv_btn_create(rowBtns);
  lv_obj_add_event_cb(backBtn, [](lv_event_t*) { lv_scr_load(scrMain); }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* bl = lv_label_create(backBtn);
  lv_obj_set_style_text_font(bl, &font_thai, 0);
  lv_label_set_text(bl, "กลับ");
  lv_obj_center(bl);

  lv_scr_load(scrData);
}
```

- [ ] **Step 5: Add-patient dialog** — appends via `cbAdd`, strips commas/newlines to keep the CSV simple:

```cpp
static lv_obj_t *dlgCode, *dlgName, *dlgAge, *dlgKb;

static String sanitize(const char* s) {
  String out;
  for (const char* p = s; *p; ++p)
    if (*p != ',' && *p != '\n' && *p != '\r') out += *p;
  return out;
}

static void addPatientDialog() {
  lv_obj_t* scr = lv_obj_create(nullptr);
  lv_obj_t* col = lv_obj_create(scr);
  lv_obj_set_size(col, lv_pct(100), lv_pct(100));
  lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(col, 6, 0);

  dlgCode = lv_textarea_create(col); lv_textarea_set_one_line(dlgCode, true);
  lv_textarea_set_placeholder_text(dlgCode, "รหัสผู้ป่วย (P001)");
  dlgName = lv_textarea_create(col); lv_textarea_set_one_line(dlgName, true);
  lv_textarea_set_placeholder_text(dlgName, "ชื่อ");
  lv_obj_set_style_text_font(dlgName, &font_thai, 0);
  dlgAge = lv_textarea_create(col); lv_textarea_set_one_line(dlgAge, true);
  lv_textarea_set_accepted_chars(dlgAge, "0123456789");
  lv_textarea_set_placeholder_text(dlgAge, "อายุ");

  auto focus = [](lv_event_t* e) {
    lv_keyboard_set_textarea(dlgKb, (lv_obj_t*)lv_event_get_target(e));
    lv_obj_clear_flag(dlgKb, LV_OBJ_FLAG_HIDDEN);
  };
  lv_obj_add_event_cb(dlgCode, focus, LV_EVENT_FOCUSED, nullptr);
  lv_obj_add_event_cb(dlgName, focus, LV_EVENT_FOCUSED, nullptr);
  lv_obj_add_event_cb(dlgAge, focus, LV_EVENT_FOCUSED, nullptr);

  lv_obj_t* save = lv_btn_create(col);
  lv_obj_add_event_cb(save, [](lv_event_t*) {
    if (cbAdd) cbAdd(sanitize(lv_textarea_get_text(dlgCode)).c_str(),
                     sanitize(lv_textarea_get_text(dlgName)).c_str(),
                     atoi(lv_textarea_get_text(dlgAge)));
    uiOpenData();  // rebuild the list with the new patient
  }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* sl = lv_label_create(save);
  lv_obj_set_style_text_font(sl, &font_thai, 0);
  lv_label_set_text(sl, "บันทึก");
  lv_obj_center(sl);

  dlgKb = lv_keyboard_create(scr);
  lv_obj_add_flag(dlgKb, LV_OBJ_FLAG_HIDDEN);
  lv_scr_load(scr);
}
```

- [ ] **Step 6: Compile display** → success.

- [ ] **Step 7: On-device check (if CYD; else Task 8)** — data button opens the list; add a patient; it appears and lands in `/patients.csv`; tapping a patient returns to main.

- [ ] **Step 8: Commit**

```bash
git add firmware
git commit -m "feat(firmware): CYD patient data menu (list, add, set active)"
```

---

### Task 7: CYD — wire active patient + finish screen + `done` logging

**Files:**
- Modify: `firmware/pt-leg-display/ui.h`, `firmware/pt-leg-display/ui.cpp`, `firmware/pt-leg-display/pt-leg-display.ino`

**Interfaces:**
- Consumes: `store*` (Task 4), `sdlog*` (Task 5), `Link::onDone`/`LinkDone` (Task 2), `uiSetDataCallbacks`/`uiOpenData` (Task 6).
- Produces: `void uiShowFinish(const char* patientName, uint32_t durationSec, uint16_t reps, bool fault, bool saved)`.

- [ ] **Step 1: Declare + build the finish screen in `ui.{h,cpp}`** — add to `ui.h`:

```cpp
void uiShowFinish(const char* patientName, uint32_t durationSec, uint16_t reps,
                  bool fault, bool saved);
```

In `ui.cpp`:

```cpp
void uiShowFinish(const char* patientName, uint32_t durationSec, uint16_t reps,
                  bool fault, bool saved) {
  lv_obj_t* scr = lv_obj_create(nullptr);
  lv_obj_t* col = lv_obj_create(scr);
  lv_obj_set_size(col, lv_pct(100), lv_pct(100));
  lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t* who = lv_label_create(col);
  lv_obj_set_style_text_font(who, &font_thai, 0);
  lv_label_set_text_fmt(who, "%s", (patientName && patientName[0]) ? patientName : "ไม่ระบุ");

  lv_obj_t* stat = lv_label_create(col);
  lv_obj_set_style_text_font(stat, &lv_font_montserrat_28, 0);
  lv_label_set_text_fmt(stat, "%02lu:%02lu   x%u", (unsigned long)(durationSec / 60),
                        (unsigned long)(durationSec % 60), reps);

  if (fault) {
    lv_obj_t* fl = lv_label_create(col);
    lv_obj_set_style_text_font(fl, &font_thai, 0);
    lv_obj_set_style_text_color(fl, lv_color_hex(0xb00020), 0);
    lv_label_set_text(fl, "ตรวจสอบสวิตช์");
  }

  lv_obj_t* saved_lbl = lv_label_create(col);
  lv_obj_set_style_text_font(saved_lbl, &font_thai, 0);
  lv_label_set_text(saved_lbl, saved ? "บันทึกแล้ว" : "บันทึกไม่สำเร็จ");

  lv_obj_t* ok = lv_btn_create(col);
  lv_obj_add_event_cb(ok, [](lv_event_t*) { lv_scr_load(scrMain); }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* ol = lv_label_create(ok);
  lv_obj_set_style_text_font(ol, &font_thai, 0);
  lv_label_set_text(ol, "ตกลง");
  lv_obj_center(ol);

  lv_scr_load(scr);
}
```

- [ ] **Step 2: Active-patient + store/sdlog wiring in `pt-leg-display.ino`** — add includes and state:

```cpp
#include "store.h"
#include "sdlog.h"

static int gActivePatient = -1;  // index into store; -1 = none

static int dataCount() { return storeCardPresent() ? storePatientCount() : -1; }  // -1 = no card
static const Patient* dataGet(int i) { return &storePatient(i); }
static bool dataAdd(const char* code, const char* name, int age) {
  Patient p{String(code), String(name), (uint16_t)age};
  return storeAddPatient(p);
}
static void dataSetActive(int i) { gActivePatient = i; }
static void onOpenData() { uiOpenData(); }
```

- [ ] **Step 3: Log on `done`** — add the handler and register it:

```cpp
static void onLinkDone(const LinkDone& d) {
  SessionRow r;
  r.epoch = d.epoch;
  r.reps = d.reps;
  r.durationSec = d.elapsed;
  r.fault = d.fault;
  if (gActivePatient >= 0 && gActivePatient < storePatientCount()) {
    const Patient& p = storePatient(gActivePatient);
    r.patientCode = p.code;
    r.patientName = p.name;
  } else {
    r.patientCode = "-";
    r.patientName = "";
  }
  bool saved = storeCardPresent() && sdlogWrite(r);
  if (!saved) sdlogRingPush(r);
  uiShowFinish(r.patientName.c_str(), r.durationSec, r.reps, r.fault, saved);
}
```

- [ ] **Step 4: Register callbacks + mount in `setup()`** — in `pt-leg-display.ino` `setup()`, after `uiBegin();`:

```cpp
  storeBegin();
  sdlogEnsureHeader();
  uiSetDataCallbacks(dataCount, dataGet, dataAdd, dataSetActive);
  gLink.onDone = onLinkDone;
```

Update the existing `uiSetCallbacks(...)` call to pass the new fourth arg:

```cpp
  uiSetCallbacks(onTouchStart, onTouchStop, onOpenSettings, onOpenData);
```

- [ ] **Step 5: Flush the ring opportunistically** — in `loop()`, add a slow retry:

```cpp
  static uint32_t lastFlush = 0;
  if (sdlogRingDepth() > 0 && now - lastFlush > 5000) {
    lastFlush = now;
    if (!storeCardPresent()) storeBegin();  // try to (re)mount an inserted card
    sdlogFlushRing();
  }
```

- [ ] **Step 6: Compile display** → success.

- [ ] **Step 7: Commit**

```bash
git add firmware
git commit -m "feat(firmware): CYD logs sessions on done + finish screen with active patient"
```

---

### Task 8: Two-board hardware integration (SD + RTC + logging)

**Files:** none (verification; fixups committed as found)

Prereq: base-firmware Task 13 passed; DS3231 wired to controller (SDA 21 / SCL 25, coin cell in); microSD card in the CYD; both flashed.

- [ ] **Step 1: SD bring-up** — CYD boots; screen draws and touch works (VSPI/bitbang fix good); `/patients.csv` + `/sessions.csv` created with headers on first add/session; read the card on a PC to confirm.

- [ ] **Step 2: Add + pick patient** — add two patients (one Thai name, one Latin) on the CYD; both show in the list and in `/patients.csv`; tapping one sets it active (returns to main).

- [ ] **Step 3: Offline session log** — WiFi off, no phone. Pick a patient, run a full session, stop. Finish screen shows the right patient/duration/reps + "บันทึกแล้ว"; `/sessions.csv` gains a correct row (real datetime if the unit was NTP-set earlier this power-cycle-set of the RTC).

- [ ] **Step 4: No-patient session** — start without picking; row logs `-`/empty name; finish screen shows "ไม่ระบุ".

- [ ] **Step 5: Fault logging** — trigger a limit-switch FAULT mid-session; row logs `fault=1`; finish screen shows "ตรวจสอบสวิตช์".

- [ ] **Step 6: RTC persistence** — bring the controller online once (NTP sets DS3231), then power-cycle both boards with WiFi off; a new offline session still logs the correct current date. Disconnect the RTC → rows log datetime `?`; machine still runs.

- [ ] **Step 7: SD absent** — boot the CYD with no card; machine runs; data menu shows "ไม่พบการ์ด SD"; run a session (ring-buffered), insert a card → within ~5 s the buffered row flushes to `/sessions.csv`.

- [ ] **Step 8: Parity** — an online device session still produces its server record as before (`cd server; .venv\Scripts\python -m pytest` green), independent of the SD row.

- [ ] **Step 9: Touch recalibration if needed** — retune `touchCb` `map()` ranges for the bitbang driver; re-flash.

- [ ] **Step 10: Commit fixups**

```bash
git add firmware
git commit -m "fix(firmware): SD + RTC + logging integration fixups"
```
