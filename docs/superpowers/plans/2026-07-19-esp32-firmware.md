# ESP32 Firmware Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Firmware for the CYD (ESP32-2432S028R) therapy device: relay-driven pneumatic motion cycle, LVGL touch UI, v3 server contract client.

**Architecture:** Modular Arduino sketch in `firmware/pt-leg-device/` — pure-C++ session state machine, relay actuator module with safety invariants, non-blocking net client, LVGL UI. Cooperative `millis()` loop, no extra RTOS tasks. Spec: `docs/superpowers/specs/2026-07-19-esp32-firmware-design.md`.

**Tech Stack:** Arduino core for ESP32 (via arduino-cli for scripted builds; Arduino IDE opens the same sketch), lvgl 8.4.x, TFT_eSPI, XPT2046_Touchscreen, ArduinoJson 7.x.

## Global Constraints

- v3 server contract is FROZEN (`2026-07-13-server-mediated-device-control-design.md`): poll 1 s, heartbeat 1 s running / 5 s idle, event on every transition, consume-on-read commands.
- Safety invariants (spec): UP+DOWN relays never both energized; all relays OFF at boot before anything else; stop = safe-lower first.
- Relays: UP = GPIO 22, DOWN = GPIO 27, active-HIGH unless `RELAY_ACTIVE_LOW` defined.
- `session.{h,cpp}` must stay Arduino-free (only `stdint.h`) — host-testable later.
- Compile check after every task: `arduino-cli compile --fqbn esp32:esp32:esp32 firmware/pt-leg-device` (run from repo root) must succeed.
- Commit after every task. No AI attribution lines in commits.
- Library versions pinned in `firmware/README.md` (Arduino IDE has no manifest).

---

### Task 1: Toolchain bootstrap + empty sketch compiles

**Files:**
- Create: `firmware/pt-leg-device/pt-leg-device.ino`, `firmware/README.md`

- [ ] **Step 1: Install arduino-cli + ESP32 core**

```powershell
winget install ArduinoSA.CLI
arduino-cli config init
arduino-cli config add board_manager.additional_urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli core update-index
arduino-cli core install esp32:esp32
```

Expected: `arduino-cli core list` shows `esp32:esp32`.

- [ ] **Step 2: Install libraries (exact versions)**

```powershell
arduino-cli lib install "lvgl@8.4.0" "TFT_eSPI@2.5.43" "XPT2046_Touchscreen@1.4" "ArduinoJson@7.4.1"
```

- [ ] **Step 3: Minimal sketch** — `firmware/pt-leg-device/pt-leg-device.ino`:

```cpp
void setup() {
  Serial.begin(115200);
  Serial.println("pt-leg-device boot");
}

void loop() {}
```

- [ ] **Step 4: `firmware/README.md`**

```markdown
# PT Leg Device Firmware

Board: ESP32-2432S028R "Cheap Yellow Display" (esp32:esp32:esp32).
Spec: ../docs/superpowers/specs/2026-07-19-esp32-firmware-design.md

## Pinned libraries (install these exact versions)
- lvgl 8.4.0 (lv_conf.h: see Task 6 / copy from this repo)
- TFT_eSPI 2.5.43 (User_Setup.h for CYD: firmware/TFT_eSPI_User_Setup.h — copy over the library's User_Setup.h)
- XPT2046_Touchscreen 1.4
- ArduinoJson 7.4.1

## Build
arduino-cli compile --fqbn esp32:esp32:esp32 firmware/pt-leg-device
## Flash
arduino-cli upload -p COMx --fqbn esp32:esp32:esp32 firmware/pt-leg-device
```

- [ ] **Step 5: Compile**

Run: `arduino-cli compile --fqbn esp32:esp32:esp32 firmware/pt-leg-device`
Expected: `Sketch uses ... bytes` success output.

- [ ] **Step 6: Commit**

```bash
git add firmware
git commit -m "feat(firmware): toolchain bootstrap, empty sketch compiles"
```

---

### Task 2: config.h + NVS settings module

**Files:**
- Create: `firmware/pt-leg-device/config.h`, `firmware/pt-leg-device/settings.h`, `firmware/pt-leg-device/settings.cpp`

**Interfaces:**
- Produces: `struct DeviceSettings { String wifiSsid, wifiPass, serverUrl; uint16_t liftSec, holdSec, lowerSec, restSec; }`, `void settingsLoad(DeviceSettings&)`, `void settingsSave(const DeviceSettings&)`; pin macros `PIN_RELAY_UP`, `PIN_RELAY_DOWN`.

- [ ] **Step 1: `config.h`**

```cpp
#pragma once

#define FW_VERSION "0.1.0"

#define PIN_RELAY_UP 22   // P3 connector
#define PIN_RELAY_DOWN 27 // CN1 connector
// #define RELAY_ACTIVE_LOW  // uncomment if relay board is active-low

// cycle defaults (seconds) — user-adjustable in settings UI
#define DEFAULT_LIFT_SEC 3
#define DEFAULT_HOLD_SEC 10
#define DEFAULT_LOWER_SEC 3
#define DEFAULT_REST_SEC 10
```

- [ ] **Step 2: `settings.h`**

```cpp
#pragma once
#include <Arduino.h>

struct DeviceSettings {
  String wifiSsid;
  String wifiPass;
  String serverUrl; // e.g. http://192.168.0.12:8000
  uint16_t liftSec, holdSec, lowerSec, restSec;
};

void settingsLoad(DeviceSettings& s);
void settingsSave(const DeviceSettings& s);
```

