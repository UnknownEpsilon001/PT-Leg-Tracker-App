#pragma once

// ค่าคงที่การตั้งค่าฮาร์ดแวร์ของบอร์ดควบคุม (ESP32 ฝั่งคอนโทรลเลอร์)
// รวมหมายเลขขา GPIO ของตัวขับมอเตอร์ ปุ่ม และหลอดไฟ กับค่าตั้งต้นของรอบการทำงาน
// Hardware pin map + default cycle timings for the controller MCU.

#define FW_VERSION "0.1.0"

// The motor is driven by a BTS7960 H-bridge module, not by a pair of relays. The
// two signal wires keep the connectors the relay coils used, so no harness is
// re-run: RPWM takes P3 and LPWM takes CN1. Inputs are active-HIGH — there is no
// RELAY_ACTIVE_LOW for the motor any more, and LOW is the safe level on all
// three signal pins.
// [TH] มอเตอร์ขับด้วยโมดูลสะพานเอช BTS7960 ไม่ใช่รีเลย์คู่เดิมแล้ว
//      สายสัญญาณสองเส้นยังใช้ขั้วต่อชุดเดิมของขดลวดรีเลย์ จึงไม่ต้องเดินสายใหม่
//      RPWM ใช้ขั้ว P3 และ LPWM ใช้ขั้ว CN1
//      ขาสัญญาณของโมดูลนี้สั่งงานด้วยลอจิกสูง จึงไม่มี RELAY_ACTIVE_LOW สำหรับมอเตอร์อีกต่อไป
//      และลอจิกต่ำคือระดับที่ปลอดภัยของขาสัญญาณทั้งสามขา
#define PIN_MOT_RPWM 22 // P3 connector — ขา RPWM สั่งมอเตอร์ "ยกขึ้น"
#define PIN_MOT_LPWM 27 // CN1 connector — ขา LPWM สั่งมอเตอร์ "ลดลง"
// Which half-bridge lifts depends on which way round the motor leads sit on the
// BTS7960's M+/M- terminals. On the first bench test the leg went DOWN during
// Lift and UP during Lower, so this machine is wired reversed: 1 makes LPWM lift
// and RPWM lower. Set 0 instead if the motor leads are ever swapped back.
// Check it with the leg unloaded: START must raise it.
// [TH] ซีกไหนเป็นตัวยกขึ้นอยู่กับว่าสายมอเตอร์ต่อเข้าขั้ว M+/M- ของ BTS7960 ด้านไหน
//      ทดสอบครั้งแรกพบว่าตอนยกขาลง และตอนลดขาขึ้น เครื่องนี้จึงต่อกลับด้าน
//      ค่า 1 ทำให้ LPWM เป็นตัวยกและ RPWM เป็นตัวลด ถ้าสลับสายมอเตอร์กลับเมื่อไรให้ตั้งเป็น 0
//      ตรวจโดยไม่มีขาวางอยู่ กด START แล้วขาต้องยกขึ้น
#define MOTOR_REVERSED 1
// R_EN and L_EN wired together to this one pin. Separate pins would let each
// half-bridge be enabled on its own, but nothing is lost by sharing: the
// BTS7960 blocks cross-conduction inside each half-bridge on its own, and the
// two halves are never commanded high together — actuator.cpp zeroes the idle
// side before it raises the driving one. One pin is also one write, which is
// what makes the enable a single-instruction kill on every emergency path.
// [TH] ขา R_EN และ L_EN ต่อรวมกันมาที่ขานี้ขาเดียว
//      ถ้าแยกสองขาจะเปิดใช้งานสะพานครึ่งซีกแต่ละข้างแยกกันได้
//      แต่การใช้ขาร่วมกันไม่ได้เสียอะไร เพราะ BTS7960 กันการนำกระแสทะลุภายในสะพานครึ่งซีกแต่ละข้างอยู่แล้ว
//      และสองซีกไม่เคยถูกสั่งลอจิกสูงพร้อมกัน actuator.cpp จะลดฝั่งที่ไม่ได้ใช้ให้เป็นศูนย์ก่อนเสมอ
//      ขาเดียวยังหมายถึงคำสั่งเขียนครั้งเดียว ซึ่งทำให้ขาเปิดใช้งานเป็นการตัดด้วยคำสั่งเดียวในทุกเส้นทางฉุกเฉิน
#define PIN_MOT_EN 4
// PWM for the two BTS7960 inputs, through the ESP32's LEDC peripheral.
// 20 kHz is above hearing, so a patient hears the gearbox and not the drive; it
// is also the BTS7960 datasheet's ceiling of 25 kHz with a little margin. Drop
// to 10000 if the module runs hot — it will whine, but switching losses fall.
// 10-bit is the most LEDC can hold at this frequency (80 MHz / 20 kHz = 4000
// counts, so 1024 fits and 4096 does not).
// [TH] สัญญาณ PWM ของขาอินพุตทั้งสองของ BTS7960 ผ่านวงจร LEDC ของ ESP32
//      ความถี่ 20 กิโลเฮิรตซ์อยู่เหนือช่วงที่หูได้ยิน ผู้ป่วยจึงได้ยินเสียงชุดเกียร์ ไม่ใช่เสียงวงจรขับ
//      และยังต่ำกว่าเพดาน 25 กิโลเฮิรตซ์ที่ดาตาชีตของ BTS7960 กำหนดไว้เล็กน้อย
//      ถ้าโมดูลร้อนให้ลดเป็น 10000 จะมีเสียงหวีด แต่การสูญเสียจากการสวิตช์จะลดลง
//      ความละเอียด 10 บิตคือค่ามากสุดที่ LEDC รองรับที่ความถี่นี้
//      (80 เมกะเฮิรตซ์ หาร 20 กิโลเฮิรตซ์ ได้ 4000 ระดับ ค่า 1024 จึงพอดี ส่วน 4096 ไม่พอ)
#define MOTOR_PWM_HZ 20000
#define MOTOR_PWM_BITS 10
#define MOTOR_DUTY_MAX ((1 << MOTOR_PWM_BITS) - 1)

