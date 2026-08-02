// สเก็ตช์หลักของบอร์ดคอนโทรลเลอร์ (ESP32) — สมองของเครื่องกายภาพบำบัดขา
// รวมทุกโมดูล: อ่านสวิตช์ -> เดินเครื่องสถานะ -> สั่งรีเลย์ -> คุยกับเซิร์ฟเวอร์และจอ
// Controller main sketch: switches -> state machine -> relays -> server + display.
//
// Bench aid: with no display wired the board says nothing, so panel-button
// polarity cannot be checked. Enable per-build to print button edges, raw pin
// levels, phase changes and reps on the USB port — UART0 is free, the display
// link lives on Serial2. Off for real hardware; enable per-build, never commit
// it on (see firmware/README.md):
//   --build-property compiler.cpp.extra_flags=-DBENCH_DEBUG=1 --clean
// [TH] โหมดทดสอบบนโต๊ะ: พิมพ์ขอบการกดปุ่ม ค่าดิบของขา สถานะ และจำนวนรอบ ออกทางพอร์ต USB
//      ใช้ตรวจขั้วปุ่มโดยไม่ต้องต่อจอ ใช้เฉพาะตอนทดสอบ ห้าม commit ทั้งค่าเปิดไว้
#ifndef BENCH_DEBUG
#define BENCH_DEBUG 0
#endif

#include "config.h"
#include "settings.h"
#include "actuator.h"
#include "switches.h"
#include "buttons.h"
#include "session.h"
#include "net.h"
#include "link.h"
#include "rtc.h"
#include <time.h>

DeviceSettings gSettings;  // ค่าตั้งค่าปัจจุบัน
SessionSM gSession;        // เครื่องสถานะของรอบการทำงาน
Link gLink;                // ลิงก์ UART ไปยังบอร์ดจอ

// The NTP sync is one-shot per boot: the DS3231 keeps time on its own from then
// on, and the machine is meant to work offline, so re-syncing forever would be
// noise. A power cut with the coin cell in place does not need it either.
// [TH] ซิงก์เวลาจาก NTP ครั้งเดียวต่อการเปิดเครื่อง เพราะ DS3231 เดินเวลาเองต่อได้
//      และเครื่องต้องใช้งานแบบออฟไลน์ได้ ไม่จำเป็นต้องซิงก์ซ้ำเรื่อยๆ
static bool gNtpDone = false;

static void applyActuator() {
  // [TH] แปลงสถานะปัจจุบันของรอบให้เป็นคำสั่งรีเลย์ (เรียกทุกลูป)
  //
  // The state machine raises this on every SafeLower entry, so the new stroke
  // does not inherit the previous one's energize clock. Asked of the session
  // rather than inferred from a phase change seen here: tick() runs before the
  // buttons, so a SafeLower can end and another begin between two looks at the
  // phase, and this code would have seen nothing change.
  // [TH] เครื่องสถานะยกสัญญาณนี้ทุกครั้งที่เข้าสู่สถานะลดขาเพื่อหยุด
  //      การเคลื่อนที่ครั้งใหม่จะได้ไม่รับช่วงเวลาจ่ายไฟของครั้งก่อนมา
  //      ถามจากเครื่องสถานะแทนการเดาจากการเปลี่ยนสถานะที่เห็นตรงนี้
  //      เพราะ tick() ทำงานก่อนปุ่ม การลดขาครั้งหนึ่งจึงจบและอีกครั้งเริ่มได้ระหว่างการมองสองครั้ง
  //      โค้ดตรงนี้จะไม่เห็นอะไรเปลี่ยนเลย
  if (gSession.takeStrokeRenewEdge()) actuatorRenewStroke();
  switch (gSession.phase()) {
    case Phase::Lift: actuatorUp(); break;
    case Phase::Lower:
    case Phase::SafeLower: actuatorDown(); break;
    default: actuatorOff(); break;  // Idle, Hold, Rest, Fault — สถานะที่ต้องดับรีเลย์
  }
}

static void applySettings(const DeviceSettings& s) {
  // [TH] รับค่าตั้งค่าใหม่: บันทึกลง NVS, อัปเดตเวลารอบ, ตั้งเพดานรีเลย์ (เผื่อ 500 มิลลิวินาที
  //      ให้สูงกว่าเพดานของเครื่องสถานะเล็กน้อย เพื่อเป็นแนวป้องกันสำรอง), แล้วเริ่มเครือข่ายใหม่
  gSettings = s;
  settingsSave(gSettings);
  gSession.cfg = {gSettings.maxTravelSec * 1000u, gSettings.holdSec * 1000u,
                  gSettings.restSec * 1000u};
  actuatorSetCap(gSettings.maxTravelSec * 1000u + 500u);
  netBegin(gSettings);
}

