#pragma once
#include <stdint.h>

// เครื่องสถานะของหนึ่งรอบการทำงาน (session state machine)
// ลำดับ: ยกขึ้น -> ค้าง -> ลดลง -> พัก วนซ้ำ ทุกสถานะจบด้วยเวลา
// Session state machine: Lift -> Hold -> Lower -> Rest, every phase ended by a
// clock. There are no limit switches: travel length IS liftMs / lowerMs.

// The enumerator ORDER here is wire protocol: the UART state message carries
// "phase":int, which is this enum's ordinal, and pt-leg-display/session.h
// holds a matching reduced copy (Phase only) that must stay byte-identical
// in order to it. This is the file people actually edit, since it holds the
// real state machine — reordering these for tidiness silently breaks the
// display's phase name with no build error. Adding a value is safe only at
// the end.
// [TH] ลำดับค่าของ enum นี้คือรูปแบบข้อมูลบนสาย ข้อความสถานะ (state) ผ่าน UART มีฟิลด์
//      "phase":int ซึ่งคือลำดับของ enum นี้ และไฟล์ pt-leg-display/session.h มีสำเนาย่อ
//      (มีแค่ Phase) ที่ต้องมีลำดับตรงกันทุกตัวอักษรกับไฟล์นี้ ไฟล์นี้คือไฟล์ที่คนจะแก้จริง
//      เพราะเป็นที่เก็บเครื่องสถานะจริง การสลับลำดับเพื่อความเรียบร้อยจะทำให้จอแสดงผลแสดงชื่อ
//      สถานะผิดโดยไม่มี error ตอนคอมไพล์ เพิ่มค่าใหม่ได้อย่างปลอดภัยเฉพาะท้าย enum เท่านั้น
// [TH] สถานะของรอบ: ว่าง / ยกขึ้น / ค้าง / ลดลง / พัก / กำลังลงเพื่อหยุด / ผิดพลาด / รอยืนยัน
enum class Phase { Idle, Lift, Hold, Lower, Rest, SafeLower, Fault, Confirm };

struct CycleCfg {
  uint32_t liftMs;       // เวลาที่รีเลย์ยกขึ้นจ่ายไฟต่อหนึ่งจังหวะ
  uint32_t lowerMs;      // เวลาที่รีเลย์ลดลงจ่ายไฟต่อหนึ่งจังหวะ
  uint32_t holdMs;       // เวลาค้างตอนยกสุด
  uint32_t restMs;       // เวลาพักตอนลงสุด
  uint32_t maxTravelMs;  // ceiling liftMs/lowerMs are clamped to — เพดานที่ใช้จำกัดสองค่าแรก
  uint16_t targetReps;   // stop after this many reps; 0 = until STOP — 0 = ไม่จำกัด
};

// How long a confirmation waits for a human before giving up. Nothing is
// committed while it waits, so expiry is always safe — it returns to Idle
// rather than starting.
// [TH] เวลาที่หน้ายืนยันจะรอคนกดก่อนเลิกรอ ระหว่างรอยังไม่มีอะไรถูกยืนยัน
//      การหมดเวลาจึงปลอดภัยเสมอ คือกลับไปสถานะว่าง ไม่ใช่เริ่มทำงาน
static const uint32_t CONFIRM_TIMEOUT_MS = 30000;

class SessionSM {
 public:
  CycleCfg cfg{7000, 7000, 10000, 10000, 8000, 10};

