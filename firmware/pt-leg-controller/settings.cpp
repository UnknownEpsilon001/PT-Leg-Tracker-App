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
  s.maxTravelSec = p.getUShort("maxtravel", DEFAULT_MAX_TRAVEL_SEC);
  s.holdSec = p.getUShort("hold", DEFAULT_HOLD_SEC);
  s.restSec = p.getUShort("rest", DEFAULT_REST_SEC);
  p.end();
}

void settingsSave(const DeviceSettings& s) {
  Preferences p;
  p.begin(NS, false);
  p.putString("ssid", s.wifiSsid);
  p.putString("pass", s.wifiPass);
  p.putString("server", s.serverUrl);
  p.putUShort("maxtravel", s.maxTravelSec);
  p.putUShort("hold", s.holdSec);
  p.putUShort("rest", s.restSec);
  p.end();
}
