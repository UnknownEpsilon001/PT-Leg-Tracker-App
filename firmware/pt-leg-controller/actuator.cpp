// การจ่ายไฟรีเลย์ยก/ลดขา พร้อมกลไกความปลอดภัย "ตัดก่อนต่อ" และเพดานเวลาจ่ายไฟ
// Relay drive for up/down with break-before-make and a max-energize time cap.
#include "actuator.h"
#include <Arduino.h>
#include <esp_timer.h>
#include "config.h"

#ifdef RELAY_ACTIVE_LOW
static const int ON = LOW, OFF = HIGH;
#else
static const int ON = HIGH, OFF = LOW;
#endif
// [TH] ON/OFF คือระดับลอจิกที่จ่าย/ตัดรีเลย์ กลับด้านตามชนิดบอร์ด (active-low หรือไม่)

static ActState state = ACT_OFF;
static uint32_t capMs = 0, energizeStart = 0;  // เพดานเวลา และเวลาที่เริ่มจ่ายไฟ
// volatile: written by the esp_timer callback on the other core as well as by the
// loop. [TH] ต้องเป็น volatile เพราะถูกเขียนจากฟังก์ชันเรียกกลับของตัวจับเวลาบนอีกคอร์ด้วย ไม่ใช่แค่จากลูป
static volatile bool capTripped = false;       // ธงว่าเพดานเวลาถูกตัดไปแล้ว

// Dead time between de-energizing one relay and energizing the other. Commanding
// them apart is not the same as them being apart: the coils take milliseconds to
// release, and back-to-back digitalWrites are microseconds apart, so a live
// reversal — STOP during a lift, which goes straight from UP to SafeLower's DOWN
// in one loop iteration — relied on the UP relay happening to release faster than
// the DOWN one operates. That holds for most parts and is why the bench works,
// but it is a property of the relays, not of this code, and the two channels are
// on different connectors. 50 ms comfortably exceeds the release time of the
// relay modules these boards use.
// [TH] เวลาหน่วงระหว่างดับรีเลย์ตัวหนึ่งกับจ่ายไฟให้อีกตัว
//      การสั่งแยกกันไม่เท่ากับการที่หน้าสัมผัสแยกกันจริง ขดลวดใช้เวลาหลายมิลลิวินาทีกว่าจะคลาย
//      แต่คำสั่งเขียนขาสองครั้งติดกันห่างกันแค่ไมโครวินาที
//      การกลับทิศขณะกำลังเคลื่อนที่ เช่นกด STOP ตอนกำลังยก ซึ่งเปลี่ยนจากยกเป็นลดในลูปรอบเดียว
//      จึงไปพึ่งว่ารีเลย์ตัวยกจะคลายเร็วกว่าที่ตัวลดจะทำงาน
//      ซึ่งเป็นจริงกับอุปกรณ์ส่วนใหญ่และเป็นเหตุผลที่ทดสอบบนโต๊ะแล้วผ่าน
//      แต่นั่นเป็นคุณสมบัติของรีเลย์ ไม่ใช่ของโค้ดนี้ และรีเลย์สองตัวอยู่คนละขั้วต่อ
//      ค่า 50 มิลลิวินาทีมากกว่าเวลาคลายของโมดูลรีเลย์ที่บอร์ดนี้ใช้อยู่พอสมควร
static const uint32_t DEADTIME_MS = 50;
static ActState pending = ACT_OFF;   // direction waiting out the dead time
static uint32_t pendingAt = 0;       // when it may be energized
// When the relays were last actually de-energized. The dead time has to be
// measured from this, not only from a live reversal: most direction changes pass
// through an OFF phase first, and the short ones do not wait. A fault during a
// lift cuts the UP relay and the operator's STOP — or a server stop in the same
// iteration — enters SafeLower immediately after, so DOWN could be energized tens
// of milliseconds later, while the UP contacts are still closing.
// [TH] เวลาที่รีเลย์ถูกดับจริงครั้งล่าสุด ต้องวัดเวลาหน่วงจากค่านี้
//      ไม่ใช่วัดเฉพาะตอนกลับทิศขณะกำลังเคลื่อนที่
//      เพราะการเปลี่ยนทิศส่วนใหญ่ผ่านช่วงที่ดับก่อน และช่วงที่สั้นๆ ก็ไม่ได้รอ
//      ความผิดพลาดระหว่างยกขาจะตัดรีเลย์ขึ้น แล้วการกด STOP ของผู้ใช้
//      หรือคำสั่งหยุดจากเซิร์ฟเวอร์ในลูปรอบเดียวกัน จะพาเข้าสถานะลดขาทันทีหลังจากนั้น
//      รีเลย์ลงจึงอาจถูกจ่ายไฟห่างออกไปเพียงไม่กี่สิบมิลลิวินาที ขณะที่หน้าสัมผัสของรีเลย์ขึ้นยังกำลังปิดอยู่
static uint32_t offAt = 0;

