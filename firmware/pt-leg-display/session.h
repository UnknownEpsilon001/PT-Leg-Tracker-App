#pragma once
#include <stdint.h>

// เครื่องสถานะของหนึ่งรอบการทำงาน (session state machine)
// ลำดับ: ยกขึ้น -> ค้าง -> ลดลง -> พัก วนซ้ำ พร้อมเส้นทางหยุดปลอดภัยและสถานะผิดพลาด
// Session state machine: Lift -> Hold -> Lower -> Rest, with safe-stop and fault paths.

// [TH] สถานะของรอบ: ว่าง / ยกขึ้น / ค้าง / ลดลง / พัก / กำลังลงเพื่อหยุด / ผิดพลาด
enum class Phase { Idle, Lift, Hold, Lower, Rest, SafeLower, Fault };

struct CycleCfg {
  uint32_t maxTravelMs;  // LIFT/LOWER/SafeLower safety cap — เพดานเวลาความปลอดภัยต่อจังหวะ
  uint32_t holdMs;       // เวลาค้างตอนยกสุด
  uint32_t restMs;       // เวลาพักตอนลงสุด
};

class SessionSM {
 public:
  CycleCfg cfg{8000, 10000, 10000};
  void start(uint32_t nowMs);        // Idle/Fault -> Lift (start also clears a latched Fault)
                                     // [TH] เริ่มรอบ และล้างสถานะ Fault ที่ค้างอยู่ด้วย
  // Stop, and always by lowering the leg first where there is any chance it is up.
  // From Lift/Hold/Lower this ends the session via SafeLower; from Rest the leg is
  // already down so it stops immediately; from Fault it lowers WITHOUT reporting
  // anything (that session was reported when it faulted); from Idle it lowers only
  // if the leg is believed up, also without reporting. Only Idle-with-the-leg-down
  // is a no-op.
  // [TH] สั่งหยุด และให้ลดขาลงก่อนเสมอถ้ามีโอกาสที่ขาจะอยู่ข้างบน
  //      จากสถานะยกขา/ค้าง/ลดลง จะจบรอบผ่านสถานะลดขาเพื่อหยุด
  //      จากสถานะพัก ขาลงอยู่แล้วจึงหยุดได้ทันที
  //      จากสถานะผิดพลาด จะลดขาโดยไม่รายงานอะไร เพราะรอบนั้นถูกรายงานไปแล้วตอนที่ผิดพลาด
  //      จากสถานะว่าง จะลดขาเฉพาะเมื่อเชื่อว่าขาอยู่ข้างบน และไม่รายงานเช่นกัน
  //      มีแต่กรณีสถานะว่างที่ขาอยู่ล่างสุดแล้วเท่านั้นที่ไม่ทำอะไร
  void requestStop(uint32_t nowMs);
  void forceFault(uint32_t nowMs);   // external safety trip (actuator cap)
                                     // [TH] บังคับเข้าสถานะผิดพลาดจากภายนอก (เพดานเวลารีเลย์)
  // Clears the rep count, and from a Fault lowers the leg on the way to Idle
  // rather than dropping the latch where it stands. Deliberately does NOT clear
  // the pending edges or the finished session's totals — a RESET can land in the
  // same loop iteration as the fault that ended a session, and clearing them there
  // threw the session's only record away. No-op while running.
  // [TH] ล้างจำนวนครั้ง และถ้าอยู่ในสถานะผิดพลาดจะลดขาลงระหว่างทางกลับสู่สถานะว่าง
  //      แทนที่จะปล่อยสถานะทิ้งไว้ตรงนั้น
  //      จงใจไม่ล้างขอบสัญญาณที่ค้างอยู่และยอดรวมของรอบที่จบไปแล้ว
  //      เพราะการกด RESET อาจตกอยู่ในลูปรอบเดียวกับความผิดพลาดที่จบรอบนั้น
  //      การล้างตรงนั้นจะทิ้งข้อมูลชิ้นเดียวของรอบนั้นไป ไม่ทำอะไรถ้ากำลังทำงานอยู่
  void reset(uint32_t nowMs);
  void tick(uint32_t nowMs, bool topPressed, bool bottomPressed);
  bool running() const { return ph != Phase::Idle && ph != Phase::Fault; }
  bool faulted() const { return ph == Phase::Fault; }
  Phase phase() const { return ph; }
  uint32_t elapsedSec(uint32_t nowMs) const;
  uint16_t reps() const { return repCount; }
  bool takeStartedEdge();  // อ่าน+ล้างสัญญาณ "เพิ่งเริ่มรอบ" (edge)
  // Read+clear "a session just ended". Raised only by finish(), i.e. exactly once
  // per session that is reported — so NOT by a lowering started from Fault or from
  // Idle, neither of which is a session, and not by the quiet Fault re-latch when
  // such a lowering cannot confirm the bottom.
  // [TH] อ่านและล้างสัญญาณ "รอบเพิ่งจบ" ยกโดย finish() เท่านั้น
  //      คือครั้งเดียวต่อรอบที่ถูกรายงาน จึงไม่ยกให้การลดขาที่เริ่มจากสถานะผิดพลาดหรือสถานะว่าง
  //      เพราะทั้งสองอย่างนั้นไม่ใช่รอบการทำงาน
  //      และไม่ยกให้การกลับเข้าสถานะผิดพลาดแบบเงียบๆ ตอนที่การลดขาแบบนั้นยืนยันตำแหน่งล่างสุดไม่ได้
  bool takeStoppedEdge();
  uint32_t finalElapsedSec() const { return finalElapsed; }  // เวลารวมสุดท้ายของรอบที่จบ
  uint16_t finalReps() const { return finalRepCount; }       // จำนวนครั้งสุดท้ายของรอบที่จบ
  // Whether the session that just ended ended in a fault. Use this and not
  // faulted() when reporting a finished session: faulted() reads the live phase,
  // which a RESET in the same loop iteration can already have cleared.
  // [TH] รอบที่เพิ่งจบลงจบเพราะความผิดพลาดหรือไม่ ตอนรายงานรอบที่จบแล้วให้ใช้ค่านี้ ไม่ใช่ faulted()
  //      เพราะ faulted() อ่านจากสถานะปัจจุบัน ซึ่งการกด RESET ในลูปรอบเดียวกันอาจล้างไปแล้ว
  bool finalFaulted() const { return finalFault; }
  // One-shot: a new stroke has begun that may drive the same direction as the
  // last one, so the actuator's energize clock has to be restarted or the new
  // stroke silently inherits what the old one already spent. Read once per loop,
  // before the relays are commanded.
  // [TH] อ่านได้ครั้งเดียว: มีการเคลื่อนที่ครั้งใหม่เริ่มขึ้นซึ่งอาจสั่งไปทางเดิมกับครั้งก่อน
  //      ต้องเริ่มจับเวลาการจ่ายไฟใหม่ ไม่งั้นการเคลื่อนที่ครั้งใหม่จะรับช่วงเวลาที่ครั้งก่อนใช้ไปแล้วมาเงียบๆ
  //      อ่านหนึ่งครั้งต่อลูป ก่อนสั่งรีเลย์
  bool takeStrokeRenewEdge();
  // Tell the state machine where the leg is at power-up. Without this the board
  // boots believing the leg is down, because that is the common case — but a
  // power cut, brown-out or watchdog reset part-way through a lift leaves it in
  // the air, and a machine that believes otherwise refuses to lower it. Call once
  // in setup(), after the limit switches have had time to settle.
  // [TH] บอกเครื่องสถานะว่าขาอยู่ตำแหน่งใดตอนเปิดเครื่อง
  //      ถ้าไม่บอก บอร์ดจะเชื่อว่าขาอยู่ล่างสุดเพราะเป็นกรณีที่พบบ่อยที่สุด
  //      แต่ไฟดับ ไฟตก หรือการรีเซ็ตจากตัวเฝ้าระวังกลางจังหวะยกขา จะทิ้งขาไว้กลางอากาศ
  //      และเครื่องที่เชื่อเป็นอย่างอื่นจะไม่ยอมลดขาลงให้
  //      เรียกครั้งเดียวใน setup() หลังจากรอให้ลิมิตสวิตช์นิ่งแล้ว
  // The top switch gets a vote, because under SWITCH_NC a broken bottom line
  // reads PRESSED — so trusting the bottom alone would seed "leg is down" on
  // exactly the wiring fault that leaves it up. If the top switch says the leg is
  // at the top, it is up, whatever the bottom claims.
  // [TH] สวิตช์บนมีสิทธิ์ออกเสียงด้วย เพราะเมื่อใช้แบบ NC สายสวิตช์ล่างที่ขาดจะอ่านได้ว่า "กด"
  //      การเชื่อสวิตช์ล่างอย่างเดียวจึงตั้งค่าว่า "ขาอยู่ล่างสุด" ในกรณีสายผิดแบบที่ทิ้งขาไว้ข้างบนพอดี
  //      ถ้าสวิตช์บนบอกว่าขาอยู่บนสุด ก็แปลว่าขาอยู่ข้างบน ไม่ว่าสวิตช์ล่างจะบอกอะไร
  void seedLegPosition(bool bottomPressed, bool topPressed) {
    liftedSinceBottom = !bottomPressed || topPressed;
  }