- [ ] **Step 3: `settings.cpp`**

```cpp
#include "settings.h"
#include <Preferences.h>
#include "config.h"

static const char* NS = "ptleg";

void settingsLoad(DeviceSettings& s) {
  Preferences p;
  p.begin(NS, true);
  s.wifiSsid = p.getString("ssid", "");
  s.wifiPass = p.getString("pass", "");
  s.serverUrl = p.getString("server", "");
  s.liftSec = p.getUShort("lift", DEFAULT_LIFT_SEC);
  s.holdSec = p.getUShort("hold", DEFAULT_HOLD_SEC);
  s.lowerSec = p.getUShort("lower", DEFAULT_LOWER_SEC);
  s.restSec = p.getUShort("rest", DEFAULT_REST_SEC);
  p.end();
}

void settingsSave(const DeviceSettings& s) {
  Preferences p;
  p.begin(NS, false);
  p.putString("ssid", s.wifiSsid);
  p.putString("pass", s.wifiPass);
  p.putString("server", s.serverUrl);
  p.putUShort("lift", s.liftSec);
  p.putUShort("hold", s.holdSec);
  p.putUShort("lower", s.lowerSec);
  p.putUShort("rest", s.restSec);
  p.end();
}
```

- [ ] **Step 4: Wire into `.ino` (load + log)** — replace sketch body:

```cpp
#include "config.h"
#include "settings.h"

DeviceSettings gSettings;

void setup() {
  Serial.begin(115200);
  settingsLoad(gSettings);
  Serial.printf("pt-leg-device %s ssid=%s server=%s cycle=%u/%u/%u/%u\n", FW_VERSION,
                gSettings.wifiSsid.c_str(), gSettings.serverUrl.c_str(), gSettings.liftSec,
                gSettings.holdSec, gSettings.lowerSec, gSettings.restSec);
}

void loop() {}
```

- [ ] **Step 5: Compile** — `arduino-cli compile --fqbn esp32:esp32:esp32 firmware/pt-leg-device` → success.

- [ ] **Step 6: Commit**

```bash
git add firmware
git commit -m "feat(firmware): config pins + NVS settings module"
```

---

### Task 3: Actuator module (relays + safety invariants)

**Files:**
- Create: `firmware/pt-leg-device/actuator.h`, `firmware/pt-leg-device/actuator.cpp`
- Modify: `firmware/pt-leg-device/pt-leg-device.ino`

**Interfaces:**
- Produces: `void actuatorBegin()`, `void actuatorUp()`, `void actuatorDown()`, `void actuatorOff()`, `ActState actuatorState()` with `enum ActState { ACT_OFF, ACT_UP, ACT_DOWN }`.

- [ ] **Step 1: `actuator.h`**

```cpp
#pragma once

enum ActState { ACT_OFF, ACT_UP, ACT_DOWN };

// Safety invariants enforced here and nowhere else:
// - UP and DOWN are never energized at the same time (break-before-make).
// - actuatorBegin() forces both relays off and must be the first hardware
//   call in setup().
void actuatorBegin();
void actuatorUp();
void actuatorDown();
void actuatorOff();
ActState actuatorState();
```

- [ ] **Step 2: `actuator.cpp`**

```cpp
#include "actuator.h"
#include <Arduino.h>
#include "config.h"

#ifdef RELAY_ACTIVE_LOW
static const int ON = LOW, OFF = HIGH;
#else
static const int ON = HIGH, OFF = LOW;
#endif

static ActState state = ACT_OFF;

static void write(int up, int down) {
  // break-before-make: always de-energize both before energizing one
  digitalWrite(PIN_RELAY_UP, OFF);
  digitalWrite(PIN_RELAY_DOWN, OFF);
  if (up == ON) digitalWrite(PIN_RELAY_UP, ON);
  if (down == ON) digitalWrite(PIN_RELAY_DOWN, ON);
}

void actuatorBegin() {
  pinMode(PIN_RELAY_UP, OUTPUT);
  pinMode(PIN_RELAY_DOWN, OUTPUT);
  write(OFF, OFF);
  state = ACT_OFF;
}

void actuatorUp() {
  write(ON, OFF);
  state = ACT_UP;
}

void actuatorDown() {
  write(OFF, ON);
  state = ACT_DOWN;
}

void actuatorOff() {
  write(OFF, OFF);
  state = ACT_OFF;
}

ActState actuatorState() { return state; }
```

- [ ] **Step 3: Call first in `setup()`** — in `.ino`, first lines of `setup()` (before `Serial.begin`):

```cpp
  actuatorBegin(); // safety: relays off before anything else
```

with `#include "actuator.h"` at top.

- [ ] **Step 4: Compile** → success.

- [ ] **Step 5: Bench check (when hardware present, else defer to Task 9)** — flash, measure GPIO 22/27 at boot: both inactive level. Serial monitor shows boot line.

- [ ] **Step 6: Commit**

```bash
git add firmware
git commit -m "feat(firmware): relay actuator with break-before-make safety"
```

---

### Task 4: Session state machine (pure C++)

**Files:**
- Create: `firmware/pt-leg-device/session.h`, `firmware/pt-leg-device/session.cpp`
- Modify: `firmware/pt-leg-device/pt-leg-device.ino`

**Interfaces:**
- Produces:

