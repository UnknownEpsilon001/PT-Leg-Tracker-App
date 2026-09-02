// สเก็ตช์หลักของบอร์ดจอ CYD (ESP32) — รับสถานะจากคอนโทรลเลอร์มาแสดง หน้าจอหลักแสดงผลอย่างเดียว มีเพียงการตั้งค่าที่ส่งกลับไป
// Display board (CYD) main sketch: shows controller state; main screen is output-only, only settings are sent back.
//
// Bench aid: route the controller link over the USB serial port so a PC can
// impersonate the controller and drive the whole UI with no second board
// attached. Off for real hardware; enable per-build, never commit it on:
//   flip this to 1, then erase_flash before reflashing (see firmware/README.md)
// Drive it with firmware/tools/fake-controller.ps1.
// [TH] โหมดทดสอบบนโต๊ะ: ต่อลิงก์ผ่านพอร์ต USB ให้พีซีสวมรอยเป็นคอนโทรลเลอร์ ทดสอบ UI ได้โดยไม่ต้องมีบอร์ดที่สอง
//      ใช้เฉพาะตอนทดสอบ ห้าม commit ทั้งค่าเปิดไว้ ขับด้วย firmware/tools/fake-controller.ps1
#ifndef LINK_OVER_USB
#define LINK_OVER_USB 0
#endif

// Bench aid: time the loop and the arriving link frames, to tell a display that
// stalls apart from a link that starves. The screen showing 1-3-6 instead of
// 1-2-3 can mean either, and they need opposite fixes. Prints a slow-iteration
// line whenever one pass exceeds LOOP_DEBUG_SLOW_MS with a breakdown of where
// the time went, plus a once-a-second summary of frames received and the
// longest gap between them. Off for real hardware; enable per-build:
//   --build-property compiler.cpp.extra_flags=-DLOOP_DEBUG=1 --clean
// [TH] โหมดทดสอบ: จับเวลาลูปและข้อความที่เข้ามา เพื่อแยกว่าจอค้างเอง หรือข้อมูลไม่มาถึง
//      หน้าจอที่นับ 1-3-6 แทน 1-2-3 เกิดได้จากทั้งสองสาเหตุ ซึ่งต้องแก้คนละทาง
//      จะพิมพ์เมื่อลูปรอบใดใช้เวลาเกินค่าที่ตั้ง พร้อมบอกว่าเวลาหมดไปกับส่วนไหน
//      และสรุปทุกหนึ่งวินาทีว่ารับข้อความได้กี่ครั้ง และเว้นช่วงนานสุดเท่าไร
//      ใช้เฉพาะตอนทดสอบ ห้าม commit ทั้งค่าเปิดไว้
#ifndef LOOP_DEBUG
#define LOOP_DEBUG 0
#endif
#define LOOP_DEBUG_SLOW_MS 300

#include "ui.h"
#include "link.h"
#include "session.h"   // Phase enum for uiUpdateMain — ใช้ enum Phase
#include "settings.h"  // DeviceSettings cached from the controller — เก็บค่าตั้งค่าที่คอนโทรลเลอร์ส่งมา
#include "store.h"     // patient roster on the SD card — ทะเบียนคนไข้บนการ์ด SD
#include "sdlog.h"     // session rows written to the SD card — บันทึกผลแต่ละรอบลงการ์ด
#include "settingsfile.h"  // /settings.json on the card — ค่าตั้งค่าบนการ์ด
#include "uitest.h"        // bench-only serial control of the UI — โหมดทดสอบผ่านสาย
#include "audio.h"         // spoken cues from /sounds on the card — เสียงพูดจากการ์ด
#include <driver/gpio.h>

// Both drive the USB serial port, so enabling both means two readers fighting
// over the same bytes and neither working.
// [TH] ทั้งสองโหมดใช้พอร์ตยูเอสบีเดียวกัน ถ้าเปิดพร้อมกันจะแย่งกันอ่านและพังทั้งคู่
#if UI_TEST_HOOK && LINK_OVER_USB
#error "UI_TEST_HOOK and LINK_OVER_USB both take the USB serial port; enable only one"
#endif

Link gLink;  // ลิงก์ UART ไปยังคอนโทรลเลอร์
static bool uiRunning = false;
static Phase uiPhase = Phase::Idle;
static uint32_t uiElapsed = 0;
static uint16_t uiReps = 0;
static uint16_t uiTarget = 0;
// Previous value of the running flag, for the start cue's edge. The sentinel
// matters: the first state frame after boot seeds it without emitting, because a
// display restarted beside a running machine must not announce a start that
// already happened.
// [TH] ค่าก่อนหน้าของสถานะกำลังทำงาน ใช้จับขอบขาขึ้นของเสียงเริ่ม
//      ค่าพิเศษนี้สำคัญ ข้อความสถานะแรกหลังบูตจะใช้ตั้งค่าเริ่มต้นโดยไม่ส่งเสียง
//      เพราะจอที่รีสตาร์ตข้างเครื่องที่กำลังทำงานอยู่ ต้องไม่ประกาศการเริ่มที่เกิดไปแล้ว
static int8_t gPrevRunning = -1;  // -1 = ยังไม่เคยได้รับข้อความสถานะ
static bool uiWifi = false, uiServer = false;

// Last settings the controller told us about; the settings screen pre-fills from
// this. Seeded with the documented defaults rather than left zeroed: a zeroed
// struct puts 0 into the spinboxes, LVGL clamps that to their minimum of 1, and
// a SAVE before the controller ever answered would push a 1-second travel cap
// over good NVS settings. uiSetSettingsKnown() gates SAVE until a real reply
// lands; these values only decide what the screen shows in the meantime.
// [TH] ค่าตั้งค่าล่าสุดที่คอนโทรลเลอร์บอกมา ใช้เติมหน้าตั้งค่า ตั้งต้นด้วยค่าเริ่มต้นไม่ใช่ศูนย์
//      เพราะถ้าเป็นศูนย์ LVGL จะบีบเป็น 1 และหาก SAVE ก่อนคอนโทรลเลอร์ตอบ อาจเขียนเพดานเวลา 1 วินาที
//      ทับค่าที่ดีอยู่ ปุ่ม SAVE ถูกกั้นด้วย uiSetSettingsKnown() จนกว่าจะได้ค่าจริง
// The trailing `true` is soundOn. Leaving the initializer short would
// value-initialize it to false — sound off by default, from a line that does not
// mention sound.
// [TH] ค่า true ตัวท้ายคือ soundOn ถ้าปล่อยให้สั้นกว่าจำนวนฟิลด์ ค่านี้จะถูกตั้งเป็นเท็จโดยปริยาย
//      กลายเป็นปิดเสียงตั้งแต่ต้น จากบรรทัดที่ไม่ได้พูดถึงเสียงเลย
static DeviceSettings gLast = {"", "", "", "default", 8, 4, 4, 10, 10, 10, true};
static bool gSettingsKnown = false;
static bool gLinkUp = false;

// Did the card hold a readable settings file at boot? Decides which way the
// first reconciliation runs: card wins if it did, controller wins if it did not.
// [TH] ตอนบูตอ่านไฟล์ตั้งค่าจากการ์ดได้หรือไม่ ตัวแปรนี้ตัดสินว่ารอบแรกจะยึดค่าจากฝั่งไหน
//      ถ้าอ่านได้ให้ยึดการ์ด ถ้าไม่ได้ให้ยึดค่าจากคอนโทรลเลอร์
static bool gCardSettings = false;
static bool gReconciled = false;  // the first controller reply has been handled

