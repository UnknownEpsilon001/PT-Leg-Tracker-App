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
                  (uint16_t)(d["reps"] | 0), (uint16_t)(d["target"] | 0), d["wifi"] | false,
                  d["server"] | false};
      onState(s);
    }
  } else if (!strcmp(t, "settings")) {
    LinkSettings s;
    s.ssid = (const char*)(d["ssid"] | "");
    s.pass = (const char*)(d["pass"] | "");
    s.server = (const char*)(d["server"] | "");
    s.code = (const char*)(d["code"] | "");
    s.maxTravel = d["maxtravel"] | 0;
    s.lift = d["lift"] | (uint16_t)0;
    s.lower = d["lower"] | (uint16_t)0;
    s.hold = d["hold"] | 0;
    s.rest = d["rest"] | 0;
    // 0 is a legal target ("run until STOP"), unlike lift/lower/hold/rest above,
    // so an absent field cannot decode to 0 the way theirs do — that would
    // silently switch a machine configured to stop after N reps into running
    // unlimited. LINK_TARGET_ABSENT is the out-of-range sentinel instead; see
    // link.h and link-protocol.md.
    // [TH] ค่า 0 เป็นเป้าหมายที่ถูกต้อง (หมายถึง "ทำไปเรื่อยๆ จนกว่าจะกด STOP") ต่างจาก lift/lower/hold/rest
    //      ข้างบน ฟิลด์ที่หายไปจึงถอดรหัสเป็น 0 แบบเดียวกับฟิลด์เหล่านั้นไม่ได้
    //      เพราะจะเปลี่ยนเครื่องที่ตั้งให้หยุดหลังครบ N ครั้งให้กลายเป็นทำไม่จำกัดแบบเงียบๆ
    //      จึงใช้ LINK_TARGET_ABSENT เป็นค่าบอกสถานะนอกช่วงแทน ดู link.h และ link-protocol.md
    s.target = d["target"] | LINK_TARGET_ABSENT;
    if (onSettings) onSettings(s);            // controller consumes a save — ฝั่งคอนโทรลเลอร์นำไปบันทึก
    else if (onSettingsReply) onSettingsReply(s);  // display consumes a reply — ฝั่งจอรับค่าที่ตอบกลับ
  } else if (!strcmp(t, "done")) {
    if (onDone) {
      LinkDone dn{d["elapsed"] | 0u, (uint16_t)(d["reps"] | 0),
                  d["epoch"] | 0u, d["fault"] | false};
      onDone(dn);
    }
  } else if (!strcmp(t, "target")) {
    if (onTarget) onTarget((uint16_t)(d["reps"] | 0));
    return;
  } else if (!strcmp(t, "sets")) {
    // sets always carries reps: it is a brand-new message type with no older
    // sender that might omit it, and it only ever describes a pending value
    // still on screen in Phase::Confirm, not yet committed anywhere. So an
    // absent field can only mean a malformed line, and 0 ("unlimited") is a
    // safe fallback here — unlike settings.target, where an absent field has
    // to mean "keep the stored prescription" and silently defaulting to 0
    // would turn a machine set to stop after N reps into one that doesn't.
    // See LINK_TARGET_ABSENT above.
    // [TH] sets มีฟิลด์ reps มาด้วยเสมอ เพราะเป็นข้อความชนิดใหม่ ไม่มีผู้ส่งรุ่นเก่าที่จะขาดฟิลด์นี้ได้
    //      และใช้บอกแค่ค่าที่ยังรอยืนยันค้างอยู่บนจอในสถานะ Phase::Confirm เท่านั้น ยังไม่ถูกบันทึกที่ไหน
    //      ฟิลด์นี้หายไปได้แค่กรณีข้อความเสียหาย การถอดรหัสเป็น 0 ("ไม่จำกัด") จึงปลอดภัย ต่างจาก
    //      settings.target ที่ฟิลด์หายไปต้องหมายถึง "คงค่าที่บันทึกไว้เดิม" การถอดรหัสเป็น 0 เงียบๆ
    //      จะเปลี่ยนเครื่องที่ตั้งให้หยุดหลังครบ N ครั้งให้กลายเป็นทำไม่จำกัดแบบเงียบๆ ดู LINK_TARGET_ABSENT ด้านบน
    if (onSets) onSets((uint16_t)(d["reps"] | 0));
    return;
  } else if (!strcmp(t, "confirm")) {
    // Absent/malformed "ok" defaults to false (cancel), never to a start —
    // a corrupt frame must never be misread as consent to move the leg.
    // [TH] ฟิลด์ "ok" ที่หายไปหรือข้อความเสียหายถือเป็น false (ยกเลิก) เสมอ ไม่ใช่เริ่มทำงาน
    //      เพราะข้อความที่เสียหายต้องไม่ถูกตีความว่าเป็นการยินยอมให้ขาเริ่มเคลื่อนไหว
    if (onConfirm) onConfirm(d["ok"] | false);
    return;
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
  d["elapsed"] = st.elapsed; d["reps"] = st.reps; d["target"] = st.target;
  d["wifi"] = st.wifi;
  d["server"] = st.server;
  emit(_io, d);
}
void Link::sendDone(const LinkDone& dn) {
  JsonDocument d;
  d["t"] = "done"; d["elapsed"] = dn.elapsed; d["reps"] = dn.reps;
  d["epoch"] = dn.epoch; d["fault"] = dn.fault;
  emit(_io, d);
}
void Link::sendSettings(const LinkSettings& s) {
  JsonDocument d;
  d["t"] = "settings"; d["ssid"] = s.ssid; d["pass"] = s.pass;
  d["server"] = s.server; d["code"] = s.code;
  d["maxtravel"] = s.maxTravel; d["lift"] = s.lift; d["lower"] = s.lower;
  d["hold"] = s.hold; d["rest"] = s.rest; d["target"] = s.target;
  emit(_io, d);
}
void Link::sendTarget(uint16_t reps) {
  JsonDocument d; d["t"] = "target"; d["reps"] = reps; emit(_io, d);
}
void Link::sendSets(uint16_t reps) {
  JsonDocument d; d["t"] = "sets"; d["reps"] = reps; emit(_io, d);
}
void Link::sendConfirm(bool ok) {
  JsonDocument d; d["t"] = "confirm"; d["ok"] = ok; emit(_io, d);
}
