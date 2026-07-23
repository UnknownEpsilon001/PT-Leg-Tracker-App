// ตรรกะเครื่องสถานะของรอบการทำงาน: ตัดสินใจเปลี่ยนสถานะจากเวลาและลิมิตสวิตช์
// Session state-machine logic: transitions driven by time and the limit switches.
#include "session.h"

void SessionSM::enter(Phase p, uint32_t nowMs) {
  ph = p;
  phaseStart = nowMs;  // จับเวลาเริ่มสถานะใหม่
}

void SessionSM::finish(uint32_t nowMs, Phase to) {
  // [TH] ปิดรอบ: เก็บเวลารวมและจำนวนครั้งไว้เป็นค่าสุดท้าย แล้วตั้งสัญญาณ "หยุด"
  finalElapsed = (ph == Phase::Idle) ? 0 : (nowMs - sessionStart) / 1000;
  finalRepCount = repCount;
  stoppedEdge = true;
  enter(to, nowMs);
}

void SessionSM::start(uint32_t nowMs) {
  if (running()) return;  // ignore if already running; allowed from Idle or Fault
                          // [TH] ถ้ากำลังทำงานอยู่แล้วให้ข้าม เริ่มได้จากสถานะ ว่าง หรือ ผิดพลาด
  sessionStart = nowMs;
  repCount = 0;
  startedEdge = true;
  enter(Phase::Lift, nowMs);
}

void SessionSM::requestStop(uint32_t nowMs) {
  // [TH] คำขอหยุด: ตอบสนองต่างกันตามสถานะปัจจุบัน เพื่อให้ขาลงถึงพื้นก่อนหยุดเสมอ
  switch (ph) {
    case Phase::Idle:
    case Phase::Fault:
      return;  // nothing to stop; a latched Fault clears only via start()
               // [TH] ไม่มีอะไรให้หยุด สถานะ Fault ที่ค้างจะล้างได้ด้วย start() เท่านั้น
    case Phase::Lift:
    case Phase::Hold:
    case Phase::Lower:
      enter(Phase::SafeLower, nowMs);  // bring the leg down, then stop — ลดขาลงก่อนแล้วค่อยหยุด
      return;
    case Phase::Rest:
      finish(nowMs, Phase::Idle);  // already down — ขาลงอยู่แล้ว หยุดได้เลย
      return;
    case Phase::SafeLower:
      return;  // already lowering to a clean stop — กำลังลงเพื่อหยุดอยู่แล้ว
  }
}

void SessionSM::forceFault(uint32_t nowMs) {
  if (ph == Phase::Idle || ph == Phase::Fault) return;
  finish(nowMs, Phase::Fault);
}

void SessionSM::tick(uint32_t nowMs, bool topPressed, bool bottomPressed) {
  // net 2: both switches pressed at once is physically impossible -> wiring fault
  // [TH] ตาข่ายกันพลาด 2: สวิตช์บนและล่างถูกกดพร้อมกันเป็นไปไม่ได้ทางกายภาพ -> ถือว่าสายผิด
  if (running() && topPressed && bottomPressed) {
    finish(nowMs, Phase::Fault);
    return;
  }
  uint32_t dt = nowMs - phaseStart;  // เวลาที่อยู่ในสถานะปัจจุบัน
  switch (ph) {
    case Phase::Idle:
    case Phase::Fault:
      return;
    case Phase::Lift:
      if (topPressed) enter(Phase::Hold, nowMs);            // fully up — ยกสุดแล้ว
      else if (dt >= cfg.maxTravelMs) finish(nowMs, Phase::Fault);  // net 1 — เกินเวลา = ผิดพลาด
      return;
    case Phase::Hold:
      if (dt >= cfg.holdMs) enter(Phase::Lower, nowMs);     // ค้างครบเวลา -> ลดลง
      return;
    case Phase::Lower:
      if (bottomPressed) enter(Phase::Rest, nowMs);         // fully down — ลงสุดแล้ว
      else if (dt >= cfg.maxTravelMs) finish(nowMs, Phase::Fault);  // net 1 — เกินเวลา = ผิดพลาด
      return;
    case Phase::Rest:
      if (dt >= cfg.restMs) {
        repCount++;                 // พักครบ -> นับ 1 ครั้ง แล้วเริ่มยกใหม่
        enter(Phase::Lift, nowMs);
      }
      return;
    case Phase::SafeLower:
      // clean stop: lower until bottom switch OR the cap, then Idle (NOT Fault)
      // [TH] หยุดปลอดภัย: ลดลงจนสวิตช์ล่างปิดหรือครบเพดานเวลา แล้วกลับสู่ว่าง (ไม่ใช่ผิดพลาด)
      if (bottomPressed || dt >= cfg.maxTravelMs) finish(nowMs, Phase::Idle);
      return;
  }
}

uint32_t SessionSM::elapsedSec(uint32_t nowMs) const {
  // [TH] เวลาที่ผ่านไปของรอบ (วินาที): ว่าง=0, ผิดพลาด=ค่าที่แช่ไว้ตอน fault, อื่นๆ=นับสด
  if (ph == Phase::Idle) return 0;
  if (ph == Phase::Fault) return finalElapsed;  // frozen at fault time — แช่ค่าไว้ ณ เวลาที่ผิดพลาด
  return (nowMs - sessionStart) / 1000;
}

bool SessionSM::takeStartedEdge() {
  bool v = startedEdge;
  startedEdge = false;
  return v;
}

bool SessionSM::takeStoppedEdge() {
  bool v = stoppedEdge;
  stoppedEdge = false;
  return v;
}
