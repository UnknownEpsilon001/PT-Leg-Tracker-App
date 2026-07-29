#include "uitest.h"

#if UI_TEST_HOOK

#include <Arduino.h>
#include <SD.h>
#include "ui.h"  // uiSetSettingsKnown, for the "known" command

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

// A swipe is a press that travels. Scrolling a list cannot be done with taps:
// LVGL decides between a click and a scroll by how far the pointer moves while
// down, so the position has to change across several lv_timer_handler passes.
// [TH] การปัดคือการกดแล้วลากไปด้วย การเลื่อนรายการทำด้วยการแตะเฉยๆ ไม่ได้
//      เพราะ LVGL ตัดสินว่าเป็นการคลิกหรือการเลื่อนจากระยะที่นิ้วขยับระหว่างกดอยู่
//      ตำแหน่งจึงต้องเปลี่ยนไปเรื่อยๆ ข้ามหลายรอบของ lv_timer_handler
static bool swiping = false;
static int16_t swipeFromX, swipeFromY, swipeToX, swipeToY;
static uint32_t swipeStart = 0;
static const uint32_t SWIPE_MS = 250;

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

// Screenshot state. A frame is captured by asking LVGL to redraw everything and
// letting the flush callback hand each strip over as it goes past.
// [TH] สถานะการจับภาพหน้าจอ วิธีจับคือสั่งให้ LVGL วาดใหม่ทั้งหมด
//      แล้วรับแถบภาพจากฟังก์ชันผลักพิกเซลทีละแถบขณะที่มันวาด
static bool shooting = false;
static lv_area_t shotArea;
static uint32_t shotSent = 0, shotTotal = 0;

static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// Base64 rather than hex: half the bytes on a 115200 line, where a full frame is
// already 150 KB and takes about twenty seconds.
// [TH] ใช้ Base64 แทนเลขฐานสิบหก เพราะข้อมูลน้อยลงครึ่งหนึ่งบนสายความเร็ว 115200
//      ภาพเต็มหน้าจอมีขนาดราว 150 กิโลไบต์ และใช้เวลาส่งประมาณยี่สิบวินาทีอยู่แล้ว
static void b64Out(const uint8_t* d, size_t n) {
  for (size_t i = 0; i < n; i += 3) {
    uint32_t v = (uint32_t)d[i] << 16;
    if (i + 1 < n) v |= (uint32_t)d[i + 1] << 8;
    if (i + 2 < n) v |= d[i + 2];
    Serial.write(B64[(v >> 18) & 63]);
    Serial.write(B64[(v >> 12) & 63]);
    Serial.write(i + 1 < n ? B64[(v >> 6) & 63] : '=');
    Serial.write(i + 2 < n ? B64[v & 63] : '=');
  }
}