// Full-scale ramp time. Every stroke fades in over this and every normal stop
// fades out over it, so a loaded leg is never yanked into motion or stopped
// dead — which is the whole reason for changing off the relays. It costs travel:
// roughly half a ramp at each end, about 0.4 s of a 7 s stroke, so liftSec and
// lowerSec want re-tuning on the bench after this lands.
//
// Ramping is for NORMAL starts and stops only. A fault, the energize cap, a STOP
// that reverses a lift, and boot all cut the drive dead — see actuator.cpp.
// [TH] เวลาที่ใช้ไล่กำลังขับจากศูนย์ถึงเต็มสเกล
//      ทุกจังหวะการเคลื่อนที่จะค่อยๆ เร่งขึ้นในเวลานี้ และการหยุดตามปกติทุกครั้งจะค่อยๆ ผ่อนลงในเวลาเดียวกัน
//      ขาที่มีน้ำหนักกดอยู่จึงไม่ถูกกระชากให้เคลื่อนที่และไม่ถูกหยุดกึก
//      ซึ่งเป็นเหตุผลทั้งหมดของการเปลี่ยนจากรีเลย์มาเป็นตัวขับนี้
//      สิ่งที่ต้องแลกคือระยะทาง ราวครึ่งหนึ่งของช่วงไล่กำลังที่ปลายทั้งสองด้าน
//      หรือราว 0.4 วินาทีจากจังหวะยาว 7 วินาที ค่า liftSec และ lowerSec จึงควรปรับใหม่บนโต๊ะทดสอบหลังจากนี้
//      การไล่กำลังใช้กับการเริ่มและหยุดตามปกติเท่านั้น
//      ความผิดพลาด เพดานเวลาจ่ายไฟ การกด STOP ที่กลับทิศระหว่างยก และการเปิดเครื่อง
//      ทั้งหมดนี้ตัดกำลังขับทันที ดูรายละเอียดใน actuator.cpp
#define MOTOR_RAMP_MS 400