```cpp
enum class Phase { Idle, Lift, Hold, Lower, Rest, SafeLower };
struct CycleCfg { uint32_t liftMs, holdMs, lowerMs, restMs; };
class SessionSM {
 public:
  CycleCfg cfg;
  void start(uint32_t nowMs);       // Idle -> Lift; no-op if running
  void requestStop(uint32_t nowMs); // running -> SafeLower (or straight off from Lower/Rest)
  void tick(uint32_t nowMs);        // advance phases
  bool running() const;             // true unless Idle
  Phase phase() const;
  uint32_t elapsedSec(uint32_t nowMs) const; // 0 when Idle
  uint16_t reps() const;
  bool takeStartedEdge(); // true once per started transition (event trigger)
  bool takeStoppedEdge(); // true once per stopped transition, AFTER safe-lower
  uint32_t finalElapsedSec() const; // valid when stopped edge fires
  uint16_t finalReps() const;
};
```

- Consumes: nothing Arduino — caller passes `millis()`.
- Caller maps phase → actuator: `Lift`→up, `Lower`/`SafeLower`→down, else off.

- [ ] **Step 1: `session.h`** — exactly the interface above plus private members:

```cpp
#pragma once
#include <stdint.h>

enum class Phase { Idle, Lift, Hold, Lower, Rest, SafeLower };

struct CycleCfg {
  uint32_t liftMs, holdMs, lowerMs, restMs;
};

class SessionSM {
 public:
  CycleCfg cfg{3000, 10000, 3000, 10000};
  void start(uint32_t nowMs);
  void requestStop(uint32_t nowMs);
  void tick(uint32_t nowMs);
  bool running() const { return ph != Phase::Idle; }
  Phase phase() const { return ph; }
  uint32_t elapsedSec(uint32_t nowMs) const;
  uint16_t reps() const { return repCount; }
  bool takeStartedEdge();
  bool takeStoppedEdge();
  uint32_t finalElapsedSec() const { return finalElapsed; }
  uint16_t finalReps() const { return finalRepCount; }

 private:
  void enter(Phase p, uint32_t nowMs);
  Phase ph = Phase::Idle;
  uint32_t phaseStart = 0, sessionStart = 0;
  uint16_t repCount = 0;
  uint32_t finalElapsed = 0;
  uint16_t finalRepCount = 0;
  bool startedEdge = false, stoppedEdge = false;
};
```

- [ ] **Step 2: `session.cpp`**

```cpp
#include "session.h"

void SessionSM::enter(Phase p, uint32_t nowMs) {
  ph = p;
  phaseStart = nowMs;
}

void SessionSM::start(uint32_t nowMs) {
  if (running()) return;
  sessionStart = nowMs;
  repCount = 0;
  startedEdge = true;
  enter(Phase::Lift, nowMs);
}

void SessionSM::requestStop(uint32_t nowMs) {
  switch (ph) {
    case Phase::Idle:
      return;
    case Phase::Lift:
    case Phase::Hold:
      enter(Phase::SafeLower, nowMs); // leg is up: lower before stopping
      return;
    case Phase::Lower:
      enter(Phase::SafeLower, nowMs); // keep lowering, then stop
      return;
    case Phase::Rest:
    case Phase::SafeLower:
      // Rest: leg already down -> stop immediately via SafeLower with 0 wait
      finalElapsed = elapsedSec(nowMs);
      finalRepCount = repCount;
      stoppedEdge = true;
      enter(Phase::Idle, nowMs);
      return;
  }
}

void SessionSM::tick(uint32_t nowMs) {
  uint32_t dt = nowMs - phaseStart;
  switch (ph) {
    case Phase::Idle:
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
      if (dt >= cfg.restMs) {
        repCount++;
        enter(Phase::Lift, nowMs);
      }
      return;
    case Phase::SafeLower:
      if (dt >= cfg.lowerMs) {
        finalElapsed = elapsedSec(nowMs);
        finalRepCount = repCount;
        stoppedEdge = true;
        enter(Phase::Idle, nowMs);
      }
      return;
  }
}

uint32_t SessionSM::elapsedSec(uint32_t nowMs) const {
  if (ph == Phase::Idle) return 0;
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

- [ ] **Step 3: Drive actuator from phase in `.ino`** — replace sketch with the control core (UI/net come later):

```cpp
#include "config.h"
#include "settings.h"
#include "actuator.h"
#include "session.h"

DeviceSettings gSettings;
SessionSM gSession;

static void applyActuator() {
  switch (gSession.phase()) {
    case Phase::Lift:
      actuatorUp();
      break;
    case Phase::Lower:
    case Phase::SafeLower:
      actuatorDown();
      break;
    default:
      actuatorOff();
      break;
  }
}

void setup() {
  actuatorBegin(); // safety: relays off before anything else
  Serial.begin(115200);
  settingsLoad(gSettings);
  gSession.cfg = {gSettings.liftSec * 1000u, gSettings.holdSec * 1000u,
                  gSettings.lowerSec * 1000u, gSettings.restSec * 1000u};
  Serial.printf("pt-leg-device %s\n", FW_VERSION);
}

