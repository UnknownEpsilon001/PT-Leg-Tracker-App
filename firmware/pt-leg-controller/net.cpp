// ตัวรับส่งข้อมูลกับเซิร์ฟเวอร์ผ่าน HTTP: ทำงานบนคอร์เดียวกับลูปความปลอดภัย
// จึงตั้งค่า timeout ให้สั้น เพื่อไม่ให้บล็อกการอ่านปุ่มและเพดานเวลาจ่ายไฟ
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

// A queue that cannot be delivered must not be retried every loop iteration.
// Each failed attempt costs the full connect timeout, and an event the server
// never accepts stays queued forever, so the safety loop would drop from
// hundreds of iterations per second to two: the limit switches need two samples
// 25 ms apart to be believed, and the panel STOP button the same, so both would
// answer most of a second late while the motor is still driving.
// [TH] คิวที่ส่งไม่สำเร็จต้องไม่ลองใหม่ทุกรอบลูป
//      การลองแต่ละครั้งเสียเวลาเท่ากับ timeout เต็มๆ และเหตุการณ์ที่เซิร์ฟเวอร์ไม่รับจะค้างตลอดไป
//      ลูปความปลอดภัยจะเหลือแค่ไม่กี่รอบต่อวินาที ลิมิตสวิตช์ต้องอ่านสองครั้งห่างกัน 25 มิลลิวินาที
//      ปุ่ม STOP ก็เช่นกัน ทั้งคู่จึงตอบสนองช้าเกือบวินาทีทั้งที่มอเตอร์ยังขับอยู่
static const uint32_t EV_RETRY_MS = 3000;
static uint32_t evNextTryMs = 0;

// After a request that never reached a server (no route, connect timeout),
// every HTTP call stays off for this long. A typed server URL with nothing behind
// it would otherwise cost a 400 ms connect timeout on the poll every second and
// on each heartbeat, and the safety loop would spend most of its time waiting on
// a machine that is not there.
// [TH] หลังจากคำขอที่ไปไม่ถึงเซิร์ฟเวอร์ (ไม่มีเส้นทาง หรือเชื่อมต่อไม่ทันเวลา) จะหยุดเรียก HTTP ทุกชนิดไว้นานเท่านี้
//      ไม่อย่างนั้นที่อยู่เซิร์ฟเวอร์ที่พิมพ์ไว้แต่ไม่มีเครื่องอยู่จริง จะกินเวลารอเชื่อมต่อ 400 มิลลิวินาที
//      ทุกวินาทีตอนถามคำสั่งและทุกครั้งที่ส่งสัญญาณชีพ ลูปความปลอดภัยจะเสียเวลาส่วนใหญ่ไปกับการรอเครื่องที่ไม่มีอยู่
static const uint32_t NET_QUIET_MS = 30000;
static uint32_t quietUntilMs = 0;
static bool quietActive = false;

// Whether an HTTP call may run now. Clears serverOk whenever it says no, for the
// reason the comment in post() gives.
// [TH] ตอนนี้เรียก HTTP ได้หรือไม่ ถ้าไม่ได้จะล้าง serverOk ด้วยเหตุผลตามคอมเมนต์ใน post()
static bool canTalk() {
  if (quietActive && (int32_t)(millis() - quietUntilMs) < 0) { serverOk = false; return false; }
  quietActive = false;
  if (!netWifiUp() || !baseUrl.length()) { serverOk = false; return false; }
  return true;
}

// A negative HTTPClient code is a transport failure: nothing answered. An HTTP
// status, even 500, means a server is there, so it does not start the quiet time.
// [TH] รหัสติดลบจาก HTTPClient คือการส่งล้มเหลว ไม่มีอะไรตอบกลับ
//      ส่วนรหัสสถานะ HTTP แม้แต่ 500 แปลว่ามีเซิร์ฟเวอร์อยู่ จึงไม่เริ่มช่วงหยุดพัก
static void noteResult(int code) {
  serverOk = code >= 200 && code < 300;
  if (code < 0) {
    quietActive = true;
    quietUntilMs = millis() + NET_QUIET_MS;
  }
}

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
  if (!canTalk()) return false;
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
  noteResult(code);
  return serverOk;
}