// Jog: hold-to-move for manual positioning, entered by holding STOP while idle.
// Half duty because jog exists to nudge a leg into place and to watch what the
// mechanism does, not to cover travel — and because a slow drive is the one you
// can still react to with a hand on the leg.
//
// TUNE THIS ON THE BENCH. Half of full is a starting guess, not a measurement: a
// geared 12 V actuator under a loaded leg has stiction, and too low a duty buzzes
// without moving, which looks exactly like a seized mechanism. Raise it until the
// leg moves smoothly from rest, then stop.
// [TH] โหมดขยับเอง: กดค้างเพื่อเคลื่อนที่ ใช้จัดตำแหน่งด้วยมือ เข้าโหมดโดยกดปุ่ม STOP ค้างตอนเครื่องว่าง
//      ใช้กำลังขับครึ่งเดียว เพราะโหมดนี้มีไว้ขยับขาเข้าที่และดูว่ากลไกทำงานอย่างไร ไม่ใช่เพื่อเคลื่อนที่เป็นระยะ
//      และเพราะการขับช้าเป็นการขับที่ยังทันตั้งตัวได้ถ้ามือวางอยู่บนขา
//      ค่านี้ต้องปรับจากการทดลองจริง ครึ่งหนึ่งของกำลังเต็มเป็นค่าเดาเริ่มต้น ไม่ใช่ค่าที่วัดมา
//      ชุดขับเคลื่อน 12 โวลต์แบบมีเกียร์ที่มีขาวางอยู่มีความฝืดตอนออกตัว
//      ถ้ากำลังขับต่ำเกินไปจะได้แค่เสียงหวีดโดยไม่ขยับ ซึ่งดูเหมือนกลไกติดตายเลย
//      ให้เพิ่มขึ้นจนขาเริ่มเคลื่อนจากจุดหยุดได้นุ่มนวล แล้วหยุดที่ค่านั้น
#define MOTOR_JOG_DUTY (MOTOR_DUTY_MAX / 2)
// How long STOP must be held, while idle, to enter jog mode. Long enough that
// the ordinary STOP tap that clears the rep count can never reach it.
// [TH] ต้องกดปุ่ม STOP ค้างนานเท่าใดตอนเครื่องว่างจึงจะเข้าโหมดขยับเอง
//      นานพอที่การกด STOP สั้นๆ เพื่อล้างจำนวนครั้งจะไม่มีทางไปถึงค่านี้ได้
#define JOG_ENTER_MS 2000
// Jog mode leaves itself after this long with no button held, so a machine is
// never left sitting in a mode where START does not start a session.
// [TH] โหมดขยับเองจะออกเองเมื่อไม่มีการกดปุ่มนานเท่านี้
//      เครื่องจะไม่ถูกทิ้งไว้ในโหมดที่ปุ่ม START ไม่ได้เริ่มรอบการทำงาน
#define JOG_IDLE_EXIT_MS 20000

// The top and bottom limit switches are gone. Travel is timed now: LIFT drives
// RPWM for liftSec and LOWER drives LPWM for lowerSec, both editable.
// GPIO 32 became the yellow lamp; GPIO 33 is free.
//
// There is no firmware-controlled cut in the motor supply, and deliberately so.
// The machine's hardware cut is the panel emergency stop, which opens the whole
// 12 V feed — motor rail, lamps and this MCU together. A relay driven by this
// firmware could not survive a panic in this firmware; the button can, and it
// also covers a shorted output FET, which nothing in software can. It is manual,
// so it needs an operator present — already the rule this machine runs under.
//
// What this costs, stated once so nobody rediscovers it the hard way: the board
// has no measurement of where the leg is. A slipping belt, a jammed actuator and
// a clean stroke are the same event to this firmware — "the motor was driven for
// lowerSec". The actuator energize cap is the only backstop left, so the
// machine must not run unattended with a leg loaded on it.
// [TH] ลิมิตสวิตช์บนและล่างถูกยกเลิกแล้ว ตอนนี้การเคลื่อนที่ใช้เวลาเป็นตัวกำหนด
//      สถานะยกขาจ่ายกำลังทาง RPWM เป็นเวลา liftSec และสถานะลดลงจ่ายทาง LPWM เป็นเวลา lowerSec ปรับได้ทั้งคู่
//      ขา 32 กลายเป็นไฟเหลือง ส่วนขา 33 ว่าง
//      ไม่มีการตัดไฟเลี้ยงมอเตอร์ที่สั่งด้วยเฟิร์มแวร์ และตั้งใจให้เป็นเช่นนั้น
//      การตัดระดับฮาร์ดแวร์ของเครื่องคือปุ่มหยุดฉุกเฉินที่หน้าปัด
//      ซึ่งตัดไฟ 12 โวลต์ทั้งระบบ ทั้งไฟมอเตอร์ ไฟหน้าปัด และไมโครคอนโทรลเลอร์ตัวนี้พร้อมกัน
//      รีเลย์ที่สั่งด้วยเฟิร์มแวร์นี้จะไม่รอดถ้าเฟิร์มแวร์นี้เองพัง แต่ปุ่มกดรอด
//      และยังครอบคลุมกรณีมอสเฟตขาออกลัดวงจร ซึ่งซอฟต์แวร์ทำอะไรไม่ได้เลย
//      เป็นการตัดด้วยมือ จึงต้องมีผู้ควบคุมอยู่ด้วย ซึ่งเป็นกฎที่เครื่องนี้ใช้อยู่แล้ว
//      สิ่งที่ต้องแลก บอกไว้ครั้งเดียวเพื่อไม่ให้ต้องไปค้นพบเองตอนเกิดเรื่อง
//      บอร์ดไม่มีการวัดตำแหน่งขาเลย สายพานลื่น ชุดขับเคลื่อนติดขัด และการเคลื่อนที่ปกติ
//      เป็นเหตุการณ์เดียวกันหมดสำหรับเฟิร์มแวร์นี้ คือ "มอเตอร์ถูกขับครบ lowerSec"
//      เหลือเพียงเพดานเวลาจ่ายไฟของชุดขับเคลื่อนเป็นด่านสุดท้าย
//      จึงต้องไม่ปล่อยให้เครื่องทำงานโดยไม่มีคนดูขณะที่มีขาวางอยู่

