// ตัวรับส่งข้อมูลกับเซิร์ฟเวอร์ผ่าน HTTP: ทำงานในงานเบื้องหลังบนคอร์ 0
// ลูปความปลอดภัยบนคอร์ 1 จึงไม่ต้องรอเครือข่ายเลย ฟังก์ชันใน net.h แค่ฝากงานหรืออ่านผลล่าสุด
// Server HTTP client. Every request runs in a background task on core 0, so the
// safety loop on core 1 never waits on the network. The functions in net.h only
// hand work to that task or read its latest results.
//
// Why: the loop used to make these calls itself, each bounded by a 400 ms
// timeout. Over the internet (Tailscale Funnel, https) one request takes longer
// than that, so requests failed, and while they ran the loop stopped feeding the
// CYD: the screen flickered "controller not found" whenever Wi-Fi was up and
// sessions stopped on a false link loss.
// [TH] เหตุผล: เดิมลูปหลักเรียก HTTP เอง เมื่อต่อผ่านอินเทอร์เน็ต (https) คำขอหนึ่งใช้เวลานานเกิน
//      คำขอจึงล้มเหลว และระหว่างรอ ลูปหยุดส่งข้อมูลให้จอ จอจึงกะพริบว่าไม่พบตัวควบคุม และรอบการทำงานหยุดเอง
#include "net.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// Everything below the lock is shared by the loop (core 1) and the net task
// (core 0). Hold it only to copy values in or out, never across a request.
// [TH] ตัวแปรใต้ล็อกนี้ใช้ร่วมกันระหว่างลูป (คอร์ 1) และงานเครือข่าย (คอร์ 0)
//      ถือล็อกแค่ตอนคัดลอกค่าเข้าออก ห้ามถือไว้ระหว่างรอคำขอ
static SemaphoreHandle_t lock = nullptr;
static TaskHandle_t task = nullptr;

static String baseUrl;
static String devCode;
static volatile bool serverOk = false;  // last request got a 2xx — คำขอล่าสุดได้ 2xx
static uint32_t lastReconnect = 0;      // loop-only — ใช้ในลูปเท่านั้น

// Small FIFO of pending events, so a "stopped" queued before its "started" was
// delivered cannot overtake or replace it. On overflow the OLDEST is dropped.
// [TH] คิวเหตุการณ์ขนาดเล็ก เพื่อไม่ให้ "stopped" แซงหรือทับ "started" ที่ยังส่งไม่ถึง
static const int EV_MAX = 4;
struct Ev { String type; uint32_t elapsed; uint16_t reps; String uid; };
static Ev evq[EV_MAX];
static int evHead = 0, evCount = 0;
static uint32_t evNextTryMs = 0;
// Counts every removal from the head, so the task can tell whether the event it
// sent is still the head when the answer comes back.
// [TH] นับทุกครั้งที่เอาเหตุการณ์ออกจากหัวคิว เพื่อให้งานรู้ว่าเหตุการณ์ที่ส่งไปยังเป็นหัวคิวอยู่หรือไม่
static uint32_t evPops = 0;
static const uint32_t EV_RETRY_MS = 3000;

// Latest heartbeat body; a newer one replaces an unsent older one.
// [TH] ข้อมูลสัญญาณชีพล่าสุด อันใหม่ทับอันเก่าที่ยังไม่ได้ส่ง
static String hbBody;

// One SD-card session upload at a time. The loop polls netPostSession() for the
// same uid until the task reports how it went.
// [TH] ส่งรอบการทำงานจากการ์ด SD ทีละรายการ ลูปเรียก netPostSession() ซ้ำด้วย uid เดิมจนกว่างานจะรายงานผล
enum UpState { UP_NONE, UP_PENDING, UP_OK, UP_FAIL };
static UpState upState = UP_NONE;
static String upUid, upBody;

