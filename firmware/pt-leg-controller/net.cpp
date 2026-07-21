#include "net.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

static String baseUrl;
static bool serverOk = false;
static uint32_t lastReconnect = 0;

// one-slot pending event (matches server's one-slot command model)
static bool evPending = false;
static String evType;
static uint32_t evElapsed = 0;
static uint16_t evReps = 0;

void netBegin(const DeviceSettings& s) {
  baseUrl = s.serverUrl;
  if (s.wifiSsid.length()) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(s.wifiSsid.c_str(), s.wifiPass.c_str());
  }
}

bool netWifiUp() { return WiFi.status() == WL_CONNECTED; }
bool netServerUp() { return serverOk; }

static bool post(const String& path, const String& body) {
  if (!netWifiUp() || !baseUrl.length()) return false;
  HTTPClient http;
  http.setConnectTimeout(800);
  http.setTimeout(800);
  http.begin(baseUrl + path);
  http.addHeader("Content-Type", "application/json");
  int code = http.POST(body);
  http.end();
  serverOk = code >= 200 && code < 300;
  return serverOk;
}

String netPollCommand() {
  if (!netWifiUp() || !baseUrl.length()) return "";
  HTTPClient http;
  http.setConnectTimeout(800);
  http.setTimeout(800);
  http.begin(baseUrl + "/api/device/command");
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
  evPending = true; // overwrite: newest transition wins the single slot
  evType = type;
  evElapsed = elapsedSec;
  evReps = reps;
}

static void tryFlushEvent() {
  if (!evPending) return;
  String body = String("{\"type\":\"") + evType + "\",\"elapsedSec\":" + evElapsed +
                ",\"reps\":" + evReps + "}";
  if (post("/api/device/event", body)) evPending = false;
}

void netHeartbeat(const char* state, uint32_t elapsedSec, uint16_t reps) {
  String body = String("{\"state\":\"") + state + "\",\"elapsedSec\":" + elapsedSec +
                ",\"reps\":" + reps + "}";
  post("/api/device/heartbeat", body);
}

void netLoop(uint32_t nowMs) {
  if (!netWifiUp() && WiFi.getMode() == WIFI_STA && nowMs - lastReconnect > 10000) {
    lastReconnect = nowMs;
    WiFi.reconnect();
  }
  tryFlushEvent();
}