// Was a card in the slot at the previous SD tick? Only the absent-to-present
// edge means "somebody put a card in", which is the moment the settings on it
// have to be read again.
// [TH] รอบตรวจการ์ดครั้งก่อนมีการ์ดอยู่หรือไม่ เฉพาะจังหวะที่เปลี่ยนจากไม่มีเป็นมีเท่านั้น
//      ที่แปลว่ามีคนเพิ่งเสียบการ์ด ซึ่งเป็นจังหวะที่ต้องอ่านค่าตั้งค่าบนการ์ดใหม่
static bool gCardWasPresent = false;

// Who the next session belongs to. Empty until someone picks a patient, and it
// stays empty all day if nobody does: the machine must run for a walk-up patient
// with no roster at all, the row just logs without a name.
//
// The code and name are copied at the moment of the tap rather than an index
// being kept. A roster index means nothing once the roster is reloaded — and it
// is reloaded whenever a card is swapped, or simply re-mounted after a failed
// write. Row 3 of the new card is a different person, so the finished session
// was filed under a name the operator never chose, on a machine whose whole
// purpose is per-patient progress. Copying the identity also keeps the row
// correct when the chosen patient is not on the card that happens to be in the
// slot at the end of the session.
// [TH] คนไข้ที่รอบถัดไปจะถูกบันทึกให้ ว่างไว้จนกว่าจะมีคนเลือก และปล่อยว่างทั้งวันก็ได้
//      เครื่องต้องใช้งานได้แม้ไม่มีใครลงทะเบียน แค่บันทึกโดยไม่มีชื่อ
//      เก็บรหัสและชื่อตั้งแต่ตอนที่แตะเลือก ไม่ได้เก็บเป็นลำดับที่ในทะเบียน
//      เพราะลำดับที่จะไม่มีความหมายทันทีที่ทะเบียนถูกโหลดใหม่ ซึ่งเกิดขึ้นทุกครั้งที่สลับการ์ด
//      หรือแค่เมานต์การ์ดใหม่หลังเขียนไม่สำเร็จ แถวที่สามของการ์ดใบใหม่คือคนละคน
//      รอบที่จบไปแล้วจึงถูกบันทึกในชื่อที่ผู้ใช้ไม่ได้เลือก บนเครื่องที่มีไว้ติดตามผลรายคนโดยเฉพาะ
//      การเก็บตัวตนไว้ยังทำให้แถวถูกต้องแม้คนที่เลือกไว้จะไม่ได้อยู่บนการ์ดที่เสียบอยู่ตอนจบรอบ
static String gActiveCode;
static String gActiveName;

// Card values go to the controller the moment they are known, because the card
// is the master copy wherever it was read — at boot or hours later.
// [TH] ค่าจากการ์ดจะถูกส่งให้คอนโทรลเลอร์ทันทีที่รู้ค่า เพราะการ์ดคือค่าหลักเสมอ
//      ไม่ว่าจะอ่านได้ตอนบูตหรืออ่านได้ทีหลัง
static void pushSettingsToController(const DeviceSettings& s) {
  LinkSettings ls;
  ls.ssid = s.wifiSsid; ls.pass = s.wifiPass; ls.server = s.serverUrl; ls.code = s.deviceCode;
  ls.maxTravel = s.maxTravelSec; ls.lift = s.liftSec; ls.lower = s.lowerSec;
  ls.hold = s.holdSec; ls.rest = s.restSec; ls.target = s.targetReps;
  gLink.sendSettings(ls);
}

static void onOpenSettings() {
  Serial.println("touch: settings");
  // The card is only read at boot, so a card carried from a PC to a running
  // machine went unnoticed until someone power-cycled it — which is exactly the
  // trip this whole feature exists to make. The mount retry in loop() does not
  // help: it reloads the roster, not the settings. Re-read here, once, when the
  // slot has a card whose file has not been read this boot.
  // [TH] เดิมอ่านไฟล์เฉพาะตอนบูต การ์ดที่เพิ่งแก้จากคอมพิวเตอร์แล้วเสียบเข้าเครื่องที่เปิดอยู่จึงไม่มีผล
  //      จนกว่าจะปิดเปิดเครื่องใหม่ ทั้งที่นี่คือการใช้งานหลักของฟีเจอร์นี้
  //      ลูปเมานต์ใหม่ช่วยไม่ได้ เพราะมันโหลดแค่ทะเบียนคนไข้ ไม่ใช่ค่าตั้งค่า จึงอ่านซ้ำตรงนี้
  if (storeCardPresent() && !settingsFilePresent()) {
    if (settingsFileLoad(gLast)) {
      gCardSettings = true;
      uiSetDeviceCode(gLast.deviceCode.c_str());
      audioSetEnabled(gLast.soundOn);  // a card inserted late carries this too — การ์ดที่เพิ่งเสียบก็พาค่านี้มา
      pushSettingsToController(gLast);
      Serial.println("settings: read the card late and pushed it to the controller");
    } else if (gSettingsKnown) {
      // No file, or one that just got quarantined. Seed it the way boot does, so
      // a card is never left with nothing to read. Only once the controller's
      // real values are known — a fresh card must not be given placeholders.
      // [TH] ไม่มีไฟล์ หรือไฟล์เพิ่งถูกย้ายเพราะเสีย ให้สร้างไฟล์ใหม่เหมือนตอนบูต
      //      การ์ดจะได้ไม่ถูกทิ้งไว้โดยไม่มีอะไรให้อ่าน และทำเฉพาะเมื่อรู้ค่าจริงจากคอนโทรลเลอร์แล้ว
      //      การ์ดใหม่ต้องไม่ได้รับค่าตัวอย่าง
      Serial.printf("settings: card had no usable file, wrote a fresh one: %s\n",
                    settingsFileSave(gLast) ? "written" : "write failed");
    }
  }
  uiSetSdStatus(storeCardPresent(), settingsFilePresent());
  uiOpenSettings(gLast);
}
static void onOpenData() { Serial.println("touch: data"); uiOpenData(); }

// -1 tells the UI "no card", which reads differently from an empty roster.
// [TH] คืน -1 เพื่อบอก UI ว่าไม่มีการ์ด ซึ่งความหมายต่างจากทะเบียนที่ว่างเปล่า
static int dataCount() { return storeCardPresent() ? storePatientCount() : -1; }
static const Patient* dataGet(int i) { return &storePatient(i); }
static bool dataAdd(const char* code, const char* name, int age) {
  Patient p{String(code), String(name), (uint16_t)age};
  return storeAddPatient(p);
}
static void dataSetActive(int i) {
  if (i >= 0 && i < storePatientCount()) {
    const Patient& p = storePatient(i);
    gActiveCode = p.code;
    gActiveName = p.name;
  } else {
    gActiveCode = "";
    gActiveName = "";
  }
}

static void onSaveFromUi(const DeviceSettings& s) {
  gLast = s;
  audioSetEnabled(s.soundOn);  // takes effect now, not on the next boot — มีผลทันที ไม่ต้องรอเปิดเครื่องใหม่
  LinkSettings ls;
  ls.ssid = s.wifiSsid; ls.pass = s.wifiPass; ls.server = s.serverUrl; ls.code = s.deviceCode;
  ls.maxTravel = s.maxTravelSec; ls.lift = s.liftSec; ls.lower = s.lowerSec;
  ls.hold = s.holdSec; ls.rest = s.restSec; ls.target = s.targetReps;
  gLink.sendSettings(ls);
  uiSetDeviceCode(s.deviceCode.c_str());

  // The controller gets the values either way; the card is best-effort. Settings
  // are never silently dropped, so a failed card write is reported, not hidden.
  // [TH] คอนโทรลเลอร์ได้รับค่าเสมอ ส่วนการ์ดเขียนได้ก็ดี ไม่ได้ก็ไม่เป็นไร
  //      แต่ต้องไม่เงียบ ถ้าเขียนการ์ดไม่สำเร็จต้องบอกผู้ใช้
  bool toCard = settingsFileSave(s);
  Serial.printf("settings saved: controller yes, card %s\n", toCard ? "yes" : "no");
  uiReportSettingsSaved(toCard, storeCardPresent());
}