// The command poll result. A command stays until the loop takes it, so a "start"
// cannot be overwritten by the empty answer of the next poll.
// [TH] ผลการดึงคำสั่ง คำสั่งจะค้างไว้จนกว่าลูปจะหยิบไป "start" จึงไม่ถูกคำตอบว่างของรอบถัดไปทับ
static String cmdPending;
static int16_t cmdTarget = -1;
static NetTimings cmdTimings{0, 0, 0, 0, 0};

// The request timeout no longer blocks the safety loop, so it can be long enough
// for https over the internet.
// [TH] เวลารอคำขอไม่ได้บล็อกลูปความปลอดภัยแล้ว จึงตั้งยาวพอสำหรับ https ผ่านอินเทอร์เน็ต
static const uint16_t HTTP_TIMEOUT_MS = 4000;
static const uint32_t POLL_MS = 1000;
// After a request that never reached a server, wait this long before the next.
// [TH] ถ้าคำขอไปไม่ถึงเซิร์ฟเวอร์ ให้รอเท่านี้ก่อนลองใหม่
static const uint32_t NET_QUIET_MS = 5000;

static void take() { xSemaphoreTake(lock, portMAX_DELAY); }
static void give() { xSemaphoreGive(lock); }

bool netWifiUp() { return WiFi.status() == WL_CONNECTED; }
bool netServerUp() { return serverOk; }

bool netHasServer() {
  if (!lock) return false;
  take();
  bool has = baseUrl.length() > 0;
  give();
  return has;
}

// The device code is typed by hand and may hold any character, so it is encoded
// before it goes into a query string.
// [TH] รหัสอุปกรณ์พิมพ์เองจึงอาจมีอักขระใดก็ได้ ต้องเข้ารหัสก่อนใส่ใน query string
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

// --- net task (core 0) -------------------------------------------------------

static int httpPost(const String& url, const String& body) {
  HTTPClient http;
  http.setConnectTimeout(HTTP_TIMEOUT_MS);
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  int code = http.POST(body);
  http.end();
  return code;
}

static int httpPollCommand(const String& url) {
  HTTPClient http;
  http.setConnectTimeout(HTTP_TIMEOUT_MS);
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.begin(url);
  int code = http.GET();
  if (code == 200) {
    // Read as a bounded, filtered stream: serverUrl can point at any web page,
    // and pulling a large page into a String every second fragments the heap.
    // [TH] อ่านแบบจำกัดขนาดและกรองเฉพาะฟิลด์ที่ใช้ เพราะ URL อาจชี้ไปหน้าเว็บอะไรก็ได้
    int len = http.getSize();
    if (len > 2048) {
      Serial.printf("net: command body %d bytes, ignored\n", len);
    } else {
      JsonDocument filter;
      filter["command"] = true;
      filter["targetReps"] = true;
      filter["timings"] = true;
      JsonDocument doc;
      if (deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter)) ==
          DeserializationError::Ok) {
        String cmd = doc["command"].isNull() ? "" : doc["command"].as<String>();
        if (cmd.length() > 32) cmd = "";  // no command is that long — คำสั่งไม่ยาวขนาดนั้น
        JsonVariantConst t = doc["timings"];
        NetTimings timings{t["liftSec"] | (uint16_t)0, t["lowerSec"] | (uint16_t)0,
                           t["holdSec"] | (uint16_t)0, t["restSec"] | (uint16_t)0,
                           t["maxTravelSec"] | (uint16_t)0};
        take();
        if (cmd.length()) cmdPending = cmd;
        cmdTarget = doc["targetReps"].isNull() ? -1 : (int16_t)doc["targetReps"];
        cmdTimings = timings;
        give();
      }
    }
  }
  http.end();
  return code;
}

