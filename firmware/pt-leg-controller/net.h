#pragma once
#include <Arduino.h>
#include "settings.h"

// การสื่อสารกับเซิร์ฟเวอร์ผ่าน WiFi/HTTP: ดึงคำสั่ง ส่งเหตุการณ์ และส่งสัญญาณชีพ
// Server networking over WiFi/HTTP: poll commands, push events, send heartbeats.

void netBegin(const DeviceSettings& s);      // starts Wi-Fi (non-blocking) — เริ่ม WiFi แบบไม่บล็อก
void netLoop(uint32_t nowMs);                // reconnect + retry pending event — เชื่อมต่อใหม่ + ส่งเหตุการณ์ค้างซ้ำ
bool netWifiUp();                            // WiFi เชื่อมต่ออยู่หรือไม่
bool netServerUp();                          // last HTTP call succeeded — คำขอ HTTP ล่าสุดสำเร็จหรือไม่
// Returns "start" | "stop" | "". `targetOut`, when given, receives the target the
// server wants this device to use, or -1 when the server named none. The two ride
// the same poll because the device already makes it once a second; a second
// endpoint would double the traffic to carry one integer.
// [TH] คืนค่า "start" | "stop" | "" ถ้าส่ง targetOut มาด้วยจะได้เป้าหมายจำนวนครั้งที่เซิร์ฟเวอร์ต้องการ
//      หรือ -1 ถ้าเซิร์ฟเวอร์ไม่ได้ระบุ ทั้งสองอย่างมาด้วยกันเพราะอุปกรณ์ถามอยู่แล้ววินาทีละครั้ง
//      ถ้าแยกเอนด์พอยต์จะเพิ่มปริมาณการสื่อสารเป็นสองเท่าเพื่อขนเลขตัวเดียว
// Cycle timings the server wants applied, or all-zero when it named none. Same
// ride-along reasoning as the target. Any field the server omitted arrives 0 and
// means "leave this one alone" — 0 is outside every clamp, so it can never be a
// real value.
// [TH] ค่าเวลาของรอบที่เซิร์ฟเวอร์ต้องการให้ใช้ หรือเป็นศูนย์ทั้งหมดถ้าไม่ได้ระบุ
//      เหตุผลเดียวกับเป้าหมายจำนวนครั้ง คือขนมาพร้อมกัน
//      ฟิลด์ใดที่เซิร์ฟเวอร์ไม่ได้ส่งมาจะได้ค่า 0 ซึ่งแปลว่าไม่ต้องแตะค่านั้น
//      เพราะ 0 อยู่นอกช่วงที่ยอมรับทุกค่า จึงเป็นค่าจริงไม่ได้
struct NetTimings {
  uint16_t liftSec, lowerSec, holdSec, restSec, maxTravelSec;
};
String netPollCommand(int16_t* targetOut = nullptr, NetTimings* timingsOut = nullptr);
                                             // "start" | "stop" | "" ; call at 1 s cadence
                                             // [TH] ดึงคำสั่งจากเซิร์ฟเวอร์ เรียกทุก 1 วินาที
void netQueueEvent(const char* type, uint32_t elapsedSec, uint16_t reps); // one-slot buffer, retried until 2xx
                                             // [TH] ใส่เหตุการณ์ลงคิว ส่งซ้ำจนกว่าจะได้ 2xx
void netHeartbeat(const char* state, uint32_t elapsedSec, uint16_t reps, uint16_t target); // fire-and-forget — ส่งแล้วไม่รอผล
