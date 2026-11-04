#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>  // JsonDocument appears in the private sender signature

// โปรโตคอลลิงก์ระหว่างบอร์ดคอนโทรลเลอร์กับบอร์ดจอ (CYD) ผ่าน UART
// รับส่งข้อความ JSON คั่นด้วยขึ้นบรรทัดใหม่ (ดู firmware/link-protocol.md)
// UART link protocol between the controller and display boards
// (newline-delimited JSON; see firmware/link-protocol.md).

struct LinkState {  // สถานะที่คอนโทรลเลอร์ส่งให้จอแสดงผล
  bool running;     // กำลังทำงานอยู่หรือไม่
  int phase;        // หมายเลขสถานะ (ตรงกับ enum Phase)
  uint32_t elapsed; // เวลาที่ผ่านไป (วินาที)
  uint16_t reps;    // จำนวนครั้ง
  uint16_t target;  // 0 = ไม่จำกัด — the display shows reps/target when non-zero
  bool wifi;        // สถานะเชื่อมต่อ WiFi
  bool server;      // สถานะเชื่อมต่อเซิร์ฟเวอร์
  bool jog;         // panel jog mode is on — โหมดขยับเองเปิดอยู่ (absent on old images = false)
};

// LinkSettings.target's wire sentinel for "this settings message did not carry
// a target field" — a display still on an image from before the field existed.
// lift/lower/hold/rest reuse a plain 0 for the same "absent" meaning because 0
// is not a legal value for any of them, but 0 IS a legal target (run until
// STOP), so it cannot double as the sentinel here without losing that value.
// 0xFFFF is outside the field's real 0..99 range and cannot collide with it.
// See link-protocol.md.
// [TH] ค่าบอกสถานะ "ไม่มีอยู่" บนสายสำหรับ LinkSettings.target — ใช้เมื่อจอยังใช้เฟิร์มแวร์รุ่นก่อน
//      ที่ยังไม่มีฟิลด์นี้ ฟิลด์ lift/lower/hold/rest ใช้ 0 ธรรมดาแทนความหมาย "ไม่มีอยู่" ได้
//      เพราะ 0 ไม่ใช่ค่าที่ใช้ได้จริงของฟิลด์เหล่านั้น แต่ target ค่า 0 ถือเป็นค่าที่ถูกต้อง
//      (หมายถึง "ทำไปเรื่อยๆ จนกว่าจะกด STOP") จึงใช้ 0 แทนความหมาย "ไม่มีอยู่" ไม่ได้ เพราะจะเสียค่านั้นไป
//      0xFFFF อยู่นอกช่วงค่าจริง 0..99 ของฟิลด์นี้ จึงไม่มีทางชนกับค่าจริงได้ ดู link-protocol.md
static constexpr uint16_t LINK_TARGET_ABSENT = 0xFFFF;

struct LinkSettings {  // ค่าตั้งค่าที่รับส่งกันสองทางระหว่างจอกับคอนโทรลเลอร์
  String ssid, pass, server, code;
  uint16_t maxTravel, lift, lower, hold, rest, target;
};

// ส่งครั้งเดียวตอนจบรอบการทำงาน (หยุดปกติหรือขึ้น FAULT) เพื่อให้จอบันทึกลง SD
// One shot at the end of a session (clean stop or FAULT). The state message
// cannot carry this: it is a periodic snapshot, so a session that ends between
// two snapshots would be logged from stale numbers, or twice.
struct LinkDone {
  uint32_t elapsed;   // finalElapsedSec — เวลารวมของรอบที่จบไป (วินาที)
  uint16_t reps;      // finalReps — จำนวนครั้งที่ทำได้
  uint32_t epoch;     // controller RTC UTC seconds, 0 if unset — เวลาจริงจากนาฬิกา 0 = ไม่ทราบ
  bool fault;         // true if the session ended in FAULT — จบเพราะขึ้นสถานะ FAULT
};

