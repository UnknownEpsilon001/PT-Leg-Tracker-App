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
  bool wifi;        // สถานะเชื่อมต่อ WiFi
  bool server;      // สถานะเชื่อมต่อเซิร์ฟเวอร์
};

struct LinkSettings {  // ค่าตั้งค่าที่รับส่งกันสองทางระหว่างจอกับคอนโทรลเลอร์
  String ssid, pass, server, code;
  uint16_t maxTravel, hold, rest;
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
  uint32_t lastRxMs() const { return _lastRx; }

  // [TH] คอลแบ็กแยกตามฝั่ง: บางตัวใช้ฝั่งคอนโทรลเลอร์ บางตัวใช้ฝั่งจอ
  void (*onSettings)(const LinkSettings&) = nullptr;   // controller side — คอนโทรลเลอร์รับค่าตั้งค่าเพื่อบันทึก
  void (*onHello)() = nullptr;                         // controller side — จอทักทาย ขอค่าปัจจุบัน
  void (*onState)(const LinkState&) = nullptr;         // display side — จอรับสถานะมาแสดง
  void (*onSettingsReply)(const LinkSettings&) = nullptr; // display side — จอรับค่าตั้งค่าที่คอนโทรลเลอร์ตอบกลับ
  void (*onDone)(const LinkDone&) = nullptr;           // display side — จอรับผลสรุปตอนจบรอบไปบันทึก

 private:
  void dispatch(const String& line);
  Stream* _io = nullptr;
  String _buf;
  uint32_t _lastRx = 0;
};
