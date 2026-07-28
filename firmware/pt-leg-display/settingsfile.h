#pragma once
#include "settings.h"  // DeviceSettings

// ค่าตั้งค่าของเครื่องที่เก็บเป็นไฟล์ JSON บนการ์ด SD ของบอร์ดจอ (/settings.json)
// Device settings stored as JSON on the CYD's microSD card (/settings.json).
//
// The card is the master copy: a readable file wins over the controller's NVS,
// because editing the file on a PC has to actually take effect. The controller
// keeps its NVS copy so the machine still runs with no card and no screen.
// [TH] การ์ดคือค่าหลัก ถ้าอ่านไฟล์ได้จะใช้ค่าจากการ์ดแทนค่าใน NVS ของคอนโทรลเลอร์
//      เพราะการแก้ไฟล์จากคอมพิวเตอร์ต้องมีผลจริง ส่วนคอนโทรลเลอร์ยังเก็บสำเนาไว้ใน NVS
//      เพื่อให้เครื่องทำงานได้แม้ไม่มีการ์ดและไม่มีจอ
//
// Everything here is safe to call with no card: load returns false and save
// reports failure, and the caller carries on.
// [TH] ทุกฟังก์ชันเรียกได้แม้ไม่มีการ์ด โหลดจะคืน false และบันทึกจะรายงานว่าไม่สำเร็จ

void settingsFileDefaults(DeviceSettings& s);   // ค่าตั้งต้นตามเอกสารออกแบบ
bool settingsFileLoad(DeviceSettings& s);       // false: no card / no file / malformed
bool settingsFilePresent();                     // a valid file was read or written this boot
bool settingsFileSave(const DeviceSettings& s); // temp + rename; false on write failure