// Hardware backstop for the energize cap. actuatorTick() can only cut the relays
// when the loop reaches it, and the failure that makes a stroke run long — a loop
// blocked in a network call, an NVS commit, a panic — is the same failure that
// stops the tick from running at all. That made the "independent" second net
// depend on the very thing it was guarding. This timer runs off the esp_timer
// task instead, so it fires whether or not loop() is alive.
// [TH] แนวป้องกันสำรองระดับฮาร์ดแวร์สำหรับเพดานเวลาจ่ายไฟ
//      actuatorTick() ตัดรีเลย์ได้เฉพาะตอนที่ลูปเดินมาถึงมันเท่านั้น
//      และความล้มเหลวที่ทำให้จ่ายไฟยาวเกิน เช่นลูปค้างอยู่ในการเรียกเครือข่าย การเขียน NVS หรือโปรแกรมพัง
//      คือความล้มเหลวเดียวกับที่ทำให้ actuatorTick() ไม่ได้ทำงานเลย
//      ตาข่ายกันพลาดชั้นที่สองจึงไปขึ้นกับสิ่งที่ตัวเองมีไว้เฝ้าพอดี
//      ตัวจับเวลานี้ทำงานบนงานของ esp_timer แทน จึงทำงานไม่ว่าลูปหลักจะยังเดินอยู่หรือไม่
static esp_timer_handle_t capTimer = nullptr;
static volatile bool capTimerFired = false;

static void writePins(int up, int down) {
  // break-before-make: always de-energize both before energizing one
  // [TH] ตัดก่อนต่อ: ดับรีเลย์ทั้งคู่ก่อนเสมอ แล้วค่อยจ่ายไฟด้านที่ต้องการ
  digitalWrite(PIN_RELAY_UP, OFF);
  digitalWrite(PIN_RELAY_DOWN, OFF);
  if (up == ON) digitalWrite(PIN_RELAY_UP, ON);
  if (down == ON) digitalWrite(PIN_RELAY_DOWN, ON);
}

// Cuts the relays from the timer task. Only the pins are touched here; the
// bookkeeping is left to actuatorTick() on the main loop, so nothing races over
// the state variables.
// [TH] ตัดรีเลย์จากงานของตัวจับเวลา แตะเฉพาะขาสัญญาณเท่านั้น
//      ส่วนการปรับค่าสถานะปล่อยให้ actuatorTick() ในลูปหลักทำ จะได้ไม่มีการแย่งเขียนตัวแปรสถานะกัน
static void capTimerCb(void*) {
  writePins(OFF, OFF);
  // capTripped is latched here, not left for the loop to derive from the flag
  // below. The loop's own order is applyActuator() then actuatorTick(), and by
  // the time it resumes the state machine has usually faulted on its own net
  // already — so applyActuator() calls actuatorOff(), which disarms and clears
  // the flag before actuatorTick() ever reads it. The relays still opened, but
  // the trip was never reported. Latching it here survives that, and request()
  // clears it on the next energize.
  // [TH] ตั้งค่า capTripped ตรงนี้ ไม่ปล่อยให้ลูปไปสรุปเอาจากธงด้านล่าง
  //      ลำดับในลูปคือ applyActuator() แล้วจึง actuatorTick()
  //      กว่าลูปจะกลับมาทำงาน เครื่องสถานะมักตัดสินว่าผิดพลาดด้วยตาข่ายของตัวเองไปแล้ว
  //      applyActuator() จึงเรียก actuatorOff() ซึ่งยกเลิกตัวจับเวลาและล้างธงทิ้งก่อนที่ actuatorTick() จะได้อ่าน
  //      รีเลย์ถูกตัดจริง แต่ไม่มีการรายงานว่าถูกตัด การเก็บค่าไว้ตรงนี้จะรอดจากลำดับนั้น
  //      และ request() จะล้างค่าให้เองตอนจ่ายไฟครั้งถัดไป
  capTripped = true;
  capTimerFired = true;
}