  // Idle/Fault -> Confirm. Commits the machine to nothing: the actuator is
  // untouched and both relays stay off, from either origin. Accepting Fault
  // matters because start() also accepts Fault (and forceFault() can latch
  // one at any time), so refusing it here would make START-in-Fault a silent
  // no-op once panel buttons route through this call, quietly retiring an
  // existing recovery route. Confirming from Fault still gets the mandatory
  // lower-before-lift recovery at the real start -- see the legPositionUnknown
  // line below for how that survives the Fault -> Confirm -> Lift detour.
  // The pending target seeds from the stored default, which is the
  // prescription; editing it on the confirm screen changes only this session.
  // [TH] ว่าง/ผิดพลาด -> รอยืนยัน ยังไม่ผูกมัดอะไรทั้งสิ้น ไม่แตะชุดขับเคลื่อนและรีเลย์ดับทั้งคู่
  //      ไม่ว่าจะมาจากสถานะไหน การยอมรับสถานะผิดพลาดสำคัญเพราะ start() ก็ยอมรับสถานะผิดพลาดเช่นกัน
  //      (และ forceFault() ตั้งสถานะผิดพลาดได้ทุกเมื่อ) ถ้าปฏิเสธตรงนี้ ปุ่ม START ตอนอยู่ในสถานะผิดพลาด
  //      จะกลายเป็นไม่ทำอะไรเลยแบบเงียบๆ ทันทีที่ปุ่มหน้าปัดเปลี่ยนมาผ่านฟังก์ชันนี้
  //      ซึ่งเท่ากับเลิกใช้เส้นทางกู้สถานการณ์ที่มีอยู่เดิมไปโดยไม่ตั้งใจ การยืนยันจากสถานะผิดพลาด
  //      ยังคงได้รับการลดขาก่อนเสมอตอนเริ่มจริง ดูบรรทัด legPositionUnknown ด้านล่างว่าทำไมเรื่องนี้
  //      ถึงยังเป็นจริงแม้จะอ้อมผ่านสถานะรอยืนยันก่อนไปถึงสถานะยกขา ค่าเป้าหมายที่รอยืนยันเริ่มจากค่าตั้งต้น
  //      ที่บันทึกไว้ ซึ่งเป็นค่าที่แพทย์สั่ง การแก้ที่หน้ายืนยันมีผลเฉพาะรอบนี้เท่านั้น
  void requestStart(uint32_t nowMs);

  // Mirrors what the operator is looking at on the display. Ignored outside
  // Confirm, so a late or stray message can never retarget a running session.
  // Also extends the 30 s expiry, exactly like any other activity would --
  // without it, stepping the value with repeated taps (10 -> 45 is 35 of
  // them) burns down the same fixed window entry itself started, and the
  // screen can vanish mid-edit even with someone actively using it. Takes
  // nowMs for exactly that reason -- signature changed from Task 2's brief.
  // [TH] สะท้อนตัวเลขที่ผู้ใช้กำลังดูอยู่บนจอ จะถูกละเว้นถ้าไม่ได้อยู่สถานะรอยืนยัน
  //      ข้อความที่มาช้าหรือหลงมาจึงเปลี่ยนเป้าหมายของรอบที่กำลังทำงานอยู่ไม่ได้
  //      ยังต่อเวลาหมดอายุ 30 วินาทีด้วยเหมือนกิจกรรมอื่นๆ ถ้าไม่ทำแบบนี้ การกดปรับค่าซ้ำๆ
  //      (เช่นจาก 10 ไป 45 คือกด 35 ครั้ง) จะกินเวลาจากหน้าต่างเดิมตั้งแต่ตอนเข้าสถานะ
  //      และหน้าจออาจหายไปกลางการแก้ค่าทั้งที่มีคนกำลังใช้งานอยู่จริง จึงต้องรับค่า nowMs ด้วยเหตุนี้
  void setPendingTarget(uint32_t nowMs, uint16_t reps);

  // Confirm -> Lift, running the pending target for this session only.
  // Writes sessionTargetReps, never cfg.targetReps -- the stored prescription
  // is not merely "restored later", it is never touched by this call at all,
  // so there is nothing to put back once the session ends. See
  // sessionTargetReps and target() for the field this actually feeds.
  // [TH] รอยืนยัน -> ยกขึ้น ใช้ค่าเป้าหมายที่รอยืนยันเฉพาะรอบนี้เท่านั้น
  //      เขียนลง sessionTargetReps ไม่แตะ cfg.targetReps เลย ค่าที่แพทย์สั่งไม่ได้แค่ "ถูกคืนค่าทีหลัง"
  //      แต่ไม่ถูกแตะโดยฟังก์ชันนี้เลยตั้งแต่ต้น จึงไม่มีอะไรต้องคืนค่าเมื่อรอบจบ
  //      ดู sessionTargetReps และ target() ว่าค่านี้ไปป้อนอะไรจริงๆ
  void confirmStart(uint32_t nowMs);

  // Confirm -> Idle, discarding the pending target. requestStart() and
  // setPendingTarget() never committed anything to cfg.targetReps or
  // sessionTargetReps, so there is nothing else to undo -- including when
  // Confirm was entered from Fault: legPositionUnknown, set by requestStart()
  // in that case, is untouched here and stays armed for the next start.
  // [TH] รอยืนยัน -> ว่าง ทิ้งค่าเป้าหมายที่รอยืนยัน เพราะ requestStart() กับ setPendingTarget()
  //      ไม่เคยผูกมัดอะไรไว้กับ cfg.targetReps หรือ sessionTargetReps เลย จึงไม่มีอะไรอื่นต้องย้อนคืน
  //      รวมถึงตอนที่เข้าสถานะรอยืนยันมาจากสถานะผิดพลาดด้วย: legPositionUnknown ที่ requestStart()
  //      ตั้งไว้ในกรณีนั้นจะไม่ถูกแตะตรงนี้ และยังคงพร้อมทำงานสำหรับการเริ่มครั้งถัดไป
  void cancelConfirm(uint32_t nowMs);
  uint16_t pendingTarget() const { return pendingTargetReps; }
  bool confirming() const { return ph == Phase::Confirm; }

