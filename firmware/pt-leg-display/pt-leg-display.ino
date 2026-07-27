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
#define LINK_OVER_USB 1
#endif

#include "ui.h"
#include "link.h"
#include "session.h"   // Phase enum for uiUpdateMain — ใช้ enum Phase
#include "settings.h"  // DeviceSettings cached from the controller — เก็บค่าตั้งค่าที่คอนโทรลเลอร์ส่งมา
#include "store.h"     // patient roster on the SD card — ทะเบียนคนไข้บนการ์ด SD
#include "sdlog.h"     // session rows written to the SD card — บันทึกผลแต่ละรอบลงการ์ด

Link gLink;  // ลิงก์ UART ไปยังคอนโทรลเลอร์
static bool uiRunning = false;
static Phase uiPhase = Phase::Idle;
static uint32_t uiElapsed = 0;
static uint16_t uiReps = 0;
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
static DeviceSettings gLast = {"", "", "", "default", 8, 10, 10};
static bool gSettingsKnown = false;
static bool gLinkUp = false;

// Which patient the next session belongs to. -1 until someone picks one, and it
// stays -1 all day if nobody does: the machine must run for a walk-up patient
// with no roster at all, the row just logs without a name.
// [TH] คนไข้ที่รอบถัดไปจะถูกบันทึกให้ -1 คือยังไม่ได้เลือก และปล่อยเป็น -1 ทั้งวันก็ได้
//      เครื่องต้องใช้งานได้แม้ไม่มีใครลงทะเบียน แค่บันทึกโดยไม่มีชื่อ
static int gActivePatient = -1;

static void onOpenSettings() { Serial.println("touch: settings"); uiOpenSettings(gLast); }
static void onOpenData() { Serial.println("touch: data"); uiOpenData(); }

// -1 tells the UI "no card", which reads differently from an empty roster.
// [TH] คืน -1 เพื่อบอก UI ว่าไม่มีการ์ด ซึ่งความหมายต่างจากทะเบียนที่ว่างเปล่า
static int dataCount() { return storeCardPresent() ? storePatientCount() : -1; }
static const Patient* dataGet(int i) { return &storePatient(i); }
static bool dataAdd(const char* code, const char* name, int age) {
  Patient p{String(code), String(name), (uint16_t)age};
  return storeAddPatient(p);
}
static void dataSetActive(int i) { gActivePatient = i; }

static void onSaveFromUi(const DeviceSettings& s) {
  gLast = s;
  LinkSettings ls;
  ls.ssid = s.wifiSsid; ls.pass = s.wifiPass; ls.server = s.serverUrl; ls.code = s.deviceCode;
  ls.maxTravel = s.maxTravelSec; ls.hold = s.holdSec; ls.rest = s.restSec;
  gLink.sendSettings(ls);
  uiSetDeviceCode(s.deviceCode.c_str());
}

static void onLinkSettingsReply(const LinkSettings& s) {
  gLast.wifiSsid = s.ssid; gLast.wifiPass = s.pass; gLast.serverUrl = s.server;
  gLast.deviceCode = s.code;
  gLast.maxTravelSec = s.maxTravel; gLast.holdSec = s.hold; gLast.restSec = s.rest;
  uiSetDeviceCode(s.code.c_str());
  gSettingsKnown = true;
  uiSetSettingsKnown(true);  // the controller's real values are on screen now
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
  // The index is re-checked against the roster because a card can be swapped
  // between picking a patient and finishing the session.
  // [TH] ตรวจดัชนีซ้ำกับทะเบียนอีกครั้ง เพราะการ์ดอาจถูกเปลี่ยนหลังเลือกคนไข้ไปแล้ว
  if (gActivePatient >= 0 && gActivePatient < storePatientCount()) {
    const Patient& p = storePatient(gActivePatient);
    r.patientCode = p.code;
    r.patientName = p.name;
  } else {
    r.patientCode = "-";
    r.patientName = "";
  }
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
  uiWifi = s.wifi;
  uiServer = s.server;
}

void setup() {
  // [TH] เริ่มพอร์ตดีบัก, ตั้งลิงก์ UART ไปคอนโทรลเลอร์, เริ่ม UI แล้วทักทายขอค่าตั้งค่า/สถานะ
  Serial.begin(115200);
  delay(200);  // UART0 settles after begin(); without this the first line is lost — รอ UART0 นิ่ง ไม่งั้นบรรทัดแรกหาย
  Serial.println("pt-leg-display boot");
  Serial2.begin(115200, SERIAL_8N1, 22, 27);  // UART to controller (CYD free pins) — สาย UART ไปคอนโทรลเลอร์ (ขาว่างของ CYD)
  uiBegin();
  // A missing or unreadable card is not an error worth stopping for: the roster
  // is empty, sessions ring-buffer, and the machine still runs.
  // [TH] ไม่มีการ์ดหรืออ่านการ์ดไม่ได้ ไม่ถือเป็นข้อผิดพลาดที่ต้องหยุดเครื่อง
  //      ทะเบียนจะว่าง ผลรอบจะพักไว้ในหน่วยความจำ และเครื่องยังทำงานได้ตามปกติ
  Serial.printf("sd card: %s, patients=%d\n", storeBegin() ? "mounted" : "absent",
                storePatientCount());
  sdlogEnsureHeader();
  uiSetCallbacks(onOpenSettings, onOpenData);
  uiSetDataCallbacks(dataCount, dataGet, dataAdd, dataSetActive);
  uiSetSettingsSaved(onSaveFromUi);
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
  uint32_t lastRx = gLink.lastRxMs();
  gLinkUp = lastRx != 0 && (now - lastRx) < 2500;

  static uint32_t lastUi = 0, lastPing = 0, lastHello = 0;
  if (now - lastUi >= 250) {  // [TH] รีเฟรชหน้าจอ ~4 ครั้ง/วินาที
    lastUi = now;
    uiUpdateMain(uiRunning, uiPhase, uiElapsed, uiReps, uiWifi, uiServer, gLinkUp);
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
  if (now - lastSd > 5000 && (sdlogRingDepth() > 0 || !storeCardPresent())) {
    lastSd = now;
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
  }
  delay(2);
}