// Armed whenever a relay is energized, cancelled the moment both are off.
// [TH] ตั้งเวลาทุกครั้งที่จ่ายไฟรีเลย์ และยกเลิกทันทีที่ดับทั้งคู่
static void armCapTimer() {
  if (!capTimer || capMs == 0) return;
  esp_timer_stop(capTimer);  // harmless if it is not running — ถ้ายังไม่ได้เดินอยู่ก็ไม่มีผล
  esp_timer_start_once(capTimer, (uint64_t)capMs * 1000ULL);
}

static void disarmCapTimer() {
  if (!capTimer) return;
  esp_timer_stop(capTimer);
  // Cleared after the stop, not before: a callback already dispatched on the
  // other core can set the flag between the two, and it would otherwise be read
  // on a later iteration as a cap trip for a stroke that had ended normally —
  // faulting a session that was fine. Only reached from paths that are taking the
  // relays off anyway, so a genuine trip during a driving phase is untouched: the
  // driving paths early-return without disarming.
  // [TH] ล้างค่าหลังสั่งหยุด ไม่ใช่ก่อน เพราะฟังก์ชันเรียกกลับที่ถูกส่งไปทำงานบนอีกคอร์แล้ว
  //      อาจตั้งค่าธงในช่วงระหว่างสองคำสั่งนี้ ซึ่งจะถูกอ่านในรอบถัดไปว่าเป็นการตัดเพราะเกินเพดานเวลา
  //      ของการเคลื่อนที่ที่จบไปตามปกติแล้ว และทำให้รอบที่ปกติกลายเป็นรอบที่ผิดพลาด
  //      จะมาถึงตรงนี้เฉพาะเส้นทางที่กำลังจะดับรีเลย์อยู่แล้ว การตัดจริงระหว่างสถานะที่กำลังขับเคลื่อนจึงไม่ถูกแตะ
  //      เพราะเส้นทางที่กำลังขับเคลื่อนจะคืนค่าออกไปก่อนโดยไม่เรียกฟังก์ชันนี้
  capTimerFired = false;
}

void actuatorBegin() {
  // [TH] ตั้งขาเป็นเอาต์พุตและบังคับรีเลย์ดับทั้งคู่ ต้องเรียกเป็นอย่างแรกใน setup()
  pinMode(PIN_RELAY_UP, OUTPUT);
  pinMode(PIN_RELAY_DOWN, OUTPUT);
  writePins(OFF, OFF);
  state = ACT_OFF;
  pending = ACT_OFF;
  const esp_timer_create_args_t args = {capTimerCb, nullptr, ESP_TIMER_TASK, "actcap", false};
  // Recorded, not printed: actuatorBegin() is the first call in setup(), before
  // Serial.begin(), and HardwareSerial drops writes while its UART is null — the
  // message would have gone nowhere, which is the one place it must not go.
  // setup() reports it once the port is open.
  // [TH] เก็บค่าไว้ ไม่พิมพ์ทันที เพราะ actuatorBegin() เป็นคำสั่งแรกใน setup() ก่อน Serial.begin()
  //      และไลบรารีจะทิ้งข้อมูลที่เขียนตอนที่พอร์ตยังไม่ถูกเปิด ข้อความจะหายไปเฉยๆ
  //      ซึ่งเป็นสิ่งเดียวที่ต้องไม่เกิดกับข้อความนี้ ให้ setup() รายงานหลังเปิดพอร์ตแล้ว
  if (esp_timer_create(&args, &capTimer) != ESP_OK) capTimer = nullptr;
}

