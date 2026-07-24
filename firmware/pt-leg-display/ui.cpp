// การสร้างส่วนติดต่อผู้ใช้ทั้งหมดบนจอสัมผัส CYD ด้วย LVGL
// ประกอบด้วยหน้าจอหลัก (นาฬิกา/จำนวนครั้ง/สถานะ แสดงผลอย่างเดียว) และหน้าตั้งค่า (WiFi/เซิร์ฟเวอร์/เวลารอบ)
// All LVGL UI for the CYD touchscreen: output-only main screen and settings screen.
#include "ui.h"
#include <Arduino.h>  // millis() for the in-phase timer — ใช้ millis() จับเวลาในสถานะ
#include <lvgl.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <WiFi.h>  // settings screen scans for SSIDs — หน้าตั้งค่าใช้สแกนหา WiFi

// CYD touch pins (separate SPI bus) — ขาทัชสกรีนของ CYD (ใช้บัส SPI แยกต่างหาก)
#define XPT2046_IRQ 36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK 25
#define XPT2046_CS 33

// App palette (matches the phone app's v4 rebrand)
// [TH] ชุดสีของแอป ให้เข้ากับธีมของแอปบนมือถือ
#define COL_BG 0x1a2b47      // navy-dark  - screen background
#define COL_SURFACE 0x24395e // navy       - cards, inputs
#define COL_TEXT 0xf2f5f9    // near-white - primary text
#define COL_MUTED 0xc6ccd6   // silver     - secondary text
#define COL_FAULT 0xb00020   // red        - fault

static TFT_eSPI tft;
static SPIClass touchSpi(VSPI);
static XPT2046_Touchscreen touch(XPT2046_CS, XPT2046_IRQ);

static lv_disp_draw_buf_t drawBuf;
// A taller buffer means far fewer flush round-trips per frame, which is most of
// the sluggishness on this panel. 240x100x2B = 48 KB - too big to sit in static
// DRAM next to LVGL's own 48 KB pool, so it comes off the heap in uiBegin().
#define DRAW_BUF_LINES 100
static lv_color_t* buf = nullptr;

static void buildMain();
static void leaveSettingsForStop();  // defined with the settings screen below

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
  if (touch.tirqTouched() && touch.touched()) {
    TS_Point p = touch.getPoint();
    // raw calibration for CYD landscape; tune on the bench if offset
    // [TH] ค่าคาลิเบรตแบบดิบสำหรับ CYD แนวนอน ปรับจูนหน้างานถ้าตำแหน่งเพี้ยน
    data->point.x = map(p.x, 200, 3700, 0, 320);
    data->point.y = map(p.y, 240, 3800, 0, 240);
    data->state = LV_INDEV_STATE_PRESSED;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
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
  touchSpi.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  touch.begin(touchSpi);
  touch.setRotation(1);

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

  buildMain();
}

// ---------------------------------------------------------------- main screen

static lv_obj_t *scrMain, *lblClock, *lblReps, *lblPhase, *lblPhaseTimer, *lblStatus;
static lv_obj_t *lblCode, *btnGear, *imgLogo;

LV_IMG_DECLARE(logo_img)  // firmware/pt-leg-display/logo_img.c
static void (*cbSettings)() = nullptr;
static bool uiRunning = false;

void uiSetCallbacks(void (*onOpenSettings)()) {
  cbSettings = onOpenSettings;
}

static void gearClicked(lv_event_t*) {
  if (uiRunning) return;  // STOP must stay reachable during a session
                          // [TH] ห้ามเข้าหน้าตั้งค่าระหว่างทำงาน เพื่อให้ปุ่ม STOP กดถึงตลอด
  if (cbSettings) cbSettings();
}

