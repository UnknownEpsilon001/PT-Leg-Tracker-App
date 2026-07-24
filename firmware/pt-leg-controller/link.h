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

class Link {
 public:
  void begin(Stream* io) { _io = io; }
  void poll();
  void sendSettings(const LinkSettings& s);
  void sendHello();
  void sendState(const LinkState& st);
  void sendPing();
  uint32_t lastRxMs() const { return _lastRx; }

  // [TH] คอลแบ็กแยกตามฝั่ง: บางตัวใช้ฝั่งคอนโทรลเลอร์ บางตัวใช้ฝั่งจอ
  void (*onSettings)(const LinkSettings&) = nullptr;   // controller side — คอนโทรลเลอร์รับค่าตั้งค่าเพื่อบันทึก
  void (*onHello)() = nullptr;                         // controller side — จอทักทาย ขอค่าปัจจุบัน
  void (*onState)(const LinkState&) = nullptr;         // display side — จอรับสถานะมาแสดง
  void (*onSettingsReply)(const LinkSettings&) = nullptr; // display side — จอรับค่าตั้งค่าที่คอนโทรลเลอร์ตอบกลับ

 private:
  void dispatch(const String& line);
  Stream* _io = nullptr;
  String _buf;
  uint32_t _lastRx = 0;
};
