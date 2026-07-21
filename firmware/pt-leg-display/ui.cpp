#include "ui.h"
#include <lvgl.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <WiFi.h>  // settings screen scans for SSIDs

LV_FONT_DECLARE(font_thai)

// CYD touch pins (separate SPI bus)
#define XPT2046_IRQ 36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK 25
#define XPT2046_CS 33

static TFT_eSPI tft;
static SPIClass touchSpi(VSPI);
static XPT2046_Touchscreen touch(XPT2046_CS, XPT2046_IRQ);

static lv_disp_draw_buf_t drawBuf;
static lv_color_t buf[240 * 40];

static void buildMain();

static void flushCb(lv_disp_drv_t* disp, const lv_area_t* area, lv_color_t* pixels) {
  uint32_t w = area->x2 - area->x1 + 1, h = area->y2 - area->y1 + 1;
  tft.startWrite();
  tft.setAddrWindow(area->x1, area->y1, w, h);
  tft.pushColors((uint16_t*)&pixels->full, w * h, true);
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

void uiBegin() {
  tft.init();
  tft.setRotation(1); // landscape 320x240
  touchSpi.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  touch.begin(touchSpi);
  touch.setRotation(1);

  lv_init();
  lv_disp_draw_buf_init(&drawBuf, buf, nullptr, 240 * 40);

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

  lblClock = lv_label_create(scrMain);
  lv_obj_set_style_text_font(lblClock, &lv_font_montserrat_48, 0);
  lv_label_set_text(lblClock, "00:00");
  lv_obj_align(lblClock, LV_ALIGN_TOP_MID, 0, 18);

  lblReps = lv_label_create(scrMain);
  lv_obj_set_style_text_font(lblReps, &lv_font_montserrat_28, 0);
  lv_label_set_text(lblReps, "0");
  lv_obj_align(lblReps, LV_ALIGN_TOP_MID, 0, 74);

  lblPhase = lv_label_create(scrMain);
  lv_obj_set_style_text_font(lblPhase, &font_thai, 0);
  lv_label_set_text(lblPhase, "");
  lv_obj_align(lblPhase, LV_ALIGN_TOP_MID, 0, 108);

  btnGo = lv_btn_create(scrMain);
  lv_obj_set_size(btnGo, 200, 70);
  lv_obj_align(btnGo, LV_ALIGN_BOTTOM_MID, 0, -34);
  lv_obj_add_event_cb(btnGo, goClicked, LV_EVENT_CLICKED, nullptr);
  lblGo = lv_label_create(btnGo);
  lv_obj_set_style_text_font(lblGo, &font_thai, 0);
  lv_obj_center(lblGo);

  lv_obj_t* gear = lv_btn_create(scrMain);
  lv_obj_set_size(gear, 44, 44);
  lv_obj_align(gear, LV_ALIGN_TOP_RIGHT, -6, 6);
  lv_obj_add_event_cb(gear, gearClicked, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* gl = lv_label_create(gear);
  lv_label_set_text(gl, LV_SYMBOL_SETTINGS);
  lv_obj_center(gl);

  lblStatus = lv_label_create(scrMain);
  lv_label_set_text(lblStatus, "");
  lv_obj_align(lblStatus, LV_ALIGN_TOP_LEFT, 6, 6);

  lv_scr_load(scrMain);
}

static const char* phaseName(Phase p) {
  switch (p) {
    case Phase::Lift: return "ยกขา";
    case Phase::Hold: return "ค้างไว้";
    case Phase::Lower: return "ลดขา";
    case Phase::Rest: return "พัก";
    case Phase::SafeLower: return "กำลังหยุด";
    case Phase::Fault: return "สวิตช์ผิดพลาด";
    default: return "";
  }
}

void uiUpdateMain(bool running, Phase phase, uint32_t elapsedSec, uint16_t reps,
                  bool wifiUp, bool serverUp) {
  uiRunning = running;
  lv_label_set_text_fmt(lblClock, "%02lu:%02lu", (unsigned long)(elapsedSec / 60),
                        (unsigned long)(elapsedSec % 60));
  lv_label_set_text_fmt(lblReps, "x %u", reps);
  lv_label_set_text(lblPhase, phaseName(phase));

  bool fault = (phase == Phase::Fault);
  lv_label_set_text(lblGo, fault ? "เริ่มใหม่" : (running ? "หยุด" : "เริ่ม"));
  uint32_t goCol = fault ? 0xb00020 : (running ? 0x8c2f39 : 0x2e7d32);
  lv_obj_set_style_bg_color(btnGo, lv_color_hex(goCol), 0);
  lv_obj_set_style_text_color(lblPhase, lv_color_hex(fault ? 0xb00020 : 0x000000), 0);

  lv_label_set_text_fmt(lblStatus, "%s %s", wifiUp ? LV_SYMBOL_WIFI : "--",
                        serverUp ? LV_SYMBOL_OK : LV_SYMBOL_CLOSE);
}

// ------------------------------------------------------------ settings screen

static lv_obj_t *scrSettings, *taSsid, *taPass, *taServer, *kb;
static lv_obj_t *spinMax, *spinHold, *spinRest;
static void (*cbSave)(const DeviceSettings&) = nullptr;

void uiSetSettingsSaved(void (*onSave)(const DeviceSettings&)) { cbSave = onSave; }

static void taFocused(lv_event_t* e) {
  lv_keyboard_set_textarea(kb, (lv_obj_t*)lv_event_get_target(e));
  lv_obj_clear_flag(kb, LV_OBJ_FLAG_HIDDEN);
}

static void saveClicked(lv_event_t*) {
  DeviceSettings s;
  s.wifiSsid = lv_textarea_get_text(taSsid);
  s.wifiPass = lv_textarea_get_text(taPass);
  s.serverUrl = lv_textarea_get_text(taServer);
  s.maxTravelSec = (uint16_t)lv_spinbox_get_value(spinMax);
  s.holdSec = (uint16_t)lv_spinbox_get_value(spinHold);
  s.restSec = (uint16_t)lv_spinbox_get_value(spinRest);
  if (cbSave) cbSave(s);
  lv_scr_load(scrMain);
}

static lv_obj_t* makeTa(lv_obj_t* parent, const char* placeholder, bool password) {
  lv_obj_t* ta = lv_textarea_create(parent);
  lv_textarea_set_one_line(ta, true);
  lv_textarea_set_placeholder_text(ta, placeholder);
  lv_textarea_set_password_mode(ta, password);
  lv_obj_set_width(ta, lv_pct(100));
  lv_obj_add_event_cb(ta, taFocused, LV_EVENT_FOCUSED, nullptr);
  return ta;
}

static lv_obj_t* makeSpin(lv_obj_t* parent, int val) {
  lv_obj_t* sp = lv_spinbox_create(parent);
  lv_spinbox_set_range(sp, 1, 60);
  lv_spinbox_set_value(sp, val);
  lv_obj_set_width(sp, 90);
  return sp;
}

void uiOpenSettings(const DeviceSettings& cur) {
  scrSettings = lv_obj_create(nullptr);
  lv_obj_t* col = lv_obj_create(scrSettings);
  lv_obj_set_size(col, lv_pct(100), lv_pct(100));
  lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(col, 8, 0);

  // Wi-Fi scan list: dropdown of visible networks fills the SSID field
  lv_obj_t* dd = lv_dropdown_create(col);
  lv_obj_set_width(dd, lv_pct(100));
  {
    int n = WiFi.scanNetworks(); // blocking ~2 s, acceptable on settings entry
    String opts = "(scan)";
    for (int i = 0; i < n && i < 12; i++) opts += String("\n") + WiFi.SSID(i);
    lv_dropdown_set_options(dd, opts.c_str());
  }
  lv_obj_add_event_cb(
      dd,
      [](lv_event_t* e) {
        char sel[64];
        lv_dropdown_get_selected_str((lv_obj_t*)lv_event_get_target(e), sel, sizeof(sel));
        if (strcmp(sel, "(scan)") != 0) lv_textarea_set_text(taSsid, sel);
      },
      LV_EVENT_VALUE_CHANGED, nullptr);

  taSsid = makeTa(col, "WiFi SSID", false);
  lv_textarea_set_text(taSsid, cur.wifiSsid.c_str());
  taPass = makeTa(col, "WiFi password", true);
  lv_textarea_set_text(taPass, cur.wifiPass.c_str());
  taServer = makeTa(col, "http://server:8000", false);
  lv_textarea_set_text(taServer, cur.serverUrl.c_str());

  lv_obj_t* row = lv_obj_create(col);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
  spinMax = makeSpin(row, cur.maxTravelSec);
  spinHold = makeSpin(row, cur.holdSec);
  spinRest = makeSpin(row, cur.restSec);

  lv_obj_t* btnSave = lv_btn_create(col);
  lv_obj_add_event_cb(btnSave, saveClicked, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* sl = lv_label_create(btnSave);
  lv_obj_set_style_text_font(sl, &font_thai, 0);
  lv_label_set_text(sl, "บันทึก");
  lv_obj_center(sl);

  kb = lv_keyboard_create(scrSettings);
  lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);

  lv_scr_load(scrSettings);
}

void uiLoop() { lv_timer_handler(); }