// panel buttons (momentary, INPUT_PULLUP) — two of them, one job each
// START begins a session; STOP ends one, and when the machine is already idle it
// clears the rep count and any latched FAULT. Separate pins rather than one
// toggle, so a press means the same thing in every phase and the interrupt latch
// can go back to being wired to STOP alone.
// [TH] ปุ่มหน้าปัด (กดชั่วขณะ) ใช้ INPUT_PULLUP มีสองปุ่ม ปุ่มละหน้าที่
//      START เริ่มรอบ STOP จบรอบ และถ้าเครื่องว่างอยู่แล้วจะล้างจำนวนครั้งและสถานะ FAULT ที่ค้างอยู่
//      ใช้ขาแยกกันแทนปุ่มสลับปุ่มเดียว การกดจึงมีความหมายเดียวกันในทุกสถานะ
//      และกลไกจับค้างของอินเทอร์รัปต์กลับไปผูกกับปุ่ม STOP เพียงปุ่มเดียวได้เหมือนเดิม
#define PIN_BTN_START 25
#define PIN_BTN_STOP 26

// Per-button contact type: 1 = normally-closed, 0 = normally-open.
// Both are NO on this panel. An NO button that loses its wire is simply dead —
// it cannot raise an edge, and it cannot read as held down either. That kills the
// old fail-safe argument for NC in both directions: a broken STOP wire no longer
// stops the machine by failing, and a broken START wire no longer jams the panel.
// The stuck-line guard in buttons.cpp covers what is left, which on an NO button
// is a SHORT rather than a break.
// [TH] ชนิดหน้าสัมผัสของแต่ละปุ่ม: 1 = ปกติปิด (NC), 0 = ปกติเปิด (NO)
//      หน้าปัดชุดนี้เป็น NO ทั้งคู่ ปุ่มแบบ NO ที่สายขาดจะตายเงียบ
//      คือไม่เกิดขอบสัญญาณ และไม่อ่านได้ว่ากดค้างด้วย
//      เหตุผลเรื่องความปลอดภัยของแบบ NC จึงหมดไปทั้งสองทาง
//      สาย STOP ที่ขาดไม่ได้ทำให้เครื่องหยุดอีกต่อไป และสาย START ที่ขาดก็ไม่ได้ทำให้หน้าปัดใช้ไม่ได้
//      ส่วนที่เหลือคือกลไกตรวจสายค้างใน buttons.cpp ซึ่งบนปุ่ม NO คือสายลัดวงจร ไม่ใช่สายขาด
// Guarded so a bench build can flip one with
// --build-property compiler.cpp.extra_flags=-DBTN_START_NC=1 --clean
// [TH] ครอบด้วย #ifndef เพื่อให้สลับขั้วผ่าน build flag ตอนทดสอบได้
#ifndef BTN_START_NC
#define BTN_START_NC 0
#endif
#ifndef BTN_STOP_NC
#define BTN_STOP_NC 0
#endif
#define BUTTON_DEBOUNCE_MS 25 // เวลากันเด้งของปุ่ม (มิลลิวินาที)

