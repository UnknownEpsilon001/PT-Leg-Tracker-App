#pragma once
#include <Arduino.h>
#include "settings.h"

// การสื่อสารกับเซิร์ฟเวอร์ผ่าน WiFi/HTTP: ดึงคำสั่ง ส่งเหตุการณ์ และส่งสัญญาณชีพ
// Server networking over WiFi/HTTP: poll commands, push events, send heartbeats.

void netBegin(const DeviceSettings& s);      // starts Wi-Fi (non-blocking) — เริ่ม WiFi แบบไม่บล็อก
void netLoop(uint32_t nowMs);                // reconnect + retry pending event — เชื่อมต่อใหม่ + ส่งเหตุการณ์ค้างซ้ำ
bool netWifiUp();                            // WiFi เชื่อมต่ออยู่หรือไม่
bool netServerUp();                          // last HTTP call succeeded — คำขอ HTTP ล่าสุดสำเร็จหรือไม่
String netPollCommand();                     // "start" | "stop" | "" ; call at 1 s cadence
                                             // [TH] ดึงคำสั่งจากเซิร์ฟเวอร์ เรียกทุก 1 วินาที
void netQueueEvent(const char* type, uint32_t elapsedSec, uint16_t reps); // one-slot buffer, retried until 2xx
                                             // [TH] ใส่เหตุการณ์ลงคิว ส่งซ้ำจนกว่าจะได้ 2xx
void netHeartbeat(const char* state, uint32_t elapsedSec, uint16_t reps); // fire-and-forget — ส่งแล้วไม่รอผล
