#include "thai_kb.h"
#include <Arduino.h>  // Serial, for the one failure worth shouting about
#include <string.h>

// Kedmanee, in the row order a Thai keyboard actually has: the number row, then
// the three letter rows. Reflowing it into ten wide columns was tried first and
// rejected on sight — it moves บ and ล off the end of the first row and every
// key after them shifts, so the layout stops matching the one in the typist's
// hands. Twelve columns means 26 px keys, which is the price of that fidelity.
// [TH] ผังแบบเกษมณีเรียงแถวเหมือนแป้นพิมพ์ไทยจริง คือแถวตัวเลขแล้วตามด้วยแถวตัวอักษรสามแถว
//      เคยลองจัดใหม่ให้เหลือสิบคอลัมน์เพื่อให้ปุ่มใหญ่ขึ้น แต่ใช้ไม่ได้
//      เพราะ บ กับ ล จะหลุดไปแถวอื่น แล้วปุ่มถัดจากนั้นเลื่อนตามกันหมด ผังจึงไม่ตรงกับที่คนพิมพ์จำได้
//      ใช้สิบสองคอลัมน์ ปุ่มกว้าง 26 พิกเซล ถือเป็นราคาที่ต้องจ่ายเพื่อความถูกต้องของผัง
static const char* MAP_TH1[] = {
  "ๅ", "/", "_", "ภ", "ถ", "ุ", "ึ", "ค", "ต", "จ", "ข", "ช", "\n",
  "ๆ", "ไ", "ำ", "พ", "ะ", "ั", "ี", "ร", "น", "ย", "บ", "ล", "\n",
  "ฟ", "ห", "ก", "ด", "เ", "้", "่", "า", "ส", "ว", "ง", "ฃ", "\n",
  LV_SYMBOL_UP, "ผ", "ป", "แ", "อ", "ิ", "ื", "ท", "ม", "ใ", "ฝ", LV_SYMBOL_BACKSPACE, "\n",
  "?123", LV_SYMBOL_KEYBOARD, " ", LV_SYMBOL_OK, ""
};

// The shifted layer, also Kedmanee. Every Thai character has to be on one of the
// two pages: LVGL's button matrix has no long-press popup, so there is no third
// place to put a rare consonant. Thai numerals ride along on the number row
// because Kedmanee puts them there and the row exists either way.
// [TH] ชั้นที่กด Shift ก็เรียงแบบเกษมณีเช่นกัน ตัวอักษรไทยทุกตัวต้องอยู่ในสองหน้านี้
//      เพราะปุ่มของ LVGL ไม่มีการกดค้างเพื่อเลือกตัวอักษรอื่น จึงไม่มีที่เก็บตัวที่ใช้น้อยไว้ที่อื่น
//      ส่วนเลขไทยใส่ไว้ในแถวตัวเลขตามผังเกษมณี เพราะแถวนี้มีอยู่แล้ว
static const char* MAP_TH2[] = {
  "+", "๑", "๒", "๓", "๔", "ู", "฿", "๕", "๖", "๗", "๘", "๙", "\n",
  "๐", "\"", "ฎ", "ฑ", "ธ", "ํ", "๊", "ณ", "ฯ", "ญ", "ฐ", ",", "\n",
  "ฤ", "ฆ", "ฏ", "โ", "ฌ", "็", "๋", "ษ", "ศ", "ซ", ".", "ฅ", "\n",
  LV_SYMBOL_UP, "(", ")", "ฉ", "ฮ", "ฺ", "์", "?", "ฒ", "ฬ", "ฦ", LV_SYMBOL_BACKSPACE, "\n",
  "?123", LV_SYMBOL_KEYBOARD, " ", LV_SYMBOL_OK, ""
};

// The low bits of a control entry are the key's relative width, and widths are
// relative within their own row, so the bottom row's 2 + 2 + 6 + 2 spans the
// same width as the twelve single-width keys above it.
// [TH] บิตล่างของค่าควบคุมคือความกว้างของปุ่ม และความกว้างคิดเทียบกันเฉพาะภายในแถวเดียวกัน
//      แถวล่างจึงใช้ 2+2+6+2 ซึ่งกว้างเท่ากับปุ่มขนาดหนึ่งหน่วยสิบสองปุ่มในแถวบน
static const lv_btnmatrix_ctrl_t CTRL_TH1[] = {
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  2, 2, 6, 2
};

static const lv_btnmatrix_ctrl_t CTRL_TH2[] = {
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  2, 2, 6, 2
};