static void netTask(void*) {
  uint32_t lastPoll = 0, quietUntil = 0;
  bool lastWasBeat = false;
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(20));
    uint32_t now = millis();
    if (!netWifiUp()) { serverOk = false; continue; }  // no route is not "up" — ไม่มีเส้นทางไม่ถือว่าออนไลน์
    if ((int32_t)(now - quietUntil) < 0) continue;

    // Pick ONE job, copying what it needs under the lock: events first (they
    // carry session start/stop), then the session upload. The command poll and the
    // heartbeat take turns when both are due: when each request takes about a
    // second, a poll that always won left no turn for heartbeats, and the server
    // marked a working device offline.
    // [TH] เลือกงานเดียว และคัดลอกข้อมูลที่ต้องใช้ใต้ล็อก ลำดับ: เหตุการณ์ การส่งรอบ
    //      ส่วนดึงคำสั่งกับสัญญาณชีพผลัดกันเมื่อถึงเวลาทั้งคู่ ถ้าดึงคำสั่งได้ก่อนเสมอ สัญญาณชีพจะไม่ได้ส่งเลย
    enum Job { NONE, EVENT, UPLOAD, POLL, BEAT } job = NONE;
    String url, body;
    uint32_t sentPops = 0;
    take();
    if (baseUrl.length()) {
      if (evCount && (int32_t)(now - evNextTryMs) >= 0) {
        const Ev& e = evq[evHead];
        JsonDocument d;
        d["type"] = e.type;
        d["elapsedSec"] = e.elapsed;
        d["reps"] = e.reps;
        d["deviceId"] = devCode;
        if (e.uid.length()) d["sessionUid"] = e.uid;
        serializeJson(d, body);
        url = baseUrl + "/api/device/event";
        sentPops = evPops;
        job = EVENT;
      } else if (upState == UP_PENDING) {
        url = baseUrl + "/api/device/session";
        body = upBody;
        job = UPLOAD;
      } else if (now - lastPoll >= POLL_MS && !(hbBody.length() && !lastWasBeat)) {
        url = baseUrl + "/api/device/command";
        if (devCode.length()) url += "?deviceId=" + urlEncode(devCode);
        job = POLL;
      } else if (hbBody.length()) {
        url = baseUrl + "/api/device/heartbeat";
        body = hbBody;
        hbBody = "";
        job = BEAT;
      }
    }
    bool hasUrl = baseUrl.length() > 0;
    give();
    if (!hasUrl) serverOk = false;
    if (job == NONE) continue;

    if (job == POLL) lastPoll = now;
    lastWasBeat = job == BEAT;
    int code = job == POLL ? httpPollCommand(url) : httpPost(url, body);
    bool ok = code >= 200 && code < 300;
    serverOk = ok;
    // Any HTTP status means a server answered; only a failed connect waits.
    // [TH] ได้สถานะ HTTP ใดก็ตามแปลว่ามีเซิร์ฟเวอร์ตอบ รอเฉพาะกรณีเชื่อมต่อไม่ได้
    if (code < 0) quietUntil = millis() + NET_QUIET_MS;

    take();
    if (job == EVENT) {
      if (ok && evCount && evPops == sentPops) {
        evHead = (evHead + 1) % EV_MAX;
        evCount--;
        evPops++;
        evNextTryMs = millis();
      } else if (ok) {
        evNextTryMs = millis();  // dropped while in flight; the new head goes next
      } else {
        evNextTryMs = millis() + EV_RETRY_MS;
      }
    } else if (job == UPLOAD && upState == UP_PENDING) {
      upState = ok ? UP_OK : UP_FAIL;
    }
    give();
  }
}

// --- loop-side API (core 1) ---------------------------------------------------

void netBegin(const DeviceSettings& s) {
  if (!lock) lock = xSemaphoreCreateMutex();
  String url = s.serverUrl;
  // "http://pc:8000/" + "/api/..." made "//api", which the server answered 404.
  // [TH] ตัด / ท้าย URL ทิ้ง ไม่อย่างนั้นจะได้ "//api" ซึ่งเซิร์ฟเวอร์ตอบ 404
  while (url.endsWith("/")) url.remove(url.length() - 1);
  take();
  baseUrl = url;
  devCode = s.deviceCode;
  give();
  if (!task) xTaskCreatePinnedToCore(netTask, "net", 12288, nullptr, 1, &task, 0);
  if (s.wifiSsid.length()) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(s.wifiSsid.c_str(), s.wifiPass.c_str());
  }
}

