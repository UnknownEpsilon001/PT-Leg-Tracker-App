#pragma once
#include <stdint.h>

// เสียงพูดภาษาไทยจากการ์ด SD ผ่านลำโพงของบอร์ดจอ
// Spoken Thai cues, played from /sounds on the SD card through the CYD's
// speaker output on GPIO 26.
//
// This module never touches LVGL and the UI does not know it exists: the sketch
// is the only caller. It opens the card directly, the way sdlog.cpp and
// settingsfile.cpp do — the rule that the card is not touched directly is a rule
// about ui.cpp, not about modules.
// [TH] โมดูลนี้ไม่ยุ่งกับ LVGL และฝั่ง UI ไม่รู้ว่ามีโมดูลนี้อยู่ มีแต่สเก็ตช์ที่เรียกใช้
//      โมดูลเปิดการ์ดเอง เหมือนที่ sdlog.cpp และ settingsfile.cpp ทำ
//      กติกาที่ห้ามแตะการ์ดโดยตรงเป็นกติกาของ ui.cpp ไม่ใช่ของโมดูล
//
// Audio is strictly additive: nothing here stops the machine, blocks a session,
// or changes what the screen shows. Every failure ends in silence plus one
// serial line, never in a stall.
// [TH] เสียงเป็นส่วนเสริมล้วนๆ ไม่มีอะไรในนี้ที่หยุดเครื่อง ขวางรอบการทำงาน หรือเปลี่ยนสิ่งที่จอแสดง
//      ความผิดพลาดทุกแบบจบที่ความเงียบพร้อมข้อความหนึ่งบรรทัด ไม่ใช่การค้าง

enum class Cue {
  Boot,    // เครื่องพร้อมใช้งาน — /sounds/boot.wav
  Start,   // เริ่มการทำงาน — /sounds/start.wav
  Stop,    // หยุดการทำงาน — /sounds/stop.wav
  Target,  // ครบตามเป้าหมายแล้ว — /sounds/target.wav
};

// Allocates the playback buffer and creates the DAC channel. Safe to call with
// no card in the slot.
// [TH] จองบัฟเฟอร์และสร้างช่องสัญญาณ DAC เรียกได้แม้ไม่มีการ์ดในช่อง
void audioBegin();

// Called every loop(). A steady-state call does at most one 4 KB card read or
// one non-blocking DMA write, both cheap.
//
// The exception is the tick that starts a cue: that one also opens the file,
// reads its header and seeks, which on a slow card is tens of milliseconds. That
// work is here rather than in audioPlay() deliberately — audioPlay runs inside a
// link callback, where the same delay sat in the UART receive path and, for the
// stop cue, on the same pass as the sessions.csv append. It is the one call that
// can exceed a millisecond, and LOOP_DEBUG times it separately so it shows up as
// audio rather than as LVGL.
// [TH] เรียกทุกลูป ในสภาวะปกติจะอ่านการ์ดไม่เกิน 4 กิโลไบต์ หรือเขียน DMA แบบไม่รอหนึ่งครั้ง
//      ซึ่งทั้งสองอย่างใช้เวลาน้อย
//      ข้อยกเว้นคือรอบที่เริ่มเสียงใหม่ รอบนั้นจะเปิดไฟล์ อ่านส่วนหัว และเลื่อนตำแหน่งด้วย
//      ซึ่งบนการ์ดที่ช้าใช้เวลาหลายสิบมิลลิวินาที
//      งานส่วนนี้อยู่ตรงนี้แทนที่จะอยู่ใน audioPlay() โดยตั้งใจ
//      เพราะ audioPlay ทำงานอยู่ในคอลแบ็กของลิงก์ ความหน่วงเดียวกันจะไปอยู่ในเส้นทางรับ UART
//      และสำหรับเสียงหยุด จะตกอยู่รอบเดียวกับการเขียน sessions.csv
//      นี่เป็นการเรียกเดียวที่เกินหนึ่งมิลลิวินาทีได้ และโหมด LOOP_DEBUG จับเวลาแยกไว้
//      จะได้แสดงเป็นเวลาของเสียง ไม่ใช่ของ LVGL
void audioLoop();

// Requests a cue and returns immediately — it records the request and touches
// neither the card nor the DAC. The next audioLoop() does that work.
//
// The newest request wins: one already loading or playing is abandoned, because
// the event that just happened is the one worth hearing. Two requests inside a
// single poll batch collapse to the later one.
// [TH] ขอเล่นเสียงหนึ่งรายการแล้วคืนค่าทันที ฟังก์ชันนี้แค่บันทึกคำขอไว้
//      ไม่แตะทั้งการ์ดและ DAC งานเหล่านั้นทำใน audioLoop() รอบถัดไป
//      คำขอล่าสุดชนะเสมอ รายการที่กำลังโหลดหรือเล่นอยู่จะถูกยกเลิก
//      เพราะเหตุการณ์ที่เพิ่งเกิดคือเรื่องที่ควรได้ยิน
//      คำขอสองครั้งในรอบ poll เดียวกันจะเหลือเพียงครั้งหลัง
void audioPlay(Cue c);

// From the settings screen. Off drops requests inside audioPlay(), before any
// card access.
// [TH] มาจากหน้าตั้งค่า ถ้าปิดอยู่ คำขอจะถูกทิ้งใน audioPlay() ก่อนแตะการ์ด
void audioSetEnabled(bool on);

// False when the buffer or the DAC channel could not be had at boot. The machine
// then runs exactly as it does today, silently.
// [TH] เป็นเท็จเมื่อจองบัฟเฟอร์หรือสร้างช่อง DAC ไม่สำเร็จตอนบูต
//      เครื่องจะทำงานเหมือนเดิมทุกอย่าง เพียงแต่ไม่มีเสียง
bool audioReady();