// The cycle times set the actuator's hard cap, so they are clamped here rather
// than trusted from the wire. Every sender clamps already — the spinboxes on the
// settings screen and the card reader on the display — but this board owns the
// relay: a maxTravel of 0 arriving down the UART would make the cap 0 ms, fault
// every stroke instantly, and persist to NVS so it survived the reboot. The
// ranges match the settings screen's spinbox limits, kept in step by hand.
// [TH] เวลารอบเป็นตัวกำหนดเพดานเวลาของรีเลย์ จึงจำกัดช่วงค่าที่นี่ ไม่เชื่อค่าที่ส่งมาตรงๆ
//      ฝั่งผู้ส่งจำกัดค่าให้อยู่แล้วทั้งช่องกรอกในหน้าตั้งค่าและตอนอ่านไฟล์จากการ์ด
//      แต่บอร์ดนี้เป็นเจ้าของรีเลย์ ถ้าค่า maxTravel เป็น 0 ส่งมาทาง UART เพดานจะเป็น 0 มิลลิวินาที
//      ทุกจังหวะยกจะผิดพลาดทันที และค่าจะถูกบันทึกลง NVS ค้างข้ามการเปิดปิดเครื่องด้วย
//      ช่วงตัวเลขตรงกับที่หน้าตั้งค่ากำหนดไว้ ต้องแก้ให้ตรงกันเองถ้ามีการเปลี่ยน
static uint16_t clampSec(uint16_t v, uint16_t lo, uint16_t hi, const char* name) {
  if (v < lo || v > hi) {
    Serial.printf("settings: %s=%u out of range %u..%u, clamped\n", name, v, lo, hi);
    return v < lo ? lo : hi;
  }
  return v;
}

// A settings message that arrives mid-session is held until the machine is idle.
// applySettings commits to NVS and restarts WiFi, both of which can stall for as
// long as a flash erase takes — and gLink.poll() runs after applyActuator() and
// actuatorTick(), so that stall happens with a relay energized and neither the
// limit switches nor the travel cap being read. It also swaps the live cycle
// timings out from under a stroke already in progress. Nothing is lost by
// waiting: the operator's SAVE lands the moment the leg is down.
// [TH] ข้อความตั้งค่าที่มาถึงระหว่างรอบทำงานจะถูกพักไว้จนกว่าเครื่องจะว่าง
//      applySettings เขียนลง NVS และเริ่มการเชื่อมต่อ WiFi ใหม่ ทั้งสองอย่างค้างได้นานเท่าการลบแฟลช
//      และ gLink.poll() ทำงานหลัง applyActuator() กับ actuatorTick()
//      การค้างนั้นจึงเกิดขึ้นตอนที่รีเลย์กำลังจ่ายไฟ โดยไม่มีการอ่านลิมิตสวิตช์และไม่มีการตรวจเพดานเวลาเลย
//      ทั้งยังสลับค่าเวลาของรอบที่กำลังเดินอยู่ด้วย
//      การรอไม่ทำให้เสียอะไร ค่าที่ผู้ใช้กดบันทึกจะมีผลทันทีที่ขาลงถึงพื้น
static bool gPendingSettings = false;
static DeviceSettings gPending;