static void onLinkSettingsReply(const LinkSettings& s) {
  // The card wins when it had a file, so a password edited on a PC actually
  // takes effect. Push it once, then let later replies flow in normally — the
  // controller echoes what it just stored, and fighting that would loop.
  // [TH] ถ้าการ์ดมีไฟล์อยู่ให้ยึดค่าจากการ์ด รหัสผ่านที่แก้จากคอมพิวเตอร์จะได้มีผลจริง
  //      ส่งให้คอนโทรลเลอร์ครั้งเดียว จากนั้นปล่อยให้การตอบกลับครั้งถัดไปทำงานตามปกติ
  //      เพราะคอนโทรลเลอร์จะสะท้อนค่าที่เพิ่งบันทึกกลับมา ถ้าดันกันไปมาจะวนไม่จบ
  if (gCardSettings && !gReconciled) {
    gReconciled = true;
    pushSettingsToController(gLast);
    Serial.println("settings: pushed the card's values to the controller");
    gSettingsKnown = true;
    uiSetSettingsKnown(true);
    return;
  }

  // Assigned field by field, never as a whole struct. soundOn is deliberately
  // absent from LinkSettings, so it survives this only because nothing here
  // overwrites it — rewriting this as `gLast = built` would silently reset the
  // user's sound setting on every controller reconnect.
  // [TH] กำหนดค่าทีละฟิลด์ ไม่ใช่ทั้งโครงสร้าง ค่า soundOn จงใจไม่มีใน LinkSettings
  //      จึงรอดมาได้เพราะไม่มีอะไรตรงนี้เขียนทับ
  //      ถ้าเขียนใหม่เป็น gLast = built ค่าเสียงที่ผู้ใช้ตั้งไว้จะถูกล้างทุกครั้งที่คอนโทรลเลอร์เชื่อมต่อใหม่
  gLast.wifiSsid = s.ssid; gLast.wifiPass = s.pass; gLast.serverUrl = s.server;
  gLast.deviceCode = s.code;
  // The absent-field conventions are consumed here too, not just on the
  // controller. link.cpp is shared, so a reply decodes exactly the same way on
  // this board: 0 for a timing field it did not carry, LINK_TARGET_ABSENT for a
  // target it did not carry. Assigning those raw put 0 s timings and 65535 reps
  // into gLast, which is what the settings screen shows, what settingsFileSave
  // writes to the card, and what pushSettingsToController sends back — and 65535
  // reads as "absent" at the controller, so the card's real target would be
  // silently ignored on the very push that is supposed to carry it. That is the
  // mixed-version case the sentinel exists for, defeated on the return leg.
  // [TH] ข้อตกลงเรื่องฟิลด์ที่ไม่ได้ส่งมาต้องถูกจัดการที่นี่ด้วย ไม่ใช่แค่ฝั่งคอนโทรลเลอร์
  //      เพราะ link.cpp ใช้ร่วมกัน ข้อความตอบกลับจึงถอดรหัสแบบเดียวกันเป๊ะบนบอร์ดนี้
  //      คือ 0 สำหรับฟิลด์เวลาที่ไม่ได้ส่งมา และ LINK_TARGET_ABSENT สำหรับเป้าหมายที่ไม่ได้ส่งมา
  //      ถ้ากำหนดค่าดิบเข้าไปเลย จะได้ค่าเวลาเป็น 0 วินาทีและเป้าหมาย 65535 ครั้งลงใน gLast
  //      ซึ่งเป็นค่าที่หน้าตั้งค่าแสดง เป็นค่าที่ settingsFileSave เขียนลงการ์ด
  //      และเป็นค่าที่ pushSettingsToController ส่งกลับไป
  //      แล้วค่า 65535 จะถูกคอนโทรลเลอร์อ่านว่า "ไม่มีอยู่" เป้าหมายจริงบนการ์ดจึงถูกละเลยเงียบๆ
  //      ในการส่งครั้งที่ควรจะพามันไปพอดี ซึ่งเป็นกรณีเฟิร์มแวร์คนละรุ่นที่ค่าบอกสถานะนี้มีไว้เพื่อมันโดยตรง
  gLast.maxTravelSec = s.maxTravel ? s.maxTravel : gLast.maxTravelSec;
  gLast.liftSec = s.lift ? s.lift : gLast.liftSec;
  gLast.lowerSec = s.lower ? s.lower : gLast.lowerSec;
  gLast.holdSec = s.hold ? s.hold : gLast.holdSec;
  gLast.restSec = s.rest ? s.rest : gLast.restSec;
  gLast.targetReps = s.target == LINK_TARGET_ABSENT ? gLast.targetReps : s.target;
  uiSetDeviceCode(s.code.c_str());
  gSettingsKnown = true;
  uiSetSettingsKnown(true);  // the controller's real values are on screen now

  // A card with no file seeds itself from the machine's current configuration,
  // so a blank card put into a configured machine does not blank it.
  // [TH] การ์ดที่ยังไม่มีไฟล์จะถูกสร้างไฟล์จากค่าที่เครื่องใช้อยู่จริง
  //      การ์ดเปล่าที่เสียบเข้าเครื่องที่ตั้งค่าไว้แล้วจึงไม่ทำให้ค่าหาย
  if (!gReconciled) {
    gReconciled = true;
    if (storeCardPresent() && !settingsFilePresent()) {
      Serial.printf("settings: seeding the card from the controller: %s\n",
                    settingsFileSave(gLast) ? "written" : "write failed");
    }
  }
}

