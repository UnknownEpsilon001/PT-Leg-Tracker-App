// ขับไฟหน้าปัดสองดวงผ่านรีเลย์ เขียนขาเฉพาะตอนสถานะเปลี่ยน
// Drives the two panel lamps through their relays. The only cleverness here is
// that a pin is written on change and never per loop: behind a mechanical relay a
// redundant write is a contact operation and a click, not a no-op.
#include "panel_lamps.h"
#include <Arduino.h>
#include "config.h"

static int gLastGreen = -1, gLastYellow = -1;  // -1 = ยังไม่เคยเขียนขา

static void writeLamp(uint8_t pin, int& last, bool on) {
#ifdef LED_ACTIVE_LOW
  int level = on ? LOW : HIGH;
#else
  int level = on ? HIGH : LOW;
#endif
  if (level == last) return;
  last = level;
  digitalWrite(pin, level);
}

static void beginLamp(uint8_t pin) {
  // Level before mode, deliberately the opposite order to actuatorBegin(). A pin
  // comes out of reset high-Z; pinMode(OUTPUT) makes it drive whatever the output
  // register already holds, which is LOW. On an active-low relay board LOW is
  // energized, so mode-then-write flicks the lamp on for the microseconds until
  // the next line — a click nobody asked for, at every boot.
  // [TH] ตั้งระดับก่อนตั้งโหมด ซึ่งเป็นลำดับตรงข้ามกับ actuatorBegin() โดยตั้งใจ
  //      ขา GPIO ออกจากรีเซ็ตมาเป็นอินพุตอิมพีแดนซ์สูง
  //      คำสั่ง pinMode(OUTPUT) จะทำให้ขาเริ่มขับค่าที่ค้างอยู่ในรีจิสเตอร์ ซึ่งคือ LOW
  //      บนบอร์ดรีเลย์แบบสั่งงานด้วยลอจิกต่ำ ค่า LOW คือสถานะที่รีเลย์ทำงาน
  //      การตั้งโหมดก่อนจึงทำให้หลอดวาบขึ้นชั่วไมโครวินาที เป็นเสียงคลิกที่ไม่มีใครสั่งทุกครั้งที่เปิดเครื่อง
#ifdef LED_ACTIVE_LOW
  digitalWrite(pin, HIGH);
#else
  digitalWrite(pin, LOW);
#endif
  pinMode(pin, OUTPUT);
}

void panelLampsBegin() {
  beginLamp(PIN_LAMP_GREEN);
  beginLamp(PIN_LAMP_YELLOW);
  gLastGreen = gLastYellow = -1;  // force the first set to write — ให้การเรียกครั้งแรกเขียนขาเสมอ
  // Idle is the boot state, so yellow comes on and stays on. That is not a lamp
  // test and must not become one: the machine is specified to show exactly one
  // lamp at all times, and a green flash at power-up would read as the machine
  // starting by itself, which is the one impression this panel must never give.
  // [TH] สถานะตอนเปิดเครื่องคือว่าง ไฟเหลืองจึงติดและติดค้างไว้
  //      นี่ไม่ใช่การทดสอบหลอดและต้องไม่กลายเป็นการทดสอบหลอด
  //      ข้อกำหนดคือให้มีไฟติดดวงเดียวตลอดเวลา
  //      การวาบไฟเขียวตอนเปิดเครื่องจะถูกอ่านว่าเครื่องเริ่มทำงานเอง
  //      ซึ่งเป็นความรู้สึกเดียวที่หน้าปัดนี้ต้องไม่สร้างขึ้นเลย
  panelLampsSet(false, false);
}

void panelLampsSet(bool inProgress, bool faulted) {
  // A fault lights both. Green is still only ever on when the actuator may move,
  // so an operator who reads nothing but the green lamp is not misled by this.
  // [TH] ความผิดพลาดจะติดทั้งสองดวง ไฟเขียวยังคงติดเฉพาะตอนที่ชุดขับเคลื่อนอาจเคลื่อนที่ได้เท่านั้น
  //      ผู้ใช้ที่ดูแค่ไฟเขียวจึงไม่ถูกทำให้เข้าใจผิดจากกฎนี้
  writeLamp(PIN_LAMP_GREEN, gLastGreen, inProgress || faulted);
  writeLamp(PIN_LAMP_YELLOW, gLastYellow, !inProgress || faulted);
}