static void onLinkSettings(const LinkSettings& s) {
  DeviceSettings d;
  d.wifiSsid = s.ssid; d.wifiPass = s.pass; d.serverUrl = s.server;
  d.deviceCode = s.code;
  d.maxTravelSec = clampSec(s.maxTravel, 1, 20, "maxTravelSec");
  d.holdSec = clampSec(s.hold, 1, 60, "holdSec");
  d.restSec = clampSec(s.rest, 1, 60, "restSec");
  if (gSession.running()) {
    gPending = d;
    gPendingSettings = true;
    Serial.println("settings: held until the session ends");
    // The HELD values are echoed, not the running ones. currentAsLink() would
    // report what the machine is still running — i.e. the values the operator
    // just replaced — so the settings screen would snap back to the old numbers
    // and read as a rejected save. The operator then re-saves what the screen
    // shows, which overwrites the pending values with the old ones, and the edit
    // is lost for good while the card file still holds it.
    // [TH] ส่งค่าที่พักไว้กลับไป ไม่ใช่ค่าที่กำลังใช้อยู่
    //      currentAsLink() จะรายงานค่าที่เครื่องยังใช้อยู่ ซึ่งคือค่าที่ผู้ใช้เพิ่งเปลี่ยนไปแล้ว
    //      หน้าตั้งค่าจะเด้งกลับไปเป็นตัวเลขเดิมและดูเหมือนว่าบันทึกไม่สำเร็จ
    //      ผู้ใช้จะกดบันทึกตามที่เห็นบนจออีกครั้ง ซึ่งไปเขียนทับค่าที่พักไว้ด้วยค่าเก่า
    //      ค่าที่แก้ไว้จะหายไปถาวรทั้งที่ไฟล์บนการ์ดยังเก็บไว้อยู่
    LinkSettings held;
    held.ssid = d.wifiSsid; held.pass = d.wifiPass; held.server = d.serverUrl;
    held.code = d.deviceCode;
    held.maxTravel = d.maxTravelSec; held.hold = d.holdSec; held.rest = d.restSec;
    gLink.sendSettings(held);
    return;
  }
  // A newer save supersedes one still waiting. Both land in the same loop
  // iteration when a session ends: poll() applies this one, and the pending block
  // further down would then write the OLDER values over them and persist those.
  // [TH] ค่าที่บันทึกใหม่กว่าจะแทนที่ค่าที่ยังรออยู่
  //      ทั้งสองมาถึงในลูปรอบเดียวกันได้ตอนที่รอบทำงานจบพอดี
  //      poll() จะนำค่านี้ไปใช้ แล้วบล็อกที่รออยู่ด้านล่างจะเขียนทับด้วยค่าเก่ากว่าและบันทึกค่านั้นแทน
  gPendingSettings = false;
  applySettings(d);
  gLink.sendSettings(currentAsLink());
}

static LinkSettings currentAsLink() {
  LinkSettings s;
  s.ssid = gSettings.wifiSsid; s.pass = gSettings.wifiPass;
  s.server = gSettings.serverUrl; s.code = gSettings.deviceCode;
  s.maxTravel = gSettings.maxTravelSec; s.hold = gSettings.holdSec;
  s.rest = gSettings.restSec;
  return s;
}

// [TH] เมื่อจอทักทาย (hello) ให้ส่งค่าตั้งค่าปัจจุบันกลับไป
static void onLinkHello() { gLink.sendSettings(currentAsLink()); }

#if BENCH_DEBUG
static const char* phaseName(Phase p) {
  switch (p) {
    case Phase::Idle: return "IDLE";
    case Phase::Lift: return "LIFT";
    case Phase::Hold: return "HOLD";
    case Phase::Lower: return "LOWER";
    case Phase::Rest: return "REST";
    case Phase::SafeLower: return "SAFE-LOWER";
    case Phase::Fault: return "FAULT";
  }
  return "?";
}

// The raw level alone means nothing without the button's contact type: HIGH is
// "pressed" on an NC button and "at rest" on an NO one. Print both so the
// polarity being tested is never inferred from the pin state.
// [TH] ค่าดิบอย่างเดียวบอกอะไรไม่ได้ถ้าไม่รู้ชนิดหน้าสัมผัส: HIGH คือ "กด" สำหรับ NC แต่คือ "ปกติ" สำหรับ NO
static const char* levelName(int level, bool nc) {
  bool pressed = nc ? (level == HIGH) : (level == LOW);
  if (level == HIGH) return pressed ? "HIGH/pressed" : "HIGH/rest";
  return pressed ? "LOW/pressed" : "LOW/rest";
}