// One row per finished session, arriving once from the controller — see the
// done message in link-protocol.md.
// [TH] หนึ่งแถวต่อหนึ่งรอบที่จบ ส่งมาครั้งเดียวจากคอนโทรลเลอร์
static void onLinkDone(const LinkDone& d) {
  SessionRow r;
  r.epoch = d.epoch;
  r.reps = d.reps;
  r.durationSec = d.elapsed;
  r.fault = d.fault;
  // The identity was copied when the patient was picked, so nothing here depends
  // on the roster still holding the same rows it held then.
  // [TH] ตัวตนของคนไข้ถูกคัดลอกไว้ตั้งแต่ตอนเลือก ตรงนี้จึงไม่ต้องพึ่งว่าทะเบียนยังมีแถวเดิมอยู่หรือไม่
  r.patientCode = gActiveCode.length() ? gActiveCode : String("-");
  r.patientName = gActiveName;
  bool saved = storeCardPresent() && sdlogWrite(r);
  if (!saved) sdlogRingPush(r);  // retried from loop() — ลองใหม่จากลูปหลัก
  Serial.printf("session done: %lus x%u fault=%d saved=%d\n", (unsigned long)r.durationSec,
                r.reps, r.fault ? 1 : 0, saved ? 1 : 0);
  uiShowFinish(r.patientName.c_str(), r.durationSec, r.reps, r.fault, saved);

  // Stop and target come from `done`, not from the running flag falling.
  //
  // This board cannot tell "the session ended" from "the link dropped and the
  // watchdog cleared the state", and a machine that says หยุดการทำงาน every time
  // a wire glitches is worse than one that says nothing. `done` is emitted
  // exactly once per session, is on the retransmit list, and carries the final
  // rep count — so the split below is decided from the numbers the controller
  // actually finished with, not from the last snapshot that happened to arrive.
  // [TH] เสียงหยุดและเสียงครบเป้าหมายมาจากข้อความ done ไม่ใช่จากสถานะกำลังทำงานที่ตกลง
  //      จอแยกไม่ออกระหว่าง "รอบจบแล้ว" กับ "ลิงก์ขาดจนตัวเฝ้าระวังล้างสถานะ"
  //      เครื่องที่พูดว่าหยุดการทำงานทุกครั้งที่สายกระตุก แย่กว่าเครื่องที่เงียบ
  //      ข้อความ done ถูกส่งครั้งเดียวต่อรอบ อยู่ในรายการส่งซ้ำ และพามาทั้งจำนวนครั้งสุดท้าย
  //      การแยกด้านล่างจึงตัดสินจากตัวเลขที่คอนโทรลเลอร์จบรอบมาจริง ไม่ใช่ภาพสถานะล่าสุดที่บังเอิญมาถึง
  //
  // uiTarget is the live target from the state stream. A target of 0 means "run
  // until STOP", so every end of such a session is a stop, never a target.
  // [TH] uiTarget คือค่าเป้าหมายสดจากกระแสข้อความสถานะ ค่า 0 แปลว่าทำไปจนกว่าจะกดหยุด
  //      รอบแบบนั้นจึงจบด้วยเสียงหยุดเสมอ ไม่มีทางเป็นเสียงครบเป้าหมาย
  //
  // Adding a fault cue later is one branch here: d.fault is already in hand.
  // [TH] ถ้าจะเพิ่มเสียงแจ้งความผิดพลาดภายหลัง ก็เพิ่มเงื่อนไขเดียวตรงนี้ เพราะ d.fault มีอยู่แล้ว
  if (uiTarget != 0 && d.reps >= uiTarget) audioPlay(Cue::Target);
  else audioPlay(Cue::Stop);

  // The session is over; the next start is a real edge again.
  // [TH] รอบจบแล้ว การเริ่มครั้งถัดไปจึงนับเป็นขอบขาขึ้นจริงอีกครั้ง
  gPrevRunning = 0;
}

// When the last accepted state frame landed. The seconds shown on screen are
// counted locally from it, so a run of rejected frames does not freeze the
// clock — see uiElapsedNow().
// [TH] เวลาที่ได้รับสถานะที่ผ่านการตรวจสอบล่าสุด วินาทีที่แสดงบนจอนับต่อเองจากจุดนี้
//      ช่วงที่ข้อความถูกปฏิเสธติดกันหลายครั้งนาฬิกาบนจอจึงไม่ค้าง ดู uiElapsedNow()
static uint32_t uiElapsedRxMs = 0;
// The controller reports running with a clock that does not advance in two
// states: the safe-lower recovery that precedes a start out of a latched fault
// (which is not a session and has no elapsed time of its own), and a latched
// FAULT, where the value is frozen at what it was when the fault hit. Counting
// locally through either produces a screen that climbs, gets snapped back by the
// next frame, and climbs again — 0-1-0-1-0-1-2-0... So the local clock is only
// allowed to run while the controller's own is seen to move.
// [TH] มีสองสถานะที่คอนโทรลเลอร์รายงานว่ากำลังทำงานแต่เวลาไม่เดิน
//      อย่างแรกคือช่วงลดขาเพื่อกู้สถานการณ์ก่อนเริ่มรอบจากสถานะผิดพลาดที่ค้างอยู่
//      ซึ่งไม่ใช่รอบการทำงานจึงไม่มีเวลาของตัวเอง อย่างที่สองคือสถานะผิดพลาดที่ค้างอยู่
//      ซึ่งค่าเวลาถูกแช่ไว้ที่ค่าตอนเกิดเหตุ
//      ถ้านับต่อเองในสองสถานะนี้ จอจะไต่ขึ้นแล้วถูกดึงกลับด้วยข้อความถัดไป แล้วไต่ใหม่
//      กลายเป็น 0-1-0-1-0-1-2-0... จึงให้นาฬิกาบนจอเดินได้เฉพาะตอนที่เห็นว่าเวลาฝั่งคอนโทรลเลอร์เดินจริง
static bool uiElapsedMoving = false;

static void onLinkState(const LinkState& s) {
  // Resync the local clock only when the controller has really diverged.
  //
  // The wire carries whole seconds, truncated: a frame reading 3 can arrive when
  // the true elapsed is 3.9, so a clock started from it runs up to a second
  // behind and the next frame drags it forward again. Snapping on every frame
  // makes that visible as a stall followed by a skip — 1-2-3, pause, 4-5. Inside
  // one second the local count is as accurate as the message, so it keeps its
  // own phase; a gap of 2 s or more is a real divergence (a long run of rejected
  // frames, or a session that restarted) and is taken from the controller.
  // [TH] ปรับเทียบนาฬิกาบนจอเฉพาะตอนที่ค่าจากคอนโทรลเลอร์ต่างไปจริงๆ
  //      ค่าที่ส่งมาเป็นวินาทีเต็มแบบตัดเศษทิ้ง ข้อความที่บอกว่า 3 อาจมาถึงตอนที่เวลาจริงคือ 3.9
  //      นาฬิกาที่เริ่มนับจากค่านั้นจึงช้าได้ถึงหนึ่งวินาที แล้วข้อความถัดไปก็ดึงกลับไปข้างหน้า
  //      ถ้าปรับตามทุกข้อความจะเห็นเป็นการค้างแล้วกระโดด คือ 1-2-3 หยุด แล้ว 4-5
  //      ภายในหนึ่งวินาที การนับบนจอแม่นเท่ากับตัวข้อความอยู่แล้ว จึงให้รักษาจังหวะของตัวเองไว้
  //      ส่วนที่ต่างกันตั้งแต่ 2 วินาทีขึ้นไปถือว่าต่างกันจริง
  //      (ข้อความถูกปฏิเสธติดกันนาน หรือรอบเริ่มใหม่) กรณีนั้นให้ยึดค่าจากคอนโทรลเลอร์
  uint32_t now = millis();
  uint32_t shown = (uiRunning && uiElapsedMoving && uiElapsedRxMs != 0)
                       ? uiElapsed + (uint32_t)((int32_t)(now - uiElapsedRxMs) / 1000)
                       : uiElapsed;
  // A value that changed proves the controller's clock is running; one that has
  // not changed for over 1.5 s says it is held, and holding is a real state here
  // rather than a gap in the link — the link's own gaps are covered by the 2.5 s
  // watchdog above.
  // [TH] ค่าที่เปลี่ยนไปเป็นหลักฐานว่านาฬิกาฝั่งคอนโทรลเลอร์เดินอยู่
  //      ค่าที่ไม่เปลี่ยนเกิน 1.5 วินาทีแปลว่าเวลาถูกแช่ไว้ ซึ่งเป็นสถานะจริงของเครื่อง
  //      ไม่ใช่ช่วงที่ลิงก์ขาด เพราะช่วงที่ลิงก์ขาดมีตัวเฝ้าระวัง 2.5 วินาทีด้านบนดูแลอยู่แล้ว
  if (s.elapsed != uiElapsed) uiElapsedMoving = true;
  else if (uiElapsedRxMs != 0 && (int32_t)(now - uiElapsedRxMs) > 1500) uiElapsedMoving = false;
  bool diverged = !s.running || !uiRunning || uiElapsedRxMs == 0 || !uiElapsedMoving ||
                  s.elapsed > shown + 1 || shown > s.elapsed + 1;
  if (diverged) {
    uiElapsed = s.elapsed;
    uiElapsedRxMs = now;
  }
  uiRunning = s.running;
  uiPhase = (Phase)s.phase;
  uiReps = s.reps;
  uiTarget = s.target;
  uiWifi = s.wifi;
  uiServer = s.server;

  // Start has no one-shot message, so it must edge-detect, and both ways that
  // can go wrong are real.
  //
  // gLinkUp: a transition seen while the link is considered down is a reporting
  // artifact, not a session. Without this, a glitching jumper wire makes the
  // machine announce a start every time the watchdog clears and refills the
  // state. Note gLinkUp is recomputed in loop() AFTER gLink.poll() has already
  // delivered this callback, so it still holds the previous iteration's value —
  // which is exactly what is wanted here: on the first frame after a link
  // recovery it is still false, and the spurious start is suppressed. Do not
  // "fix" that staleness; moving the recompute earlier defeats this guard.
  //
  // gPrevRunning < 0: the first frame after boot only seeds the value.
  // [TH] สถานะเริ่มไม่มีข้อความส่งครั้งเดียวรองรับ จึงต้องจับขอบเอง และพลาดได้จริงสองทาง
  //      gLinkUp: การเปลี่ยนสถานะที่เห็นตอนลิงก์ถือว่าขาด เป็นผลจากการรายงาน ไม่ใช่รอบการทำงานจริง
  //      ถ้าไม่มีเงื่อนไขนี้ สายจัมเปอร์ที่สัญญาณกระตุกจะทำให้เครื่องประกาศเริ่มทุกครั้งที่ตัวเฝ้าระวังล้างและเติมสถานะใหม่
  //      สังเกตว่า gLinkUp ถูกคำนวณใหม่ในลูปหลัง gLink.poll() ส่งข้อความนี้มาแล้ว
  //      ค่าที่เห็นตรงนี้จึงเป็นค่าของรอบก่อนหน้า ซึ่งเป็นสิ่งที่ต้องการพอดี
  //      เพราะข้อความแรกหลังลิงก์กลับมาจะยังเห็นเป็นเท็จ และเสียงเริ่มที่ไม่ควรดังจะถูกระงับ
  //      ห้ามแก้ให้คำนวณเร็วขึ้น เพราะจะทำให้เงื่อนไขนี้ใช้ไม่ได้
  //      gPrevRunning < 0: ข้อความแรกหลังบูตใช้ตั้งค่าเริ่มต้นเท่านั้น
  if (gPrevRunning >= 0 && gLinkUp && s.running && gPrevRunning == 0) audioPlay(Cue::Start);
  gPrevRunning = s.running ? 1 : 0;
}

