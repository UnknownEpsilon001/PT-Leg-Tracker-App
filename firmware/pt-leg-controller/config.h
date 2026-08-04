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

// panel buttons (momentary, INPUT_PULLUP) — two of them
// RUN is a toggle: it starts a session when the machine is idle with the leg
// down, and stops (by lowering) in every other state. One button rather than a
// separate START and STOP, because the panel only has room for two.
// [TH] ปุ่มหน้าปัด (กดชั่วขณะ) ใช้ INPUT_PULLUP มีสองปุ่ม
//      ปุ่ม RUN เป็นแบบสลับ: กดตอนว่างและขาอยู่ล่างสุด = เริ่มรอบ กดตอนอื่นทั้งหมด = หยุดโดยลดขาลง
//      ใช้ปุ่มเดียวแทนที่จะแยก START กับ STOP เพราะหน้าปัดมีที่ให้แค่สองปุ่ม
#define PIN_BTN_RUN 25     // start / stop toggle — ปุ่มสลับ เริ่ม/หยุด
#define PIN_BTN_RESET 4    // clear reps + FAULT when idle — ล้างจำนวนครั้งและสถานะ FAULT ตอนว่าง
// GPIO 26 was the old dedicated STOP pin and is now free.
// [TH] ขา 26 เคยเป็นปุ่ม STOP แยก ตอนนี้ว่างแล้ว

// Per-button contact type: 1 = normally-closed, 0 = normally-open. Separate from
// SWITCH_NC and from each other, because panel buttons are often sourced as
// mixed parts and a wrong guess reads as a button held down forever.
// NC used to be the clear fail-safe choice, back when STOP was its own button: a
// cut wire read as pressed and stopped the machine. RUN is both start and stop,
// so neither polarity is purely fail-safe for it. NC means a wire that breaks
// mid-session raises one press, which stops - good - and then reads pressed
// forever, which kills the button for the rest of the power cycle; a wire that
// breaks while idle raises one press too, which the state machine turns into a
// lower or a no-op, never a start (see SessionSM::toggle). NO means a broken RUN
// wire is simply dead, with no stop from the panel at all. NC is kept for that
// reason, but set these to match the real parts.
// [TH] ชนิดหน้าสัมผัสของแต่ละปุ่ม: 1 = ปกติปิด (NC), 0 = ปกติเปิด (NO) แยกจาก SWITCH_NC และแยกจากกัน
//      เพราะปุ่มมักซื้อคนละล็อตและคนละแบบ ถ้าตั้งผิดจะอ่านได้เหมือนกดค้างตลอดเวลา
//      สมัยที่ STOP เป็นปุ่มแยก แบบ NC ปลอดภัยชัดเจน เพราะสายขาด = เหมือนกด = เครื่องหยุด
//      แต่ปุ่ม RUN ทำทั้งเริ่มและหยุด จึงไม่มีขั้วไหนปลอดภัยล้วนๆ
//      แบบ NC: สายขาดกลางรอบจะเกิดการกดหนึ่งครั้งซึ่งสั่งหยุด (ดี) แล้วอ่านว่ากดค้างตลอดจนกว่าจะปิดเครื่อง
//      ส่วนสายที่ขาดตอนว่างก็เกิดการกดหนึ่งครั้งเช่นกัน ซึ่งเครื่องสถานะจะแปลเป็นการลดขาหรือไม่ทำอะไร
//      ไม่มีทางกลายเป็นการเริ่มรอบ (ดู SessionSM::toggle)
//      แบบ NO: สาย RUN ที่ขาดจะตายเงียบ ไม่มีปุ่มหยุดที่หน้าปัดเลย จึงยังเลือก NC ไว้
//      แต่ต้องตั้งให้ตรงกับปุ่มจริง
// Guarded so a bench build can flip one with
// --build-property compiler.cpp.extra_flags=-DBTN_RUN_NC=0 --clean
// while the committed value keeps describing the real panel.
// [TH] ครอบด้วย #ifndef เพื่อให้ทดสอบสลับขั้วผ่าน build flag ได้ โดยค่าที่ commit ยังตรงกับแผงปุ่มจริง
#ifndef BTN_RUN_NC
#define BTN_RUN_NC 1
#endif
#ifndef BTN_RESET_NC
#define BTN_RESET_NC 1
#endif
#define BUTTON_DEBOUNCE_MS 25 // เวลากันเด้งของปุ่ม (มิลลิวินาที)

