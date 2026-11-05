#include "sdlog.h"
#include <SD.h>
#include <time.h>

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
  // A space, not ISO's "T": Excel reads "2026-11-05 14:32:10" as a date and
  // time it can sort and filter, and the T form as plain text.
  // [TH] ใช้ช่องว่างแทนตัว T แบบ ISO เพราะ Excel อ่านรูปแบบนี้เป็นวันเวลาที่เรียงและกรองได้ ส่วนแบบมี T จะเป็นข้อความธรรมดา
  char buf[24];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tmv);
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
// The file starts with a UTF-8 byte order mark. Without it Excel opens the CSV
// in the PC's ANSI code page and every Thai name turns into mojibake; with it a
// double-click shows them correctly. A card made before this keeps its old
// header (the mark is written only when the file is created), so rename or
// delete that file once to get a fresh one.
// [TH] ไฟล์ขึ้นต้นด้วยเครื่องหมาย BOM ของ UTF-8 ถ้าไม่มี Excel จะเปิดด้วยรหัสอักษรของเครื่อง ชื่อภาษาไทยจะกลายเป็นตัวอ่านไม่ออก
//      ถ้ามีจะดับเบิลคลิกแล้วแสดงถูกต้อง การ์ดที่สร้างไฟล์ไว้ก่อนหน้านี้ยังเป็นหัวตารางเดิม (BOM เขียนเฉพาะตอนสร้างไฟล์)
//      จึงต้องเปลี่ยนชื่อหรือลบไฟล์เดิมหนึ่งครั้งเพื่อให้ได้ไฟล์ใหม่
// sessionId is last so every column before it keeps its place for anyone who
// reads the file by position.
// [TH] sessionId อยู่ท้ายสุด คอลัมน์ก่อนหน้าจึงอยู่ตำแหน่งเดิมสำหรับคนที่อ่านไฟล์ตามตำแหน่งคอลัมน์
static const char* HEADER =
    "\xEF\xBB\xBF" "datetime,patientCode,patientName,durationSec,reps,fault,sessionId\n";
#define SYNCED "/synced.txt"

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
                (r.fault ? "1" : "0") + "," + r.uid + "\n";
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

// "2026-11-05 14:32:10" (or the older "2026-11-05T14:32:10") -> "05/11 14:32".
// Anything else, "?" included, is shown as it is.
// [TH] แปลงวันเวลาบนการ์ดเป็นรูปแบบสั้นสำหรับจอ ค่าอื่นรวมถึง "?" แสดงตามเดิม
static String shortWhen(const String& s) {
  if (s.length() < 16 || s[4] != '-' || s[7] != '-') return s;
  return s.substring(8, 10) + "/" + s.substring(5, 7) + " " + s.substring(11, 16);
}

// The tail read: 6 KB holds about sixty rows of Thai names, three times what the
// screen lists.
// [TH] อ่านท้ายไฟล์ 6 กิโลไบต์ ซึ่งจุได้ราวหกสิบแถวที่มีชื่อภาษาไทย มากกว่าที่จอแสดงสามเท่า
static const uint32_t TAIL_BYTES = 6144;

int sdlogReadRecent(HistRow* out, int max) {
  if (max <= 0) return 0;
  File f = SD.open(SESSIONS, FILE_READ);
  if (!f) return -1;
  uint32_t size = f.size();
  uint32_t start = size > TAIL_BYTES ? size - TAIL_BYTES : 0;
  f.seek(start);
  if (start > 0) f.readStringUntil('\n');  // landed mid-row: drop the fragment — ตกกลางแถว ทิ้งเศษนั้น

  // out[] is used as a ring while reading, so only the newest `max` rows stay;
  // head is the next slot to write.
  // [TH] ใช้ out[] เป็นวงแหวนระหว่างอ่าน จึงเหลือเฉพาะ max แถวล่าสุด head คือช่องถัดไปที่จะเขียน
  int n = 0, head = 0;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (!line.length()) continue;
    if (line.indexOf("datetime,") >= 0) continue;  // the header, BOM or not — หัวตาราง
    // datetime,code,name,durationSec,reps,fault. Names cannot hold a comma (the
    // add-patient form strips it), so a positional split is safe.
    // [TH] ชื่อมีเครื่องหมายจุลภาคไม่ได้ เพราะฟอร์มลงทะเบียนตัดทิ้ง การแยกตามตำแหน่งจึงปลอดภัย
    int c[5], at = -1;
    bool ok = true;
    for (int i = 0; i < 5; i++) {
      at = line.indexOf(',', at + 1);
      if (at < 0) { ok = false; break; }
      c[i] = at;
    }
    if (!ok) continue;  // a torn row from a full card — แถวที่ขาดจากการ์ดเต็ม
    HistRow& r = out[head];
    r.when = shortWhen(line.substring(0, c[0]));
    String code = line.substring(c[0] + 1, c[1]);
    r.who = line.substring(c[1] + 1, c[2]);
    if (!r.who.length()) r.who = code;
    r.durationSec = (uint32_t)line.substring(c[2] + 1, c[3]).toInt();
    r.reps = (uint16_t)line.substring(c[3] + 1, c[4]).toInt();
    // The fault column is followed by sessionId on new files and by nothing on old
    // ones, so it ends at the next comma or at the end of the line.
    // [TH] คอลัมน์ fault ตามด้วย sessionId ในไฟล์ใหม่ และไม่มีอะไรตามในไฟล์เก่า จึงจบที่จุลภาคถัดไปหรือท้ายบรรทัด
    int c5 = line.indexOf(',', c[4] + 1);
    r.fault = line.substring(c[4] + 1, c5 < 0 ? line.length() : c5) == "1";
    head = (head + 1) % max;
    if (n < max) n++;
  }
  f.close();

  // Rotate the ring so out[0] is the newest row.
  // [TH] หมุนวงแหวนให้ out[0] เป็นแถวล่าสุด
  HistRow* tmp = new HistRow[n > 0 ? n : 1];
  for (int i = 0; i < n; i++) tmp[i] = out[(head - 1 - i + max) % max];
  for (int i = 0; i < n; i++) out[i] = tmp[i];
  delete[] tmp;
  return n;
}

