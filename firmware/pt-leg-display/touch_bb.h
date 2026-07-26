#pragma once
#include <stdint.h>

// อ่านทัชสกรีน XPT2046 ด้วยการกระตุกขาเอง (bit-bang) ไม่ใช้ SPI ฮาร์ดแวร์
// เพราะ ESP32 มีบัส SPI ฮาร์ดแวร์แค่สองชุด: จอ TFT ใช้ HSPI และการ์ด SD ต้องใช้ VSPI
// Software-SPI read of the XPT2046 touch controller.
//
// Why not hardware SPI: the ESP32 has two usable SPI peripherals. TFT_eSPI owns
// HSPI (12/13/14/15) and the CYD's microSD socket is hard-wired to VSPI's pins
// (5/18/19/23), so once the card is in use there is no peripheral left for
// touch. Bit-banging four GPIOs costs nothing here — the panel is polled at LVGL
// rate, not in a hot loop.
//
// Why not a library: the usual XPT2046 bitbang driver is GitHub-only and absent
// from the Arduino library index, so it could not be version-pinned alongside
// the others in firmware/README.md. The protocol is three bytes per axis.
// [TH] ไม่ใช้ไลบรารีสำเร็จรูปเพราะตัวที่นิยมมีแต่บน GitHub ล็อกเวอร์ชันไม่ได้
//      และโปรโตคอลนี้สั้นมาก (ส่งคำสั่งหนึ่งไบต์ อ่านผลสองไบต์ต่อหนึ่งแกน)

void touchBBBegin();

// Raw 12-bit ADC counts, not screen pixels — the caller maps them.
// Returns false when the panel is not being pressed.
// [TH] คืนค่าดิบ 12 บิตจาก ADC ไม่ใช่พิกัดจอ ผู้เรียกเป็นคนแปลงเอง
//      คืน false เมื่อไม่มีการสัมผัส
bool touchBBRead(uint16_t& x, uint16_t& y);
