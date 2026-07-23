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

void actuatorSetCap(uint32_t capMs);  // max continuous energize before forced off; 0 = disabled
                                      // [TH] เวลาจ่ายไฟต่อเนื่องสูงสุดก่อนตัดอัตโนมัติ (0 = ปิดใช้งาน)
void actuatorTick(uint32_t nowMs);    // enforces the cap; call every loop
                                      // [TH] บังคับเพดานเวลา ต้องเรียกทุกลูป
bool actuatorCapTripped();            // latched true after a cap trip until next up()/down()
                                      // [TH] ค้างเป็น true หลังตัดจนกว่าจะสั่ง up()/down() ครั้งถัดไป
