#include "config.h"
#include "settings.h"

DeviceSettings gSettings;

void setup() {
  Serial.begin(115200);
  settingsLoad(gSettings);
  Serial.printf("pt-leg-controller %s ssid=%s server=%s cycle max/hold/rest=%u/%u/%u\n",
                FW_VERSION, gSettings.wifiSsid.c_str(), gSettings.serverUrl.c_str(),
                gSettings.maxTravelSec, gSettings.holdSec, gSettings.restSec);
}

void loop() {}
