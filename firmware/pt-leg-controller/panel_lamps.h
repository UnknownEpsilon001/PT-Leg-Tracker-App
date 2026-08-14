#pragma once

// ไฟหน้าปัดสองดวง 12V ขับผ่านรีเลย์ ติดครั้งละดวง: เขียว=กำลังทำงาน เหลือง=ว่าง
// The two 12 V panel lamps, each behind a mechanical relay. Exactly one is lit at
// any moment:
//   green   a session is in progress and the actuator may move — keep clear
//   yellow  idle, or a latched FAULT; nothing is going to move on its own
//
// No blinking and no heartbeat. Every change of state is a contact operation and
// an audible click, so a 5 Hz blink is 18,000 operations an hour and would wear a
// ~100k-operation relay out in a few hours of one latched fault. Solid states cost
// four operations per session across the pair, so the relays outlive the machine.
// [TH] ไม่กะพริบและไม่วาบเป็นจังหวะ ทุกการเปลี่ยนสถานะคือการตัดต่อหน้าสัมผัสหนึ่งครั้งและมีเสียงคลิก
//      การกะพริบ 5 ครั้งต่อวินาทีคือ 18,000 ครั้งต่อชั่วโมง
//      จะกินอายุรีเลย์ที่ทนได้ราวแสนครั้งจนหมดภายในไม่กี่ชั่วโมงจากความผิดพลาดที่ค้างอยู่ครั้งเดียว
//      การใช้สถานะติดค้างหรือดับสนิทเสียแค่สี่ครั้งต่อรอบสำหรับหลอดทั้งคู่ รีเลย์จึงอยู่ได้นานกว่าตัวเครื่อง
//
// A latched FAULT reads yellow: it is not running, and the relays really are off.
// The pair answers "is it about to move" and nothing else — a fault is diagnosed
// on the CYD, the serial log and the card row.
// [TH] สถานะผิดพลาดที่ค้างอยู่จะแสดงเป็นสีเหลือง เพราะไม่ได้กำลังทำงานและรีเลย์ดับจริงๆ
//      หลอดคู่นี้ตอบแค่คำถามเดียวว่า "กำลังจะเคลื่อนที่ไหม"
//      การวินิจฉัยความผิดพลาดให้ดูที่จอ CYD บันทึกทางพอร์ตอนุกรม และแถวข้อมูลบนการ์ด

void panelLampsBegin();  // both pins driven dark before anything else can — ตั้งให้ดับทั้งคู่ก่อน

// Call every loop with SessionSM::running(). Writes each pin only when its own
// state changes, so the relays click twice a session each and not once a loop.
// [TH] เรียกทุกลูปด้วยค่าจาก SessionSM::running()
//      เขียนแต่ละขาเฉพาะตอนที่สถานะของขานั้นเปลี่ยนจริงๆ รีเลย์จึงคลิกดวงละสองครั้งต่อรอบ ไม่ใช่ทุกลูป
void panelLampsSet(bool inProgress);