bool actuatorCapTimerReady() { return capTimer != nullptr; }

// Energizes `dir`, or parks the request behind the dead time when the opposite
// relay is still energized. Idempotent: applyActuator() calls these every loop
// while a phase runs. Re-stamping energizeStart every time would keep
// nowMs-energizeStart near zero forever and the travel cap could never trip, so
// it is stamped only on a real transition.
// [TH] จ่ายไฟให้ทิศที่ขอ หรือพักคำขอไว้รอเวลาหน่วงถ้ารีเลย์ฝั่งตรงข้ามยังจ่ายไฟอยู่
//      ทำซ้ำได้ปลอดภัย: applyActuator() เรียกทุกลูประหว่างที่สถานะกำลังทำงาน
//      ถ้าจับเวลา energizeStart ใหม่ทุกครั้ง ค่าจะเกือบศูนย์ตลอดและเพดานเวลาจะไม่มีวันตัด
//      จึงจับเวลาเฉพาะตอนเปลี่ยนสถานะจริงเท่านั้น
static void request(ActState dir) {
  if (state == dir) { pending = ACT_OFF; return; }  // already there — อยู่ในทิศนั้นแล้ว
  if (pending == dir) return;                       // already waiting — รออยู่แล้ว
  // Relays off, but not off for long enough yet: wait out the remainder before
  // driving the other way.
  // [TH] รีเลย์ดับอยู่ แต่ยังดับไม่นานพอ ให้รอเวลาที่เหลือก่อนจึงค่อยสั่งไปอีกทาง
  // Unsigned here, unlike the compares against the loop's captured `now`. offAt is
  // always stamped from millis() and so is never ahead of it, which means the
  // signed form bought nothing — and cost a great deal: for an elapsed time
  // between 24.9 and 49.7 days the cast goes negative, every request parks in
  // pending, and the honour test below never fires either. A machine left powered
  // for 25 days would answer START and STOP with a stroke that never energizes,
  // faulting after the cap with zero motion, until a reboot.
  // [TH] ตรงนี้ใช้แบบไม่มีเครื่องหมาย ต่างจากการเทียบกับค่า now ที่ลูปอ่านไว้
  //      ค่า offAt ถูกจับเวลาจาก millis() เสมอ จึงไม่มีทางล้ำหน้า millis() ได้
  //      การใช้แบบมีเครื่องหมายจึงไม่ได้ประโยชน์อะไรเลย แต่เสียหายมาก
  //      ถ้าเวลาที่ผ่านไปอยู่ระหว่าง 24.9 ถึง 49.7 วัน การแปลงชนิดจะได้ค่าติดลบ
  //      คำขอทุกครั้งจะไปพักรอ และเงื่อนไขปลดการรอด้านล่างก็จะไม่เป็นจริงเช่นกัน
  //      เครื่องที่เปิดทิ้งไว้ 25 วันจะตอบปุ่มเริ่มและปุ่มหยุดด้วยการเคลื่อนที่ที่ไม่จ่ายไฟเลย
  //      แล้วจบด้วยความผิดพลาดหลังครบเพดานเวลาโดยไม่ขยับ จนกว่าจะรีบูต
  if (state == ACT_OFF && (millis() - offAt) < DEADTIME_MS) {
    pending = dir;
    pendingAt = offAt + DEADTIME_MS;
    return;
  }
  if (state != ACT_OFF) {
    // Live reversal: drop both now, energize the new direction once the coils
    // have had time to release.
    // [TH] กลับทิศขณะกำลังเคลื่อนที่: ดับทั้งคู่ทันที แล้วค่อยจ่ายไฟทิศใหม่เมื่อขดลวดคลายแล้ว
    writePins(OFF, OFF);
    disarmCapTimer();
    state = ACT_OFF;
    offAt = millis();
    pending = dir;
    pendingAt = offAt + DEADTIME_MS;
    return;
  }
  writePins(dir == ACT_UP ? ON : OFF, dir == ACT_DOWN ? ON : OFF);
  state = dir;
  pending = ACT_OFF;
  energizeStart = millis();
  capTripped = false;
  armCapTimer();
}

