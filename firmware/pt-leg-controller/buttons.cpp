// กันเด้งปุ่มแบบเดียวกับ switches.cpp แล้วยกธง "เพิ่งกด" หนึ่งครั้งเมื่อค่านิ่งเปลี่ยนจากไม่กด->กด
// Debounce like switches.cpp, then raise a one-shot "just pressed" flag when the
// stable state transitions not-pressed -> pressed.
#include "buttons.h"
#include <Arduino.h>
#include "config.h"

struct Btn {
  uint8_t pin;
  bool stable;      // debounced logical pressed — ค่าที่ผ่านกันเด้งแล้ว
  bool lastRaw;     // last raw logical read — ค่าดิบครั้งก่อน
  uint32_t changed; // ms of last raw change — เวลาที่ค่าดิบเปลี่ยนล่าสุด
  bool edge;        // one-shot press edge, cleared on read — ขอบกดครั้งเดียว อ่านแล้วล้าง
};

static Btn bStart{PIN_BTN_START, false, false, 0, false};
static Btn bStop{PIN_BTN_STOP, false, false, 0, false};
static Btn bReset{PIN_BTN_RESET, false, false, 0, false};

static bool rawPressed(uint8_t pin) {
  int level = digitalRead(pin);
#ifdef SWITCH_NC
  return level == HIGH;  // NC: pressed (or broken wire) reads open/HIGH — แบบ NC กด/สายขาด = HIGH
#else
  return level == LOW;   // NO to GND with pullup: pressed reads LOW — แบบ NO ลง GND = LOW
#endif
}

static void poll(Btn& b, uint32_t nowMs) {
  bool raw = rawPressed(b.pin);
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