// Everything here prints on change only. The loop runs hundreds of times a
// second, so a per-loop dump would bury the one line being looked for.
// Raw pin levels are printed alongside the debounced edges because polarity is
// the thing under test: an unwired NC input floats HIGH, which reads as a held
// press, and only the raw level makes that obvious.
// [TH] พิมพ์เฉพาะตอนค่าเปลี่ยน เพราะลูปเดินเร็วมาก ถ้าพิมพ์ทุกลูปจะท่วมจนหาบรรทัดที่ต้องการไม่เจอ
//      พิมพ์ค่าดิบของขาคู่กับขอบที่ผ่านกันเด้งแล้ว เพราะกำลังตรวจขั้ว: ขา NC ที่ไม่ได้ต่อจะลอยเป็น HIGH
//      ซึ่งอ่านได้เหมือนกดค้าง จะเห็นชัดก็ต่อเมื่อดูค่าดิบ
static void debugTick(uint32_t nowMs, bool startEdge, bool stopEdge, bool resetEdge) {
  static bool firstRun = true;
  static int lvStart = -1, lvStop = -1, lvReset = -1;
  static bool lastTop = false, lastBot = false;
  static Phase lastPhase = Phase::Idle;
  static uint16_t lastReps = 0;

  if (startEdge) Serial.printf("[dbg %lu] EDGE START\n", (unsigned long)nowMs);
  if (stopEdge) Serial.printf("[dbg %lu] EDGE STOP\n", (unsigned long)nowMs);
  if (resetEdge) Serial.printf("[dbg %lu] EDGE RESET\n", (unsigned long)nowMs);

  int s = digitalRead(PIN_BTN_START), t = digitalRead(PIN_BTN_STOP),
      r = digitalRead(PIN_BTN_RESET);
  if (firstRun || s != lvStart || t != lvStop || r != lvReset) {
    lvStart = s; lvStop = t; lvReset = r;
    Serial.printf("[dbg %lu] btn levels start=%s stop=%s reset=%s\n", (unsigned long)nowMs,
                  levelName(s, BTN_START_NC), levelName(t, BTN_STOP_NC),
                  levelName(r, BTN_RESET_NC));
  }

  bool top = switchTop(), bot = switchBottom();
  if (firstRun || top != lastTop || bot != lastBot) {
    lastTop = top; lastBot = bot;
    Serial.printf("[dbg %lu] limits top=%s bottom=%s\n", (unsigned long)nowMs,
                  top ? "AT-LIMIT" : "clear", bot ? "AT-LIMIT" : "clear");
  }

  // The clock is printed only when it crosses into "known", which is the moment
  // worth seeing: chip found, and either battery-backed or just NTP-set. A
  // per-tick print would be a new line every second and drown the bench.
  // [TH] พิมพ์เวลาเฉพาะตอนที่เปลี่ยนจาก "ไม่ทราบ" เป็น "ทราบ" ซึ่งเป็นจังหวะที่ต้องการเห็น
  //      ถ้าพิมพ์ทุกรอบจะได้บรรทัดใหม่ทุกวินาทีจนกลบข้อความอื่น
  static bool lastEpochKnown = false;
  uint32_t epoch = rtcEpoch();
  if (firstRun || (epoch != 0) != lastEpochKnown) {
    lastEpochKnown = (epoch != 0);
    Serial.printf("[dbg %lu] rtc %s epoch=%lu\n", (unsigned long)nowMs,
                  rtcPresent() ? "found" : "ABSENT", (unsigned long)epoch);
  }

  // A press always reprints the state, even when nothing moved. RESET pressed on
  // an already-clear session changes neither phase nor reps, and a change-only
  // print would leave the press looking like it did nothing at all.
  // [TH] ทุกครั้งที่กดปุ่มจะพิมพ์สถานะซ้ำเสมอ แม้ค่าไม่เปลี่ยน เพราะกด RESET ตอนที่ค่าเป็นศูนย์อยู่แล้ว
  //      จะไม่มีอะไรเปลี่ยน ถ้าพิมพ์เฉพาะตอนเปลี่ยนจะดูเหมือนการกดไม่มีผล
  bool pressed = startEdge || stopEdge || resetEdge;
  if (firstRun || pressed || gSession.phase() != lastPhase || gSession.reps() != lastReps) {
    lastPhase = gSession.phase();
    lastReps = gSession.reps();
    Serial.printf("[dbg %lu] phase=%s reps=%u elapsed=%lus\n", (unsigned long)nowMs,
                  phaseName(lastPhase), lastReps,
                  (unsigned long)gSession.elapsedSec(nowMs));
  }
  firstRun = false;
}
#endif