void loop() {
  uint32_t now = millis();
  gSession.tick(now);
  applyActuator();

  // temporary serial control until UI/net land: 's' start, 'x' stop
  if (Serial.available()) {
    char c = Serial.read();
    if (c == 's') gSession.start(now);
    if (c == 'x') gSession.requestStop(now);
  }
  if (gSession.takeStartedEdge()) Serial.println("EVENT started");
  if (gSession.takeStoppedEdge())
    Serial.printf("EVENT stopped elapsed=%lu reps=%u\n",
                  (unsigned long)gSession.finalElapsedSec(), gSession.finalReps());
  delay(5);
}
```

- [ ] **Step 4: Compile** → success.

- [ ] **Step 5: On-device check (if board on desk; else defer to Task 9)** — serial `s`: relays click through lift(3 s)/hold(10 s)/lower(3 s)/rest(10 s) loop; `x` mid-hold: DOWN energizes ~3 s then all off, `EVENT stopped` printed.

- [ ] **Step 6: Commit**

```bash
git add firmware
git commit -m "feat(firmware): motion session state machine with safe-lower stop"
```

---

### Task 5: Net module (Wi-Fi + v3 server client)

**Files:**
- Create: `firmware/pt-leg-device/net.h`, `firmware/pt-leg-device/net.cpp`
- Modify: `firmware/pt-leg-device/pt-leg-device.ino`

**Interfaces:**
- Produces:

```cpp
void netBegin(const DeviceSettings& s);      // starts Wi-Fi (non-blocking)
void netLoop(uint32_t nowMs);                // reconnect + retry pending event
bool netWifiUp();
bool netServerUp();                          // last HTTP call succeeded
String netPollCommand();                     // "start" | "stop" | "" ; call at 1 s cadence
void netQueueEvent(const char* type, uint32_t elapsedSec, uint16_t reps); // one-slot buffer, retried until 2xx
void netHeartbeat(const char* state, uint32_t elapsedSec, uint16_t reps); // fire-and-forget
```

- Consumes: `DeviceSettings` from Task 2.

- [ ] **Step 1: `net.h`** — interface above with `#pragma once`, `#include <Arduino.h>`, `#include "settings.h"`.

- [ ] **Step 2: `net.cpp`**

```cpp
#include "net.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

static String baseUrl;
static bool serverOk = false;
static uint32_t lastReconnect = 0;

// one-slot pending event (matches server's one-slot command model)
static bool evPending = false;
static String evType;
static uint32_t evElapsed = 0;
static uint16_t evReps = 0;

void netBegin(const DeviceSettings& s) {
  baseUrl = s.serverUrl;
  if (s.wifiSsid.length()) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(s.wifiSsid.c_str(), s.wifiPass.c_str());
  }
}

bool netWifiUp() { return WiFi.status() == WL_CONNECTED; }
bool netServerUp() { return serverOk; }

static bool post(const String& path, const String& body) {
  if (!netWifiUp() || !baseUrl.length()) return false;
  HTTPClient http;
  http.setConnectTimeout(800);
  http.setTimeout(800);
  http.begin(baseUrl + path);
  http.addHeader("Content-Type", "application/json");
  int code = http.POST(body);
  http.end();
  serverOk = code >= 200 && code < 300;
  return serverOk;
}

String netPollCommand() {
  if (!netWifiUp() || !baseUrl.length()) return "";
  HTTPClient http;
  http.setConnectTimeout(800);
  http.setTimeout(800);
  http.begin(baseUrl + "/api/device/command");
  int code = http.GET();
  String cmd = "";
  if (code == 200) {
    JsonDocument doc;
    if (deserializeJson(doc, http.getString()) == DeserializationError::Ok &&
        !doc["command"].isNull())
      cmd = doc["command"].as<String>();
  }
  http.end();
  serverOk = code >= 200 && code < 300;
  return cmd;
}

void netQueueEvent(const char* type, uint32_t elapsedSec, uint16_t reps) {
  evPending = true; // overwrite: newest transition wins the single slot
  evType = type;
  evElapsed = elapsedSec;
  evReps = reps;
}

static void tryFlushEvent() {
  if (!evPending) return;
  String body = String("{\"type\":\"") + evType + "\",\"elapsedSec\":" + evElapsed +
                ",\"reps\":" + evReps + "}";
  if (post("/api/device/event", body)) evPending = false;
}

void netHeartbeat(const char* state, uint32_t elapsedSec, uint16_t reps) {
  String body = String("{\"state\":\"") + state + "\",\"elapsedSec\":" + elapsedSec +
                ",\"reps\":" + reps + "}";
  post("/api/device/heartbeat", body);
}

void netLoop(uint32_t nowMs) {
  if (!netWifiUp() && WiFi.getMode() == WIFI_STA && nowMs - lastReconnect > 10000) {
    lastReconnect = nowMs;
    WiFi.reconnect();
  }
  tryFlushEvent();
}
```

- [ ] **Step 3: Wire scheduler in `.ino`** — replace the serial-control block of `loop()` and extend `setup()`:

```cpp
#include "net.h"

static uint32_t lastPoll = 0, lastHb = 0;

// in setup(), after settingsLoad + gSession.cfg:
  netBegin(gSettings);

// in loop(), replacing the temporary Serial control block:
  netLoop(now);

  if (now - lastPoll >= 1000) {
    lastPoll = now;
    String cmd = netPollCommand();
    if (cmd == "start") gSession.start(now);
    else if (cmd == "stop") gSession.requestStop(now);
  }

  uint32_t hbInterval = gSession.running() ? 1000 : 5000;
  if (now - lastHb >= hbInterval) {
    lastHb = now;
    netHeartbeat(gSession.running() ? "running" : "idle", gSession.elapsedSec(now),
                 gSession.reps());
  }

  if (gSession.takeStartedEdge()) netQueueEvent("started", gSession.elapsedSec(now), gSession.reps());
  if (gSession.takeStoppedEdge())
    netQueueEvent("stopped", gSession.finalElapsedSec(), gSession.finalReps());
```

