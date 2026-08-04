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
  // Where the pending edge came from: the interrupt latch, or ordinary polling.
  // Reported alongside the edge so the caller can refuse a latched press for an
  // action a glitch must never trigger. Meaningless when `edge` is false.
  // [TH] ขอบที่ค้างอยู่มาจากไหน จากกลไกจับค้างของอินเทอร์รัปต์ หรือจากการอ่านตามรอบปกติ
  //      รายงานคู่ไปกับตัวขอบ ผู้เรียกจะได้ปฏิเสธการกดที่มาจากกลไกจับค้าง
  //      สำหรับการกระทำที่สัญญาณรบกวนต้องไม่มีวันสั่งได้ ไม่มีความหมายถ้า edge เป็นเท็จ
  bool edgeFromIsr;
  // Set by the interrupt only for a press it TIMED at full debounce width, so a
  // press that began and ended entirely inside one blocked loop iteration is not
  // lost — and a microsecond glitch is still not a press.
  // [TH] อินเทอร์รัปต์จะตั้งค่านี้เฉพาะการกดที่จับเวลาได้ครบตามเกณฑ์กันเด้งเท่านั้น
  //      การกดที่เริ่มและจบภายในลูปรอบเดียวที่ถูกบล็อกอยู่จึงไม่หาย
  //      และสัญญาณรบกวนระดับไมโครวินาทีก็ยังไม่ถูกนับว่าเป็นการกด
  volatile bool isrPressed;
  volatile bool isrDown;       // a press is in progress — กำลังกดอยู่
  volatile uint32_t isrDownMs; // when it started — เวลาที่เริ่มกด
  bool isrMayEdge;   // may the latch raise an edge? not RESET — ให้ยกขอบสัญญาณได้ไหม ไม่ให้สำหรับปุ่ม RESET
  bool bootPressed;  // read as pressed at power-up — อ่านได้ว่ากดอยู่ตั้งแต่เปิดเครื่อง
};

// One RUN button now does both start and stop, so there is one struct where there
// used to be two, and it inherits STOP's interrupt latch — a press lost inside a
// blocked loop iteration must still be able to stop the machine. What used to be
// guaranteed by wiring the latch to the STOP pin alone is now enforced at the
// point of use instead: see edgeFromIsr and the sketch's toggleWouldStart check.
// [TH] ปุ่ม RUN ปุ่มเดียวทำทั้งเริ่มและหยุด จึงเหลือโครงสร้างเดียวจากเดิมสองอัน
//      และรับกลไกจับค้างของอินเทอร์รัปต์จากปุ่ม STOP เดิมมาด้วย
//      เพราะการกดที่หายไปในลูปรอบที่ถูกบล็อกต้องยังสั่งหยุดเครื่องได้
//      สิ่งที่เดิมรับประกันด้วยการต่อกลไกนี้ไว้กับขาของปุ่ม STOP เท่านั้น
//      ตอนนี้ไปบังคับที่จุดใช้งานแทน ดู edgeFromIsr และการตรวจ toggleWouldStart ในสเก็ตช์
//                pin, nc, stable, lastRaw, changed, edge, edgeFromIsr, isrPressed, isrDown,
//                isrDownMs, isrMayEdge, bootPressed
static Btn bRun{PIN_BTN_RUN, BTN_RUN_NC, false, false, 0, false, false, false, false, 0,
                true, false};
static Btn bReset{PIN_BTN_RESET, BTN_RESET_NC, false, false, 0, false, false, false, false, 0,
                  false, false};

