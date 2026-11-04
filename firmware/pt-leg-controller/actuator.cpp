// การขับมอเตอร์ยก/ลดขาด้วยสะพานเอช BTS7960 พร้อมกลไก "ตัดก่อนต่อ" การไล่กำลังขับ
// และเพดานเวลาจ่ายไฟ
// BTS7960 drive for up/down, with break-before-make, ramping and a max-energize
// time cap.
#include "actuator.h"
#include <Arduino.h>
#include <esp_timer.h>
#include "config.h"

// The BTS7960's inputs need no active-low flip: RPWM, LPWM and the shared enable
// are all active-HIGH, and LOW is the safe level on every one of them.
//
// There is no firmware-controlled cut in the motor supply. The machine's hardware
// cut is the panel emergency stop, which opens the whole 12 V feed — motor rail,
// lamps and this MCU together. That covers a shorted output FET, which no amount
// of firmware can, and it covers a panic in this firmware, which a relay driven
// BY this firmware could not. It is manual, so it needs an operator present —
// which is already the rule this machine is documented under.
// [TH] ขาอินพุตของ BTS7960 ไม่ต้องกลับด้านแบบ active-low ทั้ง RPWM, LPWM และขาเปิดใช้งานที่ใช้ร่วมกัน
//      ล้วนสั่งงานด้วยลอจิกสูง และลอจิกต่ำคือระดับที่ปลอดภัยของทุกขา
//      ไม่มีการตัดไฟเลี้ยงมอเตอร์ที่สั่งด้วยเฟิร์มแวร์
//      การตัดระดับฮาร์ดแวร์ของเครื่องคือปุ่มหยุดฉุกเฉินที่หน้าปัด ซึ่งตัดไฟ 12 โวลต์ทั้งระบบ
//      ทั้งไฟมอเตอร์ ไฟหน้าปัด และตัวไมโครคอนโทรลเลอร์นี้พร้อมกัน
//      ครอบคลุมกรณีมอสเฟตขาออกลัดวงจร ซึ่งเฟิร์มแวร์ทำอะไรไม่ได้เลย
//      และครอบคลุมกรณีเฟิร์มแวร์นี้เองพัง ซึ่งรีเลย์ที่สั่งด้วยเฟิร์มแวร์นี้ก็ทำไม่ได้
//      เป็นการตัดด้วยมือ จึงต้องมีผู้ควบคุมอยู่ด้วย ซึ่งเป็นกฎที่เครื่องนี้ระบุไว้อยู่แล้ว

// state is the COMMANDED direction; driveDir is the half-bridge that currently
// carries duty. They are the same thing except while a normal stop fades out —
// there, state is already ACT_OFF but the motor is still turning, and every
// safety check below has to look at driveDir rather than state to see that.
// [TH] state คือทิศที่ถูกสั่ง ส่วน driveDir คือซีกสะพานที่กำลังจ่ายกำลังอยู่จริง
//      สองค่านี้เท่ากันเสมอ ยกเว้นระหว่างที่การหยุดตามปกติกำลังผ่อนกำลังลง
//      ตอนนั้น state เป็น ACT_OFF ไปแล้วแต่มอเตอร์ยังหมุนอยู่
//      การตรวจสอบความปลอดภัยทุกจุดด้านล่างจึงต้องดู driveDir ไม่ใช่ state
static ActState state = ACT_OFF;
static ActState driveDir = ACT_OFF;
static uint16_t duty = 0, dutyTarget = 0;  // กำลังขับปัจจุบัน และค่าที่กำลังไล่ไปหา
// Where the current leg of the ramp started, and when. Every step is measured
// from these rather than from the previous step: MOTOR_DUTY_MAX * 1 ms /
// MOTOR_RAMP_MS truncates to 2 of the 1023 counts, and stepping by a truncated 2
// every millisecond stretches a 400 ms ramp to 512 ms. Measuring the whole
// distance from the start of the leg has no accumulating error, and it costs one
// extra variable.
// [TH] จุดเริ่มและเวลาเริ่มของช่วงไล่กำลังปัจจุบัน ทุกขั้นวัดจากสองค่านี้ ไม่ใช่วัดจากขั้นก่อนหน้า
//      เพราะ MOTOR_DUTY_MAX คูณ 1 มิลลิวินาที หารด้วย MOTOR_RAMP_MS ถูกปัดเศษเหลือ 2 จาก 1023 หน่วย
//      การเดินทีละ 2 ที่ถูกปัดแล้วทุกมิลลิวินาทีจะยืดช่วงไล่ 400 มิลลิวินาทีเป็น 512 มิลลิวินาที
//      การวัดระยะทั้งหมดจากจุดเริ่มของช่วงจึงไม่มีความคลาดเคลื่อนสะสม และเสียตัวแปรเพิ่มแค่ตัวเดียว
static uint16_t rampFrom = 0;
static uint32_t rampAt = 0;
static uint32_t capMs = 0, energizeStart = 0;  // เพดานเวลา และเวลาที่เริ่มจ่ายไฟ
// volatile: written by the esp_timer callback on the other core as well as by the
// loop. [TH] ต้องเป็น volatile เพราะถูกเขียนจากฟังก์ชันเรียกกลับของตัวจับเวลาบนอีกคอร์ด้วย ไม่ใช่แค่จากลูป
static volatile bool capTripped = false;       // ธงว่าเพดานเวลาถูกตัดไปแล้ว

static bool pwmReady = false;

