// กันเด้งปุ่มแบบเดียวกับ switches.cpp แล้วยกธง "เพิ่งกด" หนึ่งครั้งเมื่อค่านิ่งเปลี่ยนจากไม่กด->กด
// Debounce like switches.cpp, then raise a one-shot "just pressed" flag when the
// stable state transitions not-pressed -> pressed.
#include "buttons.h"
#include <Arduino.h>
#include "config.h"

struct Btn {
  uint8_t pin;
  bool nc;          // normally-closed contact — หน้าสัมผัสแบบปกติปิด
  bool stable;      // debounced logical pressed — ค่าที่ผ่านกันเด้งแล้ว
  bool lastRaw;     // last raw logical read — ค่าดิบครั้งก่อน
  uint32_t changed; // ms of last raw change — เวลาที่ค่าดิบเปลี่ยนล่าสุด
  bool edge;        // one-shot press edge, cleared on read — ขอบกดครั้งเดียว อ่านแล้วล้าง
  // Set by the interrupt only for a press it TIMED at full debounce width, so a
  // press that began and ended entirely inside one blocked loop iteration is not
  // lost — and a microsecond glitch is still not a press.
  // [TH] อินเทอร์รัปต์จะตั้งค่านี้เฉพาะการกดที่จับเวลาได้ครบตามเกณฑ์กันเด้งเท่านั้น
  //      การกดที่เริ่มและจบภายในลูปรอบเดียวที่ถูกบล็อกอยู่จึงไม่หาย
  //      และสัญญาณรบกวนระดับไมโครวินาทีก็ยังไม่ถูกนับว่าเป็นการกด
  volatile bool isrPressed;
  volatile bool isrDown;       // a press is in progress — กำลังกดอยู่
  volatile uint32_t isrDownMs; // when it started — เวลาที่เริ่มกด
  bool isrMayEdge;   // may the latch raise an edge? STOP only — ให้ยกขอบสัญญาณได้ไหม เฉพาะปุ่มหยุด
  bool bootPressed;  // read as pressed at power-up — อ่านได้ว่ากดอยู่ตั้งแต่เปิดเครื่อง
};

//                pin, nc, stable, lastRaw, changed, edge, isrPressed, isrDown, isrDownMs,
//                isrMayEdge, bootPressed
static Btn bStart{PIN_BTN_START, BTN_START_NC, false, false, 0, false, false, false, 0,
                  false, false};
static Btn bStop{PIN_BTN_STOP, BTN_STOP_NC, false, false, 0, false, false, false, 0,
                 true, false};
static Btn bReset{PIN_BTN_RESET, BTN_RESET_NC, false, false, 0, false, false, false, 0,
                  false, false};

// Polarity is per button now, so it is a runtime field rather than a #ifdef: the
// three can differ, and mixed panels are the common case.
// [TH] ขั้วของแต่ละปุ่มแยกกัน จึงเก็บเป็นค่าในโครงสร้างแทนการใช้ #ifdef
static bool rawPressed(uint8_t pin, bool nc) {
  int level = digitalRead(pin);
  // NC: pressed (or a broken wire) opens the contact and the pullup reads HIGH.
  // NO to GND: pressed closes it and reads LOW.
  // [TH] NC: กด (หรือสายขาด) = วงจรเปิด = HIGH ; NO ลง GND: กด = LOW
  return nc ? (level == HIGH) : (level == LOW);
}

// Runs on a pin change, and TIMES the press rather than just noticing one.
//
// Latching on the level alone would be a debounce bypass, and on this panel that
// is dangerous rather than merely sloppy: every button is NC with a pullup, so
// "pressed" IS the open-circuit state, and a loose spade lug chattering open for
// microseconds on the START line would read as a press and lift a patient's leg
// with nobody at the panel. So the interrupt measures the gap between the make
// and the break, and only a press that lasted the full debounce window is handed
// to the loop — the same bar the polling path applies, just measured by hardware
// timestamps instead of by sampling.
// [TH] ทำงานเมื่อระดับขาเปลี่ยน และจับเวลาความยาวของการกด ไม่ใช่แค่รู้ว่ามีการกด
//      ถ้าตัดสินจากระดับสัญญาณอย่างเดียวจะเป็นการข้ามกลไกกันเด้ง ซึ่งบนหน้าปัดชุดนี้อันตราย ไม่ใช่แค่หละหลวม
//      ปุ่มทุกตัวเป็นแบบปกติปิดและมีตัวต้านทานดึงขึ้น สถานะ "กด" จึงคือสถานะวงจรเปิด
//      ขั้วต่อที่หลวมและกระพริบเปิดเพียงไมโครวินาทีบนสาย START จะถูกอ่านว่าเป็นการกด
//      แล้วยกขาผู้ป่วยขึ้นโดยไม่มีใครอยู่ที่หน้าปัด
//      อินเทอร์รัปต์จึงวัดช่วงเวลาระหว่างจังหวะกดกับจังหวะปล่อย
//      และส่งให้ลูปหลักเฉพาะการกดที่ยาวครบเกณฑ์กันเด้งเท่านั้น
//      ซึ่งเป็นเกณฑ์เดียวกับการอ่านตามรอบ ต่างกันแค่วัดด้วยเวลาจากฮาร์ดแวร์แทนการสุ่มอ่าน
static void IRAM_ATTR btnIsr(void* arg) {
  Btn* b = (Btn*)arg;
  uint32_t now = millis();
  if (rawPressed(b->pin, b->nc)) {
    b->isrDownMs = now;
    b->isrDown = true;
    return;
  }
  if (!b->isrDown) return;
  b->isrDown = false;
  if (now - b->isrDownMs >= BUTTON_DEBOUNCE_MS) b->isrPressed = true;
}

