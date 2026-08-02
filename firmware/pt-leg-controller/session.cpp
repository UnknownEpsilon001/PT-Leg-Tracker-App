// ตรรกะเครื่องสถานะของรอบการทำงาน: ตัดสินใจเปลี่ยนสถานะจากเวลาและลิมิตสวิตช์
// Session state-machine logic: transitions driven by time and the limit switches.
#include "session.h"

void SessionSM::enter(Phase p, uint32_t nowMs) {
  // Entering SafeLower begins a new stroke that may keep driving the same
  // direction as the phase before it, which the actuator cannot see for itself.
  // The edge is raised here, in the one place every entry passes through, rather
  // than inferred by the sketch from a phase it happened to observe: tick() runs
  // before the buttons, so a SafeLower can end and a new one begin between two
  // consecutive looks, and the observer would see no change at all.
  // [TH] การเข้าสู่สถานะลดขาเพื่อหยุดคือการเริ่มเคลื่อนที่ครั้งใหม่ ซึ่งอาจสั่งไปทางเดิมกับสถานะก่อนหน้า
  //      ตัวขับเคลื่อนมองไม่เห็นเรื่องนี้เอง
  //      จึงยกสัญญาณตรงนี้ซึ่งเป็นจุดเดียวที่การเข้าสู่สถานะทุกครั้งต้องผ่าน
  //      แทนที่จะให้สเก็ตช์เดาจากสถานะที่มันบังเอิญเห็น
  //      เพราะ tick() ทำงานก่อนปุ่ม การลดขาครั้งหนึ่งจึงจบและครั้งใหม่เริ่มได้ระหว่างการมองสองครั้งติดกัน
  //      ผู้สังเกตจะไม่เห็นการเปลี่ยนแปลงใดๆ เลย
  if (p == Phase::SafeLower) strokeRenewEdge = true;
  // Commanding a lift is what makes a "still at the bottom" reading stale. Only a
  // confirmed arrival clears it again — see the Lower and SafeLower cases.
  // [TH] การสั่งยกขาคือสิ่งที่ทำให้ค่าที่อ่านว่า "ยังอยู่ล่างสุด" ใช้ไม่ได้อีกต่อไป
  //      จะล้างค่านี้ได้ก็ต่อเมื่อยืนยันการมาถึงตำแหน่งล่างสุดจริงเท่านั้น ดูสถานะลดลงและลดลงเพื่อหยุด
  if (p == Phase::Lift) liftedSinceBottom = true;
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
  // Latched with the other totals, not read back off the live phase later. A
  // RESET taken in the same loop iteration that a fault ended the session moves
  // the machine to Idle before the edge is consumed, and faulted() would then
  // answer "no" for a session that tripped a safety net — the card row and the
  // finish screen would both call it clean.
  // [TH] เก็บค่าพร้อมยอดรวมอื่นๆ ไม่ได้ไปอ่านจากสถานะปัจจุบันทีหลัง
  //      ถ้ามีการกด RESET ในลูปรอบเดียวกับที่รอบจบเพราะความผิดพลาด เครื่องจะไปอยู่สถานะว่างก่อนที่จะอ่านสัญญาณ
  //      แล้ว faulted() จะตอบว่า "ไม่ผิดพลาด" ทั้งที่รอบนั้นชนกลไกความปลอดภัย
  //      แถวในไฟล์และหน้าสรุปจะรายงานว่ารอบนั้นปกติทั้งคู่
  //
  // Passed in rather than derived from `to`, because a safe-lower that ran its
  // cap out without ever reaching the bottom switch ends in Idle by design and is
  // still not a clean session — the leg never came down.
  // [TH] ส่งค่าเข้ามาแทนการเดาจากสถานะปลายทาง เพราะการลดขาลงเพื่อหยุดที่ใช้เวลาจนครบเพดาน
  //      โดยไม่เคยแตะสวิตช์ล่างเลย จะจบที่สถานะว่างตามที่ออกแบบไว้ แต่ยังไม่ถือว่าเป็นรอบที่ปกติ
  //      เพราะขาไม่เคยลงถึงพื้น
  finalFault = fault;
  stoppedEdge = true;
  enter(to, nowMs);
}

