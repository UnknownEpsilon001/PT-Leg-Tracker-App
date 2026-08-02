#include "store.h"
#include <vector>
#include <SPI.h>
#include <SD.h>

// microSD socket wiring on the CYD — fixed by the board.
// [TH] ขาช่องเสียบการ์ด SD ของบอร์ด CYD ตายตัวมาจากโรงงาน
#define SD_CS 5
#define SD_SCK 18
#define SD_MISO 19
#define SD_MOSI 23

static SPIClass sdSpi(VSPI);
static bool mounted = false;
static std::vector<Patient> patients;
static const Patient kBlank{"", "", 0};

static const char* PATIENTS = "/patients.csv";

// The card is meant to be edited on a PC, so nothing about the file is trusted.
// A roster long enough to exhaust the heap, or a file with no newlines at all,
// must not take the board down: push_back throws with exceptions off, which is
// abort() — and storeBegin runs in setup(), so that is a boot loop that ends only
// when the card comes out.
// [TH] การ์ดถูกแก้จากคอมพิวเตอร์ได้ จึงไม่เชื่อข้อมูลในไฟล์เลย
//      ทะเบียนที่ยาวจนหน่วยความจำหมด หรือไฟล์ที่ไม่มีการขึ้นบรรทัดเลย ต้องไม่ทำให้บอร์ดล้ม
//      push_back จะ abort() เมื่อปิด exception และ storeBegin ทำงานใน setup()
//      นั่นคือการรีบูตวนไม่จบจนกว่าจะถอดการ์ด
static const size_t MAX_LINE = 256;
static const size_t MAX_PATIENTS = 200;

// The add form strips commas and newlines from what the user types, so a plain
// positional split is enough and the file needs no quoting rules. A file edited
// by hand obeys no such rule, so the separators are stripped again here: a name
// typed as "สมชาย, จันทร์" would otherwise be loaded whole and then written raw
// into sessions.csv, giving that row seven fields instead of six and handing the
// reader a duration of " จันทร์".
// [TH] ฟอร์มเพิ่มผู้ป่วยตัดเครื่องหมายจุลภาคและการขึ้นบรรทัดใหม่ออกแล้ว
//      จึงแยกคอลัมน์ด้วยตำแหน่งได้ตรงๆ ไม่ต้องมีกฎการใส่เครื่องหมายคำพูด
//      แต่ไฟล์ที่แก้ด้วยมือไม่มีกฎนั้น จึงตัดตัวคั่นออกอีกครั้งที่นี่
//      ชื่อที่พิมพ์ว่า "สมชาย, จันทร์" จะถูกเขียนลง sessions.csv ตรงๆ
//      ทำให้แถวนั้นมีเจ็ดคอลัมน์แทนหกคอลัมน์ และผู้อ่านจะได้ระยะเวลาเป็น " จันทร์"
static void scrubField(String& s) {
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == ',' || c == '\r' || c == '\n') s.setCharAt(i, ' ');
  }
}

// Set only when a row was actually dropped. Comparing the final size against the
// cap instead would announce a truncation for a file holding exactly MAX_PATIENTS
// good rows, none of which were lost.
// [TH] ตั้งค่าเฉพาะตอนที่มีแถวถูกทิ้งจริงๆ ถ้าเทียบขนาดสุดท้ายกับเพดานแทน
//      ไฟล์ที่มีแถวดีครบพอดีตามเพดานจะถูกรายงานว่าโดนตัด ทั้งที่ไม่มีแถวใดหายไปเลย
static bool rosterTruncated = false;

static void parseLine(const String& line) {
  if (line.length() == 0) return;
  if (patients.size() >= MAX_PATIENTS) {  // roster capped — จำกัดจำนวนคนไข้
    rosterTruncated = true;
    return;
  }
  int first = line.indexOf(',');
  int last = line.lastIndexOf(',');
  if (first < 0 || last <= first) return;  // malformed -> skip — บรรทัดเสียให้ข้าม
  Patient p;
  p.code = line.substring(0, first);
  p.name = line.substring(first + 1, last);
  p.age = (uint16_t)line.substring(last + 1).toInt();
  if (p.code == "code") return;  // header — บรรทัดหัวตาราง
  scrubField(p.code);
  scrubField(p.name);
  patients.push_back(p);
}

