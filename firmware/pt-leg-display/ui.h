#pragma once
#include <stdint.h>
#include "session.h"
#include "settings.h"

// ส่วนติดต่อผู้ใช้บนจอสัมผัส CYD (สร้างด้วยไลบรารี LVGL)
// หน้าจอหลักแสดงเวลา/จำนวนครั้ง/สถานะแบบอย่างเดียว (แสดงผลเท่านั้น) และมีปุ่มเฟืองไปหน้าตั้งค่า
// Touchscreen UI (LVGL) for the CYD: output-only main screen + settings screen.

void uiBegin();  // เริ่มต้นจอ ทัชสกรีน และ LVGL
void uiLoop();   // เรียกทุกลูปเพื่อให้ LVGL ทำงาน
void uiSetCallbacks(void (*onOpenSettings)());  // main screen is output-only; only the gear is interactive
                                                // [TH] หน้าจอหลักแสดงผลอย่างเดียว มีเพียงปุ่มเฟืองที่กดได้
// linkUp false = no UART traffic from the controller recently; the screen must
// not keep presenting the last known state as if it were live.
// [TH] linkUp เป็น false = ไม่ได้รับข้อมูลจากคอนโทรลเลอร์เมื่อเร็วๆ นี้
//      จอต้องไม่แสดงสถานะเก่าราวกับว่ายังสด
void uiUpdateMain(bool running, Phase phase, uint32_t elapsedSec, uint16_t reps,
                  bool wifiUp, bool serverUp, bool linkUp);
void uiSetDeviceCode(const char* code);  // ตั้งรหัสอุปกรณ์ที่แสดงบนจอ
void uiOpenSettings(const DeviceSettings& current);  // เปิดหน้าตั้งค่าพร้อมค่าปัจจุบัน
void uiSetSettingsSaved(void (*onSave)(const DeviceSettings&));
// SAVE stays disabled until the controller has sent its settings at least once,
// so a screen full of placeholder values can never overwrite good NVS settings.
// [TH] ปุ่ม SAVE ถูกปิดไว้จนกว่าคอนโทรลเลอร์จะส่งค่าตั้งค่ามาอย่างน้อยหนึ่งครั้ง
//      กันไม่ให้ค่าตัวอย่างบนหน้าจอไปเขียนทับค่าจริงใน NVS
void uiSetSettingsKnown(bool known);