void SessionSM::start(uint32_t nowMs) {
  if (running()) return;  // ignore if already running; allowed from Idle or Fault
                          // [TH] ถ้ากำลังทำงานอยู่แล้วให้ข้าม เริ่มได้จากสถานะ ว่าง หรือ ผิดพลาด
  sessionStart = nowMs;
  repCount = 0;
  startedEdge = true;
  recovering = false;
  safeLowerFresh = false;
  // Belongs to the fault that raised it, not to the machine. Left set, it
  // disarmed the FIRST recovery descent of the next, unrelated fault: RESET would
  // clear that fault straight to Idle with the leg still up, having attempted
  // nothing.
  // [TH] ค่านี้เป็นของความผิดพลาดครั้งที่ทำให้มันเกิด ไม่ใช่ของตัวเครื่อง
  //      ถ้าปล่อยค้างไว้ มันจะไปปลดการลดขาครั้งแรกของความผิดพลาดครั้งถัดไปที่ไม่เกี่ยวข้องกันเลย
  //      การกด RESET จะล้างความผิดพลาดนั้นไปสถานะว่างทันทีทั้งที่ขายังอยู่ข้างบนและยังไม่ได้พยายามลดเลย
  recoveryFailed = false;
  enter(Phase::Lift, nowMs);
}

