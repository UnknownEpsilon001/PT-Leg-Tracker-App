#include "switches.h"
#include <Arduino.h>
#include "config.h"

struct Deb {
  uint8_t pin;
  bool stable;      // debounced logical pressed
  bool lastRaw;     // last raw logical read
  uint32_t changed; // ms of last raw change
};

static Deb top{PIN_SWITCH_TOP, false, false, 0};
static Deb bot{PIN_SWITCH_BOTTOM, false, false, 0};

static bool rawPressed(uint8_t pin) {
  int level = digitalRead(pin);
#ifdef SWITCH_NC
  return level == HIGH;  // normally-closed: pressed (or broken wire) reads open/HIGH
#else
  return level == LOW;   // normally-open to GND with pullup: pressed reads LOW
#endif
}

static void poll(Deb& d, uint32_t nowMs) {
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