void uitestCaptureStrip(const lv_area_t* area, lv_color_t* pixels) {
  if (!shooting) return;

  uint32_t w = area->x2 - area->x1 + 1;
  for (int32_t y = area->y1; y <= area->y2; y++) {
    if (y < shotArea.y1 || y > shotArea.y2) continue;
    // One serial line per pixel row, tagged with its own y and x range, so a
    // dropped or garbled line costs that row and not the whole frame.
    // [TH] ส่งทีละแถวพิกเซล พร้อมบอกตำแหน่ง y และช่วง x ของแถวนั้น
    //      ถ้าบรรทัดใดหายหรือเพี้ยน จะเสียแค่แถวนั้น ไม่เสียทั้งภาพ
    int32_t x1 = area->x1 > shotArea.x1 ? area->x1 : shotArea.x1;
    int32_t x2 = area->x2 < shotArea.x2 ? area->x2 : shotArea.x2;
    if (x2 < x1) continue;
    lv_color_t* row = pixels + (uint32_t)(y - area->y1) * w + (x1 - area->x1);
    Serial.printf("SHOT %d %d %d ", (int)y, (int)x1, (int)x2);
    b64Out((const uint8_t*)row, (size_t)(x2 - x1 + 1) * sizeof(lv_color_t));
    Serial.println();
    shotSent += (uint32_t)(x2 - x1 + 1);

    // This runs inside the display flush callback, with lv_disp_flush_ready not
    // yet called. A row is about 900 bytes of base64, so on a 115200 line the
    // write blocks for tens of milliseconds once the transmit buffer fills — do
    // that 240 times without letting anything else run and the task watchdog
    // reboots the board mid-screenshot. Yielding hands the idle task its turn,
    // which is what feeds the watchdog.
    // [TH] โค้ดนี้ทำงานอยู่ในฟังก์ชันผลักพิกเซล ซึ่งยังไม่ได้แจ้ง lv_disp_flush_ready
    //      หนึ่งแถวมีข้อมูลราว 900 ไบต์ บนสายความเร็ว 115200 การเขียนจะค้างหลายสิบมิลลิวินาทีเมื่อบัฟเฟอร์เต็ม
    //      ถ้าทำแบบนั้น 240 รอบโดยไม่ปล่อยให้งานอื่นทำงานเลย ตัวเฝ้าระวังจะรีบูตบอร์ดกลางคัน
    //      การ yield คือการปล่อยให้งานว่างได้ทำงาน ซึ่งเป็นสิ่งที่เลี้ยงตัวเฝ้าระวังไว้
    yield();
  }

  if (shotSent >= shotTotal) {
    shooting = false;
    Serial.println("SHOT END");
  }
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

  if (cmd.startsWith("swipe ")) {
    int v[4], n = 0, from = 6;
    while (n < 4 && from < (int)cmd.length()) {
      int sp = cmd.indexOf(' ', from);
      if (sp < 0) sp = cmd.length();
      v[n++] = cmd.substring(from, sp).toInt();
      from = sp + 1;
    }
    if (n != 4) { Serial.println("test: swipe needs X1 Y1 X2 Y2"); return; }
    swipeFromX = (int16_t)v[0]; swipeFromY = (int16_t)v[1];
    swipeToX = (int16_t)v[2];   swipeToY = (int16_t)v[3];
    px = swipeFromX; py = swipeFromY;
    swipeStart = millis();
    swiping = true;
    pressed = true;
    Serial.printf("test: swipe %d,%d -> %d,%d\n", v[0], v[1], v[2], v[3]);
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

  // Unlocks SAVE and RESET without a controller on the desk. Both are gated on
  // the controller having reported its settings at least once, which is right on
  // a real machine and makes the reset dialog — the one screen with a history of
  // eating the LVGL pool — untestable on a bench with only the display attached.
  // [TH] ปลดล็อกปุ่มบันทึกและปุ่มคืนค่าโดยไม่ต้องมีคอนโทรลเลอร์ต่ออยู่
  //      ปกติปุ่มทั้งสองจะถูกกั้นไว้จนกว่าคอนโทรลเลอร์จะส่งค่าตั้งค่ามาอย่างน้อยหนึ่งครั้ง ซึ่งถูกต้องสำหรับเครื่องจริง
  //      แต่ทำให้หน้าต่างคืนค่า ซึ่งเป็นหน้าที่เคยกินหน่วยความจำ LVGL จนหมด ทดสอบไม่ได้ถ้ามีแต่จอ
  if (cmd == "known") {
    uiSetSettingsKnown(true);
    Serial.println("test: settings marked known — SAVE and RESET unlocked");
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

  // shot            — the whole screen
  // shot x1 y1 x2 y2 — one rectangle, which is the fast way to look at a label
  // [TH] shot คือจับทั้งหน้าจอ ส่วน shot พร้อมพิกัดสี่ตัวคือจับเฉพาะกรอบที่ระบุ
  //      ซึ่งเร็วกว่ามากเวลาต้องการดูแค่ข้อความบรรทัดเดียว
  if (cmd == "shot" || cmd.startsWith("shot ")) {
    shotArea.x1 = 0; shotArea.y1 = 0; shotArea.x2 = 319; shotArea.y2 = 239;
    if (cmd.startsWith("shot ")) {
      int v[4], n = 0, from = 5;
      while (n < 4 && from < (int)cmd.length()) {
        int sp = cmd.indexOf(' ', from);
        if (sp < 0) sp = cmd.length();
        v[n++] = cmd.substring(from, sp).toInt();
        from = sp + 1;
      }
      if (n == 4) {
        shotArea.x1 = v[0]; shotArea.y1 = v[1]; shotArea.x2 = v[2]; shotArea.y2 = v[3];
      }
    }
    shotSent = 0;
    shotTotal = (uint32_t)(shotArea.x2 - shotArea.x1 + 1) * (shotArea.y2 - shotArea.y1 + 1);
    shooting = true;
    Serial.printf("SHOT BEGIN %d %d %d %d\n", (int)shotArea.x1, (int)shotArea.y1,
                  (int)shotArea.x2, (int)shotArea.y2);
    // Redraw everything: LVGL only flushes what changed, and nothing has.
    // [TH] สั่งวาดใหม่ทั้งหมด เพราะปกติ LVGL จะผลักเฉพาะส่วนที่เปลี่ยน ซึ่งตอนนี้ไม่มีอะไรเปลี่ยน
    lv_obj_invalidate(lv_scr_act());
    return;
  }

  if (cmd.startsWith("cat ")) { dumpFile(cmd.substring(4).c_str()); return; }

  if (cmd == "reset") { Serial.println("test: resetting"); delay(50); ESP.restart(); }

  Serial.printf("test: unknown command: %s\n", cmd.c_str());
}

void uitestPoll() {
  if (swiping) {
    uint32_t el = millis() - swipeStart;
    if (el >= SWIPE_MS) {
      px = swipeToX;
      py = swipeToY;
      swiping = false;
      pressed = false;  // the release is what ends the gesture
    } else {
      px = (int16_t)(swipeFromX + (int32_t)(swipeToX - swipeFromX) * (int32_t)el / (int32_t)SWIPE_MS);
      py = (int16_t)(swipeFromY + (int32_t)(swipeToY - swipeFromY) * (int32_t)el / (int32_t)SWIPE_MS);
      pressed = true;
    }
  } else if (pressed && (int32_t)(millis() - pressUntil) >= 0) {
    pressed = false;
  }

  static String line;
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n') { handle(line); line = ""; }
    else if (c != '\r') line += c;
  }
}

#endif  // UI_TEST_HOOK
