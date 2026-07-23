#pragma once
#include <stdint.h>

// อ่านลิมิตสวิตช์บน/ล่างแบบกันเด้ง (debounce) เพื่อบอกว่าขายก/ลงสุดแล้วหรือยัง
// Debounced reads of the top/bottom limit switches.
// Reads PIN_SWITCH_TOP / PIN_SWITCH_BOTTOM (INPUT_PULLUP), debounced.
// SWITCH_NC flips read polarity: NC pressed == open == HIGH; NO pressed == LOW.
// [TH] SWITCH_NC สลับขั้วการอ่าน: NC กดแล้ว=วงจรเปิด=HIGH ; NO กดแล้ว=LOW
void switchesBegin();
void switchesPoll(uint32_t nowMs);  // call every loop before SessionSM::tick
                                    // [TH] เรียกทุกลูปก่อน SessionSM::tick
bool switchTop();                   // debounced logical "pressed" — สวิตช์บนถูกกด (ผ่านกันเด้ง)
bool switchBottom();                // สวิตช์ล่างถูกกด (ผ่านกันเด้ง)