// Panel lamps — 12 V, each switched by a mechanical relay off its pin.
//   green only    a session is in progress and the actuator may move — keep clear
//   yellow only   idle; nothing is going to move on its own
//   both lit      a latched FAULT — the motor is cut, but the leg may be raised
// Exactly one lamp is lit in both normal states, so both lit is unmistakably
// abnormal without anything blinking. A fault used to read plain yellow, which
// looked identical to idle; on a machine running without a CYD the panel is the
// only indicator there is, and "ready" is the one impression it must not give
// about a machine that faulted with the leg raised. What a fault IS is still
// diagnosed on the CYD, the serial log and the card row — the lamps only ever
// answer "is it about to move", plus this one exception.
//
// No blinking and no heartbeat, on either lamp. Each change of state is a contact
// operation on a mechanical relay, so a 5 Hz blink is 18,000 operations an hour
// and eats a ~100k-operation relay in a few hours of one latched fault. A fault
// costs two extra contact operations over the old rule (both lamps flip instead
// of one), not a blink, so this argument is untouched.
// [TH] ไฟหน้าปัด 12 โวลต์ แต่ละดวงสั่งผ่านรีเลย์ของตัวเอง
//      ติดเขียวดวงเดียว = อยู่ในรอบการทำงาน ชุดขับเคลื่อนอาจเคลื่อนที่ ให้อยู่ห่างไว้
//      ติดเหลืองดวงเดียว = ว่าง จะไม่มีอะไรเคลื่อนที่เอง
//      ติดทั้งสองดวง = ค้างสถานะผิดพลาด มอเตอร์ถูกตัดแล้วแต่ขาอาจยังยกอยู่
//      สถานะปกติทั้งสองจะติดดวงเดียวเสมอ การติดทั้งคู่จึงผิดปกติอย่างชัดเจนโดยไม่ต้องกะพริบ
//      เดิมความผิดพลาดแสดงเป็นสีเหลืองเฉยๆ ซึ่งดูเหมือนสถานะว่างตามปกติ
//      บนเครื่องที่ใช้งานโดยไม่มีจอ CYD หน้าปัดคือตัวบอกสถานะเพียงอย่างเดียวที่มี
//      การที่ผู้ใช้อ่านว่า "พร้อมใช้งาน" บนเครื่องที่ผิดพลาดค้างอยู่โดยขายังยกอยู่
//      คือความเข้าใจผิดเดียวที่หน้าปัดนี้ต้องไม่สร้างขึ้น
//      รายละเอียดว่าผิดพลาดอะไรยังดูได้จากจอ CYD บันทึกทางพอร์ตอนุกรม และแถวข้อมูลบนการ์ด
//      ห้ามกะพริบและห้ามวาบเป็นจังหวะทั้งสองดวง ทุกการเปลี่ยนสถานะคือการตัดต่อหน้าสัมผัสของรีเลย์กลไก
//      การกะพริบ 5 ครั้งต่อวินาทีคือ 18,000 ครั้งต่อชั่วโมง
//      กินอายุรีเลย์ที่ทนได้ราวแสนครั้งจนหมดในไม่กี่ชั่วโมงจากความผิดพลาดที่ค้างอยู่ครั้งเดียว
//      ความผิดพลาดหนึ่งครั้งเสียการตัดต่อหน้าสัมผัสเพิ่มสองครั้งจากกฎเดิม (ไฟติดทั้งสองดวงแทนดวงเดียว) ไม่ใช่การกะพริบ
//      เหตุผลเรื่องอายุรีเลย์ข้างต้นจึงยังใช้ได้เหมือนเดิม
//
// 23 was already the status lamp. 32 is the pin the TOP limit switch freed; it is
// not a strapping pin and does not glitch at boot the way 0/2/5/12/15 can.
// [TH] ขา 23 เคยเป็นไฟแสดงสถานะอยู่แล้ว ส่วนขา 32 คือขาที่ว่างจากลิมิตสวิตช์บน
//      ไม่ใช่ขา strapping และไม่มีสัญญาณแกว่งตอนบูตแบบขา 0/2/5/12/15
#define PIN_LAMP_GREEN 23
#define PIN_LAMP_YELLOW 32
// The lamps are the only relay channels left on the machine — two channels of
// one active-low board. This covers BOTH of them. Check before
// first power-up: backwards, green is lit whenever the machine is idle, which is
// the dangerous way round.
// [TH] หลอดไฟเป็นช่องรีเลย์เพียงชุดเดียวที่เหลืออยู่บนเครื่อง คือสองช่องของบอร์ดสั่งงานด้วยลอจิกต่ำแผ่นเดียว
//      ใช้กับหลอดทั้งสองดวง โดยถือว่าเป็นสองช่องของบอร์ดเดียวกัน
//      ต้องตรวจก่อนเปิดเครื่องครั้งแรก ถ้ากลับด้าน ไฟเขียวจะติดตอนเครื่องว่าง ซึ่งเป็นด้านที่อันตราย
#define LED_ACTIVE_LOW