void setup() {
  actuatorBegin();  // safety first — ดับรีเลย์ก่อนเป็นอันดับแรกเพื่อความปลอดภัย
  switchesBegin();
  buttonsBegin();
  rtcBegin();  // absent chip is fine: epochs just read 0 — ไม่มีชิปก็ไม่เป็นไร เวลาจะเป็น 0
  Serial.begin(115200);
  delay(200);  // let UART0 settle so the boot banner is not clipped — รอ UART0 นิ่งกันข้อความต้นเปิดเครื่องขาด
  Serial2.begin(115200, SERIAL_8N1, 16, 17);  // UART to CYD — สาย UART ไปบอร์ดจอ (RX=16, TX=17)
  // Where the leg actually is, before anything can be commanded. The debounce
  // needs two samples 25 ms apart, so it is polled until it settles. A board that
  // boots after a power cut mid-lift has the leg in the air, and without this it
  // would believe otherwise and refuse to lower it — the one state with no way
  // down.
  // [TH] ตำแหน่งจริงของขา ก่อนที่จะสั่งอะไรได้
  //      กลไกกันเด้งต้องอ่านสองครั้งห่างกัน 25 มิลลิวินาที จึงอ่านวนจนค่านิ่ง
  //      บอร์ดที่เปิดขึ้นมาหลังไฟดับกลางจังหวะยกขาจะมีขาค้างอยู่กลางอากาศ
  //      ถ้าไม่ทำแบบนี้ เครื่องจะเชื่อเป็นอย่างอื่นและไม่ยอมลดขาลง ซึ่งเป็นสถานะเดียวที่ไม่มีทางเอาขาลง
  for (uint32_t t0 = millis(); millis() - t0 < 60;) switchesPoll(millis());
  gSession.seedLegPosition(switchBottom(), switchTop());
  Serial.printf("leg at boot: top=%d bottom=%d\n", switchTop() ? 1 : 0, switchBottom() ? 1 : 0);

  settingsLoad(gSettings);
  gSession.cfg = {gSettings.maxTravelSec * 1000u, gSettings.holdSec * 1000u,
                  gSettings.restSec * 1000u};
  actuatorSetCap(gSettings.maxTravelSec * 1000u + 500u);
  netBegin(gSettings);
  // UTC, no DST rule: every epoch on the wire is UTC and the CYD adds the Thai
  // offset only when it formats a datetime for the log.
  // [TH] ตั้งเป็น UTC ล้วน ไม่ใส่เขตเวลา เพราะเวลาที่ส่งกันเป็น UTC ทั้งหมด
  //      ฝั่งจอค่อยบวกเวลาไทยตอนเขียนลงไฟล์
  configTime(0, 0, "pool.ntp.org", "time.google.com");
  if (!actuatorCapTimerReady())
    Serial.println("actuator: cap timer missing, the hardware backstop is OFF");
  gLink.begin(&Serial2);
  gLink.onSettings = onLinkSettings;
  gLink.onHello = onLinkHello;
  Serial.printf("pt-leg-controller %s ssid=%s server=%s code=%s cycle max/hold/rest=%u/%u/%u\n",
                FW_VERSION, gSettings.wifiSsid.c_str(), gSettings.serverUrl.c_str(),
                gSettings.deviceCode.c_str(), gSettings.maxTravelSec, gSettings.holdSec,
                gSettings.restSec);
#if BENCH_DEBUG
  // ASCII only: an em dash here reaches a serial monitor as mojibake.
  Serial.printf("debug prints ON - btn start=%d(%s) stop=%d(%s) reset=%d(%s), limits=%s\n",
                PIN_BTN_START, BTN_START_NC ? "NC" : "NO",
                PIN_BTN_STOP, BTN_STOP_NC ? "NC" : "NO",
                PIN_BTN_RESET, BTN_RESET_NC ? "NC" : "NO",
#ifdef SWITCH_NC
                "NC");
#else
                "NO");
#endif
#endif
}