void SessionSM::requestStop(uint32_t nowMs) {
  // [TH] คำขอหยุด: ตอบสนองต่างกันตามสถานะปัจจุบัน เพื่อให้ขาลงถึงพื้นก่อนหยุดเสมอ
  switch (ph) {
    case Phase::Idle:
      // STOP lowers from Idle too, but only when the leg is believed to be up.
      // Idle was the one state with no DOWN command at all, and it is reachable
      // with the leg in the air: a second RESET after a failed recovery lands
      // here, and so does a power cut part-way through a lift. A machine whose
      // leg is up and whose only motion command drives it further up is the trap
      // every other path in this file is written to avoid.
      //
      // Gated on liftedSinceBottom so the ordinary case is untouched: after a
      // normal session the leg is at the bottom, the flag is clear, and STOP at
      // Idle stays the no-op it has always been rather than driving the actuator
      // into the bottom stop every time somebody leans on the button.
      // [TH] ปุ่ม STOP ลดขาจากสถานะว่างได้ด้วย แต่เฉพาะตอนที่เชื่อว่าขาอยู่ข้างบน
      //      สถานะว่างเป็นสถานะเดียวที่ไม่มีคำสั่งลดขาเลย และไปถึงได้ทั้งที่ขายังลอยอยู่
      //      การกด RESET ครั้งที่สองหลังการกู้สถานการณ์ที่ล้มเหลวก็มาจบที่นี่ เช่นเดียวกับไฟดับกลางคันระหว่างยกขา
      //      เครื่องที่ขาอยู่ข้างบนและมีคำสั่งเคลื่อนที่เพียงอย่างเดียวคือสั่งยกขึ้นต่อ
      //      คือกับดักที่เส้นทางอื่นทั้งหมดในไฟล์นี้เขียนขึ้นมาเพื่อเลี่ยง
      //      ใช้เงื่อนไข liftedSinceBottom เพื่อไม่ให้กระทบการใช้งานปกติ
      //      หลังจบรอบตามปกติ ขาจะอยู่ล่างสุดและค่านี้ถูกล้างแล้ว
      //      การกด STOP ตอนว่างจึงยังไม่ทำอะไรเหมือนเดิม แทนที่จะสั่งชุดขับเคลื่อนชนกลไกด้านล่างทุกครั้งที่มีคนเผลอกด
      // Ends under the same rules as a recovery: Idle if the bottom was
      // confirmed, otherwise Fault with recoveryFailed set. An earlier version
      // let this one always return to Idle, on the reasoning that Idle has no
      // fault to go back to — which meant it could never confirm, never latch
      // anything, and never stop being repeatable. Every press then drove the
      // full cap into the mechanical bottom stop, silently, on a screen reading
      // "พร้อม". Latching Fault makes the failure visible and hands the repeat
      // guard to recoveryFailed, exactly as the Fault path already does.
      // [TH] จบด้วยกฎเดียวกับการกู้สถานการณ์ ไปสถานะว่างถ้ายืนยันตำแหน่งล่างสุดได้
      //      ถ้าไม่ได้ให้ไปสถานะผิดพลาดพร้อมตั้งค่า recoveryFailed
      //      รุ่นก่อนหน้าให้เส้นทางนี้กลับไปสถานะว่างเสมอ ด้วยเหตุผลว่าสถานะว่างไม่มีความผิดพลาดให้กลับไปหา
      //      ผลคือมันยืนยันอะไรไม่ได้เลย ไม่ค้างสถานะอะไรไว้เลย และทำซ้ำได้ไม่รู้จบ
      //      การกดแต่ละครั้งจึงสั่งลงจนครบเพดานเวลาชนกลไกด้านล่างแบบเงียบๆ บนจอที่ขึ้นว่าพร้อม
      //      การค้างที่สถานะผิดพลาดทำให้เห็นความล้มเหลว และส่งหน้าที่กันการทำซ้ำให้ recoveryFailed
      //      เหมือนกับที่เส้นทางจากสถานะผิดพลาดทำอยู่แล้ว
      if (!liftedSinceBottom) return;  // believed already down — เชื่อว่าขาอยู่ล่างสุดแล้ว
      if (recoveryFailed) return;      // a descent already failed — เคยลดขาแล้วไม่สำเร็จ
      recovering = true;               // no session to report — ไม่มีรอบให้รายงาน
      safeLowerFresh = true;
      enter(Phase::SafeLower, nowMs);
      return;
    case Phase::Fault:
      // STOP in a latched Fault lowers the leg. A fault stops the relays wherever
      // the leg happens to be, and it can be halfway up — a travel timeout during
      // a lift parks it there. Every other input made that worse: STOP did
      // nothing, RESET only cleared the latch, and START drove the actuator
      // further UP. The patient's leg could not be brought down by any button at
      // all, which is a far worse place to be than a fault that is still latched.
      // The fault is already recorded (finalFault, and the done message went out
      // when it happened), so this lowering reports nothing of its own — see
      // `recovering`.
      // [TH] การกด STOP ตอนอยู่ในสถานะผิดพลาดที่ค้างอยู่ จะสั่งลดขาลง
      //      ความผิดพลาดจะตัดรีเลย์ทันทีไม่ว่าขาจะอยู่ตรงไหน และอาจค้างอยู่กลางทาง
      //      เช่นกรณีที่ยกเกินเวลาที่กำหนด ขาจะค้างอยู่ตรงนั้น
      //      ปุ่มอื่นทำให้แย่ลงทั้งหมด STOP ไม่ทำอะไร RESET แค่ล้างสถานะ ส่วน START สั่งยกขึ้นต่อ
      //      ขาของผู้ป่วยจึงเอาลงไม่ได้เลยด้วยปุ่มใดๆ ซึ่งแย่กว่าการค้างสถานะผิดพลาดไว้มาก
      //      ความผิดพลาดถูกบันทึกไปแล้ว ทั้งค่า finalFault และข้อความ done ที่ส่งไปตอนเกิดเหตุ
      //      การลดขาครั้งนี้จึงไม่รายงานอะไรซ้ำอีก ดูตัวแปร recovering
      recovering = true;
      safeLowerFresh = true;
      enter(Phase::SafeLower, nowMs);
      return;
    case Phase::Lift:
    case Phase::Hold:
    case Phase::Lower:
      safeLowerFresh = true;
      enter(Phase::SafeLower, nowMs);  // bring the leg down, then stop — ลดขาลงก่อนแล้วค่อยหยุด
      return;
    case Phase::Rest:
      finish(nowMs, Phase::Idle, false);  // already down — ขาลงอยู่แล้ว หยุดได้เลย
      return;
    case Phase::SafeLower:
      return;  // already lowering to a clean stop — กำลังลงเพื่อหยุดอยู่แล้ว
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
    enter(Phase::Fault, nowMs);
    return;
  }
  finish(nowMs, Phase::Fault, true);
}