  // Idle/Fault -> Lift. From Fault the latch is cleared and the leg is lowered
  // first: with no limit switches there is no way to know where it is, and
  // lowering a leg that is already down costs one stroke against the bottom stop
  // while lifting one that is already up drives it into the top.
  //
  // The same is true from Idle right after boot: a reboot does not put the
  // machine in Fault, it puts it in Idle, and a mains blip or brown-out that
  // lands mid-Lift leaves the leg raised with the state machine none the
  // wiser. legPositionUnknown is what catches that case — it starts true and
  // is cleared only once a Lower stroke has actually run to completion, so
  // the first START after any boot takes the Fault branch too, however the
  // machine got to Idle.
  // [TH] จากสถานะว่างหรือผิดพลาดไปสถานะยกขา ถ้ามาจากสถานะผิดพลาดจะล้างสถานะและลดขาลงก่อน
  //      เพราะไม่มีลิมิตสวิตช์แล้วจึงไม่รู้ว่าขาอยู่ตรงไหน
  //      การลดขาที่อยู่ล่างสุดอยู่แล้วเสียแค่การเคลื่อนที่หนึ่งครั้งชนกลไกด้านล่าง
  //      แต่การยกขาที่อยู่บนสุดอยู่แล้วคือการดันเข้าหากลไกด้านบน
  //
  //      เรื่องเดียวกันนี้จริงสำหรับสถานะว่างหลังเปิดเครื่องใหม่ด้วย: การรีเซ็ตเครื่องไม่ได้พาไปสถานะ
  //      ผิดพลาด แต่พาไปสถานะว่าง และไฟตกหรือไฟกระตุกกลางจังหวะยกขาจะทิ้งขาไว้ข้างบน
  //      โดยที่เครื่องสถานะไม่รู้ตัว legPositionUnknown คือสิ่งที่จับกรณีนี้ไว้
  //      เริ่มต้นเป็นจริงและจะถูกล้างก็ต่อเมื่อการเคลื่อนที่ลงทำงานจบครบจริงเท่านั้น
  //      ปุ่มเริ่มครั้งแรกหลังเปิดเครื่องทุกครั้งจึงเข้าเส้นทางเดียวกับสถานะผิดพลาดเสมอ
  //      ไม่ว่าเครื่องจะมาถึงสถานะว่างด้วยวิธีใด
  void start(uint32_t nowMs);

  // Running -> SafeLower -> Idle, reporting the session.
  // Fault -> SafeLower -> Idle, quiet: that session was reported when it faulted,
  // and a second stopped edge would file it on the card twice.
  // Idle -> zero the rep count. This is what replaces the retired RESET button.
  // [TH] กำลังทำงาน -> ลดขาเพื่อหยุด -> ว่าง พร้อมรายงานรอบนั้น
  //      ผิดพลาด -> ลดขาเพื่อหยุด -> ว่าง แบบเงียบ เพราะรอบนั้นถูกรายงานไปแล้วตอนที่ผิดพลาด
  //      ถ้าตั้งสัญญาณหยุดซ้ำ รอบเดียวกันจะลงการ์ดสองครั้ง
  //      ว่าง -> ล้างจำนวนครั้ง ซึ่งเป็นสิ่งที่มาแทนปุ่ม RESET ที่เลิกใช้แล้ว
  void requestStop(uint32_t nowMs);

  void forceFault(uint32_t nowMs);   // external safety trip (actuator cap)
                                     // [TH] บังคับเข้าสถานะผิดพลาดจากภายนอก (เพดานเวลารีเลย์)