// Dead time between dropping one direction and driving the other.
//
// The relays are gone but the reason is not. Commanding a reversal apart is not
// the same as it being apart: a live reversal — STOP during a lift, which goes
// straight from UP to SafeLower's DOWN in one loop iteration — would otherwise
// slam a loaded, still-turning motor into the opposite direction in the
// microseconds between two writes. The BTS7960 survives that; the gearbox and
// the leg on it are what this protects.
// [TH] เวลาหน่วงระหว่างการหยุดขับทิศหนึ่งกับการเริ่มขับอีกทิศ
//      รีเลย์มอเตอร์หายไปแล้วแต่เหตุผลยังอยู่
//      การสั่งกลับทิศแยกกันไม่เท่ากับการที่มันแยกกันจริง
//      การกลับทิศขณะกำลังขับ เช่นกด STOP ตอนกำลังยก ซึ่งเปลี่ยนจากยกเป็นลดในลูปรอบเดียว
//      จะกระชากมอเตอร์ที่มีโหลดและยังหมุนอยู่ให้กลับทิศภายในเวลาไม่กี่ไมโครวินาทีระหว่างคำสั่งเขียนสองครั้ง
//      ตัว BTS7960 ทนได้ แต่สิ่งที่ป้องกันคือชุดเกียร์และขาที่วางอยู่บนนั้น
static const uint32_t DEADTIME_MS = 50;
static ActState pending = ACT_OFF;   // direction waiting out the dead time
// The duty that deferred direction asked for. Without it the reversal resumed at
// whatever request()'s default was, so a jog that reversed across the dead time
// came back at full power — a manual nudge turning into a full-speed stroke the
// moment the operator changed their mind about the direction.
// [TH] กำลังขับที่ทิศซึ่งถูกพักไว้ขอมา ถ้าไม่เก็บค่านี้ การกลับทิศจะกลับมาด้วยค่าตั้งต้นของ request()
//      การขยับเองที่กลับทิศข้ามช่วงเวลาหน่วงจึงกลับมาด้วยกำลังเต็ม
//      คือการขยับด้วยมือเบาๆ กลายเป็นการเคลื่อนที่เต็มกำลังทันทีที่ผู้ใช้เปลี่ยนใจเรื่องทิศทาง
static uint16_t pendingDuty = MOTOR_DUTY_MAX;
static uint32_t pendingAt = 0;       // when it may be driven
// When the drive was last actually at zero. The dead time has to be measured
// from this, not only from a live reversal: most direction changes pass through
// an OFF phase first, and the short ones do not wait. A fault during a lift cuts
// the drive and the operator's STOP — or a server stop in the same iteration —
// enters SafeLower immediately after, so DOWN could be driven tens of
// milliseconds later with the motor still coasting the other way.
// [TH] เวลาที่กำลังขับลงถึงศูนย์จริงครั้งล่าสุด ต้องวัดเวลาหน่วงจากค่านี้
//      ไม่ใช่วัดเฉพาะตอนกลับทิศขณะกำลังขับ
//      เพราะการเปลี่ยนทิศส่วนใหญ่ผ่านช่วงที่ดับก่อน และช่วงที่สั้นๆ ก็ไม่ได้รอ
//      ความผิดพลาดระหว่างยกขาจะตัดกำลังขับ แล้วการกด STOP ของผู้ใช้
//      หรือคำสั่งหยุดจากเซิร์ฟเวอร์ในลูปรอบเดียวกัน จะพาเข้าสถานะลดขาทันทีหลังจากนั้น
//      ทิศลงจึงอาจถูกขับห่างออกไปเพียงไม่กี่สิบมิลลิวินาที ขณะที่มอเตอร์ยังไหลไปทางเดิมอยู่
static uint32_t offAt = 0;

// Hardware backstop for the energize cap. actuatorTick() can only cut the drive
// when the loop reaches it, and the failure that makes a stroke run long — a loop
// blocked in a network call, an NVS commit, a panic — is the same failure that
// stops the tick from running at all. That made the "independent" second net
// depend on the very thing it was guarding. This timer runs off the esp_timer
// task instead, so it fires whether or not loop() is alive.
// [TH] แนวป้องกันสำรองระดับฮาร์ดแวร์สำหรับเพดานเวลาจ่ายไฟ
//      actuatorTick() ตัดกำลังขับได้เฉพาะตอนที่ลูปเดินมาถึงมันเท่านั้น
//      และความล้มเหลวที่ทำให้จ่ายไฟยาวเกิน เช่นลูปค้างอยู่ในการเรียกเครือข่าย การเขียน NVS หรือโปรแกรมพัง
//      คือความล้มเหลวเดียวกับที่ทำให้ actuatorTick() ไม่ได้ทำงานเลย
//      ตาข่ายกันพลาดชั้นที่สองจึงไปขึ้นกับสิ่งที่ตัวเองมีไว้เฝ้าพอดี
//      ตัวจับเวลานี้ทำงานบนงานของ esp_timer แทน จึงทำงานไม่ว่าลูปหลักจะยังเดินอยู่หรือไม่
static esp_timer_handle_t capTimer = nullptr;
static volatile bool capTimerFired = false;

// The only place PWM levels are written. The idle half-bridge is always zeroed
// BEFORE the driving one is raised, which is what break-before-make means on a
// bridge: RPWM and LPWM high together is a shoot-through command, and the fact
// that the BTS7960 refuses it internally is a property of the chip, not of this
// code.
// [TH] เป็นจุดเดียวที่เขียนค่ากำลังขับ ซีกที่ไม่ได้ใช้จะถูกลดเป็นศูนย์ก่อนที่ซีกที่ขับจะถูกยกขึ้นเสมอ
//      นี่คือความหมายของ "ตัดก่อนต่อ" บนสะพานเอช
//      การสั่ง RPWM และ LPWM สูงพร้อมกันคือการสั่งให้กระแสทะลุ
//      และการที่ BTS7960 ปฏิเสธคำสั่งนั้นเองเป็นคุณสมบัติของชิป ไม่ใช่ของโค้ดนี้
#if MOTOR_REVERSED
static const uint8_t PIN_LIFT = PIN_MOT_LPWM, PIN_LOWER = PIN_MOT_RPWM;
#else
static const uint8_t PIN_LIFT = PIN_MOT_RPWM, PIN_LOWER = PIN_MOT_LPWM;
#endif

