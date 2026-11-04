// ขับไฟหน้าปัดสองดวงผ่านรีเลย์สองทางตัวเดียว เขียนขาเฉพาะตอนสถานะเปลี่ยน
// Drives the two panel lamps through one changeover relay. The only cleverness
// here is that the pin is written on change and never per loop: behind a
// mechanical relay a redundant write is a contact operation and a click, not a
// no-op.
#include "panel_lamps.h"
#include <Arduino.h>
#include "config.h"

static int gLastLevel = -1;  // -1 = ยังไม่เคยเขียนขา

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
  // Level before mode, the same order actuatorBegin() uses. A pin
  // comes out of reset high-Z; pinMode(OUTPUT) makes it drive whatever the output
  // register already holds, which is LOW. On an active-low relay board LOW is
  // energized, so mode-then-write flicks the lamp on for the microseconds until
  // the next line — a click nobody asked for, at every boot.
  // [TH] ตั้งระดับก่อนตั้งโหมด ลำดับเดียวกับ actuatorBegin()
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
  beginLamp(PIN_LAMP_RELAY);
  gLastLevel = -1;  // force the first set to write — ให้การเรียกครั้งแรกเขียนขาเสมอ
  // Idle is the boot state, so yellow comes on and stays on. That is not a lamp
  // test and must not become one: boot is a normal (non-fault) idle state, so it
  // shows exactly one lamp like every other normal state, and a green flash at
  // power-up would read as the machine starting by itself, which is the one
  // impression this panel must never give.
  // [TH] สถานะตอนเปิดเครื่องคือว่าง ไฟเหลืองจึงติดและติดค้างไว้
  //      นี่ไม่ใช่การทดสอบหลอดและต้องไม่กลายเป็นการทดสอบหลอด
  //      ตอนเปิดเครื่องคือสถานะว่างตามปกติ (ไม่ใช่ความผิดพลาด) จึงติดไฟดวงเดียวเหมือนสถานะปกติอื่นๆ
  //      การวาบไฟเขียวตอนเปิดเครื่องจะถูกอ่านว่าเครื่องเริ่มทำงานเอง
  //      ซึ่งเป็นความรู้สึกเดียวที่หน้าปัดนี้ต้องไม่สร้างขึ้นเลย
  panelLampsSet(false, false, false);
}

void panelLampsSet(bool inProgress, bool faulted, bool jog) {
  // Blink phase from the clock, not from a counter: every caller sees the same
  // phase, and a loop that stalls in the network code just stretches one half.
  // [TH] จังหวะกะพริบคิดจากนาฬิกา ไม่ใช่ตัวนับ ลูปที่ค้างในงานเครือข่ายจะแค่ยืดครึ่งรอบนั้นออกไป
  bool green = (faulted || jog) ? ((millis() / LAMP_BLINK_HALF_MS) & 1) != 0 : inProgress;
  writeLamp(PIN_LAMP_RELAY, gLastLevel, green);  // relay on = NO = green — รีเลย์ทำงาน = NO = เขียว
}
