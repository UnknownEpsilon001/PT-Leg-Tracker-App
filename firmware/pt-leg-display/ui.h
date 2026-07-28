#pragma once
#include <stdint.h>
#include "session.h"
#include "settings.h"
#include "store.h"  // Patient — โครงสร้างข้อมูลคนไข้

// ส่วนติดต่อผู้ใช้บนจอสัมผัส CYD (สร้างด้วยไลบรารี LVGL)
// หน้าจอหลักแสดงเวลา/จำนวนครั้ง/สถานะแบบอย่างเดียว (แสดงผลเท่านั้น) และมีปุ่มเฟืองไปหน้าตั้งค่า
// Touchscreen UI (LVGL) for the CYD: output-only main screen + settings screen.

void uiBegin();  // เริ่มต้นจอ ทัชสกรีน และ LVGL
void uiLoop();   // เรียกทุกลูปเพื่อให้ LVGL ทำงาน
// Main screen is output-only apart from two corner buttons: the gear (settings)
// and the list (patient data). Run control is on the physical panel buttons.
// [TH] หน้าจอหลักแสดงผลอย่างเดียว กดได้แค่ปุ่มมุมสองปุ่ม: เฟือง (ตั้งค่า) และรายการ (ข้อมูลคนไข้)
//      การสั่งเริ่ม/หยุดอยู่ที่ปุ่มจริงบนแผงควบคุม
void uiSetCallbacks(void (*onOpenSettings)(), void (*onOpenData)());

// Patient roster, supplied by the sketch so the UI never touches the SD card
// directly. count() returns -1 when there is no card, which the screen reports
// instead of showing an empty roster as if nobody were registered.
// [TH] ทะเบียนคนไข้ ส่งเข้ามาจากสเก็ตช์ ไม่ให้ส่วน UI ยุ่งกับการ์ด SD เอง
//      count() คืน -1 เมื่อไม่มีการ์ด จอจะได้บอกว่าไม่พบการ์ด แทนที่จะดูเหมือนไม่มีใครลงทะเบียน
void uiSetDataCallbacks(int (*count)(), const Patient* (*get)(int),
                        bool (*add)(const char* code, const char* name, int age),
                        void (*setActive)(int));
void uiOpenData();

// End-of-session summary. saved=false means the row could not reach the card and
// is waiting in RAM, which the patient should see rather than assume it stored.
// [TH] หน้าสรุปเมื่อจบรอบ ถ้า saved=false แปลว่ายังเขียนลงการ์ดไม่ได้ ข้อมูลค้างอยู่ในหน่วยความจำ
void uiShowFinish(const char* patientName, uint32_t durationSec, uint16_t reps,
                  bool fault, bool saved);
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
// Result of the last SAVE. The controller always receives the values; the card
// may not have been there, and the screen must not imply it was written.
// [TH] ผลของการกดบันทึกครั้งล่าสุด ค่าจะถูกส่งให้คอนโทรลเลอร์เสมอ แต่การ์ดอาจไม่ได้เสียบอยู่
//      หน้าจอต้องไม่ทำให้เข้าใจว่าเขียนลงการ์ดแล้ว
void uiReportSettingsSaved(bool toCard);
