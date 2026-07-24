// สเก็ตช์หลักของบอร์ดคอนโทรลเลอร์ (ESP32) — สมองของเครื่องกายภาพบำบัดขา
// รวมทุกโมดูล: อ่านสวิตช์ -> เดินเครื่องสถานะ -> สั่งรีเลย์ -> คุยกับเซิร์ฟเวอร์และจอ
// Controller main sketch: switches -> state machine -> relays -> server + display.
#include "config.h"
#include "settings.h"
#include "actuator.h"
#include "switches.h"
#include "buttons.h"
#include "session.h"
#include "net.h"
#include "link.h"

DeviceSettings gSettings;  // ค่าตั้งค่าปัจจุบัน
SessionSM gSession;        // เครื่องสถานะของรอบการทำงาน
Link gLink;                // ลิงก์ UART ไปยังบอร์ดจอ

static void applyActuator() {
  // [TH] แปลงสถานะปัจจุบันของรอบให้เป็นคำสั่งรีเลย์ (เรียกทุกลูป)
  switch (gSession.phase()) {
    case Phase::Lift: actuatorUp(); break;
    case Phase::Lower:
    case Phase::SafeLower: actuatorDown(); break;
    default: actuatorOff(); break;  // Idle, Hold, Rest, Fault — สถานะที่ต้องดับรีเลย์
  }
}

static void applySettings(const DeviceSettings& s) {
  // [TH] รับค่าตั้งค่าใหม่: บันทึกลง NVS, อัปเดตเวลารอบ, ตั้งเพดานรีเลย์ (เผื่อ 500 มิลลิวินาที
  //      ให้สูงกว่าเพดานของเครื่องสถานะเล็กน้อย เพื่อเป็นแนวป้องกันสำรอง), แล้วเริ่มเครือข่ายใหม่
  gSettings = s;
  settingsSave(gSettings);
  gSession.cfg = {gSettings.maxTravelSec * 1000u, gSettings.holdSec * 1000u,
                  gSettings.restSec * 1000u};
  actuatorSetCap(gSettings.maxTravelSec * 1000u + 500u);
  netBegin(gSettings);
}

static void onLinkSettings(const LinkSettings& s) {
  DeviceSettings d;
  d.wifiSsid = s.ssid; d.wifiPass = s.pass; d.serverUrl = s.server;
  d.deviceCode = s.code;
  d.maxTravelSec = s.maxTravel; d.holdSec = s.hold; d.restSec = s.rest;
  applySettings(d);
}

static LinkSettings currentAsLink() {
  LinkSettings s;
  s.ssid = gSettings.wifiSsid; s.pass = gSettings.wifiPass;
  s.server = gSettings.serverUrl; s.code = gSettings.deviceCode;
  s.maxTravel = gSettings.maxTravelSec; s.hold = gSettings.holdSec;
  s.rest = gSettings.restSec;
  return s;
}

// [TH] เมื่อจอทักทาย (hello) ให้ส่งค่าตั้งค่าปัจจุบันกลับไป
static void onLinkHello() { gLink.sendSettings(currentAsLink()); }

void setup() {
  actuatorBegin();  // safety first — ดับรีเลย์ก่อนเป็นอันดับแรกเพื่อความปลอดภัย
  switchesBegin();
  buttonsBegin();
  Serial.begin(115200);
  delay(200);  // let UART0 settle so the boot banner is not clipped — รอ UART0 นิ่งกันข้อความต้นเปิดเครื่องขาด
  Serial2.begin(115200, SERIAL_8N1, 16, 17);  // UART to CYD — สาย UART ไปบอร์ดจอ (RX=16, TX=17)
  settingsLoad(gSettings);
  gSession.cfg = {gSettings.maxTravelSec * 1000u, gSettings.holdSec * 1000u,
                  gSettings.restSec * 1000u};
  actuatorSetCap(gSettings.maxTravelSec * 1000u + 500u);
  netBegin(gSettings);
  gLink.begin(&Serial2);
  gLink.onSettings = onLinkSettings;
  gLink.onHello = onLinkHello;
  Serial.printf("pt-leg-controller %s ssid=%s server=%s code=%s cycle max/hold/rest=%u/%u/%u\n",
                FW_VERSION, gSettings.wifiSsid.c_str(), gSettings.serverUrl.c_str(),
                gSettings.deviceCode.c_str(), gSettings.maxTravelSec, gSettings.holdSec,
                gSettings.restSec);
}

