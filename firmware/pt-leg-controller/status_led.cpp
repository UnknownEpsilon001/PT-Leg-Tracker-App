// ขับไฟแสดงสถานะผ่านรีเลย์ เขียนขาเฉพาะตอนสถานะเปลี่ยน เพื่อไม่ให้รีเลย์ตัดต่อถี่เกินจำเป็น
// Drives the panel lamp through its relay. The only cleverness here is that the
// pin is written on change and never per loop: behind a mechanical relay a
// redundant write is a contact operation and a click, not a no-op.
#include "status_led.h"
#include <Arduino.h>
#include "config.h"

static int gLastLevel = -1;  // -1 = nothing written yet — ยังไม่เคยเขียนขา

static void write(bool on) {
#ifdef LED_ACTIVE_LOW
  int level = on ? LOW : HIGH;
#else
  int level = on ? HIGH : LOW;
#endif
  if (level == gLastLevel) return;
  gLastLevel = level;
  digitalWrite(PIN_STATUS_LED, level);
}

void statusLedBegin() {
  // Level before mode, which is deliberately the opposite order to
  // actuatorBegin(). A pin comes out of reset as a high-Z input; pinMode(OUTPUT)
  // makes it drive whatever the output register already holds, and that is LOW.
  // On an active-low relay board LOW is energized, so mode-then-write flicks the
  // lamp on for the microseconds until the next line — a click nobody asked for,
  // at every single boot. Writing the register first means the pin drives the
  // right level from the instant it becomes an output.
  // actuatorBegin() gets away with the other order only because the motor relays
  // are active-high (RELAY_ACTIVE_LOW is commented out in config.h), where the
  // default LOW happens to be "off".
  // [TH] ตั้งระดับก่อนตั้งโหมด ซึ่งเป็นลำดับตรงข้ามกับ actuatorBegin() โดยตั้งใจ
  //      ขา GPIO ออกจากรีเซ็ตมาเป็นอินพุตอิมพีแดนซ์สูง
  //      คำสั่ง pinMode(OUTPUT) จะทำให้ขาเริ่มขับค่าที่ค้างอยู่ในรีจิสเตอร์ ซึ่งคือ LOW
  //      บนบอร์ดรีเลย์แบบสั่งงานด้วยลอจิกต่ำ ค่า LOW คือสถานะที่รีเลย์ทำงาน
  //      การตั้งโหมดก่อนจึงทำให้หลอดวาบขึ้นชั่วไมโครวินาทีจนกว่าจะถึงบรรทัดถัดไป
  //      เป็นเสียงคลิกที่ไม่มีใครสั่ง และเกิดขึ้นทุกครั้งที่เปิดเครื่อง
  //      การเขียนรีจิสเตอร์ก่อนทำให้ขาขับระดับที่ถูกต้องตั้งแต่วินาทีที่มันกลายเป็นเอาต์พุต
  //      ที่ actuatorBegin() ใช้อีกลำดับได้โดยไม่มีปัญหา เป็นเพราะรีเลย์มอเตอร์เป็นแบบสั่งงานด้วยลอจิกสูง
  //      (บรรทัด RELAY_ACTIVE_LOW ใน config.h ถูกปิดไว้) ค่า LOW ตั้งต้นจึงบังเอิญเป็น "ดับ" พอดี
#ifdef LED_ACTIVE_LOW
  digitalWrite(PIN_STATUS_LED, HIGH);
#else
  digitalWrite(PIN_STATUS_LED, LOW);
#endif
  pinMode(PIN_STATUS_LED, OUTPUT);
  gLastLevel = -1;  // force the first statusLedSet to write — ให้การเรียกครั้งแรกเขียนขาเสมอ
  write(false);
  // No power-on lamp test. It would prove the bulb and its wiring at every boot,
  // which is worth something, but the lamp is specified to light only while the
  // machine is working — a flash and a click at power-up would read as the
  // machine starting by itself, which is the one impression this panel must never
  // give. Test the lamp by starting a session.
  // [TH] ไม่มีการทดสอบหลอดตอนเปิดเครื่อง
  //      การทดสอบจะพิสูจน์ว่าหลอดและสายใช้ได้ทุกครั้งที่เปิดเครื่อง ซึ่งมีประโยชน์อยู่
  //      แต่ข้อกำหนดคือให้หลอดติดเฉพาะตอนเครื่องกำลังทำงาน
  //      การวาบพร้อมเสียงคลิกตอนเปิดเครื่องจะถูกอ่านว่าเครื่องเริ่มทำงานเอง
  //      ซึ่งเป็นความรู้สึกเดียวที่หน้าปัดนี้ต้องไม่สร้างขึ้นเลย ถ้าจะทดสอบหลอดให้เริ่มรอบการทำงานดู
}

void statusLedSet(bool inProgress) { write(inProgress); }