// The Latin pages are ours too, because LVGL's stock ones have no globe key:
// its bottom row is close / left / space / right / OK, so a keyboard switched to
// Latin could never get back to Thai — and the mode outlives the screen, so
// reopening the form did not rescue it either. Ten columns here, phone-shaped;
// the Thai pages need twelve only because Kedmanee has twelve.
// [TH] แป้นภาษาอังกฤษก็ต้องทำเองด้วย เพราะแป้นที่มากับ LVGL ไม่มีปุ่มลูกโลก
//      แถวล่างของมันคือปิด/ซ้าย/เว้นวรรค/ขวา/ตกลง เมื่อสลับเป็นอังกฤษแล้วจะกลับมาเป็นไทยไม่ได้เลย
//      และโหมดยังค้างอยู่แม้ปิดหน้าจอไปแล้ว เปิดใหม่ก็ไม่ช่วย
//      แป้นอังกฤษใช้สิบคอลัมน์แบบโทรศัพท์ ส่วนแป้นไทยต้องใช้สิบสองเพราะผังเกษมณีมีสิบสอง
static const char* MAP_EN1[] = {
  "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", "\n",
  "a", "s", "d", "f", "g", "h", "j", "k", "l", "\n",
  LV_SYMBOL_UP, "z", "x", "c", "v", "b", "n", "m", LV_SYMBOL_BACKSPACE, "\n",
  "?123", LV_SYMBOL_KEYBOARD, " ", LV_SYMBOL_OK, ""
};

static const char* MAP_EN2[] = {
  "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "\n",
  "A", "S", "D", "F", "G", "H", "J", "K", "L", "\n",
  LV_SYMBOL_UP, "Z", "X", "C", "V", "B", "N", "M", LV_SYMBOL_BACKSPACE, "\n",
  "?123", LV_SYMBOL_KEYBOARD, " ", LV_SYMBOL_OK, ""
};

static const lv_btnmatrix_ctrl_t CTRL_EN[] = {
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1,
  2, 2, 4, 2
};

// LVGL's own keyboard handler knows only the keys in its own maps. A "?123" or
// globe key defined in a user map is not one of them, so the stock handler types
// the label into the textarea instead of switching pages — a patient called
// "สมชาย?123" is the symptom. It is removed and this one put in its place, which
// means this function owns the Latin pages' control keys too.
// [TH] ตัวจัดการปุ่มของ LVGL รู้จักเฉพาะปุ่มในแป้นของตัวเอง ปุ่ม "?123" หรือปุ่มลูกโลกที่เราสร้างเองมันไม่รู้จัก
//      มันจะพิมพ์ข้อความบนปุ่มลงในช่องกรอกแทนที่จะสลับหน้า จึงต้องถอดออกแล้วใช้ตัวนี้แทน
//      ผลคือฟังก์ชันนี้ต้องดูแลปุ่มควบคุมของแป้นภาษาอังกฤษด้วย
static void thaiKbEvent(lv_event_t* e) {
  lv_obj_t* kb = (lv_obj_t*)lv_event_get_target(e);
  lv_obj_t* ta = lv_keyboard_get_textarea(kb);
  uint16_t id = lv_btnmatrix_get_selected_btn(kb);
  const char* txt = lv_btnmatrix_get_btn_text(kb, id);
  if (!txt) return;

  // Page and script switching.
  // [TH] การสลับหน้าแป้นและสลับภาษา
  if (strcmp(txt, LV_SYMBOL_UP) == 0) {
    // Shift means "the other half of this script", whichever script is showing.
    // [TH] ปุ่ม Shift หมายถึงสลับไปอีกครึ่งหนึ่งของภาษาที่กำลังใช้อยู่ ไม่ว่าจะเป็นภาษาใด
    switch (lv_keyboard_get_mode(kb)) {
      case LV_KEYBOARD_MODE_USER_1: lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_USER_2); break;
      case LV_KEYBOARD_MODE_USER_2: lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_USER_1); break;
      case LV_KEYBOARD_MODE_TEXT_UPPER: lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_LOWER); break;
      default: lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_UPPER); break;
    }
    return;
  }
  if (strcmp(txt, LV_SYMBOL_KEYBOARD) == 0) {
    lv_keyboard_mode_t m = lv_keyboard_get_mode(kb);
    bool onThai = (m == LV_KEYBOARD_MODE_USER_1 || m == LV_KEYBOARD_MODE_USER_2);
    lv_keyboard_set_mode(kb, onThai ? LV_KEYBOARD_MODE_TEXT_LOWER : LV_KEYBOARD_MODE_USER_1);
    return;
  }
  if (strcmp(txt, "?123") == 0 || strcmp(txt, "1#") == 0) {
    lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_SPECIAL);
    return;
  }
  if (strcmp(txt, "ABC") == 0) { lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_UPPER); return; }
  if (strcmp(txt, "abc") == 0) { lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_LOWER); return; }

  // Editing and closing, kept identical to what the stock handler did, so the
  // OK and X keys behave the way every other screen already expects.
  // [TH] ปุ่มแก้ไขและปุ่มปิด ทำงานเหมือนของเดิมทุกอย่าง เพื่อให้ปุ่มตกลงและปุ่มกากบาทยังทำงานตามที่หน้าจออื่นคาดไว้
  if (strcmp(txt, LV_SYMBOL_BACKSPACE) == 0) { if (ta) lv_textarea_del_char(ta); return; }
  if (strcmp(txt, LV_SYMBOL_OK) == 0 || strcmp(txt, LV_SYMBOL_NEW_LINE) == 0) {
    lv_event_send(kb, LV_EVENT_READY, NULL);
    return;
  }
  if (strcmp(txt, LV_SYMBOL_CLOSE) == 0) {
    lv_event_send(kb, LV_EVENT_CANCEL, NULL);
    return;
  }
  if (strcmp(txt, LV_SYMBOL_LEFT) == 0) { if (ta) lv_textarea_cursor_left(ta); return; }
  if (strcmp(txt, LV_SYMBOL_RIGHT) == 0) { if (ta) lv_textarea_cursor_right(ta); return; }

  // Anything else is a character. lv_textarea_add_text takes a UTF-8 string, so
  // a three-byte Thai character arrives intact.
  // [TH] ที่เหลือคือตัวอักษร ฟังก์ชันนี้รับสตริงแบบ UTF-8 อักษรไทยขนาดสามไบต์จึงเข้าไปครบ
  if (ta) lv_textarea_add_text(ta, txt);
}

