// การสร้างส่วนติดต่อผู้ใช้ทั้งหมดบนจอสัมผัส CYD ด้วย LVGL
// ประกอบด้วยหน้าจอหลัก (นาฬิกา/จำนวนครั้ง/สถานะ แสดงผลอย่างเดียว) และหน้าตั้งค่า (WiFi/เซิร์ฟเวอร์/เวลารอบ)
// All LVGL UI for the CYD touchscreen: output-only main screen and settings screen.
#include "ui.h"
#include <Arduino.h>  // millis() for the in-phase timer — ใช้ millis() จับเวลาในสถานะ
#include <time.h>     // gmtime_r for the clock — ใช้แปลงเวลาสำหรับนาฬิกา
#include <lvgl.h>
#include <TFT_eSPI.h>
#include "touch_bb.h"  // touch pins live there — ขาทัชสกรีนอยู่ในไฟล์นั้น
#include <WiFi.h>  // settings screen scans for SSIDs — หน้าตั้งค่าใช้สแกนหา WiFi
#include "settingsfile.h"  // settingsFileDefaults for RESET — ค่าตั้งต้นสำหรับปุ่มคืนค่า
#include "thai_kb.h"       // Thai key pages for both keyboards — แป้นไทยของทั้งสองแป้นพิมพ์
#include "uitest.h"        // UI_TEST_HOOK and the two accessors below — โหมดทดสอบผ่านสาย

// App palette (matches the phone app's v4 rebrand)
// [TH] ชุดสีของแอป ให้เข้ากับธีมของแอปบนมือถือ
#define COL_BG 0x1a2b47      // navy-dark  - screen background
#define COL_SURFACE 0x24395e // navy       - cards, inputs
#define COL_TEXT 0xf2f5f9    // near-white - primary text
#define COL_MUTED 0xc6ccd6   // silver     - secondary text
#define COL_FAULT 0xb00020   // red        - fault

static TFT_eSPI tft;

static lv_disp_draw_buf_t drawBuf;
// A taller buffer means far fewer flush round-trips per frame, which is most of
// the sluggishness on this panel. 240x100x2B = 48 KB - too big to sit in static
// DRAM next to LVGL's own 48 KB pool, so it comes off the heap in uiBegin().
#define DRAW_BUF_LINES 100
static lv_color_t* buf = nullptr;

static void buildMain();
static void initThaiFonts();         // defined with the main screen below
static void leaveSubScreenForRun();  // defined with the settings screen below
static void hideAddKeyboard();       // defined with the add-patient form below
static bool settingsScreenOpen();    // defined with the settings screen below
static bool confirmScreenActive();   // defined with the confirm screen below
static void openTimeScreen();        // defined with the date/time screen below
static void openHistory();           // defined with the history screen below

static void flushCb(lv_disp_drv_t* disp, const lv_area_t* area, lv_color_t* pixels) {
  // [TH] LVGL เรียกฟังก์ชันนี้เพื่อผลักพิกเซลลงหน้าจอจริงตามพื้นที่ที่เปลี่ยน
  uint32_t w = area->x2 - area->x1 + 1, h = area->y2 - area->y1 + 1;
  tft.startWrite();
  tft.setAddrWindow(area->x1, area->y1, w, h);
  // swap=false: lv_conf.h sets LV_COLOR_16_SWAP 1, so LVGL already hands us
  // byte-swapped pixels. Swapping again here mangles every colour and shows up
  // as fringed outlines around anti-aliased text.
  // [TH] swap=false: เพราะ lv_conf.h ตั้ง LV_COLOR_16_SWAP 1 ทำให้ LVGL สลับไบต์มาแล้ว
  //      ถ้าสลับซ้ำตรงนี้สีจะเพี้ยนและมีขอบสีรอบตัวอักษร
  tft.pushColors((uint16_t*)&pixels->full, w * h, false);
  tft.endWrite();
#if UI_TEST_HOOK
  // The same strip goes out of the serial port when a screenshot is running.
  // There is no framebuffer to read back — 320x240 at 16 bpp is 150 KB and this
  // board draws in strips of 100 lines — so the only place a whole frame exists
  // is right here, one piece at a time.
  // [TH] ตอนกำลังจับภาพหน้าจอ แถบพิกเซลชุดเดียวกันนี้จะถูกส่งออกทางพอร์ตอนุกรมด้วย
  //      บอร์ดนี้ไม่มีบัฟเฟอร์เก็บทั้งหน้าจอ เพราะ 320x240 ที่ 16 บิตต่อพิกเซลกินถึง 150 กิโลไบต์
  //      และวาดทีละแถบ 100 บรรทัด จุดเดียวที่เห็นภาพครบคือตรงนี้ ทีละชิ้น
  uitestCaptureStrip(area, pixels);
#endif
  lv_disp_flush_ready(disp);
}

static void touchCb(lv_indev_drv_t*, lv_indev_data_t* data) {
  // [TH] อ่านตำแหน่งสัมผัสแล้วแปลงพิกัดดิบของทัชสกรีนเป็นพิกัดหน้าจอ
  static bool wasDown = false;
  uint16_t rx, ry;
  if (touchBBRead(rx, ry)) {
    // The old driver applied setRotation(1) itself and handed back landscape
    // coordinates; these are unrotated panel counts, so the axis swap lives here
    // now. Both panel axes ascend with the screen's: panel Y grows towards screen
    // X, panel X grows towards screen Y.
    //
    // The ranges below are measured, not guessed. Panel X read ~700 with a finger
    // on the gear button, whose centre is y=24, and spans 336..3813 end to end;
    // the first version of this line ran the map descending and put every press
    // 240 pixels away from where the finger was, which looked exactly like a dead
    // touch panel. Re-tune against a button of known position — corner taps are
    // ambiguous once the display is rotated, and they mislead.
    // [TH] ไดรเวอร์เดิมหมุนภาพให้เองแล้วคืนพิกัดแนวนอนมา ค่าที่ได้ตรงนี้เป็นค่าดิบยังไม่หมุน จึงต้องสลับแกนเอง
    //      ค่าช่วงด้านล่างวัดจากเครื่องจริง ไม่ใช่ค่าเดา ตอนแตะปุ่มเฟือง (จุดกึ่งกลางที่ y=24) อ่านแกน X ของแผงได้ราว 700
    //      รุ่นแรกของบรรทัดนี้แปลงค่าแบบกลับด้าน ทำให้จุดที่แตะเพี้ยนไป 240 พิกเซล ดูเหมือนทัชสกรีนเสีย
    //      ถ้าต้องปรับใหม่ ให้อ้างอิงปุ่มที่รู้พิกัด อย่าใช้การแตะมุมจอเพราะกำกวมเมื่อจอถูกหมุน
    int sx = map(ry, 240, 3800, 0, 320);
    int sy = map(rx, 340, 3800, 0, 240);
    data->point.x = constrain(sx, 0, 319);
    data->point.y = constrain(sy, 0, 239);
    data->state = LV_INDEV_STATE_PRESSED;
    // One line per press, not per poll: the ranges above are a starting point
    // and get retuned against these numbers on the bench.
    // [TH] พิมพ์บรรทัดเดียวต่อการกดหนึ่งครั้ง ไม่ใช่ทุกครั้งที่อ่าน
    //      ค่าช่วงด้านบนเป็นค่าตั้งต้น ใช้ตัวเลขนี้ปรับจูนหน้างาน
    if (!wasDown) Serial.printf("touch raw=%u,%u -> %d,%d\n", rx, ry, (int)data->point.x,
                                (int)data->point.y);
    wasDown = true;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
    wasDown = false;
  }
}

// ------------------------------------------------------------------- helpers

static void styleScreen(lv_obj_t* scr) {
  lv_obj_set_style_bg_color(scr, lv_color_hex(COL_BG), 0);
  lv_obj_set_style_text_color(scr, lv_color_hex(COL_TEXT), 0);
  lv_obj_set_style_border_width(scr, 0, 0);
  lv_obj_set_style_pad_all(scr, 0, 0);
}

static lv_obj_t* makeBtn(lv_obj_t* parent, const char* text, uint32_t colour,
                         const lv_font_t* font, lv_event_cb_t cb) {
  lv_obj_t* b = lv_btn_create(parent);
  lv_obj_set_style_bg_color(b, lv_color_hex(colour), 0);
  lv_obj_set_style_radius(b, 8, 0);
  if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* l = lv_label_create(b);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(COL_TEXT), 0);
  lv_label_set_text(l, text);
  lv_obj_center(l);
  return b;
}

void uiBegin() {
  // [TH] เริ่มต้นจอ TFT, ทัชสกรีน, LVGL และจองบัฟเฟอร์วาดภาพจากหน่วยความจำ DMA
  tft.init();
  tft.setRotation(1); // landscape 320x240 — แนวนอน
  touchBBBegin();

  lv_init();
  uint32_t lines = DRAW_BUF_LINES;
  buf = (lv_color_t*)heap_caps_malloc(240 * lines * sizeof(lv_color_t), MALLOC_CAP_DMA);
  while (!buf && lines > 10) {  // degrade rather than boot-loop on a tight heap
                               // [TH] ถ้าหน่วยความจำไม่พอ ให้ลดขนาดบัฟเฟอร์ลงแทนที่จะบูตวนไม่จบ
    lines /= 2;
    buf = (lv_color_t*)heap_caps_malloc(240 * lines * sizeof(lv_color_t), MALLOC_CAP_DMA);
  }
  if (!buf) {
    // Handing LVGL a null buffer trips LV_ASSERT_MALLOC, whose handler is
    // while(1) - a silent freeze. A reboot is far easier to diagnose.
    // [TH] ถ้าส่งบัฟเฟอร์ null ให้ LVGL จะไปติด LV_ASSERT_MALLOC ที่ค้างเป็น while(1) เงียบๆ
    //      รีบูตแทนเพื่อให้วินิจฉัยปัญหาได้ง่ายกว่า
    Serial.println("FATAL: no DMA memory for the draw buffer, restarting");
    Serial.flush();
    esp_restart();
  }
  Serial.printf("lvgl draw buffer: %lu lines, free heap %lu\n", (unsigned long)lines,
                (unsigned long)ESP.getFreeHeap());
  lv_disp_draw_buf_init(&drawBuf, buf, nullptr, 240 * lines);

  static lv_disp_drv_t dispDrv;
  lv_disp_drv_init(&dispDrv);
  dispDrv.hor_res = 320;
  dispDrv.ver_res = 240;
  dispDrv.flush_cb = flushCb;
  dispDrv.draw_buf = &drawBuf;
  lv_disp_drv_register(&dispDrv);

  static lv_indev_drv_t indevDrv;
  lv_indev_drv_init(&indevDrv);
  indevDrv.type = LV_INDEV_TYPE_POINTER;
  indevDrv.read_cb = touchCb;
  lv_indev_drv_register(&indevDrv);

  initThaiFonts();  // must precede buildMain(): it styles labels with these fonts
  buildMain();
}

// ---------------------------------------------------------------- main screen

static lv_obj_t *scrMain, *lblClock, *lblReps, *lblPhase, *lblPhaseTimer, *lblStatus;
static lv_obj_t *lblCode, *lblBrand, *btnGear, *btnData, *imgLogo;
static lv_obj_t *rowTarget, *btnJogExit;
// Declared here, ahead of the settings screen's other spinboxes further down, so
// that uiUpdateMain (below) can keep it synced to the live target — see the
// comment at that call site for why. Not grouped with spinMax/spinLift/etc. only
// because of that ordering requirement.
// [TH] ประกาศไว้ตรงนี้ ก่อนช่องปรับค่าอื่นๆ ของหน้าตั้งค่าที่อยู่ถัดลงไป เพื่อให้ uiUpdateMain
//      ด้านล่างผูกค่าให้ตรงกับเป้าหมายที่ใช้งานจริงได้ ดูคำอธิบายที่จุดเรียกนั้น
//      ไม่ได้จัดกลุ่มรวมกับ spinMax/spinLift/ฯลฯ เพราะข้อจำกัดเรื่องลำดับการประกาศนี้เท่านั้น
static lv_obj_t* spinTarget = nullptr;

LV_IMG_DECLARE(logo_img)  // firmware/pt-leg-display/logo_img.c

// [TH] ฟอนต์ไทย (Kanit) สำหรับข้อความบนจอ สร้างจาก lv_font_conv ดูวิธีสร้างใน firmware/README.md
// Thai UI text. Regenerate with the command in firmware/README.md - it MUST pass
// --no-compress (lv_conf.h keeps LV_USE_FONT_COMPRESSED 0, and a compressed font
// draws nothing at all) and both the latin and thai fontsource woffs.
LV_FONT_DECLARE(font_th_14)  // firmware/pt-leg-display/font_th_14.c
LV_FONT_DECLARE(font_th_20)  // firmware/pt-leg-display/font_th_20.c
LV_FONT_DECLARE(font_th_28)  // firmware/pt-leg-display/font_th_28.c

// The generated fonts carry no LV_SYMBOL_* glyphs - those live in Montserrat's
// 0xF000 range - so each Thai font falls back to its Montserrat twin and the
// wifi/warning/gear icons keep resolving. The generated structs are const, hence
// in unwritable flash, so fallback is set on RAM copies; only the ~40-byte struct
// is copied, the glyph data stays in flash.
// [TH] ฟอนต์ที่สร้างมาไม่มีไอคอน LV_SYMBOL_* จึงตั้ง fallback ไปที่ Montserrat
//      ตัวโครงสร้างเป็น const อยู่ในแฟลชเขียนไม่ได้ จึงคัดลอกมาไว้ใน RAM ก่อนตั้งค่า
static lv_font_t thai14, thai20, thai28;

static void initThaiFonts() {
  thai14 = font_th_14; thai14.fallback = &lv_font_montserrat_14;
  thai20 = font_th_20; thai20.fallback = &lv_font_montserrat_20;
  thai28 = font_th_28; thai28.fallback = &lv_font_montserrat_28;
}
static void (*cbSettings)() = nullptr;
static void (*cbData)() = nullptr;
static bool uiRunning = false;
// last target reported by the controller — updated from uiUpdateMain.
// Defaulted to 10, matching gLast.targetReps's own default (pt-leg-display.ino),
// so the spinbox does not flash 0 ("unlimited") before the first state message
// ever arrives.
// [TH] ค่าเป้าหมายล่าสุดที่คอนโทรลเลอร์รายงานมา อัปเดตจาก uiUpdateMain
//      ตั้งต้นเป็น 10 ให้ตรงกับค่าเริ่มต้นของ gLast.targetReps เอง (pt-leg-display.ino)
//      เพื่อไม่ให้ช่องปรับค่ากะพริบเป็น 0 ("ไม่จำกัด") ก่อนได้รับข้อความสถานะข้อความแรก
static uint16_t uiTargetShown = 10;
static void (*cbTarget)(uint16_t) = nullptr;

void uiSetCallbacks(void (*onOpenSettings)(), void (*onOpenData)()) {
  cbSettings = onOpenSettings;
  cbData = onOpenData;
}

void uiSetTargetChanged(void (*cb)(uint16_t)) { cbTarget = cb; }

static void (*cbJogExit)() = nullptr;
void uiSetJogExit(void (*cb)()) { cbJogExit = cb; }

static uint32_t uiNowUtc = 0;  // controller clock, 0 = unknown — เวลาของคอนโทรลเลอร์ 0 = ไม่ทราบ
void uiSetClock(uint32_t nowUtc) { uiNowUtc = nowUtc; }

// Local time from a UTC epoch, gmtime on purpose: the offset is already added.
// [TH] เวลาท้องถิ่นจากวินาที UTC ใช้ gmtime โดยตั้งใจ เพราะบวกเวลาไทยไว้แล้ว
static void localTm(uint32_t utc, struct tm* out) {
  time_t t = (time_t)utc + TZ_OFFSET_SEC;
  gmtime_r(&t, out);
}
static void jogExitClicked(lv_event_t*) {
  if (cbJogExit) cbJogExit();
}

static void gearClicked(lv_event_t*) {
  if (uiRunning) return;  // STOP must stay reachable during a session
                          // [TH] ห้ามเข้าหน้าตั้งค่าระหว่างทำงาน เพื่อให้ปุ่ม STOP กดถึงตลอด
  if (cbSettings) cbSettings();
}

// Same gate as the gear: nothing but the live session should be on screen while
// the machine is moving.
// [TH] เงื่อนไขเดียวกับปุ่มเฟือง: ระหว่างเครื่องทำงาน จอต้องแสดงแต่สถานะรอบปัจจุบัน
static void dataClicked(lv_event_t*) {
  if (uiRunning) return;
  if (cbData) cbData();
}

// The controller owns the target: this sends the change and does NOT update the
// label. The label follows the next state message, so what the screen shows is
// always what the machine will actually do — including a change somebody made in
// the phone app a moment ago.
// [TH] คอนโทรลเลอร์เป็นเจ้าของค่าเป้าหมาย ตรงนี้ส่งการเปลี่ยนแปลงออกไปและไม่แก้ป้ายเอง
//      ป้ายจะเปลี่ยนตามข้อความสถานะครั้งถัดไป สิ่งที่จอแสดงจึงเป็นสิ่งที่เครื่องจะทำจริงเสมอ
//      รวมถึงค่าที่มีคนเพิ่งเปลี่ยนจากแอปบนมือถือด้วย
static void targetStep(int delta) {
  int next = (int)uiTargetShown + delta;
  if (next < 0) next = 0;
  if (next > 99) next = 99;
  // The tap is accumulated locally as well as sent. uiTargetShown is otherwise
  // only written by the next state message, which is up to 250 ms away plus the
  // UART round trip — so without this, every tap inside that window computed its
  // next value from the same stale number and three quick presses moved the
  // target by one. The controller still owns the value: the next state message
  // overwrites this with whatever the machine actually took, so a rejected or
  // clamped change corrects itself within a refresh.
  // [TH] เก็บผลการกดไว้ที่นี่ด้วย ไม่ใช่แค่ส่งออกไป
  //      ปกติค่า uiTargetShown จะถูกเขียนโดยข้อความสถานะครั้งถัดไปเท่านั้น
  //      ซึ่งห่างได้ถึง 250 มิลลิวินาทีบวกเวลาไปกลับของ UART
  //      ถ้าไม่ทำแบบนี้ การกดทุกครั้งในช่วงนั้นจะคำนวณค่าถัดไปจากตัวเลขเก่าตัวเดียวกัน
  //      กดเร็วๆ สามครั้งจึงขยับเป้าหมายไปแค่หนึ่ง
  //      ค่าจริงยังเป็นของคอนโทรลเลอร์ ข้อความสถานะครั้งถัดไปจะเขียนทับค่านี้ด้วยค่าที่เครื่องรับไปจริง
  //      การเปลี่ยนที่ถูกปฏิเสธหรือถูกจำกัดช่วงจึงแก้ตัวเองได้ภายในหนึ่งรอบรีเฟรช
  uiTargetShown = (uint16_t)next;
  if (cbTarget) cbTarget((uint16_t)next);
}
static void targetMinusClicked(lv_event_t*) { targetStep(-1); }
static void targetPlusClicked(lv_event_t*) { targetStep(1); }