// DS3231 real-time clock on the controller I2C bus. Not the ESP32 default pair
// (21/22): 22 drives the motor, and 25 is now START. 21 and 19 are what is left
// free after motor 22/27, UART 16/17, the START/STOP buttons on 25/26, and the
// yellow lamp on 32 — 4 is now the motor enable and 33 is free. Left as they
// are: moving a working I2C bus buys nothing and invalidates every wiring
// photo.
// [TH] นาฬิกา DS3231 ต่อผ่าน I2C ของบอร์ดคอนโทรลเลอร์
//      ใช้ขา 21/19 เพราะขา 22 เป็นขาขับมอเตอร์ และขา 25 เป็นปุ่ม START ไปแล้ว
//      ขาที่เหลือว่างหลังหักขาขับมอเตอร์ 22/27, UART 16/17, ปุ่ม START/STOP 25/26 และไฟเหลืองที่ขา 32
//      คือขา 4 ซึ่งตอนนี้เป็นขาเปิดใช้งานตัวขับมอเตอร์ ส่วนขา 33 ยังว่างอยู่
//      ไม่ย้ายบัสนี้ เพราะบัส I2C ที่ใช้งานได้อยู่แล้วย้ายไปก็ไม่ได้อะไรเพิ่ม
//      มีแต่จะทำให้รูปการต่อสายทั้งหมดใช้ไม่ได้
#define PIN_RTC_SDA 21
#define PIN_RTC_SCL 19

// Pairing code a machine answers to before anyone configures one. Matches the
// phone app's zero-setup default, so a fresh machine and a fresh app find each
// other with no typing. An empty code would pair with nothing.
// [TH] รหัสจับคู่ตั้งต้นของเครื่องก่อนตั้งค่าใดๆ ตรงกับค่าเริ่มต้นของแอปบนมือถือ
//      เครื่องใหม่กับแอปใหม่จึงจับคู่กันได้เลยโดยไม่ต้องพิมพ์ ถ้าเป็นค่าว่างจะจับคู่ไม่ได้เลย
#define DEFAULT_DEVICE_CODE "default"

// safety cap: max time a stroke (LIFT/LOWER/SAFE-LOWER) may drive before FAULT
// [TH] เพดานความปลอดภัย: เวลาสูงสุดที่มอเตอร์วิ่งได้ต่อจังหวะก่อนขึ้นสถานะ FAULT
#define DEFAULT_MAX_TRAVEL_SEC 8

// cycle defaults (seconds) — user-adjustable in the settings UI
// [TH] ค่าตั้งต้นของรอบ (วินาที) ผู้ใช้ปรับได้ในหน้าตั้งค่า
#define DEFAULT_LIFT_SEC 7  // เวลาที่ขับมอเตอร์ยกขึ้น
#define DEFAULT_LOWER_SEC 7 // เวลาที่ขับมอเตอร์ลดลง
#define DEFAULT_HOLD_SEC 10 // เวลาค้างตอนยกสุด
#define DEFAULT_REST_SEC 10 // เวลาพักตอนลงสุด

// How many reps a session runs before it stops itself. 0 = run until STOP.
// [TH] จำนวนครั้งที่รอบหนึ่งทำก่อนหยุดเอง ค่า 0 = ทำไปเรื่อยๆ จนกว่าจะกด STOP
#define DEFAULT_TARGET_REPS 10
