#pragma once
#include <stdint.h>

// อ่านปุ่มหน้าปัดสามปุ่ม (START/STOP/RESET) แบบกันเด้งและจับ "ขอบขากด" ครั้งเดียว
// Debounced, edge-triggered reads of the three panel buttons.
// Unlike switches.* (which report a debounced level for the safety limit
// switches), each accessor here returns true for exactly ONE loop on the
// press edge, so a held button fires once. Contact type is per button:
// BTN_START_NC / BTN_STOP_NC / BTN_RESET_NC in config.h, independent of
// SWITCH_NC, because a panel can mix NC and NO parts.
// [TH] ชนิดหน้าสัมผัสแยกรายปุ่มที่ config.h ไม่ผูกกับ SWITCH_NC เพราะปุ่มอาจเป็นคนละแบบ
// [TH] ต่างจาก switches.* ที่รายงานระดับสัญญาณของลิมิตสวิตช์เพื่อความปลอดภัย
//      ที่นี่คืนค่า true เพียงลูปเดียวตอนกด เพื่อให้กดค้างก็ทำงานครั้งเดียว
void buttonsBegin();
void buttonsPoll(uint32_t nowMs);  // call every loop, before SessionSM::tick
                                   // [TH] เรียกทุกลูปก่อน SessionSM::tick
bool buttonStartPressed();         // one-shot press edge — ขอบกดครั้งเดียว
bool buttonStopPressed();
bool buttonResetPressed();