static void driveRaw(ActState dir, uint16_t d) {
  if (dir != ACT_UP) ledcWrite(PIN_LIFT, 0);
  if (dir != ACT_DOWN) ledcWrite(PIN_LOWER, 0);
  if (dir == ACT_OFF || d == 0) {
    digitalWrite(PIN_MOT_EN, LOW);
    return;
  }
  digitalWrite(PIN_MOT_EN, HIGH);
  ledcWrite(dir == ACT_UP ? PIN_LIFT : PIN_LOWER, d);
}

// Everything the emergency paths need, and nothing else — no bookkeeping, so it
// is safe to call from the timer task as well as from the loop.
//
// The enable pin goes LOW first, deliberately. It is one GPIO write and it
// disables both half-bridges on its own, so even if a ledcWrite here interleaves
// with one on the loop's side, the bridge is already off before either lands.
// [TH] มีเฉพาะสิ่งที่เส้นทางฉุกเฉินต้องใช้ ไม่มีการปรับค่าสถานะ
//      จึงเรียกจากงานของตัวจับเวลาได้เช่นเดียวกับเรียกจากลูป
//      ขาเปิดใช้งานถูกสั่งลอจิกต่ำก่อนโดยตั้งใจ เป็นการเขียนขา GPIO ครั้งเดียว
//      และปิดสะพานทั้งสองซีกได้ด้วยตัวเอง
//      ต่อให้การเขียนค่ากำลังขับตรงนี้ไปสลับจังหวะกับการเขียนจากฝั่งลูป สะพานก็ปิดไปก่อนแล้วทั้งคู่
static void killPins() {
  digitalWrite(PIN_MOT_EN, LOW);
  ledcWrite(PIN_MOT_RPWM, 0);
  ledcWrite(PIN_MOT_LPWM, 0);
}

// Cuts the drive from the timer task. Only the pins are touched; the
// bookkeeping is left to actuatorTick() on the main loop, so nothing races over
// the state variables.
//
// This cannot cover a shorted output FET — such a FET drives the motor from B+
// no matter what these pins say. That case belongs to the panel emergency stop,
// which is the machine's only hardware cut.
// [TH] ตัดกำลังขับจากงานของตัวจับเวลา แตะเฉพาะขาสัญญาณเท่านั้น
//      ส่วนการปรับค่าสถานะปล่อยให้ actuatorTick() ในลูปหลักทำ จะได้ไม่มีการแย่งเขียนตัวแปรสถานะกัน
//      ตรงนี้ครอบคลุมกรณีมอสเฟตขาออกลัดวงจรไม่ได้
//      เพราะมอสเฟตแบบนั้นขับมอเตอร์จากขา B+ ไม่ว่าขาเหล่านี้จะสั่งอะไร
//      กรณีนั้นเป็นหน้าที่ของปุ่มหยุดฉุกเฉินที่หน้าปัด ซึ่งเป็นการตัดระดับฮาร์ดแวร์เพียงอย่างเดียวของเครื่อง
static void capTimerCb(void*) {
  killPins();
  // capTripped is latched here, not left for the loop to derive from the flag
  // below. The loop's own order is applyActuator() then actuatorTick(), and by
  // the time it resumes the state machine has usually faulted on its own net
  // already — so applyActuator() calls actuatorOff(), which disarms and clears
  // the flag before actuatorTick() ever reads it. The drive was still cut, but
  // the trip was never reported. Latching it here survives that, and request()
  // clears it on the next energize.
  // [TH] ตั้งค่า capTripped ตรงนี้ ไม่ปล่อยให้ลูปไปสรุปเอาจากธงด้านล่าง
  //      ลำดับในลูปคือ applyActuator() แล้วจึง actuatorTick()
  //      กว่าลูปจะกลับมาทำงาน เครื่องสถานะมักตัดสินว่าผิดพลาดด้วยตาข่ายของตัวเองไปแล้ว
  //      applyActuator() จึงเรียก actuatorOff() ซึ่งยกเลิกตัวจับเวลาและล้างธงทิ้งก่อนที่ actuatorTick() จะได้อ่าน
  //      กำลังขับถูกตัดจริง แต่ไม่มีการรายงานว่าถูกตัด การเก็บค่าไว้ตรงนี้จะรอดจากลำดับนั้น
  //      และ request() จะล้างค่าให้เองตอนจ่ายไฟครั้งถัดไป
  capTripped = true;
  capTimerFired = true;
}

// Armed whenever the motor is driven, cancelled the moment it is genuinely at
// zero. A fade-out re-arms it rather than running uncovered — see
// actuatorSoftOff().
// [TH] ตั้งเวลาทุกครั้งที่ขับมอเตอร์ และยกเลิกทันทีที่กำลังขับลงถึงศูนย์จริง
//      ช่วงผ่อนกำลังลงจะตั้งเวลาใหม่แทนการปล่อยให้ทำงานโดยไม่มีอะไรคุม
//      ดูรายละเอียดที่ actuatorSoftOff()
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
  // drive down anyway, so a genuine trip during a driving phase is untouched: the
  // driving paths early-return without disarming.
  // [TH] ล้างค่าหลังสั่งหยุด ไม่ใช่ก่อน เพราะฟังก์ชันเรียกกลับที่ถูกส่งไปทำงานบนอีกคอร์แล้ว
  //      อาจตั้งค่าธงในช่วงระหว่างสองคำสั่งนี้ ซึ่งจะถูกอ่านในรอบถัดไปว่าเป็นการตัดเพราะเกินเพดานเวลา
  //      ของการเคลื่อนที่ที่จบไปตามปกติแล้ว และทำให้รอบที่ปกติกลายเป็นรอบที่ผิดพลาด
  //      จะมาถึงตรงนี้เฉพาะเส้นทางที่กำลังจะลดกำลังขับอยู่แล้ว การตัดจริงระหว่างสถานะที่กำลังขับเคลื่อนจึงไม่ถูกแตะ
  //      เพราะเส้นทางที่กำลังขับเคลื่อนจะคืนค่าออกไปก่อนโดยไม่เรียกฟังก์ชันนี้
  capTimerFired = false;
}

