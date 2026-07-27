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
  if (newFile) f.println("code,name,age");
  f.printf("%s,%s,%u\n", p.code.c_str(), p.name.c_str(), p.age);
  f.close();
  patients.push_back(p);
  return true;
}