static void poll(Btn& b, uint32_t nowMs) {
  bool raw = rawPressed(b.pin, b.nc);

  // A press the polling missed entirely. Levels are only sampled when this runs,
  // and one loop iteration can block for most of a second inside a network call
  // with an unreachable server — long enough for a normal 300 ms press to begin
  // and end unseen, leaving `raw` back at rest and no edge at all. A STOP that
  // never happened is the worst version of that, so the interrupt latches the
  // press and it is consumed here. Both flag and edge are one-shot, and the
  // machine treats a repeated STOP or a start it is already running as a no-op,
  // so contact bounce costs nothing.
  // [TH] การกดที่การอ่านตามรอบพลาดไปทั้งครั้ง ระดับขาถูกอ่านเฉพาะตอนที่ฟังก์ชันนี้ทำงาน
  //      และลูปหนึ่งรอบอาจค้างเกือบทั้งวินาทีในการเรียกเครือข่ายตอนที่เซิร์ฟเวอร์ติดต่อไม่ได้
  //      ซึ่งนานพอให้การกดปกติราว 300 มิลลิวินาทีเริ่มและจบโดยไม่มีใครเห็น
  //      ค่า raw กลับมาอยู่ที่ปกติและไม่เกิดขอบสัญญาณเลย
  //      การกด STOP ที่ไม่เคยเกิดขึ้นคือกรณีที่แย่ที่สุด อินเทอร์รัปต์จึงจับการกดนั้นค้างไว้และมาอ่านที่นี่
  //      ทั้งธงและขอบสัญญาณอ่านได้ครั้งเดียว และเครื่องถือว่าการกด STOP ซ้ำ
  //      หรือการสั่งเริ่มทั้งที่ทำงานอยู่แล้วไม่มีผลอะไร หน้าสัมผัสเด้งจึงไม่เสียหาย
  // Always consumed, so a press the polling DID see cannot be reported twice: the
  // interrupt times every press, including the ones the loop was awake for, and
  // those have already produced their edge below.
  // [TH] อ่านทิ้งทุกครั้ง การกดที่การอ่านตามรอบเห็นอยู่แล้วจะได้ไม่ถูกรายงานซ้ำ
  //      อินเทอร์รัปต์จับเวลาการกดทุกครั้งรวมถึงครั้งที่ลูปยังตื่นอยู่ ซึ่งได้สร้างขอบสัญญาณไว้ด้านล่างแล้ว
  // Only STOP is allowed to raise an edge from the interrupt, and the asymmetry
  // is the point. The failure this latch exists for is a STOP press lost inside a
  // blocked loop iteration; a lost START just means pressing it again, while a
  // FALSE start lifts a patient's leg with nobody at the panel. The interrupt is
  // not IRAM-registered on this core, so it is masked while the flash cache is
  // off and one pending edge is delivered late, timed against the level at
  // delivery — which can inflate a glitch into a full-width "press". Wiring that
  // path to START would put a self-start behind an electrical transient, so it is
  // not wired there. A spurious STOP costs a safe-lower; that is the trade.
  // [TH] ให้เฉพาะปุ่ม STOP เท่านั้นที่สร้างขอบสัญญาณจากอินเทอร์รัปต์ได้ และความไม่สมมาตรนี้คือประเด็นสำคัญ
  //      สิ่งที่กลไกนี้มีไว้แก้คือการกด STOP ที่หายไปในลูปรอบที่ถูกบล็อก
  //      การกด START ที่หายไปแค่กดใหม่ก็จบ แต่การเริ่มทำงานผิดพลาดคือการยกขาผู้ป่วยโดยไม่มีใครอยู่ที่หน้าปัด
  //      อินเทอร์รัปต์บนคอร์นี้ไม่ได้ลงทะเบียนใน IRAM จึงถูกปิดกั้นระหว่างที่แคชแฟลชถูกปิด
  //      ขอบที่ค้างอยู่หนึ่งครั้งจะถูกส่งช้าและจับเวลาเทียบกับระดับสัญญาณ ณ เวลาที่ส่ง
  //      ซึ่งอาจทำให้สัญญาณรบกวนกลายเป็นการกดที่ยาวเต็มเกณฑ์
  //      ถ้าต่อเส้นทางนี้เข้ากับปุ่ม START จะเท่ากับเอาการเริ่มทำงานเองไปผูกกับสัญญาณรบกวนทางไฟฟ้า จึงไม่ต่อ
  //      การกด STOP ที่เกินมาแลกด้วยการลดขาลงหนึ่งครั้ง นั่นคือสิ่งที่ยอมแลก
  if (b.isrPressed) {
    b.isrPressed = false;
    if (b.isrMayEdge && !b.stable) b.edge = true;  // a full-width press the loop
                                                   // never saw
                                                   // [TH] การกดที่ยาวครบเกณฑ์ซึ่งลูปไม่เคยเห็น
  }

  if (raw != b.lastRaw) {
    b.lastRaw = raw;
    b.changed = nowMs;
  } else if (nowMs - b.changed >= BUTTON_DEBOUNCE_MS && raw != b.stable) {
    b.stable = raw;
    if (raw) b.edge = true;  // rising: not-pressed -> pressed — ขอบขาขึ้น: จากไม่กดเป็นกด
    // A line that came up already reading pressed has finally moved, so it was a
    // held button and not a broken wire. Clear the suspicion either way.
    // [TH] ขาที่ตอนเปิดเครื่องอ่านได้ว่ากดอยู่ ตอนนี้เปลี่ยนค่าแล้ว
    //      แปลว่าเป็นปุ่มที่ถูกกดค้าง ไม่ใช่สายขาด จึงล้างข้อสงสัยทิ้งทั้งสองกรณี
    b.bootPressed = false;
  }
}