void actuatorBegin() {
  // Level before mode, the same order beginLamp() uses and for the same reason. A
  // pin comes out of reset high-Z; pinMode(OUTPUT) makes it drive whatever the
  // output register already holds, which is LOW. On the BTS7960's three
  // active-high inputs LOW is already the safe level, so the order costs nothing
  // here — it is kept so the rule is one rule across this file and panel_lamps,
  // where the active-low lamp board does depend on it.
  // [TH] ตั้งระดับก่อนตั้งโหมด ลำดับเดียวกับ beginLamp() และด้วยเหตุผลเดียวกัน
  //      ขา GPIO ออกจากรีเซ็ตมาเป็นอินพุตอิมพีแดนซ์สูง
  //      คำสั่ง pinMode(OUTPUT) จะทำให้ขาเริ่มขับค่าที่ค้างอยู่ในรีจิสเตอร์ ซึ่งคือ LOW
  //      ขาอินพุตสามขาของ BTS7960 ที่สั่งงานด้วยลอจิกสูงนั้น LOW ปลอดภัยอยู่แล้ว
  //      ลำดับนี้จึงไม่ได้ช่วยอะไรตรงนี้ แต่คงไว้เพื่อให้กฎเป็นข้อเดียวกันทั้งไฟล์นี้และ panel_lamps
  //      ซึ่งบอร์ดหลอดไฟแบบสั่งงานด้วยลอจิกต่ำต้องพึ่งลำดับนี้จริง
  digitalWrite(PIN_MOT_EN, LOW);
  pinMode(PIN_MOT_EN, OUTPUT);
  digitalWrite(PIN_MOT_RPWM, LOW);
  pinMode(PIN_MOT_RPWM, OUTPUT);
  digitalWrite(PIN_MOT_LPWM, LOW);
  pinMode(PIN_MOT_LPWM, OUTPUT);
  // ledcAttach takes the pins over from here; both are written to zero duty
  // immediately, because attaching leaves the channel's duty register at whatever
  // it held and "whatever it held" is not a safe level to assume.
  // [TH] จากตรงนี้ไป ledcAttach เป็นผู้ควบคุมขาทั้งสอง
  //      ต้องเขียนกำลังขับเป็นศูนย์ทันทีทั้งคู่ เพราะการผูกช่องสัญญาณจะทิ้งค่าเดิมไว้ในรีจิสเตอร์
  //      และ "ค่าเดิมที่ทิ้งไว้" ไม่ใช่ค่าที่จะเดาว่าปลอดภัยได้
  pwmReady = ledcAttach(PIN_MOT_RPWM, MOTOR_PWM_HZ, MOTOR_PWM_BITS);
  pwmReady = ledcAttach(PIN_MOT_LPWM, MOTOR_PWM_HZ, MOTOR_PWM_BITS) && pwmReady;
  ledcWrite(PIN_MOT_RPWM, 0);
  ledcWrite(PIN_MOT_LPWM, 0);
  state = ACT_OFF;
  driveDir = ACT_OFF;
  pending = ACT_OFF;
  duty = dutyTarget = 0;
  const esp_timer_create_args_t args = {capTimerCb, nullptr, ESP_TIMER_TASK, "actcap", false};
  // Recorded, not printed: actuatorBegin() is the first call in setup(), before
  // Serial.begin(), and HardwareSerial drops writes while its UART is null — the
  // message would have gone nowhere, which is the one place it must not go.
  // setup() reports it once the port is open. Same for pwmReady above.
  // [TH] เก็บค่าไว้ ไม่พิมพ์ทันที เพราะ actuatorBegin() เป็นคำสั่งแรกใน setup() ก่อน Serial.begin()
  //      และไลบรารีจะทิ้งข้อมูลที่เขียนตอนที่พอร์ตยังไม่ถูกเปิด ข้อความจะหายไปเฉยๆ
  //      ซึ่งเป็นสิ่งเดียวที่ต้องไม่เกิดกับข้อความนี้ ให้ setup() รายงานหลังเปิดพอร์ตแล้ว
  //      ค่า pwmReady ด้านบนก็ด้วยเหตุผลเดียวกัน
  if (esp_timer_create(&args, &capTimer) != ESP_OK) capTimer = nullptr;
}

bool actuatorCapTimerReady() { return capTimer != nullptr; }

bool actuatorPwmReady() { return pwmReady; }

