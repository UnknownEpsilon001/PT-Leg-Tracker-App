// สเก็ตช์หลักของบอร์ดคอนโทรลเลอร์ (ESP32) — สมองของเครื่องกายภาพบำบัดขา
// รวมทุกโมดูล: อ่านปุ่ม -> เดินเครื่องสถานะ -> สั่งมอเตอร์ -> คุยกับเซิร์ฟเวอร์และจอ
// Controller main sketch: buttons -> state machine -> motor -> server + display.
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

// Bench aid: time each part of the loop and print any pass that runs long. The
// display sends state at 4 Hz and the CYD measured it arriving in bursts with
// ~1.3 s gaps every 4-5 s, which means this loop stops running for about a
// second at a time. That starves the state cadence and, when two stalls land
// together, trips the display's 2.5 s link timeout. This names the call
// responsible instead of eliminating candidates one reflash at a time.
// Off for real hardware; enable per-build:
//   --build-property compiler.cpp.extra_flags=-DLOOP_DEBUG=1 --clean
// [TH] โหมดทดสอบ: จับเวลาแต่ละส่วนของลูป แล้วพิมพ์เฉพาะรอบที่ใช้เวลานาน
//      บอร์ดนี้ส่งสถานะ 4 ครั้ง/วินาที แต่ฝั่งจอวัดได้ว่าข้อความมาเป็นช่วง
//      และเว้นช่วงราว 1.3 วินาที ทุกๆ 4-5 วินาที แปลว่าลูปนี้หยุดเดินราวหนึ่งวินาที
//      ทำให้ส่งสถานะไม่ทัน และถ้าเกิดติดกันสองครั้งจะเลยเกณฑ์ 2.5 วินาทีของฝั่งจอ
//      โหมดนี้ชี้ตัวการโดยตรง แทนการไล่ตัดทีละข้อแล้วแฟลชใหม่ทุกครั้ง
#ifndef LOOP_DEBUG
#define LOOP_DEBUG 0
#endif
#define LOOP_DEBUG_SLOW_MS 200

#include "config.h"
#include "settings.h"
#include "actuator.h"
#include "buttons.h"
#include "panel_lamps.h"
#include "session.h"
#include "net.h"
#include "link.h"
#include "rtc.h"
#include <time.h>
#include <esp_log.h>
#include <driver/gpio.h>

DeviceSettings gSettings;  // ค่าตั้งค่าปัจจุบัน
SessionSM gSession;        // เครื่องสถานะของรอบการทำงาน
Link gLink;                // ลิงก์ UART ไปยังบอร์ดจอ

// The NTP sync is one-shot per boot: the DS3231 keeps time on its own from then
// on, and the machine is meant to work offline, so re-syncing forever would be
// noise. A power cut with the coin cell in place does not need it either.
// [TH] ซิงก์เวลาจาก NTP ครั้งเดียวต่อการเปิดเครื่อง เพราะ DS3231 เดินเวลาเองต่อได้
//      และเครื่องต้องใช้งานแบบออฟไลน์ได้ ไม่จำเป็นต้องซิงก์ซ้ำเรื่อยๆ
static bool gNtpDone = false;

// Jog mode: hold a button to move the leg by hand, for bench tuning and for
// nudging a leg into place. Entered by holding STOP while idle for JOG_ENTER_MS,
// left by holding both buttons for JOG_EXIT_HOLD_MS or after JOG_IDLE_EXIT_MS
// with nothing held — and every exit lowers the leg in full.
//
// It lives here and not in SessionSM on purpose. Jog is not a phase of a therapy
// session — it reports nothing, logs nothing, counts no reps, and belongs to no
// session. Adding a Phase for it would put it on the wire, where the display
// mirrors the enum by ordinal, and would give the state machine a state in which
// none of its own rules apply.
//
// Entry is a hold on STOP rather than anything involving START, because the
// START edge starts a session the instant it is seen: there is no gesture
// beginning with START that does not run the machine first. STOP while idle only
// clears the rep count, so holding it past that costs nothing. Inside the mode
// neither button keeps its normal meaning, which is why the mode exists at all —
// outside it, both are exactly the edges they have always been, and STOP stays
// instant.
// [TH] โหมดขยับเอง: กดปุ่มค้างเพื่อขยับขาด้วยมือ ใช้ปรับค่าบนโต๊ะและจัดตำแหน่งขา
//      เข้าโหมดโดยกดปุ่ม STOP ค้างตอนเครื่องว่างเป็นเวลา JOG_ENTER_MS
//      ออกจากโหมดโดยกดสองปุ่มค้าง JOG_EXIT_HOLD_MS หรือเมื่อไม่มีการกดอะไรนาน JOG_IDLE_EXIT_MS
//      และการออกทุกครั้งจะลดขาลงเต็มระยะ
//      โหมดนี้อยู่ที่ไฟล์นี้ ไม่ได้อยู่ใน SessionSM โดยตั้งใจ
//      เพราะการขยับเองไม่ใช่สถานะหนึ่งของรอบการรักษา ไม่รายงาน ไม่บันทึก ไม่นับจำนวนครั้ง
//      และไม่เป็นของรอบไหนเลย การเพิ่มค่าใน Phase จะทำให้มันไปอยู่บนสายสื่อสาร
//      ซึ่งฝั่งจอเทียบค่าด้วยลำดับของ enum และจะให้เครื่องสถานะมีสถานะที่กฎของตัวเองใช้ไม่ได้เลย
//      การเข้าโหมดใช้การกดค้างที่ปุ่ม STOP ไม่ใช่อะไรที่เกี่ยวกับปุ่ม START
//      เพราะขอบของปุ่ม START เริ่มรอบการทำงานทันทีที่เห็น
//      จึงไม่มีท่าทางใดที่เริ่มด้วยปุ่ม START แล้วไม่สั่งให้เครื่องเดินไปก่อน
//      ส่วนปุ่ม STOP ตอนเครื่องว่างแค่ล้างจำนวนครั้ง การกดค้างเลยจุดนั้นจึงไม่เสียอะไร
//      ในโหมดนี้ปุ่มทั้งสองไม่มีความหมายเดิม ซึ่งเป็นเหตุผลที่ต้องมีโหมดแยก
//      เพราะนอกโหมด ปุ่มทั้งสองยังเป็นขอบสัญญาณแบบเดิมทุกอย่าง และปุ่ม STOP ยังตัดทันที
static bool gJogMode = false;
static uint32_t gJogHeldSince = 0;  // when STOP began being held while idle
static uint32_t gJogLastInput = 0;  // last time a jog button was held
// Set on entry and on an energize-cap trip: nothing drives until both buttons
// have been let go. Without it the STOP still held from the entry gesture
// lowered the leg the moment the mode came on, a shorted START line lifted it as
// soon as STOP was released, and a held button re-drove 50 ms after the cap cut
// it — the cap never bounded a jog at all.
// [TH] ตั้งค่าตอนเข้าโหมดและตอนชนเพดานเวลา จะไม่ขับอะไรจนกว่าจะปล่อยปุ่มทั้งสองก่อน
//      ถ้าไม่มี ปุ่ม STOP ที่ยังกดค้างจากการเข้าโหมดจะลดขาลงทันทีที่โหมดเปิด
//      สาย START ที่ลัดวงจรจะยกขาขึ้นทันทีที่ปล่อย STOP
//      และปุ่มที่กดค้างจะขับใหม่ 50 มิลลิวินาทีหลังเพดานเวลาตัด เพดานจึงไม่เคยคุมการขยับเองได้เลย
static bool gJogNeedRelease = false;
static uint32_t gJogBothSince = 0;  // when START+STOP began being held together
// Set on every exit: STOP has to be let go before a new entry hold can count, so
// the two-button exit (STOP still down) does not walk straight back in 2 s later.
// [TH] ตั้งค่าทุกครั้งที่ออกจากโหมด ต้องปล่อย STOP ก่อนการกดค้างเพื่อเข้าโหมดครั้งใหม่จะเริ่มนับ
//      การออกด้วยสองปุ่ม (ซึ่ง STOP ยังกดอยู่) จึงไม่วนกลับเข้าโหมดเองใน 2 วินาทีถัดมา
static bool gJogEntryBlocked = false;

// Leaves jog mode and lowers the leg in full: after a jog the position is unknown,
// and down is the one place a leg can always be left safely.
// [TH] ออกจากโหมดขยับเองแล้วลดขาลงเต็มระยะ เพราะหลังขยับเองไม่ทราบตำแหน่งขา
//      และด้านล่างคือที่เดียวที่ทิ้งขาไว้ได้อย่างปลอดภัยเสมอ
static void jogExit(uint32_t nowMs, const char* why) {
  gJogMode = false;
  gJogHeldSince = 0;
  gJogBothSince = 0;
  gJogEntryBlocked = true;
  actuatorSoftOff();
  gSession.lowerFromIdle(nowMs);
  Serial.printf("jog: mode off - %s, lowering the leg\n", why);
}

