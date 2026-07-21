#include "actuator.h"
#include <Arduino.h>
#include "config.h"

#ifdef RELAY_ACTIVE_LOW
static const int ON = LOW, OFF = HIGH;
#else
static const int ON = HIGH, OFF = LOW;
#endif

static ActState state = ACT_OFF;
static uint32_t capMs = 0, energizeStart = 0;
static bool capTripped = false;

static void write(int up, int down) {
  // break-before-make: always de-energize both before energizing one
  digitalWrite(PIN_RELAY_UP, OFF);
  digitalWrite(PIN_RELAY_DOWN, OFF);
  if (up == ON) digitalWrite(PIN_RELAY_UP, ON);
  if (down == ON) digitalWrite(PIN_RELAY_DOWN, ON);
}

void actuatorBegin() {
  pinMode(PIN_RELAY_UP, OUTPUT);
  pinMode(PIN_RELAY_DOWN, OUTPUT);
  write(OFF, OFF);
  state = ACT_OFF;
}

void actuatorUp() {
  write(ON, OFF);
  state = ACT_UP;
  energizeStart = millis();
  capTripped = false;
}

void actuatorDown() {
  write(OFF, ON);
  state = ACT_DOWN;
  energizeStart = millis();
  capTripped = false;
}

void actuatorOff() {
  write(OFF, OFF);
  state = ACT_OFF;
}

ActState actuatorState() { return state; }

void actuatorSetCap(uint32_t ms) { capMs = ms; }

void actuatorTick(uint32_t nowMs) {
  if (state == ACT_OFF || capMs == 0) return;
  if (nowMs - energizeStart >= capMs) {
    write(OFF, OFF);  // OFF here is the file-scope relay-off level, not ACT_OFF
    state = ACT_OFF;
    capTripped = true;
  }
}

bool actuatorCapTripped() { return capTripped; }
