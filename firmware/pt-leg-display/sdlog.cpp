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

// Opened for append, never for write. FILE_WRITE truncates, and pairing it with
// an SD.exists() check makes the file's survival depend on a read succeeding:
// one unreadable directory sector answers "no file", the create then wipes every
// session ever logged on that card. Appending cannot destroy anything, and an
// empty file is exactly the one this open just created.
// [TH] เปิดแบบต่อท้ายเสมอ ไม่ใช่แบบเขียนทับ เพราะ FILE_WRITE จะล้างไฟล์ทิ้ง
//      และการใช้คู่กับ SD.exists ทำให้ไฟล์อยู่รอดหรือไม่ขึ้นกับว่าการอ่านสำเร็จหรือเปล่า
//      ถ้าเซกเตอร์ไดเรกทอรีอ่านไม่ได้ครั้งเดียวจะตอบว่าไม่มีไฟล์ แล้วการสร้างใหม่จะลบผลรอบทั้งหมดบนการ์ดนั้น
//      การเปิดแบบต่อท้ายทำลายอะไรไม่ได้ และไฟล์ที่ว่างเปล่าคือไฟล์ที่การเปิดครั้งนี้เพิ่งสร้างขึ้นเอง
static const char* HEADER = "datetime,patientCode,patientName,durationSec,reps,fault\n";

// Written in one checked call, like the data rows. An unchecked println on a full
// card leaves a fragment of the header behind with a non-zero size, so no later
// call writes it again and the next append is concatenated onto that fragment —
// line 1 of the file is then unparseable.
// [TH] เขียนครั้งเดียวและตรวจผลเหมือนแถวข้อมูล ถ้าใช้ println โดยไม่ตรวจผลบนการ์ดที่เต็ม
//      จะเหลือเศษหัวตารางค้างไว้และขนาดไฟล์ไม่เป็นศูนย์ จึงไม่มีการเขียนหัวตารางซ้ำอีก
//      แล้วแถวถัดไปจะถูกต่อท้ายเศษนั้น บรรทัดแรกของไฟล์จะอ่านไม่ออก
static void writeHeader(File& f) {
  size_t want = strlen(HEADER);
  if (f.write((const uint8_t*)HEADER, want) != want)
    Serial.println("sd log: header write was short, the card may be full");
}

void sdlogEnsureHeader() {
  File f = SD.open(SESSIONS, FILE_APPEND);
  if (!f) return;
  if (f.size() == 0) writeHeader(f);
  f.close();
}

bool sdlogWrite(const SessionRow& r) {
  File f = SD.open(SESSIONS, FILE_APPEND);
  if (!f) return false;

  // FILE_APPEND creates the file, so the header has to be settled here and not
  // only in setup(): boot with no card, insert a blank one, finish a session, and
  // the row comes straight down this path — the ring is never used, so the header
  // check in sdlogFlushRing never runs and the card ends up with a sessions.csv
  // whose first line is data.
  //
  // Decided from the handle that is already open, never from SD.exists plus a
  // fresh FILE_WRITE. FILE_WRITE truncates, and SD.exists is a read that a single
  // bad directory sector can fail: exists() says no, the create succeeds, and
  // every session ever logged on that card is gone. size() is 0 exactly when this
  // open created the file, and nothing here can truncate anything.
  // [TH] FILE_APPEND สร้างไฟล์ให้ด้วย จึงต้องจัดการหัวตารางตรงนี้ ไม่ใช่แค่ใน setup()
  //      เปิดเครื่องโดยไม่มีการ์ด แล้วเสียบการ์ดเปล่า จบรอบการใช้งานหนึ่งครั้ง แถวจะมาทางนี้ตรงๆ
  //      ไม่ได้ผ่านที่พักแถว การตรวจหัวตารางใน sdlogFlushRing จึงไม่เคยทำงาน
  //      การ์ดจะได้ไฟล์ที่บรรทัดแรกเป็นข้อมูลแทนหัวตาราง
  //      ตัดสินจากไฟล์ที่เปิดค้างอยู่แล้ว ไม่ใช่จาก SD.exists แล้วเปิดใหม่ด้วย FILE_WRITE
  //      เพราะ FILE_WRITE ล้างไฟล์ทิ้ง และ SD.exists คือการอ่านที่ล้มเหลวได้ถ้าเซกเตอร์ไดเรกทอรีเสียครั้งเดียว
  //      ถ้า exists ตอบว่าไม่มีแล้วสร้างไฟล์สำเร็จ ผลรอบทั้งหมดที่เคยบันทึกบนการ์ดใบนั้นจะหายไป
  //      ค่า size() เป็นศูนย์เฉพาะตอนที่การเปิดครั้งนี้เป็นคนสร้างไฟล์ และไม่มีอะไรตรงนี้ล้างไฟล์ได้เลย
  if (f.size() == 0) writeHeader(f);

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
  // The bytes of a short write stay on the card and the FS layer has no truncate,
  // so the fragment is closed with a newline. Otherwise the retry from the ring
  // appends straight onto it and the two rows merge into one unreadable line.
  // [TH] ไบต์ที่เขียนค้างยังอยู่บนการ์ดและระบบไฟล์ไม่มีคำสั่งตัดท้ายไฟล์
  //      จึงปิดท้ายเศษด้วยการขึ้นบรรทัดใหม่ ไม่อย่างนั้นแถวที่ลองเขียนซ้ำจะต่อท้ายเศษนั้น
  //      แล้วสองแถวจะรวมเป็นบรรทัดเดียวที่อ่านไม่ออก
  if (wrote != want && wrote > 0 && line[wrote - 1] != '\n') f.write((const uint8_t*)"\n", 1);
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
  // No header check here any more: sdlogWrite settles it on the handle it already
  // has open, so every row that reaches the card carries the guarantee with it.
  // The check that used to stand here cost an extra open and close per batch and
  // could not cover the direct write path anyway.
  // [TH] ไม่ต้องตรวจหัวตารางตรงนี้แล้ว เพราะ sdlogWrite จัดการจากไฟล์ที่เปิดค้างอยู่
  //      ทุกแถวที่ลงการ์ดจึงมีหลักประกันนี้ติดไปด้วย
  //      การตรวจที่เคยอยู่ตรงนี้เสียเวลาเปิดปิดไฟล์เพิ่มหนึ่งครั้งต่อชุด และครอบคลุมทางเขียนตรงไม่ได้อยู่ดี
  while (ringCount > 0) {
    // Oldest first, so the file stays in chronological order.
    // [TH] เขียนแถวเก่าสุดก่อน ไฟล์จะได้เรียงตามเวลา
    int idx = (ringHead - ringCount + RING) % RING;
    if (!sdlogWrite(ring[idx])) return;  // still failing: keep the rest for later
    ringCount--;
  }
}

int sdlogRingDepth() { return ringCount; }