// The controller's seconds, carried forward locally between frames.
//
// The link drops a real share of its frames — the checksum rejects roughly half
// of them on the bench wiring, and a run of consecutive rejects leaves the
// screen with nothing new for a second or two, which reads as a clock that
// stalls and then skips (1-2-3 ... 5). The controller remains the authority:
// every accepted frame resets both the value and its timestamp, so this only
// fills the gaps and can never drift more than the gap itself.
//
// Free-running is bounded by the link watchdog above: past 2.5 s of silence
// gLinkUp goes false and the screen stops presenting the state as live, so this
// cannot keep counting against a machine that has actually stopped.
// [TH] วินาทีของฝั่งคอนโทรลเลอร์ นับต่อเองบนจอในช่วงระหว่างข้อความ
//      สายลิงก์ทำข้อความหายจริงพอสมควร ค่าตรวจสอบปฏิเสธราวครึ่งหนึ่งบนสายที่ใช้ทดสอบ
//      ถ้าถูกปฏิเสธติดกันหลายครั้ง จอจะไม่มีค่าใหม่ราวหนึ่งถึงสองวินาที
//      ซึ่งเห็นเป็นนาฬิกาที่ค้างแล้วกระโดด (1-2-3 ... 5)
//      ฝั่งคอนโทรลเลอร์ยังเป็นเจ้าของค่าจริง ทุกข้อความที่ผ่านการตรวจสอบจะรีเซ็ตทั้งค่าและเวลาที่รับ
//      ตรงนี้แค่เติมช่วงที่ขาด จึงคลาดเคลื่อนได้ไม่เกินความยาวของช่วงที่ขาดนั้น
//      การนับต่อเองถูกจำกัดด้วยตัวเฝ้าระวังลิงก์ด้านบน ถ้าเงียบเกิน 2.5 วินาที gLinkUp จะเป็นเท็จ
//      จอจะเลิกแสดงว่าสถานะยังสด จึงนับต่อไปเรื่อยๆ กับเครื่องที่หยุดไปแล้วไม่ได้
static uint32_t uiElapsedNow(uint32_t now) {
  if (!uiRunning || !uiElapsedMoving || uiElapsedRxMs == 0) return uiElapsed;
  return uiElapsed + (uint32_t)((int32_t)(now - uiElapsedRxMs) / 1000);
}

// The operator's -/+ taps on the main screen go straight to the controller, live
// — not through settings, which is deferred to the end of the session.
// [TH] การกดปุ่ม -/+ ที่หน้าหลักส่งตรงไปคอนโทรลเลอร์ทันที ไม่ผ่านเส้นทางค่าตั้งค่าที่รอจนจบรอบ
static void onTargetChanged(uint16_t reps) { gLink.sendTarget(reps); }

