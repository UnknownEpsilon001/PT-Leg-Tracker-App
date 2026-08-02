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
// True while the STOP line reads pressed with no plausible person pressing it:
// either it has read that way since power-up, or it has held it for longer than
// anyone holds a button. On an NC button that state is an open circuit — a broken
// wire or an unseated connector — and STOP cannot raise an edge while it lasts,
// so no session should be started. Clears as soon as the line moves, which a held
// button does when it is released and a cut wire never does.
// [TH] เป็นจริงเมื่อสาย STOP อ่านได้ว่ากดอยู่โดยไม่น่าจะมีคนกดจริง
//      คืออ่านได้แบบนั้นมาตั้งแต่เปิดเครื่อง หรือค้างอยู่นานเกินกว่าที่คนจะกดค้างไว้
//      สำหรับปุ่มแบบปกติปิด สถานะนั้นคือวงจรเปิด ซึ่งมักเป็นสายขาดหรือขั้วต่อหลวม
//      และปุ่ม STOP จะไม่เกิดขอบสัญญาณตราบที่ยังเป็นแบบนั้น จึงไม่ควรให้เริ่มรอบใหม่
//      ค่านี้จะถูกล้างทันทีที่ระดับขาเปลี่ยน ซึ่งปุ่มที่ถูกกดค้างจะเปลี่ยนเมื่อปล่อย แต่สายที่ขาดจะไม่มีวันเปลี่ยน
bool buttonStopWireSuspect();
