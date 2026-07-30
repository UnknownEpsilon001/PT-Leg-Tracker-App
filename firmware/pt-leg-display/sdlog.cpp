#include "sdlog.h"
#include <SD.h>
#include <time.h>

// The wire carries UTC end to end; the offset is added only here, where a human
// reads the result. Thailand has no DST, so a fixed offset is exact.
// [TH] เวลาที่ส่งกันเป็น UTC ทั้งหมด บวกเวลาไทยเฉพาะตอนเขียนให้คนอ่าน
//      ประเทศไทยไม่มีการปรับเวลาตามฤดูกาล ค่าคงที่จึงถูกต้องเสมอ
#define TZ_OFFSET_SEC (7 * 3600)
#define SESSIONS "/sessions.csv"

// Rows that could not be written (card absent, write error) wait here. Eight is
// a therapy session's worth: enough to survive someone yanking the card, small
// enough to cost nothing in RAM.
// [TH] แถวที่เขียนไม่สำเร็จจะพักไว้ตรงนี้ แปดแถวพอสำหรับการใช้งานหนึ่งช่วง
static const int RING = 8;
static SessionRow ring[RING];
static int ringHead = 0, ringCount = 0;

static String isoLocal(uint32_t epochUtc) {
  // "?" rather than 1970-01-01: a wrong-looking date invites someone to trust
  // it. A question mark is greppable and obviously not a time.
  // [TH] ใช้ "?" แทนวันที่ปี 1970 เพราะวันที่ผิดๆ ชวนให้คนเข้าใจว่าเป็นเวลาจริง
  if (epochUtc == 0) return "?";
  time_t t = (time_t)epochUtc + TZ_OFFSET_SEC;
  struct tm tmv;
  gmtime_r(&t, &tmv);  // gmtime, because the offset is already in t
  char buf[24];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tmv);
  return String(buf);
}

void sdlogEnsureHeader() {
  if (SD.exists(SESSIONS)) return;
  File f = SD.open(SESSIONS, FILE_WRITE);
  if (!f) return;
  f.println("datetime,patientCode,patientName,durationSec,reps,fault");
  f.close();
}

bool sdlogWrite(const SessionRow& r) {
  File f = SD.open(SESSIONS, FILE_APPEND);
  if (!f) return false;

  // Built first, then written in one call and checked: printf's return value was
  // being thrown away, so a card that accepted the open but not the bytes — a
  // full one — reported success. sdlogFlushRing believed it and dropped the row,
  // losing the only copy of a finished session, and half a line left behind
  // corrupts whatever is appended next.
  // [TH] สร้างข้อความให้ครบก่อน แล้วเขียนครั้งเดียวและตรวจผล เดิมทิ้งค่าที่ printf คืนมา
  //      การ์ดที่เปิดไฟล์ได้แต่เขียนไม่ได้ เช่นการ์ดเต็ม จึงรายงานว่าสำเร็จ
  //      ตัวรวบรวมแถวค้างเชื่อตามนั้นแล้วทิ้งแถวไป ผลรอบนั้นหายไปทั้งที่มีสำเนาเดียว
  //      และบรรทัดที่เขียนค้างครึ่งเดียวยังทำให้แถวถัดไปเสียด้วย
  String line = isoLocal(r.epoch) + "," + r.patientCode + "," + r.patientName + "," +
                String((unsigned long)r.durationSec) + "," + String(r.reps) + "," +
                (r.fault ? "1" : "0") + "\n";
  size_t want = line.length();
  size_t wrote = f.write((const uint8_t*)line.c_str(), want);
  f.close();
  return wrote == want;
}

void sdlogRingPush(const SessionRow& r) {
  ring[ringHead] = r;
  ringHead = (ringHead + 1) % RING;
  if (ringCount < RING) ringCount++;  // past RING the oldest row is overwritten
                                      // [TH] ถ้าเกินความจุ แถวเก่าสุดจะถูกเขียนทับ
}

void sdlogFlushRing() {
  // Once for the whole batch, not once per row: the header can only be missing
  // on the first write after a mount, and the old placement cost an SD.exists
  // probe plus an open/close for every row in the ring.
  // [TH] ตรวจหัวตารางครั้งเดียวต่อการเขียนหนึ่งชุด ไม่ใช่ทุกแถว
  //      หัวตารางจะขาดได้เฉพาะการเขียนครั้งแรกหลังเมานต์การ์ด
  //      ของเดิมเสียเวลาตรวจไฟล์และเปิดปิดไฟล์ทุกแถว
  if (ringCount > 0) sdlogEnsureHeader();
  while (ringCount > 0) {
    // Oldest first, so the file stays in chronological order.
    // [TH] เขียนแถวเก่าสุดก่อน ไฟล์จะได้เรียงตามเวลา
    int idx = (ringHead - ringCount + RING) % RING;
    if (!sdlogWrite(ring[idx])) return;  // still failing: keep the rest for later
    ringCount--;
  }
}

int sdlogRingDepth() { return ringCount; }