  // Drop a start queued behind a recovery lowering, without touching the
  // lowering itself. For a genuine actuator-cap trip that lands mid-SafeLower
  // and is deliberately swallowed there (the descent must not be aborted the
  // same iteration it was entered) — but a trip is a trip, and a start still
  // queued behind that descent must not fire the instant the timer expires:
  // the cap may have been reached because the descent itself is in trouble,
  // and lifting straight into that is the same hazard forceFault exists to
  // stop for every other trip.
  // [TH] ยกเลิกคำสั่งเริ่มที่เข้าคิวรออยู่หลังการลดขากู้สถานการณ์ โดยไม่แตะการลดขาเอง
  //      ใช้กับการตัดเพดานเวลาจริงที่เกิดกลางสถานะลดขาเพื่อหยุดและถูกกลืนไปโดยตั้งใจตรงนั้น
  //      (การลดขาต้องไม่ถูกยกเลิกในลูปเดียวกับที่เพิ่งเข้าสถานะนี้)
  //      แต่การตัดก็คือการตัด คำสั่งเริ่มที่ยังเข้าคิวรออยู่หลังการลดขานั้นต้องไม่ทำงานทันทีที่หมดเวลา
  //      เพราะเพดานอาจถูกชนเนื่องจากการลดขาเองกำลังมีปัญหา การยกขึ้นทันทีจึงเป็นอันตรายแบบเดียว
  //      กับที่ forceFault มีไว้ป้องกันสำหรับการตัดอื่นทุกครั้ง
  void cancelQueuedStart() { startAfterRecovery = false; }

  // Change the target mid-session as well as at rest, because both UIs (the
  // CYD's live stepper and the app's /target route) can edit it while the
  // machine runs, and both are meant to move the stored prescription too --
  // that is what makes this a live, persistent edit rather than a
  // confirm-screen one-off. Writes cfg.targetReps AND sessionTargetReps
  // together so the edit actually reaches what tick() is counting toward, not
  // just what a settings screen echoes back; leaving sessionTargetReps
  // unwritten here would silently stop a live mid-session edit from ending
  // the session any earlier, while still looking like it took effect
  // everywhere else. confirmStart() deliberately does NOT go through this
  // setter -- a confirm-screen edit is session-only and must not move the
  // prescription, which is the whole point of the confirm screen.
  // Lowering it below the reps already done ends the session at the end of
  // the current Rest, which is where the check lives.
  // [TH] เปลี่ยนเป้าหมายได้ทั้งตอนว่างและตอนกำลังทำงาน เพราะทั้งสองหน้าจอ (ตัวปรับค่าแบบสดบนจอ CYD
  //      กับเส้นทาง /target ของแอป) แก้ได้ระหว่างเครื่องเดิน และทั้งสองตั้งใจให้ขยับค่าที่แพทย์สั่งด้วย
  //      นั่นคือสิ่งที่ทำให้เป็นการแก้แบบสดที่คงอยู่ถาวร ไม่ใช่ค่าครั้งเดียวจากหน้ายืนยัน
  //      เขียนทั้ง cfg.targetReps และ sessionTargetReps พร้อมกัน เพื่อให้การแก้นี้มีผลกับสิ่งที่ tick()
  //      กำลังนับจริง ไม่ใช่แค่ค่าที่หน้าตั้งค่าสะท้อนกลับไป ถ้าไม่เขียน sessionTargetReps ด้วย
  //      การแก้แบบสดกลางรอบจะไม่ทำให้รอบจบเร็วขึ้นจริง ทั้งที่ดูเหมือนมีผลทุกที่อื่น
  //      confirmStart() จงใจไม่ผ่านฟังก์ชันนี้ เพราะการแก้ที่หน้ายืนยันมีผลเฉพาะรอบนี้เท่านั้น
  //      ต้องไม่ไปขยับค่าที่แพทย์สั่ง ซึ่งเป็นจุดประสงค์หลักของหน้ายืนยัน
  //      ถ้าตั้งต่ำกว่าจำนวนครั้งที่ทำไปแล้ว รอบจะจบเมื่อสิ้นสุดสถานะพักครั้งปัจจุบัน ซึ่งเป็นจุดที่ตรวจค่านี้
  void setTarget(uint16_t reps) {
    cfg.targetReps = reps;
    sessionTargetReps = reps;
  }

  // What a running (or just-faulted) session actually counts toward, which
  // may be a confirm-screen one-off and so may differ from the stored
  // prescription; the stored prescription itself the rest of the time (Idle,
  // or Confirm before it is committed), since nothing has a target of its own
  // yet. Feeds the "target" field of the heartbeat and the link state message
  // -- see firmware/link-protocol.md's "a session ends by itself when reps
  // reaches target", which is only true on the wire if this reports the same
  // number tick() is actually comparing against.
  // [TH] เป้าหมายที่รอบที่กำลังทำงาน (หรือเพิ่งผิดพลาด) กำลังนับจริง ซึ่งอาจเป็นค่าครั้งเดียวจากหน้ายืนยัน
  //      จึงอาจไม่ตรงกับค่าที่แพทย์สั่ง ส่วนเวลาอื่น (ว่าง หรือรอยืนยันที่ยังไม่กดยืนยัน) จะเป็นค่าที่แพทย์สั่งเอง
  //      เพราะยังไม่มีรอบที่มีเป้าหมายเป็นของตัวเอง ค่านี้ใช้ป้อนฟิลด์ "target" ในสัญญาณชีพและข้อความสถานะ
  //      ดู firmware/link-protocol.md ที่ว่า "รอบจบเองเมื่อ reps ถึง target" ซึ่งจะเป็นจริงบนสายจริง
  //      ก็ต่อเมื่อค่านี้รายงานตัวเลขเดียวกับที่ tick() กำลังเทียบอยู่จริงเท่านั้น
  uint16_t target() const {
    return (ph == Phase::Idle || ph == Phase::Confirm) ? cfg.targetReps : sessionTargetReps;
  }