// Polarity is per button now, so it is a runtime field rather than a #ifdef: the
// two can differ, and mixed panels are the common case.
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
// is dangerous rather than merely sloppy: both buttons are NC with a pullup, so
// "pressed" IS the open-circuit state, and a loose spade lug chattering open for
// microseconds on the RUN line would read as a press and — with the machine idle
// and the leg down — lift a patient's leg with nobody at the panel. So the
// interrupt measures the gap between the make
// and the break, and only a press that lasted the full debounce window is handed
// to the loop — the same bar the polling path applies, just measured by hardware
// timestamps instead of by sampling.
// [TH] ทำงานเมื่อระดับขาเปลี่ยน และจับเวลาความยาวของการกด ไม่ใช่แค่รู้ว่ามีการกด
//      ถ้าตัดสินจากระดับสัญญาณอย่างเดียวจะเป็นการข้ามกลไกกันเด้ง ซึ่งบนหน้าปัดชุดนี้อันตราย ไม่ใช่แค่หละหลวม
//      ปุ่มทั้งสองเป็นแบบปกติปิดและมีตัวต้านทานดึงขึ้น สถานะ "กด" จึงคือสถานะวงจรเปิด
//      ขั้วต่อที่หลวมและกระพริบเปิดเพียงไมโครวินาทีบนสาย RUN จะถูกอ่านว่าเป็นการกด
//      แล้วยกขาผู้ป่วยขึ้นโดยไม่มีใครอยู่ที่หน้าปัด ในตอนที่เครื่องว่างและขาอยู่ล่างสุด
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
  // RESET is excluded from the latch (isrMayEdge), and the asymmetry is the point.
  // The failure the latch exists for is a stop press lost inside a blocked loop
  // iteration; a lost start just means pressing the button again, while a FALSE
  // start lifts a patient's leg with nobody at the panel. The interrupt is not
  // IRAM-registered on this core, so it is masked while the flash cache is off and
  // one pending edge is delivered late, timed against the level at delivery —
  // which can inflate a glitch into a full-width "press". A spurious stop costs a
  // safe-lower; that is the trade.
  //
  // With START and STOP on separate pins that trade could be settled here, by
  // wiring the latch to STOP alone. One RUN button cannot: the same press starts
  // or stops depending on the phase, and the phase can still change after this
  // runs (tick() is called between buttonsPoll and the sketch's edge consumption).
  // So the latch fires for RUN and the origin travels with the edge in
  // edgeFromIsr; the sketch drops a latched press that would start. The property
  // preserved is identical — a latched press can only ever stop or lower.
  // [TH] ปุ่ม RESET ถูกกันออกจากกลไกจับค้าง (isrMayEdge) และความไม่สมมาตรนี้คือประเด็นสำคัญ
  //      สิ่งที่กลไกนี้มีไว้แก้คือการกดเพื่อหยุดที่หายไปในลูปรอบที่ถูกบล็อก
  //      การกดเพื่อเริ่มที่หายไปแค่กดใหม่ก็จบ แต่การเริ่มทำงานผิดพลาดคือการยกขาผู้ป่วยโดยไม่มีใครอยู่ที่หน้าปัด
  //      อินเทอร์รัปต์บนคอร์นี้ไม่ได้ลงทะเบียนใน IRAM จึงถูกปิดกั้นระหว่างที่แคชแฟลชถูกปิด
  //      ขอบที่ค้างอยู่หนึ่งครั้งจะถูกส่งช้าและจับเวลาเทียบกับระดับสัญญาณ ณ เวลาที่ส่ง
  //      ซึ่งอาจทำให้สัญญาณรบกวนกลายเป็นการกดที่ยาวเต็มเกณฑ์
  //      การกดเพื่อหยุดที่เกินมาแลกด้วยการลดขาลงหนึ่งครั้ง นั่นคือสิ่งที่ยอมแลก
  //      สมัยที่ START กับ STOP อยู่คนละขา เรื่องนี้ตัดสินได้ตรงนี้เลย ด้วยการต่อกลไกจับค้างไว้กับ STOP เท่านั้น
  //      แต่ปุ่ม RUN ปุ่มเดียวทำแบบนั้นไม่ได้ เพราะการกดเดียวกันจะเริ่มหรือหยุดขึ้นอยู่กับสถานะ
  //      และสถานะยังเปลี่ยนได้อีกหลังจากโค้ดตรงนี้ทำงาน
  //      (tick() ถูกเรียกคั่นระหว่าง buttonsPoll กับตอนที่สเก็ตช์อ่านขอบสัญญาณ)
  //      กลไกจับค้างจึงทำงานให้ปุ่ม RUN ด้วย และที่มาของขอบจะติดไปกับตัวขอบผ่าน edgeFromIsr
  //      แล้วสเก็ตช์เป็นฝ่ายทิ้งการกดที่จับค้างไว้ซึ่งจะกลายเป็นการเริ่มรอบ
  //      คุณสมบัติที่รักษาไว้เหมือนเดิมทุกประการ คือการกดที่จับค้างไว้จะหยุดหรือลดขาได้เท่านั้น
  if (b.isrPressed) {
    b.isrPressed = false;
    if (b.isrMayEdge && !b.stable) {
      b.edge = true;          // a full-width press the loop never saw
                              // [TH] การกดที่ยาวครบเกณฑ์ซึ่งลูปไม่เคยเห็น
      b.edgeFromIsr = true;
    }
  }

  if (raw != b.lastRaw) {
    b.lastRaw = raw;
    b.changed = nowMs;
  } else if (nowMs - b.changed >= BUTTON_DEBOUNCE_MS && raw != b.stable) {
    b.stable = raw;
    // A press the loop saw for itself outranks a latched one: if both happen in
    // the same iteration the origin ends up "polled", which is the truthful
    // answer — the level really was sampled pressed here.
    // [TH] การกดที่ลูปเห็นเองสำคัญกว่าการกดที่กลไกจับค้างไว้
    //      ถ้าเกิดทั้งคู่ในลูปรอบเดียวกัน ที่มาจะกลายเป็น "อ่านตามรอบ" ซึ่งเป็นคำตอบที่ตรงความจริง
    //      เพราะระดับขาถูกอ่านได้ว่ากดจริงตรงนี้
    if (raw) { b.edge = true; b.edgeFromIsr = false; }  // rising: not-pressed -> pressed
                                                        // [TH] ขอบขาขึ้น: จากไม่กดเป็นกด
    // A line that came up already reading pressed has finally moved, so it was a
    // held button and not a broken wire. Clear the suspicion either way.
    // [TH] ขาที่ตอนเปิดเครื่องอ่านได้ว่ากดอยู่ ตอนนี้เปลี่ยนค่าแล้ว
    //      แปลว่าเป็นปุ่มที่ถูกกดค้าง ไม่ใช่สายขาด จึงล้างข้อสงสัยทิ้งทั้งสองกรณี
    b.bootPressed = false;
  }
}