// Panel status LED — 12 V, switched by a mechanical relay off this pin.
// Lit while a session is in progress, dark otherwise. One bit, no blinking: a
// relay charges a contact operation and an audible click for every change, so
// patterns that toggle on their own would wear it out. See status_led.h.
//
// 23 is free, is not a strapping pin, and does not glitch at boot the way
// 0/2/5/12/15 can — 12 in particular is read at reset to pick the flash voltage,
// and an LED pulling it up stops the board booting until the wire comes off.
//
// GPIO 26, which the retired STOP button freed, would work equally well and is
// not reserved for anything. The only reason not to use it is if a STOP button
// is still physically wired to that header on an older machine: an output tied
// to a switch that shorts to GND is a dead short on every press. On a panel
// built to this two-button design there is no such wire.
// [TH] ขา 23 ว่าง ไม่ใช่ขา strapping และไม่มีสัญญาณแกว่งตอนบูตแบบขา 0/2/5/12/15
//      โดยเฉพาะขา 12 ที่ถูกอ่านตอนรีเซ็ตเพื่อเลือกแรงดันแฟลช ถ้ามี LED ดึงขึ้นจะบูตไม่ขึ้นจนกว่าจะถอดสาย
//      ส่วนขา 26 ที่ว่างลงจากการเลิกใช้ปุ่ม STOP ก็ใช้ได้ดีเท่ากันและไม่ได้กันไว้ให้อะไร
//      เหตุผลเดียวที่ไม่ควรใช้คือถ้าเครื่องรุ่นเก่ายังมีปุ่ม STOP ต่อสายอยู่ที่หัวต่อนั้นจริงๆ
//      เพราะขาออกที่ไปต่อกับสวิตช์ซึ่งลัดลง GND จะกลายเป็นการลัดวงจรทุกครั้งที่กด
//      หน้าปัดที่สร้างตามแบบสองปุ่มนี้ไม่มีสายเส้นนั้น
#define PIN_STATUS_LED 23
// Uncomment for an active-low relay board — most cheap opto-isolated modules are,
// and they energize on LOW. Check yours before first power-up: get this backwards
// and the lamp is lit whenever the machine is idle and dark while it is working,
// which is the dangerous way round. The motor relays are active-high (see
// RELAY_ACTIVE_LOW above), but the lamp relay is a separate part and need not match.
// [TH] เปิดบรรทัดนี้ถ้าบอร์ดรีเลย์เป็นแบบสั่งงานด้วยลอจิกต่ำ
//      โมดูลออปโตราคาถูกส่วนใหญ่เป็นแบบนี้ คือทำงานเมื่อได้รับ LOW
//      ต้องตรวจก่อนเปิดเครื่องครั้งแรก ถ้าตั้งกลับด้าน หลอดจะติดตอนเครื่องว่างและดับตอนเครื่องทำงาน
//      ซึ่งเป็นด้านที่อันตราย
//      รีเลย์มอเตอร์เป็นแบบสั่งงานด้วยลอจิกสูง (ดู RELAY_ACTIVE_LOW ด้านบน)
//      แต่รีเลย์ของหลอดเป็นคนละตัว ไม่จำเป็นต้องเหมือนกัน
// #define LED_ACTIVE_LOW

// DS3231 real-time clock on the controller I2C bus. Not the ESP32 default pair
// (21/22): 22 drives a relay, and 25 — the pin the plan first picked for SCL —
// is now RUN. 21 and 19 are what is left free after relays 22/27, UART 16/17,
// limits 32/33 and buttons 25/4. Left as they are now that 26 has freed up:
// moving a working I2C bus buys nothing and invalidates every wiring photo.
// [TH] นาฬิกา DS3231 ต่อผ่าน I2C ของบอร์ดคอนโทรลเลอร์
//      ใช้ขา 21/19 เพราะขา 22 เป็นรีเลย์ และขา 25 ถูกใช้เป็นปุ่ม RUN ไปแล้ว
//      ถึงตอนนี้ขา 26 จะว่างแล้วก็ไม่ย้าย เพราะบัส I2C ที่ใช้งานได้อยู่แล้วย้ายไปก็ไม่ได้อะไรเพิ่ม
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

// cycle defaults (seconds) — user-adjustable in settings UI
// [TH] ค่าตั้งต้นของรอบ (วินาที) ผู้ใช้ปรับได้ในหน้าตั้งค่า
#define DEFAULT_HOLD_SEC 10 // เวลาค้างตอนยกสุด
#define DEFAULT_REST_SEC 10 // เวลาพักตอนลงสุด