String netPollCommand(int16_t* targetOut, NetTimings* timingsOut) {
  // Defaulted up front so every early return — including the one right below —
  // leaves the caller with -1 / all-zero rather than a stale value from a
  // previous poll.
  // [TH] ตั้งค่าเริ่มต้นไว้ก่อน เพื่อให้ทุกทางออกก่อนกำหนด รวมถึงบรรทัดถัดไปนี้เอง
  //      ส่งค่ากลับเป็น -1 / ศูนย์ทั้งหมด ไม่ใช่ค่าค้างจากการถามครั้งก่อน
  if (targetOut) *targetOut = -1;
  if (timingsOut) *timingsOut = NetTimings{0, 0, 0, 0, 0};
  if (!canTalk()) return "";
  HTTPClient http;
  http.setConnectTimeout(400);
  http.setTimeout(400);
  String url = baseUrl + "/api/device/command";
  if (devCode.length()) url += "?deviceId=" + urlEncode(devCode);
  http.begin(url);
  int code = http.GET();
  String cmd = "";
  if (code == 200) {
    // The body is read as a bounded stream, not pulled into a String whole. This
    // poll runs once a second, and serverUrl can point at anything someone typed
    // — a captive portal, a router's web UI on the wrong port — which answers 200
    // with a page. getString() would allocate that page every second on a board
    // with ~150 KB of usable heap and fragment what the UART buffer, the event
    // queue and the WiFi stack draw from. The filter keeps only the one field
    // this function wants, so a large body costs nothing to parse either.
    // [TH] อ่านเนื้อหาแบบสตรีมที่จำกัดขนาด ไม่ดึงมาเก็บเป็นสตริงทั้งก้อน
    //      การถามคำสั่งทำทุกวินาที และ serverUrl ชี้ไปที่อะไรก็ได้ที่มีคนพิมพ์ไว้
    //      เช่นหน้าล็อกอินไวไฟหรือหน้าเว็บของเราเตอร์ที่ตอบ 200 พร้อมหน้าเว็บทั้งหน้า
    //      getString() จะจองหน่วยความจำเท่าหน้านั้นทุกวินาทีบนบอร์ดที่มีหน่วยความจำว่างราว 150 KB
    //      และทำให้หน่วยความจำแตกกระจัดกระจายจนบัฟเฟอร์ UART คิวเหตุการณ์ และ WiFi ไม่พอใช้
    //      ตัวกรองเก็บเฉพาะฟิลด์เดียวที่ต้องใช้ เนื้อหาก้อนใหญ่จึงไม่กินหน่วยความจำตอนแปลงด้วย
    static const int MAX_BODY = 2048;
    int len = http.getSize();
    if (len > MAX_BODY) {
      Serial.printf("net: command body %d bytes, ignored\n", len);
    } else {
      JsonDocument filter;
      filter["command"] = true;
      filter["targetReps"] = true;
      filter["timings"] = true;
      JsonDocument doc;
      if (deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter)) ==
          DeserializationError::Ok) {
        if (!doc["command"].isNull()) {
          cmd = doc["command"].as<String>();
          if (cmd.length() > 32) cmd = "";  // no command is that long — คำสั่งไม่ยาวขนาดนั้น
        }
        if (targetOut)
          *targetOut = doc["targetReps"].isNull() ? -1 : (int16_t)doc["targetReps"];
        if (timingsOut) {
          JsonVariantConst t = doc["timings"];
          timingsOut->liftSec = t["liftSec"] | (uint16_t)0;
          timingsOut->lowerSec = t["lowerSec"] | (uint16_t)0;
          timingsOut->holdSec = t["holdSec"] | (uint16_t)0;
          timingsOut->restSec = t["restSec"] | (uint16_t)0;
          timingsOut->maxTravelSec = t["maxTravelSec"] | (uint16_t)0;
        }
      }
    }
  }
  http.end();
  noteResult(code);
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

static void tryFlushEvent(uint32_t nowMs) {
  if (evCount == 0) return;
  // Signed compare so the wrap at 49 days does not park the queue for a month.
  // [TH] เทียบแบบมีเครื่องหมาย เวลาที่วนกลับที่ 49 วันจะได้ไม่ทำให้คิวหยุดส่งเป็นเดือน
  if ((int32_t)(nowMs - evNextTryMs) < 0) return;
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
    evNextTryMs = nowMs;  // draining works: take the next one on the next pass
                          // [TH] ส่งได้แล้ว รอบถัดไปส่งตัวต่อไปได้ทันที
  } else {
    evNextTryMs = nowMs + EV_RETRY_MS;
  }
}

void netHeartbeat(const char* state, uint32_t elapsedSec, uint16_t reps, uint16_t target) {
  // [TH] สัญญาณชีพ: บอกเซิร์ฟเวอร์ว่าอุปกรณ์ยังทำงานและอยู่สถานะใด (ส่งแล้วไม่รอผล)
  JsonDocument d;
  d["state"] = state;
  d["elapsedSec"] = elapsedSec;
  d["reps"] = reps;
  d["target"] = target;
  d["deviceId"] = devCode;
  String body;
  serializeJson(d, body);
  post("/api/device/heartbeat", body);
}

// With no server configured the radio has one job, setting the clock, and once
// that is done it is switched off: an idle machine then makes no reconnect
// attempts at all. WIFI_OFF also stops netLoop's reconnect below, which only
// runs in STA mode. netBegin() turns it back on after a settings change.
// [TH] ถ้าไม่ได้ตั้งเซิร์ฟเวอร์ไว้ วิทยุมีหน้าที่เดียวคือตั้งนาฬิกา พอเสร็จแล้วจึงปิด
//      เครื่องที่ว่างอยู่จะไม่พยายามเชื่อมต่อใหม่อีกเลย WIFI_OFF ยังหยุดการเชื่อมต่อใหม่ใน netLoop ด้านล่างด้วย
//      เพราะส่วนนั้นทำงานเฉพาะโหมด STA ส่วน netBegin() จะเปิดกลับมาเมื่อมีการเปลี่ยนค่าตั้งค่า
void netRadioOff() {
  if (WiFi.getMode() == WIFI_OFF) return;
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  Serial.println("wifi: clock is set and no server is configured, radio off");
}

bool netHasServer() { return baseUrl.length() > 0; }

void netLoop(uint32_t nowMs) {
  // [TH] งานเครือข่ายประจำลูป: ถ้า WiFi หลุดให้ลองเชื่อมใหม่ทุก 10 วินาที และส่งเหตุการณ์ที่ค้าง
  if (!netWifiUp() && WiFi.getMode() == WIFI_STA && nowMs - lastReconnect > 10000) {
    lastReconnect = nowMs;
    WiFi.reconnect();
  }
  tryFlushEvent(nowMs);
}