void actuatorUp() { request(ACT_UP); }

void actuatorDown() { request(ACT_DOWN); }

void actuatorOff() {
  // The pins are re-asserted every call, deliberately: applyActuator() runs this
  // every loop in the phases that must not drive, so a relay line disturbed by
  // anything else is driven back OFF within one iteration. Only the timer and the
  // state bookkeeping are skipped when there is nothing to undo.
  // [TH] สั่งระดับขาซ้ำทุกครั้งโดยตั้งใจ เพราะ applyActuator() เรียกฟังก์ชันนี้ทุกลูป
  //      ในสถานะที่ต้องไม่ขับเคลื่อน ถ้าสายรีเลย์ถูกรบกวนจากอย่างอื่นก็จะถูกสั่งดับกลับภายในหนึ่งรอบ
  //      ส่วนตัวจับเวลาและการปรับค่าสถานะจะข้ามไปเมื่อไม่มีอะไรต้องยกเลิก
  writePins(OFF, OFF);
  if (state == ACT_OFF && pending == ACT_OFF) return;  // nothing to undo — ไม่มีอะไรต้องยกเลิก
  disarmCapTimer();
  // Stamped only on the transition, so idling does not keep pushing the dead time
  // forward and delay a genuine start.
  // [TH] จับเวลาเฉพาะตอนเปลี่ยนสถานะ การอยู่เฉยๆ จะได้ไม่เลื่อนเวลาหน่วงออกไปเรื่อยๆ
  //      จนทำให้การเริ่มเคลื่อนที่จริงถูกหน่วงตามไปด้วย
  if (state != ACT_OFF) offAt = millis();
  state = ACT_OFF;
  pending = ACT_OFF;  // an OFF cancels a reversal still waiting out its dead time
                      // [TH] คำสั่งดับจะยกเลิกการกลับทิศที่ยังรอเวลาหน่วงอยู่ด้วย
}

ActState actuatorState() { return state; }

// Restarts the energize clock for a new stroke that happens to keep driving the
// same way. Lower and SafeLower both command DOWN, so request() sees no direction
// change and early-returns without re-stamping — a STOP three seconds into an
// eight-second Lower left the safe-lower only five and a half seconds of the cap
// it is supposed to get, and the cap then faulted the machine part-way down
// rather than letting it reach the bottom.
// [TH] เริ่มจับเวลาการจ่ายไฟใหม่สำหรับการเคลื่อนที่ครั้งใหม่ที่บังเอิญยังสั่งไปทางเดิม
//      ทั้งสถานะลดลงตามปกติและสถานะลดลงเพื่อหยุดต่างก็สั่งลง request() จึงไม่เห็นการเปลี่ยนทิศ
//      และคืนค่าออกไปโดยไม่จับเวลาใหม่
//      การกด STOP ที่วินาทีที่สามของการลดลงแปดวินาที ทำให้การลดขาเพื่อหยุดเหลือเวลาเพียงห้าวินาทีครึ่ง
//      จากเพดานที่ควรได้เต็ม แล้วเพดานเวลาก็ตัดเป็นความผิดพลาดกลางทางแทนที่จะปล่อยให้ลงถึงพื้น
void actuatorRenewStroke() {
  if (state == ACT_OFF) return;  // nothing running to renew — ไม่มีการเคลื่อนที่ให้เริ่มจับเวลาใหม่
  // capTripped is deliberately NOT cleared here. Only a real energize clears it,
  // in request(). Clearing it on a renew threw away a trip the hardware timer had
  // just recorded — the renew runs in applyActuator(), which is before the sketch
  // reads the flag — so the relays had been cut by the backstop and nothing said
  // so.
  // [TH] จงใจไม่ล้างค่า capTripped ตรงนี้ มีแต่การจ่ายไฟจริงเท่านั้นที่ล้างได้ ซึ่งอยู่ใน request()
  //      การล้างค่าตอนเริ่มจับเวลาใหม่จะทิ้งการตัดที่ตัวจับเวลาฮาร์ดแวร์เพิ่งบันทึกไว้
  //      เพราะการเริ่มจับเวลาใหม่ทำงานใน applyActuator() ซึ่งอยู่ก่อนที่สเก็ตช์จะอ่านค่าธง
  //      รีเลย์ถูกแนวป้องกันสำรองตัดไปแล้วโดยไม่มีอะไรบอก
  energizeStart = millis();
  armCapTimer();
}