static void load() {
  patients.clear();
  rosterTruncated = false;
  File f = SD.open(PATIENTS, FILE_READ);
  if (!f) return;  // no file yet: first boot writes it on first add
                   // [TH] ยังไม่มีไฟล์ จะถูกสร้างตอนเพิ่มผู้ป่วยคนแรก
  String line;
  bool overlong = false;
  while (f.available()) {
    char c = f.read();
    if (c == '\n') {
      if (overlong) Serial.println("store: over-long line in patients.csv, skipped");
      else parseLine(line);
      line = "";
      overlong = false;
    } else if (c != '\r') {
      // Past the cap the rest of the line is dropped instead of accumulated: a
      // file whose newlines were lost would otherwise become one String the size
      // of the whole file.
      // [TH] เกินขนาดที่กำหนดจะทิ้งส่วนที่เหลือของบรรทัด ไม่เก็บต่อ
      //      ไฟล์ที่ไม่มีตัวขึ้นบรรทัดจะกลายเป็นสตริงเดียวขนาดเท่าไฟล์ทั้งไฟล์
      if (line.length() >= MAX_LINE) overlong = true;
      else line += c;
    }
  }
  if (line.length() && !overlong) parseLine(line);  // last line may lack a newline
                                                    // [TH] บรรทัดสุดท้ายอาจไม่มีตัวขึ้นบรรทัด
  f.close();
  if (rosterTruncated)
    Serial.printf("store: roster capped at %u patients, later rows ignored\n",
                  (unsigned)MAX_PATIENTS);
}

bool storeBegin() {
  sdSpi.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  mounted = SD.begin(SD_CS, sdSpi);
  if (mounted) load();
  return mounted;
}

// Pulling the card does not tell the SD driver anything, so `mounted` keeps
// claiming a card that left the slot and every write fails forever. Whoever
// sees the writes fail calls this to throw the mount away and start over.
// [TH] ดึงการ์ดออกไม่มีสัญญาณบอกไดรเวอร์ ตัวแปร mounted จึงยังบอกว่ามีการ์ดอยู่
//      และการเขียนจะล้มเหลวตลอด ฝั่งที่เห็นการเขียนล้มเหลวจึงเรียกฟังก์ชันนี้เพื่อเมานต์ใหม่
void storeRemount() {
  // SD.end() runs whether or not `mounted` is set. SD.begin() returns true
  // immediately while a driver slot is still claimed, so a mount that was only
  // marked absent — by storeProbe — would otherwise be "re-mounted" without the
  // card ever being touched.
  // [TH] เรียก SD.end() เสมอ ไม่ว่า mounted จะเป็นค่าใด
  //      SD.begin() จะคืนค่าจริงทันทีถ้าไดรเวอร์ยังจองช่องอยู่
  //      การเมานต์ที่ถูกทำเครื่องหมายว่าหายไปโดย storeProbe จึงจะ "เมานต์ใหม่" โดยไม่แตะการ์ดเลย
  SD.end();
  mounted = false;
  patients.clear();
  storeBegin();
}

bool storeCardPresent() { return mounted; }

