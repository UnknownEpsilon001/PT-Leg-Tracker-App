// การรับส่งข้อความ JSON บนสาย UART: อ่านทีละไบต์จนเจอขึ้นบรรทัดใหม่แล้วแยกวิเคราะห์
// UART JSON transport: read bytes until newline, then parse and dispatch.
#include "link.h"
#include <ArduinoJson.h>

// CRC-16/CCITT-FALSE over the JSON text, sent as four hex digits after a '*'.
//
// A bare UART has no integrity check of its own: a flipped bit arrives as a
// different byte and nothing notices. That is not theoretical here — a bit flip
// inside a key name turned `"target"` into `"targep"` on the bench, which still
// parsed as valid JSON, so the missing key fell back to its default and the
// display applied "not running, 0 seconds" for that frame. The screen flicked to
// idle and back. A checksum turns that silent corruption into a detected drop:
// one skipped update instead of a wrong one.
// [TH] ค่าตรวจสอบความถูกต้องแบบ CRC-16/CCITT-FALSE คำนวณจากข้อความ JSON
//      ส่งต่อท้ายด้วยเครื่องหมาย * แล้วตามด้วยเลขฐานสิบหกสี่หลัก
//      สาย UART เปล่าๆ ไม่มีการตรวจสอบความถูกต้องในตัว บิตที่เพี้ยนจะกลายเป็นไบต์อื่นโดยไม่มีใครรู้
//      เรื่องนี้เกิดขึ้นจริงบนโต๊ะทดสอบ บิตที่เพี้ยนในชื่อฟิลด์ทำให้ "target" กลายเป็น "targep"
//      ซึ่งยังเป็น JSON ที่ถูกต้อง ฟิลด์ที่หายไปจึงถูกแทนด้วยค่าปริยาย
//      จอเลยแสดงว่า "ไม่ได้ทำงาน 0 วินาที" ในเฟรมนั้น หน้าจอจึงกระพริบกลับไปหน้าว่างแล้วกลับมา
//      การใส่ค่าตรวจสอบเปลี่ยนความเสียหายที่เงียบให้กลายเป็นการทิ้งที่ตรวจจับได้
//      คือข้ามการอัปเดตหนึ่งครั้ง ดีกว่าอัปเดตด้วยค่าที่ผิด
static uint16_t crc16(const char* p, size_t n) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < n; i++) {
    crc ^= (uint16_t)(uint8_t)p[i] << 8;
    for (int b = 0; b < 8; b++) crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
  }
  return crc;
}

// [TH] ประกอบเป็นบรรทัด: ข้อความ JSON ต่อท้ายด้วยค่าตรวจสอบ
static String frame(JsonDocument& d) {
  String s;
  serializeJson(d, s);
  char tail[8];
  snprintf(tail, sizeof(tail), "*%04X", crc16(s.c_str(), s.length()));
  s += tail;
  return s;
}

static void emit(Stream* io, JsonDocument& d) {
  // [TH] ส่งแบบไม่รอการตอบรับ ใช้กับ state และ ping
  if (!io) return;
  io->println(frame(d));
}

void Link::emitRaw(const String& line) {
  if (_io) _io->println(line);
}

// Sent with an id and held until the other side acknowledges it.
// [TH] ส่งพร้อมรหัสข้อความ และเก็บไว้จนกว่าอีกฝั่งจะตอบรับ
void Link::sendReliable(JsonDocument& d) {
  uint16_t id = ++_nextId;
  if (id == 0) id = ++_nextId;  // 0 means "no id" on the wire — ค่า 0 หมายถึงไม่มีรหัส
  d["id"] = id;
  String line = frame(d);
  emitRaw(line);
  for (int i = 0; i < LINK_TXQ; i++) {
    if (!_txq[i].used) {
      _txq[i] = {line, id, millis() + LINK_ACK_MS, 1, true};
      return;
    }
  }
  // Queue full: the oldest slot loses its place rather than the newest never
  // being sent. Four outstanding one-shot messages already means the link is in
  // trouble, and the newest is the one whose loss the operator would notice.
  // [TH] คิวเต็ม ให้ช่องที่เก่าที่สุดเสียที่ไป ดีกว่าปล่อยให้ข้อความใหม่สุดไม่ได้ส่งเลย
  //      การมีข้อความค้างสี่ข้อความแปลว่าลิงก์มีปัญหาอยู่แล้ว
  //      และข้อความใหม่สุดคือข้อความที่ผู้ใช้จะสังเกตเห็นถ้ามันหายไป
  txDropped++;
  _txq[0] = {line, id, millis() + LINK_ACK_MS, 1, true};
}

