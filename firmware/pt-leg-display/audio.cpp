#include "audio.h"

#include <Arduino.h>
#include <SD.h>
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

// One card read per audioLoop() call. Small enough not to hold the loop, large
// enough that a 48 KB clip is resident in about a dozen iterations.
// [TH] อ่านการ์ดครั้งละเท่านี้ต่อการเรียก audioLoop() หนึ่งครั้ง
//      เล็กพอที่จะไม่ยึดลูปไว้ และใหญ่พอให้คลิป 48 กิโลไบต์โหลดครบในราวสิบกว่ารอบ
static constexpr size_t CHUNK_BYTES = 4096;

// Idle ──audioPlay()──▶ Loading ──buffer full──▶ Playing ──drained──▶ Idle
// [TH] ว่าง ──ขอเล่น──▶ กำลังโหลด ──โหลดครบ──▶ กำลังเล่น ──เล่นจบ──▶ ว่าง
enum class AudioState { Idle, Loading, Playing };

static uint8_t* gBuf = nullptr;
static size_t gBufCap = 0;
static dac_continuous_handle_t gDac = nullptr;
static bool gEnabled = true;

static AudioState gState = AudioState::Idle;
static File gFile;
static uint32_t gWant = 0;        // ไบต์ที่ต้องโหลดทั้งหมด — total bytes to load
static uint32_t gHave = 0;        // โหลดมาแล้ว — bytes loaded so far
static uint32_t gSent = 0;        // ส่งให้ DAC แล้ว — bytes handed to the DAC
static uint32_t gDrainStart = 0;  // เวลาที่เริ่มรอคิว DMA ให้หมด — when the drain wait began
static bool gDacOn = false;

bool audioReady() { return gBuf != nullptr && gDac != nullptr; }

void audioSetEnabled(bool on) { gEnabled = on; }

static const char* cuePath(Cue c) {
  switch (c) {
    case Cue::Boot: return "/sounds/boot.wav";
    case Cue::Start: return "/sounds/start.wav";
    case Cue::Stop: return "/sounds/stop.wav";
    case Cue::Target: return "/sounds/target.wav";
  }
  return nullptr;
}

// Always leaves the converter on 0x80. The DAC holds the last code it was given,
// so disabling on a loud sample steps the output and pops; 0x80 is silence for
// unsigned 8-bit and the converter's rest value.
//
// Best-effort by design. On a natural end the queue has already drained and this
// write lands. On an interrupted cue the queue may still be full, the write is
// dropped, and the small step that follows is masked by the replacement clip
// starting immediately — which is the only reason a 20 ms timeout is acceptable
// in a function reached from audioLoop().
// [TH] ปิดโดยทิ้งค่า 0x80 ไว้เสมอ ตัวแปลงจะค้างค่าสุดท้ายที่ได้รับ
//      ถ้าปิดตอนที่ค่ายังดังอยู่ เอาต์พุตจะกระโดดจนได้ยินเป็นเสียงป๊อก
//      ส่วน 0x80 คือความเงียบของรูปแบบ 8 บิตไม่มีเครื่องหมาย และเป็นค่าพักของตัวแปลง
//      ตั้งใจให้เป็นแบบทำเท่าที่ทำได้ ตอนจบตามปกติคิวว่างแล้วจึงเขียนสำเร็จ
//      ตอนถูกขัดจังหวะคิวอาจยังเต็ม การเขียนจะถูกทิ้ง และการกระโดดเล็กน้อยที่ตามมา
//      จะถูกกลบด้วยคลิปใหม่ที่เริ่มทันที ซึ่งเป็นเหตุผลเดียวที่ยอมให้รอ 20 มิลลิวินาทีในเส้นทางนี้ได้
static void dacOff() {
  if (!gDacOn) return;
  uint8_t rest = 0x80;
  size_t loaded = 0;
  dac_continuous_write(gDac, &rest, 1, &loaded, 20);
  dac_continuous_disable(gDac);
  gDacOn = false;
}

