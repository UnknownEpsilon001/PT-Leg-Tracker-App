#include "config.h"
#include "settings.h"
#include "actuator.h"
#include "switches.h"
#include "session.h"
#include "net.h"

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
    default:  // Idle, Hold, Rest, Fault: relays off
      actuatorOff();
      break;
  }
}

void setup() {
  actuatorBegin(); // safety: relays off before anything else
  switchesBegin();
  Serial.begin(115200);
  settingsLoad(gSettings);
  gSession.cfg = {gSettings.maxTravelSec * 1000u, gSettings.holdSec * 1000u,
                  gSettings.restSec * 1000u};
  actuatorSetCap(gSettings.maxTravelSec * 1000u + 500u);
  netBegin(gSettings);
  Serial.printf("pt-leg-controller %s ssid=%s server=%s cycle max/hold/rest=%u/%u/%u\n",
                FW_VERSION, gSettings.wifiSsid.c_str(), gSettings.serverUrl.c_str(),
                gSettings.maxTravelSec, gSettings.holdSec, gSettings.restSec);
}

void loop() {
  uint32_t now = millis();
  switchesPoll(now);
  gSession.tick(now, switchTop(), switchBottom());
  applyActuator();
  actuatorTick(now);
  if (actuatorCapTripped()) gSession.forceFault(now);  // backstop: SM never commanded OFF
  netLoop(now);

  // bench stand-in for the touch button until the CYD link lands: 's' start, 'x' stop
  if (Serial.available()) {
    char c = Serial.read();
    if (c == 's') gSession.start(now);
    if (c == 'x') gSession.requestStop(now);
  }

  static uint32_t lastPoll = 0, lastHb = 0;
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
  delay(5);
}
