#pragma once
#include <stdint.h>

// อ่านปุ่มหน้าปัดสองปุ่ม (START/STOP) แบบกันเด้งและจับ "ขอบขากด" ครั้งเดียว
// Debounced, edge-triggered reads of the two panel buttons. Each accessor returns
// true for exactly ONE loop on the press edge, so a held button fires once.
// Contact type is per button: BTN_START_NC / BTN_STOP_NC in config.h, because a
// panel can mix NO and NC parts even when both are specified NO.
// [TH] คืนค่า true เพียงลูปเดียวตอนกด เพื่อให้กดค้างก็ทำงานครั้งเดียว
//      ชนิดหน้าสัมผัสแยกรายปุ่มที่ config.h เพราะหน้าปัดอาจมีปุ่มคนละแบบปนกัน
void buttonsBegin();
void buttonsPoll(uint32_t nowMs);  // call every loop, before SessionSM::tick
                                   // [TH] เรียกทุกลูปก่อน SessionSM::tick

// One-shot press edges.
// STOP alone carries the interrupt latch: a press lost inside a loop iteration
// that blocked on the network must still be able to stop the machine, and a
// latched press can inflate an electrical transient into a full-width press. That
// trade is worth taking for a stop and never for a start, and with separate pins
// it is settled by the wiring rather than by the phase.
// [TH] ขอบกดครั้งเดียว
//      มีเพียงปุ่ม STOP ที่ใช้กลไกจับค้างของอินเทอร์รัปต์
//      การกดที่หายไปในลูปรอบที่ค้างรอเครือข่ายต้องยังสั่งหยุดเครื่องได้
//      และการจับค้างอาจทำให้สัญญาณรบกวนกลายเป็นการกดเต็มเกณฑ์
//      ข้อแลกเปลี่ยนนี้คุ้มสำหรับการหยุดแต่ไม่คุ้มสำหรับการเริ่ม
//      พอแยกขากันแล้วเรื่องนี้จบด้วยการต่อสาย ไม่ต้องดูสถานะ
bool buttonStartPressed();
bool buttonStopPressed();

// True while the START line reads pressed with no plausible person pressing it:
// since power-up, or held longer than anyone holds a button. On an NO button that
// is a SHORT — a pinched loom, a jammed button, a screwdriver left across the
// terminals — and it would restart the machine the moment each session ended. No
// session starts while it lasts, including one commanded from the server. Clears
// as soon as the line moves, which a real held button does on release.
// [TH] เป็นจริงเมื่อสาย START อ่านได้ว่ากดโดยไม่น่าจะมีคนกดจริง
//      คืออ่านได้แบบนั้นตั้งแต่เปิดเครื่อง หรือค้างนานเกินกว่าที่คนจะกดค้าง
//      สำหรับปุ่มแบบ NO นั่นคือสายลัดวงจร เช่นสายถูกหนีบ ปุ่มค้าง หรือไขควงพาดขั้วอยู่
//      และจะทำให้เครื่องเริ่มรอบใหม่ทันทีทุกครั้งที่รอบก่อนหน้าจบ
//      จึงไม่ให้เริ่มรอบใดๆ ตราบที่ยังเป็นแบบนั้น รวมถึงรอบที่สั่งจากเซิร์ฟเวอร์
//      ค่านี้ล้างทันทีที่ระดับขาเปลี่ยน ซึ่งปุ่มที่ถูกกดค้างจริงจะเปลี่ยนเมื่อปล่อย
bool buttonStartStuck();