static void stopAll() {
  if (gFile) gFile.close();
  dacOff();
  gState = AudioState::Idle;
  gWant = gHave = gSent = 0;
  gDrainStart = 0;
}

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
  if (!gEnabled || !audioReady()) return;

  const char* path = cuePath(c);
  if (!path) return;

  // A newer cue abandons whatever is in flight, including mid-clip.
  // [TH] คำขอใหม่ยกเลิกรายการที่ค้างอยู่ รวมถึงตอนที่เล่นไปได้ครึ่งเดียว
  stopAll();

  File f = SD.open(path, FILE_READ);
  if (!f) {
    Serial.printf("audio: %s could not be opened\n", path);
    return;
  }

  uint8_t hdr[WAV_SCAN_BYTES];
  size_t n = f.read(hdr, sizeof(hdr));
  WavInfo info{};
  WavError e = wavParse(hdr, n, (uint32_t)f.size(), (uint32_t)gBufCap, info);
  if (e != WavError::Ok) {
    // Named field, named file. A silent speaker and a rejected file must be
    // distinguishable without an oscilloscope.
    // [TH] บอกทั้งชื่อไฟล์และฟิลด์ที่ผิด ต้องแยกออกได้ว่าลำโพงเงียบเพราะอะไร
    //      โดยไม่ต้องใช้ออสซิลโลสโคป
    Serial.printf("audio: %s rejected — %s\n", path, wavErrorText(e));
    f.close();
    return;
  }

  if (!f.seek(info.dataOffset)) {
    Serial.printf("audio: %s could not seek to the samples\n", path);
    f.close();
    return;
  }

  gFile = f;
  gWant = info.dataBytes;
  gHave = 0;
  gSent = 0;
  gState = AudioState::Loading;
}

void audioLoop() {
  if (gState == AudioState::Idle) return;

  if (gState == AudioState::Loading) {
    size_t take = gWant - gHave;
    if (take > CHUNK_BYTES) take = CHUNK_BYTES;
    int got = gFile.read(gBuf + gHave, take);
    if (got <= 0) {
      // The header promised more than the card gave. Play nothing rather than
      // whatever the buffer's tail happens to hold.
      // [TH] ส่วนหัวบอกว่ามีมากกว่าที่การ์ดให้มาจริง ให้ไม่เล่นเลย
      //      ดีกว่าเล่นสิ่งที่ค้างอยู่ท้ายบัฟเฟอร์
      Serial.println("audio: short read, clip dropped");
      stopAll();
      return;
    }
    gHave += (uint32_t)got;
    if (gHave < gWant) return;

    gFile.close();
    esp_err_t err = dac_continuous_enable(gDac);
    if (err != ESP_OK) {
      Serial.printf("audio: dac_continuous_enable failed (%d)\n", (int)err);
      stopAll();
      return;
    }
    gDacOn = true;
    gState = AudioState::Playing;
    return;
  }

  // Playing. A timeout of 0 makes this a partial, non-blocking write: it takes
  // what fits in the DMA descriptors and reports how much, and the rest waits
  // for the next loop() rather than stalling LVGL here. Most calls write nothing
  // and return ESP_ERR_TIMEOUT, because the queue only frees a descriptor every
  // ~46 ms while loop() runs far faster than that — which is why a timeout is
  // an ordinary outcome here and not a failure.
  // [TH] กำลังเล่น การตั้งเวลารอเป็น 0 ทำให้เป็นการเขียนแบบไม่รอและอาจไม่ครบ
  //      คือรับเท่าที่ตัวอธิบาย DMA ว่างแล้วบอกว่ารับไปเท่าไร ที่เหลือรอลูปถัดไป
  //      แทนที่จะยึด LVGL ไว้ตรงนี้
  //      การเรียกส่วนใหญ่จะเขียนไม่ได้เลยและคืนค่า ESP_ERR_TIMEOUT
  //      เพราะคิวว่างทีละช่องทุกราว 46 มิลลิวินาที ขณะที่ลูปทำงานเร็วกว่านั้นมาก
  //      การหมดเวลาตรงนี้จึงเป็นเรื่องปกติ ไม่ใช่ความผิดพลาด
  if (gSent < gWant) {
    size_t loaded = 0;
    esp_err_t err = dac_continuous_write(gDac, gBuf + gSent, gWant - gSent, &loaded, 0);
    if (err != ESP_OK && err != ESP_ERR_TIMEOUT) {
      Serial.printf("audio: dac_continuous_write failed (%d)\n", (int)err);
      stopAll();
      return;
    }
    gSent += (uint32_t)loaded;
    return;
  }

  // Everything is in the DMA queue, not yet out of the speaker. Disabling now
  // would cut the tail off, so wait out the samples still in flight: 4 KB of DMA
  // at 22050 Hz is about 186 ms, and 250 ms clears it with margin.
  // [TH] ข้อมูลเข้าคิว DMA ครบแล้ว แต่ยังไม่ออกลำโพงจนหมด ถ้าปิดตอนนี้จะตัดหางเสียงทิ้ง
  //      จึงต้องรอตัวอย่างที่ค้างอยู่ในคิว คือ 4 กิโลไบต์ที่ 22050 เฮิรตซ์ ราว 186 มิลลิวินาที
  //      และ 250 มิลลิวินาทีเผื่อไว้พอ
  if (gDrainStart == 0) gDrainStart = millis();
  if ((uint32_t)(millis() - gDrainStart) < 250) return;
  stopAll();
}
