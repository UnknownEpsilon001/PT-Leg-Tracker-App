#pragma once

// ค่าคงที่การตั้งค่าฮาร์ดแวร์ของบอร์ดควบคุม (ESP32 ฝั่งคอนโทรลเลอร์)
// รวมหมายเลขขา GPIO ของรีเลย์และลิมิตสวิตช์ กับค่าตั้งต้นของรอบการทำงาน
// Hardware pin map + default cycle timings for the controller MCU.

#define FW_VERSION "0.1.0"

#define PIN_RELAY_UP 22   // P3 connector — ขารีเลย์สั่งมอเตอร์ "ยกขึ้น"
#define PIN_RELAY_DOWN 27 // CN1 connector — ขารีเลย์สั่งมอเตอร์ "ลดลง"
// #define RELAY_ACTIVE_LOW  // uncomment if relay board is active-low
// [TH] เปิดบรรทัดนี้ถ้าบอร์ดรีเลย์เป็นแบบ active-low (สั่งงานด้วยลอจิก LOW)

// The top and bottom limit switches are gone. Travel is timed now: LIFT drives
// the UP relay for liftSec and LOWER drives DOWN for lowerSec, both editable.
// GPIO 32 became the yellow lamp; GPIO 33 is free.
//
// What this costs, stated once so nobody rediscovers it the hard way: the board
// has no measurement of where the leg is. A slipping belt, a jammed actuator and
// a clean stroke are the same event to this firmware — "the relay was energized
// for lowerSec". The actuator energize cap is the only backstop left, so the
// machine must not run unattended with a leg loaded on it.
// [TH] ลิมิตสวิตช์บนและล่างถูกยกเลิกแล้ว ตอนนี้การเคลื่อนที่ใช้เวลาเป็นตัวกำหนด
//      สถานะยกขาจ่ายไฟรีเลย์ขึ้นเป็นเวลา liftSec และสถานะลดลงจ่ายไฟรีเลย์ลงเป็นเวลา lowerSec ปรับได้ทั้งคู่
//      ขา 32 กลายเป็นไฟเหลือง ส่วนขา 33 ว่าง
//      สิ่งที่ต้องแลก บอกไว้ครั้งเดียวเพื่อไม่ให้ต้องไปค้นพบเองตอนเกิดเรื่อง
//      บอร์ดไม่มีการวัดตำแหน่งขาเลย สายพานลื่น ชุดขับเคลื่อนติดขัด และการเคลื่อนที่ปกติ
//      เป็นเหตุการณ์เดียวกันหมดสำหรับเฟิร์มแวร์นี้ คือ "รีเลย์จ่ายไฟครบ lowerSec"
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