void SessionSM::reset(uint32_t nowMs) {
  // [TH] ปุ่ม RESET: ใช้ได้เฉพาะตอนว่าง/ผิดพลาด ไม่ให้ล้างกลางรอบ (ต้องกด STOP ก่อน)
  if (running()) return;         // must STOP first; RESET never interrupts a session
  repCount = 0;
  // The edges and the finished session's totals are deliberately left alone. A
  // session can finish and a RESET edge be taken inside the same loop iteration —
  // tick() runs before the buttons, and the edges are consumed after them — and
  // clearing them here threw away both the server "stopped" event and the done
  // message, which is the session's only row on the card. The edges are one-shot
  // already, and the totals are read only while an edge is pending; elapsedSec()
  // returns 0 in Idle whatever they hold.
  // [TH] จงใจไม่ล้างค่าขอบสัญญาณและยอดรวมของรอบที่จบไปแล้ว
  //      รอบหนึ่งอาจจบและมีการกด RESET ในลูปรอบเดียวกันได้ เพราะ tick() ทำงานก่อนปุ่ม
  //      และการอ่านขอบสัญญาณอยู่หลังปุ่ม การล้างตรงนี้จึงทิ้งทั้งเหตุการณ์ "stopped" ฝั่งเซิร์ฟเวอร์
  //      และข้อความ done ซึ่งเป็นแถวข้อมูลเดียวของรอบนั้นบนการ์ด
  //      ขอบสัญญาณอ่านได้ครั้งเดียวอยู่แล้ว ส่วนยอดรวมจะถูกอ่านเฉพาะตอนที่ยังมีขอบค้างอยู่
  //      และ elapsedSec() คืนค่า 0 ตอนสถานะว่างไม่ว่าค่าเหล่านี้จะเป็นเท่าใด
  // RESET out of a Fault lowers the leg on its way to Idle, for the same reason
  // STOP does. RESET is the button an operator reaches for to "clear the fault",
  // and going straight to Idle put the machine somewhere STOP is a no-op and only
  // START moves the actuator — upwards, into whatever jammed it. Clearing the
  // latch must not cost the one way down.
  // [TH] การกด RESET ออกจากสถานะผิดพลาดจะลดขาลงระหว่างทางกลับสู่สถานะว่าง ด้วยเหตุผลเดียวกับปุ่ม STOP
  //      RESET คือปุ่มที่ผู้ใช้เอื้อมไปกดเพื่อ "ล้างความผิดพลาด"
  //      การไปสถานะว่างตรงๆ ทำให้เครื่องไปอยู่ในจุดที่ STOP ไม่ทำอะไรเลย
  //      และมีแต่ START ที่ขยับชุดขับเคลื่อน ซึ่งขยับขึ้นไปหาสิ่งที่ทำให้ติดขัดอยู่
  //      การล้างสถานะต้องไม่แลกมาด้วยทางเดียวที่จะเอาขาลง
  if (ph == Phase::Fault && !recoveryFailed) {
    recovering = true;
    safeLowerFresh = true;
    enter(Phase::SafeLower, nowMs);
    return;
  }
  // A second RESET after a descent that could not confirm the bottom clears the
  // fault instead of driving again. The first press always tries to lower; if
  // that did not work, repeating it forever is not help — it is an
  // eight-second stall against whatever is in the way, every time. STOP still
  // commands another descent for an operator who wants one.
  // [TH] การกด RESET ครั้งที่สองหลังการลดขาที่ยืนยันไม่ได้ว่าถึงพื้น จะล้างความผิดพลาดแทนการสั่งลงซ้ำ
  //      การกดครั้งแรกจะพยายามลดขาเสมอ ถ้าไม่ได้ผล การทำซ้ำไปเรื่อยๆ ไม่ได้ช่วยอะไร
  //      มันคือการค้างแปดวินาทีชนกับสิ่งที่ขวางอยู่ทุกครั้ง
  //      ส่วนปุ่ม STOP ยังสั่งลดขาอีกครั้งได้ถ้าผู้ใช้ต้องการ
  recoveryFailed = false;
  enter(Phase::Idle, nowMs);
}

