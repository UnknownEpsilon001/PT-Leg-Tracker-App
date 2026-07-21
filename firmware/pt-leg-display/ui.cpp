#include "ui.h"
#include <lvgl.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <WiFi.h>  // settings screen scans for SSIDs

// CYD touch pins (separate SPI bus)
#define XPT2046_IRQ 36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK 25
#define XPT2046_CS 33

// App palette (matches the phone app's v4 rebrand)
#define COL_BG 0x1a2b47      // navy-dark  — screen background
#define COL_SURFACE 0x24395e // navy       — cards, inputs
#define COL_TEXT 0xf2f5f9    // near-white — primary text
#define COL_MUTED 0xc6ccd6   // silver     — secondary text
#define COL_STOP 0x8c2f39    // maroon     — stop
#define COL_FAULT 0xb00020   // red        — fault

static TFT_eSPI tft;
static SPIClass touchSpi(VSPI);
static XPT2046_Touchscreen touch(XPT2046_CS, XPT2046_IRQ);

static lv_disp_draw_buf_t drawBuf;
// A taller buffer means far fewer flush round-trips per frame, which is most of
// the sluggishness on this panel. 240x100x2B = 48 KB — too big to sit in static
// DRAM next to LVGL's own 48 KB pool, so it comes off the heap in uiBegin().
#define DRAW_BUF_LINES 100
static lv_color_t* buf = nullptr;

static void buildMain();

static void flushCb(lv_disp_drv_t* disp, const lv_area_t* area, lv_color_t* pixels) {
  uint32_t w = area->x2 - area->x1 + 1, h = area->y2 - area->y1 + 1;
  tft.startWrite();
  tft.setAddrWindow(area->x1, area->y1, w, h);
  // swap=false: lv_conf.h sets LV_COLOR_16_SWAP 1, so LVGL already hands us
  // byte-swapped pixels. Swapping again here mangles every colour and shows up
  // as fringed outlines around anti-aliased text.
  tft.pushColors((uint16_t*)&pixels->full, w * h, false);
  tft.endWrite();
  lv_disp_flush_ready(disp);
}

static void touchCb(lv_indev_drv_t*, lv_indev_data_t* data) {
  if (touch.tirqTouched() && touch.touched()) {
    TS_Point p = touch.getPoint();
    // raw calibration for CYD landscape; tune on the bench if offset
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
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* l = lv_label_create(b);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(COL_TEXT), 0);
  lv_label_set_text(l, text);
  lv_obj_center(l);
  return b;
}

