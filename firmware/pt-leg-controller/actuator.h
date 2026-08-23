#pragma once
#include <stdint.h>

// ตัวขับมอเตอร์ (แอกชูเอเตอร์) ของอุปกรณ์กายภาพบำบัดขา ใช้โมดูลสะพานเอช BTS7960
// เป็นจุดเดียวที่รับผิดชอบความปลอดภัยของการขับมอเตอร์
// Actuator driver for the BTS7960 H-bridge — the single owner of motor safety.

enum ActState { ACT_OFF, ACT_UP, ACT_DOWN };  // สถานะ: ปิด / ยกขึ้น / ลดลง

// Safety invariants enforced here and nowhere else:
// - UP and DOWN are never driven at the same time (break-before-make).
// - Nothing is ever driven while the master cut relay is open, and the relay is
//   never made or broken while the drive is live.
// - actuatorBegin() forces the drive dead and the master relay open, and must be
//   the first hardware call in setup().
// - Ramping is a comfort feature, never a safety one: every emergency path
//   (fault, energize cap, live reversal, boot) cuts the drive instantly.
// [TH] ข้อกำหนดความปลอดภัยที่บังคับใช้ที่ไฟล์นี้เท่านั้น:
//  - ห้ามขับ "ขึ้น" และ "ลง" พร้อมกัน (ตัดก่อนต่อ = break-before-make)
//  - ห้ามขับมอเตอร์ขณะที่รีเลย์ตัดหลักเปิดวงจรอยู่ และห้ามตัดต่อรีเลย์นั้นขณะที่กำลังขับอยู่
//  - actuatorBegin() บังคับให้กำลังขับดับและรีเลย์ตัดหลักเปิดวงจร
//    และต้องเป็นคำสั่งฮาร์ดแวร์แรกใน setup()
//  - การไล่กำลังขับมีไว้เพื่อความนุ่มนวล ไม่ใช่เพื่อความปลอดภัย
//    ทุกเส้นทางฉุกเฉิน (ความผิดพลาด เพดานเวลา การกลับทิศขณะขับอยู่ การเปิดเครื่อง) ตัดกำลังขับทันที
void actuatorBegin();
void actuatorUp();
void actuatorDown();
// Immediate stop. The drive is killed in the same call, with no ramp — this is
// what a fault and every other emergency path calls, and what applyActuator()
// re-asserts every loop in a phase that must not move.
// [TH] หยุดทันที กำลังขับถูกตัดในการเรียกครั้งเดียวโดยไม่ไล่ลง
//      เป็นสิ่งที่ความผิดพลาดและทุกเส้นทางฉุกเฉินเรียกใช้
//      และเป็นสิ่งที่ applyActuator() สั่งซ้ำทุกลูปในสถานะที่ต้องไม่เคลื่อนที่
void actuatorOff();
// Normal stop: fade the drive out over MOTOR_RAMP_MS instead of dropping it.
// Only for a stroke that ended because it was supposed to (Lift -> Hold,
// Lower -> Rest, the bottom of a safe-lower). Never for a fault. Identical to
// actuatorOff() once the fade has finished, so it is safe to call every loop.
// [TH] การหยุดตามปกติ: ค่อยๆ ผ่อนกำลังขับลงภายในเวลา MOTOR_RAMP_MS แทนการตัดทิ้ง
//      ใช้เฉพาะการเคลื่อนที่ที่จบเพราะถึงเวลาของมันเอง
//      (ยกขึ้น -> ค้าง, ลดลง -> พัก, และตอนถึงพื้นของการลดขาเพื่อหยุด) ห้ามใช้กับความผิดพลาด
//      เมื่อผ่อนจนสุดแล้วจะเหมือน actuatorOff() ทุกประการ จึงเรียกซ้ำทุกลูปได้ปลอดภัย
void actuatorSoftOff();
// Close (true) or open (false) the master cut relay. Drive the session's
// "may the motor move at all" state into this every loop; it is idempotent.
// Closing does not start anything — it only permits it, and the first stroke
// still waits out the contact settle time. Opening is deferred until the drive
// has actually reached zero, so a soft stop is never cut short and the contacts
// never break a live circuit; an emergency cut opens it immediately, but only
// after the drive is already dead.
// [TH] ปิดวงจร (true) หรือเปิดวงจร (false) ของรีเลย์ตัดหลัก
//      ให้ส่งสถานะ "มอเตอร์เคลื่อนที่ได้หรือไม่" ของรอบการทำงานเข้ามาทุกลูป เรียกซ้ำได้ปลอดภัย
//      การปิดวงจรไม่ได้เริ่มอะไร เป็นแค่การอนุญาต และการเคลื่อนที่ครั้งแรกยังต้องรอให้หน้าสัมผัสนิ่งก่อน
//      ส่วนการเปิดวงจรจะรอจนกำลังขับลงถึงศูนย์จริง การหยุดแบบนุ่มนวลจึงไม่ถูกตัดกลางคัน
//      และหน้าสัมผัสไม่เคยตัดวงจรที่มีกระแส
//      การตัดฉุกเฉินจะเปิดวงจรทันที แต่ก็ต่อเมื่อกำลังขับดับไปแล้วเช่นกัน
void actuatorArm(bool on);
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
// Whether both LEDC channels attached. False means the motor cannot be driven at
// all — the machine will time out every stroke and fault, with nothing having
// moved. Reported by setup() for the same reason the cap timer is: this runs
// before Serial is open.
// [TH] ช่อง LEDC ทั้งสองช่องถูกผูกกับขาสำเร็จหรือไม่
//      ถ้าเป็นเท็จ จะขับมอเตอร์ไม่ได้เลย เครื่องจะหมดเวลาทุกจังหวะแล้วขึ้นความผิดพลาดโดยไม่มีอะไรขยับ
//      ให้ setup() เป็นคนรายงานด้วยเหตุผลเดียวกับตัวจับเวลาเพดานเวลา คือฟังก์ชันนี้ทำงานก่อนพอร์ตอนุกรมจะเปิด
bool actuatorPwmReady();
