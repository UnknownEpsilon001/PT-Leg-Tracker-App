#pragma once
#include <stdint.h>

// เครื่องสถานะของหนึ่งรอบการทำงาน (session state machine)
// ลำดับ: ยกขึ้น -> ค้าง -> ลดลง -> พัก วนซ้ำ ทุกสถานะจบด้วยเวลา
// Session state machine: Lift -> Hold -> Lower -> Rest, every phase ended by a
// clock. There are no limit switches: travel length IS liftMs / lowerMs.

// [TH] สถานะของรอบ: ว่าง / ยกขึ้น / ค้าง / ลดลง / พัก / กำลังลงเพื่อหยุด / ผิดพลาด
enum class Phase { Idle, Lift, Hold, Lower, Rest, SafeLower, Fault };

struct CycleCfg {
  uint32_t liftMs;       // เวลาที่รีเลย์ยกขึ้นจ่ายไฟต่อหนึ่งจังหวะ
  uint32_t lowerMs;      // เวลาที่รีเลย์ลดลงจ่ายไฟต่อหนึ่งจังหวะ
  uint32_t holdMs;       // เวลาค้างตอนยกสุด
  uint32_t restMs;       // เวลาพักตอนลงสุด
  uint32_t maxTravelMs;  // ceiling liftMs/lowerMs are clamped to — เพดานที่ใช้จำกัดสองค่าแรก
  uint16_t targetReps;   // stop after this many reps; 0 = until STOP — 0 = ไม่จำกัด
};

class SessionSM {
 public:
  CycleCfg cfg{4000, 4000, 10000, 10000, 8000, 10};

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
  // loop, before the relays are commanded.
  // [TH] อ่านได้ครั้งเดียว: มีการเคลื่อนที่ครั้งใหม่ที่อาจสั่งไปทางเดิม ต้องเริ่มจับเวลาการจ่ายไฟใหม่
  //      อ่านหนึ่งครั้งต่อลูป ก่อนสั่งรีเลย์
  bool takeStrokeRenewEdge();

 private:
  void enter(Phase p, uint32_t nowMs);
  void finish(uint32_t nowMs, Phase to, bool fault);
  Phase ph = Phase::Idle;
  uint32_t phaseStart = 0, sessionStart = 0;
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
  // True until a Lower stroke has run to completion at least once. Set at
  // construction (and stays true across a reboot, since this is not
  // persisted) so the first start of any boot is treated like a start out of
  // Fault: lower first, because a power cut mid-Lift boots to Idle, not
  // Fault, and leaves the real position unknown. Cleared unconditionally the
  // moment a Lower or SafeLower stroke reaches its full duration — see
  // tick() — which is the one signal, timed travel or not, that the leg is
  // actually at the bottom. Never set back to true except by this class's own
  // initializer, so it cannot re-arm itself into a lower-forever loop.
  // [TH] เป็นจริงจนกว่าการเคลื่อนที่ลงจะทำงานจบครบอย่างน้อยหนึ่งครั้ง ตั้งไว้ตั้งแต่สร้างอ็อบเจกต์
  //      (และเป็นจริงข้ามการเปิดเครื่องใหม่เสมอ เพราะค่านี้ไม่ถูกบันทึกลง NVS)
  //      เพื่อให้การเริ่มครั้งแรกของการเปิดเครื่องทุกครั้งถูกปฏิบัติเหมือนการเริ่มจากสถานะผิดพลาด
  //      คือลดขาก่อนเสมอ เพราะไฟตกกลางจังหวะยกขาจะรีเซ็ตเครื่องไปสถานะว่าง ไม่ใช่สถานะผิดพลาด
  //      และทิ้งตำแหน่งจริงไว้โดยไม่มีใครรู้ ถูกล้างโดยไม่มีเงื่อนไขทันทีที่การเคลื่อนที่ลง
  //      (ไม่ว่าจะลงปกติหรือลดขาเพื่อหยุด) ทำงานครบเต็มเวลา ดู tick() ซึ่งเป็นสัญญาณเดียวที่บอกว่า
  //      ขาอยู่ล่างสุดจริง ไม่ว่าจะเป็นการเดินทางแบบจับเวลาหรือไม่ก็ตาม
  //      ไม่มีที่ใดตั้งค่านี้กลับเป็นจริงอีกนอกจากค่าเริ่มต้นของคลาสนี้เอง จึงไม่มีทางย้อนตัวเองกลับเข้า
  //      วงวนลดขาไม่รู้จบ
  bool legPositionUnknown = true;
};