void uiBegin() {
  tft.init();
  tft.setRotation(1); // landscape 320x240
  touchSpi.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  touch.begin(touchSpi);
  touch.setRotation(1);

  lv_init();
  uint32_t lines = DRAW_BUF_LINES;
  buf = (lv_color_t*)heap_caps_malloc(240 * lines * sizeof(lv_color_t), MALLOC_CAP_DMA);
  while (!buf && lines > 10) {  // degrade rather than boot-loop on a tight heap
    lines /= 2;
    buf = (lv_color_t*)heap_caps_malloc(240 * lines * sizeof(lv_color_t), MALLOC_CAP_DMA);
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

static lv_obj_t *scrMain, *lblClock, *lblReps, *lblPhase, *lblStatus, *btnGo, *lblGo;
static lv_obj_t* lblCode;
static void (*cbStart)() = nullptr;
static void (*cbStop)() = nullptr;
static void (*cbSettings)() = nullptr;
static bool uiRunning = false;

void uiSetCallbacks(void (*onStart)(), void (*onStop)(), void (*onOpenSettings)()) {
  cbStart = onStart;
  cbStop = onStop;
  cbSettings = onOpenSettings;
}

static void goClicked(lv_event_t*) {
  if (uiRunning) {
    if (cbStop) cbStop();
  } else {
    if (cbStart) cbStart();  // also clears a latched FAULT on the controller
  }
}

static void gearClicked(lv_event_t*) {
  if (cbSettings) cbSettings();
}

static void buildMain() {
  scrMain = lv_obj_create(nullptr);
  styleScreen(scrMain);

  lblStatus = lv_label_create(scrMain);
  lv_obj_set_style_text_color(lblStatus, lv_color_hex(COL_MUTED), 0);
  lv_label_set_text(lblStatus, "");
  lv_obj_align(lblStatus, LV_ALIGN_TOP_LEFT, 8, 10);

  lv_obj_t* gear = makeBtn(scrMain, LV_SYMBOL_SETTINGS, COL_SURFACE,
                           &lv_font_montserrat_20, gearClicked);
  lv_obj_set_size(gear, 48, 40);
  lv_obj_align(gear, LV_ALIGN_TOP_RIGHT, -8, 4);

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

  btnGo = makeBtn(scrMain, "START", COL_SURFACE, &lv_font_montserrat_28, goClicked);
  lv_obj_set_size(btnGo, 260, 56);
  lv_obj_align(btnGo, LV_ALIGN_BOTTOM_MID, 0, -24);
  lblGo = lv_obj_get_child(btnGo, 0);

  lblCode = lv_label_create(scrMain);
  lv_obj_set_style_text_color(lblCode, lv_color_hex(COL_MUTED), 0);
  lv_label_set_text(lblCode, "");
  lv_obj_align(lblCode, LV_ALIGN_BOTTOM_MID, 0, -6);

  lv_scr_load(scrMain);
}

void uiSetDeviceCode(const char* code) {
  lv_label_set_text_fmt(lblCode, "CODE: %s", code);
}

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
                  bool wifiUp, bool serverUp) {
  uiRunning = running;
  lv_label_set_text_fmt(lblClock, "%02lu:%02lu", (unsigned long)(elapsedSec / 60),
                        (unsigned long)(elapsedSec % 60));
  lv_label_set_text_fmt(lblReps, "%u REPS", reps);
  lv_label_set_text(lblPhase, phaseName(phase));

  bool fault = (phase == Phase::Fault);
  lv_label_set_text(lblGo, fault ? "RESTART" : (running ? "STOP" : "START"));
  uint32_t goCol = fault ? COL_FAULT : (running ? COL_STOP : COL_SURFACE);
  lv_obj_set_style_bg_color(btnGo, lv_color_hex(goCol), 0);
  lv_obj_set_style_text_color(lblPhase,
                              lv_color_hex(fault ? COL_FAULT : COL_MUTED), 0);

  lv_label_set_text_fmt(lblStatus, "%s %s", wifiUp ? LV_SYMBOL_WIFI : "--",
                        serverUp ? LV_SYMBOL_OK : LV_SYMBOL_CLOSE);
}

// ------------------------------------------------------------ settings screen

static lv_obj_t *scrSettings, *taSsid, *taPass, *taServer, *taCode, *kb, *ddScan;
static lv_obj_t *spinMax, *spinHold, *spinRest;
static void (*cbSave)(const DeviceSettings&) = nullptr;

void uiSetSettingsSaved(void (*onSave)(const DeviceSettings&)) { cbSave = onSave; }

static void hideKeyboard() {
  lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
  lv_keyboard_set_textarea(kb, nullptr);
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
  int n = WiFi.scanNetworks();
  String opts = "(pick network)";
  for (int i = 0; i < n && i < 12; i++) opts += String("\n") + WiFi.SSID(i);
  lv_dropdown_set_options(ddScan, opts.c_str());
  lv_obj_clear_flag(ddScan, LV_OBJ_FLAG_HIDDEN);
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

static lv_obj_t* makeSpin(lv_obj_t* parent, const char* label, int val) {
  lv_obj_t* box = lv_obj_create(parent);
  lv_obj_set_size(box, 96, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(box, 0, 0);
  lv_obj_set_style_pad_all(box, 2, 0);

  lv_obj_t* l = lv_label_create(box);
  lv_obj_set_style_text_color(l, lv_color_hex(COL_MUTED), 0);
  lv_label_set_text(l, label);

  lv_obj_t* sp = lv_spinbox_create(box);
  lv_spinbox_set_range(sp, 1, 60);
  lv_spinbox_set_value(sp, val);
  lv_obj_set_width(sp, 88);
  lv_obj_set_style_bg_color(sp, lv_color_hex(COL_SURFACE), 0);
  lv_obj_set_style_text_color(sp, lv_color_hex(COL_TEXT), 0);
  lv_obj_set_style_border_width(sp, 0, 0);
  return sp;
}

// Built exactly once. Rebuilding it per open leaked ~8.9 KB of LVGL's 48 KB
// pool every time (screens are not freed when another is loaded), and the fifth
// open hit LV_ASSERT_MALLOC, whose handler is `while(1)` — a silent hard freeze
// with a dead touchscreen and no reboot.
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

  lv_obj_t* save = makeBtn(bar, "SAVE", COL_BG, &lv_font_montserrat_20, saveClicked);
  lv_obj_set_size(save, 90, 34);
  lv_obj_align(save, LV_ALIGN_RIGHT_MID, 8, 0);

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

  lv_obj_t* row = lv_obj_create(col);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_set_style_pad_all(row, 0, 0);
  spinMax = makeSpin(row, "MAX TRAVEL", 8);
  spinHold = makeSpin(row, "HOLD", 10);
  spinRest = makeSpin(row, "REST", 10);

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