// Pulling a card raises no interrupt, so `mounted` keeps claiming one until
// something tries to write and fails. That left a swap invisible whenever the
// machine was idle: no failed write, no remount, and the display went on serving
// the settings and roster of a card that had left the slot. Opening the root
// directory is the cheapest thing that actually touches the card.
// [TH] การดึงการ์ดออกไม่มีสัญญาณแจ้ง ตัวแปร mounted จึงยังบอกว่ามีการ์ดจนกว่าจะมีการเขียนแล้วล้มเหลว
//      ทำให้การสลับการ์ดตอนเครื่องว่างมองไม่เห็นเลย ไม่มีการเขียนล้มเหลว ก็ไม่มีการเมานต์ใหม่
//      จอจึงใช้ค่าตั้งค่าและทะเบียนของการ์ดที่ถอดออกไปแล้ว การเปิดโฟลเดอร์รากคือวิธีที่ถูกที่สุดที่แตะการ์ดจริง
bool storeProbe() {
  if (!mounted) return false;
  File root = SD.open("/");
  if (root) {
    root.close();
    return true;
  }
  // The driver is torn down, not just flagged. Clearing the flag alone left
  // SD.begin() with a slot still claimed, so the caller's immediate re-mount
  // returned true against a card that was no longer in the slot: the roster came
  // back empty as "no patients" instead of "no card", the same card pushed back in
  // never reloaded, and the absent->present edge that forgets the old card's
  // settings never fired.
  // [TH] ปิดไดรเวอร์จริง ไม่ใช่แค่ตั้งค่าสถานะ การล้างสถานะอย่างเดียวทำให้ SD.begin() ยังจองช่องอยู่
  //      การเมานต์ใหม่ทันทีของผู้เรียกจึงคืนค่าจริงทั้งที่ไม่มีการ์ดในช่อง
  //      ทะเบียนกลับมาว่างในความหมาย "ไม่มีคนไข้" แทน "ไม่พบการ์ด"
  //      การ์ดใบเดิมที่เสียบกลับเข้าไปก็ไม่ถูกอ่านใหม่ และจังหวะเปลี่ยนจากไม่มีเป็นมีการ์ด
  //      ที่ใช้ลืมค่าตั้งค่าของการ์ดใบเก่าก็ไม่เคยเกิดขึ้น
  SD.end();
  mounted = false;
  patients.clear();
  return false;
}
int storePatientCount() { return (int)patients.size(); }

const Patient& storePatient(int i) {
  if (i < 0 || i >= (int)patients.size()) return kBlank;
  return patients[i];
}

bool storeAddPatient(const Patient& p) {
  if (!mounted) return false;
  if (patients.size() >= MAX_PATIENTS) return false;  // same cap the loader uses
                                                      // [TH] ใช้เพดานเดียวกับตอนโหลดไฟล์
  bool newFile = !SD.exists(PATIENTS);
  File f = SD.open(PATIENTS, FILE_APPEND);
  if (!f) return false;

  // The write is checked, and the roster only grows if the card took every byte.
  // printf's result used to be discarded: a full card reported a saved patient
  // who was on screen until the next boot and never in the file, and a half
  // written line corrupts the row appended after it.
  // [TH] ตรวจผลการเขียน และเพิ่มชื่อลงทะเบียนในหน่วยความจำเฉพาะเมื่อการ์ดรับข้อมูลครบ
  //      เดิมทิ้งค่าที่ printf คืนมา การ์ดเต็มจึงรายงานว่าบันทึกคนไข้แล้ว
  //      ชื่อจะอยู่บนจอจนกว่าจะปิดเครื่องแต่ไม่เคยอยู่ในไฟล์ และบรรทัดที่ขาดครึ่งทำให้แถวถัดไปเสีย
  String line;
  if (newFile) line += "code,name,age\n";
  line += p.code + "," + p.name + "," + String(p.age) + "\n";
  size_t want = line.length();
  size_t wrote = f.write((const uint8_t*)line.c_str(), want);
  // A short write leaves its bytes on the card; the FS layer offers no truncate,
  // so the fragment is closed off with a newline. The damage is then one
  // unparseable line — parseLine skips anything without two commas — instead of a
  // fragment that the next append is concatenated onto, which would merge two rows
  // into one and put a bogus patient in the roster on the next boot.
  // [TH] เขียนได้ไม่ครบจะทิ้งไบต์ไว้บนการ์ด และระบบไฟล์ไม่มีคำสั่งตัดท้ายไฟล์
  //      จึงปิดท้ายเศษที่ค้างด้วยการขึ้นบรรทัดใหม่ ความเสียหายจะเหลือแค่บรรทัดเดียวที่อ่านไม่ออก
  //      parseLine ข้ามบรรทัดที่ไม่มีจุลภาคสองตัวอยู่แล้ว แทนที่จะปล่อยให้แถวถัดไปต่อท้ายเศษนั้น
  //      ซึ่งจะรวมสองแถวเป็นแถวเดียวและใส่คนไข้ปลอมลงทะเบียนตอนเปิดเครื่องครั้งหน้า
  if (wrote != want && wrote > 0 && line[wrote - 1] != '\n') f.write((const uint8_t*)"\n", 1);
  f.close();
  if (wrote != want) return false;

  patients.push_back(p);
  return true;
}