void actuatorSetCap(uint32_t ms) { capMs = ms; }  // ตั้งเพดานเวลาจ่ายไฟสูงสุด

void actuatorTick(uint32_t nowMs) {
  // The hardware backstop got there first: the relays are already open, so this
  // only catches the bookkeeping up and reports it. Reached whenever the loop was
  // blocked past the cap — which is the case the timer exists for.
  // [TH] แนวป้องกันระดับฮาร์ดแวร์ตัดไปก่อนแล้ว รีเลย์เปิดวงจรอยู่
  //      ตรงนี้แค่ปรับค่าสถานะให้ตรงและรายงานผล
  //      จะมาถึงตรงนี้เมื่อลูปถูกบล็อกนานเกินเพดานเวลา ซึ่งเป็นกรณีที่ตัวจับเวลามีไว้เพื่อมัน
  // A reversal that has waited out its dead time is honoured before anything
  // else. Handling it after the cap branch let that branch clear `pending` and
  // drop the queued direction; applyActuator then re-issued it on the next
  // iteration, energizing the new direction about 2 ms after the old one
  // dropped instead of the 50 ms this file exists to guarantee.
  // [TH] การกลับทิศที่รอเวลาหน่วงครบแล้วจะถูกจัดการก่อนสิ่งอื่น
  //      ถ้าจัดการหลังส่วนตรวจเพดานเวลา ส่วนนั้นจะล้างค่า pending และทิ้งทิศที่รอคิวอยู่
  //      แล้ว applyActuator จะสั่งใหม่ในลูปรอบถัดไป ทำให้จ่ายไฟทิศใหม่ห่างจากตอนดับทิศเก่าราว 2 มิลลิวินาที
  //      แทนที่จะเป็น 50 มิลลิวินาทีตามที่ไฟล์นี้มีไว้เพื่อรับประกัน
  if (pending != ACT_OFF && (int32_t)(nowMs - pendingAt) >= 0) {
    ActState dir = pending;
    pending = ACT_OFF;
    request(dir);
    return;
  }

  if (capTimerFired) {
    capTimerFired = false;
    // Disarmed as well as noted. The timer is a one-shot so it is not running,
    // but leaving it un-stopped left an armed handle nobody owned once the
    // bookkeeping below marks the actuator off.
    // [TH] ยกเลิกตัวจับเวลาด้วย ไม่ใช่แค่บันทึกว่าเกิดขึ้น
    //      ตัวจับเวลาเป็นแบบทำงานครั้งเดียวจึงไม่ได้เดินอยู่แล้ว
    //      แต่การไม่สั่งหยุดจะทิ้งแฮนเดิลที่ยังตั้งเวลาไว้โดยไม่มีใครเป็นเจ้าของ
    //      เมื่อการปรับค่าสถานะด้านล่างทำเครื่องหมายว่าชุดขับเคลื่อนดับแล้ว
    disarmCapTimer();
    if (state != ACT_OFF) offAt = nowMs;  // the callback cut them — ฟังก์ชันเรียกกลับเป็นคนตัด
    state = ACT_OFF;
    pending = ACT_OFF;
    capTripped = true;
    return;
  }

  // [TH] ตรวจทุกลูป: ถ้าจ่ายไฟค้างเกินเพดานเวลา ให้ตัดรีเลย์และตั้งธง capTripped
  if (state == ACT_OFF || capMs == 0) return;
  // Signed, because energizeStart can be AHEAD of nowMs. The loop captures
  // millis() once at the top and passes that same value all the way down, but a
  // relay energized part-way through the iteration stamps a fresh millis() —
  // so on any iteration that crosses a millisecond boundary, energizeStart is
  // nowMs+1. Unsigned, that subtraction is ~4.29e9, instantly past any cap: the
  // stroke was cut and the session faulted in the same iteration it started, a
  // zero-second faulted row on the card with no cause. A coin flip on every
  // energize, including the safe-lower out of a fault.
  // [TH] ใช้การเทียบแบบมีเครื่องหมาย เพราะ energizeStart อาจล้ำหน้า nowMs ได้
  //      ลูปอ่านค่า millis() ครั้งเดียวที่ต้นรอบแล้วส่งค่านั้นต่อไปทั้งรอบ
  //      แต่รีเลย์ที่เริ่มจ่ายไฟกลางรอบจะจับเวลา millis() ใหม่
  //      ในรอบที่คร่อมขอบมิลลิวินาที ค่า energizeStart จึงเป็น nowMs+1
  //      ถ้าลบแบบไม่มีเครื่องหมายจะได้ราวสี่พันล้าน ซึ่งเกินเพดานทุกค่าทันที
  //      การเคลื่อนที่จะถูกตัดและรอบถูกตัดสินว่าผิดพลาดในรอบเดียวกับที่เพิ่งเริ่ม
  //      ได้แถวที่ผิดพลาดความยาวศูนย์วินาทีลงการ์ดโดยไม่มีสาเหตุ
  //      และเป็นการเสี่ยงแบบนี้ทุกครั้งที่จ่ายไฟ รวมถึงตอนลดขาลงหลังเกิดความผิดพลาดด้วย
  if ((int32_t)(nowMs - energizeStart) >= (int32_t)capMs) {
    writePins(OFF, OFF);
    disarmCapTimer();
    offAt = nowMs;
    state = ACT_OFF;
    capTripped = true;
  }
}