static uint32_t readSyncedOffset() {
  File f = SD.open(SYNCED, FILE_READ);
  if (!f) return 0;
  uint32_t v = (uint32_t)f.readStringUntil('\n').toInt();
  f.close();
  return v;
}

// Overwritten whole each time: it is one number, and FILE_WRITE truncating it is
// the point. A torn write leaves a short number, which only means a few rows are
// offered again, and the server answers repeats with the row it already has.
// [TH] เขียนทับทั้งไฟล์ทุกครั้ง เพราะเป็นตัวเลขตัวเดียว และการล้างไฟล์ของ FILE_WRITE คือสิ่งที่ต้องการ
//      ถ้าเขียนค้างครึ่งทางจะได้ตัวเลขที่น้อยกว่า ซึ่งแค่ทำให้บางแถวถูกส่งซ้ำ และเซิร์ฟเวอร์ตอบการส่งซ้ำด้วยแถวเดิม
void sdlogMarkSynced(uint32_t endOffset) {
  File f = SD.open(SYNCED, FILE_WRITE);
  if (!f) return;
  f.print(endOffset);
  f.print("\n");
  f.close();
}

bool sdlogNextUnsynced(SyncRow& out) {
  File f = SD.open(SESSIONS, FILE_READ);
  if (!f) return false;
  uint32_t size = f.size();
  uint32_t at = readSyncedOffset();
  // A mark past the end means the card's sessions.csv was replaced or deleted:
  // start over from the top of the new one.
  // [TH] ตำแหน่งที่เลยท้ายไฟล์แปลว่า sessions.csv บนการ์ดถูกเปลี่ยนหรือลบไปแล้ว จึงเริ่มใหม่จากต้นไฟล์ใหม่
  if (at > size) at = 0;
  f.seek(at);
  while (f.available()) {
    String line = f.readStringUntil('\n');
    uint32_t end = f.position();
    line.trim();
    if (!line.length() || line.indexOf("datetime,") >= 0) {
      sdlogMarkSynced(end);  // header or blank: nothing to send — หัวตารางหรือบรรทัดว่าง ไม่มีอะไรต้องส่ง
      continue;
    }
    int c[6], pos = -1, k = 0;
    while (k < 6 && (pos = line.indexOf(',', pos + 1)) >= 0) c[k++] = pos;
    if (k < 5) {
      sdlogMarkSynced(end);  // a torn row: skip it rather than stall on it — แถวที่ขาด ข้ามไปแทนการค้างอยู่ที่แถวนั้น
      continue;
    }
    out.endOffset = end;
    out.when = line.substring(0, c[0]);
    out.code = line.substring(c[0] + 1, c[1]);
    out.durationSec = (uint32_t)line.substring(c[2] + 1, c[3]).toInt();
    out.reps = (uint16_t)line.substring(c[3] + 1, c[4]).toInt();
    out.fault = line.substring(c[4] + 1, k > 5 ? c[5] : line.length()) == "1";
    out.uid = k > 5 ? line.substring(c[5] + 1) : String("");
    if (!out.uid.length()) {
      // Made from the row itself: the same row always gets the same number. The
      // offset is added only when the time is unknown, so two identical "?" rows
      // stay two sessions.
      // [TH] สร้างจากเนื้อหาของแถว แถวเดิมจึงได้หมายเลขเดิมเสมอ ใส่ตำแหน่งไบต์เพิ่มเฉพาะตอนที่ไม่ทราบเวลา
      //      แถว "?" สองแถวที่เหมือนกันจึงยังเป็นสองรอบ
      String w = out.when;
      w.replace("-", ""); w.replace(" ", ""); w.replace(":", ""); w.replace("T", "");
      out.uid = "csv-" + w + "-" + out.code + "-" + String(out.durationSec) + "-" + String(out.reps);
      if (out.when == "?") out.uid += "-" + String(end);
      if (out.uid.length() > 64) out.uid = out.uid.substring(0, 64);
    }
    f.close();
    return true;
  }
  f.close();
  return false;
}
