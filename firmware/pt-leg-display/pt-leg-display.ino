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

#include "ui.h"
#include "link.h"
#include "session.h"   // Phase enum for uiUpdateMain — ใช้ enum Phase
#include "settings.h"  // DeviceSettings cached from the controller — เก็บค่าตั้งค่าที่คอนโทรลเลอร์ส่งมา
#include "store.h"     // patient roster on the SD card — ทะเบียนคนไข้บนการ์ด SD
#include "sdlog.h"     // session rows written to the SD card — บันทึกผลแต่ละรอบลงการ์ด
#include "settingsfile.h"  // /settings.json on the card — ค่าตั้งค่าบนการ์ด
#include "uitest.h"        // bench-only serial control of the UI — โหมดทดสอบผ่านสาย

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
static DeviceSettings gLast = {"", "", "", "default", 8, 4, 4, 10, 10, 10};
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
}

static void onLinkState(const LinkState& s) {
  uiRunning = s.running;
  uiPhase = (Phase)s.phase;
  uiElapsed = s.elapsed;
  uiReps = s.reps;
  uiTarget = s.target;
  uiWifi = s.wifi;
  uiServer = s.server;
}

// The operator's -/+ taps on the main screen go straight to the controller, live
// — not through settings, which is deferred to the end of the session.
// [TH] การกดปุ่ม -/+ ที่หน้าหลักส่งตรงไปคอนโทรลเลอร์ทันที ไม่ผ่านเส้นทางค่าตั้งค่าที่รอจนจบรอบ
static void onTargetChanged(uint16_t reps) { gLink.sendTarget(reps); }

// The confirm screen's own -/+ taps, mirrored to the controller the same
// immediate way as onTargetChanged just above — see ui.cpp's confirmStep().
// [TH] ปุ่มปรับค่าบนหน้ายืนยัน ส่งตรงไปคอนโทรลเลอร์ทันทีแบบเดียวกับ onTargetChanged ด้านบน
//      ดู confirmStep() ใน ui.cpp
static void onConfirmSets(uint16_t reps) { gLink.sendSets(reps); }
// ตกลง/ยกเลิก บนหน้ายืนยัน — true คือกดตกลง (เริ่มรอบ), false คือยกเลิก (กลับไปว่าง)
static void onConfirmDecision(bool ok) { gLink.sendConfirm(ok); }

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
  Serial2.begin(115200, SERIAL_8N1, 22, 27);  // UART to controller (CYD free pins) — สาย UART ไปคอนโทรลเลอร์ (ขาว่างของ CYD)
  uiBegin();
  // A missing or unreadable card is not an error worth stopping for: the roster
  // is empty, sessions ring-buffer, and the machine still runs.
  // [TH] ไม่มีการ์ดหรืออ่านการ์ดไม่ได้ ไม่ถือเป็นข้อผิดพลาดที่ต้องหยุดเครื่อง
  //      ทะเบียนจะว่าง ผลรอบจะพักไว้ในหน่วยความจำ และเครื่องยังทำงานได้ตามปกติ
  Serial.printf("sd card: %s, patients=%d\n", storeBegin() ? "mounted" : "absent",
                storePatientCount());
  gCardWasPresent = storeCardPresent();  // boot state, so the first tick is not read as an insertion
  sdlogEnsureHeader();
  // Read before the link comes up: the controller's reply is what triggers
  // reconciliation, and this decides which side of it wins.
  // [TH] อ่านไฟล์ก่อนที่ลิงก์จะพร้อม เพราะการตอบกลับของคอนโทรลเลอร์คือจุดที่เริ่มเทียบค่า
  //      และค่าที่อ่านได้ตรงนี้เป็นตัวตัดสินว่าจะยึดฝั่งใด
  gCardSettings = settingsFileLoad(gLast);
  Serial.printf("settings source: %s\n", gCardSettings ? "sd card" : "controller nvs");
  if (gCardSettings) uiSetDeviceCode(gLast.deviceCode.c_str());
  uitestBegin();  // no-op unless UI_TEST_HOOK — ไม่ทำอะไรเลยถ้าไม่ได้เปิดโหมดทดสอบ
  uiSetCallbacks(onOpenSettings, onOpenData);
  uiSetDataCallbacks(dataCount, dataGet, dataAdd, dataSetActive);
  uiSetSettingsSaved(onSaveFromUi);
  uiSetTargetChanged(onTargetChanged);
  uiSetConfirmCallbacks(onConfirmSets, onConfirmDecision);
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
}

