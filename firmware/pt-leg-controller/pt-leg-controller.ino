#include "config.h"
#include "settings.h"
#include "actuator.h"
#include "switches.h"
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

  // temporary serial control until UI/net land: 's' start, 'x' stop
  if (Serial.available()) {
    char c = Serial.read();
    if (c == 's') gSession.start(now);
    if (c == 'x') gSession.requestStop(now);
  }
  if (gSession.takeStartedEdge()) Serial.println("EVENT started");
  if (gSession.takeStoppedEdge())
    Serial.printf("EVENT stopped elapsed=%lu reps=%u fault=%d\n",
                  (unsigned long)gSession.finalElapsedSec(), gSession.finalReps(),
                  gSession.faulted() ? 1 : 0);
  delay(5);
}
