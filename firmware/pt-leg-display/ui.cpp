#include "ui.h"
#include <lvgl.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>

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
  lv_obj_set_style_text_font(lblPhase, &lv_font_montserrat_20, 0);
  lv_label_set_text(lblPhase, "");
  lv_obj_align(lblPhase, LV_ALIGN_TOP_MID, 0, 108);

  btnGo = lv_btn_create(scrMain);
  lv_obj_set_size(btnGo, 200, 70);
  lv_obj_align(btnGo, LV_ALIGN_BOTTOM_MID, 0, -34);
  lv_obj_add_event_cb(btnGo, goClicked, LV_EVENT_CLICKED, nullptr);
  lblGo = lv_label_create(btnGo);
  lv_obj_set_style_text_font(lblGo, &lv_font_montserrat_28, 0);
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
    case Phase::Lift: return "UP";
    case Phase::Hold: return "HOLD";
    case Phase::Lower: return "DOWN";
    case Phase::Rest: return "REST";
    case Phase::SafeLower: return "STOPPING";
    case Phase::Fault: return "SWITCH FAULT";
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
  lv_label_set_text(lblGo, fault ? "RESTART" : (running ? "STOP" : "START"));
  uint32_t goCol = fault ? 0xb00020 : (running ? 0x8c2f39 : 0x2e7d32);
  lv_obj_set_style_bg_color(btnGo, lv_color_hex(goCol), 0);
  lv_obj_set_style_text_color(lblPhase, lv_color_hex(fault ? 0xb00020 : 0x000000), 0);

  lv_label_set_text_fmt(lblStatus, "%s %s", wifiUp ? LV_SYMBOL_WIFI : "--",
                        serverUp ? LV_SYMBOL_OK : LV_SYMBOL_CLOSE);
}

void uiLoop() { lv_timer_handler(); }
