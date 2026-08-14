// ตรรกะเครื่องสถานะของรอบการทำงาน: ทุกสถานะเปลี่ยนด้วยเวลา ไม่มีลิมิตสวิตช์แล้ว
// Session state-machine logic. Every transition is a clock; there are no switches.
#include "session.h"

void SessionSM::enter(Phase p, uint32_t nowMs) {
  // Entering a descending phase begins a stroke that may keep driving the same
  // direction as the phase before it, which the actuator cannot see for itself.
  // Raised here, the one place every entry passes through, rather than inferred
  // by the sketch from a phase it happened to observe.
  // [TH] การเข้าสู่สถานะที่สั่งลงคือการเริ่มเคลื่อนที่ครั้งใหม่ ซึ่งอาจสั่งไปทางเดิมกับสถานะก่อนหน้า
  //      ตัวขับเคลื่อนมองไม่เห็นเรื่องนี้เอง จึงยกสัญญาณตรงนี้ซึ่งเป็นจุดเดียวที่การเข้าสถานะทุกครั้งต้องผ่าน
  //      แทนที่จะให้สเก็ตช์เดาจากสถานะที่มันบังเอิญเห็น
  if (p == Phase::SafeLower) strokeRenewEdge = true;
  ph = p;
  phaseStart = nowMs;  // จับเวลาเริ่มสถานะใหม่
}

bool SessionSM::takeStrokeRenewEdge() {
  bool v = strokeRenewEdge;
  strokeRenewEdge = false;
  return v;
}

void SessionSM::finish(uint32_t nowMs, Phase to, bool fault) {
  // [TH] ปิดรอบ: เก็บเวลารวมและจำนวนครั้งไว้เป็นค่าสุดท้าย แล้วตั้งสัญญาณ "หยุด"
  finalElapsed = (ph == Phase::Idle) ? 0 : (nowMs - sessionStart) / 1000;
  finalRepCount = repCount;
  // Latched with the other totals, not read back off the live phase later: a stop
  // press taken in the same loop iteration as the fault that ended the session
  // would otherwise let faulted() answer "no" for a session that tripped a net.
  // [TH] เก็บค่าพร้อมยอดรวมอื่นๆ ไม่ได้ไปอ่านจากสถานะปัจจุบันทีหลัง
  //      ถ้ามีการกดหยุดในลูปรอบเดียวกับความผิดพลาดที่จบรอบนั้น faulted() จะตอบว่าไม่ผิดพลาด
  //      ทั้งที่รอบนั้นชนกลไกความปลอดภัย
  finalFault = fault;
  stoppedEdge = true;
  enter(to, nowMs);
}

void SessionSM::start(uint32_t nowMs) {
  if (running()) return;  // ignore while a session is under way — ถ้ากำลังทำงานอยู่ให้ข้าม
  // Out of a latched Fault, lower before lifting. The fault stopped the relays
  // wherever the leg happened to be, and this firmware has no way to find out
  // where that was. The lowering runs first and the start is queued behind it.
  // [TH] ถ้ามาจากสถานะผิดพลาดที่ค้างอยู่ ให้ลดขาก่อนแล้วค่อยยก
  //      ความผิดพลาดตัดรีเลย์ตรงจุดไหนก็ได้ที่ขาอยู่ และเฟิร์มแวร์นี้ไม่มีทางรู้ว่าตรงไหน
  //      จึงให้การลดขาทำงานก่อน แล้วเข้าคิวเริ่มรอบไว้ข้างหลัง
  if (ph == Phase::Fault) {
    recovering = true;
    startAfterRecovery = true;
    enter(Phase::SafeLower, nowMs);
    return;
  }
  sessionStart = nowMs;
  repCount = 0;
  startedEdge = true;
  recovering = false;
  enter(Phase::Lift, nowMs);
}

