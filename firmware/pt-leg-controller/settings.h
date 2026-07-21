#pragma once
#include <Arduino.h>

struct DeviceSettings {
  String wifiSsid;
  String wifiPass;
  String serverUrl; // e.g. http://192.168.0.12:8000
  String deviceCode; // machine pairing code, e.g. KNEE-01
  uint16_t maxTravelSec, holdSec, restSec;
};

void settingsLoad(DeviceSettings& s);
void settingsSave(const DeviceSettings& s);