static bool takeEdge(Btn& b, bool* fromIsr) {
  bool e = b.edge;
  if (fromIsr) *fromIsr = e && b.edgeFromIsr;
  b.edge = false;
  b.edgeFromIsr = false;
  return e;
}

// Seeded from the pins, not from `false`. The structs used to start as
// not-pressed, so a line already reading pressed at power-up looked like a
// transition and raised a press edge about one debounce later. RUN is NC, so
// an unplugged panel or a cut wire reads pressed forever — and with the machine
// idle and the leg down, that edge is a start: it would begin lifting a leg
// roughly 25 ms after power-up with nobody touching it.
// Taking the boot state as the baseline means a press has to be a release-then-
// press seen after startup, which a broken wire can never produce.
// [TH] ตั้งค่าเริ่มต้นจากค่าที่อ่านได้จริงจากขา ไม่ใช่กำหนดเป็น "ไม่ได้กด"
//      เดิมโครงสร้างเริ่มที่ไม่ได้กด ถ้าตอนเปิดเครื่องขาใดอ่านได้ว่ากดอยู่ จะถูกมองว่าเพิ่งเปลี่ยนสถานะ
//      แล้วเกิดขอบกดขึ้นหลังพ้นเวลากันเด้ง ปุ่ม RUN เป็นแบบปกติปิด สายที่ขาดหรือยังไม่ได้เสียบจะอ่านว่ากดค้าง
//      และถ้าเครื่องว่างโดยขาอยู่ล่างสุด ขอบนั้นคือการเริ่มรอบ
//      เครื่องจะเริ่มยกขาผู้ป่วยเองราว 25 มิลลิวินาทีหลังเปิดเครื่องโดยไม่มีใครแตะปุ่ม
//      การยึดสถานะตอนเปิดเครื่องเป็นค่าตั้งต้น ทำให้ต้องมีการปล่อยแล้วกดใหม่เท่านั้นจึงนับเป็นการกด
//      ซึ่งสายที่ขาดอยู่ทำไม่ได้
// The flip side of seeding: taking the boot state as the baseline also means a
// line that is ALREADY open at power-up — an NC button whose wire is broken or
// whose connector was never seated — seeds as pressed and can never produce an
// edge again, because an edge needs a not-pressed -> pressed transition and the
// line never leaves pressed. A broken RUN wire therefore leaves the panel with no
// stop at all for the whole power cycle while RESET works normally — worse than
// when STOP was its own button, because now the same break takes the start with
// it. Which buttons came up pressed is recorded, and the sketch refuses to start a
// session — including one commanded over the link — while RUN is one of them.
// Releasing a genuinely held button clears it (see poll); a cut wire never does.
// [TH] อีกด้านหนึ่งของการยึดสถานะตอนเปิดเครื่องเป็นค่าตั้งต้น คือสายที่ขาดอยู่แล้วตั้งแต่เปิดเครื่อง
//      เช่นปุ่มแบบปกติปิดที่สายขาดหรือขั้วต่อไม่ได้เสียบ จะถูกตั้งต้นว่ากดอยู่และจะไม่เกิดขอบสัญญาณได้อีกเลย
//      เพราะขอบสัญญาณต้องเปลี่ยนจากไม่กดเป็นกด แต่ขานั้นไม่เคยออกจากสถานะกดเลย
//      สาย RUN ที่ขาดจึงทำให้หน้าปัดไม่มีปุ่มหยุดเลยตลอดการเปิดเครื่องครั้งนั้น ขณะที่ RESET ยังทำงานปกติ
//      ซึ่งแย่กว่าสมัยที่ STOP เป็นปุ่มแยก เพราะสายที่ขาดเส้นเดียวกันนี้พาการเริ่มรอบหายไปด้วย
//      จึงบันทึกไว้ว่าปุ่มใดอ่านได้ว่ากดอยู่ตอนเปิดเครื่อง
//      และไม่ยอมให้เริ่มรอบใหม่ถ้า RUN เป็นหนึ่งในนั้น รวมถึงรอบที่สั่งมาทางลิงก์
//      ถ้าเป็นปุ่มที่ถูกกดค้างจริง พอปล่อยแล้วข้อสงสัยจะถูกล้าง (ดู poll) แต่สายที่ขาดจะไม่มีวันล้าง
static void seed(Btn& b, uint32_t nowMs) {
  b.lastRaw = b.stable = b.bootPressed = rawPressed(b.pin, b.nc);
  b.changed = nowMs;
  b.edge = false;
  b.edgeFromIsr = false;
  b.isrPressed = false;
  b.isrDown = false;
  b.isrDownMs = nowMs;
}

