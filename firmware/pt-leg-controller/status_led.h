#pragma once

// ไฟแสดงสถานะ 12V บนหน้าปัด ขับผ่านรีเลย์ ติดตอนเครื่องกำลังทำงาน นอกนั้นดับ
// The 12 V panel lamp. ON while a session is in progress, OFF otherwise — no
// blinking, no heartbeat, nothing that toggles on its own.
//
// That is a hardware constraint, not a preference. The lamp is 12 V and is
// switched by a mechanical relay, so every change of state is a contact
// operation and an audible click. A 5 Hz blink is 18,000 operations an hour and
// would wear a ~100k-operation relay out in a few hours of one latched fault; an
// idle heartbeat would do it while the machine sat doing nothing. Solid states
// cost two operations per session, so a relay outlives the machine.
// [TH] นี่คือข้อจำกัดของฮาร์ดแวร์ ไม่ใช่ความชอบ
//      หลอดเป็นแบบ 12 โวลต์และสั่งผ่านรีเลย์กลไก ทุกการเปลี่ยนสถานะจึงเป็นการตัดต่อหน้าสัมผัสหนึ่งครั้งและมีเสียงคลิก
//      การกะพริบ 5 ครั้งต่อวินาทีคือ 18,000 ครั้งต่อชั่วโมง
//      จะกินอายุรีเลย์ที่ทนได้ราวแสนครั้งจนหมดภายในไม่กี่ชั่วโมงจากความผิดพลาดที่ค้างอยู่ครั้งเดียว
//      ส่วนการวาบเป็นจังหวะตอนว่างจะกินอายุรีเลย์ไปเรื่อยๆ ทั้งที่เครื่องไม่ได้ทำอะไรเลย
//      การใช้สถานะติดค้างหรือดับสนิทเสียค่าใช้จ่ายแค่สองครั้งต่อรอบ รีเลย์จึงอยู่ได้นานกว่าตัวเครื่อง
//
// So the lamp carries one bit, and it is the bit that matters at arm's reach:
//   lit   the machine is in a session and the actuator may move — keep clear
//   dark  idle, or a latched fault; nothing is going to move on its own
// A latched FAULT reads as dark because the relays really are off in that state.
// The CYD, the serial log and the card row are where a fault is diagnosed; the
// lamp only answers "is it about to move".
// [TH] หลอดจึงบอกข้อมูลเพียงบิตเดียว และเป็นบิตที่สำคัญที่สุดเมื่อยืนอยู่ข้างเครื่อง
//      ติด = อยู่ในรอบการทำงาน ชุดขับเคลื่อนอาจเคลื่อนที่ ให้อยู่ห่างไว้
//      ดับ = ว่าง หรือค้างสถานะผิดพลาด จะไม่มีอะไรเคลื่อนที่เอง
//      สถานะผิดพลาดที่ค้างอยู่จะแสดงเป็นดับ เพราะตอนนั้นรีเลย์ดับจริงๆ
//      การวินิจฉัยความผิดพลาดให้ดูที่จอ CYD บันทึกทางพอร์ตอนุกรม และแถวข้อมูลบนการ์ด
//      หลอดตอบแค่คำถามเดียวว่า "กำลังจะเคลื่อนที่ไหม"

void statusLedBegin();  // pin low / lamp dark before anything else can drive it
                        // [TH] ตั้งขาให้ดับไว้ก่อน ก่อนที่อะไรจะมาสั่งได้

// Call every loop with SessionSM::running(). Writes the pin only when the state
// actually changes, so a relay clicks twice a session and not once a loop.
// [TH] เรียกทุกลูปด้วยค่าจาก SessionSM::running()
//      เขียนขาเฉพาะตอนที่สถานะเปลี่ยนจริงๆ รีเลย์จึงคลิกสองครั้งต่อรอบ ไม่ใช่ทุกลูป
void statusLedSet(bool inProgress);
