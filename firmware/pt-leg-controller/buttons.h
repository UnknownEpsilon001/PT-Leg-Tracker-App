#pragma once
#include <stdint.h>

// อ่านปุ่มหน้าปัดสองปุ่ม (RUN/RESET) แบบกันเด้งและจับ "ขอบขากด" ครั้งเดียว
// Debounced, edge-triggered reads of the two panel buttons.
// Unlike switches.* (which report a debounced level for the safety limit
// switches), each accessor here returns true for exactly ONE loop on the
// press edge, so a held button fires once. Contact type is per button:
// BTN_RUN_NC / BTN_RESET_NC in config.h, independent of SWITCH_NC, because a
// panel can mix NC and NO parts.
// RUN is a toggle — this module only reports that it was pressed; what a press
// means in the current phase is SessionSM::toggle's decision.
// [TH] ชนิดหน้าสัมผัสแยกรายปุ่มที่ config.h ไม่ผูกกับ SWITCH_NC เพราะปุ่มอาจเป็นคนละแบบ
// [TH] ต่างจาก switches.* ที่รายงานระดับสัญญาณของลิมิตสวิตช์เพื่อความปลอดภัย
//      ที่นี่คืนค่า true เพียงลูปเดียวตอนกด เพื่อให้กดค้างก็ทำงานครั้งเดียว
//      ปุ่ม RUN เป็นแบบสลับ โมดูลนี้รายงานแค่ว่ามีการกด ส่วนความหมายของการกดในแต่ละสถานะ
//      เป็นหน้าที่ของ SessionSM::toggle ตัดสิน
void buttonsBegin();
void buttonsPoll(uint32_t nowMs);  // call every loop, before SessionSM::tick
                                   // [TH] เรียกทุกลูปก่อน SessionSM::tick
// One-shot press edge. `fromIsr`, when given, reports whether THIS edge came from
// the interrupt latch rather than from ordinary polling — false when the call
// returns false. The caller must refuse a latched press for anything that would
// start the machine: the latch exists to recover a press the polling missed inside
// a blocked loop iteration, which is worth having for a stop, but it can also
// inflate an electrical transient into a full-width press (see the long note in
// buttons.cpp). With START and STOP on separate pins that was settled by wiring
// the latch to STOP only; one RUN button means the same press can mean either
// thing, so the decision moves to the caller, which knows the phase.
// [TH] ขอบกดครั้งเดียว ถ้าส่ง fromIsr มาด้วยจะบอกว่าขอบครั้งนี้มาจากกลไกจับค้างของอินเทอร์รัปต์
//      หรือมาจากการอ่านตามรอบปกติ ถ้าฟังก์ชันคืนค่าเท็จ ค่านี้จะเป็นเท็จเสมอ
//      ผู้เรียกต้องปฏิเสธการกดที่มาจากกลไกจับค้างสำหรับทุกอย่างที่จะทำให้เครื่องเริ่มทำงาน
//      กลไกจับค้างมีไว้กู้การกดที่การอ่านตามรอบพลาดไปในลูปรอบที่ถูกบล็อก ซึ่งคุ้มที่จะมีสำหรับการสั่งหยุด
//      แต่มันก็ทำให้สัญญาณรบกวนทางไฟฟ้ากลายเป็นการกดเต็มเกณฑ์ได้เช่นกัน (ดูคำอธิบายยาวใน buttons.cpp)
//      สมัยที่ START กับ STOP อยู่คนละขา เรื่องนี้จบด้วยการต่อกลไกนี้ไว้กับปุ่ม STOP เท่านั้น
//      พอเหลือปุ่ม RUN ปุ่มเดียว การกดเดียวกันหมายได้ทั้งสองอย่าง
//      การตัดสินจึงย้ายไปอยู่ที่ผู้เรียก ซึ่งเป็นฝ่ายที่รู้สถานะ
bool buttonRunPressed(bool* fromIsr = nullptr);
bool buttonResetPressed();
// True while the RUN line reads pressed with no plausible person pressing it:
// either it has read that way since power-up, or it has held it for longer than
// anyone holds a button. On an NC button that state is an open circuit — a broken
// wire or an unseated connector — and RUN cannot raise an edge while it lasts, so
// the panel has no stop at all and no session should be started (including one
// commanded remotely). Clears as soon as the line moves, which a held button does
// when it is released and a cut wire never does.
// [TH] เป็นจริงเมื่อสาย RUN อ่านได้ว่ากดอยู่โดยไม่น่าจะมีคนกดจริง
//      คืออ่านได้แบบนั้นมาตั้งแต่เปิดเครื่อง หรือค้างอยู่นานเกินกว่าที่คนจะกดค้างไว้
//      สำหรับปุ่มแบบปกติปิด สถานะนั้นคือวงจรเปิด ซึ่งมักเป็นสายขาดหรือขั้วต่อหลวม
//      และปุ่ม RUN จะไม่เกิดขอบสัญญาณตราบที่ยังเป็นแบบนั้น หน้าปัดจึงไม่มีปุ่มหยุดเลย
//      ไม่ควรให้เริ่มรอบใหม่ รวมถึงรอบที่สั่งมาจากระยะไกล
//      ค่านี้จะถูกล้างทันทีที่ระดับขาเปลี่ยน ซึ่งปุ่มที่ถูกกดค้างจะเปลี่ยนเมื่อปล่อย แต่สายที่ขาดจะไม่มีวันเปลี่ยน
bool buttonRunWireSuspect();
