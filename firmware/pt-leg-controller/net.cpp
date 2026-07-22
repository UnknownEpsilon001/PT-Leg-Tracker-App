#include "net.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

static String baseUrl;
static String devCode;
static bool serverOk = false;
static uint32_t lastReconnect = 0;

// Small FIFO of pending events. A single slot dropped a "started" whenever a
// "stopped" was queued before the first flushed on a slow link, leaving the
// server with a stop for a session it never saw begin. Four slots comfortably
// hold a start+stop (plus a spare) in flight; on overflow the OLDEST is dropped.
static const int EV_MAX = 4;
struct Ev { String type; uint32_t elapsed; uint16_t reps; };
static Ev evq[EV_MAX];
static int evHead = 0, evCount = 0;

void netBegin(const DeviceSettings& s) {
  baseUrl = s.serverUrl;
  devCode = s.deviceCode;
  if (s.wifiSsid.length()) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(s.wifiSsid.c_str(), s.wifiPass.c_str());
  }
}

bool netWifiUp() { return WiFi.status() == WL_CONNECTED; }
bool netServerUp() { return serverOk; }

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
  if (devCode.length()) url += "?deviceId=" + devCode;
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
  String body = String("{\"type\":\"") + e.type + "\",\"elapsedSec\":" + e.elapsed +
                ",\"reps\":" + e.reps + ",\"deviceId\":\"" + devCode + "\"}";
  if (post("/api/device/event", body)) {
    evHead = (evHead + 1) % EV_MAX;
    evCount--;
  }
}

void netHeartbeat(const char* state, uint32_t elapsedSec, uint16_t reps) {
  String body = String("{\"state\":\"") + state + "\",\"elapsedSec\":" + elapsedSec +
                ",\"reps\":" + reps + ",\"deviceId\":\"" + devCode + "\"}";
  post("/api/device/heartbeat", body);
}

void netLoop(uint32_t nowMs) {
  if (!netWifiUp() && WiFi.getMode() == WIFI_STA && nowMs - lastReconnect > 10000) {
    lastReconnect = nowMs;
    WiFi.reconnect();
  }
  tryFlushEvent();
}