// Retransmission of the one-shot messages. `state` and `ping` are deliberately
// excluded: another one follows in 250 ms / 1 s, so resending a stale snapshot
// is worse than dropping it. The rest are single events whose loss is silent and
// permanent — a lost `done` is a session that never reaches the SD card, a lost
// `settings` is a SAVE that did nothing, a lost `target` is a rep count the
// operator changed and the machine ignored. On the bench wiring the checksum
// rejects roughly half of all frames, which makes losing any of them a coin
// flip rather than a rare event.
// [TH] การส่งซ้ำสำหรับข้อความที่ส่งครั้งเดียว
//      ข้อความ state และ ping จงใจไม่ใช้กลไกนี้ เพราะอีก 250 มิลลิวินาที / 1 วินาทีก็มีข้อความใหม่มาแล้ว
//      การส่งซ้ำภาพสถานะเก่าจึงแย่กว่าการทิ้งไป
//      ที่เหลือเป็นเหตุการณ์ครั้งเดียวที่ถ้าหายแล้วจะเงียบและหายถาวร
//      ข้อความ done ที่หาย = รอบที่ไม่ได้ถูกบันทึกลงการ์ด
//      ข้อความ settings ที่หาย = การกดบันทึกที่ไม่มีผลอะไร
//      ข้อความ target ที่หาย = จำนวนครั้งที่ผู้ใช้เปลี่ยนแล้วเครื่องไม่รับรู้
//      บนสายที่ใช้ทดสอบ ค่าตรวจสอบปฏิเสธราวครึ่งหนึ่งของข้อความทั้งหมด
//      การหายของข้อความเหล่านี้จึงเป็นเรื่องพอๆ กับโยนหัวก้อย ไม่ใช่เรื่องนานๆ ครั้ง
// Eight, not four: started and jogexit joined done/settings/target, and on a full
// queue the overflow overwrites slot 0, which can be a session's unacked done.
// [TH] แปดช่องแทนสี่ เพราะมี started และ jogexit เพิ่มมา ถ้าคิวเต็มจะเขียนทับช่องแรก ซึ่งอาจเป็น done ที่ยังไม่ได้รับการตอบรับ
static const int LINK_TXQ = 8;         // outstanding unacked messages — ข้อความที่ยังไม่ได้รับการตอบรับ
static const uint32_t LINK_ACK_MS = 300;  // wait before resending — รอเท่านี้ก่อนส่งซ้ำ
static const uint8_t LINK_MAX_TRIES = 8;  // ~2.4 s total — รวมแล้วราว 2.4 วินาที
static const int LINK_SEEN = 8;        // recent ids remembered for de-dup — จำรหัสล่าสุดไว้กันข้อความซ้ำ

class Link {
 public:
  // Message ids start somewhere arbitrary rather than at 1. The receiver
  // remembers the last few ids to drop duplicates, and a board that reboots
  // would otherwise restart at 1 — straight into ids the peer still remembers,
  // so its first messages after a restart would be discarded as repeats.
  // [TH] รหัสข้อความเริ่มที่ค่าสุ่มๆ ไม่ใช่เริ่มที่ 1
  //      ฝั่งรับจำรหัสล่าสุดไว้ไม่กี่ตัวเพื่อทิ้งข้อความซ้ำ
  //      ถ้าบอร์ดรีบูตแล้วเริ่มนับที่ 1 ใหม่ จะไปชนกับรหัสที่อีกฝั่งยังจำได้
  //      ข้อความแรกๆ หลังรีบูตจึงจะถูกทิ้งเพราะถูกมองว่าเป็นข้อความซ้ำ
  void begin(Stream* io) {
    _io = io;
    _nextId = (uint16_t)millis();
  }
  void poll();
  void sendSettings(const LinkSettings& s);
  void sendHello();
  void sendState(const LinkState& st);
  void sendDone(const LinkDone& d);
  void sendStarted();
  void sendPing();
  void sendTarget(uint16_t reps);
  void sendSets(uint16_t reps);   // display -> controller, pending target
  void sendConfirm(bool ok);      // display -> controller, commit or cancel
  void sendJogExit();
  uint32_t lastRxMs() const { return _lastRx; }