void SessionSM::requestStop(uint32_t nowMs) {
  // [TH] คำขอหยุด: ตอบสนองต่างกันตามสถานะปัจจุบัน เพื่อให้ขาลงถึงพื้นก่อนหยุดเสมอ
  switch (ph) {
    case Phase::Idle:
      // Nothing is moving and nothing is latched, so STOP is the "clear" button:
      // it zeroes the count the panel and the screen are showing. This is what
      // the retired RESET did.
      // [TH] ไม่มีอะไรเคลื่อนที่และไม่มีสถานะค้าง ปุ่ม STOP จึงทำหน้าที่ล้างค่า
      //      คือล้างจำนวนครั้งที่หน้าปัดและหน้าจอแสดงอยู่ ซึ่งเป็นสิ่งที่ปุ่ม RESET ที่เลิกใช้เคยทำ
      repCount = 0;
      // The edges and the finished session's totals are deliberately left alone. A
      // session can finish and a STOP edge be taken inside the same loop iteration
      // — tick() runs before the buttons, and the edges are consumed after them —
      // and clearing them here would throw away both the server "stopped" event
      // and the done message, which is the session's only row on the card.
      // [TH] จงใจไม่ล้างขอบสัญญาณและยอดรวมของรอบที่จบไปแล้ว
      //      รอบหนึ่งจบและมีการกด STOP ในลูปรอบเดียวกันได้ เพราะ tick() ทำงานก่อนปุ่ม
      //      และการอ่านขอบสัญญาณอยู่หลังปุ่ม การล้างตรงนี้จะทิ้งทั้งเหตุการณ์ฝั่งเซิร์ฟเวอร์
      //      และข้อความ done ซึ่งเป็นแถวข้อมูลเดียวของรอบนั้นบนการ์ด
      return;
    case Phase::Fault:
      // A fault stops the relays wherever the leg is, and it can be halfway up.
      // STOP is the button an operator reaches for, so it brings the leg down and
      // clears the latch when it gets there. The fault was already recorded when
      // it happened, so this lowering reports nothing — see `recovering`.
      // [TH] ความผิดพลาดตัดรีเลย์ไม่ว่าขาจะอยู่ตรงไหน และอาจค้างกลางทาง
      //      ปุ่ม STOP คือปุ่มที่ผู้ใช้เอื้อมไปกด จึงพาขาลงมาและล้างสถานะเมื่อถึงพื้น
      //      ความผิดพลาดถูกบันทึกไปแล้วตอนเกิดเหตุ การลดขาครั้งนี้จึงไม่รายงานอะไร ดูตัวแปร recovering
      recovering = true;
      startAfterRecovery = false;
      enter(Phase::SafeLower, nowMs);
      return;
    case Phase::Lift:
    case Phase::Hold:
    case Phase::Lower:
      enter(Phase::SafeLower, nowMs);  // bring the leg down, then stop — ลดขาลงก่อนแล้วค่อยหยุด
      return;
    case Phase::Rest:
      finish(nowMs, Phase::Idle, false);  // already down — ขาลงอยู่แล้ว หยุดได้เลย
      return;
    case Phase::SafeLower:
      // Already lowering. A STOP here cancels a start that was queued behind a
      // recovery, which is the operator changing their mind mid-descent.
      // [TH] กำลังลดขาอยู่แล้ว การกด STOP ตรงนี้คือการยกเลิกคำสั่งเริ่มที่เข้าคิวรออยู่หลังการลดขา
      //      ซึ่งคือผู้ใช้เปลี่ยนใจระหว่างที่ขากำลังลง
      startAfterRecovery = false;
      return;
  }
}

void SessionSM::forceFault(uint32_t nowMs) {
  if (ph == Phase::Idle || ph == Phase::Fault) return;
  // A recovery lower has no session of its own to end — the fault that preceded
  // it already sent one. Latch back to Fault without raising a second stopped
  // edge, which would put a duplicate row on the card for one session.
  // [TH] การลดขาเพื่อกู้สถานการณ์ไม่มีรอบของตัวเองให้จบ เพราะความผิดพลาดก่อนหน้าส่งไปแล้ว
  //      จึงกลับไปสถานะผิดพลาดโดยไม่ตั้งสัญญาณหยุดซ้ำ ซึ่งจะทำให้มีแถวซ้ำในการ์ดสำหรับรอบเดียว
  if (recovering) {
    recovering = false;
    startAfterRecovery = false;
    enter(Phase::Fault, nowMs);
    return;
  }
  finish(nowMs, Phase::Fault, true);
}

