// การสร้างส่วนติดต่อผู้ใช้ทั้งหมดบนจอสัมผัส CYD ด้วย LVGL
// ประกอบด้วยหน้าจอหลัก (นาฬิกา/จำนวนครั้ง/สถานะ แสดงผลอย่างเดียว) และหน้าตั้งค่า (WiFi/เซิร์ฟเวอร์/เวลารอบ)
// All LVGL UI for the CYD touchscreen: output-only main screen and settings screen.
#include "ui.h"
#include <Arduino.h>  // millis() for the in-phase timer — ใช้ millis() จับเวลาในสถานะ
#include <lvgl.h>
#include <TFT_eSPI.h>
#include "touch_bb.h"  // touch pins live there — ขาทัชสกรีนอยู่ในไฟล์นั้น
#include <WiFi.h>  // settings screen scans for SSIDs — หน้าตั้งค่าใช้สแกนหา WiFi

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
  lv_disp_flush_ready(disp);
}

static void touchCb(lv_indev_drv_t*, lv_indev_data_t* data) {
  // [TH] อ่านตำแหน่งสัมผัสแล้วแปลงพิกัดดิบของทัชสกรีนเป็นพิกัดหน้าจอ
  static bool wasDown = false;
  uint16_t rx, ry;
  if (touchBBRead(rx, ry)) {
    // The old driver applied setRotation(1) itself and handed back landscape
    // coordinates; these are unrotated panel counts, so the swap and the flip
    // live here now. Panel Y grows towards screen X, and panel X grows the
    // opposite way from screen Y.
    // [TH] ไดรเวอร์เดิมหมุนภาพให้เองแล้วคืนพิกัดแนวนอนมา ค่าที่ได้ตรงนี้เป็นค่าดิบยังไม่หมุน
    //      จึงต้องสลับแกนและกลับด้านเอง: แกน Y ของแผงวิ่งไปทางแกน X ของจอ ส่วนแกน X วิ่งกลับทางกับแกน Y
    int sx = map(ry, 240, 3800, 0, 320);
    int sy = map(rx, 3700, 200, 0, 240);
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
static lv_obj_t *lblCode, *btnGear, *btnData, *imgLogo;

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

void uiSetCallbacks(void (*onOpenSettings)(), void (*onOpenData)()) {
  cbSettings = onOpenSettings;
  cbData = onOpenData;
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
  // No card behind it: logo_img.c is generated with -Tint f2f5f9, so the artwork
  // is already near-white and reads directly on the navy background. The source
  // PNG is dark navy (~#002040) and would otherwise vanish into it.
  imgLogo = lv_img_create(scrMain);
  lv_img_set_src(imgLogo, &logo_img);
  lv_obj_align(imgLogo, LV_ALIGN_CENTER, 0, -6);  // full-screen idle branding; nothing below it now
                                                  // [TH] โลโก้ตอนว่างเต็มจอ ไม่มีปุ่มอยู่ข้างล่างแล้ว

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
  lv_obj_align(lblCode, LV_ALIGN_BOTTOM_MID, 0, -6);

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
    case Phase::Fault: return "สวิตช์ผิดพลาด";
    default: return "พร้อม";
  }
}

void uiUpdateMain(bool running, Phase phase, uint32_t elapsedSec, uint16_t reps,
                  bool wifiUp, bool serverUp, bool linkUp) {
  // Repainting identical text still copies the string and invalidates the area,
  // so an idle screen would redraw four times a second for nothing.
  // [TH] วาดข้อความเดิมซ้ำก็ยังกินงาน จึงข้ามการอัปเดตถ้าค่าไม่เปลี่ยน กันจอวาดซ้ำ 4 ครั้ง/วินาทีเปล่าๆ
  static bool first = true;
  static bool pRunning, pWifi, pServer, pLink;
  static Phase pPhase;
  static uint32_t pElapsed;
  static uint16_t pReps;
  static uint32_t phaseTimerStartMs = 0;  // display's own millis() at the last phase edge
  if (!first && running == pRunning && phase == pPhase && elapsedSec == pElapsed &&
      reps == pReps && wifiUp == pWifi && serverUp == pServer && linkUp == pLink)
    return;
  // The wire only carries total session elapsed, not per-phase, so the in-phase
  // timer below is clocked locally from the phase edge. The mirrored 250 ms
  // UI/state refresh keeps the drift within a tick.
  // [TH] สายส่งแค่เวลารวมของรอบ ไม่ใช่เวลาต่อสถานะ จึงจับเวลาเองฝั่งจอจากขอบที่สถานะเปลี่ยน
  //      คลาดเคลื่อนไม่เกินหนึ่งรอบรีเฟรช (250 ms) เพราะรีเฟรชจอกับสถานะมาที่ความถี่ใกล้กัน
  if (first || phase != pPhase) phaseTimerStartMs = millis();
  first = false;
  pRunning = running; pPhase = phase; pElapsed = elapsedSec; pReps = reps;
  pWifi = wifiUp; pServer = serverUp; pLink = linkUp;

  uiRunning = running && linkUp;

  // A session can start from a server command while the user is on the settings
  // screen (reachable only when idle, but the state can change underneath them).
  // That screen has no STOP control, so force back to the main screen where STOP
  // is one tap away. Pending edits are discarded on purpose: STOP beats editing.
  if (uiRunning) leaveSubScreenForRun();

  lv_label_set_text_fmt(lblClock, "%02lu:%02lu", (unsigned long)(elapsedSec / 60),
                        (unsigned long)(elapsedSec % 60));
  lv_label_set_text_fmt(lblReps, "%u รอบ", reps);

  bool fault = (phase == Phase::Fault);

  // Logo whenever the machine is idle. Link state deliberately does NOT gate it:
  // a display with no controller attached is the most common idle case, and
  // hiding the branding there left no state that ever showed it. Link trouble is
  // reported in the status corner instead.
  bool showLogo = !running && !fault;
  if (showLogo) {
    lv_obj_clear_flag(imgLogo, LV_OBJ_FLAG_HIDDEN);
    lv_obj_fade_in(imgLogo, 200, 0);
    lv_obj_add_flag(lblClock, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(lblReps, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(lblPhase, LV_OBJ_FLAG_HIDDEN);  // "พร้อม" adds nothing next to the logo
  } else {
    lv_obj_add_flag(imgLogo, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(lblClock, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(lblReps, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(lblPhase, LV_OBJ_FLAG_HIDDEN);
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

  // While a session runs, STOP must stay one tap away - the settings screen has
  // no stop control, and the controller's UART watchdog will not help because
  // the display keeps pinging from in there.
  if (running) lv_obj_add_state(btnGear, LV_STATE_DISABLED);
  else lv_obj_clear_state(btnGear, LV_STATE_DISABLED);

  if (linkUp) {
    lv_label_set_text_fmt(lblStatus, "%s %s", wifiUp ? LV_SYMBOL_WIFI : "--",
                          serverUp ? LV_SYMBOL_OK : LV_SYMBOL_CLOSE);
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

static lv_obj_t *scrSettings, *taSsid, *taPass, *taServer, *taCode, *kb, *ddScan;
static lv_obj_t *spinMax, *spinHold, *spinRest, *btnSave;
static void (*cbSave)(const DeviceSettings&) = nullptr;
static bool settingsKnown = false;  // has the controller ever sent its settings?

void uiSetSettingsSaved(void (*onSave)(const DeviceSettings&)) { cbSave = onSave; }

static void applySaveEnabled() {
  if (!btnSave) return;
  if (settingsKnown) {
    lv_obj_clear_state(btnSave, LV_STATE_DISABLED);
    lv_obj_set_style_bg_color(btnSave, lv_color_hex(COL_BG), 0);
  } else {
    lv_obj_add_state(btnSave, LV_STATE_DISABLED);
    lv_obj_set_style_bg_color(btnSave, lv_color_hex(COL_MUTED), LV_STATE_DISABLED);
  }
}

void uiSetSettingsKnown(bool known) {
  settingsKnown = known;
  applySaveEnabled();
}

static void hideKeyboard() {
  if (!kb) return;
  lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
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

static void taFocused(lv_event_t* e) {
  lv_obj_t* ta = (lv_obj_t*)lv_event_get_target(e);
  lv_keyboard_set_textarea(kb, ta);
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
  s.holdSec = (uint16_t)lv_spinbox_get_value(spinHold);
  s.restSec = (uint16_t)lv_spinbox_get_value(spinRest);
  if (cbSave) cbSave(s);
  hideKeyboard();
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

  // The textarea keeps the default Montserrat: everything typed into it (SSID,
  // password, server URL, device code) is Latin, and so is the on-screen keyboard.
  lv_obj_t* ta = lv_textarea_create(parent);
  lv_textarea_set_one_line(ta, true);
  lv_textarea_set_placeholder_text(ta, placeholder);
  lv_textarea_set_password_mode(ta, password);
  lv_obj_set_width(ta, lv_pct(100));
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
static lv_obj_t* makeSpinRow(lv_obj_t* parent, const char* label, int val,
                             int lo, int hi) {
  lv_obj_t* row = lv_obj_create(parent);
  lv_obj_set_size(row, lv_pct(100), 42);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_set_style_pad_all(row, 0, 0);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  // Let taps on the row's own background reach the column's dismiss handler. The
  // -/+ buttons are children without this flag, so their clicks stay theirs.
  lv_obj_add_flag(row, LV_OBJ_FLAG_EVENT_BUBBLE);

  lv_obj_t* l = lv_label_create(row);
  lv_obj_set_style_text_font(l, &thai14, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(COL_MUTED), 0);
  lv_label_set_text(l, label);
  lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);

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

  lv_obj_t* col = lv_obj_create(scrSettings);
  lv_obj_set_size(col, lv_pct(100), 196);
  lv_obj_align(col, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(col, 8, 0);
  lv_obj_set_style_pad_row(col, 4, 0);
  lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(col, 0, 0);
  lv_obj_add_event_cb(col, tapHidesKeyboard, LV_EVENT_CLICKED, nullptr);
  lv_obj_add_event_cb(col, scrollHidesKeyboard, LV_EVENT_SCROLL_BEGIN, nullptr);

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
        char sel[64];
        lv_dropdown_get_selected_str((lv_obj_t*)lv_event_get_target(e), sel, sizeof(sel));
        if (strcmp(sel, SCAN_PLACEHOLDER) != 0) lv_textarea_set_text(taSsid, sel);
      },
      LV_EVENT_VALUE_CHANGED, nullptr);

  makeField(col, "เครือข่าย WiFi", "SSID", false, &taSsid);
  makeField(col, "รหัสผ่าน WiFi", "password", true, &taPass);
  makeField(col, "เซิร์ฟเวอร์", "http://192.168.0.12:8000", false, &taServer);
  makeField(col, "รหัสอุปกรณ์", "KNEE-01", false, &taCode);

  // MAX TRAVEL is the safety cap the whole switch-timeout story rests on: it is
  // how long a relay may stay energized with no end-of-travel switch closing.
  // Its ceiling is deliberately far lower than HOLD/REST.
  // [TH] MAX TRAVEL คือเพดานความปลอดภัยหลัก: เวลาที่รีเลย์จ่ายไฟได้โดยไม่มีสวิตช์ปลายทางปิด
  //      เพดานของมันตั้งไว้ต่ำกว่า HOLD/REST มากโดยตั้งใจ
  spinMax = makeSpinRow(col, "เวลาเดินทางสูงสุด (วิ)", 8, 1, 20);
  spinHold = makeSpinRow(col, "ค้าง (วิ)", 10, 1, 60);
  spinRest = makeSpinRow(col, "พัก (วิ)", 10, 1, 60);

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
}

void uiOpenSettings(const DeviceSettings& cur) {
  if (!scrSettings) buildSettings();

  lv_textarea_set_text(taSsid, cur.wifiSsid.c_str());
  lv_textarea_set_text(taPass, cur.wifiPass.c_str());
  lv_textarea_set_text(taServer, cur.serverUrl.c_str());
  lv_textarea_set_text(taCode, cur.deviceCode.c_str());
  lv_spinbox_set_value(spinMax, cur.maxTravelSec);
  lv_spinbox_set_value(spinHold, cur.holdSec);
  lv_spinbox_set_value(spinRest, cur.restSec);

  hideKeyboard();
  lv_obj_add_flag(ddScan, LV_OBJ_FLAG_HIDDEN);  // stale scan results
  lv_obj_scroll_to_y(lv_obj_get_parent(taSsid), 0, LV_ANIM_OFF);

  lv_scr_load(scrSettings);
}

// ---------------------------------------------------------------- data screen

// Roster access is injected, so this file never includes SD or touches the card.
// [TH] ฟังก์ชันเข้าถึงทะเบียนถูกส่งเข้ามา ไฟล์นี้จึงไม่ต้องรู้จักการ์ด SD เลย
static int (*cbCount)() = nullptr;
static const Patient* (*cbGet)(int) = nullptr;
static bool (*cbAdd)(const char*, const char*, int) = nullptr;
static void (*cbSetActive)(int) = nullptr;

static lv_obj_t *scrData, *lblDataTitle, *lstPatients;
static lv_obj_t *scrAdd, *taPCode, *taPName, *taPAge, *kbAdd;

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
  lv_keyboard_set_textarea(kbAdd, nullptr);
}

static void addFieldFocused(lv_event_t* e) {
  lv_obj_t* ta = (lv_obj_t*)lv_event_get_target(e);
  lv_keyboard_set_textarea(kbAdd, ta);
  lv_obj_clear_flag(kbAdd, LV_OBJ_FLAG_HIDDEN);
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
  lv_obj_add_event_cb(ta, addFieldFocused, LV_EVENT_FOCUSED, nullptr);
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

  lv_obj_t* col = lv_obj_create(scrAdd);
  lv_obj_set_size(col, lv_pct(100), 80);
  lv_obj_align(col, LV_ALIGN_TOP_MID, 0, 44);
  lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(col, 8, 0);
  lv_obj_set_style_pad_row(col, 4, 0);
  lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(col, 0, 0);

  // The on-screen keyboard is Latin-only, so names typed here are Latin. Thai
  // names can still be put in /patients.csv from a PC — the screen renders them
  // fine, it is only the keyboard that cannot produce them.
  // [TH] แป้นพิมพ์บนจอมีแต่ตัวอักษรละติน ชื่อที่พิมพ์ตรงนี้จึงเป็นละติน
  //      ถ้าต้องการชื่อไทย ให้แก้ไฟล์ /patients.csv จากคอมพิวเตอร์ จอแสดงผลภาษาไทยได้ปกติ
  makeAddField(col, "รหัสคนไข้ (P001)", nullptr, &taPCode);
  makeAddField(col, "นาม", nullptr, &taPName);
  makeAddField(col, "อายุ", "0123456789", &taPAge);

  kbAdd = lv_keyboard_create(scrAdd);
  lv_obj_set_size(kbAdd, lv_pct(100), 120);
  lv_obj_align(kbAdd, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_add_event_cb(kbAdd, [](lv_event_t*) { hideAddKeyboard(); }, LV_EVENT_READY, nullptr);
  lv_obj_add_event_cb(kbAdd, [](lv_event_t*) { hideAddKeyboard(); }, LV_EVENT_CANCEL, nullptr);
  lv_obj_add_flag(kbAdd, LV_OBJ_FLAG_HIDDEN);
}

static void openAddForm() {
  if (!scrAdd) buildAddForm();
  lv_textarea_set_text(taPCode, "");
  lv_textarea_set_text(taPName, "");
  lv_textarea_set_text(taPAge, "");
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

  lblFinWho = lv_label_create(col);
  lv_obj_set_style_text_font(lblFinWho, &thai20, 0);

  lblFinStat = lv_label_create(col);
  lv_obj_set_style_text_font(lblFinStat, &lv_font_montserrat_28, 0);

  lblFinFault = lv_label_create(col);
  lv_obj_set_style_text_font(lblFinFault, &thai20, 0);
  lv_obj_set_style_text_color(lblFinFault, lv_color_hex(COL_FAULT), 0);

  lblFinSaved = lv_label_create(col);
  lv_obj_set_style_text_font(lblFinSaved, &thai14, 0);
  lv_obj_set_style_text_color(lblFinSaved, lv_color_hex(COL_MUTED), 0);

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

void uiLoop() { lv_timer_handler(); }