  // Receive tallies. lastRxMs() is stamped for every complete line, before the
  // JSON is parsed, so a corrupted frame keeps the link looking alive while
  // updating nothing — which is indistinguishable from a healthy link unless the
  // outcomes are counted separately. rxLines counts lines that arrived, rxBad
  // the ones that failed to parse, rxUnknown the ones that parsed but carried a
  // type this side has no handler for.
  // [TH] ตัวนับฝั่งรับ ค่าเวลารับล่าสุดถูกบันทึกทุกบรรทัดที่มาครบ ก่อนแปลง JSON
  //      ข้อความที่เสียหายจึงทำให้ลิงก์ดูเหมือนยังดีอยู่ทั้งที่ไม่มีอะไรถูกอัปเดต
  //      ซึ่งแยกจากลิงก์ที่ปกติไม่ได้เลยถ้าไม่นับผลลัพธ์แยกกัน
  //      rxLines นับบรรทัดที่เข้ามา rxBad นับบรรทัดที่แปลงไม่ผ่าน
  //      rxUnknown นับบรรทัดที่แปลงผ่านแต่เป็นชนิดที่ฝั่งนี้ไม่มีตัวรับ
  uint32_t rxLines = 0;
  uint32_t rxBad = 0;
  uint32_t rxUnknown = 0;
  // Frames that arrived with junk in front of the payload and were recovered by
  // trimming to the first '{'. Kept visible because a rising count means the RX
  // line is floating or noisy — the recovery works, but the wiring wants fixing.
  // [TH] ข้อความที่มีขยะนำหน้าแล้วกู้คืนได้ด้วยการตัดถึงวงเล็บปีกกาแรก
  //      แสดงค่าไว้เพราะถ้าตัวเลขนี้เพิ่มขึ้นแปลว่าขารับลอยหรือมีสัญญาณรบกวน
  //      การกู้คืนทำงานได้ก็จริง แต่การเดินสายยังควรแก้
  uint32_t rxTrimmed = 0;
  // Frames rejected by the checksum, and frames that carried none at all. The
  // second one is the mixed-firmware case: both boards are flashed separately,
  // so an old image on either side sends unchecksummed lines this one drops.
  // A steady rxNoCrc with a dead link means "flash the other board too".
  // [TH] ข้อความที่ค่าตรวจสอบไม่ตรง และข้อความที่ไม่มีค่าตรวจสอบมาเลย
  //      อย่างหลังคือกรณีเฟิร์มแวร์คนละรุ่น เพราะสองบอร์ดแฟลชแยกกัน
  //      ถ้าฝั่งใดยังเป็นรุ่นเก่าจะส่งบรรทัดที่ไม่มีค่าตรวจสอบ ซึ่งฝั่งนี้จะทิ้ง
  //      ถ้า rxNoCrc เพิ่มเรื่อยๆ และลิงก์ไม่ทำงาน แปลว่าต้องแฟลชอีกบอร์ดด้วย
  uint32_t rxCrcBad = 0;
  uint32_t rxNoCrc = 0;
  // The most recent line that failed to parse, kept so a bench build can print
  // what actually arrived. Counting failures says the frames are broken; only
  // the bytes say whether they are noise, truncated, or split.
  // [TH] บรรทัดล่าสุดที่แปลงไม่ผ่าน เก็บไว้ให้โหมดทดสอบพิมพ์ออกมาดูได้
  //      ตัวนับบอกได้แค่ว่าข้อความเสีย แต่ตัวไบต์เท่านั้นที่บอกว่าเป็นสัญญาณรบกวน ถูกตัดสั้น หรือถูกแบ่งท่อน
  String rxLastBad;

