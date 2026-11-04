// ตรรกะเครื่องสถานะของรอบการทำงาน: ทุกสถานะเปลี่ยนด้วยเวลา ไม่มีลิมิตสวิตช์แล้ว
// Session state-machine logic. Every transition is a clock; there are no switches.
#include "session.h"

// Fixed slack added on top of the proportional descent figure computed below —
// NOT what makes a slower descent reach the bottom; that is the lowerMs/liftMs
// ratio itself. This margin only covers integer truncation in that ratio
// (which biases the result short by under a millisecond) and a deliberate
// lean toward over-driving into the bottom stop rather than stopping short of
// it. Capped by strokeCapMs at cfg.lowerMs, so the margin can never stretch a
// stroke past a full-travel descent.
// [TH] เวลาเผื่อคงที่ที่บวกเพิ่มจากตัวเลขสัดส่วนที่คำนวณไว้ด้านล่าง ไม่ใช่สิ่งที่ทำให้การลงช้ากว่ายังถึงพื้นได้
//      สิ่งนั้นคืออัตราส่วน lowerMs/liftMs เอง เวลาเผื่อนี้ครอบคลุมแค่ความคลาดเคลื่อนจากการปัดเศษจำนวนเต็มในอัตราส่วนนั้น
//      (ซึ่งทำให้ผลลัพธ์สั้นกว่าความเป็นจริงไม่ถึงหนึ่งมิลลิวินาที)
//      และแนวโน้มที่ตั้งใจให้ดันเข้าหากลไกด้านล่างมากกว่าจะหยุดสั้นไป
//      ถูกจำกัดไว้ที่ cfg.lowerMs เสมอโดย strokeCapMs จึงไม่มีทางยืดการเคลื่อนที่เกินกว่าการลดขาแบบเต็มระยะ
static const uint32_t LOWER_MARGIN_MS = 500;

