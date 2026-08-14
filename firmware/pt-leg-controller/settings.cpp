// อ่าน/เขียนค่าตั้งค่าลงหน่วยความจำถาวร NVS ผ่านไลบรารี Preferences
// Load/save device settings to NVS via the Preferences library.
#include "settings.h"
#include <Preferences.h>
#include "config.h"

static const char* NS = "ptleg";  // ชื่อเนมสเปซใน NVS

// The cycle times come back through the same clamp the UART path uses, because a
// Preferences default only applies when the key is ABSENT — a key present and
// holding 0 is returned as 0. That 0 becomes cfg.maxTravelMs, and a safe-lower
// then ends on its first tick before the DOWN relay has energized: every STOP and
// every RESET turns into an instant no-motion fault, and the leg cannot be
// lowered by any button. The actuator cap would be 500 ms, so the backstop cannot
// help either. A 0 can get in there from an older firmware, a partial write, or a
// flash image from another build.
// [TH] เวลาของรอบต้องผ่านการจำกัดช่วงชุดเดียวกับที่ทาง UART ใช้
//      เพราะค่าตั้งต้นของ Preferences จะถูกใช้เฉพาะตอนที่ "ไม่มีคีย์" เท่านั้น
//      ถ้ามีคีย์อยู่และเก็บค่า 0 ไว้ ก็จะได้ 0 กลับมา
//      ค่า 0 นั้นจะกลายเป็น cfg.maxTravelMs แล้วการลดขาเพื่อหยุดจะจบตั้งแต่จังหวะแรก
//      ก่อนที่รีเลย์ลงจะได้จ่ายไฟด้วยซ้ำ ทุกการกด STOP และ RESET จะกลายเป็นความผิดพลาดทันทีโดยไม่มีการเคลื่อนที่
//      และจะเอาขาลงด้วยปุ่มใดไม่ได้เลย เพดานเวลาของชุดขับเคลื่อนจะเหลือ 500 มิลลิวินาที จึงช่วยอะไรไม่ได้
//      ค่า 0 เข้าไปอยู่ในนั้นได้จากเฟิร์มแวร์รุ่นเก่า การเขียนที่ไม่สมบูรณ์ หรืออิมเมจแฟลชจากบิลด์อื่น
// Out of range falls back to the compiled default, not to the nearest bound.
// Clamping a stored 0 up to the minimum would give maxTravel 1 second, which is a
// legal value and a useless one: every lift faults after a second and every
// safe-lower buys a second of descent, so bringing the leg down takes eight
// alternating presses instead of one. A value this far out is corruption, not a
// preference, and the default is the better guess.
// [TH] ค่าที่อยู่นอกช่วงจะกลับไปใช้ค่าตั้งต้นที่คอมไพล์มา ไม่ใช่ค่าขอบที่ใกล้ที่สุด
//      การบีบค่า 0 ขึ้นมาเป็นค่าต่ำสุดจะได้เวลาเดินทางสูงสุดหนึ่งวินาที ซึ่งเป็นค่าที่ถูกกฎแต่ใช้งานไม่ได้
//      การยกขาทุกครั้งจะผิดพลาดหลังหนึ่งวินาที และการลดขาเพื่อหยุดก็ได้เวลาแค่หนึ่งวินาที
//      การเอาขาลงจึงต้องกดสลับกันแปดครั้งแทนที่จะกดครั้งเดียว
//      ค่าที่ผิดไปไกลขนาดนี้คือข้อมูลเสีย ไม่ใช่ค่าที่ตั้งใจตั้งไว้ ค่าตั้งต้นจึงเป็นการเดาที่ดีกว่า
static uint16_t clampSec(uint16_t v, uint16_t lo, uint16_t hi, uint16_t dflt, const char* name) {
  if (v < lo || v > hi) {
    Serial.printf("settings: stored %s=%u out of range %u..%u, using default %u\n", name, v, lo,
                  hi, dflt);
    return dflt;
  }
  return v;
}

void settingsLoad(DeviceSettings& s) {
  // [TH] เปิดแบบอ่านอย่างเดียว (true) ; ถ้าไม่มีคีย์ให้คืนค่าตั้งต้นจาก config.h
  Preferences p;
  p.begin(NS, true);
  s.wifiSsid = p.getString("ssid", "");
  s.wifiPass = p.getString("pass", "");
  s.serverUrl = p.getString("server", "");
  s.deviceCode = p.getString("code", DEFAULT_DEVICE_CODE);
  s.maxTravelSec = clampSec(p.getUShort("maxtravel", DEFAULT_MAX_TRAVEL_SEC), 1, 20,
                            DEFAULT_MAX_TRAVEL_SEC, "maxtravel");
  s.liftSec = clampSec(p.getUShort("lift", DEFAULT_LIFT_SEC), 1, s.maxTravelSec, DEFAULT_LIFT_SEC,
                       "lift");
  s.lowerSec = clampSec(p.getUShort("lower", DEFAULT_LOWER_SEC), 1, s.maxTravelSec,
                        DEFAULT_LOWER_SEC, "lower");
  s.holdSec = clampSec(p.getUShort("hold", DEFAULT_HOLD_SEC), 1, 60, DEFAULT_HOLD_SEC, "hold");
  s.restSec = clampSec(p.getUShort("rest", DEFAULT_REST_SEC), 1, 60, DEFAULT_REST_SEC, "rest");
  s.targetReps = clampSec(p.getUShort("target", DEFAULT_TARGET_REPS), 0, 99, DEFAULT_TARGET_REPS,
                          "target");
  p.end();
}

void settingsSave(const DeviceSettings& s) {
  // [TH] เปิดแบบเขียนได้ (false) แล้วบันทึกทุกฟิลด์ลง NVS
  Preferences p;
  p.begin(NS, false);
  p.putString("ssid", s.wifiSsid);
  p.putString("pass", s.wifiPass);
  p.putString("server", s.serverUrl);
  p.putString("code", s.deviceCode);
  p.putUShort("maxtravel", s.maxTravelSec);
  p.putUShort("lift", s.liftSec);
  p.putUShort("lower", s.lowerSec);
  p.putUShort("hold", s.holdSec);
  p.putUShort("rest", s.restSec);
  p.putUShort("target", s.targetReps);
  p.end();
}