// Drives `dir`, or parks the request behind the dead time when the motor is
// still turning the other way. Idempotent: applyActuator() calls these every loop
// while a phase runs. Re-stamping energizeStart every time would keep
// nowMs-energizeStart near zero forever and the travel cap could never trip, so
// it is stamped only on a real transition.
// [TH] ขับไปในทิศที่ขอ หรือพักคำขอไว้รอเวลาหน่วงถ้ามอเตอร์ยังหมุนไปอีกทางอยู่
//      ทำซ้ำได้ปลอดภัย: applyActuator() เรียกทุกลูประหว่างที่สถานะกำลังทำงาน
//      ถ้าจับเวลา energizeStart ใหม่ทุกครั้ง ค่าจะเกือบศูนย์ตลอดและเพดานเวลาจะไม่มีวันตัด
//      จึงจับเวลาเฉพาะตอนเปลี่ยนสถานะจริงเท่านั้น
static void request(ActState dir, uint16_t target) {
  // Unsigned here, unlike the compares against the loop's captured `now`. offAt
  // is always stamped from millis() and so is never ahead of it,
  // which means the signed form bought nothing — and cost a great deal: for an
  // elapsed time between 24.9 and 49.7 days the cast goes negative, every request
  // parks in pending, and the honour test below never fires either. A machine
  // left powered for 25 days would answer START and STOP with a stroke that never
  // drives, faulting after the cap with zero motion, until a reboot.
  // [TH] ตรงนี้ใช้แบบไม่มีเครื่องหมาย ต่างจากการเทียบกับค่า now ที่ลูปอ่านไว้
  //      ค่า offAt ถูกจับเวลาจาก millis() เสมอ จึงไม่มีทางล้ำหน้า millis() ได้
  //      การใช้แบบมีเครื่องหมายจึงไม่ได้ประโยชน์อะไรเลย แต่เสียหายมาก
  //      ถ้าเวลาที่ผ่านไปอยู่ระหว่าง 24.9 ถึง 49.7 วัน การแปลงชนิดจะได้ค่าติดลบ
  //      คำขอทุกครั้งจะไปพักรอ และเงื่อนไขปลดการรอด้านล่างก็จะไม่เป็นจริงเช่นกัน
  //      เครื่องที่เปิดทิ้งไว้ 25 วันจะตอบปุ่มเริ่มและปุ่มหยุดด้วยการเคลื่อนที่ที่ไม่ขับมอเตอร์เลย
  //      แล้วจบด้วยความผิดพลาดหลังครบเพดานเวลาโดยไม่ขยับ จนกว่าจะรีบูต
  // Already there, but the duty may still differ: a session stroke taking over a
  // jog in the same direction would otherwise stay at MOTOR_JOG_DUTY and cover
  // half its timed travel. Re-ramped from where it is, not restarted.
  // [TH] อยู่ในทิศนั้นแล้ว แต่กำลังขับอาจต่างกัน จังหวะของรอบที่รับช่วงต่อจากการขยับเองในทิศเดียวกัน
  //      จะค้างอยู่ที่ MOTOR_JOG_DUTY และเคลื่อนที่ได้แค่ครึ่งระยะที่จับเวลาไว้ จึงไล่กำลังต่อจากค่าปัจจุบัน
  if (state == dir) {
    pending = ACT_OFF;
    if (dutyTarget != target) {
      dutyTarget = target;
      rampFrom = duty;
      rampAt = millis();
    }
    return;
  }
  if (pending == dir) { pendingDuty = target; return; }  // already waiting — รออยู่แล้ว
  // Still carrying duty the other way. This covers a full-power live reversal and
  // a reversal that catches a fade-out part-way down, which is the same hazard at
  // a lower speed — a fade is not a substitute for the dead time.
  // [TH] ยังจ่ายกำลังไปทางตรงข้ามอยู่ ครอบคลุมทั้งการกลับทิศขณะขับเต็มกำลัง
  //      และการกลับทิศที่ไปทับช่วงผ่อนกำลังลงกลางคัน ซึ่งเป็นอันตรายแบบเดียวกันที่ความเร็วต่ำกว่า
  //      การผ่อนกำลังลงไม่ใช่สิ่งที่ใช้แทนเวลาหน่วงได้
  if (driveDir != ACT_OFF && driveDir != dir) {
    killPins();
    driveDir = ACT_OFF;
    duty = dutyTarget = 0;
    disarmCapTimer();
    state = ACT_OFF;
    offAt = millis();
    pending = dir;
    pendingDuty = target;
    pendingAt = offAt + DEADTIME_MS;
    return;
  }
  // Drive is at zero, but not for long enough yet: wait out the remainder before
  // driving the other way.
  // [TH] กำลังขับเป็นศูนย์แล้ว แต่ยังไม่นานพอ ให้รอเวลาที่เหลือก่อนจึงค่อยสั่งไปอีกทาง
  if (driveDir == ACT_OFF && (millis() - offAt) < DEADTIME_MS) {
    pending = dir;
    pendingDuty = target;
    pendingAt = offAt + DEADTIME_MS;
    return;
  }
  // Either the drive is off and the dead time is served, or a fade-out on this
  // same direction is still running and the stroke simply resumes from whatever
  // duty it had reached — a STOP inside the first 400 ms of a Rest does exactly
  // that, and re-ramping from zero would be a second jolt for no reason.
  // [TH] ไม่กำลังขับดับและเวลาหน่วงครบแล้ว ก็เป็นช่วงผ่อนกำลังลงในทิศเดิมที่ยังไม่จบ
  //      แล้วจังหวะใหม่เร่งต่อจากกำลังขับที่ค้างอยู่ตรงนั้นเลย
  //      การกด STOP ภายใน 400 มิลลิวินาทีแรกของช่วงพักเป็นกรณีนี้พอดี
  //      การไล่กำลังใหม่จากศูนย์จะเป็นการกระชากครั้งที่สองโดยไม่ได้อะไร
  state = dir;
  driveDir = dir;
  dutyTarget = target;
  rampFrom = duty;  // 0 for a fresh stroke, mid-fade for a resumed one
                    // [TH] เป็นศูนย์ถ้าเริ่มใหม่ หรือเป็นค่ากลางทางถ้าเร่งต่อจากช่วงผ่อนกำลัง
  rampAt = millis();
  driveRaw(dir, duty);
  pending = ACT_OFF;
  energizeStart = millis();
  capTripped = false;
  armCapTimer();
}

void actuatorUp() { request(ACT_UP, MOTOR_DUTY_MAX); }

void actuatorDown() { request(ACT_DOWN, MOTOR_DUTY_MAX); }

// Jog drives through the same request() as a stroke, so it inherits every safety
// path unchanged: break-before-make, the 50 ms dead time on a reversal, the
// energize cap and its hardware timer. The only difference is the duty it ramps
// to. Nothing here needs its own kill path, which is the point — a second way to
// drive the motor would be a second way to get that wrong.
// [TH] การขยับเองสั่งผ่าน request() ตัวเดียวกับการเคลื่อนที่ปกติ จึงได้กลไกความปลอดภัยทุกอย่างมาครบ
//      ทั้งตัดก่อนต่อ เวลาหน่วง 50 มิลลิวินาทีเมื่อกลับทิศ เพดานเวลาจ่ายไฟ และตัวจับเวลาฮาร์ดแวร์ของมัน
//      ต่างกันแค่กำลังขับที่ไล่ไปหา และไม่ต้องมีเส้นทางตัดกำลังของตัวเอง ซึ่งเป็นประเด็นสำคัญ
//      เพราะการมีทางขับมอเตอร์ทางที่สองก็คือการมีทางที่จะทำพลาดทางที่สอง
void actuatorJog(ActState dir) {
  if (dir == ACT_UP || dir == ACT_DOWN) request(dir, MOTOR_JOG_DUTY);
  else actuatorSoftOff();
}