void SessionSM::enter(Phase p, uint32_t nowMs) {
  // Entering a descending phase begins a stroke that may keep driving the same
  // direction as the phase before it, which the actuator cannot see for itself.
  // Raised here, the one place every entry passes through, rather than inferred
  // by the sketch from a phase it happened to observe.
  // [TH] การเข้าสู่สถานะที่สั่งลงคือการเริ่มเคลื่อนที่ครั้งใหม่ ซึ่งอาจสั่งไปทางเดิมกับสถานะก่อนหน้า
  //      ตัวขับเคลื่อนมองไม่เห็นเรื่องนี้เอง จึงยกสัญญาณตรงนี้ซึ่งเป็นจุดเดียวที่การเข้าสถานะทุกครั้งต้องผ่าน
  //      แทนที่จะให้สเก็ตช์เดาจากสถานะที่มันบังเอิญเห็น
  if (p == Phase::SafeLower) {
    strokeRenewEdge = true;
    // How long this descent needs, rather than always a full lowerMs. Travel is
    // timed, so the ELEVATION to give back is the fraction of full travel that was
    // actually driven — the DOWN time for that same fraction, not the UP time spent
    // reaching it. With equal lift/lower times a stop half a second into a
    // four-second lift needs half a second of DOWN; with a slower lower it needs
    // proportionally more. Whatever is left beyond that would be spent stalled
    // against the bottom stop with the limb loaded — every time, on every early
    // stop.
    //
    // Only the two phases whose position is known get the short version. From
    // Hold the leg is fully up, and from Fault or Idle the position is unknown by
    // definition, so those keep the full stroke.
    // [TH] คำนวณว่าการลดขาครั้งนี้ต้องใช้เวลาเท่าไร แทนที่จะใช้ค่าเต็มของ lowerMs เสมอ
    //      เพราะการเดินทางนับด้วยเวลา ระดับที่ต้องคืนจึงเท่ากับสัดส่วนของระยะเต็มที่ขับเคลื่อนไปจริง
    //      คือเวลาลงของสัดส่วนเดียวกันนั้น ไม่ใช่เวลาขึ้นที่ใช้ไปกว่าจะถึงจุดนั้น
    //      ถ้าเวลายกกับเวลาลงเท่ากัน การกดหยุดหลังเริ่มยกไปครึ่งวินาทีจากทั้งหมดสี่วินาที
    //      ต้องการการสั่งลงแค่ครึ่งวินาที แต่ถ้าลงช้ากว่าก็ต้องการเวลามากขึ้นตามสัดส่วน
    //      ส่วนที่เหลือเกินกว่านั้นคือการดันค้างชนกลไกด้านล่างโดยมีขาวางอยู่ ทุกครั้งที่หยุดก่อนกำหนด
    //      ให้ใช้เวลาแบบสั้นเฉพาะสองสถานะที่รู้ตำแหน่งเท่านั้น
    //      จากสถานะค้างขาอยู่บนสุด ส่วนจากสถานะผิดพลาดหรือว่างถือว่าไม่รู้ตำแหน่งอยู่แล้ว
    //      สองกรณีหลังจึงใช้ระยะเต็ม
    uint32_t dt = nowMs - phaseStart;
    if (ph == Phase::Lift) {
      // Travel is timed, not measured, so the elevation reached after dt of
      // lifting is dt/liftMs of full travel, and the DOWN time that returns
      // it is that same fraction of lowerMs — not dt itself. The two only
      // coincide when liftMs and lowerMs happen to match; a slower descent
      // (the normal case, for patient comfort) needs proportionally more.
      // cfg.liftMs is >= 1000 by every clamp, so no divide-by-zero, and the
      // product stays well inside 32 bits even at the 20 s ceiling.
      // [TH] การเดินทางนับด้วยเวลา ไม่ได้วัดจริง ระยะที่ยกไปหลังจากเวลา dt
      //      จึงเท่ากับสัดส่วน dt/liftMs ของระยะเต็ม และเวลาลงที่จะคืนระยะนั้น
      //      ก็ต้องเป็นสัดส่วนเดียวกันของ lowerMs ไม่ใช่ dt ตรงๆ
      //      ทั้งสองจะเท่ากันก็ต่อเมื่อ liftMs กับ lowerMs บังเอิญเท่ากันเท่านั้น
      //      การลงที่ช้ากว่า (กรณีปกติ เพื่อความสบายของผู้ป่วย) ต้องการเวลามากขึ้นตามสัดส่วน
      //      cfg.liftMs มีค่าอย่างน้อย 1000 เสมอจากการจำกัดค่า จึงไม่มีการหารด้วยศูนย์
      //      และผลคูณยังอยู่ในขอบเขต 32 บิตสบายๆ แม้ที่เพดาน 20 วินาที
      uint32_t need = (uint32_t)((uint64_t)dt * cfg.lowerMs / cfg.liftMs) + LOWER_MARGIN_MS;
      strokeCapMs = need < cfg.lowerMs ? need : cfg.lowerMs;
    } else if (ph == Phase::Lower) {
      uint32_t left = dt >= cfg.lowerMs ? 0 : cfg.lowerMs - dt;
      uint32_t need = left + LOWER_MARGIN_MS;
      strokeCapMs = need < cfg.lowerMs ? need : cfg.lowerMs;
    } else {
      strokeCapMs = cfg.lowerMs;
    }
  }
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
  // Out of a latched Fault, lower before lifting. The fault stopped the motor
  // wherever the leg happened to be, and this firmware has no way to find out
  // where that was. The lowering runs first and the start is queued behind it.
  // [TH] ถ้ามาจากสถานะผิดพลาดที่ค้างอยู่ ให้ลดขาก่อนแล้วค่อยยก
  //      ความผิดพลาดตัดมอเตอร์ตรงจุดไหนก็ได้ที่ขาอยู่ และเฟิร์มแวร์นี้ไม่มีทางรู้ว่าตรงไหน
  //      จึงให้การลดขาทำงานก่อน แล้วเข้าคิวเริ่มรอบไว้ข้างหลัง
  // legPositionUnknown covers the reboot case the Fault branch does not: a
  // power cut mid-Lift boots straight to Idle, not Fault, and the state
  // machine has no memory of where the leg was left. See session.h for why
  // this flag exists and when it clears.
  // [TH] legPositionUnknown ครอบคลุมกรณีเปิดเครื่องใหม่ที่สาขาผิดพลาดครอบคลุมไม่ถึง
  //      ไฟตกกลางจังหวะยกขาจะบูตตรงไปสถานะว่าง ไม่ใช่ผิดพลาด และเครื่องสถานะไม่มีความจำว่าขาถูกทิ้งไว้ตรงไหน
  //      ดู session.h ว่าทำไมตัวแปรนี้ถึงมีอยู่และล้างเมื่อไหร่
  if (ph == Phase::Fault || legPositionUnknown) {
    recovering = true;
    startAfterRecovery = true;
    // Zeroed now, not when the queued start finally fires. running() is already
    // true for the whole recovery descent, so the heartbeat and the link state go
    // out during it — carrying the PREVIOUS session's count, which reads on the
    // phone and the CYD as a session that is somehow already part-done before it
    // has begun. elapsedSec() is given exactly this treatment a few lines down.
    // [TH] ล้างค่าตรงนี้ ไม่ใช่ตอนที่คำสั่งเริ่มที่เข้าคิวไว้ทำงานจริง
    //      เพราะ running() เป็นจริงตลอดช่วงลดขากู้สถานการณ์ สัญญาณชีพและสถานะบนลิงก์จึงถูกส่งออกระหว่างนั้น
    //      โดยพาจำนวนครั้งของรอบก่อนหน้าไปด้วย ซึ่งบนมือถือและบนจอจะอ่านได้ว่า
    //      เป็นรอบที่ทำไปแล้วบางส่วนทั้งที่ยังไม่เริ่ม ค่า elapsedSec() ก็ถูกจัดการแบบเดียวกันนี้อยู่ไม่กี่บรรทัดถัดไป
    repCount = 0;
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
      // A fault stops the motor wherever the leg is, and it can be halfway up.
      // STOP is the button an operator reaches for, so it brings the leg down and
      // clears the latch when it gets there. The fault was already recorded when
      // it happened, so this lowering reports nothing — see `recovering`.
      // [TH] ความผิดพลาดตัดมอเตอร์ไม่ว่าขาจะอยู่ตรงไหน และอาจค้างกลางทาง
      //      ปุ่ม STOP คือปุ่มที่ผู้ใช้เอื้อมไปกด จึงพาขาลงมาและล้างสถานะเมื่อถึงพื้น
      //      ความผิดพลาดถูกบันทึกไปแล้วตอนเกิดเหตุ การลดขาครั้งนี้จึงไม่รายงานอะไร ดูตัวแปร recovering
      //
      // repCount is deliberately left alone here, unlike start()'s Fault branch,
      // which zeroes it before queuing a lift. The two are not symmetric: start()
      // is about to begin a NEW session, so the old count showing on the heartbeat
      // and the state message during that descent is stale and misleading. This
      // STOP path begins no new session — it is the tail end of the one that just
      // faulted, already latched into finalRepCount by finish() when the fault
      // happened. Continuing to show that same count while the leg comes down is
      // the honest number, not a leftover.
      // [TH] จงใจไม่ล้าง repCount ตรงนี้ ต่างจากสาขาผิดพลาดใน start() ที่ล้างค่าก่อนเข้าคิวยกขา
      //      ทั้งสองไม่สมมาตรกันโดยเจตนา start() กำลังจะเริ่มรอบใหม่ จำนวนครั้งเก่าที่ยังค้างอยู่บนสัญญาณชีพ
      //      และข้อความสถานะระหว่างการลดขานั้นจึงเป็นค่าเก่าที่ทำให้เข้าใจผิด
      //      ส่วนเส้นทาง STOP นี้ไม่ได้เริ่มรอบใหม่เลย เป็นแค่ช่วงท้ายของรอบที่เพิ่งผิดพลาดไป
      //      ซึ่งถูกบันทึกลง finalRepCount โดย finish() ไปแล้วตอนที่เกิดความผิดพลาด
      //      การยังคงแสดงจำนวนครั้งเดิมนี้ระหว่างที่ขากำลังลงจึงเป็นตัวเลขที่ถูกต้อง ไม่ใช่ค่าตกค้าง
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

void SessionSM::lowerFromIdle(uint32_t nowMs) {
  if (ph != Phase::Idle) return;
  // The recovery path, so it ends quietly and clears legPositionUnknown once the
  // full stroke has run. Entered from Idle, enter() gives it the full lowerMs.
  // [TH] ใช้เส้นทางเดียวกับการลดขากู้สถานการณ์ จึงจบแบบเงียบ
  //      และล้าง legPositionUnknown เมื่อลงครบเต็มระยะ เข้าจากสถานะว่าง enter() จึงให้เวลาเต็ม lowerMs
  recovering = true;
  startAfterRecovery = false;
  enter(Phase::SafeLower, nowMs);
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
      // A Lower stroke that ran its full duration is the one proof, under
      // timed travel, that the leg reached the bottom — clear the unknown
      // flag before Rest is entered, not after, so a fault raised the moment
      // Rest begins still finds the position already known.
      // [TH] การเคลื่อนที่ลงที่ทำงานจบครบเวลาคือหลักฐานเดียวภายใต้การเดินทางแบบจับเวลาว่าขาลงถึงพื้นแล้ว
      //      ล้างสัญญาณ "ไม่ทราบตำแหน่ง" ก่อนเข้าสถานะพัก ไม่ใช่หลังจากนั้น
      //      เพื่อให้ความผิดพลาดที่เกิดขึ้นทันทีที่เข้าสถานะพักยังเห็นว่าตำแหน่งเป็นที่ทราบแล้ว
      if (dt >= cfg.lowerMs) {
        legPositionUnknown = false;
        enter(Phase::Rest, nowMs);
      }
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
      if (dt < strokeCapMs) return;
      // Same signal as the Lower case above, but only when this stroke WAS a
      // full-length one: strokeCapMs == cfg.lowerMs. A shortened SafeLower (a stop
      // taken mid-Lift or mid-Lower) is an arithmetic inference from the drive
      // time, not a measurement — this machine has no position feedback at all —
      // so it must not be trusted to clear the flag just because its own, shorter
      // timer ran out. Nothing downstream ever sets this back to true, so clearing
      // it here on a full stroke cannot strand the machine lowering forever, and
      // leaving it set after a shortened one costs at most one extra full descent
      // before the next START.
      // [TH] สัญญาณเดียวกับกรณีลดลงด้านบน แต่เฉพาะเมื่อการลดขาครั้งนี้เป็นแบบเต็มระยะเท่านั้น
      //      คือ strokeCapMs == cfg.lowerMs การลดขาเพื่อหยุดแบบย่นระยะ (หยุดกลางจังหวะยกหรือกลางจังหวะลง)
      //      เป็นการอนุมานทางคณิตศาสตร์จากเวลาที่ขับเคลื่อนไป ไม่ใช่การวัดจริง
      //      เพราะเครื่องนี้ไม่มีสัญญาณตำแหน่งใดๆ เลย จึงไม่อาจเชื่อให้ล้างสัญญาณนี้
      //      เพียงเพราะตัวจับเวลาที่สั้นกว่าของมันเองหมดเวลา ไม่มีจุดใดตั้งค่านี้กลับเป็นจริงอีก
      //      การล้างตรงนี้ตอนเป็นการลดขาแบบเต็มระยะจึงไม่ทำให้เครื่องค้างวนลดขาไม่รู้จบ
      //      ส่วนการปล่อยให้ค่านี้ยังเป็นจริงหลังการลดขาแบบย่นระยะ มีต้นทุนแค่การลงเต็มระยะเพิ่มอีกหนึ่งครั้ง
      //      ก่อนการกดเริ่มครั้งถัดไป
      if (strokeCapMs == cfg.lowerMs) legPositionUnknown = false;
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
