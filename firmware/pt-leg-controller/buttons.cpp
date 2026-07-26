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

void buttonsBegin() {
  pinMode(PIN_BTN_START, INPUT_PULLUP);
  pinMode(PIN_BTN_STOP, INPUT_PULLUP);
  pinMode(PIN_BTN_RESET, INPUT_PULLUP);
}

void buttonsPoll(uint32_t nowMs) {
  poll(bStart, nowMs);
  poll(bStop, nowMs);
  poll(bReset, nowMs);
}

bool buttonStartPressed() { return takeEdge(bStart); }
bool buttonStopPressed() { return takeEdge(bStop); }
bool buttonResetPressed() { return takeEdge(bReset); }
