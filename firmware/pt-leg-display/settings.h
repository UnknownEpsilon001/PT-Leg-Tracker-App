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

  // เปิดเสียงพูดบนบอร์ดจอ — spoken cues on the display board.
  //
  // Positive sense everywhere: JSON key, struct field, switch label and API all
  // mean the same thing. A `muted` field behind a "เสียง" switch is an
  // inversion, and inversions get flipped by whoever touches them next.
  // [TH] ใช้ความหมายเชิงบวกทุกที่ ทั้งคีย์ใน JSON ฟิลด์ในโครงสร้าง ป้ายบนสวิตช์ และ API
  //      ถ้าเก็บเป็น "ปิดเสียง" แต่ป้ายเขียนว่า "เสียง" ค่าจะกลับด้าน
  //      และค่าที่กลับด้านจะถูกสลับผิดโดยคนที่มาแก้ทีหลัง
  //
  // This board never reads it: the speaker is on the display, so the setting
  // belongs to the board that owns the hardware. Carrying it here is the price of
  // one shared settings type, and it deliberately does NOT appear in LinkSettings
  // — it never crosses the wire, which is exactly why the display's copy of it
  // survives a settings push from this board.
  // [TH] บอร์ดนี้ไม่เคยอ่านค่านี้ ลำโพงอยู่บนบอร์ดจอ ค่านี้จึงเป็นของบอร์ดที่มีฮาร์ดแวร์นั้น
  //      การมีค่านี้อยู่ตรงนี้คือราคาของการใช้ชนิดข้อมูลตั้งค่าร่วมกันเพียงชนิดเดียว
  //      และจงใจไม่ใส่ไว้ใน LinkSettings ค่านี้จึงไม่เคยถูกส่งข้ามสาย
  //      ซึ่งเป็นเหตุผลที่ค่าฝั่งจอไม่ถูกล้างเมื่อบอร์ดนี้ส่งค่าตั้งค่าไปให้
  bool soundOn;
};

void settingsLoad(DeviceSettings& s);  // โหลดค่าจาก NVS (ไม่มีให้ใช้ค่าตั้งต้น)
void settingsSave(const DeviceSettings& s);  // บันทึกค่าลง NVS