void actuatorOff() {
  // The pins are re-asserted every call, deliberately: applyActuator() runs this
  // every loop in the phases that must not drive, so an input disturbed by
  // anything else is driven back to zero within one iteration. Only the timer and
  // the state bookkeeping are skipped when there is nothing to undo.
  // [TH] สั่งระดับขาซ้ำทุกครั้งโดยตั้งใจ เพราะ applyActuator() เรียกฟังก์ชันนี้ทุกลูป
  //      ในสถานะที่ต้องไม่ขับเคลื่อน ถ้าขาอินพุตถูกรบกวนจากอย่างอื่นก็จะถูกสั่งกลับเป็นศูนย์ภายในหนึ่งรอบ
  //      ส่วนตัวจับเวลาและการปรับค่าสถานะจะข้ามไปเมื่อไม่มีอะไรต้องยกเลิก
  killPins();
  bool live = (state != ACT_OFF) || (driveDir != ACT_OFF);
  duty = dutyTarget = 0;
  driveDir = ACT_OFF;
  if (!live && pending == ACT_OFF) return;  // nothing to undo — ไม่มีอะไรต้องยกเลิก
  disarmCapTimer();
  // Stamped only on the transition, so idling does not keep pushing the dead time
  // forward and delay a genuine start.
  // [TH] จับเวลาเฉพาะตอนเปลี่ยนสถานะ การอยู่เฉยๆ จะได้ไม่เลื่อนเวลาหน่วงออกไปเรื่อยๆ
  //      จนทำให้การเริ่มเคลื่อนที่จริงถูกหน่วงตามไปด้วย
  if (live) offAt = millis();
  state = ACT_OFF;
  pending = ACT_OFF;  // an OFF cancels a reversal still waiting out its dead time
                      // [TH] คำสั่งดับจะยกเลิกการกลับทิศที่ยังรอเวลาหน่วงอยู่ด้วย
}

void actuatorSoftOff() {
  // Nothing turning: this IS actuatorOff(), including the every-loop re-assert of
  // the pins. Falling through to it rather than returning early is what lets
  // applyActuator() call soft-off unconditionally for Idle, Hold and Rest.
  // [TH] ถ้าไม่มีอะไรหมุนอยู่ ฟังก์ชันนี้คือ actuatorOff() ทุกประการ รวมถึงการสั่งระดับขาซ้ำทุกลูปด้วย
  //      การส่งต่อไปที่ actuatorOff() แทนการคืนค่าออกไปเฉยๆ
  //      คือสิ่งที่ทำให้ applyActuator() เรียกการหยุดแบบนุ่มนวลได้เลยโดยไม่ต้องมีเงื่อนไข
  //      ทั้งในสถานะว่าง สถานะค้าง และสถานะพัก
  if (driveDir == ACT_OFF || duty == 0) { actuatorOff(); return; }
  if (state == ACT_OFF && dutyTarget == 0) return;  // already fading — กำลังผ่อนลงอยู่แล้ว
  state = ACT_OFF;
  dutyTarget = 0;
  rampFrom = duty;
  rampAt = millis();
  pending = ACT_OFF;
  // Re-armed for the fade with a FULL cap, not disarmed with the stroke and not
  // given a short window of its own.
  //
  // Disarming would leave the motor turning with no hardware backstop, and the
  // loop is no substitute: the failure that strands a fade half-way down is the
  // same blocked loop that stops the ramp advancing. A short window sized to the
  // ramp — the obvious answer — is wrong for a reason worth writing down: this
  // loop's blocking budget is two 400 ms HTTP timeouts per iteration, so an
  // unreachable server routinely costs more than a ramp's worth of wall clock,
  // and a fade window near MOTOR_RAMP_MS would fault the machine in Hold every
  // session the Wi-Fi was down. A full cap cannot false-trip that way. What it
  // costs is honest: a stroke plus a stranded fade can drive for up to twice
  // capMs before the backstop lands, where the relay build had one capMs.
  // [TH] ตั้งเวลาใหม่ให้ช่วงผ่อนกำลังลงด้วยเพดานเต็ม ไม่ใช่ยกเลิกไปพร้อมกับจังหวะที่จบ
  //      และไม่ใช่ให้หน้าต่างเวลาสั้นๆ ของตัวเอง
  //      การยกเลิกไปเลยจะทิ้งให้มอเตอร์หมุนโดยไม่มีแนวป้องกันระดับฮาร์ดแวร์
  //      และลูปใช้แทนไม่ได้ เพราะความล้มเหลวที่ทำให้การผ่อนกำลังค้างกลางทาง
  //      คือลูปที่ถูกบล็อกตัวเดียวกับที่ทำให้การไล่กำลังไม่เดินต่อ
  //      ส่วนการให้หน้าต่างสั้นเท่าช่วงไล่กำลังซึ่งเป็นคำตอบที่ดูตรงไปตรงมาที่สุดนั้นผิด ด้วยเหตุผลที่ควรบันทึกไว้
  //      งบเวลาที่ลูปนี้ยอมให้บล็อกได้คือ HTTP timeout 400 มิลลิวินาทีสองครั้งต่อหนึ่งรอบลูป
  //      เซิร์ฟเวอร์ที่ติดต่อไม่ได้จึงกินเวลาจริงมากกว่าหนึ่งช่วงไล่กำลังเป็นปกติ
  //      หน้าต่างเวลาที่ใกล้เคียง MOTOR_RAMP_MS จะทำให้เครื่องขึ้นความผิดพลาดตอนค้างขาทุกรอบที่ WiFi ใช้ไม่ได้
  //      เพดานเต็มไม่มีทางตัดผิดพลาดแบบนั้น สิ่งที่ต้องแลกก็บอกกันตรงๆ
  //      คือหนึ่งจังหวะบวกกับการผ่อนกำลังที่ค้าง อาจขับมอเตอร์ได้นานถึงสองเท่าของ capMs ก่อนที่แนวป้องกันจะตัด
  //      ในขณะที่รุ่นที่ใช้รีเลย์มีเพดานเดียวคือ capMs
  armCapTimer();
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
  // reads the flag — so the drive had been cut by the backstop and nothing said
  // so.
  // [TH] จงใจไม่ล้างค่า capTripped ตรงนี้ มีแต่การจ่ายไฟจริงเท่านั้นที่ล้างได้ ซึ่งอยู่ใน request()
  //      การล้างค่าตอนเริ่มจับเวลาใหม่จะทิ้งการตัดที่ตัวจับเวลาฮาร์ดแวร์เพิ่งบันทึกไว้
  //      เพราะการเริ่มจับเวลาใหม่ทำงานใน applyActuator() ซึ่งอยู่ก่อนที่สเก็ตช์จะอ่านค่าธง
  //      กำลังขับถูกแนวป้องกันสำรองตัดไปแล้วโดยไม่มีอะไรบอก
  energizeStart = millis();
  armCapTimer();
}

