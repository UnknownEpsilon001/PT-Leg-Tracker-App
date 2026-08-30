#include "audio.h"

#include <Arduino.h>
#include <driver/dac_continuous.h>
#include <stdlib.h>

#include "wav.h"

// 48 KB is 2.23 s at 22050 Hz, which covers every prompt in /sounds. The clip is
// loaded whole before a single sample is emitted, so the card is never read
// while audio is playing — see the design doc for why streaming was rejected.
// Keep this in step with -MaxBytes in tools/make-sounds.ps1.
// [TH] 48 กิโลไบต์คือ 2.23 วินาทีที่ 22050 เฮิรตซ์ ครอบคลุมทุกข้อความใน /sounds
//      คลิปถูกโหลดจนครบก่อนเล่นตัวอย่างแรก การ์ดจึงไม่เคยถูกอ่านระหว่างมีเสียง
//      ดูเหตุผลที่ไม่เลือกวิธีอ่านไปเล่นไปได้ในเอกสารออกแบบ
//      ค่านี้ต้องตรงกับ -MaxBytes ใน tools/make-sounds.ps1
static constexpr size_t AUDIO_BUF_BYTES = 48u * 1024u;

static uint8_t* gBuf = nullptr;
static size_t gBufCap = 0;
static dac_continuous_handle_t gDac = nullptr;
static bool gEnabled = true;

bool audioReady() { return gBuf != nullptr && gDac != nullptr; }

void audioSetEnabled(bool on) { gEnabled = on; }

void audioBegin() {
  // Halve until it fits, the way the LVGL draw buffer does in ui.cpp. Unlike the
  // draw buffer this never restarts the chip: a machine that cannot play sounds
  // is a machine that works, and rebooting over it would turn a missing
  // convenience into an outage.
  // [TH] ลดขนาดลงครึ่งหนึ่งจนกว่าจะจองได้ เหมือนบัฟเฟอร์วาดภาพของ LVGL ใน ui.cpp
  //      ต่างกันตรงที่ตรงนี้ไม่รีบูตชิป เครื่องที่เล่นเสียงไม่ได้ยังเป็นเครื่องที่ใช้งานได้
  //      การรีบูตเพราะเรื่องนี้จะเปลี่ยนของอำนวยความสะดวกที่ขาดไปให้กลายเป็นเครื่องล่ม
  size_t want = AUDIO_BUF_BYTES;
  while (want >= 16u * 1024u && !gBuf) {
    gBuf = (uint8_t*)malloc(want);
    if (gBuf) {
      gBufCap = want;
      break;
    }
    want /= 2;
  }
  if (!gBuf) {
    Serial.println("audio: no heap for the playback buffer, cues are off");
    return;
  }

  // CH1 is GPIO 26 alone. CH0 is GPIO 25, which is the touchscreen clock on this
  // board — the legacy I2S DAC path brings up both channels by default, and
  // enabling both here would trade the speaker for a dead panel.
  // [TH] CH1 คือขา 26 เท่านั้น ส่วน CH0 คือขา 25 ซึ่งเป็นสายนาฬิกาของทัชสกรีนบนบอร์ดนี้
  //      วิธีเดิมที่ใช้ I2S เปิดช่องสัญญาณทั้งสองช่องโดยปริยาย
  //      ถ้าเปิดทั้งคู่ตรงนี้จะได้ลำโพงมาแลกกับจอสัมผัสที่ใช้ไม่ได้
  dac_continuous_config_t cfg = {};
  cfg.chan_mask = DAC_CHANNEL_MASK_CH1;
  cfg.desc_num = 4;
  cfg.buf_size = 1024;  // 4 KB of DMA in total — รวมบัฟเฟอร์ DMA 4 กิโลไบต์
  cfg.freq_hz = WAV_RATE;
  cfg.offset = 0;
  cfg.clk_src = DAC_DIGI_CLK_SRC_DEFAULT;
  cfg.chan_mode = DAC_CHANNEL_MODE_SIMUL;  // one channel enabled, so this is moot

  esp_err_t err = dac_continuous_new_channels(&cfg, &gDac);
  if (err != ESP_OK) {
    gDac = nullptr;
    Serial.printf("audio: dac_continuous_new_channels failed (%d), cues are off\n", (int)err);
    return;
  }

  Serial.printf("audio: %u byte buffer (%.2f s), DAC on GPIO 26\n", (unsigned)gBufCap,
                (double)gBufCap / (double)WAV_RATE);
}

void audioPlay(Cue c) {
  (void)c;  // Task 4 — ยังไม่ได้ทำในขั้นนี้
}

void audioLoop() {
  // Task 4 — ยังไม่ได้ทำในขั้นนี้
}
