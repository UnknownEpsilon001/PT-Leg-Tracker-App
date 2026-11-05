#pragma once
#include <stdint.h>
#include "session.h"
#include "settings.h"
#include "store.h"  // Patient — โครงสร้างข้อมูลคนไข้
#include "sdlog.h"  // HistRow, TZ_OFFSET_SEC — แถวประวัติและเวลาไทย

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
// jog = the controller is in panel jog mode; the screen replaces the logo with
// the button hint so the operator can see the mode is on.
// [TH] jog = คอนโทรลเลอร์อยู่ในโหมดขยับเอง จอจะแสดงคำแนะนำปุ่มแทนโลโก้ ผู้ใช้จะได้เห็นว่าโหมดเปิดอยู่
void uiUpdateMain(bool running, Phase phase, uint32_t elapsedSec, uint16_t reps,
                  uint16_t target, bool wifiUp, bool serverUp, bool linkUp, bool jog);
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
// sketch has to call this instead, every tick where phase == Phase::Confirm
// AND the link is up, and simply stop calling it the moment either stops
// being true; ui.cpp notices that and returns to the main screen on its own
// (see uiUpdateMain).
//
// linkUp follows the same rule this file already states for uiUpdateMain
// above: false means no recent UART traffic, and this screen must not go on
// presenting stale state as live. A confirmation cannot silently expire
// without a message telling the display so -- the controller's own 30 s
// timeout is invisible here unless a fresh state message reports it -- so a
// dead link would otherwise strand the operator on a frozen confirm screen
// with a ตกลง button that looks live but cannot reach the controller. Passed
// through mainly so this function can defend itself (belt and braces) if it
// is ever called with the link down; the sketch is expected to stop calling
// it and let uiUpdateMain's own linkUp-gated leave take the screen back to
// scrMain, which already renders "ไม่พบตัวควบคุม".
//
// controllerTarget is target() as echoed back over the state message -- while
// Confirm, that is pendingTargetReps, not the stored prescription (session.h's
// target()), and it is the only proof this screen has that an edit actually
// reached the controller: the "sets" message that carries an edit down is
// acknowledged by the link layer, but an ack is not the session applying it. This value is seeded onto
// the on-screen number exactly once -- the first call after the screen was
// not already the active one, i.e. the transition into Confirm -- and every
// later call while it stays active instead compares the same argument against
// the number already on screen, showing a disagreement that outlives a short
// grace window as unconfirmed rather than either silently accepting it or
// resending. A single tick's disagreement is expected, not a defect: the
// controller emits state on its own fixed cadence, not on receipt of "sets",
// so an edit's echo and this function's next call race independently and
// disagree for up to one tick on ordinary, healthy taps -- see ui.cpp for the
// grace window this reserves for that race before calling it unconfirmed for
// real. Seeding on every call, rather than once, would separately stomp an
// in-flight local edit with an echo of the value from up to 250 ms (plus a
// UART round trip) earlier -- exactly the round-trip wait that makes the main
// screen's own target stepper feel dead when the link is down (see
// targetStep() in ui.cpp). See ui.cpp for the rest.
// [TH] หน้าจอยืนยัน แสดงเมื่อคอนโทรลเลอร์อยู่สถานะรอยืนยัน ขับด้วยสถานะโดยตรง ไม่ใช่ตัวแปร running
//      เพราะสถานะรอยืนยันถูกกันออกจาก running() โดยตั้งใจฝั่งคอนโทรลเลอร์ (ดู session.h ที่นั่น)
//      ตรรกะสลับหน้าจอที่อิงกับ running ของ uiUpdateMain จึงไม่มีวันมาถึงหน้านี้เอง สเก็ตช์หลักต้องเรียก
//      ฟังก์ชันนี้แทน ทุกรอบที่ phase == Phase::Confirm และลิงก์ยังเชื่อมอยู่ แล้วเลิกเรียกทันทีที่ข้อใดข้อหนึ่ง
//      ไม่จริงแล้ว ui.cpp จะสังเกตเห็นเองและกลับไปหน้าหลักให้ (ดู uiUpdateMain)
//
//      linkUp ใช้กติกาเดียวกับที่ไฟล์นี้กำหนดไว้ให้ uiUpdateMain ด้านบนแล้ว: false แปลว่าไม่มีข้อมูลจาก UART
//      เข้ามาเมื่อเร็วๆ นี้ และหน้าจอนี้ต้องไม่แสดงสถานะเก่าต่อไปราวกับว่ายังสด การหมดเวลายืนยันของคอนโทรลเลอร์
//      เองก็มองไม่เห็นตรงนี้ถ้าไม่มีข้อความสถานะใหม่มารายงาน สายที่หลุดจึงอาจขังผู้ใช้ไว้บนหน้ายืนยันที่ค้างนิ่ง
//      พร้อมปุ่มตกลงที่ดูใช้งานได้แต่ไปไม่ถึงคอนโทรลเลอร์จริง ส่งค่านี้เข้ามาหลักๆ เพื่อให้ฟังก์ชันนี้ป้องกันตัวเองได้
//      (กันพลาดสองชั้น) ถ้าถูกเรียกตอนลิงก์หลุดจริงๆ ตามปกติสเก็ตช์จะเลิกเรียกฟังก์ชันนี้ แล้วปล่อยให้เงื่อนไข
//      ออกจากหน้านี้ของ uiUpdateMain ที่กันด้วย linkUp เช่นกัน พาไปหน้าหลักซึ่งแสดง "ไม่พบตัวควบคุม" อยู่แล้ว
//
//      controllerTarget คือค่า target() ที่สะท้อนกลับมาทางข้อความสถานะ ขณะอยู่สถานะรอยืนยันคือ
//      pendingTargetReps ไม่ใช่ค่าที่แพทย์สั่ง (ดู target() ใน session.h) และเป็นหลักฐานเดียวที่หน้าจอนี้มี
//      ว่าการแก้ไขไปถึงคอนโทรลเลอร์จริงหรือไม่ เพราะข้อความ "sets" ที่ส่งการแก้ไขลงไปได้รับการตอบรับจากชั้นลิงก์
//      แต่การตอบรับไม่ได้แปลว่ารอบนำค่าไปใช้แล้ว ค่านี้จะถูกใช้ตั้งต้นตัวเลขบนจอครั้งเดียวเท่านั้น คือการเรียกครั้งแรก
//      หลังจากที่หน้าจอนี้ยังไม่ใช่หน้าที่กำลังแสดงอยู่ (นั่นคือจังหวะเพิ่งเข้าสถานะรอยืนยัน) ส่วนการเรียกครั้งถัดไป
//      ขณะที่ยังแสดงอยู่จะนำอาร์กิวเมนต์เดียวกันนี้ไปเทียบกับตัวเลขที่แสดงอยู่แล้วแทน แสดงความไม่ตรงกันที่ค้างอยู่
//      เกินช่วงผ่อนผันสั้นๆ เป็นสถานะยังไม่ยืนยัน แทนที่จะยอมรับเงียบๆ หรือส่งซ้ำเอง ความไม่ตรงกันแค่หนึ่งรอบ
//      ไม่ใช่ข้อผิดพลาด เพราะคอนโทรลเลอร์ส่งสถานะตามจังหวะของตัวเองเสมอ ไม่ใช่ตอนได้รับ "sets" ค่าที่สะท้อนกลับ
//      ของการแก้ไขกับการเรียกครั้งถัดไปของฟังก์ชันนี้จึงแข่งกันเป็นอิสระ และไม่ตรงกันได้นานถึงหนึ่งรอบแม้ลิงก์ปกติดี
//      ดู ui.cpp ว่าช่วงผ่อนผันนี้ยาวแค่ไหนก่อนจะถือว่าไม่ยืนยันจริง ถ้าตั้งต้นทุกครั้งที่เรียกแทนการตั้งต้นครั้งเดียว
//      จะทับการแก้ไขที่ยังค้างอยู่ด้วยค่าสะท้อนกลับที่เก่ากว่าได้ถึง 250 มิลลิวินาที (บวกเวลาไปกลับของสาย UART)
//      ซึ่งคือการรอรอบเดินทางกลับแบบเดียวกับที่ทำให้ปุ่มปรับค่าเป้าหมายบนหน้าหลักรู้สึกเหมือนกดไม่ติดตอนลิงก์หลุด
//      (ดู targetStep() ใน ui.cpp) ที่เหลือดูใน ui.cpp
void uiShowConfirm(uint16_t controllerTarget, const DeviceSettings& s, bool linkUp);
// ปุ่ม -/+ (onSets ส่ง sendSets) และ ตกลง/ยกเลิก (onConfirm ส่ง sendConfirm: true=ตกลง, false=ยกเลิก)
void uiSetConfirmCallbacks(void (*onSets)(uint16_t), void (*onConfirm)(bool));