void SessionSM::tick(uint32_t nowMs) {
  uint32_t dt = nowMs - phaseStart;  // เวลาที่อยู่ในสถานะปัจจุบัน
  switch (ph) {
    case Phase::Idle:
    case Phase::Fault:
      return;
    case Phase::Lift:
      if (dt >= cfg.liftMs) enter(Phase::Hold, nowMs);
      return;
    case Phase::Hold:
      if (dt >= cfg.holdMs) enter(Phase::Lower, nowMs);
      return;
    case Phase::Lower:
      if (dt >= cfg.lowerMs) enter(Phase::Rest, nowMs);
      return;
    case Phase::Rest:
      if (dt < cfg.restMs) return;
      repCount++;  // พักครบ -> นับ 1 ครั้ง
      // The target is checked here and nowhere else, because here is the one
      // point in the cycle where the leg is already down: the session can end on
      // the spot with no safe-lower and no fault. A target of 0 means "until
      // STOP", and a target already passed (someone lowered it mid-session) ends
      // the session at the end of this rest for the same reason.
      // [TH] ตรวจเป้าหมายที่นี่ที่เดียว เพราะเป็นจุดเดียวในรอบที่ขาลงถึงพื้นอยู่แล้ว
      //      รอบจึงจบได้ทันทีโดยไม่ต้องลดขาและไม่เป็นความผิดพลาด
      //      ค่า 0 แปลว่าทำจนกว่าจะกด STOP ส่วนเป้าหมายที่เลยมาแล้ว (มีคนปรับลดกลางรอบ)
      //      ก็จบรอบเมื่อสิ้นสุดการพักครั้งนี้ด้วยเหตุผลเดียวกัน
      if (cfg.targetReps != 0 && repCount >= cfg.targetReps) {
        finish(nowMs, Phase::Idle, false);
        return;
      }
      enter(Phase::Lift, nowMs);
      return;
    case Phase::SafeLower:
      if (dt < cfg.lowerMs) return;
      if (recovering) {
        // Lowering after a fault, or ahead of a start that was pressed in one.
        // Either way the session that faulted was already reported, so this ends
        // quietly; a second stopped edge would file it on the card twice.
        // [TH] เป็นการลดขาหลังความผิดพลาด หรือลดขาก่อนคำสั่งเริ่มที่กดตอนอยู่ในสถานะผิดพลาด
        //      ทั้งสองกรณีรอบที่ผิดพลาดถูกรายงานไปแล้ว ตรงนี้จึงจบแบบเงียบ
        //      ถ้าตั้งสัญญาณหยุดซ้ำ รอบเดียวกันจะลงการ์ดสองครั้ง
        recovering = false;
        if (startAfterRecovery) {
          startAfterRecovery = false;
          sessionStart = nowMs;
          repCount = 0;
          startedEdge = true;
          enter(Phase::Lift, nowMs);
          return;
        }
        enter(Phase::Idle, nowMs);
        return;
      }
      finish(nowMs, Phase::Idle, false);  // a clean stop — หยุดตามปกติ
      return;
  }
}

uint32_t SessionSM::elapsedSec(uint32_t nowMs) const {
  // [TH] เวลาที่ผ่านไปของรอบ (วินาที): ว่าง=0, ผิดพลาด=ค่าที่แช่ไว้ตอน fault, อื่นๆ=นับสด
  if (ph == Phase::Idle) return 0;
  // A recovery lowering has no session of its own, so it has no elapsed time
  // either. Measuring from sessionStart would report the age of whatever ran
  // before it — after a power-cut reboot that is the uptime — and running() is
  // true during a recovery, so that number went out on every heartbeat.
  // [TH] การลดขาเพื่อกู้สถานการณ์ไม่มีรอบของตัวเอง จึงไม่มีเวลาที่ผ่านไปของตัวเอง
  //      ถ้าวัดจาก sessionStart จะได้อายุของสิ่งที่ทำงานก่อนหน้า
  //      หลังไฟดับแล้วเปิดใหม่ ค่านั้นคือเวลาที่เครื่องเปิดมา
  //      และระหว่างกู้สถานการณ์ running() เป็นจริง ตัวเลขนั้นจึงถูกส่งออกทุกครั้งที่ส่งสัญญาณชีพ
  if (recovering) return 0;
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