void SessionSM::tick(uint32_t nowMs, bool topPressed, bool bottomPressed) {
  // net 2: both switches pressed at once is physically impossible -> wiring fault
  //
  // SafeLower is exempt, because faulting out of it is what strands the leg. The
  // usual cause of both reading pressed is one open bottom-switch line, which
  // reads pressed forever; with the leg at the top that trips the moment the top
  // switch closes, and faulting there stops the relays with the leg fully up.
  // A safe-lower is time-capped and only ever drives DOWN, so letting it finish
  // is bounded and always moves the leg the safe way.
  // [TH] ยกเว้นสถานะกำลังลดขาเพื่อหยุด เพราะการตัดเป็นความผิดพลาดตอนนั้นคือสิ่งที่ทำให้ขาค้างอยู่บนสุด
  //      สาเหตุปกติของการอ่านได้ว่ากดทั้งคู่คือสายสวิตช์ล่างขาด ซึ่งจะอ่านว่ากดค้างตลอด
  //      ถ้าขาอยู่บนสุด เงื่อนไขนี้จะเป็นจริงทันทีที่สวิตช์บนปิด และการตัดตรงนั้นจะหยุดรีเลย์ทั้งที่ขายกสุด
  //      ส่วนการลดขาเพื่อหยุดมีเพดานเวลาและสั่งลงอย่างเดียว ปล่อยให้ทำจนจบจึงมีขอบเขตและพาขาไปในทางที่ปลอดภัยเสมอ
  if (running() && ph != Phase::SafeLower && topPressed && bottomPressed) {
    finish(nowMs, Phase::Fault, true);
    return;
  }
  uint32_t dt = nowMs - phaseStart;  // เวลาที่อยู่ในสถานะปัจจุบัน
  switch (ph) {
    case Phase::Idle:
    case Phase::Fault:
      return;
    case Phase::Lift:
      if (topPressed) enter(Phase::Hold, nowMs);            // fully up — ยกสุดแล้ว
      else if (dt >= cfg.maxTravelMs) finish(nowMs, Phase::Fault, true);  // net 1 — เกินเวลา = ผิดพลาด
      return;
    case Phase::Hold:
      if (dt >= cfg.holdMs) enter(Phase::Lower, nowMs);     // ค้างครบเวลา -> ลดลง
      return;
    case Phase::Lower:
      if (bottomPressed) {
        liftedSinceBottom = false;  // a real arrival — ถึงตำแหน่งล่างสุดจริง
        enter(Phase::Rest, nowMs);  // fully down — ลงสุดแล้ว
      }
      else if (dt >= cfg.maxTravelMs) finish(nowMs, Phase::Fault, true);  // net 1 — เกินเวลา = ผิดพลาด
      return;
    case Phase::Rest:
      if (dt >= cfg.restMs) {
        repCount++;                 // พักครบ -> นับ 1 ครั้ง แล้วเริ่มยกใหม่
        enter(Phase::Lift, nowMs);
      }
      return;
    case Phase::SafeLower: {
      // clean stop: lower until the bottom switch OR the cap, then Idle (NOT Fault)
      // [TH] หยุดปลอดภัย: ลดลงจนสวิตช์ล่างปิดหรือครบเพดานเวลา แล้วกลับสู่ว่าง (ไม่ใช่ผิดพลาด)
      //
      // The bottom switch is believed only when the top one disagrees. Both
      // reading pressed means at least one of them is lying — the open-line case
      // net 2 exists for — and ending the stroke on a lie is how the leg gets left
      // in the air. Neither is trusted then, and the cap ends the stroke instead.
      // [TH] จะเชื่อสวิตช์ล่างก็ต่อเมื่อสวิตช์บนบอกไม่ตรงกัน ถ้าอ่านได้ว่ากดทั้งคู่
      //      แปลว่าอย่างน้อยหนึ่งตัวให้ค่าผิด ซึ่งคือกรณีสายขาดที่ตาข่ายกันพลาด 2 มีไว้ดัก
      //      การจบการเคลื่อนที่ตามค่าที่ผิดคือสาเหตุที่ทำให้ขาค้างอยู่กลางอากาศ
      //      ตอนนั้นจึงไม่เชื่อทั้งคู่ และให้เพดานเวลาเป็นตัวจบแทน
      //
      // It is also only believed if it was CLEAR when this stroke began. A leg
      // that still needs lowering cannot already be at the bottom, so a bottom
      // line reading pressed from the first tick is an open circuit, not an
      // arrival. Checking `!topPressed` alone was not enough: the canonical net-2
      // fault is exactly that open bottom line, tripping when the leg reaches the
      // top — and about 25 ms into the recovery lower the top switch debounces
      // clear, at which point the lying bottom switch was believed and the stroke
      // ended with the leg still up, having travelled a couple of centimetres.
      // [TH] และจะเชื่อได้ก็ต่อเมื่อสวิตช์ล่างว่างอยู่ตอนที่การเคลื่อนที่นี้เริ่มต้นด้วย
      //      ขาที่ยังต้องลดลงอีกจะอยู่ที่ตำแหน่งล่างสุดไปแล้วไม่ได้
      //      สายสวิตช์ล่างที่อ่านได้ว่ากดตั้งแต่จังหวะแรกจึงคือวงจรเปิด ไม่ใช่การมาถึง
      //      การตรวจแค่ว่าสวิตช์บนไม่ได้ถูกกดยังไม่พอ
      //      ความผิดพลาดแบบตาข่ายที่สองที่พบบ่อยที่สุดคือสายสวิตช์ล่างขาดแบบนี้เอง
      //      ซึ่งจะทำงานตอนขาขึ้นไปถึงบนสุด แล้วราว 25 มิลลิวินาทีหลังเริ่มลดขาเพื่อกู้สถานการณ์
      //      สวิตช์บนจะผ่านกันเด้งเป็นว่าง ตอนนั้นสวิตช์ล่างที่ให้ค่าผิดจะถูกเชื่อ
      //      การเคลื่อนที่จึงจบลงโดยที่ขายังอยู่ข้างบน เคลื่อนที่ลงมาได้แค่ไม่กี่เซนติเมตร
      if (safeLowerFresh) {
        safeLowerFresh = false;
        bottomWasClear = !bottomPressed;
        // Bottom pressed and top clear at the very first tick is not a lie — it
        // is a leg that is already down, which is what a fault latched at the
        // bottom of a stroke looks like. Driving DOWN for the full cap against
        // the mechanical stop, every time, was the alternative, and it never
        // confirmed because the switch was closed before the stroke began.
        // [TH] สวิตช์ล่างถูกกดและสวิตช์บนว่างตั้งแต่จังหวะแรกไม่ใช่ค่าที่ผิด
        //      แต่คือขาที่ลงถึงพื้นอยู่แล้ว ซึ่งเป็นลักษณะของความผิดพลาดที่เกิดตอนจบการเคลื่อนที่ด้านล่าง
        //      ทางเลือกอีกทางคือสั่งลงจนครบเพดานเวลาชนกลไกด้านล่างทุกครั้ง
        //      และจะไม่มีวันยืนยันได้เลยเพราะสวิตช์ปิดอยู่ก่อนการเคลื่อนที่จะเริ่มด้วยซ้ำ
        // ...but only if the leg has not been driven up since the bottom was last
        // confirmed. Without that guard this is worse than the trap it removes: an
        // open bottom line reads pressed forever, so a STOP part-way through a
        // lift satisfies it on the first tick, the stroke "confirms" instantly,
        // the DOWN relay never energizes at all, and the leg is left mid-air in
        // Idle with the session logged clean. The open bottom line is the exact
        // fault this whole block is written around.
        // [TH] แต่ต้องเป็นกรณีที่ขายังไม่ถูกสั่งยกขึ้นนับตั้งแต่ครั้งสุดท้ายที่ยืนยันว่าอยู่ล่างสุด
        //      ถ้าไม่มีเงื่อนไขนี้จะแย่กว่ากับดักที่มันแก้เสียอีก
        //      สายสวิตช์ล่างที่ขาดจะอ่านได้ว่ากดตลอดเวลา การกด STOP กลางคันระหว่างยกขา
        //      จึงเข้าเงื่อนไขตั้งแต่จังหวะแรก การเคลื่อนที่จะ "ยืนยัน" ทันที
        //      รีเลย์ลงไม่ได้จ่ายไฟเลยแม้แต่ครั้งเดียว และขาถูกทิ้งไว้กลางอากาศในสถานะว่าง
        //      โดยบันทึกรอบนั้นว่าปกติ ทั้งที่สายสวิตช์ล่างขาดคือความผิดพลาดที่บล็อกนี้เขียนขึ้นมาเพื่อรับมือ
        alreadyDown = bottomPressed && !topPressed && !liftedSinceBottom;
      }
      bool downConfirmed = alreadyDown || (bottomWasClear && bottomPressed && !topPressed);
      if (!downConfirmed && dt < cfg.maxTravelMs) return;
      if (downConfirmed) liftedSinceBottom = false;  // the leg is down — ขาลงถึงพื้นแล้ว
      if (recovering) {
        // Lowering after a fault. That session was reported when it faulted, so
        // this ends quietly either way — a second stopped edge would file the
        // same session on the card twice.
        //
        // Where it ends depends on whether the leg actually got down. Idle only
        // on a confirmed arrival; otherwise back to Fault, because Idle has no
        // way to lower anything: STOP returns immediately there, RESET just
        // re-enters Idle, and START drives UP. A recovery that ran its cap out
        // without reaching the bottom would otherwise leave the leg in the air,
        // in a state with no DOWN command and nothing on screen to say the
        // attempt failed. Staying in Fault keeps the fault visible and makes
        // every further STOP or RESET buy another full-length descent.
        // [TH] เป็นการลดขาหลังเกิดความผิดพลาด รอบนั้นถูกรายงานไปแล้วตอนที่ผิดพลาด
        //      ตรงนี้จึงจบแบบเงียบๆ ทั้งสองทาง ถ้าตั้งสัญญาณหยุดซ้ำ รอบเดียวกันจะถูกบันทึกลงการ์ดสองครั้ง
        //      ส่วนจะไปจบที่สถานะใดขึ้นกับว่าขาลงถึงพื้นจริงหรือไม่
        //      ไปสถานะว่างเฉพาะเมื่อยืนยันได้ว่าถึงพื้นแล้ว ถ้าไม่ใช่ให้กลับไปสถานะผิดพลาด
        //      เพราะสถานะว่างไม่มีคำสั่งลดขาเลย STOP จะคืนค่าออกทันที RESET ก็แค่เข้าสถานะว่างใหม่
        //      และ START สั่งยกขึ้น การกู้สถานการณ์ที่ใช้เวลาจนครบเพดานโดยไม่ถึงพื้น
        //      จะทิ้งขาไว้กลางอากาศในสถานะที่ไม่มีคำสั่งลง และไม่มีอะไรบนจอบอกว่าล้มเหลว
        //      การอยู่ในสถานะผิดพลาดต่อไปทำให้ยังเห็นความผิดพลาดอยู่
        //      และการกด STOP หรือ RESET ครั้งต่อไปจะได้การลดขาเต็มระยะอีกครั้ง
        recovering = false;
        // A recovery that could not confirm the bottom is remembered, so RESET
        // has somewhere to go afterwards. Without that the machine can only bounce
        // between Fault and an eight-second descent that never confirms.
        // [TH] การกู้สถานการณ์ที่ยืนยันไม่ได้ว่าถึงพื้นจะถูกจดจำไว้ เพื่อให้ปุ่ม RESET มีทางไปต่อ
        //      ถ้าไม่จำไว้ เครื่องจะได้แต่วนไปมาระหว่างสถานะผิดพลาดกับการลดขาแปดวินาทีที่ไม่เคยยืนยันได้
        recoveryFailed = !downConfirmed;
        enter(downConfirmed ? Phase::Idle : Phase::Fault, nowMs);
        return;
      }
      // Ran the cap out without the leg ever reaching the bottom: not a clean
      // session, and not a state to leave the machine in either. Ending in Idle
      // here was the same trap the recovery path had — Idle has no DOWN command,
      // so a failed DOWN relay or a jammed actuator left the leg in the air with
      // the screen reading "ready" and no button able to lower it. This one still
      // reports (unlike a recovery, it IS a session ending), and it latches Fault
      // so STOP and RESET still command a descent.
      // [TH] ใช้เวลาจนครบเพดานโดยที่ขาไม่เคยลงถึงล่างเลย ไม่ใช่รอบที่ปกติ
      //      และไม่ใช่สถานะที่ควรทิ้งเครื่องไว้ด้วย
      //      การจบที่สถานะว่างตรงนี้คือกับดักเดียวกับที่เส้นทางกู้สถานการณ์เคยมี
      //      สถานะว่างไม่มีคำสั่งลดขา รีเลย์ลงที่เสียหรือชุดขับเคลื่อนที่ติดขัดจึงทิ้งขาไว้กลางอากาศ
      //      โดยที่จอขึ้นว่าพร้อมใช้งานและไม่มีปุ่มใดเอาขาลงได้
      //      กรณีนี้ยังต้องรายงาน เพราะต่างจากการกู้สถานการณ์ตรงที่มันคือการจบรอบจริง
      //      และให้ค้างที่สถานะผิดพลาดเพื่อให้ STOP กับ RESET ยังสั่งลดขาได้
      recoveryFailed = false;  // this was a session, not a failed recovery
                               // [TH] นี่คือการจบรอบ ไม่ใช่การกู้สถานการณ์ที่ล้มเหลว
      finish(nowMs, downConfirmed ? Phase::Idle : Phase::Fault, !downConfirmed);
      return;
    }
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
