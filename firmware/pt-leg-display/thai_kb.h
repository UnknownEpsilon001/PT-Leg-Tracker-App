#pragma once
#include <lvgl.h>

// แป้นพิมพ์ภาษาไทยแบบโทรศัพท์สำหรับจอ CYD
// Phone-style Thai keyboard pages for the CYD's on-screen keyboards.
//
// Two Thai pages are installed into LVGL's USER_1 (unshifted) and USER_2
// (shifted) keyboard modes; the Latin pages LVGL ships with are left alone.
// [TH] ติดตั้งแป้นไทยสองหน้าในโหมด USER_1 และ USER_2 ของ LVGL
//      ส่วนแป้นภาษาอังกฤษที่มากับ LVGL ยังใช้ได้ตามเดิม
//
// Call once per keyboard, right after lv_keyboard_create. startThai decides
// which script the keyboard opens on: true for patient names, false for
// settings, where the fields are ASCII by protocol.
// [TH] เรียกครั้งเดียวต่อแป้นพิมพ์หนึ่งตัว หลังสร้างแป้นพิมพ์เสร็จ
//      startThai กำหนดว่าจะเปิดมาเป็นภาษาไทยหรืออังกฤษ ชื่อคนไข้ใช้ไทย ส่วนหน้าตั้งค่าใช้อังกฤษ
void thaiKbApply(lv_obj_t* kb, bool startThai);
