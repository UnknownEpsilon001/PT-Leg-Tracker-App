#include "settingsfile.h"
#include <SD.h>
#include <ArduinoJson.h>
#include "store.h"  // storeCardPresent — การ์ดถูกเมานต์ที่ store.cpp ที่เดียว

static const char* PATH = "/settings.json";
static const char* TMP = "/settings.tmp";
static const char* OLD = "/settings.old.json";
static const char* BAD = "/settings.bad.json";

static bool filePresent = false;

void settingsFileDefaults(DeviceSettings& s) {
  // Blank network fields, because there is no sensible guess for someone else's
  // clinic Wi-Fi. The pairing code is not blank: "default" is what the phone app
  // looks for before anyone configures anything.
  // [TH] ช่องเครือข่ายเว้นว่างไว้ เพราะเดาค่า WiFi ของคลินิกแต่ละแห่งไม่ได้
  //      ส่วนรหัสจับคู่ไม่เว้นว่าง ใช้ "default" ซึ่งเป็นค่าที่แอปบนมือถือมองหา
  s.wifiSsid = "";
  s.wifiPass = "";
  s.serverUrl = "";
  s.deviceCode = "default";
  s.maxTravelSec = 8;
  s.holdSec = 10;
  s.restSec = 10;
}

// Values a person typed into a text editor get the same range the spinboxes on
// the settings screen enforce. maxTravelSec is the safety cap on how long a
// relay may stay energized with no end-of-travel switch closing, and a typed 0
// or 600 goes straight to the controller unchallenged otherwise. A missing key
// takes its default; a present but silly one is pulled back to the edge.
// [TH] ค่าที่พิมพ์มาจากคอมพิวเตอร์ต้องอยู่ในช่วงเดียวกับที่หน้าตั้งค่าบังคับไว้
//      maxTravelSec คือเพดานเวลาที่รีเลย์จ่ายไฟได้โดยไม่มีสวิตช์ปลายทางปิด
//      ถ้าพิมพ์ 0 หรือ 600 มาแล้วส่งต่อให้คอนโทรลเลอร์ตรงๆ จะอันตราย จึงต้องดึงกลับเข้าช่วง
static uint16_t clampU16(uint16_t v, uint16_t lo, uint16_t hi, const char* key) {
  if (v >= lo && v <= hi) return v;
  uint16_t c = v < lo ? lo : hi;
  Serial.printf("settings: %s=%u is outside %u-%u, using %u\n", key, v, lo, hi, c);
  return c;
}

// Reads and parses one file. quarantine decides what happens to a file that
// will not parse: the real settings file is kept under settings.bad.json for
// the person who broke it, the recovery copy is simply dropped.
// [TH] อ่านและแปลงไฟล์หนึ่งไฟล์ ถ้าไฟล์เสีย quarantine เป็นตัวกำหนดว่าจะทำอย่างไร
//      ไฟล์ตั้งค่าจริงจะถูกเก็บไว้ในชื่อ settings.bad.json ส่วนไฟล์สำรองจะลบทิ้งไปเลย
static bool loadFrom(const char* path, DeviceSettings& s, bool quarantine) {
  File f = SD.open(path, FILE_READ);
  if (!f) return false;

  // Windows editors happily save "UTF-8 with BOM", which puts EF BB BF in front
  // of the '{'. The JSON parser reads that as garbage and the file gets thrown in
  // the quarantine bin, so a tech who picked the wrong dropdown in Notepad would
  // silently lose the settings they just typed. Step over it if it is there.
  // [TH] โปรแกรมแก้ไขข้อความบนวินโดวส์มักบันทึกแบบ UTF-8 ที่มี BOM คือมีไบต์ EF BB BF นำหน้า
  //      ตัวอ่าน JSON จะมองว่าไฟล์เสียแล้วย้ายไปเป็นไฟล์เสีย ค่าที่เพิ่งพิมพ์จะหายไปเงียบๆ
  //      ถ้าเจอ BOM ให้ข้ามไปสามไบต์
  if (f.size() >= 3) {
    uint8_t bom[3];
    if (f.read(bom, 3) != 3 || bom[0] != 0xEF || bom[1] != 0xBB || bom[2] != 0xBF) {
      f.seek(0);
    }
  }

  JsonDocument d;
  DeserializationError err = deserializeJson(d, f);
  f.close();

  if (err) {
    // A half-written or hand-mangled file is renamed rather than deleted: the
    // person who broke it may want to see it, and the caller writes a fresh one
    // straight after. Deleting would destroy the only copy of their password.
    // [TH] ไฟล์ที่เสียหรือถูกแก้จนผิดรูปแบบจะถูกเปลี่ยนชื่อเก็บไว้ ไม่ลบทิ้ง
    //      เพราะเจ้าของอาจอยากดูของเดิม และตัวเรียกจะเขียนไฟล์ใหม่ให้ทันที
    if (quarantine) {
      SD.remove(BAD);
      SD.rename(path, BAD);
    } else {
      SD.remove(path);
    }
    return false;
  }

  // Missing keys take the documented default, unknown keys are ignored, so a
  // half-filled file someone typed by hand still boots the machine.
  // [TH] คีย์ที่ขาดไปจะใช้ค่าตั้งต้น คีย์แปลกปลอมจะถูกข้าม ไฟล์ที่พิมพ์มาไม่ครบจึงยังใช้งานได้
  DeviceSettings out;
  settingsFileDefaults(out);
  out.wifiSsid = d["wifiSsid"] | "";
  out.wifiPass = d["wifiPass"] | "";
  out.serverUrl = d["serverUrl"] | "";
  out.deviceCode = d["deviceCode"] | "default";
  // The ranges are the settings screen's spinbox limits, kept in step by hand.
  // [TH] ช่วงตัวเลขตรงกับที่หน้าตั้งค่ากำหนดไว้ ต้องแก้ให้ตรงกันเองถ้ามีการเปลี่ยน
  out.maxTravelSec = clampU16(d["maxTravelSec"] | (uint16_t)8, 1, 20, "maxTravelSec");
  out.holdSec = clampU16(d["holdSec"] | (uint16_t)10, 1, 60, "holdSec");
  out.restSec = clampU16(d["restSec"] | (uint16_t)10, 1, 60, "restSec");

  s = out;
  return true;
}

