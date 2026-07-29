#pragma once
#include <lvgl.h>

// โหมดดีบักสำหรับสั่งงานหน้าจอผ่านสายยูเอสบี ใช้ตอนทดสอบบนโต๊ะเท่านั้น
// Bench-only debug mode: drives the UI over the USB serial line.
//
// The panel is the only way into this firmware, so every check costs a human
// tapping a touchscreen. With this on, a PC can tap, type, read a text field
// back, watch the LVGL heap and dump a file off the card — which covers the
// repetitive half of the bench matrix, the leak checks especially.
// [TH] ปกติต้องมีคนแตะจอเท่านั้นถึงจะทดสอบได้ ทุกการตรวจจึงต้องใช้คนนั่งกด
//      เปิดโหมดนี้แล้วคอมพิวเตอร์สั่งแตะ พิมพ์ อ่านค่าในช่องกรอก ดูหน่วยความจำ LVGL
//      และดึงไฟล์จากการ์ดมาดูได้ ครอบคลุมงานตรวจซ้ำๆ โดยเฉพาะการตรวจหน่วยความจำรั่ว
//
// It cannot see the screen. Everything it reports is what the firmware believes,
// not what was drawn: a field holding "เกิ่น" reads the same whether the glyphs
// rendered or came out as empty boxes. Anything about appearance still needs eyes
// on the panel.
// [TH] โหมดนี้มองไม่เห็นหน้าจอ สิ่งที่รายงานคือค่าที่เฟิร์มแวร์เก็บไว้ ไม่ใช่สิ่งที่วาดออกมา
//      ช่องที่เก็บคำว่า "เกิ่น" จะอ่านได้เหมือนกันทั้งตอนที่ตัวอักษรแสดงถูกและตอนที่ขึ้นเป็นสี่เหลี่ยมว่าง
//      เรื่องหน้าตายังต้องใช้คนดูจอจริงเสมอ
//
// Off in every commit. Flip it to 1 locally for a bench run, the same rule
// LINK_OVER_USB follows — and the two cannot both be on, because both own the
// USB serial port.
// [TH] ต้องเป็น 0 ทุกครั้งที่ commit เปิดเป็น 1 เฉพาะตอนทดสอบบนโต๊ะ กติกาเดียวกับ LINK_OVER_USB
//      และเปิดพร้อมกันทั้งสองไม่ได้ เพราะแย่งใช้พอร์ตยูเอสบีเดียวกัน
#ifndef UI_TEST_HOOK
#define UI_TEST_HOOK 1
#endif

#if UI_TEST_HOOK

void uitestBegin();  // register the synthetic pointer — ลงทะเบียนอุปกรณ์ชี้ตำแหน่งจำลอง
void uitestPoll();   // read one command line, release a finished tap — อ่านคำสั่งและปล่อยการแตะที่ครบเวลา

// Implemented in ui.cpp, which owns the screen and keyboard pointers.
// [TH] สองฟังก์ชันนี้อยู่ใน ui.cpp เพราะไฟล์นั้นเป็นเจ้าของตัวแปรหน้าจอและแป้นพิมพ์
lv_obj_t* uiTestTextarea();      // textarea of whichever keyboard is showing, or null
const char* uiTestScreenName();  // "main" / "settings" / "data" / "add" / "finish" / "confirm"

#else

static inline void uitestBegin() {}
static inline void uitestPoll() {}

#endif
