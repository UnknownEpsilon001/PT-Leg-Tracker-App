#pragma once
#include <Arduino.h>

// บันทึกผลของแต่ละรอบการทำงานลงไฟล์ /sessions.csv บนการ์ด SD
// ถ้าเขียนไม่ได้ (ไม่มีการ์ด/การ์ดเสีย) จะพักไว้ในหน่วยความจำก่อนแล้วค่อยลองใหม่
// Session log on the CYD's card (/sessions.csv). Rows that cannot be written are
// parked in a small RAM ring and retried, so pulling the card mid-therapy costs
// at most the oldest few sessions instead of every session until someone notices.

struct SessionRow {
  uint32_t epoch;        // UTC seconds, 0 = clock unknown — 0 คือไม่ทราบเวลา
  uint16_t reps;
  uint32_t durationSec;
  bool fault;
  String patientCode;    // "-" if none — ใส่ "-" เมื่อไม่ได้เลือกผู้ป่วย
  String patientName;
};

void sdlogEnsureHeader();           // write /sessions.csv header if the file is new
bool sdlogWrite(const SessionRow&); // append one row; false on failure (caller may ring)
void sdlogRingPush(const SessionRow&);
void sdlogFlushRing();              // retry buffered rows to SD — ลองเขียนแถวที่ค้างอีกครั้ง
int sdlogRingDepth();
