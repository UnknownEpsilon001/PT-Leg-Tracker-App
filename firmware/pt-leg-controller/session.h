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

 private:
  void enter(Phase p, uint32_t nowMs);
  void finish(uint32_t nowMs, Phase to);  // record final elapsed/reps, raise stoppedEdge, go to `to`
                                          // [TH] บันทึกเวลา/จำนวนครั้งสุดท้าย ตั้งสัญญาณหยุด แล้วไปสถานะ to
  Phase ph = Phase::Idle;
  uint32_t phaseStart = 0, sessionStart = 0;
  uint16_t repCount = 0;
  uint32_t finalElapsed = 0;
  uint16_t finalRepCount = 0;
  bool startedEdge = false, stoppedEdge = false;
};
