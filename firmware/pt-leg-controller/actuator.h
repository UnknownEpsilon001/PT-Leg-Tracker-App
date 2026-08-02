#pragma once
#include <stdint.h>

// ตัวขับมอเตอร์/รีเลย์ (แอกชูเอเตอร์) ของอุปกรณ์กายภาพบำบัดขา
// เป็นจุดเดียวที่รับผิดชอบความปลอดภัยของการจ่ายไฟรีเลย์
// Actuator (relay) driver — the single owner of relay safety.

enum ActState { ACT_OFF, ACT_UP, ACT_DOWN };  // สถานะ: ปิด / ยกขึ้น / ลดลง

// Safety invariants enforced here and nowhere else:
// - UP and DOWN are never energized at the same time (break-before-make).
// - actuatorBegin() forces both relays off and must be the first hardware
//   call in setup().
// [TH] ข้อกำหนดความปลอดภัยที่บังคับใช้ที่ไฟล์นี้เท่านั้น:
//  - ห้ามจ่ายไฟรีเลย์ "ขึ้น" และ "ลง" พร้อมกัน (ตัดก่อนต่อ = break-before-make)
//  - actuatorBegin() บังคับรีเลย์ดับทั้งคู่ และต้องเป็นคำสั่งฮาร์ดแวร์แรกใน setup()
void actuatorBegin();
void actuatorUp();
void actuatorDown();
void actuatorOff();
ActState actuatorState();
// Restart the energize clock without changing direction. Call on a phase change
// that begins a new stroke in the same direction as the last one (Lower ->
// SafeLower), or the new stroke silently inherits what the old one already spent.
// [TH] เริ่มจับเวลาการจ่ายไฟใหม่โดยไม่เปลี่ยนทิศทาง
//      เรียกเมื่อเปลี่ยนสถานะไปเริ่มการเคลื่อนที่ครั้งใหม่ในทิศเดียวกับครั้งก่อน (ลดลง -> ลดลงเพื่อหยุด)
//      ถ้าไม่เรียก การเคลื่อนที่ครั้งใหม่จะรับช่วงเวลาที่ครั้งก่อนใช้ไปแล้วมาเงียบๆ
void actuatorRenewStroke();

void actuatorSetCap(uint32_t capMs);  // max continuous energize before forced off; 0 = disabled
                                      // [TH] เวลาจ่ายไฟต่อเนื่องสูงสุดก่อนตัดอัตโนมัติ (0 = ปิดใช้งาน)
void actuatorTick(uint32_t nowMs);    // enforces the cap; call every loop
                                      // [TH] บังคับเพดานเวลา ต้องเรียกทุกลูป
// Read-and-clear, and the only way to observe a cap trip. A plain "has it
// tripped" getter used to exist; it was removed rather than kept alongside this,
// because the flag is now consumed every loop iteration and any polling caller
// would essentially always have read false — a getter that silently never
// reports the thing it is named after is worse than no getter.
// [TH] อ่านแล้วล้างค่า และเป็นทางเดียวที่จะรู้ว่าเพดานเวลาถูกตัด
//      เดิมมีฟังก์ชันแบบถามว่า "ตัดไปหรือยัง" อยู่ด้วย แต่ถูกลบทิ้งแทนที่จะเก็บไว้คู่กัน
//      เพราะตอนนี้ค่าธงถูกอ่านและล้างทุกลูป ผู้เรียกที่คอยถามจะได้ค่าเท็จแทบตลอด
//      ฟังก์ชันที่ไม่เคยรายงานสิ่งที่ตัวเองตั้งชื่อไว้เลย แย่กว่าการไม่มีฟังก์ชันนั้น
bool actuatorTakeCapTrip();
// Whether the hardware timer that backs the cap exists. False means the cap is
// enforced only from actuatorTick(), i.e. only while the loop is running — the
// single point of failure the timer was added to remove. actuatorBegin() runs
// before Serial is open, so setup() reports this once it can be seen.
// [TH] ตัวจับเวลาระดับฮาร์ดแวร์ที่หนุนเพดานเวลานี้มีอยู่จริงหรือไม่
//      ถ้าเป็นเท็จ เพดานเวลาจะถูกบังคับจาก actuatorTick() อย่างเดียว คือเฉพาะตอนที่ลูปยังเดินอยู่
//      ซึ่งเป็นจุดเสียหายจุดเดียวที่ตัวจับเวลานี้มีไว้กำจัด
//      actuatorBegin() ทำงานก่อนที่พอร์ตอนุกรมจะเปิด setup() จึงเป็นคนรายงานเมื่อมองเห็นได้แล้ว
bool actuatorCapTimerReady();
