#include "thai_kb.h"
#include <string.h>

// Ten columns, because 320 px / 10 gives 32 px keys. A PC Kedmanee layout wants
// twelve, and 26 px keys on a resistive panel are hard to hit.
// [TH] ใช้สิบคอลัมน์ เพราะ 320 หารสิบได้ปุ่มกว้าง 32 พิกเซล
//      ถ้าเรียงแบบแป้นพิมพ์คอมพิวเตอร์ต้องใช้สิบสองคอลัมน์ ปุ่มจะเหลือ 26 พิกเซล ซึ่งกดยากบนจอสัมผัสแบบต้านทาน
static const char* MAP_TH1[] = {
  "ๆ", "ไ", "ำ", "พ", "ะ", "ั", "ี", "ร", "น", "ย", "\n",
  "บ", "ล", "ฟ", "ห", "ก", "ด", "เ", "้", "่", "า", "\n",
  "ส", "ว", "ง", "ผ", "ป", "แ", "อ", "ิ", "ื", "ท", "\n",
  LV_SYMBOL_UP, "ม", "ใ", "ฝ", "ถ", "ุ", "ึ", "ค", "ต", LV_SYMBOL_BACKSPACE, "\n",
  "?123", LV_SYMBOL_KEYBOARD, " ", LV_SYMBOL_OK, ""
};

// The characters page 1 has no room for. Every Thai character must be reachable
// from one of the two pages: LVGL's button matrix has no long-press popup, so
// there is no third way to get at a rare consonant.
// [TH] ตัวอักษรที่หน้าแรกใส่ไม่พอ ทุกตัวต้องกดถึงได้จากสองหน้านี้
//      เพราะปุ่มของ LVGL ไม่มีการกดค้างเพื่อเลือกตัวอักษรอื่น
static const char* MAP_TH2[] = {
  "ข", "ช", "ฃ", "ฅ", "ฆ", "จ", "ฉ", "ซ", "ฌ", "ญ", "\n",
  "ฎ", "ฏ", "ฐ", "ฑ", "ฒ", "ณ", "ธ", "ภ", "ศ", "ษ", "\n",
  "ฬ", "ฮ", "ฤ", "ฦ", "โ", "ู", "็", "๊", "๋", "์", "\n",
  LV_SYMBOL_UP, "ํ", "ฯ", "ฺ", "฿", "๏", "๚", "๛", LV_SYMBOL_BACKSPACE, "\n",
  "?123", LV_SYMBOL_KEYBOARD, " ", LV_SYMBOL_OK, ""
};

// The low bits of a control entry are the key's relative width, so the bottom
// row reads 2 + 2 + 4 + 2 = ten units, matching the ten columns above it.
// [TH] บิตล่างของค่าควบคุมคือความกว้างของปุ่ม แถวล่างจึงรวมได้ 2+2+4+2 = สิบหน่วย เท่ากับสิบคอลัมน์ด้านบน
static const lv_btnmatrix_ctrl_t CTRL_TH1[] = {
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  2, 2, 4, 2
};

static const lv_btnmatrix_ctrl_t CTRL_TH2[] = {
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  2, 1, 1, 1, 1, 1, 1, 1, 1,
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
    lv_keyboard_mode_t m = lv_keyboard_get_mode(kb);
    lv_keyboard_set_mode(kb, m == LV_KEYBOARD_MODE_USER_1 ? LV_KEYBOARD_MODE_USER_2
                                                          : LV_KEYBOARD_MODE_USER_1);
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
  lv_obj_remove_event_cb(kb, lv_keyboard_def_event_cb);
  lv_obj_add_event_cb(kb, thaiKbEvent, LV_EVENT_VALUE_CHANGED, NULL);
  if (startThai) lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_USER_1);
}
