#pragma once

// ค่าคงที่การตั้งค่าฮาร์ดแวร์ของบอร์ดควบคุม (ESP32 ฝั่งคอนโทรลเลอร์)
// รวมหมายเลขขา GPIO ของรีเลย์และลิมิตสวิตช์ กับค่าตั้งต้นของรอบการทำงาน
// Hardware pin map + default cycle timings for the controller MCU.

#define FW_VERSION "0.1.0"

#define PIN_RELAY_UP 22   // P3 connector — ขารีเลย์สั่งมอเตอร์ "ยกขึ้น"
#define PIN_RELAY_DOWN 27 // CN1 connector — ขารีเลย์สั่งมอเตอร์ "ลดลง"
// #define RELAY_ACTIVE_LOW  // uncomment if relay board is active-low
// [TH] เปิดบรรทัดนี้ถ้าบอร์ดรีเลย์เป็นแบบ active-low (สั่งงานด้วยลอจิก LOW)

// limit switches (controller GPIOs that support INPUT_PULLUP — NOT 34/35/36/39)
// [TH] ลิมิตสวิตช์ ต้องใช้ขาที่รองรับ INPUT_PULLUP เท่านั้น (ห้ามใช้ 34/35/36/39)
#define PIN_SWITCH_TOP 32     // closes when leg fully up — สวิตช์บน: ปิดเมื่อขายกสุด
#define PIN_SWITCH_BOTTOM 33  // closes when leg fully down — สวิตช์ล่าง: ปิดเมื่อขาลงสุด
#define SWITCH_NC             // limit switches are normally-closed (fail-safe on wire break)
// [TH] กำหนดถ้าใช้สวิตช์แบบ NC (ปกติปิด) เพื่อให้ปลอดภัยเมื่อสายขาด
#define SWITCH_DEBOUNCE_MS 25 // เวลากันเด้งสัญญาณสวิตช์ (มิลลิวินาที)

// panel buttons (momentary, INPUT_PULLUP)
// [TH] ปุ่มหน้าปัด (กดชั่วขณะ) ใช้ INPUT_PULLUP
#define PIN_BTN_START 25   // start a session — เริ่มรอบ (เฉพาะตอนว่าง)
#define PIN_BTN_STOP 26    // stop / safe-lower — หยุดและลดขาลงอย่างปลอดภัย (ตลอดเวลา)
#define PIN_BTN_RESET 4    // clear reps + FAULT when idle — ล้างจำนวนครั้งและสถานะ FAULT ตอนว่าง

// Per-button contact type: 1 = normally-closed, 0 = normally-open. Separate from
// SWITCH_NC and from each other, because panel buttons are often sourced as
// mixed parts and a wrong guess reads as a button held down forever.
// NC is the fail-safe choice for STOP: a cut wire reads as pressed, which stops
// the machine. For START it is the opposite trade - a cut or unplugged START
// wire reads as a permanent press - so change these to match the real parts.
// [TH] ชนิดหน้าสัมผัสของแต่ละปุ่ม: 1 = ปกติปิด (NC), 0 = ปกติเปิด (NO) แยกจาก SWITCH_NC และแยกจากกัน
//      เพราะปุ่มมักซื้อคนละล็อตและคนละแบบ ถ้าตั้งผิดจะอ่านได้เหมือนกดค้างตลอดเวลา
//      NC ปลอดภัยสำหรับ STOP (สายขาด = เหมือนกด = เครื่องหยุด) แต่สำหรับ START กลับตรงข้าม
// Guarded so a bench build can flip one with
// --build-property compiler.cpp.extra_flags=-DBTN_START_NC=0 --clean
// while the committed value keeps describing the real panel.
// [TH] ครอบด้วย #ifndef เพื่อให้ทดสอบสลับขั้วผ่าน build flag ได้ โดยค่าที่ commit ยังตรงกับแผงปุ่มจริง
#ifndef BTN_START_NC
#define BTN_START_NC 1
#endif
#ifndef BTN_STOP_NC
#define BTN_STOP_NC 1
#endif
#ifndef BTN_RESET_NC
#define BTN_RESET_NC 1
#endif
#define BUTTON_DEBOUNCE_MS 25 // เวลากันเด้งของปุ่ม (มิลลิวินาที)

// DS3231 real-time clock on the controller I2C bus. Not the ESP32 default pair
// (21/22): 22 drives a relay, and 25 — the pin the plan first picked for SCL —
// is now START. 21 and 19 are what is left free after relays 22/27, UART 16/17,
// limits 32/33 and buttons 25/26/4.
// [TH] นาฬิกา DS3231 ต่อผ่าน I2C ของบอร์ดคอนโทรลเลอร์
//      ใช้ขา 21/19 เพราะขา 22 เป็นรีเลย์ และขา 25 ถูกใช้เป็นปุ่ม START ไปแล้ว
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

// cycle defaults (seconds) — user-adjustable in settings UI
// [TH] ค่าตั้งต้นของรอบ (วินาที) ผู้ใช้ปรับได้ในหน้าตั้งค่า
#define DEFAULT_HOLD_SEC 10 // เวลาค้างตอนยกสุด
#define DEFAULT_REST_SEC 10 // เวลาพักตอนลงสุด
