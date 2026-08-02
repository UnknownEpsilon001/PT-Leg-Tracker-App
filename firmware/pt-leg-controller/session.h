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
  void requestStop(uint32_t nowMs);  // clean stop -> SafeLower (or straight off from Rest)
                                     // [TH] สั่งหยุดแบบปลอดภัย: ลดขาลงก่อนแล้วค่อยหยุด
  void forceFault(uint32_t nowMs);   // external safety trip (actuator cap)
                                     // [TH] บังคับเข้าสถานะผิดพลาดจากภายนอก (เพดานเวลารีเลย์)
  void reset(uint32_t nowMs);        // idle-only: zero reps/elapsed, clear FAULT -> Idle; no-op while running
                                     // [TH] เฉพาะตอนว่าง: ล้างจำนวนครั้ง/เวลา และล้าง FAULT กลับสู่ Idle ; ไม่ทำอะไรถ้ากำลังทำงาน
  void tick(uint32_t nowMs, bool topPressed, bool bottomPressed);
  bool running() const { return ph != Phase::Idle && ph != Phase::Fault; }
  bool faulted() const { return ph == Phase::Fault; }
  Phase phase() const { return ph; }
  uint32_t elapsedSec(uint32_t nowMs) const;
  uint16_t reps() const { return repCount; }
  bool takeStartedEdge();  // อ่าน+ล้างสัญญาณ "เพิ่งเริ่มรอบ" (edge)
  bool takeStoppedEdge();  // fires after safe-lower AND on entering FAULT
                           // [TH] อ่าน+ล้างสัญญาณ "เพิ่งหยุด" เกิดหลังลงเสร็จ และตอนเข้าสถานะ FAULT
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
  // A SafeLower that is bringing the leg down after a fault, rather than ending a
  // session. Its session was already reported when the fault happened, so this one
  // finishes without raising a stopped edge. See requestStop's Fault case.
  // [TH] สถานะกำลังลดขาลงหลังเกิดความผิดพลาด ไม่ใช่การจบรอบตามปกติ
  //      รอบนั้นถูกรายงานไปแล้วตอนเกิดเหตุ การลดขาครั้งนี้จึงจบโดยไม่ตั้งสัญญาณหยุด
  //      ดูกรณีสถานะผิดพลาดใน requestStop
  bool recovering = false;
  // A safe-lower has just been entered and has not yet sampled the switches, and
  // whether the bottom switch was clear when it did. A bottom line that reads
  // pressed from the start is lying — the leg cannot already be down and still
  // need lowering — so its "we have arrived" must not be believed.
  // [TH] เพิ่งเข้าสถานะลดขาลงและยังไม่ได้อ่านสวิตช์ กับค่าที่ว่าสวิตช์ล่างว่างหรือไม่ตอนที่อ่าน
  //      สายสวิตช์ล่างที่อ่านได้ว่ากดตั้งแต่ต้นคือสายที่ให้ค่าผิด
  //      ขาจะลงไปสุดอยู่แล้วและยังต้องลดลงอีกไม่ได้ จึงต้องไม่เชื่อคำว่า "ถึงแล้ว" ของมัน
  bool safeLowerFresh = false;
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