// Consumed on read, so the one place that acts on it is also the one place that
// can report it. Leaving it latched meant the diagnostic was printed from
// actuatorTick() while the decision was taken in the sketch — and by the time the
// loop got there after a long block, the state machine had usually faulted on its
// own net first, so applyActuator()'s actuatorOff() had already disarmed and
// cleared the diagnostic flag. The relays opened and nobody could tell that the
// loop, rather than the leg, was what ran out of time.
// [TH] อ่านแล้วล้างค่า จุดเดียวที่ตัดสินใจตามค่านี้จึงเป็นจุดเดียวกับที่รายงานมันได้
//      ถ้าปล่อยค้างไว้ ข้อความวินิจฉัยจะถูกพิมพ์จาก actuatorTick() ขณะที่การตัดสินใจอยู่ในสเก็ตช์
//      และกว่าลูปจะมาถึงหลังถูกบล็อกนานๆ เครื่องสถานะมักตัดสินว่าผิดพลาดด้วยตาข่ายของตัวเองไปก่อนแล้ว
//      actuatorOff() ที่ applyActuator() เรียกจึงยกเลิกตัวจับเวลาและล้างธงวินิจฉัยไปเรียบร้อย
//      รีเลย์ถูกตัดจริงแต่ไม่มีใครบอกได้ว่าสิ่งที่หมดเวลาคือลูป ไม่ใช่ขา
bool actuatorTakeCapTrip() {
  bool v = capTripped;
  capTripped = false;
  return v;
}
