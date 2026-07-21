#include "ui.h"
#include "link.h"
#include "session.h"   // Phase enum for uiUpdateMain
#include "settings.h"  // DeviceSettings cached from the controller

Link gLink;
static bool uiRunning = false;
static Phase uiPhase = Phase::Idle;
static uint32_t uiElapsed = 0;
static uint16_t uiReps = 0;
static bool uiWifi = false, uiServer = false;

// Last settings the controller told us about; the settings screen pre-fills from
// this. Seeded with the documented defaults rather than left zeroed: a zeroed
// struct puts 0 into the spinboxes, LVGL clamps that to their minimum of 1, and
// a SAVE before the controller ever answered would push a 1-second travel cap
// over good NVS settings. uiSetSettingsKnown() gates SAVE until a real reply
// lands; these values only decide what the screen shows in the meantime.
static DeviceSettings gLast = {"", "", "", "default", 8, 10, 10};
static bool gSettingsKnown = false;
static bool gLinkUp = false;

static void onTouchStart() { Serial.println("touch: start"); gLink.sendCmd("start"); }
static void onTouchStop() { Serial.println("touch: stop"); gLink.sendCmd("stop"); }
static void onOpenSettings() { Serial.println("touch: settings"); uiOpenSettings(gLast); }

static void onSaveFromUi(const DeviceSettings& s) {
  gLast = s;
  LinkSettings ls;
  ls.ssid = s.wifiSsid; ls.pass = s.wifiPass; ls.server = s.serverUrl; ls.code = s.deviceCode;
  ls.maxTravel = s.maxTravelSec; ls.hold = s.holdSec; ls.rest = s.restSec;
  gLink.sendSettings(ls);
  uiSetDeviceCode(s.deviceCode.c_str());
}

static void onLinkSettingsReply(const LinkSettings& s) {
  gLast.wifiSsid = s.ssid; gLast.wifiPass = s.pass; gLast.serverUrl = s.server;
  gLast.deviceCode = s.code;
  gLast.maxTravelSec = s.maxTravel; gLast.holdSec = s.hold; gLast.restSec = s.rest;
  uiSetDeviceCode(s.code.c_str());
  gSettingsKnown = true;
  uiSetSettingsKnown(true);  // the controller's real values are on screen now
}

static void onLinkState(const LinkState& s) {
  uiRunning = s.running;
  uiPhase = (Phase)s.phase;
  uiElapsed = s.elapsed;
  uiReps = s.reps;
  uiWifi = s.wifi;
  uiServer = s.server;
}

void setup() {
  Serial.begin(115200);
  delay(200);  // UART0 settles after begin(); without this the first line is lost
  Serial.println("pt-leg-display boot");
  Serial2.begin(115200, SERIAL_8N1, 22, 27);  // UART to controller (CYD free pins)
  uiBegin();
  uiSetCallbacks(onTouchStart, onTouchStop, onOpenSettings);
  uiSetSettingsSaved(onSaveFromUi);
  gLink.begin(&Serial2);
  gLink.onState = onLinkState;
  gLink.onSettingsReply = onLinkSettingsReply;
  gLink.sendHello();  // ask controller for current settings + state
}

void loop() {
  uint32_t now = millis();
  uiLoop();
  gLink.poll();

  // The controller sends state at ~4 Hz. Nothing for 2.5 s means the link is
  // gone and the cached state is stale, so the UI must stop presenting it as
  // live. Mirrors the controller's own 2 s UART watchdog.
  uint32_t lastRx = gLink.lastRxMs();
  gLinkUp = lastRx != 0 && (now - lastRx) < 2500;

  static uint32_t lastUi = 0, lastPing = 0, lastHello = 0;
  if (now - lastUi >= 250) {
    lastUi = now;
    uiUpdateMain(uiRunning, uiPhase, uiElapsed, uiReps, uiWifi, uiServer, gLinkUp);
  }
  if (now - lastPing >= 1000) {  // keep the controller's UART watchdog fed
    lastPing = now;
    gLink.sendPing();
  }
  // Re-ask for settings if the controller was not listening at our boot (it may
  // power up later, or the cable may be reconnected).
  if (!gSettingsKnown && now - lastHello >= 3000) {
    lastHello = now;
    gLink.sendHello();
  }
  delay(2);
}