void loop() {
  uint32_t now = millis();
  uiLoop();
  gLink.poll();

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
    uiUpdateMain(uiRunning, uiPhase, uiElapsed, uiReps, uiTarget, uiWifi, uiServer, gLinkUp);
    // Phase::Confirm is deliberately excluded from `running` on the
    // controller (session.h there), so uiUpdateMain's own running-based
    // screen switch above never shows this one — it has to be driven off the
    // phase directly, called here for as long as phase says Confirm AND the
    // link is up, and simply not called the moment either stops being true
    // (ui.cpp notices and leaves the screen on its own; see uiUpdateMain).
    // The gLinkUp half of the gate matters on its own: uiPhase only ever
    // changes inside onLinkState(), so a dead link freezes it at Confirm
    // forever, and without this check that would mean calling uiShowConfirm
    // every tick regardless — uiUpdateMain's own leave check, a few lines up,
    // would fire on the very same tick gLinkUp goes false and load scrMain,
    // only for this line to immediately reopen scrConfirm right after it,
    // since the screen no longer being active is exactly what uiShowConfirm
    // reads as "just entered Confirm". uiTarget already carries the
    // controller's pendingTargetReps while phase == Confirm (session.h's
    // target()), which is exactly what uiShowConfirm treats as the seed/echo.
    // [TH] สถานะรอยืนยันถูกกันออกจาก running โดยตั้งใจฝั่งคอนโทรลเลอร์ (session.h ที่นั่น) ตรรกะสลับ
    //      หน้าจอที่อิงกับ running ของ uiUpdateMain ด้านบนจึงไม่มีวันแสดงหน้านี้ ต้องขับด้วยสถานะโดยตรง
    //      เรียกตรงนี้ตราบใดที่ phase ยังเป็นรอยืนยันและลิงก์ยังเชื่อมอยู่ แล้วเลิกเรียกทันทีที่ข้อใดข้อหนึ่ง
    //      ไม่จริงแล้ว (ui.cpp จะสังเกตเห็นเองและออกจากหน้าจอนี้ให้ ดู uiUpdateMain) ครึ่งหนึ่งของเงื่อนไข
    //      gLinkUp มีความหมายในตัวเอง: uiPhase เปลี่ยนค่าได้แค่ใน onLinkState() เท่านั้น สายที่หลุดจึงทำให้
    //      ค่านี้ค้างที่รอยืนยันตลอดไป ถ้าไม่ตรวจตรงนี้ก็จะเรียก uiShowConfirm ทุกรอบต่อไปไม่ว่าลิงก์จะเป็นอย่างไร
    //      เงื่อนไขออกจากหน้าของ uiUpdateMain เองไม่กี่บรรทัดก่อนหน้านี้จะทำงานในรอบเดียวกับที่ gLinkUp
    //      เพิ่งเป็นเท็จพอดี แล้วโหลด scrMain แต่บรรทัดนี้จะเปิด scrConfirm กลับทันทีตามหลัง เพราะการที่หน้าจอ
    //      นี้ไม่ active อีกแล้วคือสิ่งที่ uiShowConfirm อ่านว่า "เพิ่งเข้าสถานะรอยืนยัน" พอดี uiTarget มีค่า
    //      pendingTargetReps ของคอนโทรลเลอร์อยู่แล้วตอน phase == รอยืนยัน (ดู target() ใน session.h)
    //      ซึ่งตรงกับสิ่งที่ uiShowConfirm ใช้เป็นค่าตั้งต้น/ค่าสะท้อนกลับพอดี
    if (uiPhase == Phase::Confirm && gLinkUp) uiShowConfirm(uiTarget, gLast, gLinkUp);
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
  }
  uitestPoll();  // no-op unless UI_TEST_HOOK — ไม่ทำอะไรเลยถ้าไม่ได้เปิดโหมดทดสอบ
  delay(2);
}