String netPollCommand(int16_t* targetOut, NetTimings* timingsOut) {
  if (targetOut) *targetOut = -1;
  if (timingsOut) *timingsOut = NetTimings{0, 0, 0, 0, 0};
  if (!lock) return "";
  take();
  // Each poll result is handed over once, like the command: a cached target
  // returned on every call would undo an edit made on the CYD each second.
  // [TH] ผลการดึงแต่ละครั้งส่งให้ลูปครั้งเดียวเหมือนคำสั่ง ถ้าคืนค่าเก่าซ้ำทุกครั้ง จะทับค่าที่แก้บนจอทุกวินาที
  String cmd = cmdPending;
  cmdPending = "";
  if (targetOut) *targetOut = cmdTarget;
  if (timingsOut) *timingsOut = cmdTimings;
  cmdTarget = -1;
  cmdTimings = NetTimings{0, 0, 0, 0, 0};
  give();
  return cmd;
}

void netQueueEvent(const char* type, uint32_t elapsedSec, uint16_t reps, const String& uid) {
  if (!lock) return;
  take();
  if (evCount == EV_MAX) {  // full: drop the oldest so the newest is never lost
    evHead = (evHead + 1) % EV_MAX;
    evCount--;
    evPops++;
  }
  int tail = (evHead + evCount) % EV_MAX;
  evq[tail] = {String(type), elapsedSec, reps, uid};
  evCount++;
  give();
}

void netHeartbeat(const char* state, uint32_t elapsedSec, uint16_t reps, uint16_t target) {
  if (!lock) return;
  JsonDocument d;
  d["state"] = state;
  d["elapsedSec"] = elapsedSec;
  d["reps"] = reps;
  d["target"] = target;
  take();
  d["deviceId"] = devCode;
  hbBody = "";  // replace an unsent one, never append to it — แทนที่อันที่ยังไม่ได้ส่ง ไม่ต่อท้าย
  serializeJson(d, hbBody);
  give();
}

bool netPostSession(const String& uid, const String& whenLocal, const String& patientCode,
                    uint32_t durationSec, uint16_t reps, bool fault) {
  if (!lock) return false;
  take();
  if (upUid == uid && upState == UP_OK) {
    upState = UP_NONE;
    give();
    return true;
  }
  if (upUid == uid && upState == UP_PENDING) {
    give();
    return false;
  }
  JsonDocument d;
  d["uid"] = uid;
  if (whenLocal.length() && whenLocal != "?") d["endedAt"] = whenLocal;
  else d["endedAt"] = nullptr;  // the device did not know the time — อุปกรณ์ไม่ทราบเวลา
  d["durationSec"] = durationSec;
  d["reps"] = reps;
  d["fault"] = fault;
  if (patientCode.length() && patientCode != "-") d["patientCode"] = patientCode;
  d["deviceId"] = devCode;
  upBody = "";
  serializeJson(d, upBody);
  upUid = uid;
  upState = UP_PENDING;
  give();
  return false;
}

// With no server configured the radio has one job, setting the clock, and once
// that is done it is switched off.
// [TH] ถ้าไม่ได้ตั้งเซิร์ฟเวอร์ไว้ วิทยุมีงานเดียวคือตั้งนาฬิกา ตั้งเสร็จแล้วก็ปิด
void netRadioOff() {
  if (WiFi.getMode() == WIFI_OFF) return;
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  Serial.println("wifi: clock is set and no server is configured, radio off");
}

void netLoop(uint32_t nowMs) {
  if (!netWifiUp() && WiFi.getMode() == WIFI_STA && nowMs - lastReconnect > 10000) {
    lastReconnect = nowMs;
    WiFi.reconnect();
  }
}