// Tapped the on-screen jog exit — แตะปุ่มออกจากโหมดขยับเองบนจอ
void uiSetJogExit(void (*cb)());

// The controller's clock from the state stream, UTC seconds, 0 = unknown. Shown
// in the main screen's status corner and used to seed the date/time screen.
// [TH] เวลาของคอนโทรลเลอร์จากข้อความสถานะ เป็นวินาที UTC ค่า 0 = ไม่ทราบ
//      แสดงที่มุมสถานะของหน้าหลัก และใช้เป็นค่าตั้งต้นของหน้าตั้งวันเวลา
void uiSetClock(uint32_t nowUtc);
// The date/time screen (opened from settings) hands the typed time over as UTC
// seconds; the sketch sends it to the controller.
// [TH] หน้าตั้งวันเวลา (เปิดจากหน้าตั้งค่า) ส่งเวลาที่ตั้งเป็นวินาที UTC ให้สเก็ตช์ส่งต่อไปที่คอนโทรลเลอร์
void uiSetTimeSet(void (*cb)(uint32_t epochUtc));
// Session history (opened from the patient screen) reads the card through this,
// so this file still never touches SD: fill out[] newest first, return the
// count, -1 for no card.
// [TH] หน้าประวัติ (เปิดจากหน้าคนไข้) อ่านการ์ดผ่านฟังก์ชันนี้ ไฟล์นี้จึงยังไม่แตะการ์ดเอง
//      ใส่ out[] จากใหม่ไปเก่า คืนจำนวนแถว และ -1 เมื่อไม่มีการ์ด
void uiSetHistorySource(int (*read)(HistRow* out, int max));
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