void setup() {
  // [TH] เริ่มพอร์ตดีบัก, ตั้งลิงก์ UART ไปคอนโทรลเลอร์, เริ่ม UI แล้วทักทายขอค่าตั้งค่า/สถานะ
#if UI_TEST_HOOK
  // A screenshot pushes about 200 KB through this port in one go. The stock 256
  // byte transmit buffer means the sender blocks almost continuously; a larger
  // one lets whole rows queue and keeps the stalls short.
  // [TH] การจับภาพหน้าจอส่งข้อมูลราว 200 กิโลไบต์รวดเดียว บัฟเฟอร์ส่งขนาด 256 ไบต์ตามค่าเริ่มต้น
  //      ทำให้ฝั่งส่งค้างแทบตลอดเวลา ถ้าขยายให้ใหญ่ขึ้นจะพักข้อมูลได้ทีละแถวและค้างสั้นลง
  Serial.setTxBufferSize(4096);
#endif
  Serial.begin(115200);
  delay(200);  // UART0 settles after begin(); without this the first line is lost — รอ UART0 นิ่ง ไม่งั้นบรรทัดแรกหาย
  // Why the chip started. A machine that reboots on its own is otherwise a
  // guessing game between a bad cable, a sagging supply and a firmware fault,
  // and the three have completely different fixes: 1 is a real power-on, 3 is
  // the reset line, 4 is the task watchdog, 5 a panic, 9 the brownout detector.
  // [TH] บอกสาเหตุที่ชิปเริ่มทำงานใหม่ ถ้าเครื่องรีบูตเองโดยไม่มีข้อมูลนี้ จะเดาไม่ออกว่าเป็นเพราะ
  //      สายไม่ดี ไฟตก หรือเฟิร์มแวร์ผิดพลาด ซึ่งแก้คนละทางกันหมด
  //      1 คือเปิดเครื่องปกติ 3 คือกดรีเซ็ต 4 คือ watchdog 5 คือโปรแกรมพัง 9 คือไฟตก
  Serial.printf("pt-leg-display boot (reset reason %d)\n", (int)esp_reset_reason());
  // 19200, matching the controller. The link needs under 500 B/s, and a wider
  // bit is sampled further from its edges — the cheapest noise margin there is
  // on jumper wire. Both ends must match; flash them together.
  // [TH] ใช้ 19200 ให้ตรงกับฝั่งคอนโทรลเลอร์ สายนี้ใช้ไม่ถึง 500 ไบต์ต่อวินาที
  //      บิตที่กว้างขึ้นจะถูกอ่านที่จุดห่างจากขอบมากขึ้น เป็นการเพิ่มความทนต่อสัญญาณรบกวนที่ถูกที่สุดบนสายจัมเปอร์
  //      ทั้งสองฝั่งต้องตั้งค่าเท่ากัน ต้องแฟลชพร้อมกัน
  Serial2.begin(19200, SERIAL_8N1, 22, 27);  // UART to controller (CYD free pins) — สาย UART ไปคอนโทรลเลอร์ (ขาว่างของ CYD)
  // Hold RX high while the controller is idle. Measured on this board: a
  // floating GPIO 22 prefixed every incoming frame with 25-32 bytes of 0xFF,
  // which failed the JSON parse and left this screen showing idle through a
  // whole running session. link.cpp now trims the junk; this stops most of it
  // being generated. An external 10k to 3V3 is the stronger fix.
  // [TH] ดึงขารับให้เป็นลอจิกสูงตอนที่คอนโทรลเลอร์ไม่ได้ส่ง
  //      วัดได้บนบอร์ดนี้: ขา 22 ที่ลอยทำให้ทุกข้อความมีไบต์ 0xFF นำหน้า 25-32 ตัว
  //      ซึ่งแปลง JSON ไม่ผ่าน จอจึงค้างที่หน้าว่างตลอดรอบที่เครื่องทำงานอยู่
  //      ตอนนี้ link.cpp ตัดขยะให้แล้ว ส่วนนี้ช่วยไม่ให้เกิดขยะตั้งแต่ต้น
  //      ถ้ายังมีอยู่ ให้ใส่ตัวต้านทาน 10 กิโลโอห์มไป 3.3V จะแน่นอนกว่า
  gpio_set_pull_mode(GPIO_NUM_22, GPIO_PULLUP_ONLY);
  uiBegin();
  // A missing or unreadable card is not an error worth stopping for: the roster
  // is empty, sessions ring-buffer, and the machine still runs.
  // [TH] ไม่มีการ์ดหรืออ่านการ์ดไม่ได้ ไม่ถือเป็นข้อผิดพลาดที่ต้องหยุดเครื่อง
  //      ทะเบียนจะว่าง ผลรอบจะพักไว้ในหน่วยความจำ และเครื่องยังทำงานได้ตามปกติ
  Serial.printf("sd card: %s, patients=%d\n", storeBegin() ? "mounted" : "absent",
                storePatientCount());
  gCardWasPresent = storeCardPresent();  // boot state, so the first tick is not read as an insertion
  sdlogEnsureHeader();
  // After the card, because a cue reads its clip from it. Safe with no card:
  // the buffer and the DAC channel do not depend on one.
  // [TH] อยู่หลังการเปิดการ์ด เพราะเสียงแต่ละรายการอ่านคลิปจากการ์ด
  //      เรียกได้แม้ไม่มีการ์ด เพราะบัฟเฟอร์และช่องสัญญาณ DAC ไม่ได้ขึ้นกับการ์ด
  audioBegin();
  // Read before the link comes up: the controller's reply is what triggers
  // reconciliation, and this decides which side of it wins.
  // [TH] อ่านไฟล์ก่อนที่ลิงก์จะพร้อม เพราะการตอบกลับของคอนโทรลเลอร์คือจุดที่เริ่มเทียบค่า
  //      และค่าที่อ่านได้ตรงนี้เป็นตัวตัดสินว่าจะยึดฝั่งใด
  gCardSettings = settingsFileLoad(gLast);
  Serial.printf("settings source: %s\n", gCardSettings ? "sd card" : "controller nvs");
  audioSetEnabled(gLast.soundOn);
  if (gCardSettings) uiSetDeviceCode(gLast.deviceCode.c_str());
  uitestBegin();  // no-op unless UI_TEST_HOOK — ไม่ทำอะไรเลยถ้าไม่ได้เปิดโหมดทดสอบ
  uiSetCallbacks(onOpenSettings, onOpenData);
  uiSetDataCallbacks(dataCount, dataGet, dataAdd, dataSetActive);
  uiSetSettingsSaved(onSaveFromUi);
  uiSetTargetChanged(onTargetChanged);
  Serial.printf("controller link on %s\n", LINK_OVER_USB ? "USB (bench)" : "Serial2");
#if LINK_OVER_USB
  gLink.begin(&Serial);
#else
  gLink.begin(&Serial2);
#endif
  gLink.onState = onLinkState;
  gLink.onSettingsReply = onLinkSettingsReply;
  gLink.onDone = onLinkDone;
  gLink.sendHello();  // ask controller for current settings + state
  // Last, so hearing it means the whole init path finished — not merely that the
  // DAC came up. It is the cheapest end-to-end self-test this board has.
  // [TH] วางไว้ท้ายสุด การได้ยินเสียงนี้จึงแปลว่าขั้นตอนเริ่มระบบผ่านครบ
  //      ไม่ใช่แค่ว่า DAC ทำงาน เป็นการทดสอบตั้งแต่ต้นจนจบที่ถูกที่สุดของบอร์ดนี้
  audioPlay(Cue::Boot);
}

#if LOOP_DEBUG
// Counters for the once-a-second summary. A "frame" here is any message the link
// accepted, spotted by lastRxMs() moving — the display has no per-message hook,
// and for this question the message type does not matter, only that bytes made
// it through intact.
// [TH] ตัวนับสำหรับสรุปทุกหนึ่งวินาที นับ "ข้อความ" จากการที่ค่าเวลารับล่าสุดขยับ
//      เพราะฝั่งจอไม่มีจุดต่อรายข้อความ และคำถามนี้ไม่สนใจชนิดข้อความ
//      สนใจแค่ว่ามีข้อมูลผ่านเข้ามาครบถ้วนหรือไม่
static uint32_t dbgPrevRx = 0;       // last lastRxMs() seen — ค่าเวลารับล่าสุดที่เห็นรอบก่อน
static uint32_t dbgLastFrameMs = 0;  // when the previous frame landed — เวลาที่ข้อความก่อนหน้ามาถึง
static uint16_t dbgFrames = 0;       // frames this window — จำนวนข้อความในช่วงนี้
static uint32_t dbgMaxGapMs = 0;     // longest quiet stretch — ช่วงเงียบที่ยาวที่สุด
static uint32_t dbgMaxIterMs = 0;    // slowest iteration this window — รอบที่ช้าที่สุด
static uint32_t dbgWindowMs = 0;     // start of this window — เวลาเริ่มช่วง
#endif

