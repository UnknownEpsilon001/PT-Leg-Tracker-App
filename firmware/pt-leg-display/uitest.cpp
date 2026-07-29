#include "uitest.h"

#if UI_TEST_HOOK

#include <Arduino.h>
#include <SD.h>

// A second pointer device beside the real touchscreen, fed by serial commands
// instead of by a finger. It reports coordinates in screen space, so it bypasses
// the panel's calibration entirely — a tap here lands where the number says,
// which is what makes a scripted run repeatable.
// [TH] อุปกรณ์ชี้ตำแหน่งตัวที่สอง อยู่คู่กับทัชสกรีนจริง แต่รับค่าจากคำสั่งผ่านสายแทนการแตะด้วยนิ้ว
//      พิกัดที่ส่งเป็นพิกัดหน้าจอโดยตรง จึงข้ามการคาลิเบรตของแผงสัมผัสไปเลย
//      การแตะจึงตกตรงตำแหน่งที่สั่งเสมอ ทำให้การทดสอบซ้ำได้ผลเหมือนเดิมทุกครั้ง
static lv_indev_drv_t drv;
static int16_t px = 0, py = 0;
static bool pressed = false;
static uint32_t pressUntil = 0;

// A press has to survive at least one lv_timer_handler pass to be seen at all,
// and a click needs the release too. 60 ms clears both at any sane frame rate.
// [TH] การกดต้องค้างข้ามรอบ lv_timer_handler อย่างน้อยหนึ่งรอบจึงจะถูกมองเห็น
//      และการคลิกต้องมีจังหวะปล่อยด้วย ใช้ 60 มิลลิวินาทีก็พอสำหรับทุกอัตราเฟรมที่เป็นไปได้
static const uint32_t PRESS_MS = 60;

static void readCb(lv_indev_drv_t*, lv_indev_data_t* data) {
  data->point.x = px;
  data->point.y = py;
  data->state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

void uitestBegin() {
  lv_indev_drv_init(&drv);
  drv.type = LV_INDEV_TYPE_POINTER;
  drv.read_cb = readCb;
  lv_indev_drv_register(&drv);
  Serial.println("test: ready — tap X Y | type TEXT | clear | ta | screen | mem | cat PATH | reset");
}

static void dumpFile(const char* path) {
  File f = SD.open(path, FILE_READ);
  if (!f) {
    Serial.printf("test: cat %s: not found\n", path);
    return;
  }
  Serial.printf("test: cat %s (%u bytes)\n", path, (unsigned)f.size());
  while (f.available()) Serial.write(f.read());
  Serial.println();
  Serial.println("test: end");
  f.close();
}

static void handle(String cmd) {
  cmd.trim();
  if (cmd.length() == 0) return;

  if (cmd.startsWith("tap ")) {
    int sp = cmd.indexOf(' ', 4);
    if (sp < 0) { Serial.println("test: tap needs X and Y"); return; }
    px = (int16_t)cmd.substring(4, sp).toInt();
    py = (int16_t)cmd.substring(sp + 1).toInt();
    pressed = true;
    pressUntil = millis() + PRESS_MS;
    Serial.printf("test: tap %d,%d\n", px, py);
    return;
  }

  if (cmd.startsWith("type ")) {
    lv_obj_t* ta = uiTestTextarea();
    if (!ta) { Serial.println("test: type: no keyboard is showing"); return; }
    String text = cmd.substring(5);
    lv_textarea_add_text(ta, text.c_str());
    Serial.printf("test: typed %s\n", text.c_str());
    return;
  }

  if (cmd == "clear") {
    lv_obj_t* ta = uiTestTextarea();
    if (!ta) { Serial.println("test: clear: no keyboard is showing"); return; }
    lv_textarea_set_text(ta, "");
    Serial.println("test: cleared");
    return;
  }

  // Reads back the bytes the field holds, which proves storage and nothing about
  // rendering — see the warning in uitest.h.
  // [TH] อ่านค่าที่เก็บอยู่ในช่องกรอก พิสูจน์ได้แค่ว่าเก็บถูก ไม่ได้พิสูจน์ว่าวาดออกมาถูก
  if (cmd == "ta") {
    lv_obj_t* ta = uiTestTextarea();
    Serial.printf("test: ta=%s\n", ta ? lv_textarea_get_text(ta) : "(none)");
    return;
  }

  if (cmd == "screen") {
    Serial.printf("test: screen=%s\n", uiTestScreenName());
    return;
  }

  // The number that matters for the leak checks: open a dialog twenty times and
  // free_size must come back to where it started.
  // [TH] ตัวเลขสำคัญของการตรวจหน่วยความจำรั่ว เปิดหน้าต่างซ้ำยี่สิบครั้งแล้วค่า free_size ต้องกลับมาเท่าเดิม
  if (cmd == "mem") {
    lv_mem_monitor_t m;
    lv_mem_monitor(&m);
    Serial.printf("test: lvgl free=%u frag=%u%% biggest=%u\n", (unsigned)m.free_size,
                  (unsigned)m.frag_pct, (unsigned)m.free_biggest_size);
    return;
  }

  if (cmd.startsWith("cat ")) { dumpFile(cmd.substring(4).c_str()); return; }

  if (cmd == "reset") { Serial.println("test: resetting"); delay(50); ESP.restart(); }

  Serial.printf("test: unknown command: %s\n", cmd.c_str());
}

void uitestPoll() {
  if (pressed && (int32_t)(millis() - pressUntil) >= 0) pressed = false;

  static String line;
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n') { handle(line); line = ""; }
    else if (c != '\r') line += c;
  }
}

#endif  // UI_TEST_HOOK