static bool takeEdge(Btn& b) {
  bool e = b.edge;
  b.edge = false;
  return e;
}

// Seeded from the pins, not from `false`. The structs used to start as
// not-pressed, so a line already reading pressed at power-up looked like a
// transition and raised a press edge about one debounce later. START is NC, so
// an unplugged panel or a cut wire reads pressed forever — the machine would
// begin lifting a leg roughly 25 ms after power-up with nobody touching it.
// Taking the boot state as the baseline means a press has to be a release-then-
// press seen after startup, which a broken wire can never produce.
// [TH] ตั้งค่าเริ่มต้นจากค่าที่อ่านได้จริงจากขา ไม่ใช่กำหนดเป็น "ไม่ได้กด"
//      เดิมโครงสร้างเริ่มที่ไม่ได้กด ถ้าตอนเปิดเครื่องขาใดอ่านได้ว่ากดอยู่ จะถูกมองว่าเพิ่งเปลี่ยนสถานะ
//      แล้วเกิดขอบกดขึ้นหลังพ้นเวลากันเด้ง ปุ่ม START เป็นแบบปกติปิด สายที่ขาดหรือยังไม่ได้เสียบจะอ่านว่ากดค้าง
//      เครื่องจะเริ่มยกขาผู้ป่วยเองราว 25 มิลลิวินาทีหลังเปิดเครื่องโดยไม่มีใครแตะปุ่ม
//      การยึดสถานะตอนเปิดเครื่องเป็นค่าตั้งต้น ทำให้ต้องมีการปล่อยแล้วกดใหม่เท่านั้นจึงนับเป็นการกด
//      ซึ่งสายที่ขาดอยู่ทำไม่ได้
// The flip side of seeding: taking the boot state as the baseline also means a
// line that is ALREADY open at power-up — an NC button whose wire is broken or
// whose connector was never seated — seeds as pressed and can never produce an
// edge again, because an edge needs a not-pressed -> pressed transition and the
// line never leaves pressed. A broken STOP wire would then leave STOP silently
// dead for the whole power cycle while START and RESET worked normally. Which
// buttons came up pressed is therefore recorded, and the sketch refuses to start
// a session while STOP is one of them. Releasing a genuinely held button clears
// it (see poll); a cut wire never does.
// [TH] อีกด้านหนึ่งของการยึดสถานะตอนเปิดเครื่องเป็นค่าตั้งต้น คือสายที่ขาดอยู่แล้วตั้งแต่เปิดเครื่อง
//      เช่นปุ่มแบบปกติปิดที่สายขาดหรือขั้วต่อไม่ได้เสียบ จะถูกตั้งต้นว่ากดอยู่และจะไม่เกิดขอบสัญญาณได้อีกเลย
//      เพราะขอบสัญญาณต้องเปลี่ยนจากไม่กดเป็นกด แต่ขานั้นไม่เคยออกจากสถานะกดเลย
//      สาย STOP ที่ขาดจะทำให้ปุ่ม STOP ตายเงียบๆ ตลอดการเปิดเครื่องครั้งนั้น ขณะที่ START และ RESET ยังทำงานปกติ
//      จึงบันทึกไว้ว่าปุ่มใดอ่านได้ว่ากดอยู่ตอนเปิดเครื่อง และไม่ยอมให้เริ่มรอบใหม่ถ้า STOP เป็นหนึ่งในนั้น
//      ถ้าเป็นปุ่มที่ถูกกดค้างจริง พอปล่อยแล้วข้อสงสัยจะถูกล้าง (ดู poll) แต่สายที่ขาดจะไม่มีวันล้าง
static void seed(Btn& b, uint32_t nowMs) {
  b.lastRaw = b.stable = b.bootPressed = rawPressed(b.pin, b.nc);
  b.changed = nowMs;
  b.edge = false;
  b.isrPressed = false;
  b.isrDown = false;
  b.isrDownMs = nowMs;
}

