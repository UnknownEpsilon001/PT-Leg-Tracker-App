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
// R_EN and L_EN wired together to this one pin. Separate pins would let each
// half-bridge be enabled on its own, but GPIO 33 is spent on the master relay
// below and that relay is the stronger cut of the two. Nothing is lost: the
// BTS7960 blocks cross-conduction inside each half-bridge on its own, and the
// two halves are never commanded high together — actuator.cpp zeroes the idle
// side before it raises the driving one.
// [TH] ขา R_EN และ L_EN ต่อรวมกันมาที่ขานี้ขาเดียว
//      ถ้าแยกสองขาจะเปิดใช้งานสะพานครึ่งซีกแต่ละข้างแยกกันได้
//      แต่ขา 33 ถูกใช้กับรีเลย์ตัดหลักด้านล่างแล้ว และรีเลย์ตัวนั้นเป็นการตัดที่หนักแน่นกว่า
//      ไม่ได้เสียอะไรไป เพราะ BTS7960 กันการนำกระแสทะลุภายในสะพานครึ่งซีกแต่ละข้างอยู่แล้ว
//      และสองซีกไม่เคยถูกสั่งลอจิกสูงพร้อมกัน actuator.cpp จะลดฝั่งที่ไม่ได้ใช้ให้เป็นศูนย์ก่อนเสมอ
#define PIN_MOT_EN 4
// Master cut relay, in series with the BTS7960's motor supply. A relay contact
// fails open; a MOSFET fails shorted, and a shorted high-side FET drives the
// motor with the firmware unable to stop it — on a machine with a patient's leg
// loaded that is the failure mode worth spending a relay on. It is one more
// channel of the same active-low board the lamps are on.
//
// It switches on session boundaries, not on strokes: closed when a session
// starts, open when it ends or faults. Two contact operations per session rather
// than two per rep, which is ~40 a day at twenty sessions and keeps a
// ~100k-operation relay alive for years. actuator.cpp also never makes or breaks
// it under load — the drive is cut to zero first and the contacts settle before
// any PWM — so the contacts only ever switch a dead circuit.
// [TH] รีเลย์ตัดหลัก ต่ออนุกรมกับไฟเลี้ยงมอเตอร์ของ BTS7960
//      หน้าสัมผัสรีเลย์เสียแบบเปิดวงจร ส่วนมอสเฟตเสียแบบลัดวงจร
//      และมอสเฟตฝั่งบวกที่ลัดวงจรจะขับมอเตอร์โดยที่เฟิร์มแวร์หยุดไม่ได้
//      บนเครื่องที่มีขาผู้ป่วยวางอยู่ นี่คือรูปแบบความเสียหายที่คุ้มค่าที่จะเสียรีเลย์หนึ่งตัวไปกัน
//      เป็นอีกหนึ่งช่องของบอร์ดสั่งงานด้วยลอจิกต่ำแผ่นเดียวกับหลอดไฟ
//      รีเลย์นี้ตัดต่อตามขอบเขตของรอบการทำงาน ไม่ใช่ตามจังหวะการเคลื่อนที่
//      ปิดวงจรเมื่อรอบเริ่ม เปิดวงจรเมื่อรอบจบหรือเกิดความผิดพลาด
//      คือตัดต่อสองครั้งต่อรอบ ไม่ใช่สองครั้งต่อการทำหนึ่งครั้ง
//      ที่ยี่สิบรอบต่อวันคือราว 40 ครั้งต่อวัน รีเลย์ที่ทนได้ราวแสนครั้งจึงอยู่ได้หลายปี
//      และ actuator.cpp ไม่เคยตัดต่อขณะมีโหลด จะลดกำลังขับเป็นศูนย์ก่อน
//      และรอให้หน้าสัมผัสนิ่งก่อนจ่าย PWM หน้าสัมผัสจึงตัดต่อวงจรที่ไม่มีกระแสเสมอ
#define PIN_MOT_MASTER 33
#define MASTER_ACTIVE_LOW // same active-low relay board as the lamps
                          // [TH] บอร์ดรีเลย์สั่งงานด้วยลอจิกต่ำแผ่นเดียวกับหลอดไฟ

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

// The top and bottom limit switches are gone. Travel is timed now: LIFT drives
// RPWM for liftSec and LOWER drives LPWM for lowerSec, both editable.
// GPIO 32 became the yellow lamp; GPIO 33 is the master cut relay. There are no
// free GPIOs left on this board.
//
// What this costs, stated once so nobody rediscovers it the hard way: the board
// has no measurement of where the leg is. A slipping belt, a jammed actuator and
// a clean stroke are the same event to this firmware — "the motor was driven for
// lowerSec". The actuator energize cap is the only backstop left, so the
// machine must not run unattended with a leg loaded on it.
// [TH] ลิมิตสวิตช์บนและล่างถูกยกเลิกแล้ว ตอนนี้การเคลื่อนที่ใช้เวลาเป็นตัวกำหนด
//      สถานะยกขาจ่ายกำลังทาง RPWM เป็นเวลา liftSec และสถานะลดลงจ่ายทาง LPWM เป็นเวลา lowerSec ปรับได้ทั้งคู่
//      ขา 32 กลายเป็นไฟเหลือง ส่วนขา 33 เป็นรีเลย์ตัดหลัก ไม่มีขา GPIO ว่างเหลือบนบอร์ดนี้แล้ว
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
// The lamps are relay channels too, on the same active-low board as the master
// cut relay — so this is set for the same reason MASTER_ACTIVE_LOW is. Covers
// BOTH lamps, on the assumption they are two channels of one board. Check before
// first power-up: backwards, green is lit whenever the machine is idle, which is
// the dangerous way round.
// [TH] หลอดไฟก็เป็นช่องรีเลย์เหมือนกัน อยู่บนบอร์ดสั่งงานด้วยลอจิกต่ำแผ่นเดียวกับรีเลย์ตัดหลัก
//      จึงเปิดค่านี้ด้วยเหตุผลเดียวกับ MASTER_ACTIVE_LOW
//      ใช้กับหลอดทั้งสองดวง โดยถือว่าเป็นสองช่องของบอร์ดเดียวกัน
//      ต้องตรวจก่อนเปิดเครื่องครั้งแรก ถ้ากลับด้าน ไฟเขียวจะติดตอนเครื่องว่าง ซึ่งเป็นด้านที่อันตราย
#define LED_ACTIVE_LOW

// DS3231 real-time clock on the controller I2C bus. Not the ESP32 default pair
// (21/22): 22 drives the motor, and 25 is now START. 21 and 19 are what is left
// free after motor 22/27, UART 16/17, the START/STOP buttons on 25/26, and the
// yellow lamp on 32 — 4 and 33 have since gone to the motor enable and the
// master cut relay. Left as they are: moving a working I2C bus buys nothing and
// invalidates every wiring photo.
// [TH] นาฬิกา DS3231 ต่อผ่าน I2C ของบอร์ดคอนโทรลเลอร์
//      ใช้ขา 21/19 เพราะขา 22 เป็นขาขับมอเตอร์ และขา 25 เป็นปุ่ม START ไปแล้ว
//      ขาที่เหลือว่างหลังหักขาขับมอเตอร์ 22/27, UART 16/17, ปุ่ม START/STOP 25/26 และไฟเหลืองที่ขา 32
//      คือขา 4 และ 33 ซึ่งตอนนี้ไปเป็นขาเปิดใช้งานตัวขับมอเตอร์กับรีเลย์ตัดหลักแล้ว
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
