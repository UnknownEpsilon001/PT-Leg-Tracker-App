#pragma once
#include <Arduino.h>

// โครงสร้างค่าตั้งค่าของอุปกรณ์ ที่เก็บถาวรใน NVS (หน่วยความจำแฟลชของ ESP32)
// Device settings persisted in NVS flash.
struct DeviceSettings {
  String wifiSsid;   // ชื่อเครือข่าย WiFi
  String wifiPass;   // รหัสผ่าน WiFi
  String serverUrl;  // e.g. http://192.168.0.12:8000 — ที่อยู่เซิร์ฟเวอร์
  String deviceCode; // machine pairing code, e.g. KNEE-01 — รหัสจับคู่เครื่อง
  uint16_t maxTravelSec, liftSec, lowerSec, holdSec, restSec;  // เพดานต่อจังหวะ / ยก / ลง / ค้าง / พัก (วินาที)
  uint16_t targetReps;  // stop after this many reps; 0 = until STOP — 0 = ไม่จำกัด
};

void settingsLoad(DeviceSettings& s);  // โหลดค่าจาก NVS (ไม่มีให้ใช้ค่าตั้งต้น)
void settingsSave(const DeviceSettings& s);  // บันทึกค่าลง NVS