// Returns the direction the operator is asking for, and ACT_OFF for none. Both
// buttons held is deliberately nothing: two directions at once has no sensible
// answer, and picking one would drive a leg the way the operator did not mean.
// [TH] คืนทิศทางที่ผู้ใช้สั่ง และคืน ACT_OFF ถ้าไม่ได้สั่ง
//      การกดสองปุ่มพร้อมกันจงใจให้ไม่เกิดอะไร เพราะสองทิศพร้อมกันไม่มีคำตอบที่สมเหตุสมผล
//      การเลือกทางใดทางหนึ่งจะเป็นการขับขาไปในทางที่ผู้ใช้ไม่ได้ตั้งใจ
static ActState jogRequest() {
  bool up = buttonStartHeld(), down = buttonStopHeld();
  if (up == down) return ACT_OFF;
  return up ? ACT_UP : ACT_DOWN;
}

// Drives the motor from the held buttons. Returns true when it owns the motor
// this iteration, so applyActuator() leaves it alone.
// [TH] ขับมอเตอร์ตามปุ่มที่กดค้างอยู่ คืนค่าจริงเมื่อโหมดนี้เป็นเจ้าของมอเตอร์ในรอบนี้
//      applyActuator() จะได้ไม่เข้ามาแทรก
static bool jogLoop(uint32_t nowMs) {
  // A session (panel- or server-started) or a latched fault outranks the mode:
  // leave it at once and let the state machine have the motor back. Checked
  // before entry as well as during, so the mode can never be entered into a
  // machine that is already moving on its own.
  // [TH] รอบการทำงาน (เริ่มจากหน้าปัดหรือจากเซิร์ฟเวอร์) หรือความผิดพลาดที่ค้างอยู่ สำคัญกว่าโหมดนี้
  //      ให้ออกจากโหมดทันทีและคืนมอเตอร์ให้เครื่องสถานะ
  //      ตรวจทั้งก่อนเข้าโหมดและระหว่างอยู่ในโหมด
  //      โหมดนี้จึงไม่มีทางถูกเข้าไปในเครื่องที่กำลังเคลื่อนที่เองอยู่แล้ว
  if (gSession.running() || gSession.faulted()) {
    gJogMode = false;
    gJogHeldSince = 0;
    gJogBothSince = 0;
    return false;
  }

  if (!gJogMode) {
    if (gJogEntryBlocked) {
      if (buttonStopHeld()) return false;
      gJogEntryBlocked = false;
    }
    // Hold STOP while idle to enter. The hold is timed from the debounced level,
    // not from the edge, so a tap that clears the rep count never accumulates.
    // [TH] กดปุ่ม STOP ค้างตอนเครื่องว่างเพื่อเข้าโหมด
    //      จับเวลาการกดค้างจากระดับสัญญาณที่ผ่านกันเด้งแล้ว ไม่ใช่จากขอบ
    //      การกดสั้นๆ เพื่อล้างจำนวนครั้งจึงไม่ถูกนับสะสม
    if (!buttonStopHeld()) { gJogHeldSince = 0; return false; }
    if (gJogHeldSince == 0) gJogHeldSince = nowMs;
    if (nowMs - gJogHeldSince < JOG_ENTER_MS) return false;
    gJogMode = true;
    gJogHeldSince = 0;
    gJogLastInput = nowMs;
    gJogNeedRelease = true;  // the entry hold is not itself a jog — ไม่ถือว่าการกดค้างเพื่อเข้าโหมดเป็นการสั่งขยับ
    Serial.println("jog: mode on - release STOP, then hold START to raise, STOP to lower, both to leave");
    return true;
  }

  // Both held: the manual exit. Checked ahead of the release wait, so it works
  // even straight after entry with STOP still down.
  // [TH] กดค้างทั้งสองปุ่ม: ออกจากโหมดด้วยมือ ตรวจก่อนการรอปล่อยปุ่ม
  //      จึงใช้ได้แม้ทันทีหลังเข้าโหมดที่ STOP ยังกดค้างอยู่
  if (buttonStartHeld() && buttonStopHeld()) {
    if (gJogBothSince == 0) gJogBothSince = nowMs;
    actuatorSoftOff();
    if (nowMs - gJogBothSince >= JOG_EXIT_HOLD_MS) jogExit(nowMs, "both buttons held");
    return true;
  }
  gJogBothSince = 0;

  // gJogLastInput is not refreshed while waiting, so a button that never lets go
  // (a shorted line) still times the mode out.
  // [TH] ไม่อัปเดต gJogLastInput ระหว่างรอ ปุ่มที่ไม่เคยถูกปล่อย (สายลัดวงจร) จึงยังทำให้โหมดหมดเวลาได้
  if (gJogNeedRelease) {
    if (buttonStartHeld() || buttonStopHeld()) {
      actuatorSoftOff();
      if (nowMs - gJogLastInput >= JOG_IDLE_EXIT_MS) jogExit(nowMs, "a button never released");
      return true;
    }
    gJogNeedRelease = false;
  }

  ActState dir = jogRequest();
  if (dir != ACT_OFF) {
    gJogLastInput = nowMs;
    // Recorded on the first movement, not on release: a jog interrupted by a power
    // cut still has to leave the position unknown.
    // [TH] บันทึกตั้งแต่การขยับครั้งแรก ไม่ใช่ตอนปล่อยปุ่ม
    //      การขยับที่ถูกไฟดับขัดจังหวะก็ยังต้องทิ้งสถานะว่าไม่ทราบตำแหน่งไว้
    gSession.markLegMoved();
    actuatorJog(dir);
    return true;
  }

  // Nothing held. Fade out, and leave the mode if it has been quiet long enough.
  // [TH] ไม่มีการกดอะไร ให้ผ่อนกำลังลง และออกจากโหมดถ้าเงียบนานพอแล้ว
  actuatorSoftOff();
  if (nowMs - gJogLastInput >= JOG_IDLE_EXIT_MS) jogExit(nowMs, "idle timeout");
  return true;
}

static void applyActuator() {
  // [TH] แปลงสถานะปัจจุบันของรอบให้เป็นคำสั่งขับมอเตอร์ (เรียกทุกลูป)
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
  // Two ways to stop, and which one a phase gets is a safety decision rather
  // than a comfort one. Idle, Hold and Rest are strokes that ended because they
  // were supposed to, so they fade out over MOTOR_RAMP_MS and the leg is not
  // stopped dead. Fault is the one phase that cuts the drive in the same call —
  // a latched fault means something is wrong with a leg that may be raised, and
  // spending another 400 ms driving it is exactly the wrong answer. The other
  // instant cuts live inside actuator.cpp and are not reachable from here: the
  // energize cap, and a STOP that reverses a lift into a safe-lower.
  // [TH] มีสองวิธีในการหยุด และการที่สถานะไหนได้วิธีไหนเป็นการตัดสินใจเรื่องความปลอดภัย ไม่ใช่ความสบาย
  //      สถานะว่าง ค้าง และพัก คือจังหวะที่จบเพราะถึงเวลาของมันเอง
  //      จึงค่อยๆ ผ่อนกำลังลงภายในเวลา MOTOR_RAMP_MS ขาจะได้ไม่ถูกหยุดกึก
  //      ส่วนสถานะผิดพลาดเป็นสถานะเดียวที่ตัดกำลังขับในการเรียกครั้งเดียว
  //      เพราะความผิดพลาดที่ค้างอยู่แปลว่ามีอะไรผิดปกติกับขาที่อาจยกอยู่
  //      การขับต่ออีก 400 มิลลิวินาทีคือคำตอบที่ผิดพอดี
  //      การตัดทันทีที่เหลืออยู่ใน actuator.cpp และเรียกจากตรงนี้ไม่ได้
  //      คือเพดานเวลาจ่ายไฟ และการกด STOP ที่กลับทิศจากการยกไปเป็นการลดขาเพื่อหยุด
  switch (gSession.phase()) {
    case Phase::Lift: actuatorUp(); break;
    case Phase::Lower:
    case Phase::SafeLower: actuatorDown(); break;
    case Phase::Fault: actuatorOff(); break;  // instant — ตัดทันที
    default: actuatorSoftOff(); break;        // Idle, Hold, Rest — ผ่อนกำลังลง
  }
}

static void applySettings(const DeviceSettings& s) {
  // [TH] รับค่าตั้งค่าใหม่: บันทึกลง NVS, อัปเดตเวลารอบ, ตั้งเพดานเวลาจ่ายไฟ (เผื่อ 500 มิลลิวินาที
  //      ให้สูงกว่าเพดานของเครื่องสถานะเล็กน้อย เพื่อเป็นแนวป้องกันสำรอง), แล้วเริ่มเครือข่ายใหม่
  gSettings = s;
  settingsSave(gSettings);
  gSession.cfg = {gSettings.liftSec * 1000u,  gSettings.lowerSec * 1000u,
                  gSettings.holdSec * 1000u,  gSettings.restSec * 1000u,
                  gSettings.maxTravelSec * 1000u, gSettings.targetReps};
  actuatorSetCap(gSettings.maxTravelSec * 1000u + 500u);
  netBegin(gSettings);
}

