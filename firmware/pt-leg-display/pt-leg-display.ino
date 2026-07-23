// สเก็ตช์หลักของบอร์ดจอ CYD (ESP32) — รับสถานะจากคอนโทรลเลอร์มาแสดง และส่งคำสั่งจากปุ่มสัมผัสกลับไป
// Display board (CYD) main sketch: shows controller state, relays touch commands back.
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

static void onTouchStart() { Serial.println("touch: start"); gLink.sendCmd("start"); }
static void onTouchStop() { Serial.println("touch: stop"); gLink.sendCmd("stop"); }
static void onOpenSettings() { Serial.println("touch: settings"); uiOpenSettings(gLast); }

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
  uiSetCallbacks(onTouchStart, onTouchStop, onOpenSettings);
  uiSetSettingsSaved(onSaveFromUi);
  Serial.printf("controller link on %s\n", LINK_OVER_USB ? "USB (bench)" : "Serial2");
#if LINK_OVER_USB
  gLink.begin(&Serial);
#else
  gLink.begin(&Serial2);
#endif
  gLink.onState = onLinkState;
  gLink.onSettingsReply = onLinkSettingsReply;
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
  delay(2);
}