// [TH] เคยเห็นรหัสนี้มาก่อนไหม ใช้กันการทำงานซ้ำเมื่ออีกฝั่งส่งซ้ำเพราะไม่ได้รับการตอบรับ
bool Link::seenBefore(uint16_t id) {
  for (int i = 0; i < LINK_SEEN; i++)
    if (_seen[i] == id) return true;
  _seen[_seenIdx] = id;
  _seenIdx = (uint8_t)((_seenIdx + 1) % LINK_SEEN);
  return false;
}

void Link::poll() {
  // [TH] เรียกทุกลูป: สะสมไบต์เข้าบัฟเฟอร์ เจอ '\n' เมื่อไรถือว่าจบหนึ่งข้อความ
  if (!_io) return;
  while (_io->available()) {
    char c = _io->read();
    if (c == '\n') {
      if (_buf.length()) {
        _lastRx = millis();  // จับเวลารับล่าสุด ใช้เป็นตัวเฝ้าระวังสายหลุด
        rxLines++;
        dispatch(_buf);
        _buf = "";
      }
    } else if (c != '\r') {
      _buf += c;
      if (_buf.length() > 512) _buf = "";  // overflow guard — กันบัฟเฟอร์ล้น ทิ้งบรรทัดที่ยาวผิดปกติ
    }
  }

  // Resend anything still unacknowledged. Signed compare so the millis() wrap at
  // 49 days does not park a message for a month.
  // [TH] ส่งซ้ำข้อความที่ยังไม่ได้รับการตอบรับ
  //      เทียบแบบมีเครื่องหมาย เวลาที่วนกลับที่ 49 วันจะได้ไม่ทำให้ข้อความค้างเป็นเดือน
  uint32_t now = millis();
  for (int i = 0; i < LINK_TXQ; i++) {
    PendingTx& p = _txq[i];
    if (!p.used || (int32_t)(now - p.nextMs) < 0) continue;
    if (p.tries >= LINK_MAX_TRIES) {
      // Given up. Counted rather than retried forever: a link that has swallowed
      // eight copies over 2.4 s is down, and holding the slot would block the
      // messages that follow.
      // [TH] เลิกส่ง นับไว้แทนการส่งซ้ำไม่รู้จบ
      //      ลิงก์ที่กลืนข้อความไปแปดครั้งใน 2.4 วินาทีถือว่าใช้งานไม่ได้
      //      และการถือช่องไว้จะไปขวางข้อความถัดๆ ไป
      p.used = false;
      txDropped++;
      continue;
    }
    emitRaw(p.line);
    p.tries++;
    p.nextMs = now + LINK_ACK_MS;
    txRetries++;
  }
}