void loop() {
  // [TH] ลำดับสำคัญ: อ่านสวิตช์ -> เดินเครื่องสถานะ -> สั่งรีเลย์ -> ตรวจเพดานเวลา ก่อนงานเครือข่าย
  uint32_t now = millis();
  switchesPoll(now);
  buttonsPoll(now);
  gSession.tick(now, switchTop(), switchBottom());
  // panel buttons: START (idle only), STOP (any state), RESET (idle only)
  // Read all three first: each accessor clears its edge, so the debug build has
  // to see the same values this loop acts on.
  // [TH] ปุ่มหน้าปัด: START เริ่ม (เฉพาะว่าง), STOP หยุด (ตลอดเวลา), RESET ล้าง (เฉพาะว่าง)
  //      อ่านทั้งสามก่อน เพราะการอ่านจะล้างขอบทิ้ง โหมดดีบักต้องเห็นค่าชุดเดียวกับที่ลูปใช้
  bool pressStart = buttonStartPressed();
  bool pressStop = buttonStopPressed();
  bool pressReset = buttonResetPressed();
  // START is refused while the STOP line looks broken. On an NC button an open
  // circuit reads pressed forever, which means STOP can never raise an edge — so
  // the session would run with no way to stop it from the panel, on a machine
  // where the panel is the primary stop. Better to not start at all.
  // [TH] ไม่ให้เริ่มรอบถ้าสาย STOP ดูเหมือนขาด ปุ่มแบบปกติปิดที่วงจรเปิดจะอ่านได้ว่ากดค้างตลอด
  //      แปลว่าปุ่ม STOP จะไม่มีวันเกิดขอบสัญญาณ รอบนั้นจะเดินโดยไม่มีทางหยุดจากหน้าปัดได้เลย
  //      บนเครื่องที่ใช้หน้าปัดเป็นตัวหยุดหลัก ไม่เริ่มเลยดีกว่า
  if (pressStart && buttonStopWireSuspect()) {
    Serial.println("start refused: STOP has read pressed since power-up, check its wiring");
  } else if (pressStart) {
    gSession.start(now);
  }
  if (pressStop) gSession.requestStop(now);
  if (pressReset) gSession.reset(now);
  applyActuator();
  actuatorTick(now);
  // backstop: the relays stayed energized past the cap and the state machine
  // never commanded OFF. Reported here, where the flag is consumed, so the line
  // is printed exactly once per trip and cannot be swallowed by an actuatorOff()
  // earlier in this same iteration.
  // [TH] แนวป้องกันสำรอง รีเลย์จ่ายไฟเกินเพดานเวลาและเครื่องสถานะไม่ได้สั่งดับ
  //      รายงานตรงนี้ซึ่งเป็นจุดที่อ่านและล้างค่า ข้อความจึงถูกพิมพ์ครั้งเดียวต่อการตัดหนึ่งครั้ง
  //      และไม่ถูกกลืนโดย actuatorOff() ที่ทำงานก่อนหน้าในลูปรอบเดียวกัน
  if (actuatorTakeCapTrip()) {
    Serial.println("actuator: energize cap opened the relays");
    // Not while a safe-lower is running. The trip can surface in the same
    // iteration in which STOP entered SafeLower — a long loop stall latches both
    // — and faulting there would abort the descent the operator just asked for
    // before the DOWN relay had energized at all, leaving the leg where it was.
    // The safe-lower has its own fresh cap from the stroke renew, so it is
    // already bounded; let it run.
    // [TH] ไม่ทำระหว่างที่กำลังลดขาเพื่อหยุด
    //      การตัดอาจปรากฏในลูปรอบเดียวกับที่การกด STOP พาเข้าสู่สถานะลดขาเพื่อหยุด
    //      เพราะลูปที่ค้างนานจะทำให้ทั้งสองอย่างค้างมาพร้อมกัน
    //      การตัดสินว่าผิดพลาดตรงนั้นจะยกเลิกการลดขาที่ผู้ใช้เพิ่งสั่ง ตั้งแต่ก่อนที่รีเลย์ลงจะได้จ่ายไฟด้วยซ้ำ
    //      แล้วขาจะค้างอยู่ที่เดิม การลดขาเพื่อหยุดมีเพดานเวลาของตัวเองจากการเริ่มจับเวลาใหม่อยู่แล้ว
    //      จึงมีขอบเขตอยู่แล้ว ปล่อยให้ทำงานต่อไป
    if (gSession.phase() != Phase::SafeLower) gSession.forceFault(now);
  }
  netLoop(now);
  // One-shot NTP -> RTC. time(nullptr) returns a small number until SNTP has
  // answered, so the threshold is what distinguishes "clock not set yet" from a
  // real time; writing the former into the DS3231 would replace a good
  // battery-backed time with 1970.
  // [TH] ซิงก์เวลาจากอินเทอร์เน็ตลงชิปนาฬิกาครั้งเดียว ก่อน SNTP ตอบ เวลาจะเป็นค่าน้อยๆ
  //      จึงต้องเช็กเกณฑ์ก่อน ไม่งั้นจะเอาปี 1970 ไปทับเวลาจริงที่ชิปเก็บไว้
  // Marked done only when the chip actually took the write. Setting the flag on
  // the attempt meant one I2C hiccup — or a board with no chip fitted, where the
  // write is a no-op — burned the only sync of the boot, and a DS3231 sitting
  // there unset stayed unset all day.
  // [TH] ถือว่าเสร็จเฉพาะตอนที่ชิปรับค่าไปจริง ถ้าตั้งค่าสถานะตั้งแต่ตอนที่ลองเขียน
  //      สัญญาณ I2C สะดุดครั้งเดียว หรือบอร์ดที่ยังไม่ได้ติดชิปซึ่งการเขียนไม่เกิดอะไรขึ้น
  //      จะเผาการซิงก์ครั้งเดียวของการเปิดเครื่องนั้นทิ้ง และชิปที่ยังไม่ได้ตั้งเวลาก็จะไม่ถูกตั้งทั้งวัน
  if (!gNtpDone && netWifiUp()) {
    time_t t = time(nullptr);
    if (t > 1700000000) gNtpDone = rtcSetFromEpoch((uint32_t)t);  // ~2023-11
  }
  gLink.poll();

  // UART-loss safe-stop: the screen may be dead and no touch-stop is possible, so
  // a link that goes quiet mid-session lowers the leg and stops.
  //
  // Armed by the first line ever received, deliberately. A board that has never
  // heard from a display is running standalone — the documented bench setup, and
  // a machine whose CYD is unplugged — where the panel STOP button is the primary
  // stop and works regardless. Arming from boot instead would make the condition
  // permanently true on those machines: START and requestStop would land in the
  // same loop iteration and no session could ever run. See "Running the
  // controller without a display" in firmware/README.md.
  // [TH] เริ่มนับจากข้อความแรกที่ได้รับ ซึ่งเป็นความตั้งใจ
  //      บอร์ดที่ไม่เคยได้ยินเสียงจากจอเลยคือบอร์ดที่ใช้งานเดี่ยว ทั้งชุดทดสอบตามที่บันทึกไว้
  //      และเครื่องที่ถอดสายจอออก กรณีนั้นปุ่ม STOP หน้าปัดเป็นตัวหยุดหลักและใช้ได้อยู่แล้ว
  //      ถ้าเริ่มนับตั้งแต่เปิดเครื่อง เงื่อนไขจะเป็นจริงตลอดเวลาบนเครื่องเหล่านั้น
  //      คำสั่งเริ่มและคำสั่งหยุดจะตกอยู่ในลูปรอบเดียวกัน และจะไม่มีรอบใดเดินได้เลย
  //      ดูหัวข้อการใช้คอนโทรลเลอร์โดยไม่มีจอใน firmware/README.md
  //
  // Signed, for the same reason the actuator cap is: `now` was captured at the
  // top of this iteration, but gLink.poll() above stamps lastRx with a FRESH
  // millis(), and netLoop() before it can block for the full 400 ms connect
  // timeout against an unreachable server. A ping that lands inside that block is
  // timestamped later than `now`, and unsigned that subtraction is ~4.29e9 — past
  // 2000 — so a perfectly healthy link safe-stopped the session about a second
  // after START, over and over.
  // [TH] ใช้การเทียบแบบมีเครื่องหมายด้วยเหตุผลเดียวกับเพดานเวลาของชุดขับเคลื่อน
  //      ค่า now อ่านไว้ตั้งแต่ต้นรอบ แต่ gLink.poll() ด้านบนบันทึกเวลารับล่าสุดด้วย millis() ค่าใหม่
  //      และ netLoop() ก่อนหน้านั้นค้างได้นานถึง 400 มิลลิวินาทีเมื่อติดต่อเซิร์ฟเวอร์ไม่ได้
  //      ข้อความ ping ที่มาถึงระหว่างที่ค้างอยู่จะมีเวลาช้ากว่าค่า now
  //      ถ้าลบแบบไม่มีเครื่องหมายจะได้ราวสี่พันล้าน ซึ่งเกิน 2000
  //      สายที่ปกติดีจึงสั่งหยุดรอบการทำงานราวหนึ่งวินาทีหลังกดเริ่ม ซ้ำแล้วซ้ำอีก
  uint32_t lastRx = gLink.lastRxMs();
  if (gSession.running() && lastRx != 0 && (int32_t)(now - lastRx) > 2000)
    gSession.requestStop(now);

  static uint32_t lastPoll = 0, lastHb = 0, lastState = 0;
  if (now - lastPoll >= 1000) {  // [TH] ดึงคำสั่งจากเซิร์ฟเวอร์ทุก 1 วินาที
    lastPoll = now;
    String cmd = netPollCommand();
    // The server's start is held to the same condition as the panel's: a machine
    // whose STOP button cannot raise an edge must not begin a session, and it
    // makes no difference who asked. Stop is never refused.
    // [TH] คำสั่งเริ่มจากเซิร์ฟเวอร์ใช้เงื่อนไขเดียวกับปุ่มบนหน้าปัด
    //      เครื่องที่ปุ่ม STOP ไม่สามารถสร้างขอบสัญญาณได้ต้องไม่เริ่มรอบใหม่ ไม่ว่าใครจะเป็นคนสั่ง
    //      ส่วนคำสั่งหยุดไม่เคยถูกปฏิเสธ
    if (cmd == "start" && buttonStopWireSuspect())
      Serial.println("server start refused: STOP reads pressed, check its wiring");
    else if (cmd == "start") gSession.start(now);
    else if (cmd == "stop") gSession.requestStop(now);
  }
  // [TH] ส่งสัญญาณชีพถี่ขึ้นตอนทำงาน (ทุก 1 วิ) และห่างตอนว่าง (ทุก 5 วิ)
  uint32_t hbInterval = gSession.running() ? 1000 : 5000;
  if (now - lastHb >= hbInterval) {
    lastHb = now;
    netHeartbeat(gSession.running() ? "running" : "idle", gSession.elapsedSec(now),
                 gSession.reps());
  }
  // [TH] ส่งเหตุการณ์เมื่อ "เพิ่งเริ่ม" หรือ "เพิ่งหยุด" (ตรวจจับขอบสัญญาณครั้งเดียว)
  if (gSession.takeStartedEdge()) netQueueEvent("started", gSession.elapsedSec(now), gSession.reps());
  // The same edge feeds two independent records: the server event (online) and
  // the CYD's SD row (offline). Neither depends on the other succeeding.
  // [TH] ขอบเดียวกันนี้ใช้บันทึกสองที่ที่ไม่ขึ้นต่อกัน: เหตุการณ์ฝั่งเซิร์ฟเวอร์ (ตอนออนไลน์)
  //      กับแถวข้อมูลใน SD ของจอ (ตอนออฟไลน์)
  if (gSession.takeStoppedEdge()) {
    netQueueEvent("stopped", gSession.finalElapsedSec(), gSession.finalReps());
    // The clock chip first, then NTP. Taking the epoch from the DS3231 alone meant
    // that on a machine with no chip fitted — which is every machine until the RTC
    // is wired — every session on the card got "?" as its datetime, even with WiFi
    // up and the system clock correct. The same threshold as the sync above tells
    // "SNTP has not answered yet" from a real time.
    // [TH] เอาเวลาจากชิปนาฬิกาก่อน ถ้าไม่มีค่อยใช้เวลาจากอินเทอร์เน็ต
    //      เดิมใช้ค่าจาก DS3231 อย่างเดียว เครื่องที่ยังไม่ได้ติดชิป ซึ่งคือทุกเครื่องตอนนี้
    //      จึงบันทึกวันเวลาเป็น "?" ทุกรอบ ทั้งที่ต่อ WiFi ได้และเวลาในระบบถูกต้อง
    //      ใช้เกณฑ์เดียวกับตอนซิงก์ด้านบนเพื่อแยก "SNTP ยังไม่ตอบ" ออกจากเวลาจริง
    uint32_t epoch = rtcEpoch();
    if (epoch == 0) {
      time_t t = time(nullptr);
      if (t > 1700000000) epoch = (uint32_t)t;
    }
    LinkDone d{gSession.finalElapsedSec(), gSession.finalReps(), epoch, gSession.finalFaulted()};
    gLink.sendDone(d);
  }

  // Settings that arrived mid-session land here, once the relays are off. After
  // the done message on purpose: applySettings restarts WiFi, and the session's
  // record should be on its way out before that happens.
  // [TH] ค่าตั้งค่าที่มาถึงระหว่างรอบจะถูกนำมาใช้ตรงนี้ หลังจากรีเลย์ดับแล้ว
  //      วางไว้หลังข้อความ done โดยตั้งใจ เพราะ applySettings จะเริ่มการเชื่อมต่อ WiFi ใหม่
  //      ควรให้ข้อมูลของรอบนั้นถูกส่งออกไปก่อน
  if (gPendingSettings && !gSession.running()) {
    gPendingSettings = false;
    Serial.println("settings: applying the ones held during the session");
    applySettings(gPending);
    gLink.sendSettings(currentAsLink());  // confirm what actually took effect
                                          // [TH] ยืนยันค่าที่มีผลใช้งานจริง
  }

  if (now - lastState >= 250) {  // [TH] ส่งสถานะให้จอ ~4 ครั้งต่อวินาที
    lastState = now;
    LinkState st{gSession.running(), (int)gSession.phase(), gSession.elapsedSec(now),
                 gSession.reps(), netWifiUp(), netServerUp()};
    gLink.sendState(st);
  }
#if BENCH_DEBUG
  debugTick(now, pressStart, pressStop, pressReset);
#endif
  delay(2);
}