static void buildMain() {
  // [TH] สร้างหน้าจอหลัก: โลโก้ตอนว่าง, นาฬิกา, จำนวนครั้ง, ชื่อสถานะ (แสดงผลอย่างเดียว) และปุ่มตั้งค่า
  scrMain = lv_obj_create(nullptr);
  styleScreen(scrMain);

  lblStatus = lv_label_create(scrMain);
  lv_obj_set_style_text_color(lblStatus, lv_color_hex(COL_MUTED), 0);
  lv_label_set_text(lblStatus, "");
  lv_obj_align(lblStatus, LV_ALIGN_TOP_LEFT, 8, 10);

  btnGear = makeBtn(scrMain, LV_SYMBOL_SETTINGS, COL_SURFACE,
                    &lv_font_montserrat_20, gearClicked);
  lv_obj_set_size(btnGear, 48, 40);
  lv_obj_align(btnGear, LV_ALIGN_TOP_RIGHT, -8, 4);

  // Idle branding. Lives in flash, so it costs no RAM; it is swapped out for the
  // session stats the moment the machine starts moving.
  // No card behind it: logo_img.c is generated with -Tint f2f5f9, so the artwork
  // is already near-white and reads directly on the navy background. The source
  // PNG is dark navy (~#002040) and would otherwise vanish into it.
  imgLogo = lv_img_create(scrMain);
  lv_img_set_src(imgLogo, &logo_img);
  lv_obj_align(imgLogo, LV_ALIGN_CENTER, 0, -6);  // full-screen idle branding; nothing below it now
                                                  // [TH] โลโก้ตอนว่างเต็มจอ ไม่มีปุ่มอยู่ข้างล่างแล้ว

  lblClock = lv_label_create(scrMain);
  lv_obj_set_style_text_font(lblClock, &lv_font_montserrat_48, 0);
  lv_label_set_text(lblClock, "00:00");
  lv_obj_align(lblClock, LV_ALIGN_TOP_MID, 0, 44);

  lblReps = lv_label_create(scrMain);
  lv_obj_set_style_text_font(lblReps, &lv_font_montserrat_28, 0);
  lv_obj_set_style_text_color(lblReps, lv_color_hex(COL_MUTED), 0);
  lv_label_set_text(lblReps, "0 REPS");
  lv_obj_align(lblReps, LV_ALIGN_TOP_MID, 0, 100);

  lblPhase = lv_label_create(scrMain);
  lv_obj_set_style_text_font(lblPhase, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(lblPhase, lv_color_hex(COL_MUTED), 0);
  lv_label_set_text(lblPhase, "READY");
  lv_obj_align(lblPhase, LV_ALIGN_TOP_MID, 0, 134);

  // In-phase timer: seconds spent in the current HOLD/REST, so a patient can see
  // progress toward the configured hold/rest duration. Blank outside those two
  // phases - LIFTING/LOWERING/READY/etc already read fine from lblPhase alone.
  lblPhaseTimer = lv_label_create(scrMain);
  lv_obj_set_style_text_font(lblPhaseTimer, &lv_font_montserrat_28, 0);
  lv_obj_set_style_text_color(lblPhaseTimer, lv_color_hex(COL_MUTED), 0);
  lv_label_set_text(lblPhaseTimer, "");
  lv_obj_align(lblPhaseTimer, LV_ALIGN_TOP_MID, 0, 164);

  lblCode = lv_label_create(scrMain);
  lv_obj_set_style_text_color(lblCode, lv_color_hex(COL_MUTED), 0);
  lv_label_set_text(lblCode, "");
  lv_obj_align(lblCode, LV_ALIGN_BOTTOM_MID, 0, -6);

  lv_scr_load(scrMain);
}

void uiSetDeviceCode(const char* code) {
  lv_label_set_text_fmt(lblCode, "CODE: %s", code);
}

// [TH] แปลงสถานะเป็นข้อความบนจอ (ยังคงเป็นภาษาอังกฤษบนหน้าจออุปกรณ์)
static const char* phaseName(Phase p) {
  switch (p) {
    case Phase::Lift: return "LIFTING";
    case Phase::Hold: return "HOLD";
    case Phase::Lower: return "LOWERING";
    case Phase::Rest: return "REST";
    case Phase::SafeLower: return "STOPPING";
    case Phase::Fault: return "SWITCH FAULT";
    default: return "READY";
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
  if (uiRunning) leaveSettingsForStop();

  lv_label_set_text_fmt(lblClock, "%02lu:%02lu", (unsigned long)(elapsedSec / 60),
                        (unsigned long)(elapsedSec % 60));
  lv_label_set_text_fmt(lblReps, "%u REPS", reps);

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
    lv_obj_add_flag(lblPhase, LV_OBJ_FLAG_HIDDEN);  // "READY" adds nothing next to the logo
  } else {
    lv_obj_add_flag(imgLogo, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(lblClock, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(lblReps, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(lblPhase, LV_OBJ_FLAG_HIDDEN);
  }
  // With the link down the cached state is stale and the buttons do nothing,
  // so say so rather than presenting a frozen timer as if it were live.
  lv_label_set_text(lblPhase, linkUp ? phaseName(phase) : "NO LINK");
  bool showPhaseTimer = linkUp && (phase == Phase::Hold || phase == Phase::Rest);
  if (showPhaseTimer) {
    uint32_t inPhaseSec = (millis() - phaseTimerStartMs) / 1000;
    lv_label_set_text_fmt(lblPhaseTimer, "%lus", (unsigned long)inPhaseSec);
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
    lv_label_set_text(lblStatus, LV_SYMBOL_WARNING " NO LINK");
  }
}

// ------------------------------------------------------------ settings screen

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

// Called from uiUpdateMain when a session is running: if the settings screen is
// on top it has no STOP, so drop back to the main screen.
static void leaveSettingsForStop() {
  if (scrSettings && lv_scr_act() == scrSettings) {
    hideKeyboard();
    lv_scr_load(scrMain);
  }
}

static void taFocused(lv_event_t* e) {
  lv_obj_t* ta = (lv_obj_t*)lv_event_get_target(e);
  lv_keyboard_set_textarea(kb, ta);
  lv_obj_clear_flag(kb, LV_OBJ_FLAG_HIDDEN);
  lv_obj_scroll_to_view(ta, LV_ANIM_ON);  // keep the focused field above the keys
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
  String opts = (n > 0) ? "(pick network)" : "(none found - retry)";
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
  lv_obj_set_style_text_color(l, lv_color_hex(COL_MUTED), 0);
  lv_label_set_text(l, label);

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

  lv_obj_t* l = lv_label_create(row);
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

  lv_obj_t* back = makeBtn(bar, LV_SYMBOL_LEFT " BACK", COL_BG,
                           &lv_font_montserrat_20, backClicked);
  lv_obj_set_size(back, 110, 34);
  lv_obj_align(back, LV_ALIGN_LEFT_MID, -8, 0);

  btnSave = makeBtn(bar, "SAVE", COL_BG, &lv_font_montserrat_20, saveClicked);
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

  lv_obj_t* scan = makeBtn(col, LV_SYMBOL_WIFI " SCAN NETWORKS", COL_SURFACE,
                           &lv_font_montserrat_20, scanClicked);
  lv_obj_set_size(scan, lv_pct(100), 38);

  ddScan = lv_dropdown_create(col);
  lv_obj_set_width(ddScan, lv_pct(100));
  lv_dropdown_set_options(ddScan, "(pick network)");
  lv_obj_set_style_bg_color(ddScan, lv_color_hex(COL_SURFACE), 0);
  lv_obj_set_style_text_color(ddScan, lv_color_hex(COL_TEXT), 0);
  lv_obj_set_style_border_width(ddScan, 0, 0);
  lv_obj_add_flag(ddScan, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_event_cb(
      ddScan,
      [](lv_event_t* e) {
        char sel[64];
        lv_dropdown_get_selected_str((lv_obj_t*)lv_event_get_target(e), sel, sizeof(sel));
        if (strcmp(sel, "(pick network)") != 0) lv_textarea_set_text(taSsid, sel);
      },
      LV_EVENT_VALUE_CHANGED, nullptr);

  makeField(col, "WiFi network", "SSID", false, &taSsid);
  makeField(col, "WiFi password", "password", true, &taPass);
  makeField(col, "Server", "http://192.168.0.12:8000", false, &taServer);
  makeField(col, "Device code", "KNEE-01", false, &taCode);

  // MAX TRAVEL is the safety cap the whole switch-timeout story rests on: it is
  // how long a relay may stay energized with no end-of-travel switch closing.
  // Its ceiling is deliberately far lower than HOLD/REST.
  // [TH] MAX TRAVEL คือเพดานความปลอดภัยหลัก: เวลาที่รีเลย์จ่ายไฟได้โดยไม่มีสวิตช์ปลายทางปิด
  //      เพดานของมันตั้งไว้ต่ำกว่า HOLD/REST มากโดยตั้งใจ
  spinMax = makeSpinRow(col, "MAX TRAVEL s", 8, 1, 20);
  spinHold = makeSpinRow(col, "HOLD s", 10, 1, 60);
  spinRest = makeSpinRow(col, "REST s", 10, 1, 60);

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

void uiLoop() { lv_timer_handler(); }




