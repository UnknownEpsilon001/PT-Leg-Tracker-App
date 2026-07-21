#include "config.h"
#include "settings.h"
#include "actuator.h"
#include "switches.h"
#include "session.h"
#include "net.h"
#include "link.h"

DeviceSettings gSettings;
SessionSM gSession;
Link gLink;

static void applyActuator() {
  switch (gSession.phase()) {
    case Phase::Lift: actuatorUp(); break;
    case Phase::Lower:
    case Phase::SafeLower: actuatorDown(); break;
    default: actuatorOff(); break;  // Idle, Hold, Rest, Fault
  }
}

static void onLinkCmd(const char* v) {
  uint32_t now = millis();
  if (!strcmp(v, "start")) gSession.start(now);
  else if (!strcmp(v, "stop")) gSession.requestStop(now);
}

static void applySettings(const DeviceSettings& s) {
  gSettings = s;
  settingsSave(gSettings);
  gSession.cfg = {gSettings.maxTravelSec * 1000u, gSettings.holdSec * 1000u,
                  gSettings.restSec * 1000u};
  actuatorSetCap(gSettings.maxTravelSec * 1000u + 500u);
  netBegin(gSettings);
}

static void onLinkSettings(const LinkSettings& s) {
  DeviceSettings d;
  d.wifiSsid = s.ssid; d.wifiPass = s.pass; d.serverUrl = s.server;
  d.deviceCode = s.code;
  d.maxTravelSec = s.maxTravel; d.holdSec = s.hold; d.restSec = s.rest;
  applySettings(d);
}

static LinkSettings currentAsLink() {
  LinkSettings s;
  s.ssid = gSettings.wifiSsid; s.pass = gSettings.wifiPass;
  s.server = gSettings.serverUrl; s.code = gSettings.deviceCode;
  s.maxTravel = gSettings.maxTravelSec; s.hold = gSettings.holdSec;
  s.rest = gSettings.restSec;
  return s;
}

static void onLinkHello() { gLink.sendSettings(currentAsLink()); }

void setup() {
  actuatorBegin();  // safety first
  switchesBegin();
  Serial.begin(115200);
  delay(200);  // let UART0 settle so the boot banner is not clipped
  Serial2.begin(115200, SERIAL_8N1, 16, 17);  // UART to CYD
  settingsLoad(gSettings);
  gSession.cfg = {gSettings.maxTravelSec * 1000u, gSettings.holdSec * 1000u,
                  gSettings.restSec * 1000u};
  actuatorSetCap(gSettings.maxTravelSec * 1000u + 500u);
  netBegin(gSettings);
  gLink.begin(&Serial2);
  gLink.onCmd = onLinkCmd;
  gLink.onSettings = onLinkSettings;
  gLink.onHello = onLinkHello;
  Serial.printf("pt-leg-controller %s ssid=%s server=%s code=%s cycle max/hold/rest=%u/%u/%u\n",
                FW_VERSION, gSettings.wifiSsid.c_str(), gSettings.serverUrl.c_str(),
                gSettings.deviceCode.c_str(), gSettings.maxTravelSec, gSettings.holdSec,
                gSettings.restSec);
}

void loop() {
  uint32_t now = millis();
  switchesPoll(now);
  gSession.tick(now, switchTop(), switchBottom());
  applyActuator();
  actuatorTick(now);
  if (actuatorCapTripped()) gSession.forceFault(now);  // backstop: SM never commanded OFF
  netLoop(now);
  gLink.poll();

  // UART-loss safe-stop: screen may be dead, no touch-stop possible
  if (gSession.running() && gLink.lastRxMs() != 0 && now - gLink.lastRxMs() > 2000)
    gSession.requestStop(now);

  static uint32_t lastPoll = 0, lastHb = 0, lastState = 0;
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
  if (gSession.takeStoppedEdge()) netQueueEvent("stopped", gSession.finalElapsedSec(), gSession.finalReps());

  if (now - lastState >= 250) {
    lastState = now;
    LinkState st{gSession.running(), (int)gSession.phase(), gSession.elapsedSec(now),
                 gSession.reps(), netWifiUp(), netServerUp()};
    gLink.sendState(st);
  }
  delay(2);
}