  void tick(uint32_t nowMs);
  // Confirm does not count as a session: the actuator is untouched and nothing
  // is committed until confirmStart(). This answer is load-bearing well beyond
  // the lamps — it also gates the heartbeat interval, the mid-session settings
  // deferral, and the display's gear button and logo screen, and every one of
  // those wants "nothing is moving" for as long as the confirm screen is up.
  // [TH] สถานะรอยืนยันไม่นับเป็นรอบทำงาน ไม่แตะชุดขับเคลื่อนและยังไม่มีอะไรถูกผูกมัด
  //      จนกว่าจะเรียก confirmStart() คำตอบนี้ถูกใช้กว้างกว่าแค่ไฟสัญญาณมาก
  //      ยังคุมช่วงเวลาสัญญาณชีพ การเลื่อนค่าตั้งค่าระหว่างรอบทำงาน และปุ่มเฟือง/หน้าโลโก้บนจอด้วย
  //      ทุกจุดเหล่านั้นต้องการคำตอบว่า "ไม่มีอะไรเคลื่อนที่" ตลอดเวลาที่หน้ายืนยันเปิดอยู่
  bool running() const {
    return ph != Phase::Idle && ph != Phase::Fault && ph != Phase::Confirm;
  }
  bool faulted() const { return ph == Phase::Fault; }
  Phase phase() const { return ph; }
  uint32_t elapsedSec(uint32_t nowMs) const;
  uint16_t reps() const { return repCount; }
  bool takeStartedEdge();  // อ่าน+ล้างสัญญาณ "เพิ่งเริ่มรอบ" (edge)
  // Read+clear "a session just ended". Raised only by finish(), i.e. exactly once
  // per session that is reported — so NOT by a lowering started from Fault, which
  // is not a session.
  // [TH] อ่านและล้างสัญญาณ "รอบเพิ่งจบ" ยกโดย finish() เท่านั้น คือครั้งเดียวต่อรอบที่ถูกรายงาน
  //      จึงไม่ยกให้การลดขาที่เริ่มจากสถานะผิดพลาด เพราะนั่นไม่ใช่รอบการทำงาน
  bool takeStoppedEdge();
  uint32_t finalElapsedSec() const { return finalElapsed; }
  uint16_t finalReps() const { return finalRepCount; }
  // Whether the session that just ended ended in a fault. Use this and not
  // faulted() when reporting a finished session: faulted() reads the live phase.
  // [TH] รอบที่เพิ่งจบจบเพราะความผิดพลาดหรือไม่ ตอนรายงานรอบที่จบแล้วให้ใช้ค่านี้ ไม่ใช่ faulted()
  bool finalFaulted() const { return finalFault; }
  // One-shot: a new stroke has begun that may drive the same direction as the
  // last one, so the actuator's energize clock has to be restarted. Read once per
  // loop, before the relays are commanded.
  // [TH] อ่านได้ครั้งเดียว: มีการเคลื่อนที่ครั้งใหม่ที่อาจสั่งไปทางเดิม ต้องเริ่มจับเวลาการจ่ายไฟใหม่
  //      อ่านหนึ่งครั้งต่อลูป ก่อนสั่งรีเลย์
  bool takeStrokeRenewEdge();