void buttonsBegin() {
  pinMode(PIN_BTN_START, INPUT_PULLUP);
  pinMode(PIN_BTN_STOP, INPUT_PULLUP);
  pinMode(PIN_BTN_RESET, INPUT_PULLUP);
  delay(5);  // let the pullups pull the lines up before sampling — รอให้ตัวต้านทานดึงระดับขาขึ้นก่อนอ่าน
  uint32_t now = millis();
  seed(bStart, now);
  seed(bStop, now);
  seed(bReset, now);
  // Attached after seeding, so the baseline is taken from a quiet pin.
  // [TH] ผูกอินเทอร์รัปต์หลังตั้งค่าตั้งต้นแล้ว ค่าตั้งต้นจะได้มาจากขาที่ยังไม่มีอะไรมารบกวน
  attachInterruptArg(digitalPinToInterrupt(PIN_BTN_START), btnIsr, &bStart, CHANGE);
  attachInterruptArg(digitalPinToInterrupt(PIN_BTN_STOP), btnIsr, &bStop, CHANGE);
  attachInterruptArg(digitalPinToInterrupt(PIN_BTN_RESET), btnIsr, &bReset, CHANGE);
}

// Two ways a STOP line reads pressed with nobody pressing it, and the boot latch
// only caught one. A wire that breaks AFTER power-up is the likelier failure on a
// machine that moves: the line goes open, debounces to pressed, fires one STOP
// (safe), and then reads pressed forever — never raising another edge, and
// clearing bootPressed on the way past. So a line that has sat pressed far longer
// than any human press is treated the same as one that came up that way.
// [TH] มีสองทางที่สาย STOP จะอ่านได้ว่ากดโดยไม่มีใครกด แต่การตรวจตอนเปิดเครื่องดักได้ทางเดียว
//      สายที่ขาดหลังเปิดเครื่องมีโอกาสเกิดมากกว่าบนเครื่องที่มีการเคลื่อนไหว
//      วงจรจะเปิด ผ่านกันเด้งกลายเป็นสถานะกด ส่งคำสั่งหยุดหนึ่งครั้งซึ่งปลอดภัย
//      แล้วอ่านได้ว่ากดค้างตลอดไปโดยไม่เกิดขอบสัญญาณอีก และล้างค่า bootPressed ไปด้วยระหว่างทาง
//      สายที่อยู่ในสถานะกดนานเกินกว่าที่คนจะกดค้างได้ จึงถูกปฏิบัติเหมือนกับสายที่ผิดมาตั้งแต่เปิดเครื่อง
static const uint32_t STUCK_PRESSED_MS = 30000;

bool buttonStopWireSuspect() {
  if (bStop.bootPressed) return true;
  return bStop.stable && (millis() - bStop.changed) > STUCK_PRESSED_MS;
}

void buttonsPoll(uint32_t nowMs) {
  poll(bStart, nowMs);
  poll(bStop, nowMs);
  poll(bReset, nowMs);
}

bool buttonStartPressed() { return takeEdge(bStart); }
bool buttonStopPressed() { return takeEdge(bStop); }
bool buttonResetPressed() { return takeEdge(bReset); }
