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
                  uint16_t target, bool wifiUp, bool serverUp, bool linkUp);
void uiSetDeviceCode(const char* code);  // ตั้งรหัสอุปกรณ์ที่แสดงบนจอ
// Main-screen target stepper. The controller owns the target: this only reports
// a change the operator made; the label itself follows the next state message.
// [TH] ปุ่มปรับจำนวนครั้งเป้าหมายบนหน้าหลัก คอนโทรลเลอร์เป็นเจ้าของค่า ตรงนี้แค่รายงานการเปลี่ยนแปลง
//      ป้ายตัวเลขจะเปลี่ยนตามข้อความสถานะครั้งถัดไปเท่านั้น
void uiSetTargetChanged(void (*cb)(uint16_t));

// Confirmation screen: shown while the controller is in Phase::Confirm. Driven
// off the phase directly, not `running` -- Confirm is deliberately excluded
// from running() on the controller (session.h there), so uiUpdateMain's own
// running-based screen switch never reaches this screen on its own. The
// sketch has to call this instead, every tick where phase == Phase::Confirm,
// and simply stop calling it once phase leaves Confirm; ui.cpp notices that
// and returns to the main screen on its own (see uiUpdateMain).
//
// controllerTarget is target() as echoed back over the state message -- while
// Confirm, that is pendingTargetReps, not the stored prescription (session.h's
// target()), and it is the only proof this screen has that an edit actually
// reached the controller: the "sets" message that carries an edit down is a
// bare emit() with no ack on this branch (link.cpp). This value is seeded onto
// the on-screen number exactly once -- the first call after the screen was
// not already the active one, i.e. the transition into Confirm -- and every
// later call while it stays active instead compares the same argument against
// the number already on screen, showing a persistent disagreement as
// unconfirmed rather than either silently accepting it or resending. Seeding
// on every call would stomp an in-flight local edit with an echo of the value
// from up to 250 ms (plus a UART round trip) earlier -- exactly the round-trip
// wait that makes the main screen's own target stepper feel dead when the
// link is down (see targetStep() in ui.cpp). See ui.cpp for the rest.
// [TH] หน้าจอยืนยัน แสดงเมื่อคอนโทรลเลอร์อยู่สถานะรอยืนยัน ขับด้วยสถานะโดยตรง ไม่ใช่ตัวแปร running
//      เพราะสถานะรอยืนยันถูกกันออกจาก running() โดยตั้งใจฝั่งคอนโทรลเลอร์ (ดู session.h ที่นั่น)
//      ตรรกะสลับหน้าจอที่อิงกับ running ของ uiUpdateMain จึงไม่มีวันมาถึงหน้านี้เอง สเก็ตช์หลักต้องเรียก
//      ฟังก์ชันนี้แทน ทุกรอบที่ phase == Phase::Confirm แล้วแค่เลิกเรียกทันทีที่ไม่ใช่ ui.cpp จะสังเกตเห็น
//      เองและกลับไปหน้าหลักให้ (ดู uiUpdateMain)
//
//      controllerTarget คือค่า target() ที่สะท้อนกลับมาทางข้อความสถานะ ขณะอยู่สถานะรอยืนยันคือ
//      pendingTargetReps ไม่ใช่ค่าที่แพทย์สั่ง (ดู target() ใน session.h) และเป็นหลักฐานเดียวที่หน้าจอนี้มี
//      ว่าการแก้ไขไปถึงคอนโทรลเลอร์จริงหรือไม่ เพราะข้อความ "sets" ที่ส่งการแก้ไขลงไปเป็นแค่ emit() เปล่าๆ
//      ไม่มีการยืนยันรับบนสายนี้ (link.cpp) ค่านี้จะถูกใช้ตั้งต้นตัวเลขบนจอครั้งเดียวเท่านั้น คือการเรียกครั้งแรก
//      หลังจากที่หน้าจอนี้ยังไม่ใช่หน้าที่กำลังแสดงอยู่ (นั่นคือจังหวะเพิ่งเข้าสถานะรอยืนยัน) ส่วนการเรียกครั้งถัดไป
//      ขณะที่ยังแสดงอยู่จะนำอาร์กิวเมนต์เดียวกันนี้ไปเทียบกับตัวเลขที่แสดงอยู่แล้วแทน แสดงความไม่ตรงกันที่ค้างอยู่
//      เป็นสถานะยังไม่ยืนยัน แทนที่จะยอมรับเงียบๆ หรือส่งซ้ำเอง ถ้าตั้งต้นทุกครั้งที่เรียกแทน จะทับการแก้ไขที่
//      ยังค้างอยู่ด้วยค่าสะท้อนกลับที่เก่ากว่าได้ถึง 250 มิลลิวินาที (บวกเวลาไปกลับของสาย UART) ซึ่งคือการรอ
//      รอบเดินทางกลับแบบเดียวกับที่ทำให้ปุ่มปรับค่าเป้าหมายบนหน้าหลักรู้สึกเหมือนกดไม่ติดตอนลิงก์หลุด
//      (ดู targetStep() ใน ui.cpp) ที่เหลือดูใน ui.cpp
void uiShowConfirm(uint16_t controllerTarget, const DeviceSettings& s);
// ปุ่ม -/+ (onSets ส่ง sendSets) และ ตกลง/ยกเลิก (onConfirm ส่ง sendConfirm: true=ตกลง, false=ยกเลิก)
void uiSetConfirmCallbacks(void (*onSets)(uint16_t), void (*onConfirm)(bool));

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
// cardPresent separates "the card refused the write" from "there is no card".
// The first keeps the user on the screen to read the failure; the second is an
// ordinary supported state and must not make SAVE look broken.
// [TH] cardPresent แยกกรณี "การ์ดเขียนไม่ได้" ออกจาก "ไม่มีการ์ด"
//      กรณีแรกต้องค้างหน้าจอไว้ให้อ่านข้อความ ส่วนกรณีหลังเป็นการใช้งานปกติ
//      ต้องไม่ทำให้ปุ่มบันทึกดูเหมือนเสีย
void uiReportSettingsSaved(bool toCard, bool cardPresent);
// Where the settings on screen came from. A card carrying stale values wins over
// the controller's NVS by design, so the screen has to say when that happened.
// [TH] บอกว่าค่าที่แสดงอยู่มาจากไหน เพราะค่าจากการ์ดจะถูกใช้แทนค่าใน NVS เสมอ
//      ถ้าการ์ดมีค่าเก่าค้างอยู่ ผู้ใช้ต้องเห็นว่ากำลังใช้ค่าจากการ์ด
void uiSetSdStatus(bool cardPresent, bool fileLoaded);