bool settingsFileLoad(DeviceSettings& s) {
  filePresent = false;
  if (!storeCardPresent()) return false;

  if (loadFrom(PATH, s, true)) {
    // The recovery copy has done its job the moment the real file reads cleanly.
    // Leaving it means a stale one can be resurrected months later, when someone
    // finally mangles settings.json — and nothing downstream can tell a copy from
    // thirty seconds ago from one from thirty days ago.
    // [TH] เมื่ออ่านไฟล์จริงได้แล้ว ไฟล์สำรองก็หมดหน้าที่ ต้องลบทิ้ง
    //      ถ้าปล่อยไว้ วันหนึ่งที่ไฟล์จริงเสีย ระบบจะกู้ไฟล์สำรองเก่าค้างปีขึ้นมาใช้
    //      โดยไม่มีอะไรบอกได้ว่าสำเนานั้นเก่าแค่ไหน
    SD.remove(OLD);
    filePresent = true;
    return true;
  }

  // The real file is missing or was just quarantined. A save interrupted between
  // its two renames leaves the previous copy parked under settings.old.json —
  // take it and put it back, rather than reporting a card with no settings and
  // letting the controller's values overwrite it.
  // [TH] ไฟล์จริงหายไปหรือเพิ่งถูกย้ายเพราะเสีย ถ้าการบันทึกถูกขัดจังหวะระหว่างเปลี่ยนชื่อสองครั้ง
  //      ไฟล์เดิมจะค้างอยู่ในชื่อ settings.old.json ให้เอากลับมาใช้และตั้งชื่อคืน
  //      ดีกว่ารายงานว่าการ์ดไม่มีค่าตั้งค่าแล้วปล่อยให้ค่าจากคอนโทรลเลอร์เขียนทับ
  // Read into a scratch copy, not the caller's struct: a false return has to mean
  // nothing was read, and the rename below can still fail.
  // [TH] อ่านลงตัวแปรชั่วคราว ไม่ใช่ของผู้เรียกโดยตรง เพราะการคืนค่า false ต้องแปลว่าไม่ได้อ่านอะไรเลย
  //      และการเปลี่ยนชื่อด้านล่างยังล้มเหลวได้
  DeviceSettings recovered;
  if (loadFrom(OLD, recovered, false)) {
    SD.remove(PATH);
    if (SD.rename(OLD, PATH)) {
      Serial.println("settings: recovered the previous file after an interrupted save");
      s = recovered;
      filePresent = true;
      return true;
    }
  }
  return false;
}

bool settingsFilePresent() { return filePresent; }