void buttonsBegin() {
  pinMode(PIN_BTN_RUN, INPUT_PULLUP);
  pinMode(PIN_BTN_RESET, INPUT_PULLUP);
  delay(5);  // let the pullups pull the lines up before sampling — รอให้ตัวต้านทานดึงระดับขาขึ้นก่อนอ่าน
  uint32_t now = millis();
  seed(bRun, now);
  seed(bReset, now);
  // Attached after seeding, so the baseline is taken from a quiet pin.
  // [TH] ผูกอินเทอร์รัปต์หลังตั้งค่าตั้งต้นแล้ว ค่าตั้งต้นจะได้มาจากขาที่ยังไม่มีอะไรมารบกวน
  attachInterruptArg(digitalPinToInterrupt(PIN_BTN_RUN), btnIsr, &bRun, CHANGE);
  attachInterruptArg(digitalPinToInterrupt(PIN_BTN_RESET), btnIsr, &bReset, CHANGE);
}

// Two ways a RUN line reads pressed with nobody pressing it, and the boot latch
// only caught one. A wire that breaks AFTER power-up is the likelier failure on a
// machine that moves: the line goes open, debounces to pressed, fires one press
// (which mid-session is a stop, and idle is a lower or a no-op — never a start,
// see SessionSM::toggle), and then reads pressed forever — never raising another
// edge, and clearing bootPressed on the way past. So a line that has sat pressed
// far longer than any human press is treated the same as one that came up that way.
// [TH] มีสองทางที่สาย RUN จะอ่านได้ว่ากดโดยไม่มีใครกด แต่การตรวจตอนเปิดเครื่องดักได้ทางเดียว
//      สายที่ขาดหลังเปิดเครื่องมีโอกาสเกิดมากกว่าบนเครื่องที่มีการเคลื่อนไหว
//      วงจรจะเปิด ผ่านกันเด้งกลายเป็นสถานะกด เกิดการกดหนึ่งครั้ง
//      ซึ่งถ้าอยู่กลางรอบคือการสั่งหยุด ถ้าว่างอยู่คือการลดขาหรือไม่ทำอะไร
//      ไม่มีทางเป็นการเริ่มรอบ (ดู SessionSM::toggle)
//      แล้วอ่านได้ว่ากดค้างตลอดไปโดยไม่เกิดขอบสัญญาณอีก และล้างค่า bootPressed ไปด้วยระหว่างทาง
//      สายที่อยู่ในสถานะกดนานเกินกว่าที่คนจะกดค้างได้ จึงถูกปฏิบัติเหมือนกับสายที่ผิดมาตั้งแต่เปิดเครื่อง
static const uint32_t STUCK_PRESSED_MS = 30000;

bool buttonRunWireSuspect() {
  if (bRun.bootPressed) return true;
  return bRun.stable && (millis() - bRun.changed) > STUCK_PRESSED_MS;
}

void buttonsPoll(uint32_t nowMs) {
  poll(bRun, nowMs);
  poll(bReset, nowMs);
}

bool buttonRunPressed(bool* fromIsr) { return takeEdge(bRun, fromIsr); }
bool buttonResetPressed() { return takeEdge(bReset, nullptr); }
