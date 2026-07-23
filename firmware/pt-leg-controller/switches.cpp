// กันเด้งสัญญาณลิมิตสวิตช์: ค่าจะถือว่านิ่งเมื่อค้างเท่าเดิมนานเกิน SWITCH_DEBOUNCE_MS
// Debounce logic: a reading counts as stable only after it holds past the window.
#include "switches.h"
#include <Arduino.h>
#include "config.h"

struct Deb {
  uint8_t pin;
  bool stable;      // debounced logical pressed — ค่าที่ผ่านกันเด้งแล้ว
  bool lastRaw;     // last raw logical read — ค่าดิบครั้งก่อน
  uint32_t changed; // ms of last raw change — เวลาที่ค่าดิบเปลี่ยนล่าสุด
};

static Deb top{PIN_SWITCH_TOP, false, false, 0};
static Deb bot{PIN_SWITCH_BOTTOM, false, false, 0};

static bool rawPressed(uint8_t pin) {
  int level = digitalRead(pin);
#ifdef SWITCH_NC
  return level == HIGH;  // normally-closed: pressed (or broken wire) reads open/HIGH
                         // [TH] แบบ NC: กด (หรือสายขาด) จะอ่านได้วงจรเปิด/HIGH
#else
  return level == LOW;   // normally-open to GND with pullup: pressed reads LOW
                         // [TH] แบบ NO ต่อลง GND กับ pullup: กดแล้วอ่านได้ LOW
#endif
}

static void poll(Deb& d, uint32_t nowMs) {
  // [TH] ถ้าค่าดิบเปลี่ยน ให้จับเวลาใหม่ ; ถ้าค้างเท่าเดิมครบเวลากันเด้งจึงยอมรับเป็นค่านิ่ง
  bool raw = rawPressed(d.pin);
  if (raw != d.lastRaw) {
    d.lastRaw = raw;
    d.changed = nowMs;
  } else if (nowMs - d.changed >= SWITCH_DEBOUNCE_MS) {
    d.stable = raw;
  }
}

void switchesBegin() {
  pinMode(PIN_SWITCH_TOP, INPUT_PULLUP);
  pinMode(PIN_SWITCH_BOTTOM, INPUT_PULLUP);
}

void switchesPoll(uint32_t nowMs) {
  poll(top, nowMs);
  poll(bot, nowMs);
}

bool switchTop() { return top.stable; }
bool switchBottom() { return bot.stable; }