  // [TH] คอลแบ็กแยกตามฝั่ง: บางตัวใช้ฝั่งคอนโทรลเลอร์ บางตัวใช้ฝั่งจอ
  void (*onSettings)(const LinkSettings&) = nullptr;   // controller side — คอนโทรลเลอร์รับค่าตั้งค่าเพื่อบันทึก
  void (*onHello)() = nullptr;                         // controller side — จอทักทาย ขอค่าปัจจุบัน
  void (*onState)(const LinkState&) = nullptr;         // display side — จอรับสถานะมาแสดง
  void (*onSettingsReply)(const LinkSettings&) = nullptr; // display side — จอรับค่าตั้งค่าที่คอนโทรลเลอร์ตอบกลับ
  void (*onDone)(const LinkDone&) = nullptr;           // display side — จอรับผลสรุปตอนจบรอบไปบันทึก
  void (*onStarted)() = nullptr;                       // display side — จอรับสัญญาณว่ารอบเพิ่งเริ่ม (เสียงเริ่ม)
  // display -> controller, applied live. Kept off the `settings` path on purpose:
  // that one commits NVS and restarts Wi-Fi, so it is deferred until a session
  // ends, and a rep target the operator is turning right now cannot wait for that.
  // [TH] จอส่งให้คอนโทรลเลอร์ นำไปใช้ทันที จงใจไม่ใช้เส้นทาง settings
  //      เพราะเส้นทางนั้นบันทึก NVS และรีสตาร์ต WiFi จึงถูกเลื่อนไปจนกว่ารอบจะจบ
  //      แต่เป้าหมายจำนวนครั้งที่ผู้ใช้กำลังหมุนอยู่รอแบบนั้นไม่ได้
  void (*onTarget)(uint16_t) = nullptr;  // controller side
  void (*onSets)(uint16_t) = nullptr;    // controller side
  void (*onConfirm)(bool) = nullptr;     // controller side
  void (*onJogExit)() = nullptr;         // controller side — ปุ่มออกจากโหมดขยับเองบนจอ

  // Retransmission tallies. txRetries counts resends, txDropped the messages
  // that were never acknowledged inside LINK_MAX_TRIES and are gone for good —
  // a non-zero txDropped means a session row or a settings save was actually
  // lost, which is worth surfacing rather than hiding. rxDupes counts messages
  // received more than once because our ack did not get through; they are
  // acknowledged again and otherwise ignored.
  // [TH] ตัวนับการส่งซ้ำ txRetries นับจำนวนครั้งที่ส่งซ้ำ
  //      txDropped นับข้อความที่ไม่ได้รับการตอบรับจนครบจำนวนครั้งที่กำหนดและหายไปจริงๆ
  //      ถ้า txDropped ไม่เป็นศูนย์แปลว่ามีแถวข้อมูลรอบหรือการบันทึกค่าตั้งค่าหายไปจริง ควรแสดงให้เห็น ไม่ใช่ซ่อนไว้
  //      rxDupes นับข้อความที่ได้รับซ้ำเพราะการตอบรับของเราไปไม่ถึง
  //      ข้อความพวกนั้นจะถูกตอบรับอีกครั้งแล้วไม่ทำอะไรต่อ
  uint32_t txRetries = 0;
  uint32_t txDropped = 0;
  uint32_t rxDupes = 0;

 private:
  void dispatch(const String& line);
  void emitRaw(const String& line);
  void sendReliable(JsonDocument& d);
  bool seenBefore(uint16_t id);
  Stream* _io = nullptr;
  String _buf;
  uint32_t _lastRx = 0;

  struct PendingTx {
    String line;      // the framed line, checksum included — บรรทัดที่ประกอบเสร็จแล้วรวมค่าตรวจสอบ
    uint16_t id;      // its message id — รหัสข้อความ
    uint32_t nextMs;  // when to resend — เวลาที่จะส่งซ้ำ
    uint8_t tries;    // sends so far, first included — จำนวนครั้งที่ส่งไปแล้ว รวมครั้งแรก
    bool used;
  };
  PendingTx _txq[LINK_TXQ];
  uint16_t _nextId = 0;
  uint16_t _seen[LINK_SEEN] = {0};
  uint8_t _seenIdx = 0;
};