void loop() {
#if LOOP_DEBUG
  uint32_t dbgT0 = millis();
  uint32_t dbgUiMs = 0, dbgPollMs = 0, dbgDrawMs = 0, dbgSdMs = 0, dbgAudioMs = 0;
#endif
  uint32_t now = millis();
  uiLoop();
#if LOOP_DEBUG
  dbgUiMs = millis() - dbgT0;
  uint32_t dbgTAudio = millis();
#endif
  // Timed separately from uiLoop. Folded in with it, a 4 KB card read during a
  // cue's load phase was billed to LVGL — and this instrumentation exists to
  // find loop stalls, so a mislabelled millisecond sends the next investigation
  // to the wrong file.
  // [TH] จับเวลาแยกจาก uiLoop ถ้ารวมไว้ด้วยกัน การอ่านการ์ด 4 กิโลไบต์ตอนโหลดเสียง
  //      จะถูกนับเป็นเวลาของ LVGL และเครื่องมือนี้มีไว้หาจุดที่ลูปค้าง
  //      มิลลิวินาทีที่ติดป้ายผิดจึงพาการตรวจสอบครั้งถัดไปไปผิดไฟล์
  audioLoop();  // pumps a loading or playing cue — เดินหน้าเสียงที่กำลังโหลดหรือกำลังเล่น
#if LOOP_DEBUG
  dbgAudioMs = millis() - dbgTAudio;
  uint32_t dbgTPoll = millis();
#endif
  gLink.poll();
#if LOOP_DEBUG
  dbgPollMs = millis() - dbgTPoll;
  // A frame is only counted once: lastRxMs() holds its arrival time, so a value
  // that has not moved means nothing new was parsed this pass.
  // [TH] นับข้อความละครั้งเดียว ค่าเวลารับล่าสุดคือเวลาที่ข้อความมาถึง
  //      ถ้าค่ายังเท่าเดิมแปลว่ารอบนี้ไม่มีข้อความใหม่ที่อ่านได้
  uint32_t dbgRx = gLink.lastRxMs();
  if (dbgRx != dbgPrevRx) {
    if (dbgLastFrameMs != 0) {
      uint32_t gap = dbgRx - dbgLastFrameMs;
      if (gap > dbgMaxGapMs) dbgMaxGapMs = gap;
    }
    dbgLastFrameMs = dbgRx;
    dbgPrevRx = dbgRx;
    dbgFrames++;
  }
#endif

  // The controller sends state at ~4 Hz. Nothing for 2.5 s means the link is
  // gone and the cached state is stale, so the UI must stop presenting it as
  // live. Mirrors the controller's own 2 s UART watchdog.
  // [TH] คอนโทรลเลอร์ส่งสถานะราว 4 ครั้ง/วินาที ถ้าเงียบเกิน 2.5 วินาที ถือว่าสายหลุด
  //      ค่าที่เก็บไว้ถือว่าเก่า จอต้องเลิกแสดงว่าสด สอดคล้องกับตัวเฝ้าระวัง 2 วินาทีฝั่งคอนโทรลเลอร์
  // Signed: gLink.poll() just above stamps lastRx with a fresh millis(), which can
  // be later than the `now` captured at the top of this iteration. Unsigned, that
  // subtraction is ~4.29e9 and the link reads as down at the exact moment a
  // message arrived. Only a flickering indicator here, unlike the controller's
  // copy of this compare, which safe-stops the session.
  // [TH] ใช้การเทียบแบบมีเครื่องหมาย เพราะ gLink.poll() ด้านบนบันทึกเวลาด้วย millis() ค่าใหม่
  //      ซึ่งอาจช้ากว่าค่า now ที่อ่านไว้ตั้งแต่ต้นรอบ
  //      ถ้าลบแบบไม่มีเครื่องหมายจะได้ราวสี่พันล้าน แล้วจะอ่านว่าสายหลุดในจังหวะที่เพิ่งมีข้อความเข้ามาพอดี
  //      ที่นี่แค่ไฟสถานะกะพริบ ต่างจากฝั่งคอนโทรลเลอร์ที่การเทียบแบบเดียวกันสั่งหยุดรอบการทำงาน
  uint32_t lastRx = gLink.lastRxMs();
  gLinkUp = lastRx != 0 && (int32_t)(now - lastRx) < 2500;

  static uint32_t lastUi = 0, lastPing = 0, lastHello = 0;
  if (now - lastUi >= 250) {  // [TH] รีเฟรชหน้าจอ ~4 ครั้ง/วินาที
    lastUi = now;
#if LOOP_DEBUG
    uint32_t dbgTDraw = millis();
#endif
    uiUpdateMain(uiRunning, uiPhase, uiElapsedNow(now), uiReps, uiTarget, uiWifi, uiServer, gLinkUp);
#if LOOP_DEBUG
    dbgDrawMs = millis() - dbgTDraw;
#endif
  }
  if (now - lastPing >= 1000) {  // keep the controller's UART watchdog fed — ส่ง ping เลี้ยงตัวเฝ้าระวังของคอนโทรลเลอร์
    lastPing = now;
    gLink.sendPing();
  }
  // Re-ask for settings if the controller was not listening at our boot (it may
  // power up later, or the cable may be reconnected).
  // [TH] ถามค่าตั้งค่าซ้ำ เผื่อคอนโทรลเลอร์ยังไม่ฟังตอนจอเปิดเครื่อง (อาจเปิดทีหลังหรือเสียบสายใหม่)
  if (!gSettingsKnown && now - lastHello >= 3000) {
    lastHello = now;
    gLink.sendHello();
  }

  // Sessions that could not be written wait in RAM. Retry slowly, and try to
  // mount again first — the usual reason for a failed write is a card that was
  // not in the slot, and the fix is someone pushing one in.
  // [TH] ผลรอบที่เขียนไม่สำเร็จค้างอยู่ในหน่วยความจำ ลองใหม่เป็นระยะ และลองเมานต์การ์ดก่อน
  //      เพราะสาเหตุที่พบบ่อยคือยังไม่ได้เสียบการ์ด พอเสียบแล้วก็เขียนได้
  // Also runs with an empty ring while no card is mounted, so a card pushed in
  // after boot shows up in the data menu without a reboot.
  // [TH] ทำงานแม้ไม่มีแถวค้าง ถ้ายังไม่มีการ์ด เพื่อให้การ์ดที่เพิ่งเสียบใช้ได้ทันทีโดยไม่ต้องรีบูต
  static uint32_t lastSd = 0;
  if (now - lastSd > 5000) {
    lastSd = now;
#if LOOP_DEBUG
    uint32_t dbgTSd = millis();
#endif
    // Unconditional, where this used to run only with rows waiting or no card
    // mounted. With a card in the slot and nothing to write, the whole block was
    // skipped, so a card swapped on an idle machine was never noticed: the
    // display kept serving the old card's settings and roster, and the next save
    // wrote them onto the new card. Probing costs one directory open every five
    // seconds and is what makes the absent-to-present edge below observable.
    // [TH] ทำงานทุกครั้ง จากเดิมที่ทำเฉพาะตอนมีแถวค้างหรือไม่มีการ์ดเมานต์อยู่
    //      ถ้ามีการ์ดอยู่และไม่มีอะไรต้องเขียน บล็อกนี้จะถูกข้ามทั้งหมด
    //      การสลับการ์ดตอนเครื่องว่างจึงไม่ถูกตรวจพบ จอยังใช้ค่าและทะเบียนของการ์ดใบเก่า
    //      และการบันทึกครั้งถัดไปจะเขียนทับการ์ดใบใหม่ การตรวจนี้เสียแค่การเปิดโฟลเดอร์ทุกห้าวินาที
    //      และเป็นสิ่งที่ทำให้เห็นจังหวะเปลี่ยนจาก "ไม่มี" เป็น "มี" ด้านล่าง
    storeProbe();
    if (!storeCardPresent()) storeBegin();
    int before = sdlogRingDepth();
    sdlogFlushRing();
    // Rows leaving the ring is the one moment worth a line: it is the proof that
    // a session logged offline actually reached the card once it was pushed back
    // in, and there is no other way to see it happen.
    // [TH] จังหวะที่แถวค้างถูกเขียนลงการ์ดสำเร็จคือจุดเดียวที่ควรพิมพ์บอก
    //      เพราะเป็นหลักฐานว่าผลรอบที่บันทึกไว้ตอนไม่มีการ์ดได้ลงการ์ดจริงแล้ว
    if (sdlogRingDepth() < before)
      Serial.printf("sd log: flushed %d row(s), %d waiting\n", before - sdlogRingDepth(),
                    sdlogRingDepth());
    // Rows waiting and none drained: the mount is a leftover from a card that
    // has since been pulled. Throw it away; the next tick writes them for real.
    // [TH] มีแถวค้างแต่เขียนไม่ออกเลย แปลว่าการเมานต์ค้างจากการ์ดที่ถูกดึงออกไปแล้ว
    //      ทิ้งแล้วเมานต์ใหม่ รอบถัดไปจะเขียนได้จริง
    if (before > 0 && sdlogRingDepth() == before) storeRemount();

    // A card that goes from absent to present is a card someone just pushed in,
    // and it need not be the one read at boot: what settingsfile remembers would
    // then describe a card that has left the building, so the settings screen has
    // to read the new one. Keyed on the transition rather than on remounting,
    // because storeRemount() also runs when a card that never moved simply
    // refuses writes — a full card is not a swapped card.
    // Known gap: a swap completed entirely between two probes — card out and a
    // new one in inside the same five seconds — still shows no absent state.
    // [TH] การ์ดที่เปลี่ยนจาก "ไม่มี" เป็น "มี" คือการ์ดที่เพิ่งถูกเสียบ และอาจไม่ใช่ใบเดิมที่อ่านตอนบูต
    //      สิ่งที่โมดูลจำไว้จะกลายเป็นข้อมูลของการ์ดที่ถอดออกไปแล้ว หน้าตั้งค่าจึงต้องอ่านใบใหม่
    //      ใช้จังหวะเปลี่ยนสถานะเป็นตัวตัดสิน ไม่ใช้การเมานต์ใหม่ เพราะการ์ดที่ไม่ได้ถูกถอดแต่เขียนไม่ได้
    //      ก็ทำให้เมานต์ใหม่เหมือนกัน การ์ดเต็มไม่ใช่การ์ดที่ถูกสลับ
    //      ข้อจำกัดที่รู้อยู่: ถ้าสลับการ์ดจบภายในช่วงระหว่างสองรอบตรวจ จะไม่เห็นสถานะ "ไม่มีการ์ด" และจับไม่ได้
    bool cardNow = storeCardPresent();
    if (cardNow && !gCardWasPresent) {
      settingsFileForget();
      // The chosen patient goes with the old card. Whoever was picked was picked
      // off a roster that is no longer in the slot, and the next walk-up patient
      // who presses START without picking anybody would otherwise have their
      // session filed under that name on a card it does not even belong to.
      // [TH] คนไข้ที่เลือกไว้ไปพร้อมกับการ์ดใบเก่า
      //      คนที่ถูกเลือกมาจากทะเบียนของการ์ดที่ไม่ได้อยู่ในช่องแล้ว
      //      ถ้าไม่ล้าง คนไข้คนถัดไปที่กดเริ่มโดยไม่ได้เลือกใคร จะถูกบันทึกในชื่อนั้น
      //      บนการ์ดที่ชื่อนั้นไม่ได้อยู่ด้วยซ้ำ
      gActiveCode = "";
      gActiveName = "";
    }
    gCardWasPresent = cardNow;
#if LOOP_DEBUG
    dbgSdMs = millis() - dbgTSd;
#endif
  }
  uitestPoll();  // no-op unless UI_TEST_HOOK — ไม่ทำอะไรเลยถ้าไม่ได้เปิดโหมดทดสอบ
#if LOOP_DEBUG
  // Printed before the delay, so the number is the work this pass did rather
  // than the pacing that follows it.
  // [TH] พิมพ์ก่อนหน่วงเวลา ตัวเลขจะได้เป็นเวลาที่ใช้ทำงานจริงของรอบนี้ ไม่รวมการหน่วง
  uint32_t dbgIter = millis() - dbgT0;
  if (dbgIter > dbgMaxIterMs) dbgMaxIterMs = dbgIter;
  if (dbgIter >= LOOP_DEBUG_SLOW_MS)
    Serial.printf("[loop %lu] SLOW iter=%lums ui=%lu audio=%lu poll=%lu draw=%lu sd=%lu\n",
                  (unsigned long)millis(), (unsigned long)dbgIter, (unsigned long)dbgUiMs,
                  (unsigned long)dbgAudioMs, (unsigned long)dbgPollMs, (unsigned long)dbgDrawMs,
                  (unsigned long)dbgSdMs);
  // The summary is what answers the question. frames≈4/s with a small gap means
  // the link is healthy and the stutter is in the UI; frames far below 4 or a gap
  // in the seconds means the data never arrived and the UI is only reporting it.
  // [TH] บรรทัดสรุปคือคำตอบของคำถามนี้ ถ้ารับได้ราว 4 ครั้ง/วินาที และเว้นช่วงสั้น
  //      แปลว่าลิงก์ปกติ อาการกระตุกอยู่ที่การแสดงผล
  //      ถ้ารับได้น้อยกว่านั้นมาก หรือเว้นช่วงเป็นวินาที แปลว่าข้อมูลไม่มาถึง จอแค่รายงานตามที่ได้รับ
  if (millis() - dbgWindowMs >= 1000) {
    dbgWindowMs = millis();
    Serial.printf(
        "[loop %lu] frames=%u maxgap=%lums maxiter=%lums link=%d elapsed=%lus lines=%lu bad=%lu "
        "unknown=%lu trimmed=%lu crcbad=%lu nocrc=%lu dupes=%lu retries=%lu txdrop=%lu\n",
        (unsigned long)dbgWindowMs, (unsigned)dbgFrames, (unsigned long)dbgMaxGapMs,
        (unsigned long)dbgMaxIterMs, gLinkUp ? 1 : 0, (unsigned long)uiElapsed,
        (unsigned long)gLink.rxLines, (unsigned long)gLink.rxBad, (unsigned long)gLink.rxUnknown,
        (unsigned long)gLink.rxTrimmed, (unsigned long)gLink.rxCrcBad,
        (unsigned long)gLink.rxNoCrc, (unsigned long)gLink.rxDupes,
        (unsigned long)gLink.txRetries, (unsigned long)gLink.txDropped);
    // Printed as hex alongside the text: a line that looks plausible on screen
    // can still carry a flipped bit, and only the byte values show it.
    // [TH] พิมพ์ทั้งข้อความและค่าฐานสิบหก บรรทัดที่ดูเหมือนปกติอาจมีบิตเพี้ยนอยู่
    //      ซึ่งเห็นได้จากค่าไบต์เท่านั้น
    if (gLink.rxLastBad.length()) {
      Serial.printf("[loop %lu] badline text=[%s]\n", (unsigned long)dbgWindowMs,
                    gLink.rxLastBad.c_str());
      Serial.printf("[loop %lu] badline hex=", (unsigned long)dbgWindowMs);
      for (size_t i = 0; i < gLink.rxLastBad.length() && i < 48; i++)
        Serial.printf("%02X ", (uint8_t)gLink.rxLastBad[i]);
      Serial.println();
      gLink.rxLastBad = "";
    }
    dbgFrames = 0;
    dbgMaxGapMs = 0;
    dbgMaxIterMs = 0;
  }
#endif
  delay(2);
}


