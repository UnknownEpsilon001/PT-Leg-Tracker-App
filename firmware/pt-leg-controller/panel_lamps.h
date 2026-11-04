#pragma once

// ไฟหน้าปัด 12V สองดวงบนรีเลย์สองทางตัวเดียว: เขียว=กำลังทำงาน เหลือง=ว่าง กะพริบ=โหมดขยับเองหรือผิดพลาดค้างอยู่
// The two 12 V panel lamps, on one changeover relay (green on NO, yellow on NC):
//   green         a session is in progress and the actuator may move — keep clear
//   yellow        idle; nothing is going to move on its own
//   blinking      jog mode, or a latched FAULT — the motor is cut, but the leg may be raised
//
// Exactly one lamp is lit at any moment; the contact makes anything else
// impossible. That is why a fault blinks: the old "both lit" fault signal needed
// a relay per lamp, and the second channel died. A fault must not read as plain
// yellow, because on a machine running without a CYD the panel is the only
// indicator there is, and "ready" on a machine that faulted with the leg raised
// is the one wrong impression this panel must not give.
// [TH] ติดได้ทีละดวงเสมอเพราะหน้าสัมผัสสองทางทำให้เป็นอย่างอื่นไม่ได้
//      ความผิดพลาดจึงต้องกะพริบ เพราะสัญญาณ "ติดทั้งสองดวง" แบบเดิมต้องใช้รีเลย์ดวงละตัว และรีเลย์ช่องที่สองเสียไปแล้ว
//      ความผิดพลาดต้องไม่แสดงเป็นสีเหลืองเฉยๆ เพราะบนเครื่องที่ไม่มีจอ CYD หน้าปัดคือตัวบอกสถานะเพียงอย่างเดียว
//      การที่ผู้ใช้อ่านว่า "พร้อมใช้งาน" บนเครื่องที่ผิดพลาดค้างอยู่โดยขายังยกอยู่ คือความเข้าใจผิดเดียวที่หน้าปัดนี้ต้องไม่สร้าง
//
// Every toggle is a contact operation and a click, so blinking is kept to the two
// states that need attention. See LAMP_BLINK_HALF_MS in config.h for the cost.
// [TH] ทุกครั้งที่สลับคือการตัดต่อหน้าสัมผัสและมีเสียงคลิก จึงกะพริบเฉพาะสองสถานะที่ต้องการความสนใจ
//      ดูต้นทุนได้ที่ LAMP_BLINK_HALF_MS ใน config.h

void panelLampsBegin();  // relay off (yellow) before anything else can drive it — ให้รีเลย์ดับ (ไฟเหลือง) ไว้ก่อน

// Call every loop with SessionSM::running(), SessionSM::faulted() and whether jog
// mode owns the motor. Writes the pin only when the relay's state changes, so a
// solid state costs nothing and a blink costs one write per half period.
// [TH] เรียกทุกลูปด้วยค่าจาก SessionSM::running() SessionSM::faulted() และสถานะโหมดขยับเอง
//      เขียนขาเฉพาะตอนสถานะรีเลย์เปลี่ยน สถานะคงที่จึงไม่เสียอะไร ส่วนการกะพริบเขียนหนึ่งครั้งต่อครึ่งรอบ
void panelLampsSet(bool inProgress, bool faulted, bool jog);