(Keep the serial `s`/`x` control too — it becomes the bench stand-in for the touch button until Task 7.)

- [ ] **Step 4: Compile** → success.

- [ ] **Step 5: Live check against local server (board + Wi-Fi creds needed; else defer to Task 9)** — temporarily hardcode ssid/pass/server in NVS via serial or a one-off `settingsSave` call; run `server\scripts\dev.ps1 -NoMock`; phone app start → relays run; serial `x` → app returns to after-phase; server `GET /api/sessions?patientCode=` shows session.

- [ ] **Step 6: Commit**

```bash
git add firmware
git commit -m "feat(firmware): wifi + v3 server client with event retry"
```

---

### Task 6: LVGL bring-up (display + touch + lv_conf)

**Files:**
- Create: `firmware/TFT_eSPI_User_Setup.h` (CYD display config, copied over the TFT_eSPI library's `User_Setup.h`)
- Create: `firmware/lv_conf.h` (copied to Arduino libraries dir root, next to the `lvgl` folder)
- Create: `firmware/pt-leg-device/ui.h`, `firmware/pt-leg-device/ui.cpp` (init + blank screen)
- Modify: `firmware/pt-leg-device/pt-leg-device.ino`, `firmware/README.md`

**Interfaces:**
- Produces: `void uiBegin()`, `void uiLoop()` (calls `lv_timer_handler`). Task 7 adds the real screens on top.

- [ ] **Step 1: `firmware/TFT_eSPI_User_Setup.h`** — standard CYD config:

```cpp
// ESP32-2432S028R (CYD) — copy over <ArduinoLibraries>/TFT_eSPI/User_Setup.h
#define ILI9341_2_DRIVER
#define TFT_WIDTH 240
#define TFT_HEIGHT 320
#define TFT_MISO 12
#define TFT_MOSI 13
#define TFT_SCLK 14
#define TFT_CS 15
#define TFT_DC 2
#define TFT_RST -1
#define TFT_BL 21
#define TFT_BACKLIGHT_ON HIGH
#define TFT_INVERSION_OFF
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_GFXFF
#define SMOOTH_FONT
#define SPI_FREQUENCY 55000000
#define SPI_READ_FREQUENCY 20000000
#define SPI_TOUCH_FREQUENCY 2500000
```

Copy it: `Copy-Item firmware/TFT_eSPI_User_Setup.h "$env:USERPROFILE\Documents\Arduino\libraries\TFT_eSPI\User_Setup.h"` (document in README).

Note: CYD touch is on a SEPARATE SPI bus (XPT2046: CLK 25, MISO 39, MOSI 32, CS 33, IRQ 36) — handled by `XPT2046_Touchscreen` directly in `ui.cpp`, NOT via TFT_eSPI touch support.

- [ ] **Step 2: `firmware/lv_conf.h`** — copy `lv_conf_template.h` from lvgl 8.4.0 and set exactly these (leave the rest at template defaults):

```
#if 1                      (enable the file — first #if 0 → #if 1)
#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 1
#define LV_MEM_SIZE (48U * 1024U)
#define LV_TICK_CUSTOM 1   (uses millis())
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_48 1
```

Copy to `"$env:USERPROFILE\Documents\Arduino\libraries\lv_conf.h"` (sibling of the `lvgl` folder). Document both copies in `firmware/README.md` setup section.

- [ ] **Step 3: `ui.h`**

```cpp
#pragma once
void uiBegin();
void uiLoop();
```

- [ ] **Step 4: `ui.cpp`** — display + touch glue, blank LVGL screen:

```cpp
#include "ui.h"
#include <lvgl.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>

// CYD touch pins (separate SPI bus)
#define XPT2046_IRQ 36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK 25
#define XPT2046_CS 33

static TFT_eSPI tft;
static SPIClass touchSpi(VSPI);
static XPT2046_Touchscreen touch(XPT2046_CS, XPT2046_IRQ);

static lv_disp_draw_buf_t drawBuf;
static lv_color_t buf[240 * 40];

static void flushCb(lv_disp_drv_t* disp, const lv_area_t* area, lv_color_t* pixels) {
  uint32_t w = area->x2 - area->x1 + 1, h = area->y2 - area->y1 + 1;
  tft.startWrite();
  tft.setAddrWindow(area->x1, area->y1, w, h);
  tft.pushColors((uint16_t*)&pixels->full, w * h, true);
  tft.endWrite();
  lv_disp_flush_ready(disp);
}

static void touchCb(lv_indev_drv_t*, lv_indev_data_t* data) {
  if (touch.tirqTouched() && touch.touched()) {
    TS_Point p = touch.getPoint();
    // raw calibration for CYD landscape; tune in Task 9 if offset
    data->point.x = map(p.x, 200, 3700, 0, 320);
    data->point.y = map(p.y, 240, 3800, 0, 240);
    data->state = LV_INDEV_STATE_PRESSED;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

void uiBegin() {
  tft.init();
  tft.setRotation(1); // landscape 320x240
  touchSpi.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  touch.begin(touchSpi);
  touch.setRotation(1);

  lv_init();
  lv_disp_draw_buf_init(&drawBuf, buf, nullptr, 240 * 40);

  static lv_disp_drv_t dispDrv;
  lv_disp_drv_init(&dispDrv);
  dispDrv.hor_res = 320;
  dispDrv.ver_res = 240;
  dispDrv.flush_cb = flushCb;
  dispDrv.draw_buf = &drawBuf;
  lv_disp_drv_register(&dispDrv);

  static lv_indev_drv_t indevDrv;
  lv_indev_drv_init(&indevDrv);
  indevDrv.type = LV_INDEV_TYPE_POINTER;
  indevDrv.read_cb = touchCb;
  lv_indev_drv_register(&indevDrv);
}

void uiLoop() { lv_timer_handler(); }
```

- [ ] **Step 5: Wire into `.ino`** — `#include "ui.h"`; in `setup()` after `netBegin`: `uiBegin();`; in `loop()` start: `uiLoop();`.

- [ ] **Step 6: Compile** → success. On device (else defer): backlit blank LVGL screen, no boot loop.

- [ ] **Step 7: Commit**

```bash
git add firmware
git commit -m "feat(firmware): lvgl display+touch bring-up on CYD"
```

---

### Task 7: Main screen (timer, reps, start/stop, status)

**Files:**
- Modify: `firmware/pt-leg-device/ui.h`, `firmware/pt-leg-device/ui.cpp`, `firmware/pt-leg-device/pt-leg-device.ino`

**Interfaces:**
- Produces: `void uiSetCallbacks(void (*onStart)(), void (*onStop)(), void (*onOpenSettings)())`, `void uiUpdateMain(bool running, Phase phase, uint32_t elapsedSec, uint16_t reps, bool wifiUp, bool serverUp)`.
- Consumes: `Phase` from `session.h`.

- [ ] **Step 1: Extend `ui.h`**

```cpp
#pragma once
#include <stdint.h>
#include "session.h"

void uiBegin();
void uiLoop();
void uiSetCallbacks(void (*onStart)(), void (*onStop)(), void (*onOpenSettings)());
void uiUpdateMain(bool running, Phase phase, uint32_t elapsedSec, uint16_t reps,
                  bool wifiUp, bool serverUp);
```

- [ ] **Step 2: Main screen in `ui.cpp`** — add below bring-up code:

```cpp
static lv_obj_t *scrMain, *lblClock, *lblReps, *lblPhase, *lblStatus, *btnGo, *lblGo;
static void (*cbStart)() = nullptr;
static void (*cbStop)() = nullptr;
static void (*cbSettings)() = nullptr;
static bool uiRunning = false;

void uiSetCallbacks(void (*onStart)(), void (*onStop)(), void (*onOpenSettings)()) {
  cbStart = onStart;
  cbStop = onStop;
  cbSettings = onOpenSettings;
}

static void goClicked(lv_event_t*) {
  if (uiRunning) {
    if (cbStop) cbStop();
  } else {
    if (cbStart) cbStart();
  }
}

static void gearClicked(lv_event_t*) {
  if (cbSettings) cbSettings();
}

static void buildMain() {
  scrMain = lv_obj_create(nullptr);

  lblClock = lv_label_create(scrMain);
  lv_obj_set_style_text_font(lblClock, &lv_font_montserrat_48, 0);
  lv_label_set_text(lblClock, "00:00");
  lv_obj_align(lblClock, LV_ALIGN_TOP_MID, 0, 18);

  lblReps = lv_label_create(scrMain);
  lv_obj_set_style_text_font(lblReps, &lv_font_montserrat_28, 0);
  lv_label_set_text(lblReps, "0");
  lv_obj_align(lblReps, LV_ALIGN_TOP_MID, 0, 74);

  lblPhase = lv_label_create(scrMain);
  lv_obj_set_style_text_font(lblPhase, &lv_font_montserrat_20, 0);
  lv_label_set_text(lblPhase, "");
  lv_obj_align(lblPhase, LV_ALIGN_TOP_MID, 0, 108);

  btnGo = lv_btn_create(scrMain);
  lv_obj_set_size(btnGo, 200, 70);
  lv_obj_align(btnGo, LV_ALIGN_BOTTOM_MID, 0, -34);
  lv_obj_add_event_cb(btnGo, goClicked, LV_EVENT_CLICKED, nullptr);
  lblGo = lv_label_create(btnGo);
  lv_obj_set_style_text_font(lblGo, &lv_font_montserrat_28, 0);
  lv_obj_center(lblGo);

  lv_obj_t* gear = lv_btn_create(scrMain);
  lv_obj_set_size(gear, 44, 44);
  lv_obj_align(gear, LV_ALIGN_TOP_RIGHT, -6, 6);
  lv_obj_add_event_cb(gear, gearClicked, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* gl = lv_label_create(gear);
  lv_label_set_text(gl, LV_SYMBOL_SETTINGS);
  lv_obj_center(gl);

  lblStatus = lv_label_create(scrMain);
  lv_label_set_text(lblStatus, "");
  lv_obj_align(lblStatus, LV_ALIGN_TOP_LEFT, 6, 6);

  lv_scr_load(scrMain);
}

static const char* phaseName(Phase p) {
  switch (p) {
    case Phase::Lift: return "UP";
    case Phase::Hold: return "HOLD";
    case Phase::Lower: return "DOWN";
    case Phase::Rest: return "REST";
    case Phase::SafeLower: return "STOPPING";
    default: return "";
  }
}

void uiUpdateMain(bool running, Phase phase, uint32_t elapsedSec, uint16_t reps,
                  bool wifiUp, bool serverUp) {
  uiRunning = running;
  lv_label_set_text_fmt(lblClock, "%02lu:%02lu", (unsigned long)(elapsedSec / 60),
                        (unsigned long)(elapsedSec % 60));
  lv_label_set_text_fmt(lblReps, "x %u", reps);
  lv_label_set_text(lblPhase, phaseName(phase));
  lv_label_set_text(lblGo, running ? "STOP" : "START");
  lv_obj_set_style_bg_color(btnGo, running ? lv_color_hex(0x8c2f39) : lv_color_hex(0x2e7d32), 0);
  lv_label_set_text_fmt(lblStatus, "%s %s", wifiUp ? LV_SYMBOL_WIFI : "--",
                        serverUp ? LV_SYMBOL_OK : LV_SYMBOL_CLOSE);
}
```

Call `buildMain()` at the end of `uiBegin()`.

(English phase/button labels for now — Thai font lands in Task 8 and swaps the strings.)

- [ ] **Step 3: Hook callbacks + refresh in `.ino`**

```cpp
static void onTouchStart() { gSession.start(millis()); }
static void onTouchStop() { gSession.requestStop(millis()); }
static void onOpenSettings() { /* Task 8 */ }

// setup(), after uiBegin():
  uiSetCallbacks(onTouchStart, onTouchStop, onOpenSettings);

// loop(), after applyActuator(), throttled to 4 Hz:
  static uint32_t lastUi = 0;
  if (now - lastUi >= 250) {
    lastUi = now;
    uiUpdateMain(gSession.running(), gSession.phase(), gSession.elapsedSec(now),
                 gSession.reps(), netWifiUp(), netServerUp());
  }
```

Remove the temporary serial `s`/`x` control block.

- [ ] **Step 4: Compile** → success. Device: touch START runs cycle, STOP safe-lowers; timer/reps/phase update; app start/stop also flips the same screen (dual control visible).

- [ ] **Step 5: Commit**

```bash
git add firmware
git commit -m "feat(firmware): lvgl main screen with dual-control start/stop"
```

---

### Task 8: Settings screen (Wi-Fi + server + cycle timings) + Thai labels

**Files:**
- Create: `firmware/pt-leg-device/font_thai.c` (generated LVGL font)
- Modify: `firmware/pt-leg-device/ui.h`, `firmware/pt-leg-device/ui.cpp`, `firmware/pt-leg-device/pt-leg-device.ino`, `firmware/README.md`

**Interfaces:**
- Produces: `void uiOpenSettings(const DeviceSettings& current)`, `void uiSetSettingsSaved(void (*onSave)(const DeviceSettings&))`.

- [ ] **Step 1: Generate Thai font** — download Kanit Regular TTF (fonts.google.com → Kanit), then:

```powershell
npx lv_font_conv --font Kanit-Regular.ttf --size 24 --bpp 2 --format lvgl `
  --range 0x20-0x7F --range 0x0E00-0x0E7F -o firmware/pt-leg-device/font_thai.c
```

In `font_thai.c` confirm the symbol name `font_thai`; declare in `ui.cpp`: `LV_FONT_DECLARE(font_thai)`. Document the command in `firmware/README.md`.

- [ ] **Step 2: Swap main-screen strings to Thai** — in `ui.cpp` set `lblGo`, `lblPhase` to use `&font_thai` style font, and:

```cpp
static const char* phaseName(Phase p) {
  switch (p) {
    case Phase::Lift: return "ยกขา";
    case Phase::Hold: return "ค้างไว้";
    case Phase::Lower: return "ลดขา";
    case Phase::Rest: return "พัก";
    case Phase::SafeLower: return "กำลังหยุด";
    default: return "";
  }
}
// button: running ? "หยุด" : "เริ่ม"
```

If glyph rendering is broken/unreadable on device, fallback per spec: keep English phase labels, Thai only on the start/stop button.

- [ ] **Step 3: Settings screen in `ui.cpp`**

```cpp
// needs #include <WiFi.h> at top of ui.cpp for the scan list
static lv_obj_t *scrSettings, *taSsid, *taPass, *taServer, *kb;
static lv_obj_t *spinLift, *spinHold, *spinLower, *spinRest;
static void (*cbSave)(const DeviceSettings&) = nullptr;

void uiSetSettingsSaved(void (*onSave)(const DeviceSettings&)) { cbSave = onSave; }

static void taFocused(lv_event_t* e) {
  lv_keyboard_set_textarea(kb, (lv_obj_t*)lv_event_get_target(e));
  lv_obj_clear_flag(kb, LV_OBJ_FLAG_HIDDEN);
}

static void saveClicked(lv_event_t*) {
  DeviceSettings s;
  s.wifiSsid = lv_textarea_get_text(taSsid);
  s.wifiPass = lv_textarea_get_text(taPass);
  s.serverUrl = lv_textarea_get_text(taServer);
  s.liftSec = (uint16_t)lv_spinbox_get_value(spinLift);
  s.holdSec = (uint16_t)lv_spinbox_get_value(spinHold);
  s.lowerSec = (uint16_t)lv_spinbox_get_value(spinLower);
  s.restSec = (uint16_t)lv_spinbox_get_value(spinRest);
  if (cbSave) cbSave(s);
  lv_scr_load(scrMain);
}

static lv_obj_t* makeTa(lv_obj_t* parent, const char* placeholder, bool password) {
  lv_obj_t* ta = lv_textarea_create(parent);
  lv_textarea_set_one_line(ta, true);
  lv_textarea_set_placeholder_text(ta, placeholder);
  lv_textarea_set_password_mode(ta, password);
  lv_obj_set_width(ta, lv_pct(100));
  lv_obj_add_event_cb(ta, taFocused, LV_EVENT_FOCUSED, nullptr);
  return ta;
}

static lv_obj_t* makeSpin(lv_obj_t* parent, int val) {
  lv_obj_t* sp = lv_spinbox_create(parent);
  lv_spinbox_set_range(sp, 1, 60);
  lv_spinbox_set_value(sp, val);
  lv_obj_set_width(sp, 90);
  return sp;
}

void uiOpenSettings(const DeviceSettings& cur) {
  scrSettings = lv_obj_create(nullptr);
  lv_obj_t* col = lv_obj_create(scrSettings);
  lv_obj_set_size(col, lv_pct(100), lv_pct(100));
  lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(col, 8, 0);

  // Wi-Fi scan list: dropdown of visible networks fills the SSID field
  lv_obj_t* dd = lv_dropdown_create(col);
  lv_obj_set_width(dd, lv_pct(100));
  {
    int n = WiFi.scanNetworks(); // blocking ~2 s, acceptable on settings entry
    String opts = "(scan)";
    for (int i = 0; i < n && i < 12; i++) opts += String("\n") + WiFi.SSID(i);
    lv_dropdown_set_options(dd, opts.c_str());
  }
  lv_obj_add_event_cb(
      dd,
      [](lv_event_t* e) {
        char sel[64];
        lv_dropdown_get_selected_str((lv_obj_t*)lv_event_get_target(e), sel, sizeof(sel));
        if (strcmp(sel, "(scan)") != 0) lv_textarea_set_text(taSsid, sel);
      },
      LV_EVENT_VALUE_CHANGED, nullptr);

  taSsid = makeTa(col, "WiFi SSID", false);
  lv_textarea_set_text(taSsid, cur.wifiSsid.c_str());
  taPass = makeTa(col, "WiFi password", true);
  lv_textarea_set_text(taPass, cur.wifiPass.c_str());
  taServer = makeTa(col, "http://server:8000", false);
  lv_textarea_set_text(taServer, cur.serverUrl.c_str());

  lv_obj_t* row = lv_obj_create(col);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
  spinLift = makeSpin(row, cur.liftSec);
  spinHold = makeSpin(row, cur.holdSec);
  spinLower = makeSpin(row, cur.lowerSec);
  spinRest = makeSpin(row, cur.restSec);

  lv_obj_t* btnSave = lv_btn_create(col);
  lv_obj_add_event_cb(btnSave, saveClicked, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* sl = lv_label_create(btnSave);
  lv_label_set_text(sl, "SAVE");
  lv_obj_center(sl);

  kb = lv_keyboard_create(scrSettings);
  lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);

  lv_scr_load(scrSettings);
}
```

- [ ] **Step 4: Wire in `.ino`**

```cpp
static void onSettingsSaved(const DeviceSettings& s) {
  gSettings = s;
  settingsSave(gSettings);
  gSession.cfg = {gSettings.liftSec * 1000u, gSettings.holdSec * 1000u,
                  gSettings.lowerSec * 1000u, gSettings.restSec * 1000u};
  netBegin(gSettings); // reconnect with new creds/url
}

static void onOpenSettings() { uiOpenSettings(gSettings); }

// setup(): uiSetSettingsSaved(onSettingsSaved);
// setup(), after uiSetCallbacks: first-boot redirect
  if (gSettings.wifiSsid.isEmpty()) uiOpenSettings(gSettings);
```

- [ ] **Step 5: Compile** → success. Device: first boot (erase flash: `arduino-cli upload ... ; esptool erase_flash` beforehand if needed) lands on settings; enter Wi-Fi/server via on-screen keyboard; save → main screen, Wi-Fi icon appears.

- [ ] **Step 6: Commit**

```bash
git add firmware
git commit -m "feat(firmware): settings screen with wifi setup + thai labels"
```

---

### Task 9: Hardware integration test matrix

**Files:** none (verification; fixups committed as found)

Prereq: board flashed, relays wired to GPIO 22/27, `server\scripts\dev.ps1 -NoMock` running, phone app on same LAN.

- [ ] **Step 1: Safety checks**
  - Boot: both relays de-energized before/while sketch starts.
  - Never both relays ON (scope/LEDs across a few cycles).
  - Stop mid-LIFT and mid-HOLD: DOWN energizes for lowerSec, then all OFF.
  - Power pull mid-LIFT, repower: boots with relays OFF, idle.

- [ ] **Step 2: Dual-control matrix** — each row = ONE server session, correct duration/reps, app UI and device screen agree:

| start from | stop from |
|---|---|
| app | app |
| app | device touch |
| device touch | app |
| device touch | device touch |

Verify after each: `Invoke-RestMethod "http://localhost:8000/api/sessions?patientCode=PT001"` (claim via app pain-save first) or check the unclaimed row count via server logs.

- [ ] **Step 3: Offline matrix**
  - Server stopped, touch start → cycle runs standalone; start server → `started` event arrives (session appears), heartbeats resume.
  - Wi-Fi off (kill AP or move out of range) mid-session → cycle uninterrupted, device shows Wi-Fi down; restore → heartbeat resumes.
  - Device powered off, app start → app shows เครื่องออฟไลน์ within ~30 s (command expiry).

- [ ] **Step 4: Server pytest still green with real device attached**

```powershell
cd server
.venv\Scripts\python -m pytest
```

Expected: all pass (contract parity).

- [ ] **Step 5: Touch calibration check** — if touch points land offset, tune the `map()` ranges in `touchCb` and re-flash.

- [ ] **Step 6: Commit fixups**

```bash
git add firmware
git commit -m "fix(firmware): hardware integration fixups"
```
