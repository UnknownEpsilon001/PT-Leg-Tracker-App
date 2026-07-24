// การรับส่งข้อความ JSON บนสาย UART: อ่านทีละไบต์จนเจอขึ้นบรรทัดใหม่แล้วแยกวิเคราะห์
// UART JSON transport: read bytes until newline, then parse and dispatch.
#include "link.h"
#include <ArduinoJson.h>

static void emit(Stream* io, JsonDocument& d) {
  // [TH] แปลงเป็นสตริง JSON แล้วส่งออกพร้อมขึ้นบรรทัดใหม่
  if (!io) return;
  String s;
  serializeJson(d, s);
  io->println(s);
}

void Link::poll() {
  // [TH] เรียกทุกลูป: สะสมไบต์เข้าบัฟเฟอร์ เจอ '\n' เมื่อไรถือว่าจบหนึ่งข้อความ
  if (!_io) return;
  while (_io->available()) {
    char c = _io->read();
    if (c == '\n') {
      if (_buf.length()) {
        _lastRx = millis();  // จับเวลารับล่าสุด ใช้เป็นตัวเฝ้าระวังสายหลุด
        dispatch(_buf);
        _buf = "";
      }
    } else if (c != '\r') {
      _buf += c;
      if (_buf.length() > 512) _buf = "";  // overflow guard — กันบัฟเฟอร์ล้น ทิ้งบรรทัดที่ยาวผิดปกติ
    }
  }
}

void Link::dispatch(const String& line) {
  JsonDocument d;
  if (deserializeJson(d, line)) return;  // malformed dropped — ข้อความเสียหายให้ทิ้ง
  const char* t = d["t"] | "";
  if (!strcmp(t, "hello")) {
    if (onHello) onHello();
  } else if (!strcmp(t, "state")) {
    if (onState) {
      LinkState s{d["running"] | false, d["phase"] | 0, d["elapsed"] | 0u,
                  (uint16_t)(d["reps"] | 0), d["wifi"] | false, d["server"] | false};
      onState(s);
    }
  } else if (!strcmp(t, "settings")) {
    LinkSettings s;
    s.ssid = (const char*)(d["ssid"] | "");
    s.pass = (const char*)(d["pass"] | "");
    s.server = (const char*)(d["server"] | "");
    s.code = (const char*)(d["code"] | "");
    s.maxTravel = d["maxtravel"] | 0;
    s.hold = d["hold"] | 0;
    s.rest = d["rest"] | 0;
    if (onSettings) onSettings(s);            // controller consumes a save — ฝั่งคอนโทรลเลอร์นำไปบันทึก
    else if (onSettingsReply) onSettingsReply(s);  // display consumes a reply — ฝั่งจอรับค่าที่ตอบกลับ
  }
  // "ping": no handler; _lastRx already stamped in poll()
  // [TH] ชนิด "ping" ไม่มีตัวจัดการ เพราะ _lastRx ถูกจับเวลาไว้แล้วใน poll()
}

void Link::sendHello() {
  JsonDocument d; d["t"] = "hello"; emit(_io, d);
}
void Link::sendPing() {
  JsonDocument d; d["t"] = "ping"; emit(_io, d);
}
void Link::sendState(const LinkState& st) {
  JsonDocument d;
  d["t"] = "state"; d["running"] = st.running; d["phase"] = st.phase;
  d["elapsed"] = st.elapsed; d["reps"] = st.reps; d["wifi"] = st.wifi;
  d["server"] = st.server;
  emit(_io, d);
}
void Link::sendSettings(const LinkSettings& s) {
  JsonDocument d;
  d["t"] = "settings"; d["ssid"] = s.ssid; d["pass"] = s.pass;
  d["server"] = s.server; d["code"] = s.code;
  d["maxtravel"] = s.maxTravel; d["hold"] = s.hold; d["rest"] = s.rest;
  emit(_io, d);
}
