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
  sdlogEnsureHeader();
  File f = SD.open(SESSIONS, FILE_APPEND);
  if (!f) return false;
  f.printf("%s,%s,%s,%lu,%u,%u\n", isoLocal(r.epoch).c_str(), r.patientCode.c_str(),
           r.patientName.c_str(), (unsigned long)r.durationSec, r.reps, r.fault ? 1 : 0);
  f.close();
  return true;
}

void sdlogRingPush(const SessionRow& r) {
  ring[ringHead] = r;
  ringHead = (ringHead + 1) % RING;
  if (ringCount < RING) ringCount++;  // past RING the oldest row is overwritten
                                      // [TH] ถ้าเกินความจุ แถวเก่าสุดจะถูกเขียนทับ
}

void sdlogFlushRing() {
  while (ringCount > 0) {
    // Oldest first, so the file stays in chronological order.
    // [TH] เขียนแถวเก่าสุดก่อน ไฟล์จะได้เรียงตามเวลา
    int idx = (ringHead - ringCount + RING) % RING;
    if (!sdlogWrite(ring[idx])) return;  // still failing: keep the rest for later
    ringCount--;
  }
}

int sdlogRingDepth() { return ringCount; }