// Called when the card is mounted again, which on this machine means a card was
// swapped. The flag says "a file has been read this boot", and after a swap that
// is a claim about a card which is no longer in the slot: the screen would show
// the old card's values, and a save would write them onto the new card.
// [TH] เรียกเมื่อมีการเมานต์การ์ดใหม่ ซึ่งบนเครื่องนี้แปลว่ามีการสลับการ์ด
//      ตัวแปรนี้บอกว่า "อ่านไฟล์แล้วในรอบบูตนี้" ซึ่งหลังสลับการ์ดจะกลายเป็นการอ้างถึงการ์ดที่ถอดออกไปแล้ว
//      หน้าจอจะโชว์ค่าของการ์ดใบเก่า และการบันทึกจะเขียนค่านั้นทับการ์ดใบใหม่
void settingsFileForget() { filePresent = false; }

bool settingsFileSave(const DeviceSettings& s) {
  if (!storeCardPresent()) return false;

  JsonDocument d;
  d["wifiSsid"] = s.wifiSsid;
  d["wifiPass"] = s.wifiPass;
  d["serverUrl"] = s.serverUrl;
  d["deviceCode"] = s.deviceCode;
  d["maxTravelSec"] = s.maxTravelSec;
  d["holdSec"] = s.holdSec;
  d["restSec"] = s.restSec;

  // Written to a temp file and renamed over the real one. A card pulled during
  // the write then costs the new settings, never the old ones — the alternative
  // leaves a truncated file that reads as malformed on the next boot.
  // [TH] เขียนลงไฟล์ชั่วคราวก่อนแล้วค่อยเปลี่ยนชื่อทับไฟล์จริง ถ้าการ์ดถูกดึงระหว่างเขียน
  //      จะเสียแค่ค่าที่เพิ่งตั้ง ไม่ใช่ค่าเดิม ถ้าเขียนทับตรงๆ จะได้ไฟล์ครึ่งๆ กลางๆ ที่อ่านไม่ออก
  SD.remove(TMP);
  File f = SD.open(TMP, FILE_WRITE);
  if (!f) return false;
  size_t want = measureJsonPretty(d);
  size_t written = serializeJsonPretty(d, f);
  f.close();
  // A short write is a failed write. Checking only for zero lets a half-written
  // file through on a card that is full or leaving the slot, and the rename below
  // would then put that truncated file where the good one was and delete the
  // copy — turning the one thing this sequence prevents into the thing it does.
  // [TH] เขียนได้ไม่ครบถือว่าล้มเหลว ถ้าตรวจแค่ว่าเขียนได้ศูนย์ไบต์หรือไม่
  //      ไฟล์ครึ่งๆ กลางๆ จะผ่านไปได้ตอนการ์ดเต็มหรือกำลังถูกดึงออก
  //      แล้วขั้นเปลี่ยนชื่อด้านล่างจะเอาไฟล์เสียไปแทนไฟล์ดีและลบสำเนาทิ้ง
  if (written != want) {
    Serial.printf("settings: wrote %u of %u bytes, keeping the old file\n", (unsigned)written,
                  (unsigned)want);
    SD.remove(TMP);
    return false;
  }

  // The previous file is moved aside, not deleted, before the new one takes its
  // name. Deleting first leaves a window with no settings file at all: a card
  // pulled inside it costs the whole configuration, which is the one thing this
  // feature exists to prevent. If the file cannot be moved aside, the save fails
  // and the old values stay — never delete without a copy in hand.
  // [TH] ไฟล์เดิมจะถูกย้ายไปเก็บก่อน ไม่ใช่ลบทิ้ง แล้วค่อยให้ไฟล์ใหม่ใช้ชื่อแทน
  //      ถ้าลบก่อนจะมีช่วงที่ไม่มีไฟล์ตั้งค่าเลย ถ้าการ์ดถูกดึงตอนนั้นจะเสียค่าทั้งหมด
  //      ซึ่งเป็นสิ่งที่ฟีเจอร์นี้ตั้งใจป้องกัน ถ้าย้ายไม่ได้ให้ถือว่าบันทึกไม่สำเร็จและคงค่าเดิมไว้
  SD.remove(OLD);
  if (SD.exists(PATH) && !SD.rename(PATH, OLD)) {
    SD.remove(TMP);
    return false;
  }
  if (!SD.rename(TMP, PATH)) {
    SD.rename(OLD, PATH);  // put the previous file back under its own name
    return false;
  }
  SD.remove(OLD);
  filePresent = true;
  return true;
}
