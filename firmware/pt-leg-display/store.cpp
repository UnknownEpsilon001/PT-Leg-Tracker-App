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

// The add form strips commas and newlines from what the user types, so a plain
// positional split is enough and the file needs no quoting rules.
// [TH] ฟอร์มเพิ่มผู้ป่วยตัดเครื่องหมายจุลภาคและการขึ้นบรรทัดใหม่ออกแล้ว
//      จึงแยกคอลัมน์ด้วยตำแหน่งได้ตรงๆ ไม่ต้องมีกฎการใส่เครื่องหมายคำพูด
static void parseLine(const String& line) {
  if (line.length() == 0) return;
  int first = line.indexOf(',');
  int last = line.lastIndexOf(',');
  if (first < 0 || last <= first) return;  // malformed -> skip — บรรทัดเสียให้ข้าม
  Patient p;
  p.code = line.substring(0, first);
  p.name = line.substring(first + 1, last);
  p.age = (uint16_t)line.substring(last + 1).toInt();
  if (p.code == "code") return;  // header — บรรทัดหัวตาราง
  patients.push_back(p);
}

static void load() {
  patients.clear();
  File f = SD.open(PATIENTS, FILE_READ);
  if (!f) return;  // no file yet: first boot writes it on first add
                   // [TH] ยังไม่มีไฟล์ จะถูกสร้างตอนเพิ่มผู้ป่วยคนแรก
  String line;
  while (f.available()) {
    char c = f.read();
    if (c == '\n') { parseLine(line); line = ""; }
    else if (c != '\r') line += c;
  }
  if (line.length()) parseLine(line);  // last line may lack a newline — บรรทัดสุดท้ายอาจไม่มีตัวขึ้นบรรทัด
  f.close();
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
  if (mounted) SD.end();
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
  f.close();
  if (wrote != want) return false;

  patients.push_back(p);
  return true;
}