// The cycle times set the actuator's hard cap, so they are clamped here rather
// than trusted from the wire. Every sender clamps already — the spinboxes on the
// settings screen and the card reader on the display — but this board owns the
// motor: a maxTravel of 0 arriving down the UART would make the cap 0 ms, fault
// every stroke instantly, and persist to NVS so it survived the reboot. The
// ranges match the settings screen's spinbox limits, kept in step by hand.
// [TH] เวลารอบเป็นตัวกำหนดเพดานเวลาจ่ายไฟ จึงจำกัดช่วงค่าที่นี่ ไม่เชื่อค่าที่ส่งมาตรงๆ
//      ฝั่งผู้ส่งจำกัดค่าให้อยู่แล้วทั้งช่องกรอกในหน้าตั้งค่าและตอนอ่านไฟล์จากการ์ด
//      แต่บอร์ดนี้เป็นเจ้าของมอเตอร์ ถ้าค่า maxTravel เป็น 0 ส่งมาทาง UART เพดานจะเป็น 0 มิลลิวินาที
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
// actuatorTick(), so that stall happens with the motor driving and neither the
// limit switches nor the travel cap being read. It also swaps the live cycle
// timings out from under a stroke already in progress. Nothing is lost by
// waiting: the operator's SAVE lands the moment the leg is down.
// [TH] ข้อความตั้งค่าที่มาถึงระหว่างรอบทำงานจะถูกพักไว้จนกว่าเครื่องจะว่าง
//      applySettings เขียนลง NVS และเริ่มการเชื่อมต่อ WiFi ใหม่ ทั้งสองอย่างค้างได้นานเท่าการลบแฟลช
//      และ gLink.poll() ทำงานหลัง applyActuator() กับ actuatorTick()
//      การค้างนั้นจึงเกิดขึ้นตอนที่มอเตอร์กำลังขับอยู่ โดยไม่มีการอ่านลิมิตสวิตช์และไม่มีการตรวจเพดานเวลาเลย
//      ทั้งยังสลับค่าเวลาของรอบที่กำลังเดินอยู่ด้วย
//      การรอไม่ทำให้เสียอะไร ค่าที่ผู้ใช้กดบันทึกจะมีผลทันทีที่ขาลงถึงพื้น
static bool gPendingSettings = false;
static DeviceSettings gPending;
static bool gTargetDirty = false;

static void onLinkSettings(const LinkSettings& s) {
  DeviceSettings d;
  // Carried forward, not taken from the wire: soundOn belongs to the display and
  // is deliberately absent from LinkSettings. `d` is a local, so its scalars are
  // indeterminate until assigned — leaving this one out would put a garbage bool
  // into every copy of `d`, harmless only for as long as nothing on this board
  // reads it.
  // [TH] ค่านี้ยกมาจากของเดิม ไม่ได้รับมาจากสาย เพราะ soundOn เป็นของบอร์ดจอ
  //      และจงใจไม่มีอยู่ใน LinkSettings
  //      ตัวแปร d เป็นตัวแปรเฉพาะที่ ค่าตัวเลขและบูลีนจึงยังไม่แน่นอนจนกว่าจะกำหนด
  //      ถ้าไม่กำหนดค่านี้ จะมีค่าขยะติดไปกับสำเนาของ d ทุกชุด
  //      ซึ่งไม่เป็นอันตรายเฉพาะตราบใดที่ยังไม่มีโค้ดบนบอร์ดนี้อ่านมัน
  d.soundOn = gSettings.soundOn;
  d.wifiSsid = s.ssid; d.wifiPass = s.pass; d.serverUrl = s.server;
  d.deviceCode = s.code;
  // A zero means "this field was not sent", not "zero seconds" — the same
  // convention netPollCommand already uses for the server's timings, and none of
  // these has a legal value of 0. It matters because the boards are flashed
  // separately: a display still running an image from before lift/lower existed
  // sends a settings message without those keys, link.cpp decodes the absent
  // fields as 0, and clamping 0 up to the low bound would silently reconfigure
  // both strokes to one second and persist that to NVS with nothing reported.
  // target below reuses the same INTENT but not the same trick — see there.
  // [TH] ค่าศูนย์แปลว่า "ไม่ได้ส่งฟิลด์นี้มา" ไม่ใช่ "ศูนย์วินาที"
  //      เป็นข้อตกลงเดียวกับที่ netPollCommand ใช้กับค่าเวลาจากเซิร์ฟเวอร์อยู่แล้ว
  //      และไม่มีค่าใดในกลุ่มนี้ที่ใช้ 0 ได้จริง
  //      เรื่องนี้สำคัญเพราะบอร์ดสองใบแฟลชแยกกัน จอที่ยังใช้เฟิร์มแวร์รุ่นก่อนจะมี lift/lower
  //      จะส่งข้อความตั้งค่าที่ไม่มีคีย์เหล่านั้น link.cpp จะถอดรหัสฟิลด์ที่หายไปเป็น 0
  //      การบีบ 0 ขึ้นไปหาขอบล่างจะเปลี่ยนการเคลื่อนที่ทั้งสองทางเป็นหนึ่งวินาทีแบบเงียบๆ
  //      แล้วบันทึกลง NVS โดยไม่มีการรายงานใดๆ
  //      target ด้านล่างใช้เจตนาเดียวกันนี้แต่ไม่ใช้กลวิธีเดียวกัน ดูที่นั่น
  d.maxTravelSec = clampSec(s.maxTravel ? s.maxTravel : gSettings.maxTravelSec, 1, 20,
                            "maxTravelSec");
  d.liftSec = clampSec(s.lift ? s.lift : gSettings.liftSec, 1, d.maxTravelSec, "liftSec");
  d.lowerSec = clampSec(s.lower ? s.lower : gSettings.lowerSec, 1, d.maxTravelSec, "lowerSec");
  d.holdSec = clampSec(s.hold ? s.hold : gSettings.holdSec, 1, 60, "holdSec");
  d.restSec = clampSec(s.rest ? s.rest : gSettings.restSec, 1, 60, "restSec");
  // Unlike the fields above, 0 is a legal target ("run until STOP"), so an
  // absent field cannot decode to 0 and fall through a falsy check the way
  // theirs do — that reading is indistinguishable from an operator who typed
  // 0 on purpose. link.cpp decodes an absent field to the out-of-range
  // LINK_TARGET_ABSENT sentinel instead; treat it the same as the others'
  // absent case, keep the current value, before it ever reaches the clamp.
  // [TH] ต่างจากฟิลด์ข้างบน ค่า 0 เป็นเป้าหมายที่ถูกต้อง (หมายถึง "ทำไปเรื่อยๆ จนกว่าจะกด STOP")
  //      ฟิลด์ที่หายไปจึงถอดรหัสเป็น 0 แล้วใช้เงื่อนไขแบบ falsy check เหมือนฟิลด์อื่นไม่ได้
  //      เพราะจะแยกไม่ออกจากผู้ใช้ที่ตั้งใจพิมพ์ 0 เอง link.cpp จึงถอดรหัสฟิลด์ที่หายไปเป็นค่า
  //      บอกสถานะนอกช่วง LINK_TARGET_ABSENT แทน ให้จัดการเหมือนกรณี "ไม่มีอยู่" ของฟิลด์อื่น
  //      คือคงค่าปัจจุบันไว้ ก่อนจะส่งเข้า clamp
  d.targetReps =
      clampSec(s.target == LINK_TARGET_ABSENT ? gSettings.targetReps : s.target, 0, 99,
               "targetReps");
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
    held.maxTravel = d.maxTravelSec; held.lift = d.liftSec; held.lower = d.lowerSec;
    held.hold = d.holdSec; held.rest = d.restSec; held.target = d.targetReps;
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
  // Same collision as the held branch above, but on the idle path applySettings
  // writes straight into gSettings instead of into gPending, so there is no later
  // commit point to fold the live value into — it has to happen right here, before
  // the write. gLink.poll() can dispatch several messages from one UART drain, so
  // a target-dial message and this settings message can both land in the same
  // call: if the target one was dispatched first, gTargetDirty is already true and
  // gSettings.targetReps already holds the newer value by the time this runs.
  // d.targetReps only knows what THIS settings message's own field said, which can
  // be an arbitrarily old snapshot — e.g. a settings screen opened before the
  // operator ever touched the target dial — regardless of which message the UART
  // happened to deliver last. A live target is the one unambiguous signal of
  // current intent, so it wins over a bundled settings field whenever one is
  // outstanding; when none is outstanding (gTargetDirty false) this changes
  // nothing and the settings message's own target applies exactly as before.
  // [TH] ชนกันแบบเดียวกับบล็อกที่พักไว้ด้านบน แต่บนเส้นทางที่เครื่องว่าง applySettings
  //      เขียนลง gSettings ตรงๆ ไม่ผ่าน gPending จึงไม่มีจุดค่อยรวมค่าทีหลัง ต้องทำตรงนี้ก่อนเขียน
  //      gLink.poll() ดึงข้อความได้หลายฉบับต่อการเรียกหนึ่งครั้ง ข้อความปรับเป้าหมายแบบสด
  //      กับข้อความตั้งค่านี้จึงมาถึงในการเรียกเดียวกันได้ ถ้าข้อความเป้าหมายมาก่อน
  //      gTargetDirty จะเป็นจริงแล้วและ gSettings.targetReps ก็มีค่าใหม่อยู่แล้วตอนโค้ดนี้ทำงาน
  //      ส่วน d.targetReps รู้แค่ค่าที่ข้อความตั้งค่าฉบับนี้บอกมา ซึ่งอาจเป็นภาพเก่าแค่ไหนก็ได้
  //      เช่นหน้าตั้งค่าที่เปิดค้างไว้ก่อนผู้ใช้จะไปหมุนปุ่มเป้าหมาย ไม่ว่า UART จะส่งฉบับไหนถึงทีหลัง
  //      ค่าเป้าหมายแบบสดคือสัญญาณเดียวที่ชัดเจนว่าเป็นความตั้งใจล่าสุด จึงชนะฟิลด์ที่แนบมากับ
  //      ข้อความตั้งค่าทุกครั้งที่มีค่าค้างอยู่ ถ้าไม่มีค้าง (gTargetDirty เป็นเท็จ) ก็ไม่เปลี่ยนอะไร
  //      ค่าเป้าหมายของข้อความตั้งค่าเองก็มีผลตามปกติ
  if (gTargetDirty) {
    d.targetReps = gSettings.targetReps;
    gTargetDirty = false;
  }
  applySettings(d);
  gLink.sendSettings(currentAsLink());
}