void actuatorSetCap(uint32_t ms) { capMs = ms; }  // ตั้งเพดานเวลาจ่ายไฟสูงสุด

// One step of the fade, in or out. Nothing else writes `duty`.
// [TH] การไล่กำลังขับหนึ่งขั้น ทั้งขาขึ้นและขาลง ไม่มีที่อื่นเขียนค่า duty
static void rampStep(uint32_t nowMs) {
  if (driveDir == ACT_OFF || duty == dutyTarget) return;
  // Signed, for the same reason the cap compare below is: rampAt is stamped with
  // a fresh millis() inside request(), while nowMs was captured at the top of the
  // loop, so on any iteration that crosses a millisecond boundary rampAt is
  // nowMs+1. Unsigned, that subtraction is ~4.29e9, the step saturates, and the
  // ramp this whole file exists for is skipped on a coin flip at every start.
  // [TH] ใช้การเทียบแบบมีเครื่องหมายด้วยเหตุผลเดียวกับการเทียบเพดานเวลาด้านล่าง
  //      ค่า rampAt ถูกจับเวลาจาก millis() ใหม่ภายใน request()
  //      ขณะที่ nowMs อ่านไว้ตั้งแต่ต้นลูป ในรอบที่คร่อมขอบมิลลิวินาที ค่า rampAt จึงเป็น nowMs+1
  //      ถ้าลบแบบไม่มีเครื่องหมายจะได้ราวสี่พันล้าน ขั้นการไล่จะพุ่งจนสุด
  //      และการไล่กำลังซึ่งเป็นเหตุผลทั้งหมดของไฟล์นี้จะถูกข้ามไปแบบเสี่ยงทายทุกครั้งที่เริ่ม
  int32_t dt = (int32_t)(nowMs - rampAt);
  if (dt <= 0) return;
  // Distance covered since this leg of the ramp began. A fade that starts from
  // part-way — a STOP caught mid-fade, a stroke resumed before it finished
  // fading out — therefore takes proportionally less than a full MOTOR_RAMP_MS,
  // which is what makes the rate constant rather than the duration.
  // [TH] ระยะที่เดินมาแล้วนับจากจุดเริ่มของช่วงไล่กำลังนี้
  //      ช่วงที่เริ่มจากกลางทาง เช่นการกด STOP ที่ไปทับช่วงผ่อนกำลัง
  //      หรือจังหวะที่กลับมาเร่งต่อก่อนที่การผ่อนกำลังจะจบ
  //      จึงใช้เวลาน้อยกว่า MOTOR_RAMP_MS เต็มตามสัดส่วน
  //      สิ่งที่คงที่จึงเป็นอัตราการเปลี่ยน ไม่ใช่ระยะเวลา
  uint32_t moved = (uint32_t)MOTOR_DUTY_MAX * (uint32_t)dt / MOTOR_RAMP_MS;
  if (moved == 0) return;  // not a whole count yet — ยังไม่ครบหนึ่งหน่วย
  if (dutyTarget > rampFrom)
    duty = (rampFrom + moved >= dutyTarget) ? dutyTarget : (uint16_t)(rampFrom + moved);
  else
    duty = (rampFrom <= moved || rampFrom - moved <= dutyTarget) ? dutyTarget
                                                                 : (uint16_t)(rampFrom - moved);
  driveRaw(driveDir, duty);
  if (duty == 0) {
    // The fade has landed. This is the moment the motor is genuinely at zero, so
    // it is the moment the dead time starts counting and the fade's own backstop
    // is no longer needed.
    // [TH] การผ่อนกำลังลงจบแล้ว นี่คือจังหวะที่มอเตอร์เป็นศูนย์จริง
    //      จึงเป็นจังหวะที่เวลาหน่วงเริ่มนับ และแนวป้องกันของช่วงผ่อนกำลังไม่จำเป็นอีกต่อไป
    driveDir = ACT_OFF;
    offAt = nowMs;
    disarmCapTimer();
  }
}