static void buildMain() {
  // [TH] สร้างหน้าจอหลัก: โลโก้ตอนว่าง, นาฬิกา, จำนวนครั้ง, ชื่อสถานะ (แสดงผลอย่างเดียว) และปุ่มตั้งค่า
  scrMain = lv_obj_create(nullptr);
  styleScreen(scrMain);

  lblStatus = lv_label_create(scrMain);
  lv_obj_set_style_text_font(lblStatus, &thai14, 0);
  lv_obj_set_style_text_color(lblStatus, lv_color_hex(COL_MUTED), 0);
  lv_label_set_text(lblStatus, "");
  lv_obj_align(lblStatus, LV_ALIGN_TOP_LEFT, 8, 10);

  btnGear = makeBtn(scrMain, LV_SYMBOL_SETTINGS, COL_SURFACE,
                    &lv_font_montserrat_20, gearClicked);
  lv_obj_set_size(btnGear, 48, 40);
  lv_obj_align(btnGear, LV_ALIGN_TOP_RIGHT, -8, 4);

  btnData = makeBtn(scrMain, LV_SYMBOL_LIST, COL_SURFACE,
                    &lv_font_montserrat_20, dataClicked);
  lv_obj_set_size(btnData, 48, 40);
  lv_obj_align(btnData, LV_ALIGN_TOP_RIGHT, -60, 4);  // left of the gear — อยู่ซ้ายของปุ่มเฟือง

  // Idle branding. Lives in flash, so it costs no RAM; it is swapped out for the
  // session stats the moment the machine starts moving.
  // No card behind it: the badge carries its own white interior, and only the
  // paper around it is transparent. 176 px wide and 6 px below centre clears the
  // corner buttons above and the name and device code on the bottom line.
  // [TH] ไม่มีพื้นรองด้านหลัง เพราะตราสัญลักษณ์มีพื้นขาวในตัว โปร่งใสเฉพาะรอบนอก
  //      กว้าง 176 พิกเซลและต่ำกว่ากึ่งกลาง 6 พิกเซล จึงไม่ทับปุ่มมุมบน และไม่ทับชื่อกับรหัสเครื่องที่บรรทัดล่าง
  imgLogo = lv_img_create(scrMain);
  lv_img_set_src(imgLogo, &logo_img);
  lv_obj_align(imgLogo, LV_ALIGN_CENTER, 0, 6);

  // Hidden from creation, not just once the first controller state arrives: the
  // boot screen must show the logo alone, with no one-frame flash of these
  // stacked underneath it before uiUpdateMain's first call sorts visibility out.
  // [TH] ซ่อนตั้งแต่สร้าง ไม่ใช่รอสถานะแรกจากคอนโทรลเลอร์ กันไม่ให้จอตอนบูตมีข้อความซ้อนโลโก้แวบเดียว
  lblClock = lv_label_create(scrMain);
  lv_obj_set_style_text_font(lblClock, &lv_font_montserrat_48, 0);
  lv_label_set_text(lblClock, "00:00");
  lv_obj_align(lblClock, LV_ALIGN_TOP_MID, 0, 44);
  lv_obj_add_flag(lblClock, LV_OBJ_FLAG_HIDDEN);

  lblReps = lv_label_create(scrMain);
  lv_obj_set_style_text_font(lblReps, &thai28, 0);
  lv_obj_set_style_text_color(lblReps, lv_color_hex(COL_MUTED), 0);
  lv_label_set_text(lblReps, "0 รอบ");
  lv_obj_align(lblReps, LV_ALIGN_TOP_MID, 0, 100);
  lv_obj_add_flag(lblReps, LV_OBJ_FLAG_HIDDEN);

  // Target stepper, next to the reps label. Reachable at all times, including
  // mid-session — the controller applies a change to the live target
  // immediately, unlike every other setting on this machine.
  // [TH] ปุ่มปรับเป้าหมาย อยู่ข้างป้ายจำนวนครั้ง กดได้ตลอดแม้ระหว่างทำงาน
  //      คอนโทรลเลอร์นำค่าเป้าหมายไปใช้ทันที ต่างจากค่าตั้งค่าอื่นๆ ของเครื่องนี้
  rowTarget = lv_obj_create(scrMain);
  lv_obj_set_size(rowTarget, 170, 36);
  lv_obj_set_style_bg_opa(rowTarget, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(rowTarget, 0, 0);
  lv_obj_set_style_pad_all(rowTarget, 0, 0);
  lv_obj_clear_flag(rowTarget, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_align(rowTarget, LV_ALIGN_TOP_MID, 0, 190);
  lv_obj_add_flag(rowTarget, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t* btnTargetMinus =
      makeBtn(rowTarget, "-", COL_SURFACE, &lv_font_montserrat_20, targetMinusClicked);
  lv_obj_set_size(btnTargetMinus, 38, 36);
  lv_obj_align(btnTargetMinus, LV_ALIGN_LEFT_MID, 0, 0);

  lv_obj_t* lblTargetCap = lv_label_create(rowTarget);
  lv_obj_set_style_text_font(lblTargetCap, &thai14, 0);
  lv_obj_set_style_text_color(lblTargetCap, lv_color_hex(COL_MUTED), 0);
  lv_label_set_text(lblTargetCap, "เป้าหมาย");
  lv_obj_align(lblTargetCap, LV_ALIGN_CENTER, 0, 0);

  lv_obj_t* btnTargetPlus =
      makeBtn(rowTarget, "+", COL_SURFACE, &lv_font_montserrat_20, targetPlusClicked);
  lv_obj_set_size(btnTargetPlus, 38, 36);
  lv_obj_align(btnTargetPlus, LV_ALIGN_RIGHT_MID, 0, 0);

  // Jog-mode exit, in the slot the phase timer leaves empty while jog is shown —
  // high enough to clear the device code at the bottom edge. The controller
  // lowers the leg on the way out, as for the panel exits.
  // [TH] ปุ่มออกจากโหมดขยับเอง วางในตำแหน่งที่ตัวจับเวลาเว้นว่างไว้ตอนแสดงโหมดนี้
  //      สูงพอที่จะไม่ทับรหัสเครื่องที่ขอบล่าง คอนโทรลเลอร์จะลดขาลงตอนออก เหมือนการออกด้วยปุ่มหน้าปัด
  btnJogExit = makeBtn(scrMain, "ออกจากโหมด", COL_SURFACE, &thai20, jogExitClicked);
  lv_obj_set_size(btnJogExit, 170, 40);
  lv_obj_align(btnJogExit, LV_ALIGN_TOP_MID, 0, 166);
  lv_obj_add_flag(btnJogExit, LV_OBJ_FLAG_HIDDEN);

  lblPhase = lv_label_create(scrMain);
  lv_obj_set_style_text_font(lblPhase, &thai20, 0);
  lv_obj_set_style_text_color(lblPhase, lv_color_hex(COL_MUTED), 0);
  lv_label_set_text(lblPhase, "พร้อม");
  lv_obj_align(lblPhase, LV_ALIGN_TOP_MID, 0, 134);
  lv_obj_add_flag(lblPhase, LV_OBJ_FLAG_HIDDEN);

  // In-phase timer: seconds spent in the current HOLD/REST, so a patient can see
  // progress toward the configured hold/rest duration. Blank outside those two
  // phases - LIFTING/LOWERING/READY/etc already read fine from lblPhase alone.
  lblPhaseTimer = lv_label_create(scrMain);
  lv_obj_set_style_text_font(lblPhaseTimer, &thai28, 0);
  lv_obj_set_style_text_color(lblPhaseTimer, lv_color_hex(COL_MUTED), 0);
  lv_label_set_text(lblPhaseTimer, "");
  lv_obj_align(lblPhaseTimer, LV_ALIGN_TOP_MID, 0, 164);
  lv_obj_add_flag(lblPhaseTimer, LV_OBJ_FLAG_HIDDEN);

  lblCode = lv_label_create(scrMain);
  lv_obj_set_style_text_font(lblCode, &thai14, 0);
  lv_obj_set_style_text_color(lblCode, lv_color_hex(COL_MUTED), 0);
  lv_label_set_text(lblCode, "");
  lv_obj_align(lblCode, LV_ALIGN_BOTTOM_RIGHT, -8, -6);

  lblBrand = lv_label_create(scrMain);
  lv_obj_set_style_text_font(lblBrand, &thai14, 0);
  lv_obj_set_style_text_color(lblBrand, lv_color_hex(COL_MUTED), 0);
  lv_label_set_text(lblBrand, "Med-Flex Auto Smart");
  lv_obj_align(lblBrand, LV_ALIGN_BOTTOM_LEFT, 8, -6);

  lv_scr_load(scrMain);
}

void uiSetDeviceCode(const char* code) {
  lv_label_set_text_fmt(lblCode, "รหัส: %s", code);
}

// [TH] แปลงสถานะเป็นข้อความบนจอ
// Thai wording here deliberately avoids stacking a vowel above a tone mark on the
// same consonant: LVGL has no shaping, so two marks at the same height overprint
// (e.g. ครั้ง). "รอบ" is used for reps instead. See firmware/README.md.
static const char* phaseName(Phase p) {
  switch (p) {
    case Phase::Lift: return "กำลังยก";
    case Phase::Hold: return "ค้าง";
    case Phase::Lower: return "กำลังลง";
    case Phase::Rest: return "พัก";
    case Phase::SafeLower: return "กำลังหยุด";
    case Phase::Confirm: return "รอยืนยัน";
    // Not a switch fault: the branch has none to name any more. FAULT now
    // comes only from the actuator's own energize cap or the UART watchdog.
    // [TH] ไม่ใช่สวิตช์ผิดพลาดอีกต่อไป สาขานี้ไม่มีสวิตช์ให้เอ่ยชื่อแล้ว
    //      สถานะผิดพลาดตอนนี้มาจากเพดานเวลาจ่ายไฟของชุดขับเคลื่อนเองหรือตัวเฝ้าระวัง UART เท่านั้น
    case Phase::Fault: return "ระบบขัดข้อง";
    default: return "พร้อม";
  }
}

void uiUpdateMain(bool running, Phase phase, uint32_t elapsedSec, uint16_t reps,
                  uint16_t target, bool wifiUp, bool serverUp, bool linkUp, bool jog) {
  // Repainting identical text still copies the string and invalidates the area,
  // so an idle screen would redraw four times a second for nothing.
  // [TH] วาดข้อความเดิมซ้ำก็ยังกินงาน จึงข้ามการอัปเดตถ้าค่าไม่เปลี่ยน กันจอวาดซ้ำ 4 ครั้ง/วินาทีเปล่าๆ
  static bool first = true;
  static bool pRunning, pWifi, pServer, pLink, pJog;
  static Phase pPhase;
  static uint32_t pElapsed;
  static uint16_t pReps, pTarget;
  static uint32_t phaseTimerStartMs = 0;  // display's own millis() at the last phase edge
  // The status corner shows the time to the minute, so a new minute is a change.
  // [TH] มุมสถานะแสดงเวลาถึงหลักนาที นาทีที่เปลี่ยนจึงนับเป็นการเปลี่ยนแปลง
  static uint32_t pMinute = 0;
  uint32_t minute = uiNowUtc / 60;
  if (!first && running == pRunning && phase == pPhase && elapsedSec == pElapsed &&
      reps == pReps && target == pTarget && wifiUp == pWifi && serverUp == pServer &&
      linkUp == pLink && jog == pJog && minute == pMinute)
    return;
  pJog = jog;
  pMinute = minute;
  // The wire only carries total session elapsed, not per-phase, so the in-phase
  // timer below is clocked locally from the phase edge. The mirrored 250 ms
  // UI/state refresh keeps the drift within a tick.
  // [TH] สายส่งแค่เวลารวมของรอบ ไม่ใช่เวลาต่อสถานะ จึงจับเวลาเองฝั่งจอจากขอบที่สถานะเปลี่ยน
  //      คลาดเคลื่อนไม่เกินหนึ่งรอบรีเฟรช (250 ms) เพราะรีเฟรชจอกับสถานะมาที่ความถี่ใกล้กัน
  if (first || phase != pPhase) phaseTimerStartMs = millis();
  first = false;
  pRunning = running; pPhase = phase; pElapsed = elapsedSec; pReps = reps; pTarget = target;
  pWifi = wifiUp; pServer = serverUp; pLink = linkUp;

  // Both skipped during Confirm: target() reports pendingTargetReps there
  // (session.h), not the stored prescription, and it is stale everywhere
  // outside that phase by the same rule this screen enforces for its own
  // reads (see uiShowConfirm) -- letting either line run here would leak the
  // confirm screen's in-progress edit into uiTargetShown and the settings
  // spinbox. Not observable today (there is no path back to scrMain or
  // scrSettings while scrConfirm is the active screen), but it is the one
  // place that rule was left unenforced.
  // [TH] ข้ามทั้งสองบรรทัดระหว่างสถานะรอยืนยัน เพราะ target() จะรายงาน pendingTargetReps ตอนนั้น
  //      (session.h) ไม่ใช่ค่าที่แพทย์สั่ง และเป็นค่าที่ค้างนอกสถานะนั้นตามกฎเดียวกับที่หน้าจอนี้บังคับใช้
  //      กับการอ่านของตัวเอง (ดู uiShowConfirm) ถ้าปล่อยให้สองบรรทัดนี้ทำงาน จะรั่วค่าที่กำลังแก้อยู่บน
  //      หน้ายืนยันเข้าไปใน uiTargetShown และช่องปรับค่าของหน้าตั้งค่า ยังไม่เห็นผลจริงวันนี้ (ไม่มีเส้นทาง
  //      กลับไป scrMain หรือ scrSettings ระหว่างที่ scrConfirm เป็นหน้าที่กำลังแสดงอยู่) แต่เป็นจุดเดียวที่
  //      กฎนี้ยังไม่ถูกบังคับใช้
  if (phase != Phase::Confirm) {
    uiTargetShown = target;
    // The settings screen's target spinbox is bound to the live target, not only to
    // a settings reply. The controller lets an outstanding live {"t":"target"} win
    // over the target field carried in a settings save; that rule only holds if this
    // spinbox can never be stale. Without this, an operator who dials the target on
    // the main screen and then saves the settings screen would send the spinbox's
    // old value, which the controller silently discards, looking like a broken save.
    // Guarded on spinTarget existing: the settings screen is built lazily on first
    // open, and this runs from boot, long before that. Also skipped while the
    // settings screen itself is on screen — that spinbox is the only place an
    // operator edits the target there, so "open" and "focused on it" are the same
    // thing on this UI. Stomping it mid-edit would undo a tap the operator just
    // made in favour of a value that arrived on the wire a moment later; the sync
    // resumes the instant the screen is left, so the field can never come back
    // stale either.
    // [TH] ผูกช่องปรับจำนวนครั้งเป้าหมายของหน้าตั้งค่าเข้ากับค่าเป้าหมายที่ใช้งานจริง ไม่ใช่แค่ตอนตอบกลับค่าตั้งค่า
    //      คอนโทรลเลอร์ให้ค่า {"t":"target"} ที่เพิ่งส่งชนะค่า target ในการบันทึกตั้งค่า
    //      กฎนี้จะปลอดภัยก็ต่อเมื่อช่องนี้ไม่มีทางเก่าค้าง ถ้าไม่ผูกไว้ ผู้ใช้ที่หมุนเป้าหมายจากหน้าหลัก
    //      แล้วไปกดบันทึกหน้าตั้งค่า จะส่งค่าเก่าของช่องนี้ไป ซึ่งคอนโทรลเลอร์จะทิ้งเงียบๆ ดูเหมือนบันทึกไม่สำเร็จ
    //      กันด้วยเงื่อนไข spinTarget มีอยู่จริง เพราะหน้าตั้งค่าถูกสร้างตอนเปิดครั้งแรกเท่านั้น ทำงานตรงนี้ตั้งแต่ตอนบูตแล้ว
    //      และข้ามการผูกค่าขณะที่หน้าตั้งค่ากำลังเปิดอยู่ด้วย เพราะช่องนี้เป็นจุดเดียวที่ผู้ใช้แก้ค่าเป้าหมายบนหน้านั้น
    //      "เปิดอยู่" กับ "กำลังโฟกัสอยู่ที่ช่องนี้" จึงเป็นเรื่องเดียวกันในหน้าจอนี้ ถ้าไม่กันไว้จะทับค่าที่เพิ่งแตะไป
    //      ด้วยค่าที่เพิ่งมาทางสาย และจะกลับมาผูกค่าให้ทันทีที่ออกจากหน้านี้ ช่องจึงไม่มีทางค้างเก่าเช่นกัน
    if (spinTarget && !settingsScreenOpen()) lv_spinbox_set_value(spinTarget, target);
  }

  uiRunning = running && linkUp;

  // A session can start from a server command while the user is on the settings
  // screen (reachable only when idle, but the state can change underneath them).
  // That screen has no STOP control, so force back to the main screen where STOP
  // is one tap away. Pending edits are discarded on purpose: STOP beats editing.
  if (uiRunning) leaveSubScreenForRun();

  // The other half of leaving the confirm screen. Confirm -> a running phase
  // (a genuine confirmStart()) is already caught by the running branch just
  // above; Confirm -> Idle (a cancel or the 30 s expiry) is not, because
  // Phase::Confirm is deliberately excluded from `running` on the controller
  // (session.h there) and stays excluded all the way to Idle. `!linkUp` is
  // ORed in for the same reason ui.h documents on uiShowConfirm: nothing else
  // ever tells this screen the link went down mid-confirmation -- the
  // controller's own 30 s expiry cannot reach a display that cannot hear it
  // -- so without this, a dead link would strand the operator on a frozen
  // confirm screen with a ตกลง that can never reach the controller. Checked
  // every call rather than only on the edge into/out of Confirm -- this
  // function already returns early above when nothing has changed, so this
  // only ever runs on an actual transition, and it is a no-op once the
  // confirm screen is not the active one. leaveSubScreenForRun() rather than
  // a bare lv_scr_load(scrMain): same helper the running branch above already
  // uses, and it no-ops safely if that branch already left.
  // [TH] อีกครึ่งหนึ่งของการออกจากหน้ายืนยัน รอยืนยัน -> สถานะที่กำลังทำงาน (ยืนยันจริงผ่าน
  //      confirmStart()) ถูกจับโดยเงื่อนไข running ด้านบนอยู่แล้ว ส่วนรอยืนยัน -> ว่าง (ยกเลิกหรือหมดเวลา
  //      30 วินาที) ไม่ถูกจับ เพราะสถานะรอยืนยันถูกกันออกจาก running ของคอนโทรลเลอร์โดยตั้งใจ
  //      (ดู session.h ที่นั่น) และยังคงถูกกันแม้กลับไปว่างแล้วก็ตาม เพิ่มเงื่อนไข !linkUp ด้วยเหตุผลเดียวกับ
  //      ที่ ui.h อธิบายไว้ที่ uiShowConfirm: ไม่มีอะไรอื่นบอกหน้าจอนี้ว่าลิงก์หลุดกลางการยืนยัน
  //      การหมดเวลา 30 วินาทีของคอนโทรลเลอร์เองไปไม่ถึงจอที่ไม่ได้ยินมันอยู่แล้ว ถ้าไม่มีเงื่อนไขนี้
  //      สายที่หลุดจะขังผู้ใช้ไว้บนหน้ายืนยันที่ค้างนิ่ง พร้อมปุ่มตกลงที่ไปไม่ถึงคอนโทรลเลอร์ ตรวจทุกครั้งที่เรียก
  //      ไม่ใช่แค่ตอนขอบเขตเข้า/ออกจากสถานะรอยืนยัน เพราะฟังก์ชันนี้คืนค่าไปแล้วตั้งแต่ต้นถ้าไม่มีอะไรเปลี่ยน
  //      จึงทำงานจริงเฉพาะตอนเปลี่ยนสถานะจริงเท่านั้น และไม่ทำอะไรเลยถ้าหน้ายืนยันไม่ใช่หน้าที่กำลังแสดงอยู่
  //      ใช้ leaveSubScreenForRun() แทน lv_scr_load(scrMain) ตรงๆ เพราะเป็นฟังก์ชันเดียวกับที่เงื่อนไข running
  //      ด้านบนใช้อยู่แล้ว และไม่ทำอะไรซ้ำถ้าเงื่อนไขนั้นออกจากหน้าไปแล้ว
  if ((phase != Phase::Confirm || !linkUp) && confirmScreenActive()) leaveSubScreenForRun();

  lv_label_set_text_fmt(lblClock, "%02lu:%02lu", (unsigned long)(elapsedSec / 60),
                        (unsigned long)(elapsedSec % 60));
  if (target == 0) lv_label_set_text_fmt(lblReps, "%u รอบ", reps);
  else lv_label_set_text_fmt(lblReps, "%u / %u รอบ", reps, target);

  bool fault = (phase == Phase::Fault);

  // Logo whenever the machine is idle. Link state deliberately does NOT gate it:
  // a display with no controller attached is the most common idle case, and
  // hiding the branding there left no state that ever showed it. Link trouble is
  // reported in the status corner instead.
  // Jog only counts while the link is up: a stale flag must not hold the hint on
  // a screen that can no longer hear the controller.
  // [TH] นับโหมดขยับเองเฉพาะตอนลิงก์ใช้งานได้ ค่าเก่าที่ค้างอยู่ต้องไม่ทำให้คำแนะนำค้างบนจอที่ไม่ได้ยินคอนโทรลเลอร์แล้ว
  bool jogShown = jog && linkUp && !running && !fault;
  bool showLogo = !running && !fault && !jogShown;
  if (showLogo) {
    lv_obj_clear_flag(imgLogo, LV_OBJ_FLAG_HIDDEN);
    lv_obj_fade_in(imgLogo, 200, 0);
    lv_obj_clear_flag(lblBrand, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(lblClock, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(lblReps, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(lblPhase, LV_OBJ_FLAG_HIDDEN);  // "พร้อม" adds nothing next to the logo
    lv_obj_add_flag(rowTarget, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(imgLogo, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(lblBrand, LV_OBJ_FLAG_HIDDEN);  // the target row needs that corner — แถวเป้าหมายต้องใช้มุมนั้น
    lv_obj_clear_flag(lblClock, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(lblReps, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(lblPhase, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(rowTarget, LV_OBJ_FLAG_HIDDEN);
  }
  // With the link down the cached state is stale and the buttons do nothing,
  // so say so rather than presenting a frozen timer as if it were live.
  lv_label_set_text(lblPhase, linkUp ? phaseName(phase) : "ไม่พบตัวควบคุม");
  bool showPhaseTimer = linkUp && (phase == Phase::Hold || phase == Phase::Rest);
  if (showPhaseTimer) {
    uint32_t inPhaseSec = (millis() - phaseTimerStartMs) / 1000;
    lv_label_set_text_fmt(lblPhaseTimer, "%lu วิ", (unsigned long)inPhaseSec);
    lv_obj_clear_flag(lblPhaseTimer, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(lblPhaseTimer, LV_OBJ_FLAG_HIDDEN);
  }
  lv_obj_set_style_text_color(lblStatus, lv_color_hex(linkUp ? COL_MUTED : COL_FAULT), 0);
  lv_obj_set_style_text_color(
      lblPhase, lv_color_hex((fault || !linkUp) ? COL_FAULT : COL_MUTED), 0);
  lv_obj_set_style_text_opa(lblClock, linkUp ? LV_OPA_COVER : LV_OPA_50, 0);
  lv_obj_set_style_text_opa(lblReps, linkUp ? LV_OPA_COVER : LV_OPA_50, 0);

  // Jog mode: the clock, target row and phase timer mean nothing here, so the
  // reps line becomes the mode name and the phase line the button hint. Wording
  // keeps clear of vowel-over-tone stacks (no ขึ้น), the same rule as phaseName().
  // [TH] โหมดขยับเอง: นาฬิกา แถวเป้าหมาย และตัวจับเวลาไม่มีความหมาย
  //      บรรทัดจำนวนรอบจึงแสดงชื่อโหมด และบรรทัดสถานะแสดงคำแนะนำปุ่ม
  //      ถ้อยคำเลี่ยงสระบนซ้อนวรรณยุกต์ (ไม่ใช้คำว่า ขึ้น) ตามกฎเดียวกับ phaseName()
  if (jogShown) {
    lv_obj_add_flag(lblClock, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(rowTarget, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(lblPhaseTimer, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(lblReps, "ปรับตำแหน่งขา");
    lv_label_set_text(lblPhase, "ค้าง START ยก / STOP ลด");
    lv_obj_set_style_text_color(lblPhase, lv_color_hex(COL_TEXT), 0);
    lv_obj_clear_flag(btnJogExit, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(btnJogExit, LV_OBJ_FLAG_HIDDEN);
  }

  // While a session runs, STOP must stay one tap away - the settings screen has
  // no stop control, and the controller's UART watchdog will not help because
  // the display keeps pinging from in there. Jog moves the leg too, so the gear
  // stays off for the same reason.
  if (running || jogShown) lv_obj_add_state(btnGear, LV_STATE_DISABLED);
  else lv_obj_clear_state(btnGear, LV_STATE_DISABLED);

  if (linkUp) {
    // Date and time first, because a machine run offline logs whatever this says.
    // The Wi-Fi and server marks appear only when they are up: with no server the
    // radio switches itself off once the clock is set, and a permanent "-- X"
    // there would read as a fault.
    // [TH] แสดงวันเวลาก่อน เพราะเครื่องที่ใช้งานแบบออฟไลน์จะบันทึกตามเวลาที่แสดงตรงนี้
    //      เครื่องหมาย WiFi และเซิร์ฟเวอร์แสดงเฉพาะตอนใช้งานได้ เพราะถ้าไม่มีเซิร์ฟเวอร์ วิทยุจะปิดตัวเองหลังตั้งนาฬิกาเสร็จ
    //      ถ้าแสดง "-- X" ค้างไว้ตลอดจะดูเหมือนเครื่องเสีย
    char clk[40];  // Thai is 3 bytes a character — อักษรไทยใช้ 3 ไบต์ต่อตัว
    if (uiNowUtc) {
      struct tm t;
      localTm(uiNowUtc, &t);
      snprintf(clk, sizeof(clk), "%02d/%02d %02d:%02d", t.tm_mday, t.tm_mon + 1, t.tm_hour,
               t.tm_min);
    } else {
      snprintf(clk, sizeof(clk), "%s", "ไม่ทราบเวลา");
    }
    lv_label_set_text_fmt(lblStatus, "%s%s%s", clk, wifiUp ? "  " LV_SYMBOL_WIFI : "",
                          serverUp ? " " LV_SYMBOL_OK : "");
  } else {
    lv_label_set_text(lblStatus, LV_SYMBOL_WARNING " ไม่พบตัวควบคุม");
  }
}

// ------------------------------------------------------------ settings screen

// The dropdown's first row is a prompt, not a network. It is compared by string
// in the selection handler, so it must be defined once - two literals drifting
// apart would silently let "pick a network" be typed into the SSID field.
// [TH] แถวแรกของดรอปดาวน์เป็นข้อความชวนให้เลือก ไม่ใช่ชื่อเครือข่าย
//      ตัวจัดการเปรียบเทียบด้วยสตริง จึงต้องนิยามไว้ที่เดียว
#define SCAN_PLACEHOLDER "(เลือกเครือข่าย)"

static lv_obj_t *scrSettings, *taSsid, *taPass, *taServer, *taCode, *kb, *ddScan, *colSettings,
    *swSound;
static lv_obj_t *spinMax, *spinLift, *spinLower, *spinHold, *spinRest, *btnSave, *btnReset;
static void (*cbSave)(const DeviceSettings&) = nullptr;
static bool settingsKnown = false;  // has the controller ever sent its settings?

void uiSetSettingsSaved(void (*onSave)(const DeviceSettings&)) { cbSave = onSave; }

// RESET is gated with SAVE, not left free. It reaches the card and the
// controller through the same path a typed change does, so before the
// controller has said what it actually holds, one confirmed tap writes blank
// defaults over a card carrying a working configuration — and a readable card
// beats controller NVS at the next boot, so both copies end up blank. A machine
// whose link is merely down is exactly when someone reaches for it.
// [TH] ปุ่มคืนค่าถูกกั้นพร้อมปุ่มบันทึก ไม่ปล่อยให้กดได้อิสระ
//      เพราะมันเขียนถึงการ์ดและคอนโทรลเลอร์ด้วยเส้นทางเดียวกับการแก้ค่าด้วยมือ
//      ถ้ากดก่อนที่คอนโทรลเลอร์จะบอกค่าจริง ค่าตั้งต้นที่ว่างเปล่าจะทับค่าที่ใช้งานได้บนการ์ด
//      และรอบบูตถัดไปการ์ดจะชนะ ทำให้ค่าหายทั้งสองที่ ซึ่งมักเกิดตอนสายเชื่อมต่อหลุดพอดี
static void applySaveEnabled() {
  if (btnSave) {
    if (settingsKnown) {
      lv_obj_clear_state(btnSave, LV_STATE_DISABLED);
      lv_obj_set_style_bg_color(btnSave, lv_color_hex(COL_BG), 0);
    } else {
      lv_obj_add_state(btnSave, LV_STATE_DISABLED);
      lv_obj_set_style_bg_color(btnSave, lv_color_hex(COL_MUTED), LV_STATE_DISABLED);
    }
  }
  if (btnReset) {
    if (settingsKnown) {
      lv_obj_clear_state(btnReset, LV_STATE_DISABLED);
      lv_obj_set_style_bg_color(btnReset, lv_color_hex(COL_SURFACE), 0);
    } else {
      lv_obj_add_state(btnReset, LV_STATE_DISABLED);
      lv_obj_set_style_bg_color(btnReset, lv_color_hex(COL_MUTED), LV_STATE_DISABLED);
    }
  }
}

void uiSetSettingsKnown(bool known) {
  settingsKnown = known;
  applySaveEnabled();
}

static lv_obj_t* lblSdStatus = nullptr;  // filled in by buildSettings

// Read by saveClicked: a save that reached the card leaves the screen the way it
// always has, a save that did not stays put so the red line is actually read.
// Reporting into a screen being unloaded in the same frame shows nobody anything.
// [TH] saveClicked อ่านค่านี้: ถ้าเขียนการ์ดได้ให้กลับหน้าหลักเหมือนเดิม ถ้าไม่ได้ให้ค้างหน้าเดิม
//      ผู้ใช้จะได้อ่านข้อความสีแดงทัน การเขียนข้อความลงหน้าจอที่กำลังถูกปิดในเฟรมเดียวกันไม่มีใครเห็น
static bool lastSaveToCard = true;
static bool lastSaveCardPresent = false;

void uiReportSettingsSaved(bool toCard, bool cardPresent) {
  lastSaveToCard = toCard;
  lastSaveCardPresent = cardPresent;
  if (!lblSdStatus) return;
  lv_label_set_text(lblSdStatus, toCard          ? "SD: บันทึกแล้ว"
                                 : cardPresent   ? "SD: เขียนการ์ดไม่ได้"
                                                 : "SD: ไม่มีการ์ด");
  lv_obj_set_style_text_color(lblSdStatus, lv_color_hex(toCard ? COL_MUTED : COL_FAULT), 0);
}

// Held rather than written straight to the label, because the caller reports the
// card state before the screen exists: the settings screen is built on its first
// open, so the very first report would land on a null label and be lost.
// [TH] เก็บค่าไว้ก่อน ไม่เขียนลงป้ายทันที เพราะผู้เรียกรายงานสถานะการ์ดตั้งแต่ก่อนสร้างหน้าจอ
//      หน้าตั้งค่าถูกสร้างตอนเปิดครั้งแรก ถ้าเขียนทันทีค่ารอบแรกจะหายไปกับป้ายที่ยังไม่มี
static bool sdCardPresent = false, sdFileLoaded = false;

static void applySdStatus() {
  if (!lblSdStatus) return;
  const char* text = !sdCardPresent  ? "SD: ไม่มีการ์ด"
                     : sdFileLoaded  ? "SD: ใช้ค่าจากการ์ด"
                                     : "SD: การ์ดยังไม่มีไฟล์";
  lv_label_set_text(lblSdStatus, text);
  lv_obj_set_style_text_color(lblSdStatus, lv_color_hex(COL_MUTED), 0);
}

void uiSetSdStatus(bool cardPresent, bool fileLoaded) {
  sdCardPresent = cardPresent;
  sdFileLoaded = fileLoaded;
  applySdStatus();
}

// Reset goes through the ordinary save path on purpose: the defaults must reach
// the card and the controller exactly the way a typed change does, and writing a
// defaults file beats deleting one — the file always exists and is always the
// thing being read.
// [TH] การคืนค่าใช้เส้นทางเดียวกับการบันทึกปกติ เพื่อให้ค่าตั้งต้นไปถึงทั้งการ์ดและคอนโทรลเลอร์
//      เหมือนการแก้ค่าด้วยมือ และเขียนไฟล์ค่าตั้งต้นดีกว่าลบไฟล์ทิ้ง เพราะไฟล์จะมีอยู่เสมอ
static void resetConfirmed(lv_event_t*) {
  // Belt and braces, matching saveClicked: the button is disabled in this state,
  // but defaults must never reach the card before the controller's real values
  // are known.
  // [TH] กันพลาดสองชั้นเหมือนปุ่มบันทึก: ปุ่มถูกปิดอยู่แล้ว
  //      แต่ค่าตั้งต้นต้องไม่ถูกเขียนลงการ์ดก่อนที่จะรู้ค่าจริงจากคอนโทรลเลอร์
  if (!settingsKnown) { lv_scr_load(scrSettings); return; }
  DeviceSettings d;
  settingsFileDefaults(d);
  lv_textarea_set_text(taSsid, d.wifiSsid.c_str());
  lv_textarea_set_text(taPass, d.wifiPass.c_str());
  lv_textarea_set_text(taServer, d.serverUrl.c_str());
  lv_textarea_set_text(taCode, d.deviceCode.c_str());
  lv_spinbox_set_value(spinMax, d.maxTravelSec);
  lv_spinbox_set_value(spinLift, d.liftSec);
  lv_spinbox_set_value(spinHold, d.holdSec);
  lv_spinbox_set_value(spinLower, d.lowerSec);
  lv_spinbox_set_value(spinRest, d.restSec);
  lv_spinbox_set_value(spinTarget, d.targetReps);
  if (cbSave) cbSave(d);
  lv_scr_load(scrSettings);
}

// Built once and reused, like every other screen. A dialog created per open
// leaks ~9 KB of the 48 KB LVGL pool each time and freezes the device silently
// on about the fifth open — this codebase has already shipped that bug once.
// [TH] สร้างครั้งเดียวแล้วใช้ซ้ำเหมือนหน้าจออื่น ถ้าสร้างใหม่ทุกครั้งที่เปิด
//      จะกินหน่วยความจำ LVGL ครั้งละราว 9 KB จาก 48 KB และค้างตายเงียบๆ ราวครั้งที่ห้า
static lv_obj_t* scrConfirmReset = nullptr;

static void buildConfirmReset() {
  scrConfirmReset = lv_obj_create(nullptr);
  styleScreen(scrConfirmReset);

  lv_obj_t* col = lv_obj_create(scrConfirmReset);
  lv_obj_set_size(col, lv_pct(100), lv_pct(100));
  lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(col, 0, 0);
  lv_obj_set_style_pad_row(col, 10, 0);
  lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_text_color(col, lv_color_hex(COL_TEXT), 0);

  lv_obj_t* q = lv_label_create(col);
  lv_obj_set_style_text_font(q, &thai20, 0);
  lv_label_set_text(q, "คืนค่าเริ่มต้นทั้งหมด");

  lv_obj_t* warn = lv_label_create(col);
  lv_obj_set_style_text_font(warn, &thai14, 0);
  lv_obj_set_style_text_color(warn, lv_color_hex(COL_FAULT), 0);
  lv_label_set_text(warn, "รหัสผ่าน WiFi จะถูกลบ");

  lv_obj_t* row = lv_obj_create(col);
  lv_obj_set_size(row, 300, 48);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(row, 12, 0);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* cancel = makeBtn(row, "ยกเลิก", COL_SURFACE, &thai20,
                             [](lv_event_t*) { lv_scr_load(scrSettings); });
  lv_obj_set_size(cancel, 120, 40);

  lv_obj_t* ok = makeBtn(row, "คืนค่า", COL_FAULT, &thai20, resetConfirmed);
  lv_obj_set_size(ok, 120, 40);
}

static void hideKeyboard() {
  if (!kb) return;
  lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
  // Full height and the bottom anchor back, so all the fields are on screen
  // whenever the keys are not. Same shape as hideAddKeyboard.
  // [TH] คืนความสูงเต็มและการจัดวางชิดล่างให้กล่องช่องกรอก ช่องต่างๆ จะได้เห็นครบตอนไม่มีแป้นพิมพ์บัง
  //      รูปแบบเดียวกับ hideAddKeyboard
  if (colSettings) {
    lv_obj_set_height(colSettings, 196);
    lv_obj_align(colSettings, LV_ALIGN_BOTTOM_MID, 0, 0);
  }
  lv_keyboard_set_textarea(kb, nullptr);
}

// Called from uiUpdateMain when a session is running. A session can begin from a
// panel button or a server command while someone is on the settings, patient or
// add-patient screen; none of those show what the machine is doing, so the
// running session takes the screen back. Pending edits are discarded on purpose.
// [TH] เรียกเมื่อเครื่องเริ่มทำงาน: รอบอาจเริ่มจากปุ่มหน้าปัดหรือคำสั่งจากเซิร์ฟเวอร์
//      ขณะที่ผู้ใช้อยู่หน้าตั้งค่า/ทะเบียนคนไข้ ซึ่งไม่แสดงสถานะเครื่อง จึงต้องดึงกลับหน้าหลัก
//      ค่าที่กำลังกรอกค้างไว้จะถูกทิ้ง โดยตั้งใจ
static void leaveSubScreenForRun() {
  lv_obj_t* act = lv_scr_act();
  if (act == scrMain) return;
  hideKeyboard();
  hideAddKeyboard();
  lv_scr_load(scrMain);
}

// Used by uiUpdateMain to keep the live target sync from stomping an in-progress
// edit on the settings screen — see the comment at that call site.
// [TH] ใช้โดย uiUpdateMain เพื่อไม่ให้การผูกค่าเป้าหมายสดทับการแก้ไขที่ค้างอยู่บนหน้าตั้งค่า
//      ดูคำอธิบายที่จุดเรียกนั้น
static bool settingsScreenOpen() { return lv_scr_act() == scrSettings; }

static void taFocused(lv_event_t* e) {
  lv_obj_t* ta = (lv_obj_t*)lv_event_get_target(e);
  lv_keyboard_set_textarea(kb, ta);
  thaiKbSetScript(kb, false);  // these fields are ASCII — ช่องเหล่านี้ใช้อักษรอังกฤษ

  // The field column is shrunk to the strip above the keyboard, exactly as the
  // add form does it. scroll_to_view alone does nothing here: LVGL still counts a
  // field as "in view" when the keyboard is drawn on top of it, so every one of
  // these fields was typed into blind — the SD-status row added at the top of the
  // column pushed them further under the keys still. The column is bottom-
  // anchored, unlike the add form's, so it has to be re-anchored as well or
  // shrinking just slides it behind the keys.
  // [TH] ย่อความสูงกล่องช่องกรอกให้เหลือเฉพาะส่วนเหนือแป้นพิมพ์ แบบเดียวกับฟอร์มลงทะเบียน
  //      ถ้าใช้ scroll_to_view อย่างเดียวจะไม่เกิดอะไรขึ้น เพราะ LVGL ยังนับว่าช่องนั้นอยู่ในจอ
  //      ทั้งที่แป้นพิมพ์วาดทับอยู่ ทุกช่องในหน้านี้จึงต้องพิมพ์แบบมองไม่เห็น
  //      และแถวสถานะการ์ด SD ที่เพิ่มไว้บนสุดยังดันช่องต่างๆ ให้ลงไปอยู่ใต้แป้นพิมพ์มากขึ้นอีก
  //      กล่องนี้จัดวางชิดล่าง ต่างจากของฟอร์มลงทะเบียน จึงต้องย้ายจุดยึดด้วย
  //      ไม่อย่างนั้นการย่อจะแค่เลื่อนมันไปอยู่หลังแป้นพิมพ์
  if (colSettings) {
    lv_obj_set_height(colSettings, 76);
    lv_obj_align(colSettings, LV_ALIGN_TOP_MID, 0, 44);
  }
  lv_obj_clear_flag(kb, LV_OBJ_FLAG_HIDDEN);
  lv_obj_scroll_to_view(ta, LV_ANIM_ON);  // keep the focused field above the keys
}

// Tapping anywhere that is not a text field dismisses the keyboard. Attached to
// the containers rather than the screen: the top bar and the field column cover
// the screen completely, so the screen itself never receives a click. Taps on a
// textarea do not reach here - they land on the textarea, which has no bubbling.
// [TH] แตะที่ว่างนอกช่องกรอกข้อความเพื่อปิดแป้นพิมพ์
static void tapHidesKeyboard(lv_event_t*) { hideKeyboard(); }

// Scrolling dismisses it too. Both a finger drag and the lv_obj_scroll_to_view()
// that taFocused() runs immediately after showing the keyboard emit
// LV_EVENT_SCROLL_BEGIN, so without this guard the keyboard would vanish the
// instant it appeared. Only a real drag sets the input device's scroll object.
// [TH] เลื่อนหน้าจอก็ปิดแป้นพิมพ์ แต่ต้องแยกจากการเลื่อนอัตโนมัติตอนเพิ่งเปิดแป้นพิมพ์
//      ทั้งสองแบบส่ง LV_EVENT_SCROLL_BEGIN เหมือนกัน ต่างกันที่การลากนิ้วเท่านั้นที่ตั้ง scroll object
static void scrollHidesKeyboard(lv_event_t*) {
  if (lv_indev_get_scroll_obj(lv_indev_get_act()) == nullptr) return;
  hideKeyboard();
}

// The keyboard's own OK / X keys fire these; without handling them the keyboard
// covers the bottom half of the screen forever and the screen cannot be left.
static void kbDone(lv_event_t*) { hideKeyboard(); }

static void saveClicked(lv_event_t*) {
  // Belt and braces: the button is disabled in this state, but never push a
  // screen full of placeholders over the controller's real NVS settings.
  // [TH] กันพลาดสองชั้น: ปุ่มถูกปิดอยู่แล้ว แต่ห้ามส่งค่าตัวอย่างไปเขียนทับค่าจริงใน NVS เด็ดขาด
  if (!settingsKnown) return;
  DeviceSettings s;
  s.wifiSsid = lv_textarea_get_text(taSsid);
  s.wifiPass = lv_textarea_get_text(taPass);
  s.serverUrl = lv_textarea_get_text(taServer);
  s.deviceCode = lv_textarea_get_text(taCode);
  s.maxTravelSec = (uint16_t)lv_spinbox_get_value(spinMax);
  s.liftSec = (uint16_t)lv_spinbox_get_value(spinLift);
  s.holdSec = (uint16_t)lv_spinbox_get_value(spinHold);
  s.lowerSec = (uint16_t)lv_spinbox_get_value(spinLower);
  s.restSec = (uint16_t)lv_spinbox_get_value(spinRest);
  s.targetReps = (uint16_t)lv_spinbox_get_value(spinTarget);
  // Mandatory, not optional. `DeviceSettings s;` above default-constructs its
  // Strings but leaves every scalar indeterminate — the assignments in this
  // block are the only reason the others hold real values. A soundOn left
  // unassigned here is garbage, not false, and it would go straight into gLast,
  // into /settings.json and into audioSetEnabled.
  // [TH] บรรทัดนี้จำเป็น ไม่ใช่ทางเลือก การประกาศ DeviceSettings s; ด้านบนสร้างเฉพาะส่วน String
  //      ส่วนตัวเลขและบูลีนยังไม่มีค่าที่แน่นอน การกำหนดค่าในบล็อกนี้คือเหตุผลเดียวที่ฟิลด์อื่นมีค่าจริง
  //      ถ้าไม่กำหนด soundOn ตรงนี้ ค่าที่ได้คือค่าขยะ ไม่ใช่ค่าเท็จ
  //      และจะถูกส่งต่อไปยัง gLast ไปยัง /settings.json และไปยัง audioSetEnabled ทันที
  s.soundOn = lv_obj_has_state(swSound, LV_STATE_CHECKED);
  if (cbSave) cbSave(s);
  hideKeyboard();
  // Stay only when a card was in the slot and refused the write — that is a
  // failure worth reading. Running with no card is a supported configuration, so
  // it returns to the main screen the way a normal save does.
  // [TH] ค้างหน้าจอไว้เฉพาะตอนที่มีการ์ดอยู่แต่เขียนไม่ได้ ซึ่งเป็นความผิดพลาดที่ต้องให้อ่าน
  //      ส่วนการใช้งานโดยไม่มีการ์ดถือเป็นเรื่องปกติ ให้กลับหน้าหลักเหมือนการบันทึกทั่วไป
  if (!lastSaveToCard && lastSaveCardPresent) return;
  lv_scr_load(scrMain);
}

static void backClicked(lv_event_t*) {
  hideKeyboard();
  lv_scr_load(scrMain);  // discard edits
}

// Scanning blocks for ~2 s, so it is a button rather than something that runs
// every time the screen opens.
static void scanClicked(lv_event_t*) {
  // [TH] สแกนหาเครือข่าย WiFi (บล็อกราว 2 วินาที) แล้วใส่ชื่อลงในรายการดรอปดาวน์
  int n = WiFi.scanNetworks();
  String opts = (n > 0) ? SCAN_PLACEHOLDER : "(ไม่พบเครือข่าย - ลองใหม่)";
  for (int i = 0; i < n && i < 12; i++) opts += String("\n") + WiFi.SSID(i);
  lv_dropdown_set_options(ddScan, opts.c_str());
  lv_obj_clear_flag(ddScan, LV_OBJ_FLAG_HIDDEN);
  // The controller owns networking; the display only borrows the radio to read
  // SSIDs. Leaving STA up here costs RAM and power for nothing.
  WiFi.scanDelete();
  WiFi.mode(WIFI_OFF);
}

static lv_obj_t* makeField(lv_obj_t* parent, const char* label, const char* placeholder,
                           bool password, lv_obj_t** out) {
  lv_obj_t* l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, &thai14, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(COL_MUTED), 0);
  lv_label_set_text(l, label);

  // Thai font, even though these fields are meant for Latin. The keyboard on this
  // screen can reach Thai through its globe key, and a textarea left on Montserrat
  // draws every Thai character as an empty box — the text is stored correctly and
  // looks broken, which is the worst of both. Latin still renders through this
  // font's Montserrat fallback.
  // [TH] ใช้ฟอนต์ไทย แม้ช่องเหล่านี้ตั้งใจให้กรอกอักษรอังกฤษ
  //      เพราะแป้นพิมพ์หน้านี้กดปุ่มลูกโลกสลับเป็นภาษาไทยได้ ถ้าใช้ฟอนต์เดิมอักษรไทยจะกลายเป็นสี่เหลี่ยมว่าง
  //      ข้อความถูกเก็บถูกต้องแต่ดูเหมือนเสีย ซึ่งแย่ที่สุด ส่วนอักษรอังกฤษยังแสดงได้ผ่านฟอนต์สำรอง
  lv_obj_t* ta = lv_textarea_create(parent);
  lv_textarea_set_one_line(ta, true);
  lv_textarea_set_placeholder_text(ta, placeholder);
  lv_textarea_set_password_mode(ta, password);
  lv_obj_set_width(ta, lv_pct(100));
  lv_obj_set_style_text_font(ta, &thai14, 0);
  lv_obj_set_style_bg_color(ta, lv_color_hex(COL_SURFACE), 0);
  lv_obj_set_style_text_color(ta, lv_color_hex(COL_TEXT), 0);
  lv_obj_set_style_border_width(ta, 0, 0);
  lv_obj_add_event_cb(ta, taFocused, LV_EVENT_FOCUSED, nullptr);
  *out = ta;
  return ta;
}

static void spinDown(lv_event_t* e) {
  lv_spinbox_decrement((lv_obj_t*)lv_event_get_user_data(e));
}
static void spinUp(lv_event_t* e) {
  lv_spinbox_increment((lv_obj_t*)lv_event_get_user_data(e));
}

// A bare lv_spinbox cannot be changed by touch at all - LVGL only moves its
// value through lv_spinbox_increment/decrement, so it needs its own -/+ keys.
// One row per setting keeps the touch targets big enough to hit on a resistive
// panel.
// A settings row: full width, fixed height, muted label on the left, and the
// caller's control on the right. Shared so the geometry and the tap-bubbling
// contract below are stated once — the spinbox and switch rows have to line up,
// and a change made to only one of two copies would break that silently.
// [TH] แถวของหน้าตั้งค่า กว้างเต็ม สูงคงที่ ป้ายสีจางอยู่ซ้าย ส่วนตัวควบคุมของผู้เรียกอยู่ขวา
//      แยกออกมาใช้ร่วมกันเพื่อให้ขนาดและกติกาการส่งต่อการแตะด้านล่างถูกเขียนไว้ที่เดียว
//      แถวช่องตัวเลขกับแถวสวิตช์ต้องเรียงตรงกัน การแก้เพียงสำเนาเดียวจากสองชุดจะทำให้เพี้ยนแบบเงียบๆ
static lv_obj_t* makeRow(lv_obj_t* parent, const char* label) {
  lv_obj_t* row = lv_obj_create(parent);
  lv_obj_set_size(row, lv_pct(100), 42);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_set_style_pad_all(row, 0, 0);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  // Let taps on the row's own background reach the column's dismiss handler.
  // Controls added by the caller are children without this flag, so their clicks
  // stay theirs.
  // [TH] ให้การแตะพื้นหลังของแถวไปถึงตัวจัดการปิดแป้นพิมพ์ของคอลัมน์
  //      ตัวควบคุมที่ผู้เรียกเพิ่มเข้ามาเป็นลูกที่ไม่มีแฟล็กนี้ การแตะจึงยังเป็นของมันเอง
  lv_obj_add_flag(row, LV_OBJ_FLAG_EVENT_BUBBLE);

  lv_obj_t* l = lv_label_create(row);
  lv_obj_set_style_text_font(l, &thai14, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(COL_MUTED), 0);
  lv_label_set_text(l, label);
  lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);
  return row;
}

static lv_obj_t* makeSpinRow(lv_obj_t* parent, const char* label, int val,
                             int lo, int hi) {
  lv_obj_t* row = makeRow(parent, label);

  lv_obj_t* sp = lv_spinbox_create(row);
  lv_spinbox_set_range(sp, lo, hi);
  lv_spinbox_set_digit_format(sp, 2, 0);
  lv_spinbox_set_value(sp, val);
  lv_obj_set_size(sp, 46, 34);
  lv_obj_set_style_bg_color(sp, lv_color_hex(COL_SURFACE), 0);
  lv_obj_set_style_text_color(sp, lv_color_hex(COL_TEXT), 0);
  lv_obj_set_style_border_width(sp, 0, 0);
  lv_obj_set_style_pad_all(sp, 2, 0);
  lv_obj_align(sp, LV_ALIGN_RIGHT_MID, -44, 0);

  lv_obj_t* minus = makeBtn(row, "-", COL_SURFACE, &lv_font_montserrat_20, nullptr);
  lv_obj_set_size(minus, 38, 34);
  lv_obj_align(minus, LV_ALIGN_RIGHT_MID, -92, 0);
  lv_obj_add_event_cb(minus, spinDown, LV_EVENT_CLICKED, sp);

  lv_obj_t* plus = makeBtn(row, "+", COL_SURFACE, &lv_font_montserrat_20, nullptr);
  lv_obj_set_size(plus, 38, 34);
  lv_obj_align(plus, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_add_event_cb(plus, spinUp, LV_EVENT_CLICKED, sp);
  return sp;
}

// Same row as the spinboxes, with a switch on the right instead of a spinbox and
// its two buttons.
// [TH] ใช้แถวเดียวกับช่องตัวเลข ต่างกันตรงที่ด้านขวาเป็นสวิตช์ ไม่ใช่ช่องตัวเลขกับปุ่มสองปุ่ม
static lv_obj_t* makeSwitchRow(lv_obj_t* parent, const char* label, bool on) {
  lv_obj_t* row = makeRow(parent, label);

  lv_obj_t* sw = lv_switch_create(row);
  lv_obj_set_size(sw, 50, 28);
  lv_obj_align(sw, LV_ALIGN_RIGHT_MID, 0, 0);
  if (on) lv_obj_add_state(sw, LV_STATE_CHECKED);
  return sw;
}

// Built exactly once. Rebuilding it per open leaked ~8.9 KB of LVGL's 48 KB
// pool every time (screens are not freed when another is loaded), and the fifth
// open hit LV_ASSERT_MALLOC, whose handler is `while(1)` - a silent hard freeze
// with a dead touchscreen and no reboot.
// [TH] สร้างเพียงครั้งเดียว ถ้าสร้างใหม่ทุกครั้งที่เปิดจะรั่ว ~8.9 KB จากพูล 48 KB ของ LVGL
//      (หน้าจอเก่าไม่ถูกคืนหน่วยความจำ) พอเปิดครั้งที่ห้าจะไปติด LV_ASSERT_MALLOC = ค้างตายเงียบๆ
static void buildSettings() {
  scrSettings = lv_obj_create(nullptr);
  styleScreen(scrSettings);

  // top bar stays put; only the field list scrolls
  lv_obj_t* bar = lv_obj_create(scrSettings);
  lv_obj_set_size(bar, lv_pct(100), 44);
  lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_bg_color(bar, lv_color_hex(COL_SURFACE), 0);
  lv_obj_set_style_border_width(bar, 0, 0);
  lv_obj_set_style_radius(bar, 0, 0);
  lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(bar, tapHidesKeyboard, LV_EVENT_CLICKED, nullptr);

  lv_obj_t* back = makeBtn(bar, LV_SYMBOL_LEFT " กลับ", COL_BG,
                           &thai20, backClicked);
  lv_obj_set_size(back, 110, 34);
  lv_obj_align(back, LV_ALIGN_LEFT_MID, -8, 0);

  btnSave = makeBtn(bar, "บันทึก", COL_BG, &thai20, saveClicked);
  lv_obj_set_size(btnSave, 90, 34);
  lv_obj_align(btnSave, LV_ALIGN_RIGHT_MID, 8, 0);
  applySaveEnabled();


  lv_obj_t* col = colSettings = lv_obj_create(scrSettings);
  lv_obj_set_size(col, lv_pct(100), 196);
  lv_obj_align(col, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(col, 8, 0);
  lv_obj_set_style_pad_row(col, 4, 0);
  lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(col, 0, 0);
  lv_obj_add_event_cb(col, tapHidesKeyboard, LV_EVENT_CLICKED, nullptr);
  lv_obj_add_event_cb(col, scrollHidesKeyboard, LV_EVENT_SCROLL_BEGIN, nullptr);

  // First row of the field list, not the top bar: กลับ (110 px) and บันทึก (90 px)
  // leave about 120 px between them on a 320 px screen, and this text is wider
  // than that — it overlapped the buttons. The list is scrolled to the top every
  // time the screen opens, so the line is on show anyway.
  // [TH] วางเป็นแถวแรกของรายการ ไม่ใช่บนแถบด้านบน เพราะปุ่มกลับกับบันทึกกินที่จนเหลือกลางจอ
  //      ราว 120 พิกเซล ซึ่งแคบกว่าข้อความนี้ จึงเคยทับปุ่ม และรายการถูกเลื่อนขึ้นบนสุดทุกครั้งที่เปิดอยู่แล้ว
  lblSdStatus = lv_label_create(col);
  lv_obj_set_width(lblSdStatus, lv_pct(100));
  lv_obj_set_style_text_font(lblSdStatus, &thai14, 0);
  lv_obj_set_style_text_color(lblSdStatus, lv_color_hex(COL_MUTED), 0);
  lv_label_set_text(lblSdStatus, "");

  lv_obj_t* scan = makeBtn(col, LV_SYMBOL_WIFI " ค้นหาเครือข่าย", COL_SURFACE,
                           &thai20, scanClicked);
  lv_obj_set_size(scan, lv_pct(100), 38);

  ddScan = lv_dropdown_create(col);
  lv_obj_set_width(ddScan, lv_pct(100));
  lv_dropdown_set_options(ddScan, SCAN_PLACEHOLDER);
  lv_obj_set_style_text_font(ddScan, &thai14, 0);
  lv_obj_set_style_bg_color(ddScan, lv_color_hex(COL_SURFACE), 0);
  lv_obj_set_style_text_color(ddScan, lv_color_hex(COL_TEXT), 0);
  lv_obj_set_style_border_width(ddScan, 0, 0);
  lv_obj_add_flag(ddScan, LV_OBJ_FLAG_HIDDEN);
  // Its option list drops down over the same space the keyboard occupies.
  lv_obj_add_event_cb(ddScan, tapHidesKeyboard, LV_EVENT_CLICKED, nullptr);
  lv_obj_add_event_cb(
      ddScan,
      [](lv_event_t* e) {
        // Row 0 is never a network: it is either the prompt or the "no networks
        // found" line, and LVGL sends VALUE_CHANGED on every release over an
        // option whether or not the selection moved. Matching the prompt string
        // let the error line through, so tapping it typed "(ไม่พบเครือข่าย -
        // ลองใหม่)" into the SSID field — truncated mid-codepoint at 64 bytes —
        // and SAVE then wrote that to NVS and to the card, leaving a machine that
        // never joins WiFi again until someone retypes the name by hand.
        // [TH] แถวที่ 0 ไม่ใช่ชื่อเครือข่ายเสมอ เป็นข้อความชวนให้เลือกหรือบรรทัด "ไม่พบเครือข่าย"
        //      และ LVGL ส่ง VALUE_CHANGED ทุกครั้งที่ปล่อยนิ้วบนตัวเลือก ไม่ว่าค่าจะเปลี่ยนหรือไม่
        //      การเทียบกับข้อความชวนให้เลือกจึงปล่อยบรรทัดแจ้งข้อผิดพลาดผ่านไปได้
        //      แตะแล้วข้อความนั้นจะถูกใส่ลงช่อง SSID โดยถูกตัดกลางตัวอักษรที่ 64 ไบต์
        //      พอกดบันทึกก็จะถูกเขียนลง NVS และลงการ์ด เครื่องจะต่อ WiFi ไม่ได้อีกจนกว่าจะพิมพ์ชื่อใหม่เอง
        lv_obj_t* dd = (lv_obj_t*)lv_event_get_target(e);
        if (lv_dropdown_get_selected(dd) == 0) return;
        char sel[64];
        lv_dropdown_get_selected_str(dd, sel, sizeof(sel));
        lv_textarea_set_text(taSsid, sel);
      },
      LV_EVENT_VALUE_CHANGED, nullptr);

  makeField(col, "เครือข่าย WiFi", "SSID", false, &taSsid);
  makeField(col, "รหัสผ่าน WiFi", "password", true, &taPass);
  makeField(col, "เซิร์ฟเวอร์", "http://192.168.0.12:8000", false, &taServer);
  makeField(col, "รหัสอุปกรณ์", "KNEE-01", false, &taCode);

  // MAX TRAVEL is the safety cap the whole switch-timeout story rests on: it is
  // how long the motor may keep driving with no end-of-travel switch closing.
  // Its ceiling is deliberately far lower than HOLD/REST.
  // [TH] MAX TRAVEL คือเพดานความปลอดภัยหลัก: เวลาที่ขับมอเตอร์ได้โดยไม่มีสวิตช์ปลายทางปิด
  //      เพดานของมันตั้งไว้ต่ำกว่า HOLD/REST มากโดยตั้งใจ
  spinMax = makeSpinRow(col, "เวลาเดินทางสูงสุด (วิ)", 8, 1, 20);
  spinLift = makeSpinRow(col, "ยกขึ้น (วิ)", 7, 1, 20);
  spinHold = makeSpinRow(col, "ค้าง (วิ)", 10, 1, 60);
  spinLower = makeSpinRow(col, "ลดลง (วิ)", 7, 1, 20);
  spinRest = makeSpinRow(col, "พัก (วิ)", 10, 1, 60);
  spinTarget = makeSpinRow(col, "จำนวนครั้ง (0=ไม่จำกัด)", 10, 0, 99);
  // "เสียง" and not "ปิดเสียง": positive sense matches the stored field, and the
  // word has no vowel stacked over a tone mark, which this LVGL build cannot
  // shape. Built here, with the rest of the screen — see the note above: this
  // function runs exactly once, and creating widgets per open is what leaked the
  // LVGL pool and froze the board on the fifth open.
  // [TH] ใช้คำว่า "เสียง" ไม่ใช่ "ปิดเสียง" เพราะความหมายเชิงบวกตรงกับค่าที่เก็บไว้
  //      และคำนี้ไม่มีสระซ้อนกับวรรณยุกต์ ซึ่ง LVGL รุ่นนี้จัดวางให้ไม่ได้
  //      สร้างไว้ตรงนี้พร้อมกับส่วนอื่นของหน้าจอ ตามหมายเหตุด้านบน ฟังก์ชันนี้ทำงานครั้งเดียวเท่านั้น
  //      การสร้างวิดเจ็ตใหม่ทุกครั้งที่เปิดคือสาเหตุที่หน่วยความจำ LVGL รั่วและเครื่องค้างตอนเปิดครั้งที่ห้า
  swSound = makeSwitchRow(col, "เสียง", true);

  // The keyboard comes down before the dialog goes up. Both ways out of the
  // dialog return with lv_scr_load(scrSettings) rather than uiOpenSettings, so
  // nothing on the way back resets keyboard state — whatever this screen was left
  // holding, including the shrunk field column, would still be there.
  // [TH] ปิดแป้นพิมพ์ก่อนเปิดกล่องยืนยัน ทั้งสองทางออกของกล่องนั้นกลับมาด้วย lv_scr_load(scrSettings)
  //      ไม่ได้ผ่าน uiOpenSettings จึงไม่มีอะไรคืนสถานะแป้นพิมพ์ให้ตอนกลับมา
  //      สิ่งที่ค้างไว้บนหน้านี้จะยังอยู่ รวมถึงกล่องช่องกรอกที่ถูกย่อไว้ด้วย
  // Opens the date/time screen. Sits with the settings rather than on the main
  // screen: it is set once per power-on at most, not touched during a session.
  // [TH] เปิดหน้าตั้งวันเวลา วางไว้กับค่าตั้งค่าแทนที่จะอยู่หน้าหลัก เพราะตั้งอย่างมากครั้งเดียวต่อการเปิดเครื่อง
  lv_obj_t* btnTime = makeBtn(col, LV_SYMBOL_BELL " วันและเวลา", COL_SURFACE, &thai20,
                              [](lv_event_t*) {
                                hideKeyboard();
                                openTimeScreen();
                              });
  lv_obj_set_size(btnTime, lv_pct(100), 40);

  btnReset = makeBtn(col, "คืนค่าเริ่มต้น", COL_SURFACE, &thai20,
                     [](lv_event_t*) {
                       hideKeyboard();
                       if (!scrConfirmReset) buildConfirmReset();
                       lv_scr_load(scrConfirmReset);
                     });
  lv_obj_set_size(btnReset, lv_pct(100), 40);
  applySaveEnabled();  // the reset button exists only now — ปุ่มคืนค่าเพิ่งถูกสร้างตรงนี้

  lv_obj_t* lblCredit = lv_label_create(col);
  lv_obj_set_style_text_color(lblCredit, lv_color_hex(COL_MUTED), 0);
  lv_label_set_text(lblCredit, "By Leo ;3");
  lv_obj_set_style_pad_top(lblCredit, 10, 0);

  kb = lv_keyboard_create(scrSettings);
  lv_obj_set_size(kb, lv_pct(100), 120);
  lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_add_event_cb(kb, kbDone, LV_EVENT_READY, nullptr);   // OK key
  lv_obj_add_event_cb(kb, kbDone, LV_EVENT_CANCEL, nullptr);  // X key
  lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);

  // Latin on open: this screen's fields are ASCII by protocol. deviceCode is
  // matched byte-for-byte by the server and serverUrl has to resolve, so a Thai
  // character in either means the machine silently never pairs. Thai stays one
  // tap away rather than being where a finger lands first.
  // [TH] เปิดมาเป็นภาษาอังกฤษ เพราะช่องในหน้านี้ต้องเป็นอักษรอังกฤษตามข้อกำหนด
  //      รหัสอุปกรณ์ถูกเทียบแบบตรงตัวที่เซิร์ฟเวอร์ และที่อยู่เซิร์ฟเวอร์ต้องแปลงเป็นที่อยู่จริงได้
  //      ถ้ามีอักษรไทยปนจะจับคู่ไม่ได้โดยไม่มีข้อความเตือน ภาษาไทยจึงอยู่ถัดไปหนึ่งปุ่ม
  lv_obj_set_style_text_font(kb, &thai20, 0);
  thaiKbApply(kb, false);
}

void uiOpenSettings(const DeviceSettings& cur) {
  if (!scrSettings) buildSettings();

  lv_textarea_set_text(taSsid, cur.wifiSsid.c_str());
  lv_textarea_set_text(taPass, cur.wifiPass.c_str());
  lv_textarea_set_text(taServer, cur.serverUrl.c_str());
  lv_textarea_set_text(taCode, cur.deviceCode.c_str());
  lv_spinbox_set_value(spinMax, cur.maxTravelSec);
  lv_spinbox_set_value(spinLift, cur.liftSec);
  lv_spinbox_set_value(spinHold, cur.holdSec);
  lv_spinbox_set_value(spinLower, cur.lowerSec);
  lv_spinbox_set_value(spinRest, cur.restSec);
  // Value set here, widget created in buildSettings(). Creating it here instead
  // is the LVGL pool leak this screen already learned about once.
  // [TH] ตั้งค่าตรงนี้ แต่สร้างวิดเจ็ตใน buildSettings()
  //      ถ้าสร้างตรงนี้แทน จะเกิดหน่วยความจำ LVGL รั่วแบบที่หน้าจอนี้เคยเจอมาแล้ว
  if (cur.soundOn) lv_obj_add_state(swSound, LV_STATE_CHECKED);
  else lv_obj_clear_state(swSound, LV_STATE_CHECKED);
  // Seeded from uiTargetShown, NOT cur.targetReps. cur is gLast, which only
  // moves on a "settings" reply (boot/hello) — it does not see a live
  // {"t":"target"} round trip, so it can be behind by however long ago the
  // operator last dialed the target on the main screen. uiTargetShown is
  // updated unconditionally from every state message (see uiUpdateMain), so
  // it is never behind the controller's live value. Using gLast here would
  // silently reopen the exact bug the live-sync binding above exists to
  // close: SAVE would carry the stale number.
  // [TH] ตั้งต้นจาก uiTargetShown ไม่ใช่ cur.targetReps เพราะ cur คือ gLast ซึ่งขยับเฉพาะตอนได้รับ
  //      คำตอบ "settings" (ตอนบูต/hello) เท่านั้น ไม่เห็นการเปลี่ยนค่าเป้าหมายสดแบบ {"t":"target"}
  //      จึงอาจค้างอยู่นานเท่าที่ผู้ใช้เพิ่งหมุนค่าจากหน้าหลักมา ส่วน uiTargetShown อัปเดตจากทุกข้อความสถานะ
  //      โดยไม่มีเงื่อนไข (ดู uiUpdateMain) จึงไม่มีทางตามหลังค่าจริงของคอนโทรลเลอร์
  //      ถ้าใช้ gLast ตรงนี้จะเปิดช่องให้บั๊กเดิมที่การผูกค่าสดด้านบนตั้งใจปิดกลับมาอีก คือ SAVE จะส่งค่าที่ค้างไป
  lv_spinbox_set_value(spinTarget, uiTargetShown);

  applySdStatus();  // the label exists only now, on the first open — ป้ายเพิ่งมีตัวตนตอนนี้
  thaiKbSetScript(kb, false);  // same reason as the add form — ด้วยเหตุผลเดียวกับฟอร์มลงทะเบียน
  hideKeyboard();
  lv_obj_add_flag(ddScan, LV_OBJ_FLAG_HIDDEN);  // stale scan results
  lv_obj_scroll_to_y(lv_obj_get_parent(taSsid), 0, LV_ANIM_OFF);

  lv_scr_load(scrSettings);
}

// ----------------------------------------------------------- date/time screen

// For a machine with no internet: whoever is at the panel types the date and
// time once, and every session after that is logged with a real timestamp.
// Without the DS3231 the controller forgets it at power-off, which the hint on
// the screen says. Wording avoids vowel-over-tone stacks (no ตั้ง, no ชั่วโมง).
// [TH] สำหรับเครื่องที่ไม่มีอินเทอร์เน็ต ผู้ใช้ตั้งวันเวลาครั้งเดียว ทุกรอบหลังจากนั้นจะบันทึกเวลาจริง
//      ถ้าไม่มีชิป DS3231 คอนโทรลเลอร์จะลืมเวลาเมื่อไฟดับ ซึ่งข้อความบนจอบอกไว้
//      ถ้อยคำเลี่ยงสระบนซ้อนวรรณยุกต์ (ไม่ใช้ ตั้ง และ ชั่วโมง)
static void (*cbTimeSet)(uint32_t) = nullptr;
static lv_obj_t *scrTime, *spDay, *spMonth, *spYear, *spHour, *spMin;

void uiSetTimeSet(void (*cb)(uint32_t)) { cbTimeSet = cb; }

static int daysInMonth(int month, int year) {
  static const uint8_t DAYS[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
  return (month == 2 && leap) ? 29 : DAYS[month - 1];
}

// Days since 1970-01-01 for a civil date (Howard Hinnant's algorithm), so the
// conversion does not depend on whatever TZ the C library thinks it is in.
// [TH] จำนวนวันนับจาก 1970-01-01 สำหรับวันที่ปฏิทิน ไม่ต้องพึ่งเขตเวลาที่ไลบรารี C เข้าใจ
static int32_t daysFromCivil(int y, int m, int d) {
  y -= m <= 2;
  int32_t era = (y >= 0 ? y : y - 399) / 400;
  int32_t yoe = y - era * 400;
  int32_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  int32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

static void timeSaveClicked(lv_event_t*) {
  int year = 2000 + lv_spinbox_get_value(spYear);
  int month = lv_spinbox_get_value(spMonth);
  int day = lv_spinbox_get_value(spDay);
  // Clamped, not rolled over: 31 in a 30-day month means the last day, never the
  // 1st of the next month.
  // [TH] บีบให้อยู่ในช่วง ไม่ใช่ปัดไปเดือนถัดไป วันที่ 31 ในเดือนที่มี 30 วันจึงหมายถึงวันสุดท้าย
  int dim = daysInMonth(month, year);
  if (day > dim) day = dim;
  int64_t local = (int64_t)daysFromCivil(year, month, day) * 86400 +
                  lv_spinbox_get_value(spHour) * 3600 + lv_spinbox_get_value(spMin) * 60;
  uint32_t utc = (uint32_t)(local - TZ_OFFSET_SEC);
  if (cbTimeSet) cbTimeSet(utc);
  // Shown at once rather than after the controller echoes it, so the corner does
  // not flash the old time; the next state frame confirms or corrects it.
  // [TH] แสดงทันทีโดยไม่รอคอนโทรลเลอร์ตอบกลับ มุมจอจะได้ไม่วาบเวลาเก่า ข้อความสถานะถัดไปจะยืนยันหรือแก้ให้เอง
  uiNowUtc = utc;
  lv_scr_load(scrSettings);
}

static void buildTime() {
  scrTime = lv_obj_create(nullptr);
  styleScreen(scrTime);

  lv_obj_t* bar = lv_obj_create(scrTime);
  lv_obj_set_size(bar, lv_pct(100), 44);
  lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_bg_color(bar, lv_color_hex(COL_SURFACE), 0);
  lv_obj_set_style_border_width(bar, 0, 0);
  lv_obj_set_style_radius(bar, 0, 0);
  lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* back = makeBtn(bar, LV_SYMBOL_LEFT " กลับ", COL_BG, &thai20,
                           [](lv_event_t*) { lv_scr_load(scrSettings); });
  lv_obj_set_size(back, 110, 34);
  lv_obj_align(back, LV_ALIGN_LEFT_MID, -8, 0);

  lv_obj_t* save = makeBtn(bar, "บันทึก", COL_BG, &thai20, timeSaveClicked);
  lv_obj_set_size(save, 90, 34);
  lv_obj_align(save, LV_ALIGN_RIGHT_MID, 8, 0);

  lv_obj_t* col = lv_obj_create(scrTime);
  lv_obj_set_size(col, lv_pct(100), 196);
  lv_obj_align(col, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(col, 8, 0);
  lv_obj_set_style_pad_row(col, 4, 0);
  lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(col, 0, 0);

  spDay = makeSpinRow(col, "วัน", 1, 1, 31);
  spMonth = makeSpinRow(col, "เดือน", 1, 1, 12);
  spYear = makeSpinRow(col, "ปี ค.ศ. 20__", 26, 25, 99);  // two digits: the row's box holds two
  spHour = makeSpinRow(col, "ชม. (0-23)", 9, 0, 23);
  spMin = makeSpinRow(col, "นาที", 0, 0, 59);

  lv_obj_t* hint = lv_label_create(col);
  lv_obj_set_width(hint, lv_pct(100));
  lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_font(hint, &thai14, 0);
  lv_obj_set_style_text_color(hint, lv_color_hex(COL_MUTED), 0);
  lv_label_set_text(hint, "ถ้าไม่มีชิปนาฬิกา DS3231 ไฟดับแล้วเวลาจะหาย ต่อ WiFi ได้ก็จะใช้เวลาจากอินเทอร์เน็ตแทน");
}

static void openTimeScreen() {
  if (!scrTime) buildTime();
  // Seeded from the controller's clock when it has one, so a small correction is
  // a tap or two rather than five fields from scratch.
  // [TH] ตั้งค่าเริ่มต้นจากนาฬิกาของคอนโทรลเลอร์ถ้ามี การแก้นิดหน่อยจึงแตะแค่หนึ่งสองครั้ง ไม่ต้องตั้งใหม่ทั้งห้าช่อง
  if (uiNowUtc) {
    struct tm t;
    localTm(uiNowUtc, &t);
    lv_spinbox_set_value(spDay, t.tm_mday);
    lv_spinbox_set_value(spMonth, t.tm_mon + 1);
    lv_spinbox_set_value(spYear, (t.tm_year + 1900) % 100);
    lv_spinbox_set_value(spHour, t.tm_hour);
    lv_spinbox_set_value(spMin, t.tm_min);
  }
  lv_scr_load(scrTime);
}

// ---------------------------------------------------------------- data screen

// Roster access is injected, so this file never includes SD or touches the card.
// [TH] ฟังก์ชันเข้าถึงทะเบียนถูกส่งเข้ามา ไฟล์นี้จึงไม่ต้องรู้จักการ์ด SD เลย
static int (*cbCount)() = nullptr;
static const Patient* (*cbGet)(int) = nullptr;
static bool (*cbAdd)(const char*, const char*, int) = nullptr;
static void (*cbSetActive)(int) = nullptr;

static lv_obj_t *scrData, *lblDataTitle, *lstPatients;
static lv_obj_t *scrAdd, *taPCode, *taPName, *taPAge, *kbAdd, *colAdd;

void uiSetDataCallbacks(int (*count)(), const Patient* (*get)(int),
                        bool (*add)(const char*, const char*, int),
                        void (*setActive)(int)) {
  cbCount = count; cbGet = get; cbAdd = add; cbSetActive = setActive;
}

static void openAddForm();

static void patientRowClicked(lv_event_t* e) {
  int idx = (int)(intptr_t)lv_event_get_user_data(e);
  if (cbSetActive) cbSetActive(idx);
  lv_scr_load(scrMain);  // picked; the next session is theirs — เลือกแล้ว รอบต่อไปเป็นของคนนี้
}

// Rebuilt on every open, unlike the screen itself: the roster changes when a
// patient is added, and lv_obj_clean frees the old rows properly.
// [TH] สร้างรายการใหม่ทุกครั้งที่เปิด เพราะทะเบียนเปลี่ยนได้ ส่วน lv_obj_clean คืนหน่วยความจำแถวเก่าให้เรียบร้อย
static void fillPatientList() {
  lv_obj_clean(lstPatients);
  int n = cbCount ? cbCount() : 0;

  // -1 is "no card", which is not the same as "no patients registered" and must
  // not look like it: one is a missing card, the other is an empty roster.
  // [TH] ค่า -1 คือไม่มีการ์ด ซึ่งต่างจาก "ยังไม่มีคนไข้" ต้องไม่แสดงให้ดูเหมือนกัน
  if (n < 0) {
    lv_label_set_text(lblDataTitle, "ไม่พบการ์ด SD");
    return;
  }
  lv_label_set_text(lblDataTitle, n > 0 ? "คนไข้" : "ยังไม่มีคนไข้");

  for (int i = 0; i < n; i++) {
    const Patient* p = cbGet(i);
    lv_obj_t* b = lv_list_add_btn(lstPatients, LV_SYMBOL_RIGHT, "");
    // Set the font on the button, not its label: text_font is an inherited
    // style, and the label is created inside lv_list_add_btn.
    // [TH] ตั้งฟอนต์ที่ตัวปุ่ม ไม่ใช่ที่ป้ายข้อความ เพราะสไตล์ฟอนต์สืบทอดลงไปหาลูกอยู่แล้ว
    lv_obj_set_style_text_font(b, &thai14, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(COL_SURFACE), 0);
    lv_obj_set_style_text_color(b, lv_color_hex(COL_TEXT), 0);
    lv_obj_set_style_border_width(b, 0, 0);
    lv_obj_t* lbl = lv_obj_get_child(b, -1);  // the text label of the list button
    if (p->name.length())
      lv_label_set_text_fmt(lbl, "%s  (%s)", p->name.c_str(), p->code.c_str());
    else
      lv_label_set_text(lbl, p->code.c_str());
    lv_obj_add_event_cb(b, patientRowClicked, LV_EVENT_CLICKED, (void*)(intptr_t)i);
  }
}

// Built once, like the settings screen and for the same reason: LVGL does not
// free a screen when another is loaded, so rebuilding per open leaks the pool.
// [TH] สร้างครั้งเดียวด้วยเหตุผลเดียวกับหน้าตั้งค่า: LVGL ไม่คืนหน่วยความจำหน้าจอเก่า
static void buildData() {
  scrData = lv_obj_create(nullptr);
  styleScreen(scrData);

  lv_obj_t* bar = lv_obj_create(scrData);
  lv_obj_set_size(bar, lv_pct(100), 44);
  lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_bg_color(bar, lv_color_hex(COL_SURFACE), 0);
  lv_obj_set_style_border_width(bar, 0, 0);
  lv_obj_set_style_radius(bar, 0, 0);
  lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* back = makeBtn(bar, LV_SYMBOL_LEFT " กลับ", COL_BG, &thai20,
                           [](lv_event_t*) { lv_scr_load(scrMain); });
  lv_obj_set_size(back, 110, 34);
  lv_obj_align(back, LV_ALIGN_LEFT_MID, -8, 0);

  // "ลงทะเบียน" not "เพิ่มผู้ป่วย": เพิ่ม stacks a vowel and a tone on the same
  // consonant, which LVGL overprints (see firmware/README.md).
  // [TH] ใช้คำว่า ลงทะเบียน เพราะคำว่า เพิ่ม มีสระกับวรรณยุกต์ซ้อนกัน จะพิมพ์ทับกันบนจอ
  lv_obj_t* add = makeBtn(bar, "ลงทะเบียน", COL_BG, &thai20,
                          [](lv_event_t*) { openAddForm(); });
  lv_obj_set_size(add, 120, 34);
  lv_obj_align(add, LV_ALIGN_RIGHT_MID, 8, 0);

  lblDataTitle = lv_label_create(scrData);
  lv_obj_set_style_text_font(lblDataTitle, &thai14, 0);
  lv_obj_set_style_text_color(lblDataTitle, lv_color_hex(COL_MUTED), 0);
  lv_label_set_text(lblDataTitle, "");
  lv_obj_align(lblDataTitle, LV_ALIGN_TOP_LEFT, 10, 50);

  lv_obj_t* hist = makeBtn(scrData, LV_SYMBOL_FILE " ประวัติ", COL_SURFACE, &thai14,
                           [](lv_event_t*) { openHistory(); });
  lv_obj_set_size(hist, 100, 26);
  lv_obj_align(hist, LV_ALIGN_TOP_RIGHT, -6, 46);

  lstPatients = lv_list_create(scrData);
  lv_obj_set_size(lstPatients, lv_pct(100), 168);
  lv_obj_align(lstPatients, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_color(lstPatients, lv_color_hex(COL_BG), 0);
  lv_obj_set_style_border_width(lstPatients, 0, 0);
  lv_obj_set_style_pad_all(lstPatients, 4, 0);
}

void uiOpenData() {
  if (!scrData) buildData();
  fillPatientList();
  lv_scr_load(scrData);
}

// ------------------------------------------------------------- history screen

// The newest sessions on the card, so a machine used without the app can still
// show what was done. Twenty rows: about two screens of scrolling, and a fixed
// cost in LVGL's pool however long the card has been in use.
// [TH] รอบล่าสุดบนการ์ด เครื่องที่ใช้โดยไม่มีแอปจึงยังดูได้ว่าทำอะไรไปแล้ว
//      ยี่สิบแถวคือเลื่อนราวสองหน้าจอ และกินพูลของ LVGL เท่าเดิมไม่ว่าการ์ดจะใช้มานานเท่าไร
static const int HIST_MAX = 20;
static int (*cbHistory)(HistRow*, int) = nullptr;
static lv_obj_t *scrHistory, *lblHistTitle, *lstHistory;

void uiSetHistorySource(int (*read)(HistRow*, int)) { cbHistory = read; }

static void buildHistory() {
  scrHistory = lv_obj_create(nullptr);
  styleScreen(scrHistory);

  lv_obj_t* bar = lv_obj_create(scrHistory);
  lv_obj_set_size(bar, lv_pct(100), 44);
  lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_bg_color(bar, lv_color_hex(COL_SURFACE), 0);
  lv_obj_set_style_border_width(bar, 0, 0);
  lv_obj_set_style_radius(bar, 0, 0);
  lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* back = makeBtn(bar, LV_SYMBOL_LEFT " กลับ", COL_BG, &thai20,
                           [](lv_event_t*) { lv_scr_load(scrData); });
  lv_obj_set_size(back, 110, 34);
  lv_obj_align(back, LV_ALIGN_LEFT_MID, -8, 0);

  lblHistTitle = lv_label_create(bar);
  lv_obj_set_style_text_font(lblHistTitle, &thai14, 0);
  lv_obj_set_style_text_color(lblHistTitle, lv_color_hex(COL_MUTED), 0);
  lv_label_set_text(lblHistTitle, "");
  lv_obj_align(lblHistTitle, LV_ALIGN_RIGHT_MID, 0, 0);

  lstHistory = lv_list_create(scrHistory);
  lv_obj_set_size(lstHistory, lv_pct(100), 196);
  lv_obj_align(lstHistory, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_color(lstHistory, lv_color_hex(COL_BG), 0);
  lv_obj_set_style_border_width(lstHistory, 0, 0);
  lv_obj_set_style_pad_all(lstHistory, 4, 0);
}

// Rows rebuilt per open, like the patient list: lv_obj_clean frees the old ones.
// The rows live on the heap only while this runs, not for the life of the boot.
// [TH] สร้างแถวใหม่ทุกครั้งที่เปิดเหมือนรายการคนไข้ lv_obj_clean คืนหน่วยความจำแถวเก่า
//      ข้อมูลแถวอยู่ในหน่วยความจำเฉพาะตอนที่ฟังก์ชันนี้ทำงาน ไม่ได้ค้างไว้ตลอดการเปิดเครื่อง
static void fillHistory() {
  lv_obj_clean(lstHistory);
  HistRow* rows = new HistRow[HIST_MAX];
  int n = cbHistory ? cbHistory(rows, HIST_MAX) : -1;
  if (n < 0) {
    lv_label_set_text(lblHistTitle, "ไม่พบการ์ด SD");
  } else if (n == 0) {
    lv_label_set_text(lblHistTitle, "ยังไม่มีประวัติ");
  } else {
    lv_label_set_text_fmt(lblHistTitle, "ล่าสุด %d รอบ", n);
  }
  for (int i = 0; i < n; i++) {
    const HistRow& r = rows[i];
    lv_obj_t* b = lv_list_add_btn(lstHistory, nullptr, "");
    lv_obj_set_style_text_font(b, &thai14, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(COL_SURFACE), 0);
    lv_obj_set_style_text_color(b, lv_color_hex(r.fault ? COL_FAULT : COL_TEXT), 0);
    lv_obj_set_style_border_width(b, 0, 0);
    lv_obj_clear_flag(b, LV_OBJ_FLAG_CLICKABLE);  // a record, not a control — เป็นข้อมูล ไม่ใช่ปุ่ม
    lv_obj_t* lbl = lv_obj_get_child(b, -1);
    lv_label_set_text_fmt(lbl, "%s  %s\n%u รอบ  %lu:%02lu%s", r.when.c_str(), r.who.c_str(),
                          (unsigned)r.reps, (unsigned long)(r.durationSec / 60),
                          (unsigned long)(r.durationSec % 60), r.fault ? "  ขัดข้อง" : "");
  }
  delete[] rows;
}

static void openHistory() {
  if (!scrHistory) buildHistory();
  fillHistory();
  lv_obj_scroll_to_y(lstHistory, 0, LV_ANIM_OFF);
  lv_scr_load(scrHistory);
}

// ------------------------------------------------------------- add-patient form

// The CSV has no quoting rules, so the two characters that would break a
// positional split are dropped at the point of entry rather than escaped.
// [TH] ไฟล์ CSV ไม่มีกฎการใส่เครื่องหมายคำพูด จึงตัดอักขระที่ทำให้แยกคอลัมน์พังทิ้งตั้งแต่ตอนกรอก
static String sanitize(const char* s) {
  String out;
  for (const char* p = s; *p; ++p)
    if (*p != ',' && *p != '\n' && *p != '\r') out += *p;
  return out;
}

static void hideAddKeyboard() {
  if (!kbAdd) return;
  lv_obj_add_flag(kbAdd, LV_OBJ_FLAG_HIDDEN);
  // The column gets its full height back, so all three fields are on screen
  // whenever the keys are not.
  // [TH] คืนความสูงเต็มให้กล่องช่องกรอก ทั้งสามช่องจะได้เห็นครบตอนที่ไม่มีแป้นพิมพ์บัง
  if (colAdd) lv_obj_set_height(colAdd, 196);
  lv_keyboard_set_textarea(kbAdd, nullptr);
}

static void addFieldFocused(lv_event_t* e) {
  lv_obj_t* ta = (lv_obj_t*)lv_event_get_target(e);
  lv_keyboard_set_textarea(kbAdd, ta);

  // Thai for the name, Latin for the other two. Forcing Thai on all three made
  // the age box unusable: it accepts only 0-9, so LVGL dropped every Thai key
  // press and the keyboard looked alive while typing nothing. The patient code
  // is Latin by convention for the same reason.
  // [TH] ช่องชื่อใช้ภาษาไทย อีกสองช่องใช้ภาษาอังกฤษ ถ้าบังคับเป็นไทยทั้งสามช่อง ช่องอายุจะใช้ไม่ได้เลย
  //      เพราะรับเฉพาะตัวเลข 0-9 อักษรไทยที่กดจะถูกทิ้งทั้งหมด แป้นดูเหมือนทำงานแต่พิมพ์อะไรไม่ออก
  //      ส่วนรหัสคนไข้ก็ใช้อักษรอังกฤษตามที่ใช้กันมา
  thaiKbSetScript(kbAdd, ta == taPName);

  // Shrink the field column to the strip above the keyboard before scrolling the
  // focused field into it, so the box being typed into is never underneath the
  // keys that the same tap just raised.
  // [TH] ย่อความสูงของกล่องช่องกรอกให้เหลือเฉพาะส่วนเหนือแป้นพิมพ์ก่อน แล้วค่อยเลื่อนช่องที่โฟกัสเข้ามา
  //      ช่องที่กำลังพิมพ์จะได้ไม่ไปอยู่ใต้แป้นพิมพ์ที่เพิ่งถูกเปิดจากการแตะครั้งเดียวกัน
  lv_obj_set_height(colAdd, 76);
  lv_obj_clear_flag(kbAdd, LV_OBJ_FLAG_HIDDEN);
  lv_obj_scroll_to_view(ta, LV_ANIM_ON);
}

static void addSaveClicked(lv_event_t*) {
  String code = sanitize(lv_textarea_get_text(taPCode));
  String name = sanitize(lv_textarea_get_text(taPName));
  int age = atoi(lv_textarea_get_text(taPAge));
  // A row with no code cannot be identified later, in the CSV or on screen.
  // [TH] ถ้าไม่มีรหัส จะอ้างอิงแถวนี้ทีหลังไม่ได้ทั้งในไฟล์และบนจอ
  if (code.length() == 0) return;
  bool ok = cbAdd ? cbAdd(code.c_str(), name.c_str(), age) : false;
  hideAddKeyboard();
  uiOpenData();  // back to the list, now including the new row
  // A card pulled after boot fails the write. Returning to a list that quietly
  // lacks the row reads as "saved"; the title says otherwise instead.
  // [TH] ถ้าการ์ดถูกดึงออกหลังเปิดเครื่อง การเขียนจะล้มเหลว การกลับมาที่รายการเฉยๆ
  //      ทำให้เข้าใจว่าบันทึกแล้ว จึงบอกด้วยข้อความหัวเรื่องแทน
  if (!ok) lv_label_set_text(lblDataTitle, "บันทึกไม่สำเร็จ");
}

static lv_obj_t* makeAddField(lv_obj_t* parent, const char* placeholder,
                              const char* accepted, lv_obj_t** out) {
  lv_obj_t* ta = lv_textarea_create(parent);
  lv_textarea_set_one_line(ta, true);
  lv_textarea_set_placeholder_text(ta, placeholder);
  if (accepted) lv_textarea_set_accepted_chars(ta, accepted);
  lv_obj_set_width(ta, lv_pct(100));
  lv_obj_set_style_text_font(ta, &thai14, 0);  // placeholders are Thai; typed text is Latin
  lv_obj_set_style_bg_color(ta, lv_color_hex(COL_SURFACE), 0);
  lv_obj_set_style_text_color(ta, lv_color_hex(COL_TEXT), 0);
  lv_obj_set_style_border_width(ta, 0, 0);
  // CLICKED as well as FOCUSED: FOCUSED fires only when focus moves, so tapping
  // the field that already had focus would otherwise leave the keyboard on
  // whatever script the last field wanted.
  // [TH] ผูกทั้ง CLICKED และ FOCUSED เพราะ FOCUSED เกิดเฉพาะตอนที่โฟกัสย้ายที่
  //      ถ้าแตะช่องที่โฟกัสอยู่แล้ว แป้นพิมพ์จะค้างเป็นภาษาของช่องก่อนหน้า
  lv_obj_add_event_cb(ta, addFieldFocused, LV_EVENT_FOCUSED, nullptr);
  lv_obj_add_event_cb(ta, addFieldFocused, LV_EVENT_CLICKED, nullptr);
  *out = ta;
  return ta;
}

static void buildAddForm() {
  scrAdd = lv_obj_create(nullptr);
  styleScreen(scrAdd);

  lv_obj_t* bar = lv_obj_create(scrAdd);
  lv_obj_set_size(bar, lv_pct(100), 44);
  lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_bg_color(bar, lv_color_hex(COL_SURFACE), 0);
  lv_obj_set_style_border_width(bar, 0, 0);
  lv_obj_set_style_radius(bar, 0, 0);
  lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* back = makeBtn(bar, LV_SYMBOL_LEFT " กลับ", COL_BG, &thai20,
                           [](lv_event_t*) { hideAddKeyboard(); uiOpenData(); });
  lv_obj_set_size(back, 110, 34);
  lv_obj_align(back, LV_ALIGN_LEFT_MID, -8, 0);

  lv_obj_t* save = makeBtn(bar, "บันทึก", COL_BG, &thai20, addSaveClicked);
  lv_obj_set_size(save, 90, 34);
  lv_obj_align(save, LV_ALIGN_RIGHT_MID, 8, 0);

  lv_obj_t* col = colAdd = lv_obj_create(scrAdd);
  // 80 px held two of the three fields: the age box started at y=120 and fell
  // outside the container, so it could only be reached by scrolling a column
  // that gives no sign it scrolls. The screen below the top bar is 196 px, and
  // the keyboard overlaps the lower half only while it is showing.
  // [TH] ความสูง 80 พิกเซลใส่ได้แค่สองช่องจากสามช่อง ช่องอายุเริ่มที่ y=120 ซึ่งเลยขอบกล่องออกไป
  //      ต้องเลื่อนถึงจะเห็น ทั้งที่กล่องไม่มีอะไรบอกว่าเลื่อนได้ พื้นที่ใต้แถบด้านบนมี 196 พิกเซล
  //      ส่วนแป้นพิมพ์จะบังครึ่งล่างเฉพาะตอนที่เปิดอยู่เท่านั้น
  lv_obj_set_size(col, lv_pct(100), 196);
  lv_obj_align(col, LV_ALIGN_TOP_MID, 0, 44);
  lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(col, 8, 0);
  lv_obj_set_style_pad_row(col, 4, 0);
  lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(col, 0, 0);

  // The on-screen keyboard is Latin-only, so names typed here are Latin. Thai
  // The name field takes Thai from the on-screen keyboard; the code and age
  // fields stay Latin, and the age field accepts digits only.
  // [TH] ช่องชื่อพิมพ์ภาษาไทยจากแป้นพิมพ์บนจอได้ ส่วนช่องรหัสและอายุใช้อักษรอังกฤษ
  //      และช่องอายุรับเฉพาะตัวเลข
  // Tapping the form outside a field puts the keyboard away, the way the
  // settings screen already does. Without it the keyboard covers from y=120 and
  // the third field ends up behind it, so the age box cannot be reached once the
  // name has been typed — there was no way to dismiss the keys at all.
  // [TH] แตะบริเวณฟอร์มที่ไม่ใช่ช่องกรอกแล้วแป้นพิมพ์จะถูกซ่อน เหมือนที่หน้าตั้งค่าทำอยู่แล้ว
  //      ถ้าไม่มีสิ่งนี้ แป้นพิมพ์จะบังตั้งแต่ y=120 ลงไป ช่องที่สามจึงอยู่ใต้แป้นพิมพ์
  //      พิมพ์ชื่อเสร็จแล้วจะกดช่องอายุไม่ได้เลย เพราะไม่มีวิธีปิดแป้นพิมพ์
  lv_obj_add_event_cb(col, [](lv_event_t*) { hideAddKeyboard(); }, LV_EVENT_CLICKED, nullptr);
  lv_obj_add_event_cb(bar, [](lv_event_t*) { hideAddKeyboard(); }, LV_EVENT_CLICKED, nullptr);

  makeAddField(col, "รหัสคนไข้ (P001)", nullptr, &taPCode);
  makeAddField(col, "นาม", nullptr, &taPName);
  makeAddField(col, "อายุ", "0123456789", &taPAge);

  kbAdd = lv_keyboard_create(scrAdd);
  lv_obj_set_size(kbAdd, lv_pct(100), 120);
  lv_obj_align(kbAdd, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_add_event_cb(kbAdd, [](lv_event_t*) { hideAddKeyboard(); }, LV_EVENT_READY, nullptr);
  lv_obj_add_event_cb(kbAdd, [](lv_event_t*) { hideAddKeyboard(); }, LV_EVENT_CANCEL, nullptr);
  lv_obj_add_flag(kbAdd, LV_OBJ_FLAG_HIDDEN);

  // Thai on open: this keyboard exists for ชื่อผู้ป่วย, and the code and age
  // fields beside it are short enough to reach Latin in one tap.
  // [TH] เปิดมาเป็นภาษาไทย เพราะแป้นนี้มีไว้พิมพ์ชื่อผู้ป่วย
  //      ส่วนช่องรหัสและอายุกดสลับไปภาษาอังกฤษได้ในปุ่มเดียว
  lv_obj_set_style_text_font(kbAdd, &thai20, 0);
  thaiKbApply(kbAdd, true);
}

static void openAddForm() {
  if (!scrAdd) buildAddForm();
  lv_textarea_set_text(taPCode, "");
  lv_textarea_set_text(taPName, "");
  lv_textarea_set_text(taPAge, "");
  // Also here, not only in addFieldFocused: LVGL fires FOCUSED when focus
  // changes, so tapping the field that already had focus never resets anything
  // and the form would open in whatever script was left behind last time.
  // [TH] ต้องตั้งตรงนี้ด้วย ไม่ใช่แค่ใน addFieldFocused เพราะ LVGL ส่งเหตุการณ์ FOCUSED เฉพาะตอนที่โฟกัสเปลี่ยน
  //      ถ้าแตะช่องเดิมที่โฟกัสอยู่แล้วจะไม่มีการตั้งค่าใหม่ ฟอร์มจะเปิดมาเป็นภาษาที่ค้างไว้จากครั้งก่อน
  thaiKbSetScript(kbAdd, true);
  hideAddKeyboard();
  lv_scr_load(scrAdd);
}

// -------------------------------------------------------------- finish screen

static lv_obj_t *scrFinish, *lblFinWho, *lblFinStat, *lblFinFault, *lblFinSaved;

// Built once and re-filled, like every other screen here.
static void buildFinish() {
  scrFinish = lv_obj_create(nullptr);
  styleScreen(scrFinish);

  lv_obj_t* col = lv_obj_create(scrFinish);
  lv_obj_set_size(col, lv_pct(100), lv_pct(100));
  lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(col, 0, 0);
  lv_obj_set_style_pad_row(col, 6, 0);
  lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);
  // The screen's white text colour does not reach through this container: LVGL's
  // theme puts its own dark text colour on every plain object, and inheritance
  // stops at the first ancestor that sets one. Labels below with no colour of
  // their own came out near-black on navy. Set it here so children inherit it.
  // [TH] สีข้อความสีขาวของหน้าจอส่งผ่านกล่องนี้ไม่ได้ เพราะธีมของ LVGL ตั้งสีเข้มไว้ที่ตัวกล่องเอง
  //      และการสืบทอดสีจะหยุดที่ตัวแรกที่ตั้งค่าไว้ ป้ายที่ไม่ได้กำหนดสีจึงกลายเป็นสีเกือบดำบนพื้นน้ำเงิน
  lv_obj_set_style_text_color(col, lv_color_hex(COL_TEXT), 0);

  lblFinWho = lv_label_create(col);
  lv_obj_set_style_text_font(lblFinWho, &thai20, 0);

  lblFinStat = lv_label_create(col);
  lv_obj_set_style_text_font(lblFinStat, &lv_font_montserrat_28, 0);

  lblFinFault = lv_label_create(col);
  lv_obj_set_style_text_font(lblFinFault, &thai20, 0);
  lv_obj_set_style_text_color(lblFinFault, lv_color_hex(COL_FAULT), 0);

  // Full-strength text at 20 px, not muted 14 px: whether the session reached the
  // card is the one line on this screen a therapist must not misread, and the
  // muted silver was too dim against the navy to read at arm's length.
  // [TH] ใช้สีขาวเต็มและขนาด 20 แทนสีเทาขนาด 14 เพราะบรรทัดนี้บอกว่าบันทึกลงการ์ดแล้วหรือยัง
  //      เป็นข้อความที่ต้องอ่านผิดไม่ได้ และสีเทาบนพื้นน้ำเงินอ่านยากเมื่อมองจากระยะห่าง
  lblFinSaved = lv_label_create(col);
  lv_obj_set_style_text_font(lblFinSaved, &thai20, 0);
  lv_obj_set_style_text_color(lblFinSaved, lv_color_hex(COL_TEXT), 0);

  // Touching this is optional: the panel's START button ends the summary too,
  // because a starting session pulls the display back to the main screen from any
  // sub-screen (see leaveSubScreenForRun). The button stays for the case where
  // someone wants to dismiss the summary without starting another session.
  // [TH] ปุ่มนี้ไม่จำเป็นต้องแตะก็ได้ เพราะกดปุ่ม START บนแผงก็ปิดหน้าสรุปเช่นกัน
  //      เนื่องจากเมื่อเริ่มรอบใหม่ จอจะกลับไปหน้าหลักเองจากทุกหน้าจอย่อย
  //      ปุ่มนี้มีไว้สำหรับกรณีที่ต้องการปิดหน้าสรุปโดยยังไม่เริ่มรอบใหม่
  lv_obj_t* ok = makeBtn(col, "ตกลง", COL_SURFACE, &thai20,
                         [](lv_event_t*) { lv_scr_load(scrMain); });
  lv_obj_set_size(ok, 120, 40);
}

void uiShowFinish(const char* patientName, uint32_t durationSec, uint16_t reps,
                  bool fault, bool saved) {
  if (!scrFinish) buildFinish();

  lv_label_set_text(lblFinWho, (patientName && patientName[0]) ? patientName : "ไม่ระบุ");
  lv_label_set_text_fmt(lblFinStat, "%02lu:%02lu   x%u", (unsigned long)(durationSec / 60),
                        (unsigned long)(durationSec % 60), reps);
  // A faulted session still logs and still shows its numbers; the extra line
  // says why it ended, it does not replace the summary.
  // [TH] รอบที่จบเพราะขึ้นสถานะผิดพลาดก็ยังบันทึกและยังแสดงตัวเลขตามปกติ
  //      บรรทัดนี้บอกสาเหตุที่จบ ไม่ได้มาแทนที่สรุปผล
  lv_label_set_text(lblFinFault, fault ? "ตรวจสอบสวิตช์" : "");
  lv_label_set_text(lblFinSaved, saved ? "บันทึกแล้ว" : "ยังไม่ได้บันทึก รอการ์ด");

  lv_scr_load(scrFinish);
}

// ----------------------------------------------------------- confirm screen

// Reflects session.h's target() contract on the controller: 0 is a legal
// target meaning "run until STOP," not "unset," so it must render as this
// word rather than a bare 0 -- see drawConfirmReps() and confirmStep() below.
// Also used to seed the label's build-time placeholder (see buildConfirm()):
// a bare "0" there would be exactly the rendering this rule forbids, on the
// one frame before the first real draw, if some future caller ever skipped
// straight to lv_scr_load() without going through uiShowConfirm() first.
// [TH] สะท้อนสัญญาของ target() ฝั่งคอนโทรลเลอร์: 0 เป็นค่าที่ใช้ได้จริง หมายถึง "ทำจนกว่าจะกด STOP"
//      ไม่ใช่ "ยังไม่ได้ตั้งค่า" จึงต้องแสดงเป็นคำนี้ ไม่ใช่เลข 0 เปล่าๆ ดู drawConfirmReps() กับ
//      confirmStep() ด้านล่าง ใช้ตั้งต้นข้อความของป้ายตอนสร้างด้วย (ดู buildConfirm()) เพราะเลข "0" เปล่าๆ
//      ตรงนั้นจะเป็นการแสดงผลแบบที่กฎนี้ห้ามพอดี ในเฟรมเดียวก่อนการวาดจริงครั้งแรก ถ้าวันหน้ามีจุดเรียกอื่น
//      ที่ข้ามไป lv_scr_load() ตรงๆ โดยไม่ผ่าน uiShowConfirm() ก่อน
#define CONFIRM_UNLIMITED "ไม่จำกัด"

// Mirrors CONFIRM_TIMEOUT_MS in pt-leg-controller/session.h (30 s). Not shared
// code -- this task is display-side only, and the state message carries no
// seconds-left field at all -- so this is a display-side approximation of the
// controller's real deadline, clocked locally the same way the in-phase HOLD
// / REST timer above is (see phaseTimerStartMs in uiUpdateMain), and the same
// way pt-leg-display.ino's own 2.5 s link-down check mirrors the controller's
// UART watchdog without sharing its constant. The controller's own phase
// transition back to Idle remains the actual authority: if this clock and the
// controller's ever disagree, the next state message corrects the screen
// (uiUpdateMain leaves it) within one refresh either way.
// [TH] สะท้อนค่า CONFIRM_TIMEOUT_MS (30 วินาที) ในไฟล์ pt-leg-controller/session.h ไม่ได้ใช้โค้ดร่วมกัน
//      เพราะงานนี้แก้เฉพาะฝั่งจอ และข้อความสถานะไม่มีฟิลด์เวลาที่เหลืออยู่เลย จึงเป็นการประมาณเวลาหมดอายุ
//      จริงของคอนโทรลเลอร์ฝั่งจอเอง จับเวลาเองแบบเดียวกับตัวจับเวลาในสถานะค้าง/พักด้านบน
//      (ดู phaseTimerStartMs ใน uiUpdateMain) และแบบเดียวกับที่ตัวตรวจสายหลุด 2.5 วินาทีใน
//      pt-leg-display.ino สะท้อนตัวเฝ้าระวัง UART ของคอนโทรลเลอร์โดยไม่ได้ใช้ค่าคงที่ร่วมกัน
//      การเปลี่ยนสถานะกลับไปว่างของคอนโทรลเลอร์เองยังคงเป็นผู้ตัดสินตัวจริงเสมอ ถ้านาฬิกานี้กับของ
//      คอนโทรลเลอร์ไม่ตรงกัน ข้อความสถานะครั้งถัดไปจะแก้หน้าจอให้ตรง (uiUpdateMain เป็นคนออกจากหน้านี้)
//      ภายในหนึ่งรอบรีเฟรชไม่ว่าจะทางไหน
static const uint32_t CONFIRM_WINDOW_SEC = 30;

// How long a disagreement between confirmReps and the controller's echo has
// to persist before it is shown as unconfirmed. The controller emits state on
// its own fixed ~250 ms cadence (pt-leg-controller.ino), not on receipt of a
// "sets" message -- onLinkSets() there only applies the value, it does not
// trigger an extra state emission -- so after a tap the echo and this
// screen's own next redraw race independently, and disagree for up to one
// whole tick on essentially every edit even with a healthy link. Flagging
// that as "unconfirmed" would fire on roughly half of all normal taps and
// train the operator to ignore the one signal that means a "sets" frame was
// actually lost. ~3x a refresh tick comfortably covers that race without
// meaningfully delaying the real signal.
// [TH] ความไม่ตรงกันระหว่าง confirmReps กับค่าที่คอนโทรลเลอร์สะท้อนกลับต้องค้างอยู่นานแค่ไหนก่อนจะถือว่า
//      ยังไม่ยืนยัน คอนโทรลเลอร์ส่งข้อความสถานะตามจังหวะคงที่ราว 250 มิลลิวินาทีของตัวเอง
//      (pt-leg-controller.ino) ไม่ใช่ตอนได้รับ "sets" -- onLinkSets() ที่นั่นแค่นำค่าไปใช้ ไม่ได้สั่งส่งสถานะ
//      เพิ่ม -- ค่าที่สะท้อนกลับมากับการวาดใหม่ครั้งถัดไปของหน้าจอนี้จึงแข่งกันเป็นอิสระ และไม่ตรงกันได้นานถึง
//      หนึ่งรอบเต็มแทบทุกครั้งที่แก้ค่า แม้ลิงก์จะปกติดีก็ตาม ถ้าติดป้ายว่ายังไม่ยืนยันตรงนั้นจะขึ้นในเกือบ
//      ครึ่งหนึ่งของการแตะปกติ และสอนให้ผู้ใช้เพิกเฉยต่อสัญญาณเดียวที่บอกว่าเฟรม "sets" หายไปจริงๆ
//      สามเท่าของหนึ่งรอบรีเฟรชครอบคลุมการแข่งกันนี้ได้สบายๆ โดยไม่หน่วงสัญญาณจริงมากเกินไป
static const uint32_t CONFIRM_MISMATCH_GRACE_MS = 750;

static lv_obj_t *scrConfirm, *lblConfirmReps, *lblConfirmMismatch;
static lv_obj_t *lblConfirmProgramme, *lblConfirmTimer;
static void (*cbConfirmSets)(uint16_t) = nullptr;
static void (*cbConfirmOk)(bool) = nullptr;

// The number this screen shows and edits. Deliberately separate from the
// controller's echoed target (the `controllerTarget` argument to
// uiShowConfirm): this is redrawn the instant a tap lands, the echo arrives up
// to 250 ms plus a UART round trip later. See confirmStep() and
// uiShowConfirm().
// [TH] ตัวเลขที่หน้าจอนี้แสดงและแก้ไข แยกออกจากค่าที่คอนโทรลเลอร์สะท้อนกลับมาโดยตั้งใจ
//      (อาร์กิวเมนต์ controllerTarget ของ uiShowConfirm) ตัวเลขนี้ถูกวาดใหม่ทันทีที่แตะ
//      ส่วนค่าที่สะท้อนกลับมาช้ากว่าได้ถึง 250 มิลลิวินาทีบวกเวลาไปกลับของสาย UART
//      ดู confirmStep() และ uiShowConfirm()
static uint16_t confirmReps = 0;
static uint32_t confirmWindowStartMs = 0;  // this screen's own millis() clock — นาฬิกาของหน้าจอนี้เอง
// millis() of the last local edit, i.e. the last time confirmReps changed from
// a tap here. Used only to grace-period the mismatch warning in uiShowConfirm
// -- see CONFIRM_MISMATCH_GRACE_MS.
// [TH] millis() ของการแก้ไขจากจอนี้ครั้งล่าสุด คือครั้งล่าสุดที่ confirmReps เปลี่ยนจากการแตะที่นี่
//      ใช้แค่กำหนดช่วงผ่อนผันของคำเตือนยังไม่ยืนยันใน uiShowConfirm ดู CONFIRM_MISMATCH_GRACE_MS
static uint32_t confirmLastEditMs = 0;
// The last secsLeft actually drawn into lblConfirmTimer, so uiShowConfirm can
// skip the redraw on every tick where the second hasn't actually ticked over
// -- see uiUpdateMain's own change-detection just above for the same idea
// applied to the whole main screen. An impossible value (CONFIRM_WINDOW_SEC
// can never be exceeded once running) forces the first real draw.
// [TH] เลขวินาทีที่เหลือครั้งล่าสุดที่วาดลง lblConfirmTimer จริง เพื่อให้ uiShowConfirm ข้ามการวาดซ้ำ
//      ในทุกรอบที่วินาทียังไม่ขยับจริง แนวคิดเดียวกับตัวตรวจการเปลี่ยนแปลงของทั้งหน้าหลักด้านบน
//      ใช้ค่าที่เป็นไปไม่ได้ (CONFIRM_WINDOW_SEC ไม่มีทางถูกเกินตอนกำลังนับ) เพื่อบังคับให้วาดจริงครั้งแรกเสมอ
static uint32_t confirmLastSecsShown = CONFIRM_WINDOW_SEC + 1;

static bool confirmScreenActive() { return scrConfirm && lv_scr_act() == scrConfirm; }

static void drawConfirmReps() {
  if (confirmReps == 0) lv_label_set_text(lblConfirmReps, CONFIRM_UNLIMITED);
  else lv_label_set_text_fmt(lblConfirmReps, "%u", confirmReps);
}

// Steps update the local value and redraw immediately, then tell the
// controller -- redraw first, send second. The label must never wait on a
// round trip: that is exactly what makes the main screen's own target stepper
// feel dead when the link is down (see targetStep() above). The controller
// still gets told, so a panel START commits the same number this screen is
// showing. Also restarts this screen's own 30 s clock, mirroring
// setPendingTarget() extending the controller's real window on every edit
// (session.h) -- without it, stepping the value a few times would count down
// a window this very screen keeps re-opening on the controller side. And
// stamps confirmLastEditMs, which is what gates the mismatch warning's grace
// window in uiShowConfirm.
// [TH] การกดปรับค่าจะอัปเดตค่าในตัวจอเองและวาดใหม่ทันที แล้วค่อยบอกคอนโทรลเลอร์ วาดก่อนส่งทีหลัง
//      เพราะป้ายต้องไม่รอการตอบกลับ นี่คือสิ่งที่ทำให้ปุ่มปรับค่าเป้าหมายที่หน้าหลักรู้สึกเหมือนกดไม่ติด
//      ตอนลิงก์หลุด (ดู targetStep() ด้านบน) แต่ยังต้องบอกคอนโทรลเลอร์ด้วย เพื่อให้การกดปุ่มเริ่มที่หน้าปัด
//      ยืนยันตัวเลขเดียวกับที่จอแสดง ยังตั้งนาฬิกา 30 วินาทีของหน้าจอนี้เองใหม่ด้วย สะท้อนการที่
//      setPendingTarget() ต่อเวลาหน้าต่างจริงของคอนโทรลเลอร์ทุกครั้งที่แก้ค่า (session.h) ถ้าไม่ทำแบบนี้
//      การกดปรับค่าไม่กี่ครั้งจะนับถอยหลังหน้าต่างที่หน้าจอนี้เองเป็นคนต่ออายุอยู่ฝั่งคอนโทรลเลอร์ และบันทึก
//      confirmLastEditMs ซึ่งเป็นตัวกำหนดช่วงผ่อนผันของคำเตือนยังไม่ยืนยันใน uiShowConfirm
static void confirmStep(int delta) {
  int next = (int)confirmReps + delta;
  if (next < 0) next = 0;
  if (next > 99) next = 99;
  if ((uint16_t)next == confirmReps) return;
  confirmReps = (uint16_t)next;
  confirmWindowStartMs = millis();
  confirmLastEditMs = millis();
  drawConfirmReps();
  if (cbConfirmSets) cbConfirmSets(confirmReps);
}
static void confirmMinusClicked(lv_event_t*) { confirmStep(-1); }
static void confirmPlusClicked(lv_event_t*) { confirmStep(1); }
static void confirmOkClicked(lv_event_t*) { if (cbConfirmOk) cbConfirmOk(true); }
static void confirmCancelClicked(lv_event_t*) { if (cbConfirmOk) cbConfirmOk(false); }

// Built once and reused, like every other screen in this file (see buildData(),
// buildSettings()): rebuilding per open leaks the LVGL pool.
// [TH] สร้างครั้งเดียวแล้วใช้ซ้ำ เหมือนหน้าจออื่นทุกหน้าในไฟล์นี้ (ดู buildData(), buildSettings())
//      สร้างใหม่ทุกครั้งที่เปิดจะทำให้หน่วยความจำของ LVGL รั่ว
static void buildConfirm() {
  scrConfirm = lv_obj_create(nullptr);
  styleScreen(scrConfirm);

  lv_obj_t* col = lv_obj_create(scrConfirm);
  lv_obj_set_size(col, lv_pct(100), lv_pct(100));
  lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(col, 0, 0);
  lv_obj_set_style_pad_row(col, 3, 0);
  lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_text_color(col, lv_color_hex(COL_TEXT), 0);

  lv_obj_t* title = lv_label_create(col);
  lv_obj_set_style_text_font(title, &thai20, 0);
  lv_label_set_text(title, "รอยืนยัน");

  lv_obj_t* row = lv_obj_create(col);
  lv_obj_set_size(row, 200, 46);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(row, 12, 0);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* minus = makeBtn(row, "-", COL_SURFACE, &lv_font_montserrat_20, confirmMinusClicked);
  lv_obj_set_size(minus, 40, 40);

  lblConfirmReps = lv_label_create(row);
  lv_obj_set_style_text_font(lblConfirmReps, &thai28, 0);
  // Not a bare "0": that is exactly what the ไม่จำกัด rule forbids (see
  // CONFIRM_UNLIMITED above). This text is only ever visible for one frame
  // before uiShowConfirm's first real draw, but a build-time placeholder that
  // violates the screen's own display rule is a trap worth not leaving lying
  // around.
  // [TH] ไม่ใช่เลข "0" เปล่าๆ ซึ่งเป็นสิ่งที่กฎ ไม่จำกัด ห้ามพอดี (ดู CONFIRM_UNLIMITED ด้านบน)
  //      ข้อความนี้จะเห็นได้แค่เฟรมเดียวก่อนการวาดจริงครั้งแรกของ uiShowConfirm แต่ค่าตั้งต้นตอนสร้าง
  //      ที่ขัดกับกฎการแสดงผลของหน้าจอเองก็ไม่ควรปล่อยทิ้งไว้
  lv_label_set_text(lblConfirmReps, CONFIRM_UNLIMITED);

  lv_obj_t* plus = makeBtn(row, "+", COL_SURFACE, &lv_font_montserrat_20, confirmPlusClicked);
  lv_obj_set_size(plus, 40, 40);

  // Always present, never LV_OBJ_FLAG_HIDDEN -- toggling that flag on a flex
  // child excludes both it and its row gap from layout, so the ตกลง/ยกเลิก row
  // right below it would jump by the label's height every time a mismatch
  // appeared or cleared, moving the commit buttons under the operator's
  // finger. Reserving the space with an empty string instead costs a little
  // vertical room permanently, in exchange for a layout that never moves.
  // See uiShowConfirm() for when this actually gets text.
  // [TH] แสดงอยู่เสมอ ไม่ใช้ LV_OBJ_FLAG_HIDDEN เพราะการสลับแฟล็กนี้บนลูกของ flex จะตัดทั้งตัวมันเองและ
  //      ช่องว่างระหว่างแถวออกจากการจัดวาง แถวปุ่มตกลง/ยกเลิกที่อยู่ใต้มันทันทีจะกระโดดตามความสูงของป้าย
  //      ทุกครั้งที่ความไม่ตรงกันปรากฏหรือหายไป ทำให้ปุ่มยืนยันขยับหนีนิ้วผู้ใช้ การจองพื้นที่ไว้ด้วยข้อความ
  //      ว่างเปล่าแทนเสียพื้นที่แนวตั้งเล็กน้อยถาวร แลกกับการจัดวางที่ไม่ขยับเลย ดู uiShowConfirm() ว่าเมื่อไร
  //      ป้ายนี้ถึงจะมีข้อความจริง
  lblConfirmMismatch = lv_label_create(col);
  lv_obj_set_style_text_font(lblConfirmMismatch, &thai14, 0);
  lv_obj_set_style_text_color(lblConfirmMismatch, lv_color_hex(COL_FAULT), 0);
  lv_label_set_text(lblConfirmMismatch, "");

  lv_obj_t* btnRow = lv_obj_create(col);
  lv_obj_set_size(btnRow, 230, 40);
  lv_obj_set_flex_flow(btnRow, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(btnRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(btnRow, 14, 0);
  lv_obj_set_style_bg_opa(btnRow, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(btnRow, 0, 0);
  lv_obj_clear_flag(btnRow, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* cancel = makeBtn(btnRow, "ยกเลิก", COL_SURFACE, &thai20, confirmCancelClicked);
  lv_obj_set_size(cancel, 100, 36);

  lv_obj_t* ok = makeBtn(btnRow, "ตกลง", COL_SURFACE, &thai20, confirmOkClicked);
  lv_obj_set_size(ok, 100, 36);

  lblConfirmProgramme = lv_label_create(col);
  lv_obj_set_style_text_font(lblConfirmProgramme, &thai14, 0);
  lv_obj_set_style_text_color(lblConfirmProgramme, lv_color_hex(COL_MUTED), 0);
  lv_obj_set_style_text_align(lblConfirmProgramme, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(lblConfirmProgramme, "");

  lblConfirmTimer = lv_label_create(col);
  lv_obj_set_style_text_font(lblConfirmTimer, &thai14, 0);
  lv_obj_set_style_text_color(lblConfirmTimer, lv_color_hex(COL_MUTED), 0);
  lv_label_set_text(lblConfirmTimer, "");
}

void uiSetConfirmCallbacks(void (*onSets)(uint16_t), void (*onConfirm)(bool)) {
  cbConfirmSets = onSets;
  cbConfirmOk = onConfirm;
}

void uiShowConfirm(uint16_t controllerTarget, const DeviceSettings& s, bool linkUp) {
  if (!scrConfirm) buildConfirm();

  // Belt and braces (see this function's declaration in ui.h): the sketch is
  // expected to stop calling this once the link drops and let uiUpdateMain's
  // own linkUp-gated check take the screen back to scrMain. If it is ever
  // called anyway, do nothing rather than seed, compare, or redraw off a
  // controllerTarget that is not actually fresh.
  // [TH] กันพลาดสองชั้น (ดูคำอธิบายที่ประกาศฟังก์ชันนี้ใน ui.h): ตามปกติสเก็ตช์จะเลิกเรียกฟังก์ชันนี้ทันที
  //      ที่ลิงก์หลุด แล้วปล่อยให้เงื่อนไขที่กันด้วย linkUp ของ uiUpdateMain พาไปหน้าหลักเอง
  //      ถ้าถูกเรียกอยู่ดี ให้ไม่ทำอะไรเลย ดีกว่าไปตั้งต้น เทียบ หรือวาดใหม่จาก controllerTarget ที่ไม่สดจริง
  if (!linkUp) return;

  bool justEntered = !confirmScreenActive();
  if (justEntered) {
    // The transition into Confirm: seed the on-screen number from what the
    // controller is reporting right now, then open the screen. requestStart()
    // seeds its own pendingTargetReps from the stored prescription (session.h),
    // so on entry this equals the medically-prescribed target -- clamped here
    // regardless, defence in depth against a controllerTarget this display has
    // no other reason to distrust. See this function's declaration in ui.h for
    // why every later call, while the screen stays active, treats the same
    // argument as an echo to compare instead of a value to seed with.
    // leaveSubScreenForRun() first: Confirm can be entered while the operator
    // is on the settings or add-patient screen with its keyboard up (a panel
    // START is independent of what the display happens to be showing), and
    // jumping straight to scrConfirm would leave that keyboard state -- a
    // shrunk field column -- dangling for whenever that screen is reopened.
    // [TH] จังหวะเพิ่งเข้าสถานะรอยืนยัน: ตั้งต้นตัวเลขบนจอจากค่าที่คอนโทรลเลอร์กำลังรายงานอยู่ตอนนี้
    //      แล้วเปิดหน้าจอ requestStart() ตั้งต้น pendingTargetReps ของตัวเองจากค่าที่แพทย์สั่งไว้
    //      (session.h) ตอนเพิ่งเข้าสถานะนี้ค่าจึงเท่ากับค่าที่แพทย์สั่ง จำกัดช่วงตรงนี้ไว้ด้วยไม่ว่ากรณีใด
    //      กันพลาดสองชั้นเผื่อ controllerTarget ที่ไม่มีเหตุผลอื่นให้ไม่เชื่อ ดูคำอธิบายที่ประกาศฟังก์ชันนี้
    //      ใน ui.h ว่าทำไมการเรียกครั้งถัดไป ขณะที่หน้าจอยังแสดงอยู่ จึงถือว่าอาร์กิวเมนต์เดียวกันนี้เป็นค่า
    //      สะท้อนกลับสำหรับเทียบแทนการตั้งต้น เรียก leaveSubScreenForRun() ก่อน เพราะสถานะรอยืนยันเข้าได้
    //      แม้ผู้ใช้อยู่หน้าตั้งค่าหรือฟอร์มลงทะเบียนโดยเปิดแป้นพิมพ์ค้างไว้ (ปุ่ม START บนแผงเป็นอิสระจาก
    //      สิ่งที่จอกำลังแสดง) การกระโดดไป scrConfirm ตรงๆ จะทิ้งสถานะแป้นพิมพ์นั้นค้างไว้ กล่องช่องกรอกที่ถูก
    //      ย่อไว้จะยังย่ออยู่เมื่อกลับไปเปิดหน้านั้นอีกครั้ง
    confirmReps = controllerTarget > 99 ? 99 : controllerTarget;
    confirmWindowStartMs = millis();
    confirmLastEditMs = millis();
    confirmLastSecsShown = CONFIRM_WINDOW_SEC + 1;  // force the first timer draw below
    drawConfirmReps();
    lv_label_set_text(lblConfirmMismatch, "");
    // Read-only programme summary, from the settings the machine will
    // actually run with -- set once here, not every call, since nothing on
    // this screen can change gLast while it is open (there is no navigation
    // path back to the settings screen from here). Reuses the exact wording
    // of the settings screen's own rows (ยกขึ้น/ค้าง/ลดลง/พัก, see
    // buildSettings()) so the two screens agree. Gated on settingsKnown, the
    // same flag SAVE already gates on (ui.h): gLast is seeded at boot with
    // superseded placeholder stroke times (pt-leg-display.ino), not merely
    // arbitrary defaults, so showing it before the controller's real values
    // are known would state a wrong programme on the one screen whose job is
    // saying what will actually run.
    // [TH] สรุปโปรแกรมแบบอ่านอย่างเดียว จากค่าตั้งค่าที่เครื่องจะใช้จริง ตั้งค่าครั้งเดียวตรงนี้ ไม่ใช่ทุกครั้ง
    //      ที่เรียก เพราะไม่มีอะไรบนหน้าจอนี้ที่จะเปลี่ยน gLast ได้ขณะเปิดอยู่ (ไม่มีเส้นทางกลับไปหน้าตั้งค่า
    //      จากตรงนี้) ใช้คำเดียวกับแถวของหน้าตั้งค่า (ยกขึ้น/ค้าง/ลดลง/พัก ดู buildSettings())
    //      ให้สองหน้าจอนี้ใช้คำตรงกัน กั้นด้วย settingsKnown ตัวเดียวกับที่ปุ่มบันทึกกั้นอยู่แล้ว (ui.h):
    //      gLast ถูกตั้งต้นตอนบูตด้วยเวลาจังหวะเคลื่อนที่ที่เลิกใช้แล้ว (pt-leg-display.ino) ไม่ใช่แค่
    //      ค่าเริ่มต้นทั่วไป การแสดงก่อนรู้ค่าจริงจากคอนโทรลเลอร์จะบอกโปรแกรมผิดบนหน้าจอเดียวที่มีหน้าที่
    //      บอกว่าจะเกิดอะไรขึ้นจริง
    if (settingsKnown) {
      lv_label_set_text_fmt(lblConfirmProgramme, "ยกขึ้น %u ค้าง %u วิ\nลดลง %u พัก %u วิ", s.liftSec,
                            s.holdSec, s.lowerSec, s.restSec);
    } else {
      lv_label_set_text(lblConfirmProgramme, "");
    }
    leaveSubScreenForRun();
    lv_scr_load(scrConfirm);
  } else {
    // Still open: the argument is now an echo of whatever this screen last
    // sent, not a value to seed with. A disagreement that outlives
    // CONFIRM_MISMATCH_GRACE_MS means the "sets" message never arrived -- it
    // is a bare emit() with no ack on this branch (link.cpp), so this
    // comparison is the only way to know. A disagreement that clears within
    // the grace window is not shown at all: the controller's state cadence
    // and this function's own call cadence race independently (see
    // CONFIRM_MISMATCH_GRACE_MS above), so a one-tick disagreement is the
    // normal shape of a healthy edit settling in, not a lost frame. No resend
    // timer here on purpose: feature/link-crc-ack, merging before this
    // feature ships, adds retransmit under every sender including this one,
    // and a resend loop stacked on top of that would be dead complexity.
    // Until it lands, a lost frame that outlives the grace window shows as a
    // lasting mismatch instead of being silently left applied wrong.
    // [TH] หน้าจอยังเปิดอยู่: อาร์กิวเมนต์ตอนนี้คือค่าสะท้อนกลับของสิ่งที่หน้าจอนี้เพิ่งส่งไปล่าสุด ไม่ใช่ค่า
    //      ที่ต้องตั้งต้นอีกแล้ว ความไม่ตรงกันที่ค้างนานเกิน CONFIRM_MISMATCH_GRACE_MS หมายความว่าข้อความ
    //      "sets" ไม่เคยไปถึง เพราะเป็นแค่ emit() เปล่าๆ ไม่มีการยืนยันรับบนสายนี้ (link.cpp) การเทียบนี้จึง
    //      เป็นทางเดียวที่จะรู้ ความไม่ตรงกันที่หายไปเองภายในช่วงผ่อนผันจะไม่ถูกแสดงเลย เพราะจังหวะส่งสถานะ
    //      ของคอนโทรลเลอร์กับจังหวะเรียกของฟังก์ชันนี้เองแข่งกันเป็นอิสระ (ดู CONFIRM_MISMATCH_GRACE_MS
    //      ด้านบน) ความไม่ตรงกันแค่หนึ่งรอบจึงเป็นรูปแบบปกติของการแก้ไขที่กำลังจะนิ่ง ไม่ใช่เฟรมที่หายไป
    //      จงใจไม่ตั้งตัวจับเวลาส่งซ้ำ เพราะ feature/link-crc-ack ที่จะรวมเข้ามาก่อนขึ้นเครื่องจริงจะเพิ่มการ
    //      ส่งซ้ำให้ผู้ส่งทุกตัวอยู่แล้วรวมถึงตัวนี้ด้วย ถ้าตั้งตัวส่งซ้ำซ้อนทับไปอีกชั้นจะกลายเป็นความซับซ้อน
    //      ที่ไม่มีประโยชน์ ก่อนหน้านั้นเฟรมที่หายไปและค้างเกินช่วงผ่อนผันจะแสดงเป็นค่าที่ไม่ตรงกันค้างอยู่
    //      แทนที่จะถูกปล่อยให้ใช้ค่าที่ผิดไปแบบเงียบๆ
    bool disagreeNow = controllerTarget != confirmReps;
    bool graceElapsed = (millis() - confirmLastEditMs) >= CONFIRM_MISMATCH_GRACE_MS;
    lv_label_set_text(lblConfirmMismatch, (disagreeNow && graceElapsed) ? "ยังไม่ยืนยัน" : "");
  }

  // A screen that can vanish on its own has to say so. See CONFIRM_WINDOW_SEC
  // above for why this clock is local and only an approximation of the
  // controller's real one. Only rewritten when the shown second actually
  // changes -- matching uiUpdateMain's own change-detection above -- since
  // this function, unlike uiUpdateMain, has no early-return for an unchanged
  // call and would otherwise repaint an identical string every 250 ms tick.
  // [TH] หน้าจอที่หายไปเองได้ต้องบอกผู้ใช้ล่วงหน้า ดู CONFIRM_WINDOW_SEC ด้านบนว่าทำไมนาฬิกานี้จับเวลา
  //      เองฝั่งจอ และเป็นแค่การประมาณเวลาจริงของคอนโทรลเลอร์เท่านั้น วาดใหม่เฉพาะตอนวินาทีที่แสดงเปลี่ยน
  //      จริงเท่านั้น เหมือนตัวตรวจการเปลี่ยนแปลงของ uiUpdateMain ด้านบน เพราะฟังก์ชันนี้ไม่มีทางคืนค่าก่อน
  //      กำหนดแบบนั้นเมื่อไม่มีอะไรเปลี่ยน ไม่งั้นจะวาดข้อความเดิมซ้ำทุกรอบ 250 มิลลิวินาทีโดยเปล่าประโยชน์
  uint32_t inWindowSec = (millis() - confirmWindowStartMs) / 1000;
  uint32_t secsLeft = inWindowSec >= CONFIRM_WINDOW_SEC ? 0 : CONFIRM_WINDOW_SEC - inWindowSec;
  if (secsLeft != confirmLastSecsShown) {
    confirmLastSecsShown = secsLeft;
    lv_label_set_text_fmt(lblConfirmTimer, "เหลือ %lu วิ", (unsigned long)secsLeft);
  }
}

void uiLoop() { lv_timer_handler(); }

#if UI_TEST_HOOK
// The bench harness needs two things this file alone knows: which screen is up,
// and which textarea a typed string should land in. Everything else it can do
// through synthetic taps. Compiled away entirely when the hook is off.
// [TH] ตัวช่วยทดสอบต้องรู้สองอย่างที่มีแต่ไฟล์นี้รู้ คือหน้าจอไหนกำลังแสดงอยู่
//      และข้อความที่พิมพ์ควรลงในช่องกรอกไหน ที่เหลือสั่งผ่านการแตะจำลองได้หมด
//      ถ้าปิดโหมดทดสอบ โค้ดส่วนนี้จะไม่ถูกคอมไพล์เลย
lv_obj_t* uiTestTextarea() {
  if (kbAdd && !lv_obj_has_flag(kbAdd, LV_OBJ_FLAG_HIDDEN)) return lv_keyboard_get_textarea(kbAdd);
  if (kb && !lv_obj_has_flag(kb, LV_OBJ_FLAG_HIDDEN)) return lv_keyboard_get_textarea(kb);
  return nullptr;
}

const char* uiTestScreenName() {
  lv_obj_t* s = lv_scr_act();
  if (s == scrMain) return "main";
  if (s == scrSettings) return "settings";
  if (s == scrData) return "data";
  if (s == scrHistory) return "history";
  if (s == scrTime) return "time";
  if (s == scrAdd) return "add";
  if (s == scrFinish) return "finish";
  if (s == scrConfirmReset) return "confirm";
  if (s == scrConfirm) return "confirmStart";  // Phase::Confirm's screen, distinct from scrConfirmReset above
  return "unknown";
}
#endif