// Shared by currentAsLink() (the settings device is actually running on) and
// the timings change-guard below (which also needs to render gPending, the
// edit held during a session, in the same shape to compare against it).
// [TH] ใช้ร่วมกันระหว่าง currentAsLink() (ค่าตั้งค่าที่เครื่องใช้งานอยู่จริง)
//      กับตัวป้องกันการใช้ค่าเวลาซ้ำด้านล่าง ซึ่งต้องแปลง gPending
//      (ค่าที่แก้ไขค้างไว้ระหว่างรอบทำงาน) ให้อยู่ในรูปแบบเดียวกันเพื่อเทียบกัน
static LinkSettings toLink(const DeviceSettings& d) {
  LinkSettings s;
  s.ssid = d.wifiSsid; s.pass = d.wifiPass;
  s.server = d.serverUrl; s.code = d.deviceCode;
  s.maxTravel = d.maxTravelSec; s.hold = d.holdSec;
  s.rest = d.restSec;
  s.lift = d.liftSec; s.lower = d.lowerSec; s.target = d.targetReps;
  return s;
}

static LinkSettings currentAsLink() { return toLink(gSettings); }

// [TH] เมื่อจอทักทาย (hello) ให้ส่งค่าตั้งค่าปัจจุบันกลับไป
static void onLinkHello() { gLink.sendSettings(currentAsLink()); }