 private:
  void enter(Phase p, uint32_t nowMs);
  // Shared arrival test for Lower and SafeLower — see the definition for what it
  // checks and why each clause is there. Samples the switches once per stroke, so
  // it must be called on every tick of a descending phase and nowhere else.
  // [TH] การตรวจการมาถึงที่ใช้ร่วมกันของสถานะลดลงและลดลงเพื่อหยุด
  //      ดูรายละเอียดว่าตรวจอะไรและทำไมแต่ละข้อถึงมีอยู่ได้ที่ตัวฟังก์ชัน
  //      อ่านค่าสวิตช์ครั้งเดียวต่อการเคลื่อนที่หนึ่งครั้ง จึงต้องเรียกทุกจังหวะของสถานะที่สั่งลง และห้ามเรียกที่อื่น
  bool downConfirmed(bool topPressed, bool bottomPressed);
  void finish(uint32_t nowMs, Phase to, bool fault);  // record final elapsed/reps/fault, raise
                                                      // stoppedEdge, go to `to`
                                                      // [TH] บันทึกเวลา/จำนวนครั้ง/ความผิดพลาดสุดท้าย
                                                      //      ตั้งสัญญาณหยุด แล้วไปสถานะ to
  Phase ph = Phase::Idle;
  uint32_t phaseStart = 0, sessionStart = 0;
  uint16_t repCount = 0;
  uint32_t finalElapsed = 0;
  uint16_t finalRepCount = 0;
  bool finalFault = false;
  // This SafeLower is bringing the leg down rather than ending a session — set
  // from Fault (that session was reported when it faulted) and from Idle (there is
  // no session at all). Either way it must not raise a stopped edge, or the same
  // session lands on the card twice. Invariant: recovering implies SafeLower.
  // [TH] สถานะลดขาเพื่อหยุดครั้งนี้เป็นการเอาขาลง ไม่ใช่การจบรอบ
  //      ตั้งจากสถานะผิดพลาด ซึ่งรอบนั้นถูกรายงานไปแล้วตอนที่ผิดพลาด
  //      และตั้งจากสถานะว่าง ซึ่งไม่มีรอบอยู่เลย
  //      ทั้งสองกรณีต้องไม่ตั้งสัญญาณหยุด ไม่งั้นรอบเดียวกันจะลงการ์ดสองครั้ง
  //      ข้อกำหนด: ถ้า recovering เป็นจริง สถานะต้องเป็นลดขาเพื่อหยุดเสมอ
  bool recovering = false;
  // A descending stroke has just been entered and has not yet sampled the
  // switches. Armed in enter() for both Lower and SafeLower, consumed by
  // downConfirmed() on the stroke's first tick, which is what makes the two
  // sample fields below belong to THIS stroke and not the previous one.
  // [TH] เพิ่งเข้าสู่การเคลื่อนที่ลงและยังไม่ได้อ่านค่าสวิตช์
  //      ตั้งค่าใน enter() ให้ทั้งสถานะลดลงและลดลงเพื่อหยุด
  //      และถูกอ่านโดย downConfirmed() ในจังหวะแรกของการเคลื่อนที่
  //      ซึ่งเป็นสิ่งที่ทำให้ค่าที่อ่านไว้สองตัวด้านล่างเป็นของการเคลื่อนที่ครั้งนี้ ไม่ใช่ครั้งก่อน
  bool strokeFresh = false;
  bool bottomWasClear = false;
  bool alreadyDown = false;      // the leg was at the bottom when the stroke began
                                 // [TH] ขาอยู่ที่ตำแหน่งล่างสุดแล้วตั้งแต่การเคลื่อนที่เริ่มต้น
  // The actuator has driven up since the bottom was last actually confirmed, so
  // a bottom switch reading pressed right now cannot be believed as "still down".
  // This is what separates a leg that never left the bottom from an open bottom
  // line that reads pressed all the way up.
  // [TH] ชุดขับเคลื่อนสั่งยกขึ้นไปแล้วนับตั้งแต่ครั้งสุดท้ายที่ยืนยันตำแหน่งล่างสุดได้จริง
  //      ค่าที่สวิตช์ล่างอ่านได้ว่ากดอยู่ตอนนี้จึงเชื่อไม่ได้ว่าแปลว่า "ยังอยู่ล่างสุด"
  //      นี่คือสิ่งที่แยกขาที่ไม่เคยออกจากตำแหน่งล่างสุด ออกจากสายสวิตช์ล่างที่ขาดและอ่านว่ากดตลอดทาง
  bool liftedSinceBottom = false;
  bool recoveryFailed = false;   // a recovery ran without confirming the bottom
                                 // [TH] การกู้สถานการณ์ทำงานจบโดยยืนยันไม่ได้ว่าถึงพื้น
  bool strokeRenewEdge = false;  // consumed by the sketch, see takeStrokeRenewEdge
                                 // [TH] สเก็ตช์เป็นผู้อ่านค่านี้ ดู takeStrokeRenewEdge
  bool startedEdge = false, stoppedEdge = false;
};