void actuatorTick(uint32_t nowMs) {
  // The hardware backstop got there first: the drive is already dead, so this
  // only catches the bookkeeping up and reports it. Reached whenever the loop was
  // blocked past the cap — which is the case the timer exists for.
  // [TH] แนวป้องกันระดับฮาร์ดแวร์ตัดไปก่อนแล้ว กำลังขับดับอยู่
  //      ตรงนี้แค่ปรับค่าสถานะให้ตรงและรายงานผล
  //      จะมาถึงตรงนี้เมื่อลูปถูกบล็อกนานเกินเพดานเวลา ซึ่งเป็นกรณีที่ตัวจับเวลามีไว้เพื่อมัน
  // A reversal that has waited out its dead time is honoured before anything
  // else. Handling it after the cap branch let that branch clear `pending` and
  // drop the queued direction; applyActuator then re-issued it on the next
  // iteration, driving the new direction about 2 ms after the old one dropped
  // instead of the 50 ms this file exists to guarantee.
  // [TH] การกลับทิศที่รอเวลาหน่วงครบแล้วจะถูกจัดการก่อนสิ่งอื่น
  //      ถ้าจัดการหลังส่วนตรวจเพดานเวลา ส่วนนั้นจะล้างค่า pending และทิ้งทิศที่รอคิวอยู่
  //      แล้ว applyActuator จะสั่งใหม่ในลูปรอบถัดไป ทำให้ขับทิศใหม่ห่างจากตอนดับทิศเก่าราว 2 มิลลิวินาที
  //      แทนที่จะเป็น 50 มิลลิวินาทีตามที่ไฟล์นี้มีไว้เพื่อรับประกัน
  if (pending != ACT_OFF && (int32_t)(nowMs - pendingAt) >= 0) {
    ActState dir = pending;
    pending = ACT_OFF;
    request(dir, pendingDuty);
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
    if (state != ACT_OFF || driveDir != ACT_OFF) offAt = nowMs;  // the callback cut it — ฟังก์ชันเรียกกลับเป็นคนตัด
    state = ACT_OFF;
    pending = ACT_OFF;
    driveDir = ACT_OFF;
    duty = dutyTarget = 0;
    capTripped = true;
    return;
  }

  rampStep(nowMs);

  // [TH] ตรวจทุกลูป: ถ้าจ่ายไฟค้างเกินเพดานเวลา ให้ตัดกำลังขับและตั้งธง capTripped
  // A fade-out is deliberately outside this check — state is already ACT_OFF by
  // then and the stroke's cap is spent, so the fade is covered by the full cap
  // actuatorSoftOff() re-arms the hardware timer with instead.
  // [TH] ช่วงผ่อนกำลังลงอยู่นอกการตรวจนี้โดยตั้งใจ เพราะตอนนั้น state เป็น ACT_OFF ไปแล้ว
  //      และเพดานเวลาของจังหวะนั้นถูกใช้หมดแล้ว
  //      ช่วงผ่อนกำลังจึงใช้เพดานเต็มที่ actuatorSoftOff() ตั้งให้ตัวจับเวลาฮาร์ดแวร์ใหม่แทน
  if (state == ACT_OFF || capMs == 0) return;
  // Signed, because energizeStart can be AHEAD of nowMs. The loop captures
  // millis() once at the top and passes that same value all the way down, but a
  // stroke started part-way through the iteration stamps a fresh millis() —
  // so on any iteration that crosses a millisecond boundary, energizeStart is
  // nowMs+1. Unsigned, that subtraction is ~4.29e9, instantly past any cap: the
  // stroke was cut and the session faulted in the same iteration it started, a
  // zero-second faulted row on the card with no cause. A coin flip on every
  // energize, including the safe-lower out of a fault.
  // [TH] ใช้การเทียบแบบมีเครื่องหมาย เพราะ energizeStart อาจล้ำหน้า nowMs ได้
  //      ลูปอ่านค่า millis() ครั้งเดียวที่ต้นรอบแล้วส่งค่านั้นต่อไปทั้งรอบ
  //      แต่จังหวะที่เริ่มขับกลางรอบจะจับเวลา millis() ใหม่
  //      ในรอบที่คร่อมขอบมิลลิวินาที ค่า energizeStart จึงเป็น nowMs+1
  //      ถ้าลบแบบไม่มีเครื่องหมายจะได้ราวสี่พันล้าน ซึ่งเกินเพดานทุกค่าทันที
  //      การเคลื่อนที่จะถูกตัดและรอบถูกตัดสินว่าผิดพลาดในรอบเดียวกับที่เพิ่งเริ่ม
  //      ได้แถวที่ผิดพลาดความยาวศูนย์วินาทีลงการ์ดโดยไม่มีสาเหตุ
  //      และเป็นการเสี่ยงแบบนี้ทุกครั้งที่จ่ายไฟ รวมถึงตอนลดขาลงหลังเกิดความผิดพลาดด้วย
  if ((int32_t)(nowMs - energizeStart) >= (int32_t)capMs) {
    killPins();
    disarmCapTimer();
    offAt = nowMs;
    state = ACT_OFF;
    driveDir = ACT_OFF;
    duty = dutyTarget = 0;
    capTripped = true;
  }
}

// Consumed on read, so the one place that acts on it is also the one place that
// can report it. Leaving it latched meant the diagnostic was printed from
// actuatorTick() while the decision was taken in the sketch — and by the time the
// loop got there after a long block, the state machine had usually faulted on its
// own net first, so applyActuator()'s actuatorOff() had already disarmed and
// cleared the diagnostic flag. The drive was cut and nobody could tell that the
// loop, rather than the leg, was what ran out of time.
// [TH] อ่านแล้วล้างค่า จุดเดียวที่ตัดสินใจตามค่านี้จึงเป็นจุดเดียวกับที่รายงานมันได้
//      ถ้าปล่อยค้างไว้ ข้อความวินิจฉัยจะถูกพิมพ์จาก actuatorTick() ขณะที่การตัดสินใจอยู่ในสเก็ตช์
//      และกว่าลูปจะมาถึงหลังถูกบล็อกนานๆ เครื่องสถานะมักตัดสินว่าผิดพลาดด้วยตาข่ายของตัวเองไปก่อนแล้ว
//      actuatorOff() ที่ applyActuator() เรียกจึงยกเลิกตัวจับเวลาและล้างธงวินิจฉัยไปเรียบร้อย
//      กำลังขับถูกตัดจริงแต่ไม่มีใครบอกได้ว่าสิ่งที่หมดเวลาคือลูป ไม่ใช่ขา
bool actuatorTakeCapTrip() {
  bool v = capTripped;
  capTripped = false;
  return v;
}