// A live target change from the CYD. Applied at once, including mid-session, and
// remembered so the next NVS write carries it — the settings path is deferred
// until the session ends and this cannot wait for that.
// [TH] การเปลี่ยนเป้าหมายจากจอแบบสด นำไปใช้ทันทีรวมถึงตอนกลางรอบ
//      และจำไว้เพื่อให้การบันทึก NVS ครั้งถัดไปเก็บค่านี้ไปด้วย
//      เพราะเส้นทาง settings ถูกเลื่อนไปจนกว่ารอบจะจบ ซึ่งค่านี้รอไม่ได้
static void onLinkTarget(uint16_t reps) {
  if (reps > 99) reps = 99;
  gSettings.targetReps = reps;
  gSession.setTarget(reps);
  gTargetDirty = true;
}

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
static void debugTick(uint32_t nowMs, bool startEdge, bool stopEdge) {
  static bool firstRun = true;
  static int lvStart = -1, lvStop = -1;
  static Phase lastPhase = Phase::Idle;
  static uint16_t lastReps = 0;

  if (startEdge) Serial.printf("[dbg %lu] EDGE START\n", (unsigned long)nowMs);
  if (stopEdge) Serial.printf("[dbg %lu] EDGE STOP\n", (unsigned long)nowMs);

  int s = digitalRead(PIN_BTN_START), t = digitalRead(PIN_BTN_STOP);
  if (firstRun || s != lvStart || t != lvStop) {
    lvStart = s; lvStop = t;
    Serial.printf("[dbg %lu] btn levels start=%s stop=%s\n", (unsigned long)nowMs,
                  levelName(s, BTN_START_NC), levelName(t, BTN_STOP_NC));
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

  // A press always reprints the state, even when nothing moved. STOP pressed on
  // an already-clear session changes neither phase nor reps, and a change-only
  // print would leave the press looking like it did nothing at all.
  // [TH] ทุกครั้งที่กดปุ่มจะพิมพ์สถานะซ้ำเสมอ แม้ค่าไม่เปลี่ยน เพราะกด STOP ตอนที่ค่าเป็นศูนย์อยู่แล้ว
  //      จะไม่มีอะไรเปลี่ยน ถ้าพิมพ์เฉพาะตอนเปลี่ยนจะดูเหมือนการกดไม่มีผล
  bool pressed = startEdge || stopEdge;
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
  actuatorBegin();  // safety first — ตัดกำลังขับก่อนเป็นอันดับแรกเพื่อความปลอดภัย
  buttonsBegin();
  panelLampsBegin();  // lamps dark before anything can drive them — ให้หลอดดับไว้ก่อนที่อะไรจะมาสั่งได้
  rtcBegin();  // absent chip is fine: epochs just read 0 — ไม่มีชิปก็ไม่เป็นไร เวลาจะเป็น 0
  Serial.begin(115200);
  // The IDF logger is silenced for every tag, and this is a safety measure rather
  // than a tidiness one. A station that cannot associate makes the WiFi driver
  // print "sta is connecting, return error" on every retry — measured at ~1500
  // lines/s, about 26 KB/s — while UART0 carries ~11.5 KB/s. HardwareSerial's
  // write() blocks until the buffer drains (setTxTimeoutMs exists only on the
  // native-USB classes, not on a real UART), so the overflow stalls the loop that
  // sends state every 250 ms, services the actuator energize cap and reads the
  // panel STOP button. Muting the source is the only lever this core offers.
  //
  // Nothing worth reading is lost: these are driver logs, not ours. Serial.printf
  // does not go through esp_log, so the boot banner, the fault lines and the
  // BENCH_DEBUG output all still appear — and they print on change, so they
  // cannot flood the port the way a retry loop does.
  // [TH] ปิดข้อความของไลบรารีระบบทุกหมวด ทำเพื่อความปลอดภัย ไม่ใช่แค่ให้ดูสะอาด
  //      ถ้าต่อ WiFi ไม่ได้ ไดรเวอร์จะพิมพ์ข้อความผิดพลาดทุกครั้งที่ลองใหม่
  //      วัดได้ราว 1500 บรรทัดต่อวินาที หรือราว 26 กิโลไบต์ต่อวินาที ขณะที่ UART0 ส่งได้ราว 11.5 กิโลไบต์ต่อวินาที
  //      การเขียนพอร์ตจะรอจนกว่าบัฟเฟอร์จะว่าง (ฟังก์ชันตั้งเวลารอมีเฉพาะพอร์ต USB ในตัว ไม่มีสำหรับ UART จริง)
  //      พอร์ตที่ล้นจึงทำให้ลูปที่ส่งสถานะทุก 250 มิลลิวินาที ดูแลเพดานเวลาจ่ายไฟ และอ่านปุ่มหยุด ต้องหยุดรอ
  //      แกนนี้ทำได้ทางเดียวคือปิดที่ต้นทาง
  //      ไม่ได้เสียข้อความที่ต้องใช้ เพราะที่ปิดคือข้อความของไดรเวอร์ ไม่ใช่ของเรา
  //      Serial.printf ไม่ได้ผ่านระบบนี้ ข้อความตอนเปิดเครื่อง ข้อความผิดพลาด และโหมดดีบักจึงยังพิมพ์ตามปกติ
  //      และพิมพ์เฉพาะตอนค่าเปลี่ยน จึงไม่ทำให้พอร์ตล้นแบบการลองเชื่อมต่อซ้ำ
  esp_log_level_set("*", ESP_LOG_NONE);
  delay(200);  // let UART0 settle so the boot banner is not clipped — รอ UART0 นิ่งกันข้อความต้นเปิดเครื่องขาด
  // 19200, not 115200. The link carries ~4 messages a second of about 120 bytes
  // — under 500 B/s — so 115200 bought nothing but noise sensitivity. A slower
  // bit is a wider bit: each one is sampled further from its edges, which is the
  // cheapest margin available on unshielded jumper wire between two boards.
  // Both ends must match; flash them together.
  // [TH] ใช้ 19200 ไม่ใช่ 115200 สายนี้ส่งราว 4 ข้อความต่อวินาที ข้อความละราว 120 ไบต์
  //      ไม่ถึง 500 ไบต์ต่อวินาที ความเร็ว 115200 จึงไม่ได้ประโยชน์ มีแต่ไวต่อสัญญาณรบกวน
  //      บิตที่ช้าลงคือบิตที่กว้างขึ้น จุดที่อ่านค่าจะห่างจากขอบมากขึ้น
  //      เป็นวิธีเพิ่มความทนทานที่ถูกที่สุดบนสายจัมเปอร์เปลือยระหว่างสองบอร์ด
  //      ทั้งสองฝั่งต้องตั้งค่าเท่ากัน ต้องแฟลชพร้อมกัน
  Serial2.begin(19200, SERIAL_8N1, 16, 17);  // UART to CYD — สาย UART ไปบอร์ดจอ (RX=16, TX=17)
  // Hold RX high while the other board is idle. A floating RX picks up noise
  // that the UART reads as start bits, prefixing every message with a run of
  // 0xFF; link.cpp trims that off, but not creating it is better. Internal
  // pull-up is ~45k — weak. An external 10k to 3V3 is the stronger fix if junk
  // survives this.
  // [TH] ดึงขารับให้เป็นลอจิกสูงไว้ตอนที่อีกฝั่งไม่ได้ส่ง
  //      ขารับที่ลอยจะรับสัญญาณรบกวนซึ่ง UART อ่านเป็นบิตเริ่ม ทำให้มีไบต์ 0xFF นำหน้าทุกข้อความ
  //      link.cpp ตัดทิ้งให้อยู่แล้ว แต่ไม่ให้เกิดตั้งแต่แรกดีกว่า
  //      ตัวต้านทานภายในราว 45 กิโลโอห์มซึ่งค่อนข้างอ่อน ถ้ายังมีขยะอยู่ให้ใส่ตัวต้านทาน 10 กิโลโอห์มไป 3.3V
  gpio_set_pull_mode(GPIO_NUM_16, GPIO_PULLUP_ONLY);

  settingsLoad(gSettings);
  gSession.cfg = {gSettings.liftSec * 1000u,  gSettings.lowerSec * 1000u,
                  gSettings.holdSec * 1000u,  gSettings.restSec * 1000u,
                  gSettings.maxTravelSec * 1000u, gSettings.targetReps};
  actuatorSetCap(gSettings.maxTravelSec * 1000u + 500u);
  netBegin(gSettings);
  // UTC, no DST rule: every epoch on the wire is UTC and the CYD adds the Thai
  // offset only when it formats a datetime for the log.
  // [TH] ตั้งเป็น UTC ล้วน ไม่ใส่เขตเวลา เพราะเวลาที่ส่งกันเป็น UTC ทั้งหมด
  //      ฝั่งจอค่อยบวกเวลาไทยตอนเขียนลงไฟล์
  configTime(0, 0, "pool.ntp.org", "time.google.com");
  if (!actuatorCapTimerReady())
    Serial.println("actuator: cap timer missing, the hardware backstop is OFF");
  // Reported for the same reason and at the same point as the cap timer: both are
  // decided in actuatorBegin(), before the port is open. A false here means the
  // LEDC channels never attached, so no stroke can move the leg at all and every
  // one of them will run to the cap and fault with nothing having happened.
  // [TH] รายงานด้วยเหตุผลเดียวกันและที่จุดเดียวกับตัวจับเวลาเพดานเวลา
  //      ทั้งคู่ถูกตัดสินใน actuatorBegin() ก่อนที่พอร์ตจะเปิด
  //      ถ้าค่านี้เป็นเท็จแปลว่าผูกช่อง LEDC ไม่สำเร็จ
  //      จะไม่มีจังหวะไหนขยับขาได้เลย และทุกจังหวะจะวิ่งจนชนเพดานเวลาแล้วขึ้นความผิดพลาดโดยไม่มีอะไรเกิดขึ้น
  if (!actuatorPwmReady())
    Serial.println("actuator: LEDC attach failed, the motor CANNOT be driven");
  gLink.begin(&Serial2);
  gLink.onSettings = onLinkSettings;
  gLink.onHello = onLinkHello;
  gLink.onTarget = onLinkTarget;
  Serial.printf("pt-leg-controller %s ssid=%s server=%s code=%s cycle lift/hold/lower/rest=%u/%u/%u/%u target=%u\n",
                FW_VERSION, gSettings.wifiSsid.c_str(), gSettings.serverUrl.c_str(),
                gSettings.deviceCode.c_str(), gSettings.liftSec, gSettings.holdSec,
                gSettings.lowerSec, gSettings.restSec, gSettings.targetReps);
#if BENCH_DEBUG
  // ASCII only: an em dash here reaches a serial monitor as mojibake.
  Serial.printf("debug prints ON - btn start=%d(%s) stop=%d(%s), lamp relay=%d\n",
                PIN_BTN_START, BTN_START_NC ? "NC" : "NO",
                PIN_BTN_STOP, BTN_STOP_NC ? "NC" : "NO",
                PIN_LAMP_RELAY);
#endif
}

#if LOOP_DEBUG
// Each segment's elapsed ms for the current pass, printed together when the pass
// runs long. File scope so the timing points can sit inside the branches they
// measure without threading locals through them.
// [TH] เวลาที่ใช้ของแต่ละส่วนในลูปรอบปัจจุบัน พิมพ์พร้อมกันเมื่อรอบนั้นใช้เวลานาน
//      ประกาศไว้ระดับไฟล์เพื่อให้จุดจับเวลาอยู่ในบล็อกที่มันวัดได้เลย
static uint32_t dSeg_net, dSeg_ntp, dSeg_linkPoll, dSeg_cmd, dSeg_hb, dSeg_state, dSeg_btn;
#define LD_START(v) uint32_t v##_t0 = millis()
#define LD_END(v, seg) seg = millis() - v##_t0
#endif

void loop() {
  // [TH] ลำดับสำคัญ: อ่านปุ่ม -> เดินเครื่องสถานะ -> สั่งมอเตอร์ -> ตรวจเพดานเวลา ก่อนงานเครือข่าย
  uint32_t now = millis();
#if LOOP_DEBUG
  uint32_t dIter_t0 = now;
  dSeg_net = dSeg_ntp = dSeg_linkPoll = dSeg_cmd = dSeg_hb = dSeg_state = dSeg_btn = 0;
  LD_START(btn);
#endif
  buttonsPoll(now);
  gSession.tick(now);
#if LOOP_DEBUG
  LD_END(btn, dSeg_btn);
#endif

  // The energize cap is charged to the stroke that was actually driving, which
  // means consuming it BEFORE the buttons can start a new session. A long loop
  // stall latches the trip and a held RUN at the same time; handled after the
  // button block, forceFault() then finished the session that had just started
  // milliseconds ago, and its zero totals overwrote the real ones of the session
  // the cap had actually ended — destroying that session's duration and rep count
  // in both the server event and the card row.
  //
  // Not while a safe-lower is running: the trip can surface in the same iteration
  // in which a RUN press entered SafeLower, and faulting there would abort the descent
  // the operator just asked for before the DOWN stroke had driven at all. That
  // descent is separately bounded by its own fresh cap.
  // [TH] เพดานเวลาจ่ายไฟต้องถูกคิดให้กับการเคลื่อนที่ที่กำลังขับเคลื่อนอยู่จริง
  //      จึงต้องอ่านค่าก่อนที่ปุ่มจะเริ่มรอบใหม่ได้
  //      ลูปที่ค้างนานจะทำให้ทั้งการตัดและการกด RUN ค้างมาพร้อมกัน
  //      ถ้าอ่านหลังบล็อกปุ่ม forceFault() จะไปจบรอบที่เพิ่งเริ่มไปไม่กี่มิลลิวินาที
  //      แล้วยอดรวมศูนย์ของรอบนั้นจะเขียนทับยอดจริงของรอบที่เพดานเวลาเป็นคนจบ
  //      ทำลายทั้งเวลารวมและจำนวนครั้งของรอบนั้นทั้งในเหตุการณ์ฝั่งเซิร์ฟเวอร์และในแถวข้อมูลบนการ์ด
  //      และไม่ทำระหว่างที่กำลังลดขาเพื่อหยุด เพราะการตัดอาจปรากฏในลูปรอบเดียวกับที่การกด RUN พาเข้าสถานะนั้น
  //      การตัดสินว่าผิดพลาดตรงนั้นจะยกเลิกการลดขาที่ผู้ใช้เพิ่งสั่ง ตั้งแต่ก่อนที่จังหวะลงจะได้ขับเลย
  //      การลดขาครั้งนั้นมีเพดานเวลาของตัวเองคุมอยู่แล้ว
  if (actuatorTakeCapTrip()) {
    Serial.println("actuator: energize cap cut the motor drive");
    // Swallowed here does not mean ignored: a trip that lands mid-SafeLower is
    // real even though forceFault() is skipped for it (see the comment above),
    // and a start queued behind that descent must not lift the instant the
    // descent's timer runs out — the cap may have tripped because the descent
    // itself is in trouble.
    // [TH] การกลืนตรงนี้ไม่ได้แปลว่าเพิกเฉย การตัดที่เกิดกลางสถานะลดขาเพื่อหยุดเป็นเรื่องจริง
    //      แม้จะข้าม forceFault() ไปสำหรับกรณีนี้ (ดูคอมเมนต์ด้านบน)
    //      คำสั่งเริ่มที่เข้าคิวรออยู่หลังการลดขานั้นต้องไม่ยกขึ้นทันทีที่ตัวจับเวลาของการลดขาหมดเวลา
    //      เพราะเพดานอาจถูกชนเนื่องจากการลดขาเองกำลังมีปัญหา
    if (gSession.phase() != Phase::SafeLower) gSession.forceFault(now);
    else gSession.cancelQueuedStart();
    // forceFault() is a no-op in Idle, which is where jog runs, so the cap has to
    // be enforced here: the held button must be let go before it drives again.
    // [TH] forceFault() ไม่ทำอะไรในสถานะว่าง ซึ่งเป็นที่ที่โหมดขยับเองทำงาน
    //      จึงต้องบังคับเพดานตรงนี้ ต้องปล่อยปุ่มก่อนจึงจะขับได้อีก
    if (gJogMode) {
      gJogNeedRelease = true;
      Serial.println("jog: energize cap reached - release both buttons to jog again");
    }
  }

  // panel buttons: START begins a session, STOP ends one — and when the machine is
  // already idle, STOP clears the rep count. Read both first: each accessor clears
  // its edge, so the debug build has to see the same values this loop acts on.
  // [TH] ปุ่มหน้าปัด: START เริ่มรอบ STOP จบรอบ และถ้าเครื่องว่างอยู่แล้ว STOP จะล้างจำนวนครั้ง
  //      อ่านทั้งสองก่อน เพราะการอ่านจะล้างขอบทิ้ง โหมดดีบักต้องเห็นค่าชุดเดียวกับที่ลูปใช้
  bool pressStart = buttonStartPressed();
  bool pressStop = buttonStopPressed();
  // Jog mode owns the motor and both buttons while it is on, but only those: the
  // rest of the loop runs untouched. An early return here starved the heartbeat
  // and the state message, and the display's 2.5 s link watchdog then reported no
  // controller at all — a machine that looks disconnected while somebody is
  // holding its buttons is worse than one that merely looks idle.
  //
  // The edges are read and dropped rather than left alone, because the accessors
  // are what clear them: an edge left pending fires the instant the mode exits,
  // and a START press from ten seconds ago starting the machine is exactly the
  // surprise this panel must never spring.
  // [TH] โหมดขยับเองเป็นเจ้าของมอเตอร์และปุ่มทั้งสองขณะที่เปิดอยู่ แต่เป็นเจ้าของแค่นั้น
  //      ส่วนที่เหลือของลูปยังทำงานตามปกติ
  //      การคืนค่าออกจากลูปตรงนี้ทำให้สัญญาณชีพและข้อความสถานะขาดหายไป
  //      แล้วตัวเฝ้าระวังลิงก์ 2.5 วินาทีของจอจะรายงานว่าไม่พบตัวควบคุมเลย
  //      เครื่องที่ดูเหมือนขาดการเชื่อมต่อขณะที่มีคนกดปุ่มอยู่ แย่กว่าเครื่องที่ดูเหมือนแค่ว่าง
  //      ขอบสัญญาณถูกอ่านแล้วทิ้ง ไม่ใช่ปล่อยไว้ เพราะตัวอ่านเป็นตัวล้างขอบ
  //      ขอบที่ค้างอยู่จะทำงานทันทีที่ออกจากโหมด
  //      และการที่ปุ่ม START ที่กดไปสิบวินาทีก่อนมาสั่งให้เครื่องเดิน คือความประหลาดใจที่หน้าปัดนี้ต้องไม่สร้าง
  bool jogActive = jogLoop(now);
  if (!jogActive) {
    // A start is refused while the START line looks shorted: it would restart the
    // machine at the end of every session with nobody at the panel. A stop is never
    // refused by anything.
    // [TH] ไม่ให้เริ่มรอบถ้าสาย START ดูเหมือนลัดวงจร เพราะจะทำให้เครื่องเริ่มใหม่ทุกครั้งที่รอบจบ
    //      โดยไม่มีใครอยู่ที่หน้าปัด ส่วนการสั่งหยุดไม่เคยถูกปฏิเสธด้วยอะไรทั้งสิ้น
    if (pressStart && buttonStartStuck())
      Serial.println("start refused: START reads pressed, check its wiring");
    else if (pressStart) gSession.start(now);
    if (pressStop) gSession.requestStop(now);
    applyActuator();
  }
  actuatorTick(now);
  // Right after the motor command, and before the network work: the lamp agrees
  // with the actuator command issued in the same iteration, and it still updates
  // on the iterations where netLoop() later blocks on an unreachable server.
  // running() is still "the actuator may move" — true for Lift/Hold/Lower/Rest
  // and for the safe-lower, false for Idle and for a latched Fault. A latched
  // Fault and jog mode both blink, so neither reads as ordinary Idle on a
  // panel-only machine with no CYD to say which one it is.
  // [TH] วางไว้ต่อจากการสั่งขับมอเตอร์และก่อนงานเครือข่าย
  //      หลอดจะได้ตรงกับคำสั่งชุดขับเคลื่อนที่ออกไปในลูปรอบเดียวกัน
  //      และยังถูกอัปเดตในรอบที่ netLoop() ไปค้างรอเซิร์ฟเวอร์ที่ติดต่อไม่ได้ด้วย
  //      running() ยังคงหมายถึง "ชุดขับเคลื่อนอาจเคลื่อนที่"
  //      เป็นจริงตอนยกขึ้น/ค้าง/ลดลง/พัก และตอนลดขาเพื่อหยุด เป็นเท็จตอนว่างและตอนค้างสถานะผิดพลาด
  //      สถานะผิดพลาดที่ค้างอยู่และโหมดขยับเองจะกะพริบทั้งคู่
  //      จึงไม่ดูเหมือนสถานะว่างตามปกติบนเครื่องที่มีแค่หน้าปัดโดยไม่มีจอ CYD บอก
  panelLampsSet(gSession.running(), gSession.faulted(), jogActive);
  // The trip raised by actuatorTick's own software check is consumed at the top of
  // the NEXT iteration, alongside the hardware timer's. Both go through the one
  // place above, so a trip can never be charged to a session that started after
  // the stroke it belongs to.
  // [TH] การตัดที่เกิดจากการตรวจฝั่งซอฟต์แวร์ใน actuatorTick จะถูกอ่านที่ต้นลูปรอบถัดไป
  //      พร้อมกับของตัวจับเวลาฮาร์ดแวร์ ทั้งคู่ผ่านจุดเดียวกันด้านบน
  //      การตัดจึงไม่มีทางถูกคิดให้กับรอบที่เริ่มหลังการเคลื่อนที่ที่มันเป็นเจ้าของ
#if LOOP_DEBUG
  LD_START(net);
#endif
  netLoop(now);
#if LOOP_DEBUG
  LD_END(net, dSeg_net);
#endif
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
#if LOOP_DEBUG
  LD_START(ntp);
#endif
  if (!gNtpDone && netWifiUp()) {
    time_t t = time(nullptr);
    if (t > 1700000000) gNtpDone = rtcSetFromEpoch((uint32_t)t);  // ~2023-11
  }
#if LOOP_DEBUG
  LD_END(ntp, dSeg_ntp);
  LD_START(lp);
#endif
  gLink.poll();
#if LOOP_DEBUG
  LD_END(lp, dSeg_linkPoll);
#endif

  // UART-loss safe-stop: the screen may be dead and no touch-stop is possible, so
  // a link that goes quiet mid-session lowers the leg and stops.
  //
  // Armed by the first line ever received, deliberately. A board that has never
  // heard from a display is running standalone — the documented bench setup, and
  // a machine whose CYD is unplugged — where the panel RUN button is the primary
  // stop and works regardless. Arming from boot instead would make the condition
  // permanently true on those machines: a start and requestStop would land in the
  // same loop iteration and no session could ever run. See "Running the
  // controller without a display" in firmware/README.md.
  // [TH] เริ่มนับจากข้อความแรกที่ได้รับ ซึ่งเป็นความตั้งใจ
  //      บอร์ดที่ไม่เคยได้ยินเสียงจากจอเลยคือบอร์ดที่ใช้งานเดี่ยว ทั้งชุดทดสอบตามที่บันทึกไว้
  //      และเครื่องที่ถอดสายจอออก กรณีนั้นปุ่ม RUN หน้าปัดเป็นตัวหยุดหลักและใช้ได้อยู่แล้ว
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
  // after it started, over and over.
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
    int16_t wantTarget = -1;
    NetTimings wantTimings{0, 0, 0, 0, 0};
#if LOOP_DEBUG
    LD_START(cmd);
#endif
    String cmd = netPollCommand(&wantTarget, &wantTimings);
#if LOOP_DEBUG
    LD_END(cmd, dSeg_cmd);
#endif
    // Timings go through applySettings, which clamps them, persists them and — if
    // a session is running — holds the whole change until it ends. That deferral
    // is the point: the NVS commit and the Wi-Fi restart it performs must not
    // happen with the motor driving, and a stroke length that changed halfway
    // through a stroke would be worse than waiting.
    // [TH] ค่าเวลาของรอบส่งผ่าน applySettings ซึ่งจำกัดช่วง บันทึกค่า
    //      และถ้ากำลังมีรอบทำงานอยู่จะพักการเปลี่ยนแปลงทั้งชุดไว้จนกว่ารอบจะจบ
    //      การเลื่อนนั้นคือประเด็น เพราะการบันทึก NVS และการรีสตาร์ต WiFi ที่ตามมา
    //      ต้องไม่เกิดขึ้นตอนที่มอเตอร์กำลังขับอยู่
    //      และความยาวจังหวะที่เปลี่ยนกลางจังหวะก็แย่กว่าการรอ
    if (wantTimings.liftSec || wantTimings.lowerSec || wantTimings.holdSec ||
        wantTimings.restSec || wantTimings.maxTravelSec) {
      // The server hands out the same held timings on every poll until the
      // device echoes them back or the TTL expires (see fetch_desired_timings
      // in server/app/state.py) — and nothing echoes timings, so without a
      // guard this ran on every one of the ~30 polls in that window: 30x
      // applySettings() (NVS write + WiFi restart) while idle, or 30x
      // re-arming gPendingSettings and re-echoing the held values to the CYD
      // mid-session. Mirrors the target guard just below.
      //
      // Compared against gPending rather than gSettings whenever an edit is
      // already held (mid-session): gSettings does not move until the
      // session ends, so comparing against it would keep reporting a
      // difference — and re-applying — for as long as the session runs, the
      // exact mid-session repeat this guard exists to stop.
      // [TH] เซิร์ฟเวอร์ส่งค่าเวลาที่พักไว้ชุดเดิมทุกครั้งที่โพล จนกว่าอุปกรณ์จะยืนยันกลับ
      //      หรือหมดอายุตาม TTL (ดู fetch_desired_timings ใน server/app/state.py)
      //      และไม่มีอะไรยืนยันค่าเวลากลับเลย ถ้าไม่มีตัวป้องกันนี้ ทุกรอบใน ~30 รอบโพลของหน้าต่างนั้น
      //      จะทำงานซ้ำ: เขียน NVS และรีสตาร์ต WiFi 30 ครั้งตอนว่าง
      //      หรือตั้งค่าค้างใหม่และส่งค่ากลับไปจอซ้ำ 30 ครั้งระหว่างรอบทำงาน
      //      เทียบกับตัวป้องกันเป้าหมายด้านล่าง
      //
      //      เทียบกับ gPending แทน gSettings ทุกครั้งที่มีค่าค้างไว้อยู่แล้ว (ระหว่างรอบทำงาน)
      //      เพราะ gSettings จะไม่ขยับจนกว่ารอบจะจบ การเทียบกับมันจะเห็นว่าต่างกันเรื่อยๆ
      //      และใช้ค่าซ้ำเรื่อยๆ ตลอดเวลาที่รอบยังทำงานอยู่ ซึ่งเป็นการซ้ำระหว่างรอบทำงานที่ตัวป้องกันนี้ตั้งใจกัน
      LinkSettings baseline = gPendingSettings ? toLink(gPending) : currentAsLink();
      LinkSettings s = baseline;
      if (wantTimings.maxTravelSec) s.maxTravel = wantTimings.maxTravelSec;
      if (wantTimings.liftSec) s.lift = wantTimings.liftSec;
      if (wantTimings.lowerSec) s.lower = wantTimings.lowerSec;
      if (wantTimings.holdSec) s.hold = wantTimings.holdSec;
      if (wantTimings.restSec) s.rest = wantTimings.restSec;
      bool changed = s.maxTravel != baseline.maxTravel || s.lift != baseline.lift ||
                     s.lower != baseline.lower || s.hold != baseline.hold ||
                     s.rest != baseline.rest;
      if (changed) onLinkSettings(s);
    }
    // Applied before the command, so a start that carries a target runs with it
    // rather than with the previous one.
    // [TH] นำไปใช้ก่อนคำสั่ง คำสั่งเริ่มที่แนบเป้าหมายมาจะได้ใช้ค่าใหม่ ไม่ใช่ค่าเดิม
    // Clamped here like every other entry point. The route validates 0..99, but
    // this board owns the motor and the value reaches it from whatever is at the
    // other end of serverUrl — which the operator types in by hand — so the range
    // is enforced where it is used rather than trusted from the wire, exactly as
    // onLinkTarget does for the UART path.
    // [TH] จำกัดช่วงตรงนี้เหมือนทางเข้าอื่นทุกทาง เส้นทาง HTTP ตรวจ 0..99 อยู่แล้ว
    //      แต่บอร์ดนี้เป็นเจ้าของมอเตอร์ และค่านี้มาจากอะไรก็ตามที่อยู่ปลายทาง serverUrl
    //      ซึ่งผู้ใช้พิมพ์เอง จึงบังคับช่วงค่าที่จุดที่ใช้งานจริง ไม่ใช่เชื่อค่าที่ส่งมา
    //      แบบเดียวกับที่ onLinkTarget ทำกับเส้นทาง UART
    if (wantTarget > 99) wantTarget = 99;
    if (wantTarget >= 0 && (uint16_t)wantTarget != gSettings.targetReps) {
      gSettings.targetReps = (uint16_t)wantTarget;
      gSession.setTarget((uint16_t)wantTarget);
      gTargetDirty = true;
      gLink.sendSettings(currentAsLink());  // keep the CYD's spinbox honest
                                            // [TH] ให้ช่องปรับค่าบนจอตรงกับของจริง
    }
    if (cmd == "start" && buttonStartStuck())
      Serial.println("server start refused: START reads pressed, check its wiring");
    else if (cmd == "start") gSession.start(now);
    else if (cmd == "stop") gSession.requestStop(now);
  }
  // [TH] ส่งสัญญาณชีพถี่ขึ้นตอนทำงาน (ทุก 1 วิ) และห่างตอนว่าง (ทุก 5 วิ)
  uint32_t hbInterval = gSession.running() ? 1000 : 5000;
  if (now - lastHb >= hbInterval) {
    lastHb = now;
#if LOOP_DEBUG
    LD_START(hb);
#endif
    netHeartbeat(gSession.running() ? "running" : "idle", gSession.elapsedSec(now),
                 gSession.reps(), gSession.target());
#if LOOP_DEBUG
    LD_END(hb, dSeg_hb);
#endif
  }
  // Stopped is handled BEFORE started, and the order is load-bearing. Both edges
  // can be pending in one iteration: tick() can end a session on a fault, and the
  // button block below it can start a new one, because running() is false in
  // Fault. Queued the other way round the server saw started-then-stopped, where
  // the stopped belongs to the session before it — so a server pairing the two
  // closes the new session and leaves the faulted one open forever. A session can
  // never start and stop within one iteration (start always lands in Lift, and no
  // path out of Lift calls finish() in the same pass), so this order is safe.
  // [TH] จัดการสัญญาณ "หยุด" ก่อนสัญญาณ "เริ่ม" และลำดับนี้สำคัญ
  //      สัญญาณทั้งสองค้างอยู่พร้อมกันในลูปรอบเดียวได้ tick() จบรอบเพราะความผิดพลาดได้
  //      และบล็อกปุ่มที่อยู่ถัดลงไปก็เริ่มรอบใหม่ได้ เพราะ running() เป็นเท็จในสถานะผิดพลาด
  //      ถ้าเรียงกลับกัน เซิร์ฟเวอร์จะเห็นเริ่มก่อนหยุด โดยที่การหยุดนั้นเป็นของรอบก่อนหน้า
  //      เซิร์ฟเวอร์ที่จับคู่สองเหตุการณ์นี้จะปิดรอบใหม่และทิ้งรอบที่ผิดพลาดค้างไว้ตลอดไป
  //      รอบหนึ่งจะเริ่มและจบภายในลูปรอบเดียวไม่ได้ เพราะการเริ่มจะไปสถานะยกขาเสมอ
  //      และไม่มีเส้นทางออกจากสถานะยกขาที่เรียก finish() ในรอบเดียวกัน ลำดับนี้จึงปลอดภัย
  //
  // The stopped edge feeds two independent records: the server event (online) and
  // the CYD's SD row (offline). Neither depends on the other succeeding.
  // [TH] ขอบสัญญาณหยุดใช้บันทึกสองที่ที่ไม่ขึ้นต่อกัน: เหตุการณ์ฝั่งเซิร์ฟเวอร์ (ตอนออนไลน์)
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
  // [TH] ส่งเหตุการณ์ "เพิ่งเริ่ม" (ตรวจจับขอบสัญญาณครั้งเดียว)
  if (gSession.takeStartedEdge()) {
    netQueueEvent("started", gSession.elapsedSec(now), gSession.reps());
    gLink.sendStarted();  // the CYD's start cue — เสียงเริ่มการทำงานบนจอ
  }

  // Settings that arrived mid-session land here, once the motor is stopped. After
  // the done message on purpose: applySettings restarts WiFi, and the session's
  // record should be on its way out before that happens.
  // [TH] ค่าตั้งค่าที่มาถึงระหว่างรอบจะถูกนำมาใช้ตรงนี้ หลังจากมอเตอร์หยุดแล้ว
  //      วางไว้หลังข้อความ done โดยตั้งใจ เพราะ applySettings จะเริ่มการเชื่อมต่อ WiFi ใหม่
  //      ควรให้ข้อมูลของรอบนั้นถูกส่งออกไปก่อน
  if (gPendingSettings && !gSession.running()) {
    gPendingSettings = false;
    // A live target change and a held settings-save are two independent UART
    // messages, and either can land first within one running session. onLinkTarget
    // writes the new value straight into gSettings.targetReps (it has to — it is
    // applied live) but never touches gPending; onLinkSettings snapshots whatever
    // target ITS OWN message carried into gPending.targetReps, which is stale the
    // moment a live change lands before or after it. Applying gPending unexamined
    // would silently revert the operator's live change and then confirm the
    // reverted number back to the CYD as "what actually took effect" — the same
    // class of hazard the comment above (line ~142) already covers for two
    // settings messages, just between a settings message and a live-target one.
    // gSettings.targetReps is always the most recent value regardless of which
    // message arrived last, because onLinkTarget is the only thing that writes it
    // while a session runs, so folding it into gPending here — and clearing the
    // flag unconditionally so it can never dangle into a stray extra save — makes
    // the live target the one that survives to NVS and to the CYD's confirmation.
    // [TH] การเปลี่ยนเป้าหมายแบบสดกับข้อความตั้งค่าที่พักไว้เป็นคนละข้อความ UART
    //      มาถึงก่อนหลังกันได้ในรอบทำงานเดียวกัน onLinkTarget เขียนค่าใหม่ลง
    //      gSettings.targetReps ตรงๆ (ต้องทำแบบนั้นเพราะใช้ทันที) แต่ไม่แตะ gPending เลย
    //      ส่วน onLinkSettings สแนปช็อตเป้าหมายจากข้อความของตัวเองลง gPending.targetReps
    //      ซึ่งจะเก่าทันทีถ้ามีการเปลี่ยนสดมาก่อนหรือหลังก็ตาม
    //      ถ้านำ gPending ไปใช้ตรงๆ จะย้อนค่าที่ผู้ใช้เพิ่งเปลี่ยนสดโดยไม่รู้ตัว
    //      แล้วยืนยันค่าที่ย้อนกลับไปนั้นกลับไปให้จอว่า "มีผลใช้งานจริง"
    //      เป็นปัญหาแบบเดียวกับที่คอมเมนต์ด้านบน (บรรทัด ~142) พูดถึงไว้แล้วสำหรับข้อความตั้งค่าสองอัน
    //      ต่างกันแค่เป็นระหว่างข้อความตั้งค่ากับข้อความเป้าหมายแบบสด
    //      gSettings.targetReps เป็นค่าล่าสุดเสมอไม่ว่าข้อความไหนจะมาถึงทีหลัง
    //      เพราะระหว่างที่รอบทำงานอยู่มีแต่ onLinkTarget เท่านั้นที่เขียนค่านี้
    //      จึงรวมเข้า gPending ตรงนี้ และล้างธงทิ้งเสมอไม่ให้ค้างไปกระตุ้นการบันทึกซ้ำที่ผิด
    //      ค่าเป้าหมายแบบสดจึงเป็นค่าที่รอดไปถึง NVS และไปถึงการยืนยันของจอ
    if (gTargetDirty) gPending.targetReps = gSettings.targetReps;
    gTargetDirty = false;
    Serial.println("settings: applying the ones held during the session");
    applySettings(gPending);
    gLink.sendSettings(currentAsLink());  // confirm what actually took effect
                                          // [TH] ยืนยันค่าที่มีผลใช้งานจริง
  } else if (gTargetDirty && !gSession.running()) {
    // A live target change has nowhere else to persist: it bypassed the settings
    // path entirely, so this is the only place left that writes it to NVS.
    // [TH] การเปลี่ยนเป้าหมายแบบสดไม่มีที่อื่นให้บันทึกลง NVS
    //      เพราะมันข้ามเส้นทาง settings ไปตั้งแต่ต้น จุดนี้จึงเป็นจุดเดียวที่จะบันทึกค่านั้นได้
    gTargetDirty = false;
    settingsSave(gSettings);
  }

  if (now - lastState >= 250) {  // [TH] ส่งสถานะให้จอ ~4 ครั้งต่อวินาที
    lastState = now;
    LinkState st{gSession.running(), (int)gSession.phase(), gSession.elapsedSec(now),
                 gSession.reps(), gSession.target(), netWifiUp(), netServerUp(), gJogMode};
#if LOOP_DEBUG
    LD_START(st);
#endif
    gLink.sendState(st);
#if LOOP_DEBUG
    LD_END(st, dSeg_state);
#endif
  }
#if BENCH_DEBUG
  debugTick(now, pressStart, pressStop);
#endif
#if LOOP_DEBUG
  // Printed before the delay so the figure is this pass's work, not the pacing.
  // Segments that did not run this pass read 0, which is the answer for them.
  // [TH] พิมพ์ก่อนหน่วงเวลา ตัวเลขจะได้เป็นงานจริงของรอบนี้ ไม่รวมการหน่วง
  //      ส่วนที่ไม่ได้ทำงานในรอบนี้จะเป็น 0 ซึ่งก็คือคำตอบของส่วนนั้น
  uint32_t dIter = millis() - dIter_t0;
  if (dIter >= LOOP_DEBUG_SLOW_MS)
    Serial.printf(
        "[ctl %lu] SLOW iter=%lums btn=%lu net=%lu ntp=%lu linkpoll=%lu cmd=%lu hb=%lu state=%lu\n",
        (unsigned long)millis(), (unsigned long)dIter, (unsigned long)dSeg_btn,
        (unsigned long)dSeg_net, (unsigned long)dSeg_ntp, (unsigned long)dSeg_linkPoll,
        (unsigned long)dSeg_cmd, (unsigned long)dSeg_hb, (unsigned long)dSeg_state);
  // Heartbeat line: proves the loop is alive and shows its normal pace, so a
  // silent stretch in the log is distinguishable from a board that stopped
  // printing altogether.
  // [TH] บรรทัดบอกจังหวะปกติของลูป เพื่อให้ช่วงที่เงียบในบันทึกแยกออกจาก
  //      กรณีที่บอร์ดหยุดพิมพ์ไปเลย
  static uint32_t dWindow = 0;
  static uint32_t dMaxIter = 0;
  static uint16_t dPasses = 0;
  if (dIter > dMaxIter) dMaxIter = dIter;
  dPasses++;
  if (millis() - dWindow >= 1000) {
    dWindow = millis();
    Serial.printf("[ctl %lu] passes=%u maxiter=%lums running=%d elapsed=%lus\n",
                  (unsigned long)dWindow, (unsigned)dPasses, (unsigned long)dMaxIter,
                  gSession.running() ? 1 : 0, (unsigned long)gSession.elapsedSec(millis()));
    dPasses = 0;
    dMaxIter = 0;
  }
#endif
  delay(2);
}
