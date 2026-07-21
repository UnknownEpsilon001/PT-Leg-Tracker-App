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

// last settings the controller told us about; the settings screen pre-fills from this
static DeviceSettings gLast;

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

  static uint32_t lastUi = 0, lastPing = 0;
  if (now - lastUi >= 250) {
    lastUi = now;
    uiUpdateMain(uiRunning, uiPhase, uiElapsed, uiReps, uiWifi, uiServer);
  }
  if (now - lastPing >= 1000) {  // keep the controller's UART watchdog fed
    lastPing = now;
    gLink.sendPing();
  }
  delay(2);
}
