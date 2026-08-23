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
// [TH] สถานะของรอบ: ว่าง / ยกขึ้น / ค้าง / ลดลง / พัก / กำลังลงเพื่อหยุด / ผิดพลาด
enum class Phase { Idle, Lift, Hold, Lower, Rest, SafeLower, Fault };

struct CycleCfg {
  uint32_t liftMs;       // เวลาที่ขับมอเตอร์ยกขึ้นต่อหนึ่งจังหวะ
  uint32_t lowerMs;      // เวลาที่ขับมอเตอร์ลดลงต่อหนึ่งจังหวะ
  uint32_t holdMs;       // เวลาค้างตอนยกสุด
  uint32_t restMs;       // เวลาพักตอนลงสุด
  uint32_t maxTravelMs;  // ceiling liftMs/lowerMs are clamped to — เพดานที่ใช้จำกัดสองค่าแรก
  uint16_t targetReps;   // stop after this many reps; 0 = until STOP — 0 = ไม่จำกัด
};

class SessionSM {
 public:
  CycleCfg cfg{7000, 7000, 10000, 10000, 8000, 10};

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
                                     // [TH] บังคับเข้าสถานะผิดพลาดจากภายนอก (เพดานเวลาจ่ายไฟ)

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

  // Change the target mid-session as well as at rest, because both UIs can edit
  // it while the machine runs. Lowering it below the reps already done ends the
  // session at the end of the current Rest, which is where the check lives.
  // [TH] เปลี่ยนเป้าหมายได้ทั้งตอนว่างและตอนกำลังทำงาน เพราะทั้งสองหน้าจอแก้ได้ระหว่างเครื่องเดิน
  //      ถ้าตั้งต่ำกว่าจำนวนครั้งที่ทำไปแล้ว รอบจะจบเมื่อสิ้นสุดสถานะพักครั้งปัจจุบัน ซึ่งเป็นจุดที่ตรวจค่านี้
  void setTarget(uint16_t reps) { cfg.targetReps = reps; }
  uint16_t target() const { return cfg.targetReps; }

  void tick(uint32_t nowMs);
  bool running() const { return ph != Phase::Idle && ph != Phase::Fault; }
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
  // loop, before the motor is commanded.
  // [TH] อ่านได้ครั้งเดียว: มีการเคลื่อนที่ครั้งใหม่ที่อาจสั่งไปทางเดิม ต้องเริ่มจับเวลาการจ่ายไฟใหม่
  //      อ่านหนึ่งครั้งต่อลูป ก่อนสั่งมอเตอร์
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
  // not clear this flag. Never set back to true except by this class's own
  // initializer, so it cannot re-arm itself into a lower-forever loop.
  // [TH] เป็นจริงจนกว่าการลดขาแบบเต็มระยะจะทำงานจบครบอย่างน้อยหนึ่งครั้ง ตั้งไว้ตั้งแต่สร้างอ็อบเจกต์
  //      (และเป็นจริงข้ามการเปิดเครื่องใหม่เสมอ เพราะค่านี้ไม่ถูกบันทึกลง NVS)
  //      เพื่อให้การเริ่มครั้งแรกของการเปิดเครื่องทุกครั้งถูกปฏิบัติเหมือนการเริ่มจากสถานะผิดพลาด
  //      คือลดขาก่อนเสมอ เพราะไฟตกกลางจังหวะยกขาจะรีเซ็ตเครื่องไปสถานะว่าง ไม่ใช่สถานะผิดพลาด
  //      และทิ้งตำแหน่งจริงไว้โดยไม่มีใครรู้ ถูกล้างใน tick() เมื่อการลดลงปกติทำงานจบครบ cfg.lowerMs
  //      หรือเมื่อการลดขาเพื่อหยุดที่มี strokeCapMs ของตัวเองเท่ากับ cfg.lowerMs พอดี
  //      (คือไม่ใช่แบบย่นระยะที่ enter() คำนวณจากการหยุดกลางจังหวะยกหรือกลางจังหวะลง)
  //      การลดขาเพื่อหยุดแบบย่นระยะทำงานสั้นเท่าที่การอนุมานทางคณิตศาสตร์จากเวลาที่ขับเคลื่อนไปบอกว่าต้องใช้เท่านั้น
  //      เครื่องนี้ไม่มีสัญญาณตำแหน่งใดๆ เลย ตัวจับเวลาของมันหมดเวลาจึงไม่ใช่สัญญาณเดียวกับที่บอกว่า
  //      ขาอยู่ล่างสุดจริง และต้องไม่ล้างสัญญาณนี้ ไม่มีที่ใดตั้งค่านี้กลับเป็นจริงอีกนอกจากค่าเริ่มต้นของคลาสนี้เอง
  //      จึงไม่มีทางย้อนตัวเองกลับเข้าวงวนลดขาไม่รู้จบ
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