void loop() {
  // [TH] ลำดับสำคัญ: อ่านสวิตช์ -> เดินเครื่องสถานะ -> สั่งรีเลย์ -> ตรวจเพดานเวลา ก่อนงานเครือข่าย
  uint32_t now = millis();
  switchesPoll(now);
  buttonsPoll(now);
  gSession.tick(now, switchTop(), switchBottom());
  // panel buttons: START (idle only), STOP (any state), RESET (idle only)
  // [TH] ปุ่มหน้าปัด: START เริ่ม (เฉพาะว่าง), STOP หยุด (ตลอดเวลา), RESET ล้าง (เฉพาะว่าง)
  if (buttonStartPressed()) gSession.start(now);
  if (buttonStopPressed()) gSession.requestStop(now);
  if (buttonResetPressed()) gSession.reset(now);
  applyActuator();
  actuatorTick(now);
  if (actuatorCapTripped()) gSession.forceFault(now);  // backstop: SM never commanded OFF
                                                       // [TH] แนวป้องกันสำรอง เผื่อเครื่องสถานะไม่ได้สั่งดับ
  netLoop(now);
  gLink.poll();

  // UART-loss safe-stop: screen may be dead, no touch-stop possible
  // [TH] หยุดปลอดภัยเมื่อสาย UART หลุด: จออาจดับ กดหยุดไม่ได้ จึงสั่งหยุดเองเมื่อไม่ได้ข่าวเกิน 2 วินาที
  if (gSession.running() && gLink.lastRxMs() != 0 && now - gLink.lastRxMs() > 2000)
    gSession.requestStop(now);

  static uint32_t lastPoll = 0, lastHb = 0, lastState = 0;
  if (now - lastPoll >= 1000) {  // [TH] ดึงคำสั่งจากเซิร์ฟเวอร์ทุก 1 วินาที
    lastPoll = now;
    String cmd = netPollCommand();
    if (cmd == "start") gSession.start(now);
    else if (cmd == "stop") gSession.requestStop(now);
  }
  // [TH] ส่งสัญญาณชีพถี่ขึ้นตอนทำงาน (ทุก 1 วิ) และห่างตอนว่าง (ทุก 5 วิ)
  uint32_t hbInterval = gSession.running() ? 1000 : 5000;
  if (now - lastHb >= hbInterval) {
    lastHb = now;
    netHeartbeat(gSession.running() ? "running" : "idle", gSession.elapsedSec(now),
                 gSession.reps());
  }
  // [TH] ส่งเหตุการณ์เมื่อ "เพิ่งเริ่ม" หรือ "เพิ่งหยุด" (ตรวจจับขอบสัญญาณครั้งเดียว)
  if (gSession.takeStartedEdge()) netQueueEvent("started", gSession.elapsedSec(now), gSession.reps());
  if (gSession.takeStoppedEdge()) netQueueEvent("stopped", gSession.finalElapsedSec(), gSession.finalReps());

  if (now - lastState >= 250) {  // [TH] ส่งสถานะให้จอ ~4 ครั้งต่อวินาที
    lastState = now;
    LinkState st{gSession.running(), (int)gSession.phase(), gSession.elapsedSec(now),
                 gSession.reps(), netWifiUp(), netServerUp()};
    gLink.sendState(st);
  }
  delay(2);
}
