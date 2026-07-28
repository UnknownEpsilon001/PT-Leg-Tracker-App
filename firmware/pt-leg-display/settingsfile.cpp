#include "settingsfile.h"
#include <SD.h>
#include <ArduinoJson.h>
#include "store.h"  // storeCardPresent — การ์ดถูกเมานต์ที่ store.cpp ที่เดียว

static const char* PATH = "/settings.json";
static const char* TMP = "/settings.tmp";
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

bool settingsFileLoad(DeviceSettings& s) {
  filePresent = false;
  if (!storeCardPresent()) return false;
  File f = SD.open(PATH, FILE_READ);
  if (!f) return false;

  JsonDocument d;
  DeserializationError err = deserializeJson(d, f);
  f.close();

  if (err) {
    // A half-written or hand-mangled file is renamed rather than deleted: the
    // person who broke it may want to see it, and the caller writes a fresh one
    // straight after. Deleting would destroy the only copy of their password.
    // [TH] ไฟล์ที่เสียหรือถูกแก้จนผิดรูปแบบจะถูกเปลี่ยนชื่อเก็บไว้ ไม่ลบทิ้ง
    //      เพราะเจ้าของอาจอยากดูของเดิม และตัวเรียกจะเขียนไฟล์ใหม่ให้ทันที
    SD.remove(BAD);
    SD.rename(PATH, BAD);
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
  out.maxTravelSec = d["maxTravelSec"] | (uint16_t)8;
  out.holdSec = d["holdSec"] | (uint16_t)10;
  out.restSec = d["restSec"] | (uint16_t)10;

  s = out;
  filePresent = true;
  return true;
}

bool settingsFilePresent() { return filePresent; }

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
  size_t written = serializeJsonPretty(d, f);
  f.close();
  if (written == 0) {
    SD.remove(TMP);
    return false;
  }

  SD.remove(PATH);
  if (!SD.rename(TMP, PATH)) return false;
  filePresent = true;
  return true;
}