void Link::dispatch(const String& line) {
  // Anything before the first '{' is not ours. A UART RX pin sits floating
  // whenever the other board is not transmitting, and noise on a floating line
  // fakes start bits, so the receiver prepends a run of 0xFF bytes to the real
  // message. Measured on the bench: 25-32 junk bytes in front of an otherwise
  // perfect frame, which made deserializeJson reject the whole line — the
  // display showed an idle screen through an entire running session because not
  // one state message survived. The payload was never the problem, so find where
  // it starts and parse from there.
  // [TH] ไบต์ที่อยู่ก่อนวงเล็บปีกกาแรกไม่ใช่ของเรา
  //      ขารับของ UART จะลอยอยู่ตอนที่อีกฝั่งไม่ได้ส่ง สัญญาณรบกวนบนขาที่ลอยทำให้เกิดบิตเริ่มปลอม
  //      ตัวรับจึงได้ไบต์ 0xFF ต่อหน้าข้อความจริง
  //      วัดได้จากการทดสอบ: มีไบต์ขยะ 25-32 ตัวนำหน้าข้อความที่สมบูรณ์ดี
  //      ทำให้การแปลง JSON ทิ้งทั้งบรรทัด จอจึงค้างอยู่ที่หน้าจอว่างตลอดรอบที่เครื่องกำลังทำงาน
  //      เพราะไม่มีข้อความสถานะรอดสักข้อความเดียว ตัวเนื้อข้อความไม่เคยเสีย
  //      จึงหาจุดเริ่มของมันแล้วแปลงจากตรงนั้น
  int start = line.indexOf('{');
  if (start < 0) {  // no payload at all — ไม่มีเนื้อข้อความเลย
    rxBad++;
    rxLastBad = line.substring(0, 120);
    return;
  }
  if (start > 0) rxTrimmed++;
  // The checksum is the last '*' onward. Searched from the end because the JSON
  // itself may legally contain a '*' inside a string — an SSID or a password can
  // hold any character.
  // [TH] ค่าตรวจสอบคือส่วนที่อยู่หลังเครื่องหมาย * ตัวสุดท้าย
  //      ค้นจากท้ายเพราะตัว JSON เองมีเครื่องหมาย * อยู่ในข้อความได้
  //      ชื่อไวไฟหรือรหัสผ่านใส่อักขระอะไรก็ได้
  int star = line.lastIndexOf('*');
  if (star < start) {  // unchecksummed line: the other board runs an older image
                       // [TH] บรรทัดที่ไม่มีค่าตรวจสอบ แปลว่าอีกฝั่งยังเป็นเฟิร์มแวร์รุ่นเก่า
    rxNoCrc++;
    rxLastBad = line.substring(0, 120);
    return;
  }
  String payload = line.substring(start, star);
  uint16_t want = (uint16_t)strtoul(line.c_str() + star + 1, nullptr, 16);
  if (crc16(payload.c_str(), payload.length()) != want) {
    rxCrcBad++;
    rxLastBad = line.substring(0, 120);
    return;
  }
  JsonDocument d;
  if (deserializeJson(d, payload)) {  // malformed dropped — ข้อความเสียหายให้ทิ้ง
    rxBad++;
    rxLastBad = line.substring(0, 120);
    return;
  }
  const char* t = d["t"] | "";

  // An ack clears the message it names and nothing else.
  // [TH] ข้อความตอบรับมีหน้าที่เดียวคือเคลียร์ข้อความที่มันอ้างถึง
  if (!strcmp(t, "ack")) {
    uint16_t id = d["id"] | 0;
    for (int i = 0; i < LINK_TXQ; i++)
      if (_txq[i].used && _txq[i].id == id) _txq[i].used = false;
    return;
  }

  // Anything carrying an id wants one. Acknowledged before the duplicate check,
  // deliberately: a repeat means our previous ack was the thing that got lost,
  // so staying silent would keep the sender retrying until it gives up.
  // [TH] ข้อความที่มีรหัสมาด้วยต้องการการตอบรับ
  //      ตอบรับก่อนตรวจว่าซ้ำหรือไม่ ซึ่งเป็นความตั้งใจ
  //      ข้อความที่มาซ้ำแปลว่าการตอบรับครั้งก่อนของเราคือสิ่งที่หายไป
  //      ถ้าเงียบไว้ ฝั่งส่งจะส่งซ้ำไปเรื่อยๆ จนเลิกส่งไปเอง
  uint16_t inId = d["id"] | 0;
  if (inId) {
    JsonDocument a;
    a["t"] = "ack";
    a["id"] = inId;
    emitRaw(frame(a));
    if (seenBefore(inId)) {  // already acted on — ทำไปแล้ว
      rxDupes++;
      return;
    }
  }

  if (!strcmp(t, "hello")) {
    if (onHello) onHello();
  } else if (!strcmp(t, "state")) {
    // Every field must be present. ArduinoJson's `| default` turns an absent key
    // into a plausible value, which for this message means a damaged frame is
    // applied as "not running, 0 seconds" — the display drops to its idle screen
    // for one frame and comes back, which is what the flicker was. The sender
    // always writes all seven, so a missing one is corruption, not an older
    // image, and belongs in the reject count.
    // [TH] ต้องมีครบทุกฟิลด์ ค่าปริยายของไลบรารี JSON จะเปลี่ยนฟิลด์ที่หายไปให้เป็นค่าที่ดูสมเหตุสมผล
    //      ซึ่งกับข้อความนี้แปลว่าเฟรมที่เสียหายจะถูกใช้เป็น "ไม่ได้ทำงาน 0 วินาที"
    //      จอจะตกไปหน้าว่างหนึ่งเฟรมแล้วกลับมา ซึ่งคืออาการกระพริบที่เจอ
    //      ฝั่งส่งเขียนครบทั้งเจ็ดฟิลด์เสมอ ฟิลด์ที่ขาดจึงคือความเสียหาย ไม่ใช่เฟิร์มแวร์รุ่นเก่า
    //      จึงนับเป็นข้อความที่ถูกปฏิเสธ
    if (d["running"].isNull() || d["phase"].isNull() || d["elapsed"].isNull() ||
        d["reps"].isNull() || d["target"].isNull()) {
      rxBad++;
      rxLastBad = line.substring(0, 120);
      return;
    }
    if (onState) {
      LinkState s{d["running"] | false, d["phase"] | 0, d["elapsed"] | 0u,
                  (uint16_t)(d["reps"] | 0), (uint16_t)(d["target"] | 0), d["wifi"] | false,
                  d["server"] | false, d["jog"] | false};
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
  } else if (!strcmp(t, "started")) {
    if (onStarted) onStarted();
  } else if (!strcmp(t, "jogexit")) {
    if (onJogExit) onJogExit();
  } else if (!strcmp(t, "target")) {
    if (onTarget) onTarget((uint16_t)(d["reps"] | 0));
    return;
  } else if (strcmp(t, "ping")) {
    // Parsed, but the type is not one this side handles — counted rather than
    // dropped silently, because a frame arriving with an unexpected type looks
    // exactly like a healthy link that updates nothing.
    // [TH] แปลงผ่านแต่เป็นชนิดที่ฝั่งนี้ไม่มีตัวรับ จึงนับไว้แทนที่จะทิ้งเงียบๆ
    //      เพราะข้อความชนิดที่ไม่คาดคิดจะดูเหมือนลิงก์ปกติที่ไม่อัปเดตอะไรเลย
    rxUnknown++;
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
  d["jog"] = st.jog;
  emit(_io, d);
}
// The four below go out reliably: each is a single event, and each has a
// consequence that is silent when it is lost. `done` is the session's only route
// to the SD card, `started` is the start cue, `settings` is a SAVE the operator
// watched succeed, `target` is a number they just turned.
// [TH] สี่ข้อความด้านล่างส่งแบบรอการตอบรับ เพราะแต่ละอันเป็นเหตุการณ์ครั้งเดียว
//      และถ้าหายไปจะไม่มีอะไรฟ้อง ข้อความ done คือทางเดียวที่รอบจะถูกบันทึกลงการ์ด
//      ข้อความ started คือเสียงเริ่มการทำงาน
//      ข้อความ settings คือการกดบันทึกที่ผู้ใช้เห็นว่าสำเร็จ
//      ข้อความ target คือตัวเลขที่ผู้ใช้เพิ่งปรับ
void Link::sendDone(const LinkDone& dn) {
  JsonDocument d;
  d["t"] = "done"; d["elapsed"] = dn.elapsed; d["reps"] = dn.reps;
  d["epoch"] = dn.epoch; d["fault"] = dn.fault;
  sendReliable(d);
}
void Link::sendStarted() {
  JsonDocument d; d["t"] = "started"; sendReliable(d);
}
void Link::sendSettings(const LinkSettings& s) {
  JsonDocument d;
  d["t"] = "settings"; d["ssid"] = s.ssid; d["pass"] = s.pass;
  d["server"] = s.server; d["code"] = s.code;
  d["maxtravel"] = s.maxTravel; d["lift"] = s.lift; d["lower"] = s.lower;
  d["hold"] = s.hold; d["rest"] = s.rest; d["target"] = s.target;
  sendReliable(d);
}
void Link::sendTarget(uint16_t reps) {
  JsonDocument d; d["t"] = "target"; d["reps"] = reps; sendReliable(d);
}
// The on-screen jog exit. Reliable for the same reason as the rest: a lost tap
// is a button that did nothing.
// [TH] ปุ่มออกจากโหมดขยับเองบนจอ ส่งแบบรอการตอบรับ เพราะการแตะที่หายไปคือปุ่มที่กดแล้วไม่เกิดอะไร
void Link::sendJogExit() {
  JsonDocument d; d["t"] = "jogexit"; sendReliable(d);
}
