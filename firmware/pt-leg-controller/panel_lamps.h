#pragma once

// ไฟหน้าปัดสองดวง 12V ขับผ่านรีเลย์: เขียว=กำลังทำงาน เหลือง=ว่าง ติดทั้งคู่=ผิดพลาดค้างอยู่
// The two 12 V panel lamps, each behind a mechanical relay:
//   green only    a session is in progress and the actuator may move — keep clear
//   yellow only   idle; nothing is going to move on its own
//   both lit      a latched FAULT — the relays are off, but the leg may be raised
//
// Exactly one lamp is lit in both normal states, so "both lit" is unmistakably
// abnormal without anything blinking. It replaces the earlier rule that a fault
// simply read yellow: a fault and an ordinary idle then looked identical on the
// panel, and on a machine running without a CYD the panel is the only indicator
// there is. An operator reading "ready" on a machine that faulted with the leg
// raised is the one wrong impression this panel must not give.
// [TH] ในสถานะปกติจะติดดวงเดียวเสมอ การติดทั้งคู่จึงผิดปกติอย่างชัดเจนโดยไม่ต้องกะพริบเลย
//      กฎนี้มาแทนกฎเดิมที่ให้ความผิดพลาดแสดงเป็นสีเหลือง
//      ซึ่งทำให้ความผิดพลาดกับสถานะว่างตามปกติดูเหมือนกันบนหน้าปัด
//      และบนเครื่องที่ใช้งานโดยไม่มีจอ CYD หน้าปัดคือตัวบอกสถานะเพียงอย่างเดียวที่มี
//      การที่ผู้ใช้อ่านว่า "พร้อมใช้งาน" บนเครื่องที่ผิดพลาดค้างอยู่โดยที่ขายังยกอยู่
//      คือความเข้าใจผิดเดียวที่หน้าปัดนี้ต้องไม่สร้างขึ้น
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
// The lamps still answer "is it about to move" first — green is the only state
// that means the actuator may move. The fault signal costs two extra contact
// operations per fault, not a blink, so the relay-life argument above is intact.
// What a fault IS remains diagnosed on the CYD, the serial log and the card row.
// [TH] หลอดคู่นี้ยังตอบคำถาม "กำลังจะเคลื่อนที่ไหม" เป็นอันดับแรก
//      สีเขียวเป็นสถานะเดียวที่แปลว่าชุดขับเคลื่อนอาจเคลื่อนที่ได้
//      การแสดงความผิดพลาดเสียการตัดต่อหน้าสัมผัสเพิ่มสองครั้งต่อความผิดพลาดหนึ่งครั้ง ไม่ใช่การกะพริบ
//      เหตุผลเรื่องอายุรีเลย์ด้านบนจึงยังใช้ได้เหมือนเดิม
//      ส่วนรายละเอียดว่าผิดพลาดอะไรยังดูได้จากจอ CYD บันทึกทางพอร์ตอนุกรม และแถวข้อมูลบนการ์ด

void panelLampsBegin();  // both pins driven dark before anything else can — ตั้งให้ดับทั้งคู่ก่อน

// Call every loop with SessionSM::running() and SessionSM::faulted(). Writes each
// pin only when its own state changes, so the relays click a handful of times a
// session and not once a loop.
// [TH] เรียกทุกลูปด้วยค่าจาก SessionSM::running() และ SessionSM::faulted()
//      เขียนแต่ละขาเฉพาะตอนที่สถานะของขานั้นเปลี่ยนจริงๆ รีเลย์จึงคลิกไม่กี่ครั้งต่อรอบ ไม่ใช่ทุกลูป
void panelLampsSet(bool inProgress, bool faulted);
