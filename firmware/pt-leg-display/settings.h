#pragma once
#include <Arduino.h>

// โครงสร้างค่าตั้งค่าของอุปกรณ์ ที่เก็บถาวรใน NVS (หน่วยความจำแฟลชของ ESP32)
// Device settings persisted in NVS flash.
struct DeviceSettings {
  String wifiSsid;   // ชื่อเครือข่าย WiFi
  String wifiPass;   // รหัสผ่าน WiFi
  String serverUrl;  // e.g. http://192.168.0.12:8000 — ที่อยู่เซิร์ฟเวอร์
  String deviceCode; // machine pairing code, e.g. KNEE-01 — รหัสจับคู่เครื่อง
  uint16_t maxTravelSec, holdSec, restSec;  // เพดานเวลาต่อจังหวะ / เวลาค้าง / เวลาพัก (วินาที)
};

void settingsLoad(DeviceSettings& s);  // โหลดค่าจาก NVS (ไม่มีให้ใช้ค่าตั้งต้น)
void settingsSave(const DeviceSettings& s);  // บันทึกค่าลง NVS