// Panel lamps — 12 V, each switched by a mechanical relay off its pin. Exactly
// one is lit at any moment:
//   green   a session is in progress and the actuator may move — keep clear
//   yellow  idle, or a latched FAULT; nothing is going to move on its own
// A FAULT reads yellow because it is not running and the relays really are off.
// The lamp answers "is it about to move", nothing else; a fault is diagnosed on
// the CYD, the serial log and the card row.
//
// No blinking and no heartbeat, on either lamp. Each change of state is a contact
// operation on a mechanical relay, so a 5 Hz blink is 18,000 operations an hour
// and eats a ~100k-operation relay in a few hours of one latched fault. Solid
// states cost four operations per session (two lamps, on and off).
// [TH] ไฟหน้าปัด 12 โวลต์ แต่ละดวงสั่งผ่านรีเลย์ของตัวเอง ติดครั้งละดวงเดียวเสมอ
//      เขียว = อยู่ในรอบการทำงาน ชุดขับเคลื่อนอาจเคลื่อนที่ ให้อยู่ห่างไว้
//      เหลือง = ว่าง หรือค้างสถานะผิดพลาด จะไม่มีอะไรเคลื่อนที่เอง
//      สถานะผิดพลาดแสดงเป็นสีเหลือง เพราะไม่ได้กำลังทำงานและรีเลย์ดับจริงๆ
//      หลอดตอบแค่คำถามเดียวว่า "กำลังจะเคลื่อนที่ไหม" การวินิจฉัยให้ดูที่จอ พอร์ตอนุกรม และแถวข้อมูลบนการ์ด
//      ห้ามกะพริบและห้ามวาบเป็นจังหวะทั้งสองดวง ทุกการเปลี่ยนสถานะคือการตัดต่อหน้าสัมผัสของรีเลย์กลไก
//      การกะพริบ 5 ครั้งต่อวินาทีคือ 18,000 ครั้งต่อชั่วโมง
//      กินอายุรีเลย์ที่ทนได้ราวแสนครั้งจนหมดในไม่กี่ชั่วโมงจากความผิดพลาดที่ค้างอยู่ครั้งเดียว
//      การใช้สถานะติดค้างหรือดับสนิทเสียแค่สี่ครั้งต่อรอบ (สองดวง ติดและดับ)
//
// 23 was already the status lamp. 32 is the pin the TOP limit switch freed; it is
// not a strapping pin and does not glitch at boot the way 0/2/5/12/15 can.
// [TH] ขา 23 เคยเป็นไฟแสดงสถานะอยู่แล้ว ส่วนขา 32 คือขาที่ว่างจากลิมิตสวิตช์บน
//      ไม่ใช่ขา strapping และไม่มีสัญญาณแกว่งตอนบูตแบบขา 0/2/5/12/15
#define PIN_LAMP_GREEN 23
#define PIN_LAMP_YELLOW 32
// Uncomment for an active-low relay board — most cheap opto-isolated modules are.
// Covers BOTH lamps, on the assumption they are two channels of one board. Check
// before first power-up: backwards, green is lit whenever the machine is idle,
// which is the dangerous way round.
// [TH] เปิดบรรทัดนี้ถ้าบอร์ดรีเลย์เป็นแบบสั่งงานด้วยลอจิกต่ำ ซึ่งโมดูลออปโตราคาถูกส่วนใหญ่เป็นแบบนั้น
//      ใช้กับหลอดทั้งสองดวง โดยถือว่าเป็นสองช่องของบอร์ดเดียวกัน
//      ต้องตรวจก่อนเปิดเครื่องครั้งแรก ถ้ากลับด้าน ไฟเขียวจะติดตอนเครื่องว่าง ซึ่งเป็นด้านที่อันตราย
// #define LED_ACTIVE_LOW

// DS3231 real-time clock on the controller I2C bus. Not the ESP32 default pair
// (21/22): 22 drives a relay, and 25 is now START. 21 and 19 are what is left
// free after relays 22/27, UART 16/17, the START/STOP buttons on 25/26, and the
// yellow lamp on 32 — 4 and 33 are free. Left as they are: moving a working I2C
// bus buys nothing and invalidates every wiring photo.
// [TH] นาฬิกา DS3231 ต่อผ่าน I2C ของบอร์ดคอนโทรลเลอร์
//      ใช้ขา 21/19 เพราะขา 22 เป็นรีเลย์ และขา 25 เป็นปุ่ม START ไปแล้ว
//      ขาที่เหลือว่างหลังหักรีเลย์ 22/27, UART 16/17, ปุ่ม START/STOP 25/26 และไฟเหลืองที่ขา 32
//      คือขา 4 และ 33 ยังว่างอยู่ ไม่ย้ายบัสนี้ เพราะบัส I2C ที่ใช้งานได้อยู่แล้วย้ายไปก็ไม่ได้อะไรเพิ่ม
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
#define DEFAULT_LIFT_SEC 4  // เวลาที่รีเลย์ยกขึ้นจ่ายไฟ
#define DEFAULT_LOWER_SEC 4 // เวลาที่รีเลย์ลดลงจ่ายไฟ
#define DEFAULT_HOLD_SEC 10 // เวลาค้างตอนยกสุด
#define DEFAULT_REST_SEC 10 // เวลาพักตอนลงสุด

// How many reps a session runs before it stops itself. 0 = run until STOP.
// [TH] จำนวนครั้งที่รอบหนึ่งทำก่อนหยุดเอง ค่า 0 = ทำไปเรื่อยๆ จนกว่าจะกด STOP
#define DEFAULT_TARGET_REPS 10