 private:
  void enter(Phase p, uint32_t nowMs);
  void finish(uint32_t nowMs, Phase to, bool fault);
  Phase ph = Phase::Idle;
  uint32_t phaseStart = 0, sessionStart = 0;
  // How long the safe-lower currently running should drive. Computed once, in
  // enter(), from the phase it was entered from: a descent only has to give back
  // the travel that was actually driven, and driving the full lowerMs regardless
  // means the rest of the stroke is a stall against the bottom stop.
  // [TH] เวลาที่การลดขาเพื่อหยุดครั้งที่กำลังทำงานอยู่ควรใช้ คำนวณครั้งเดียวใน enter()
  //      จากสถานะที่เข้ามา เพราะการลดขาต้องคืนแค่ระยะที่ขับเคลื่อนไปจริง
  //      ถ้าใช้ค่า lowerMs เต็มทุกครั้ง ส่วนที่เหลือของการเคลื่อนที่คือการดันค้างชนกลไกด้านล่าง
  uint32_t strokeCapMs = 0;
  uint16_t repCount = 0;
  // The target the confirm screen is showing and may still be edited. Seeded
  // from cfg.targetReps by requestStart() and copied into sessionTargetReps
  // by confirmStart() only once the operator actually confirms — see
  // setPendingTarget().
  // [TH] เป้าหมายที่หน้ายืนยันกำลังแสดงและยังแก้ไขได้ เริ่มจากค่า cfg.targetReps โดย requestStart()
  //      และถูกคัดลอกไปยัง sessionTargetReps โดย confirmStart() ก็ต่อเมื่อผู้ใช้กดยืนยันจริงเท่านั้น
  //      ดู setPendingTarget()
  uint16_t pendingTargetReps = 0;
  // What the CURRENT (or most recently run/faulted) session actually counts
  // toward — see target()'s comment for the full contract. Defaulted to 0,
  // which is never observed: target() only reads this outside Idle/Confirm,
  // and by the time the phase first leaves those two, either start() or
  // confirmStart() has always just written a real value here first. Not the
  // same field as cfg.targetReps: that one is permanently the stored
  // prescription and this one may be a confirm-screen one-off that must not
  // reach it — see confirmStart() and setTarget().
  // [TH] เป้าหมายที่รอบปัจจุบัน (หรือรอบล่าสุดที่ทำงาน/ผิดพลาด) กำลังนับจริง ดูคอมเมนต์ของ target()
  //      สำหรับสัญญาว่าค่านี้หมายถึงอะไรทั้งหมด ค่าเริ่มต้นเป็น 0 แต่ไม่มีทางถูกอ่านเจอ เพราะ target()
  //      จะอ่านค่านี้เฉพาะนอกสถานะว่าง/รอยืนยันเท่านั้น และก่อนที่สถานะจะออกจากสองสถานะนั้นเป็นครั้งแรก
  //      ต้องมี start() หรือ confirmStart() เขียนค่าจริงลงมาก่อนเสมอ ไม่ใช่ตัวแปรเดียวกับ cfg.targetReps
  //      ตัวนั้นคือค่าที่แพทย์สั่งถาวร ส่วนตัวนี้อาจเป็นค่าครั้งเดียวจากหน้ายืนยันซึ่งต้องไม่ไปถึงตัวนั้น
  //      ดู confirmStart() กับ setTarget()
  uint16_t sessionTargetReps = 0;
  uint32_t finalElapsed = 0;
  uint16_t finalRepCount = 0;
  bool finalFault = false;
  // This SafeLower is bringing the leg down rather than ending a session — set
  // from Fault (that session was reported when it faulted) and from a start out
  // of Fault (the session has not begun yet). Either way it must not raise a
  // stopped edge. Invariant: recovering implies SafeLower.
  // [TH] การลดขาครั้งนี้เป็นการเอาขาลง ไม่ใช่การจบรอบ
  //      ตั้งจากสถานะผิดพลาด ซึ่งรอบนั้นถูกรายงานไปแล้ว และตั้งจากการกดเริ่มขณะอยู่ในสถานะผิดพลาด
  //      ซึ่งรอบยังไม่เริ่ม ทั้งสองกรณีต้องไม่ตั้งสัญญาณหยุด
  //      ข้อกำหนด: ถ้า recovering เป็นจริง สถานะต้องเป็นลดขาเพื่อหยุดเสมอ
  bool recovering = false;
  // A start is waiting for the recovery lowering above to finish. Consumed by the
  // SafeLower case, which then lifts instead of going to Idle.
  // [TH] มีคำสั่งเริ่มรออยู่ระหว่างที่การลดขาข้างต้นทำงาน
  //      สถานะลดขาเพื่อหยุดจะอ่านค่านี้แล้วยกขาต่อแทนที่จะกลับไปสถานะว่าง
  bool startAfterRecovery = false;
  bool strokeRenewEdge = false;
  bool startedEdge = false, stoppedEdge = false;
  // True until a full-length descent has run to completion at least once. Set
  // at construction (and stays true across a reboot, since this is not
  // persisted) so the first start of any boot is treated like a start out of
  // Fault: lower first, because a power cut mid-Lift boots to Idle, not
  // Fault, and leaves the real position unknown. Cleared in tick(), on a
  // Lower that reaches cfg.lowerMs, or a SafeLower whose own strokeCapMs was
  // set to cfg.lowerMs (i.e. NOT one of the shortened ones enter() computes
  // from a mid-Lift or mid-Lower stop). A shortened SafeLower runs only as
  // long as an arithmetic inference from the drive time says it needs to —
  // this machine has no position feedback at all — so its timer reaching zero
  // is not the same signal as the leg actually being at the bottom, and must
  // not clear this flag. Set back to true only by this class's own
  // initializer and by one deliberate exception -- requestStart() re-arms it
  // when leaving a latched Fault through Confirm, because by the time
  // confirmStart() later calls start(), ph reads Confirm rather than Fault,
  // and start()'s recovery check would otherwise miss both conditions
  // whenever an earlier session had already cleared this flag. See
  // requestStart() for why that one-shot response to a specific event cannot
  // become the re-arm-into-a-loop hazard this comment used to rule out
  // unconditionally: nothing in the running/ticking path ever sets it, only
  // the act of leaving Fault without having lowered does, and a cancelled or
  // expired Confirm just leaves it true until a real recovery eventually runs
  // — it never drives the actuator by itself.
  // [TH] เป็นจริงจนกว่าการลดขาแบบเต็มระยะจะทำงานจบครบอย่างน้อยหนึ่งครั้ง ตั้งไว้ตั้งแต่สร้างอ็อบเจกต์
  //      (และเป็นจริงข้ามการเปิดเครื่องใหม่เสมอ เพราะค่านี้ไม่ถูกบันทึกลง NVS)
  //      เพื่อให้การเริ่มครั้งแรกของการเปิดเครื่องทุกครั้งถูกปฏิบัติเหมือนการเริ่มจากสถานะผิดพลาด
  //      คือลดขาก่อนเสมอ เพราะไฟตกกลางจังหวะยกขาจะรีเซ็ตเครื่องไปสถานะว่าง ไม่ใช่สถานะผิดพลาด
  //      และทิ้งตำแหน่งจริงไว้โดยไม่มีใครรู้ ถูกล้างใน tick() เมื่อการลดลงปกติทำงานจบครบ cfg.lowerMs
  //      หรือเมื่อการลดขาเพื่อหยุดที่มี strokeCapMs ของตัวเองเท่ากับ cfg.lowerMs พอดี
  //      (คือไม่ใช่แบบย่นระยะที่ enter() คำนวณจากการหยุดกลางจังหวะยกหรือกลางจังหวะลง)
  //      การลดขาเพื่อหยุดแบบย่นระยะทำงานสั้นเท่าที่การอนุมานทางคณิตศาสตร์จากเวลาที่ขับเคลื่อนไปบอกว่าต้องใช้เท่านั้น
  //      เครื่องนี้ไม่มีสัญญาณตำแหน่งใดๆ เลย ตัวจับเวลาของมันหมดเวลาจึงไม่ใช่สัญญาณเดียวกับที่บอกว่า
  //      ขาอยู่ล่างสุดจริง และต้องไม่ล้างสัญญาณนี้ ตั้งค่ากลับเป็นจริงได้เฉพาะจากค่าเริ่มต้นของคลาสนี้เอง
  //      กับข้อยกเว้นที่ตั้งใจไว้หนึ่งจุดเท่านั้น คือ requestStart() จะตั้งค่านี้กลับเป็นจริงเมื่อออกจากสถานะผิดพลาด
  //      ที่ค้างอยู่ผ่านสถานะรอยืนยัน เพราะตอนที่ confirmStart() เรียก start() ในภายหลัง ph จะอ่านได้เป็นรอยืนยัน
  //      ไม่ใช่ผิดพลาดแล้ว และการตรวจการกู้สถานการณ์ของ start() จะพลาดทั้งสองเงื่อนไข ถ้าไม่ทำเช่นนี้
  //      ในกรณีที่รอบก่อนหน้าเคยล้างสัญญาณนี้ไปแล้ว ดู requestStart() ว่าทำไมการตอบสนองครั้งเดียวต่อเหตุการณ์
  //      เฉพาะจุดหนึ่งนี้จึงไม่กลายเป็นอันตรายย้อนตัวเองกลับเข้าวงวนไม่รู้จบที่คอมเมนต์นี้เคยกันไว้แบบไม่มีเงื่อนไข
  //      ไม่มีจุดใดในเส้นทางทำงาน/นับเวลาที่ตั้งค่านี้เลย มีเพียงการออกจากสถานะผิดพลาดโดยยังไม่ได้ลดขาเท่านั้น
  //      ที่ตั้งค่านี้ และสถานะรอยืนยันที่ถูกยกเลิกหรือหมดเวลาจะปล่อยให้ค่านี้เป็นจริงค้างไว้
  //      จนกว่าการกู้สถานการณ์จริงจะทำงานในที่สุด ซึ่งจะไม่สั่งชุดขับเคลื่อนด้วยตัวมันเองเลย
  //
  // One case here ends up optimistic rather than exact: a STOP taken at the very
  // end of a Lift computes a descent longer than lowerMs, caps to lowerMs, and
  // clears this flag the same as a genuine full-length Lower — but Lift ->
  // SafeLower is a live reversal, so actuator.cpp parks the new DOWN request
  // behind DEADTIME_MS (50 ms) while this phase's own timer, started at phase
  // entry, keeps counting through that gap. A capped full-length SafeLower
  // entered from Lift therefore only drives DOWN for lowerMs - DEADTIME_MS,
  // leaving the leg a hair short of the bottom — about 0.6% of travel high at
  // liftSec=2/lowerSec=8, about 0.7% at 7/7. That is inside the mechanical slop
  // of a machine with a hard bottom stop, so "at the bottom" here is accurate to
  // within roughly a percent of travel, not exact.
  //
  // The flag also does not clear from the Lift branch at one instant but across
  // a band: whenever floor(dt*lowerMs/liftMs) + LOWER_MARGIN_MS >= lowerMs,
  // which is the last 6.25% of the lift at liftSec=2/lowerSec=8 and the last
  // 7.1% at 7/7. A STOP anywhere in that band caps to lowerMs and clears the
  // flag exactly like one at the very last instant — the flag does not stay set
  // for every press short of the absolute end of the lift.
  // [TH] มีกรณีหนึ่งที่ผลลัพธ์เป็นแค่การมองในแง่ดี ไม่ใช่ค่าที่แม่นจริง: การกด STOP ที่ปลายสุดของ
  //      สถานะยกขาจะคำนวณได้ระยะเวลาลงมากกว่า lowerMs จึงถูกจำกัดไว้ที่ lowerMs และล้างสัญญาณนี้
  //      เหมือนกับการลดขาแบบเต็มระยะจริง แต่การเปลี่ยนจากยกขาไปเป็นลดขาเพื่อหยุดคือการกลับทิศขณะกำลัง
  //      เคลื่อนที่ actuator.cpp จึงต้องพักคำขอลงใหม่ไว้หลัง DEADTIME_MS (50 มิลลิวินาที) ในขณะที่ตัวจับเวลา
  //      ของสถานะนี้เองซึ่งเริ่มนับตั้งแต่เข้าสถานะยังคงนับต่อไปตลอดช่วงที่รอนั้น การลดขาเพื่อหยุดแบบเต็มระยะ
  //      ที่เข้ามาจากสถานะยกขาจึงจ่ายไฟลงจริงแค่ lowerMs - DEADTIME_MS ทำให้ขาค้างสูงกว่าพื้นเล็กน้อย
  //      ประมาณ 0.6% ของระยะเต็มที่ liftSec=2/lowerSec=8 และประมาณ 0.7% ที่ 7/7 ซึ่งยังอยู่ในช่วงความคลาดเคลื่อน
  //      เชิงกลของเครื่องที่มีกลไกหยุดแข็งด้านล่างอยู่แล้ว คำว่า "อยู่ที่พื้น" ตรงนี้จึงแม่นยำในระดับประมาณหนึ่งเปอร์เซ็นต์
  //      ของระยะเต็ม ไม่ใช่ค่าที่แน่นอน
  //
  //      สัญญาณนี้ยังไม่ได้ถูกล้างจากสาขายกขาที่จุดเดียว แต่ล้างได้ตลอดช่วงหนึ่ง: เมื่อใดก็ตามที่
  //      floor(dt*lowerMs/liftMs) + LOWER_MARGIN_MS >= lowerMs ซึ่งคือ 6.25% สุดท้ายของช่วงยกขาที่
  //      liftSec=2/lowerSec=8 และ 7.1% สุดท้ายที่ 7/7 การกด STOP ที่ใดก็ได้ในช่วงนั้นจะถูกจำกัดที่ lowerMs
  //      และล้างสัญญาณเหมือนกับการกดที่จุดสุดท้ายจริงๆ สัญญาณนี้จึงไม่ได้ค้างเป็นจริงตลอดจนถึงจุดสุดท้ายของ
  //      การยกขาเสมอไปอย่างที่อาจคาดไว้
  bool legPositionUnknown = true;
};
