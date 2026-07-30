// กันเด้งปุ่มแบบเดียวกับ switches.cpp แล้วยกธง "เพิ่งกด" หนึ่งครั้งเมื่อค่านิ่งเปลี่ยนจากไม่กด->กด
// Debounce like switches.cpp, then raise a one-shot "just pressed" flag when the
// stable state transitions not-pressed -> pressed.
#include "buttons.h"
#include <Arduino.h>
#include "config.h"

struct Btn {
  uint8_t pin;
  bool nc;          // normally-closed contact — หน้าสัมผัสแบบปกติปิด
  bool stable;      // debounced logical pressed — ค่าที่ผ่านกันเด้งแล้ว
  bool lastRaw;     // last raw logical read — ค่าดิบครั้งก่อน
  uint32_t changed; // ms of last raw change — เวลาที่ค่าดิบเปลี่ยนล่าสุด
  bool edge;        // one-shot press edge, cleared on read — ขอบกดครั้งเดียว อ่านแล้วล้าง
};

static Btn bStart{PIN_BTN_START, BTN_START_NC, false, false, 0, false};
static Btn bStop{PIN_BTN_STOP, BTN_STOP_NC, false, false, 0, false};
static Btn bReset{PIN_BTN_RESET, BTN_RESET_NC, false, false, 0, false};

// Polarity is per button now, so it is a runtime field rather than a #ifdef: the
// three can differ, and mixed panels are the common case.
// [TH] ขั้วของแต่ละปุ่มแยกกัน จึงเก็บเป็นค่าในโครงสร้างแทนการใช้ #ifdef
static bool rawPressed(uint8_t pin, bool nc) {
  int level = digitalRead(pin);
  // NC: pressed (or a broken wire) opens the contact and the pullup reads HIGH.
  // NO to GND: pressed closes it and reads LOW.
  // [TH] NC: กด (หรือสายขาด) = วงจรเปิด = HIGH ; NO ลง GND: กด = LOW
  return nc ? (level == HIGH) : (level == LOW);
}

static void poll(Btn& b, uint32_t nowMs) {
  bool raw = rawPressed(b.pin, b.nc);
  if (raw != b.lastRaw) {
    b.lastRaw = raw;
    b.changed = nowMs;
  } else if (nowMs - b.changed >= BUTTON_DEBOUNCE_MS && raw != b.stable) {
    b.stable = raw;
    if (raw) b.edge = true;  // rising: not-pressed -> pressed — ขอบขาขึ้น: จากไม่กดเป็นกด
  }
}

static bool takeEdge(Btn& b) {
  bool e = b.edge;
  b.edge = false;
  return e;
}

// Seeded from the pins, not from `false`. The structs used to start as
// not-pressed, so a line already reading pressed at power-up looked like a
// transition and raised a press edge about one debounce later. START is NC, so
// an unplugged panel or a cut wire reads pressed forever — the machine would
// begin lifting a leg roughly 25 ms after power-up with nobody touching it.
// Taking the boot state as the baseline means a press has to be a release-then-
// press seen after startup, which a broken wire can never produce.
// [TH] ตั้งค่าเริ่มต้นจากค่าที่อ่านได้จริงจากขา ไม่ใช่กำหนดเป็น "ไม่ได้กด"
//      เดิมโครงสร้างเริ่มที่ไม่ได้กด ถ้าตอนเปิดเครื่องขาใดอ่านได้ว่ากดอยู่ จะถูกมองว่าเพิ่งเปลี่ยนสถานะ
//      แล้วเกิดขอบกดขึ้นหลังพ้นเวลากันเด้ง ปุ่ม START เป็นแบบปกติปิด สายที่ขาดหรือยังไม่ได้เสียบจะอ่านว่ากดค้าง
//      เครื่องจะเริ่มยกขาผู้ป่วยเองราว 25 มิลลิวินาทีหลังเปิดเครื่องโดยไม่มีใครแตะปุ่ม
//      การยึดสถานะตอนเปิดเครื่องเป็นค่าตั้งต้น ทำให้ต้องมีการปล่อยแล้วกดใหม่เท่านั้นจึงนับเป็นการกด
//      ซึ่งสายที่ขาดอยู่ทำไม่ได้
static void seed(Btn& b, uint32_t nowMs) {
  b.lastRaw = b.stable = rawPressed(b.pin, b.nc);
  b.changed = nowMs;
  b.edge = false;
}

void buttonsBegin() {
  pinMode(PIN_BTN_START, INPUT_PULLUP);
  pinMode(PIN_BTN_STOP, INPUT_PULLUP);
  pinMode(PIN_BTN_RESET, INPUT_PULLUP);
  delay(5);  // let the pullups pull the lines up before sampling — รอให้ตัวต้านทานดึงระดับขาขึ้นก่อนอ่าน
  uint32_t now = millis();
  seed(bStart, now);
  seed(bStop, now);
  seed(bReset, now);
}

void buttonsPoll(uint32_t nowMs) {
  poll(bStart, nowMs);
  poll(bStop, nowMs);
  poll(bReset, nowMs);
}

bool buttonStartPressed() { return takeEdge(bStart); }
bool buttonStopPressed() { return takeEdge(bStop); }
bool buttonResetPressed() { return takeEdge(bReset); }
