// การจ่ายไฟรีเลย์ยก/ลดขา พร้อมกลไกความปลอดภัย "ตัดก่อนต่อ" และเพดานเวลาจ่ายไฟ
// Relay drive for up/down with break-before-make and a max-energize time cap.
#include "actuator.h"
#include <Arduino.h>
#include "config.h"

#ifdef RELAY_ACTIVE_LOW
static const int ON = LOW, OFF = HIGH;
#else
static const int ON = HIGH, OFF = LOW;
#endif
// [TH] ON/OFF คือระดับลอจิกที่จ่าย/ตัดรีเลย์ กลับด้านตามชนิดบอร์ด (active-low หรือไม่)

static ActState state = ACT_OFF;
static uint32_t capMs = 0, energizeStart = 0;  // เพดานเวลา และเวลาที่เริ่มจ่ายไฟ
static bool capTripped = false;                // ธงว่าเพดานเวลาถูกตัดไปแล้ว

static void write(int up, int down) {
  // break-before-make: always de-energize both before energizing one
  // [TH] ตัดก่อนต่อ: ดับรีเลย์ทั้งคู่ก่อนเสมอ แล้วค่อยจ่ายไฟด้านที่ต้องการ
  digitalWrite(PIN_RELAY_UP, OFF);
  digitalWrite(PIN_RELAY_DOWN, OFF);
  if (up == ON) digitalWrite(PIN_RELAY_UP, ON);
  if (down == ON) digitalWrite(PIN_RELAY_DOWN, ON);
}

void actuatorBegin() {
  // [TH] ตั้งขาเป็นเอาต์พุตและบังคับรีเลย์ดับทั้งคู่ ต้องเรียกเป็นอย่างแรกใน setup()
  pinMode(PIN_RELAY_UP, OUTPUT);
  pinMode(PIN_RELAY_DOWN, OUTPUT);
  write(OFF, OFF);
  state = ACT_OFF;
}

void actuatorUp() {
  // Idempotent: applyActuator() calls this every loop while lifting. Re-stamping
  // energizeStart here would keep nowMs-energizeStart near zero forever and the
  // travel cap could never trip. Only stamp on the OFF/DOWN -> UP transition.
  // [TH] ทำซ้ำได้ปลอดภัย: applyActuator() เรียกทุกลูปตอนยกขา ถ้าจับเวลา energizeStart
  //      ใหม่ทุกครั้ง ค่า nowMs-energizeStart จะเกือบศูนย์ตลอด เพดานเวลาจะไม่มีวันตัด
  //      จึงจับเวลาเฉพาะตอนเปลี่ยนสถานะจาก OFF/DOWN -> UP เท่านั้น
  if (state == ACT_UP) return;
  write(ON, OFF);
  state = ACT_UP;
  energizeStart = millis();
  capTripped = false;
}

void actuatorDown() {
  if (state == ACT_DOWN) return;  // see actuatorUp(): stamp only on transition
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

void actuatorSetCap(uint32_t ms) { capMs = ms; }  // ตั้งเพดานเวลาจ่ายไฟสูงสุด

void actuatorTick(uint32_t nowMs) {
  // [TH] ตรวจทุกลูป: ถ้าจ่ายไฟค้างเกินเพดานเวลา ให้ตัดรีเลย์และตั้งธง capTripped
  if (state == ACT_OFF || capMs == 0) return;
  if (nowMs - energizeStart >= capMs) {
    write(OFF, OFF);  // OFF here is the file-scope relay-off level, not ACT_OFF
                      // [TH] OFF ตรงนี้คือระดับลอจิกดับรีเลย์ ไม่ใช่สถานะ ACT_OFF
    state = ACT_OFF;
    capTripped = true;
  }
}

bool actuatorCapTripped() { return capTripped; }
