#pragma once
#include <Arduino.h>

// โปรโตคอลลิงก์ระหว่างบอร์ดคอนโทรลเลอร์กับบอร์ดจอ (CYD) ผ่าน UART
// รับส่งข้อความ JSON คั่นด้วยขึ้นบรรทัดใหม่ (ดู firmware/link-protocol.md)
// UART link protocol between the controller and display boards
// (newline-delimited JSON; see firmware/link-protocol.md).

struct LinkState {  // สถานะที่คอนโทรลเลอร์ส่งให้จอแสดงผล
  bool running;     // กำลังทำงานอยู่หรือไม่
  int phase;        // หมายเลขสถานะ (ตรงกับ enum Phase)
  uint32_t elapsed; // เวลาที่ผ่านไป (วินาที)
  uint16_t reps;    // จำนวนครั้ง
  uint16_t target;  // 0 = ไม่จำกัด — the display shows reps/target when non-zero
  bool wifi;        // สถานะเชื่อมต่อ WiFi
  bool server;      // สถานะเชื่อมต่อเซิร์ฟเวอร์
};

// LinkSettings.target's wire sentinel for "this settings message did not carry
// a target field" — a display still on an image from before the field existed.
// lift/lower/hold/rest reuse a plain 0 for the same "absent" meaning because 0
// is not a legal value for any of them, but 0 IS a legal target (run until
// STOP), so it cannot double as the sentinel here without losing that value.
// 0xFFFF is outside the field's real 0..99 range and cannot collide with it.
// See link-protocol.md.
// [TH] ค่าบอกสถานะ "ไม่มีอยู่" บนสายสำหรับ LinkSettings.target — ใช้เมื่อจอยังใช้เฟิร์มแวร์รุ่นก่อน
//      ที่ยังไม่มีฟิลด์นี้ ฟิลด์ lift/lower/hold/rest ใช้ 0 ธรรมดาแทนความหมาย "ไม่มีอยู่" ได้
//      เพราะ 0 ไม่ใช่ค่าที่ใช้ได้จริงของฟิลด์เหล่านั้น แต่ target ค่า 0 ถือเป็นค่าที่ถูกต้อง
//      (หมายถึง "ทำไปเรื่อยๆ จนกว่าจะกด STOP") จึงใช้ 0 แทนความหมาย "ไม่มีอยู่" ไม่ได้ เพราะจะเสียค่านั้นไป
//      0xFFFF อยู่นอกช่วงค่าจริง 0..99 ของฟิลด์นี้ จึงไม่มีทางชนกับค่าจริงได้ ดู link-protocol.md
static constexpr uint16_t LINK_TARGET_ABSENT = 0xFFFF;

struct LinkSettings {  // ค่าตั้งค่าที่รับส่งกันสองทางระหว่างจอกับคอนโทรลเลอร์
  String ssid, pass, server, code;
  uint16_t maxTravel, lift, lower, hold, rest, target;
};

// ส่งครั้งเดียวตอนจบรอบการทำงาน (หยุดปกติหรือขึ้น FAULT) เพื่อให้จอบันทึกลง SD
// One shot at the end of a session (clean stop or FAULT). The state message
// cannot carry this: it is a periodic snapshot, so a session that ends between
// two snapshots would be logged from stale numbers, or twice.
struct LinkDone {
  uint32_t elapsed;   // finalElapsedSec — เวลารวมของรอบที่จบไป (วินาที)
  uint16_t reps;      // finalReps — จำนวนครั้งที่ทำได้
  uint32_t epoch;     // controller RTC UTC seconds, 0 if unset — เวลาจริงจากนาฬิกา 0 = ไม่ทราบ
  bool fault;         // true if the session ended in FAULT — จบเพราะขึ้นสถานะ FAULT
};

class Link {
 public:
  void begin(Stream* io) { _io = io; }
  void poll();
  void sendSettings(const LinkSettings& s);
  void sendHello();
  void sendState(const LinkState& st);
  void sendDone(const LinkDone& d);
  void sendPing();
  void sendTarget(uint16_t reps);
  uint32_t lastRxMs() const { return _lastRx; }

  // [TH] คอลแบ็กแยกตามฝั่ง: บางตัวใช้ฝั่งคอนโทรลเลอร์ บางตัวใช้ฝั่งจอ
  void (*onSettings)(const LinkSettings&) = nullptr;   // controller side — คอนโทรลเลอร์รับค่าตั้งค่าเพื่อบันทึก
  void (*onHello)() = nullptr;                         // controller side — จอทักทาย ขอค่าปัจจุบัน
  void (*onState)(const LinkState&) = nullptr;         // display side — จอรับสถานะมาแสดง
  void (*onSettingsReply)(const LinkSettings&) = nullptr; // display side — จอรับค่าตั้งค่าที่คอนโทรลเลอร์ตอบกลับ
  void (*onDone)(const LinkDone&) = nullptr;           // display side — จอรับผลสรุปตอนจบรอบไปบันทึก
  // display -> controller, applied live. Kept off the `settings` path on purpose:
  // that one commits NVS and restarts Wi-Fi, so it is deferred until a session
  // ends, and a rep target the operator is turning right now cannot wait for that.
  // [TH] จอส่งให้คอนโทรลเลอร์ นำไปใช้ทันที จงใจไม่ใช้เส้นทาง settings
  //      เพราะเส้นทางนั้นบันทึก NVS และรีสตาร์ต WiFi จึงถูกเลื่อนไปจนกว่ารอบจะจบ
  //      แต่เป้าหมายจำนวนครั้งที่ผู้ใช้กำลังหมุนอยู่รอแบบนั้นไม่ได้
  void (*onTarget)(uint16_t) = nullptr;  // controller side

 private:
  void dispatch(const String& line);
  Stream* _io = nullptr;
  String _buf;
  uint32_t _lastRx = 0;
};
