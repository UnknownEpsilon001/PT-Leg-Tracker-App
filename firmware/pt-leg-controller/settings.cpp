// อ่าน/เขียนค่าตั้งค่าลงหน่วยความจำถาวร NVS ผ่านไลบรารี Preferences
// Load/save device settings to NVS via the Preferences library.
#include "settings.h"
#include <Preferences.h>
#include "config.h"

static const char* NS = "ptleg";  // ชื่อเนมสเปซใน NVS

void settingsLoad(DeviceSettings& s) {
  // [TH] เปิดแบบอ่านอย่างเดียว (true) ; ถ้าไม่มีคีย์ให้คืนค่าตั้งต้นจาก config.h
  Preferences p;
  p.begin(NS, true);
  s.wifiSsid = p.getString("ssid", "");
  s.wifiPass = p.getString("pass", "");
  s.serverUrl = p.getString("server", "");
  s.deviceCode = p.getString("code", DEFAULT_DEVICE_CODE);
  s.maxTravelSec = p.getUShort("maxtravel", DEFAULT_MAX_TRAVEL_SEC);
  s.holdSec = p.getUShort("hold", DEFAULT_HOLD_SEC);
  s.restSec = p.getUShort("rest", DEFAULT_REST_SEC);
  p.end();
}

void settingsSave(const DeviceSettings& s) {
  // [TH] เปิดแบบเขียนได้ (false) แล้วบันทึกทุกฟิลด์ลง NVS
  Preferences p;
  p.begin(NS, false);
  p.putString("ssid", s.wifiSsid);
  p.putString("pass", s.wifiPass);
  p.putString("server", s.serverUrl);
  p.putString("code", s.deviceCode);
  p.putUShort("maxtravel", s.maxTravelSec);
  p.putUShort("hold", s.holdSec);
  p.putUShort("rest", s.restSec);
  p.end();
}
