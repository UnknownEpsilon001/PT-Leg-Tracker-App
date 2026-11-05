#pragma once
#include <Arduino.h>

// บันทึกผลของแต่ละรอบการทำงานลงไฟล์ /sessions.csv บนการ์ด SD
// ถ้าเขียนไม่ได้ (ไม่มีการ์ด/การ์ดเสีย) จะพักไว้ในหน่วยความจำก่อนแล้วค่อยลองใหม่
// Session log on the CYD's card (/sessions.csv). Rows that cannot be written are
// parked in a small RAM ring and retried, so pulling the card mid-therapy costs
// at most the oldest few sessions instead of every session until someone notices.

// The wire carries UTC end to end; the offset is added only where a human reads
// the result. Thailand has no DST, so a fixed offset is exact.
// [TH] เวลาที่ส่งกันเป็น UTC ทั้งหมด บวกเวลาไทยเฉพาะตอนแสดงให้คนอ่าน ประเทศไทยไม่มีการปรับเวลาตามฤดูกาล ค่าคงที่จึงถูกต้องเสมอ
#define TZ_OFFSET_SEC (7 * 3600)

struct SessionRow {
  uint32_t epoch;        // UTC seconds, 0 = clock unknown — 0 คือไม่ทราบเวลา
  uint16_t reps;
  uint32_t durationSec;
  bool fault;
  String patientCode;    // "-" if none — ใส่ "-" เมื่อไม่ได้เลือกผู้ป่วย
  String patientName;
  String uid;            // the controller's session number — หมายเลขรอบจากคอนโทรลเลอร์
};

void sdlogEnsureHeader();           // write /sessions.csv header if the file is new
bool sdlogWrite(const SessionRow&); // append one row; false on failure (caller may ring)
void sdlogRingPush(const SessionRow&);
void sdlogFlushRing();              // retry buffered rows to SD — ลองเขียนแถวที่ค้างอีกครั้ง
int sdlogRingDepth();

// One row of /sessions.csv as the history screen shows it. `when` is the local
// time as written on the card ("?" when the clock was unknown); `who` is the
// patient name, or the code when no name was recorded.
// [TH] หนึ่งแถวของ /sessions.csv ตามที่หน้าประวัติแสดง when คือเวลาท้องถิ่นตามที่เขียนบนการ์ด
//      ("?" ถ้าตอนนั้นไม่ทราบเวลา) ส่วน who คือชื่อคนไข้ หรือรหัสถ้าไม่ได้บันทึกชื่อ
struct HistRow {
  String when;
  String who;
  uint32_t durationSec;
  uint16_t reps;
  bool fault;
};

// Fills out[] with up to max of the newest rows, newest first, and returns how
// many. Reads only the tail of the file, so a card with years of sessions costs
// the same as one with a week. -1 when the file cannot be opened.
// [TH] ใส่แถวล่าสุดไม่เกิน max แถวลงใน out[] เรียงจากใหม่ไปเก่า แล้วคืนจำนวนแถว
//      อ่านเฉพาะท้ายไฟล์ การ์ดที่มีข้อมูลหลายปีจึงใช้เวลาเท่ากับการ์ดที่มีข้อมูลหนึ่งสัปดาห์ คืน -1 ถ้าเปิดไฟล์ไม่ได้
int sdlogReadRecent(HistRow* out, int max);

// The catch-up sync to the server. /synced.txt on the card holds how far into
// sessions.csv the server has confirmed; the file is append-only, so a byte
// offset is all the state there is.
// [TH] การซิงก์ย้อนหลังไปเซิร์ฟเวอร์ ไฟล์ /synced.txt บนการ์ดเก็บว่าเซิร์ฟเวอร์ยืนยันรับไปถึงไหนแล้วใน sessions.csv
//      ไฟล์นั้นเขียนต่อท้ายอย่างเดียว ตำแหน่งไบต์ตำแหน่งเดียวจึงเป็นสถานะทั้งหมดที่ต้องมี
struct SyncRow {
  uint32_t endOffset;    // byte offset just past this row — ตำแหน่งไบต์ถัดจากแถวนี้
  String uid;
  String when;           // as written on the card — ตามที่เขียนบนการ์ด
  String code;
  uint32_t durationSec;
  uint16_t reps;
  bool fault;
};
// The first row the server has not confirmed; false when there is none (or no
// card). A row from before session numbers existed gets one made from its own
// contents, so a repeated upload of it still lands on the same server row.
// [TH] แถวแรกที่เซิร์ฟเวอร์ยังไม่ยืนยัน คืน false ถ้าไม่มี (หรือไม่มีการ์ด)
//      แถวจากก่อนที่จะมีหมายเลขรอบจะได้หมายเลขที่สร้างจากเนื้อหาของมันเอง การส่งซ้ำจึงยังลงแถวเดิมบนเซิร์ฟเวอร์
bool sdlogNextUnsynced(SyncRow& out);
void sdlogMarkSynced(uint32_t endOffset);