void thaiKbApply(lv_obj_t* kb, bool startThai) {
  lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_USER_1, MAP_TH1, CTRL_TH1);
  lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_USER_2, MAP_TH2, CTRL_TH2);
  lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_TEXT_LOWER, MAP_EN1, CTRL_EN);
  lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_TEXT_UPPER, MAP_EN2, CTRL_EN);
  // Everything here rests on the stock handler being gone. If it is not, both
  // run, and the one that does not recognise a user map's control keys appends
  // their labels to the textarea — a patient saved as "สมชาย?123", silently, onto
  // the card. Too quiet a failure to leave unreported.
  // [TH] ทั้งหมดนี้ตั้งอยู่บนเงื่อนไขว่าตัวจัดการเดิมถูกถอดออกแล้ว ถ้าไม่ถูกถอด จะทำงานพร้อมกันทั้งคู่
  //      ตัวที่ไม่รู้จักปุ่มควบคุมของแป้นที่เราสร้างเองจะพิมพ์ข้อความบนปุ่มลงในช่องกรอก
  //      กลายเป็นชื่อคนไข้ "สมชาย?123" บันทึกลงการ์ดไปเงียบๆ จึงต้องรายงานถ้าถอดไม่สำเร็จ
  if (!lv_obj_remove_event_cb(kb, lv_keyboard_def_event_cb))
    Serial.println("thai_kb: could not remove LVGL's keyboard handler — keys will double-handle");
  lv_obj_add_event_cb(kb, thaiKbEvent, LV_EVENT_VALUE_CHANGED, NULL);
  thaiKbSetScript(kb, startThai);
}

// The mode outlives the screen: a keyboard left on Latin is still on Latin the
// next time the form opens, which makes the add-patient form quietly stop being
// a Thai form after one visit. Callers reset it when they show the keyboard.
// [TH] โหมดของแป้นพิมพ์ค้างอยู่แม้ปิดหน้าจอไปแล้ว ถ้าเคยสลับเป็นอังกฤษไว้ เปิดฟอร์มครั้งถัดไปก็ยังเป็นอังกฤษ
//      ฟอร์มลงทะเบียนคนไข้จะเลิกเป็นฟอร์มภาษาไทยไปเงียบๆ หลังใช้ครั้งเดียว ตัวเรียกจึงต้องตั้งค่าใหม่ทุกครั้งที่เปิด
void thaiKbSetScript(lv_obj_t* kb, bool thai) {
  lv_keyboard_set_mode(kb, thai ? LV_KEYBOARD_MODE_USER_1 : LV_KEYBOARD_MODE_TEXT_LOWER);
}
