// ตัวรับส่งข้อมูลกับเซิร์ฟเวอร์ผ่าน HTTP: ทำงานบนคอร์เดียวกับลูปความปลอดภัย
// จึงตั้งค่า timeout ให้สั้น เพื่อไม่ให้บล็อกการอ่านลิมิตสวิตช์และเพดานเวลารีเลย์
// Server HTTP client. Runs on the same core as the safety loop, so timeouts are
// kept short to avoid starving the limit-switch / actuator-cap servicing.
#include "net.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

static String baseUrl;
static String devCode;
static bool serverOk = false;       // สถานะเซิร์ฟเวอร์จากคำขอล่าสุด
static uint32_t lastReconnect = 0;  // เวลาที่ลองเชื่อมต่อ WiFi ใหม่ครั้งล่าสุด

// Small FIFO of pending events. A single slot dropped a "started" whenever a
// "stopped" was queued before the first flushed on a slow link, leaving the
// server with a stop for a session it never saw begin. Four slots comfortably
// hold a start+stop (plus a spare) in flight; on overflow the OLDEST is dropped.
// [TH] คิวเหตุการณ์แบบ FIFO ขนาดเล็ก เดิมมีช่องเดียวจึงทำให้เหตุการณ์ "started"
//      ถูกทับด้วย "stopped" เมื่อเน็ตช้า เซิร์ฟเวอร์เลยเห็นแค่การหยุดของรอบที่ไม่เคยเห็นเริ่ม
//      สี่ช่องพอสำหรับ start+stop ที่ค้างอยู่ ถ้าเต็มจะทิ้งตัวเก่าสุด
static const int EV_MAX = 4;
struct Ev { String type; uint32_t elapsed; uint16_t reps; };
static Ev evq[EV_MAX];
static int evHead = 0, evCount = 0;

void netBegin(const DeviceSettings& s) {
  // [TH] ตั้งค่า URL เซิร์ฟเวอร์และรหัสอุปกรณ์ แล้วเริ่มเชื่อมต่อ WiFi ถ้ามี SSID
  baseUrl = s.serverUrl;
  devCode = s.deviceCode;
  if (s.wifiSsid.length()) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(s.wifiSsid.c_str(), s.wifiPass.c_str());
  }
}

bool netWifiUp() { return WiFi.status() == WL_CONNECTED; }
bool netServerUp() { return serverOk; }

// The device code is typed on a touchscreen or hand-edited into settings.json on
// the card, and neither path forbids a quote, a backslash, a space or an
// ampersand. Pasted straight into a JSON body it produces a malformed request
// the server rejects, and pasted into a query string it truncates or splits the
// parameter — either way events and heartbeats stop arriving and the only signal
// is the server showing as down, which reads as a network fault.
// [TH] รหัสอุปกรณ์พิมพ์จากหน้าจอหรือแก้ในไฟล์บนการ์ดได้ ทั้งสองทางไม่ได้ห้ามอักขระพิเศษ
//      ถ้าเอาไปต่อสตริงใส่ JSON ตรงๆ จะได้ข้อความผิดรูปแบบที่เซิร์ฟเวอร์ปฏิเสธ
//      ถ้าใส่ใน query string ก็จะถูกตัดหรือแยกพารามิเตอร์ ผลคือข้อมูลไม่ถึงเซิร์ฟเวอร์
//      และสิ่งเดียวที่เห็นคือสถานะเซิร์ฟเวอร์ดับ ซึ่งดูเหมือนปัญหาเครือข่าย
static String urlEncode(const String& s) {
  static const char* HEXDIG = "0123456789ABCDEF";
  String out;
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') {
      out += c;
    } else {
      out += '%';
      out += HEXDIG[(c >> 4) & 0x0F];
      out += HEXDIG[c & 0x0F];
    }
  }
  return out;
}

static bool post(const String& path, const String& body) {
  // Not reachable -> not "up". Returning without clearing serverOk left it frozen
  // at the last success, so the UI showed a live server while WiFi was down.
  if (!netWifiUp() || !baseUrl.length()) { serverOk = false; return false; }
  HTTPClient http;
  // Timeouts are the loop's blocking budget: this runs on the same core that
  // services the limit switches and the actuator cap, so keep it short. The
  // server is on the LAN; 400 ms is generous. (A full fix moves net I/O off the
  // safety loop onto core 0.)
  http.setConnectTimeout(400);
  http.setTimeout(400);
  http.begin(baseUrl + path);
  http.addHeader("Content-Type", "application/json");
  int code = http.POST(body);
  http.end();
  serverOk = code >= 200 && code < 300;
  return serverOk;
}

String netPollCommand() {
  if (!netWifiUp() || !baseUrl.length()) { serverOk = false; return ""; }
  HTTPClient http;
  http.setConnectTimeout(400);
  http.setTimeout(400);
  String url = baseUrl + "/api/device/command";
  if (devCode.length()) url += "?deviceId=" + urlEncode(devCode);
  http.begin(url);
  int code = http.GET();
  String cmd = "";
  if (code == 200) {
    JsonDocument doc;
    if (deserializeJson(doc, http.getString()) == DeserializationError::Ok &&
        !doc["command"].isNull())
      cmd = doc["command"].as<String>();
  }
  http.end();
  serverOk = code >= 200 && code < 300;
  return cmd;
}

void netQueueEvent(const char* type, uint32_t elapsedSec, uint16_t reps) {
  if (evCount == EV_MAX) {  // full: drop the oldest so the newest is never lost
    evHead = (evHead + 1) % EV_MAX;
    evCount--;
  }
  int tail = (evHead + evCount) % EV_MAX;
  evq[tail] = {String(type), elapsedSec, reps};
  evCount++;
}

static void tryFlushEvent() {
  if (evCount == 0) return;
  const Ev& e = evq[evHead];  // FIFO: preserve start-before-stop ordering
  // Serialised, not concatenated: ArduinoJson escapes whatever the device code
  // contains. See urlEncode above for why that matters.
  // [TH] ใช้ตัวสร้าง JSON แทนการต่อสตริง เพราะไลบรารีจะใส่อักขระหลีกให้เอง
  JsonDocument d;
  d["type"] = e.type;
  d["elapsedSec"] = e.elapsed;
  d["reps"] = e.reps;
  d["deviceId"] = devCode;
  String body;
  serializeJson(d, body);
  if (post("/api/device/event", body)) {
    evHead = (evHead + 1) % EV_MAX;
    evCount--;
  }
}

void netHeartbeat(const char* state, uint32_t elapsedSec, uint16_t reps) {
  // [TH] สัญญาณชีพ: บอกเซิร์ฟเวอร์ว่าอุปกรณ์ยังทำงานและอยู่สถานะใด (ส่งแล้วไม่รอผล)
  JsonDocument d;
  d["state"] = state;
  d["elapsedSec"] = elapsedSec;
  d["reps"] = reps;
  d["deviceId"] = devCode;
  String body;
  serializeJson(d, body);
  post("/api/device/heartbeat", body);
}

void netLoop(uint32_t nowMs) {
  // [TH] งานเครือข่ายประจำลูป: ถ้า WiFi หลุดให้ลองเชื่อมใหม่ทุก 10 วินาที และส่งเหตุการณ์ที่ค้าง
  if (!netWifiUp() && WiFi.getMode() == WIFI_STA && nowMs - lastReconnect > 10000) {
    lastReconnect = nowMs;
    WiFi.reconnect();
  }
  tryFlushEvent();
}
