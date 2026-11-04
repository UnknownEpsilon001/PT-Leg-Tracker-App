#include "audio.h"

#include <Arduino.h>
#include <SD.h>
#include <driver/dac_continuous.h>
#include <esp_heap_caps.h>
#include <stdlib.h>

#include "wav.h"

// 48 KB is 2.23 s at 22050 Hz, which covers every prompt in /sounds. The clip is
// loaded whole before a single sample is emitted, so the card is never read
// while audio is playing — see the design doc for why streaming was rejected.
// tests/run-tests.ps1 asserts this equals -MaxBytes in tools/make-sounds.ps1;
// a comment alone was not keeping the two in step.
// [TH] 48 กิโลไบต์คือ 2.23 วินาทีที่ 22050 เฮิรตซ์ ครอบคลุมทุกข้อความใน /sounds
//      คลิปถูกโหลดจนครบก่อนเล่นตัวอย่างแรก การ์ดจึงไม่เคยถูกอ่านระหว่างมีเสียง
//      ดูเหตุผลที่ไม่เลือกวิธีอ่านไปเล่นไปได้ในเอกสารออกแบบ
//      สคริปต์ tests/run-tests.ps1 ตรวจว่าค่านี้ตรงกับ -MaxBytes ใน tools/make-sounds.ps1
//      เพราะลำพังคอมเมนต์ไม่พอที่จะทำให้สองค่านี้ตรงกัน
static constexpr size_t AUDIO_BUF_BYTES = 48u * 1024u;

// The degrade ladder, written out rather than computed. Halving inside the loop
// condition silently lost the last tier: 48 -> 24 -> 12, and 12 fails a
// `>= 16 KB` guard, so the 16 KB step the design documents was never attempted
// and a heap that could hold 16 KB got no audio at all.
// [TH] บันไดลดขนาด เขียนเป็นตารางแทนการคำนวณ
//      การหารสองในเงื่อนไขของลูปทำให้ขั้นสุดท้ายหายไปเงียบๆ คือ 48 -> 24 -> 12
//      และ 12 ไม่ผ่านเงื่อนไข ">= 16 กิโลไบต์" ขั้น 16 กิโลไบต์ที่เอกสารออกแบบระบุไว้จึงไม่เคยถูกลอง
//      หน่วยความจำที่พอสำหรับ 16 กิโลไบต์เลยไม่ได้เสียงเลย
static constexpr size_t AUDIO_BUF_TIERS[] = {AUDIO_BUF_BYTES, 24u * 1024u, 16u * 1024u};

// The first tier IS the advertised cap. Spelling 48 KB again here instead would
// let the ladder's top tier drift away from AUDIO_BUF_BYTES while run-tests.ps1
// — which greps for AUDIO_BUF_BYTES — kept reporting agreement with the tool,
// which is the exact drift that check exists to catch.
// [TH] ขั้นแรกคือค่าเพดานที่ประกาศไว้ ถ้าเขียนเลข 48 กิโลไบต์ซ้ำตรงนี้
//      ขั้นบนสุดของบันไดจะเลื่อนออกจาก AUDIO_BUF_BYTES ได้
//      ขณะที่ run-tests.ps1 ซึ่งค้นหา AUDIO_BUF_BYTES ยังรายงานว่าตรงกับเครื่องมืออยู่
//      ซึ่งเป็นความไม่ตรงกันแบบเดียวกับที่การตรวจนั้นมีไว้เพื่อจับ
static_assert(AUDIO_BUF_TIERS[0] == AUDIO_BUF_BYTES,
              "the largest buffer tier must equal the cap make-sounds.ps1 enforces");

// One card read per audioLoop() call. Small enough not to hold the loop, large
// enough that a 48 KB clip is resident in about a dozen iterations.
// [TH] อ่านการ์ดครั้งละเท่านี้ต่อการเรียก audioLoop() หนึ่งครั้ง
//      เล็กพอที่จะไม่ยึดลูปไว้ และใหญ่พอให้คลิป 48 กิโลไบต์โหลดครบในราวสิบกว่ารอบ
static constexpr size_t CHUNK_BYTES = 4096;

// Idle ──audioPlay()──▶ Opening ──file parsed──▶ Loading ──full──▶ Playing ──drained──▶ Idle
// [TH] ว่าง ──ขอเล่น──▶ กำลังเปิดไฟล์ ──อ่านส่วนหัวแล้ว──▶ กำลังโหลด ──ครบ──▶ กำลังเล่น ──จบ──▶ ว่าง
enum class AudioState { Idle, Opening, Loading, Playing };

static uint8_t* gBuf = nullptr;
static size_t gBufCap = 0;
static dac_continuous_handle_t gDac = nullptr;
static bool gEnabled = true;

static AudioState gState = AudioState::Idle;
static Cue gPendingCue = Cue::Boot;
static bool gHasPending = false;
static File gFile;
static uint32_t gWant = 0;        // ไบต์ที่ต้องโหลดทั้งหมด — total bytes to load
static uint32_t gHave = 0;        // โหลดมาแล้ว — bytes loaded so far
static uint32_t gSent = 0;        // ส่งให้ DAC แล้ว — bytes handed to the DAC
static uint32_t gPlayStart = 0;   // เวลาที่เริ่มเล่น — when Playing began, for the stall guard
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
// starting immediately.
// [TH] ปิดโดยทิ้งค่า 0x80 ไว้เสมอ ตัวแปลงจะค้างค่าสุดท้ายที่ได้รับ
//      ถ้าปิดตอนที่ค่ายังดังอยู่ เอาต์พุตจะกระโดดจนได้ยินเป็นเสียงป๊อก
//      ส่วน 0x80 คือความเงียบของรูปแบบ 8 บิตไม่มีเครื่องหมาย และเป็นค่าพักของตัวแปลง
//      ตั้งใจให้เป็นแบบทำเท่าที่ทำได้ ตอนจบตามปกติคิวว่างแล้วจึงเขียนสำเร็จ
//      ตอนถูกขัดจังหวะคิวอาจยังเต็ม การเขียนจะถูกทิ้ง และการกระโดดเล็กน้อยที่ตามมาจะถูกกลบด้วยคลิปใหม่
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
  gPlayStart = 0;
  gDrainStart = 0;
}

// CH1 is GPIO 26 alone. CH0 is GPIO 25, which is the touchscreen clock on this
// board — the legacy I2S DAC path brings up both channels by default, and
// enabling both here would trade the speaker for a dead panel.
// [TH] CH1 คือขา 26 เท่านั้น ส่วน CH0 คือขา 25 ซึ่งเป็นสายนาฬิกาของทัชสกรีนบนบอร์ดนี้
//      วิธีเดิมที่ใช้ I2S เปิดช่องสัญญาณทั้งสองช่องโดยปริยาย
//      ถ้าเปิดทั้งคู่ตรงนี้จะได้ลำโพงมาแลกกับจอสัมผัสที่ใช้ไม่ได้
static esp_err_t dacCreate() {
  dac_continuous_config_t cfg = {};
  cfg.chan_mask = DAC_CHANNEL_MASK_CH1;
  cfg.desc_num = 4;
  cfg.buf_size = 1024;  // 4 KB of DMA in total — รวมบัฟเฟอร์ DMA 4 กิโลไบต์
  cfg.freq_hz = WAV_RATE;
  cfg.offset = 0;
  cfg.clk_src = DAC_DIGI_CLK_SRC_DEFAULT;
  cfg.chan_mode = DAC_CHANNEL_MODE_SIMUL;  // one channel enabled, so this is moot
  return dac_continuous_new_channels(&cfg, &gDac);
}

void audioBegin() {
  // Try each tier in turn. Unlike the LVGL draw buffer in ui.cpp this never
  // restarts the chip: a machine that cannot play sounds is a machine that
  // works, and rebooting over it would turn a missing convenience into an outage.
  // [TH] ไล่ลองทีละขั้น ต่างจากบัฟเฟอร์วาดภาพของ LVGL ใน ui.cpp ตรงที่ไม่รีบูตชิป
  //      เครื่องที่เล่นเสียงไม่ได้ยังเป็นเครื่องที่ใช้งานได้
  //      การรีบูตเพราะเรื่องนี้จะเปลี่ยนของอำนวยความสะดวกที่ขาดไปให้กลายเป็นเครื่องล่ม
  // The tier that wins is decided by the largest contiguous block, not by total
  // free heap, so that is the number worth printing when it falls short.
  // [TH] ขั้นที่ได้ขึ้นกับก้อนหน่วยความจำติดกันที่ใหญ่ที่สุด ไม่ใช่หน่วยความจำว่างรวม จึงพิมพ์ค่านี้
  size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  for (size_t i = 0; i < sizeof(AUDIO_BUF_TIERS) / sizeof(AUDIO_BUF_TIERS[0]); ++i) {
    gBuf = (uint8_t*)malloc(AUDIO_BUF_TIERS[i]);
    if (gBuf) {
      gBufCap = AUDIO_BUF_TIERS[i];
      break;
    }
  }
  if (!gBuf) {
    Serial.println("audio: no heap for the playback buffer, cues are off");
    return;
  }

  esp_err_t err = dacCreate();
  if (err != ESP_OK) {
    gDac = nullptr;
    // Give the buffer back. audioReady() is false from here on, so it could never
    // be used again, and holding tens of kilobytes of a ~227 KB budget for the
    // life of the boot can push the LVGL draw buffer into its own degrade path.
    // [TH] คืนหน่วยความจำบัฟเฟอร์ เพราะ audioReady() จะเป็นเท็จตลอดไป จึงไม่มีทางได้ใช้อีก
    //      การกันไว้หลายสิบกิโลไบต์จากงบราว 227 กิโลไบต์ตลอดการทำงาน
    //      อาจดันให้บัฟเฟอร์วาดภาพของ LVGL ต้องลดขนาดลงตามไปด้วย
    free(gBuf);
    gBuf = nullptr;
    gBufCap = 0;
    Serial.printf("audio: dac_continuous_new_channels failed (%d), cues are off\n", (int)err);
    return;
  }

  Serial.printf("audio: %u byte buffer (%.2f s), DAC on GPIO 26, largest free block was %u\n",
                (unsigned)gBufCap, (double)gBufCap / (double)WAV_RATE, (unsigned)largest);
}

// Records the request and returns. The card work happens in audioLoop().
//
// This runs inside a link callback, under gLink.poll(): opening a file, reading
// its header and seeking took tens of milliseconds on a slow card, and for the
// stop cue that landed on the same pass as the sessions.csv append. Deferring
// costs one loop iteration of latency and keeps the UART receive path short.
// [TH] แค่บันทึกคำขอแล้วคืนค่า งานที่แตะการ์ดไปทำใน audioLoop()
//      ฟังก์ชันนี้ถูกเรียกจากคอลแบ็กของลิงก์ซึ่งอยู่ใต้ gLink.poll()
//      การเปิดไฟล์ อ่านส่วนหัว และเลื่อนตำแหน่ง ใช้เวลาหลายสิบมิลลิวินาทีบนการ์ดที่ช้า
//      และสำหรับเสียงหยุด งานนั้นตกอยู่รอบเดียวกับการเขียน sessions.csv
//      การเลื่อนไปทำทีหลังแลกด้วยความหน่วงหนึ่งรอบลูป และทำให้เส้นทางรับ UART สั้นเข้าไว้
void audioPlay(Cue c) {
  if (!gEnabled || !audioReady()) return;
  // Newest wins, including over a request made earlier in this same poll batch.
  // [TH] คำขอล่าสุดชนะเสมอ รวมถึงชนะคำขอที่เพิ่งเกิดในรอบ poll เดียวกัน
  gPendingCue = c;
  gHasPending = true;
}

// Opens the pending cue and gets as far as the first sample. Returns false and
// reports why if the file cannot be used.
// [TH] เปิดไฟล์ของเสียงที่ขอไว้ แล้วเตรียมจนถึงตัวอย่างเสียงแรก
//      ถ้าใช้ไฟล์นั้นไม่ได้จะคืนค่าเท็จพร้อมบอกเหตุผล
static bool beginCue(Cue c) {
  const char* path = cuePath(c);
  if (!path) return false;

  File f = SD.open(path, FILE_READ);
  if (!f) {
    Serial.printf("audio: %s could not be opened\n", path);
    return false;
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
    return false;
  }

  if (!f.seek(info.dataOffset)) {
    Serial.printf("audio: %s could not seek to the samples\n", path);
    f.close();
    return false;
  }

  gFile = f;
  gWant = info.dataBytes;
  gHave = 0;
  gSent = 0;
  gState = AudioState::Loading;
  return true;
}

void audioLoop() {
  // A pending request is serviced first, so a newer cue cuts an older one off
  // here — in loop context — rather than inside the link callback.
  // [TH] คำขอที่ค้างอยู่ถูกจัดการก่อน เสียงใหม่จึงตัดเสียงเก่าตรงนี้ซึ่งอยู่ในบริบทของลูป
  //      ไม่ใช่ในคอลแบ็กของลิงก์
  if (gHasPending) {
    gHasPending = false;
    stopAll();
    if (!beginCue(gPendingCue)) stopAll();
    return;
  }

  if (gState == AudioState::Idle) return;

  if (gState == AudioState::Loading) {
    size_t take = gWant - gHave;
    if (take > CHUNK_BYTES) take = CHUNK_BYTES;
    size_t got = gFile.read(gBuf + gHave, take);
    if (got == 0) {
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
    // A fresh channel for every clip. Reusing one across disable/enable left the
    // driver short of DMA descriptors on about half the cues on the bench: the
    // first 2 KB went out — a click — and every later write failed with "Get
    // available descriptor timeout" until the stall guard gave up. Deleting and
    // recreating resets the descriptor pool; it costs one 4 KB DMA allocation of
    // the same size each time, so it does not fragment the heap.
    // [TH] สร้างช่องสัญญาณใหม่ทุกคลิป การใช้ช่องเดิมข้ามการปิดแล้วเปิดใหม่
    //      ทำให้ไดรเวอร์ขาดตัวอธิบาย DMA ราวครึ่งหนึ่งของเสียงที่ทดสอบ
    //      ข้อมูล 2 กิโลไบต์แรกออกไปได้ (ได้ยินเป็นเสียงคลิก) แล้วการเขียนครั้งต่อๆ ไปล้มเหลวด้วย
    //      "Get available descriptor timeout" จนตัวกันค้างยอมแพ้
    //      การลบแล้วสร้างใหม่รีเซ็ตกลุ่มตัวอธิบาย แลกด้วยการจองหน่วยความจำ DMA 4 กิโลไบต์ขนาดเดิมทุกครั้ง
    //      จึงไม่ทำให้หน่วยความจำแตกเป็นชิ้น
    dac_continuous_del_channels(gDac);
    gDac = nullptr;
    esp_err_t err = dacCreate();
    if (err != ESP_OK) {
      gDac = nullptr;
      Serial.printf("audio: dac_continuous_new_channels failed (%d), cues are off\n", (int)err);
      stopAll();
      return;
    }
    err = dac_continuous_enable(gDac);
    if (err != ESP_OK) {
      Serial.printf("audio: dac_continuous_enable failed (%d)\n", (int)err);
      stopAll();
      return;
    }
    gDacOn = true;
    gPlayStart = millis();
    gState = AudioState::Playing;
    return;
  }

  // Playing. Every other wait in this firmware is bounded — the link watchdog at
  // 2.5 s, the travel cap — and this one has to be too. ESP_ERR_TIMEOUT is the
  // ordinary outcome here (the queue frees a descriptor about every 46 ms while
  // loop() runs far faster), so a stalled DMA is indistinguishable from normal
  // operation except by the clock: without this guard it would sit in Playing
  // forever with the channel enabled, driving the amplifier into a hiss that
  // only a reboot clears.
  // [TH] กำลังเล่น การรอทุกอย่างในเฟิร์มแวร์นี้มีเพดานเวลา ทั้งตัวเฝ้าระวังลิงก์ 2.5 วินาที
  //      และเพดานเวลาเดินทาง ตรงนี้ก็ต้องมีเช่นกัน
  //      ค่า ESP_ERR_TIMEOUT เป็นผลลัพธ์ปกติของที่นี่ เพราะคิวว่างทีละช่องทุกราว 46 มิลลิวินาที
  //      ขณะที่ลูปทำงานเร็วกว่านั้นมาก DMA ที่ค้างจึงดูเหมือนการทำงานปกติ แยกได้ด้วยเวลาเท่านั้น
  //      ถ้าไม่มีเงื่อนไขนี้ จะค้างอยู่ในสถานะกำลังเล่นตลอดไปโดยเปิดช่องสัญญาณไว้
  //      แล้วขับวงจรขยายเสียงจนมีเสียงซ่าที่แก้ได้ด้วยการรีบูตเท่านั้น
  uint32_t expectedMs = (uint32_t)(((uint64_t)gWant * 1000ull) / WAV_RATE);
  if ((uint32_t)(millis() - gPlayStart) > expectedMs * 3u + 1000u) {
    Serial.printf("audio: playback stalled after %u of %u bytes, giving up\n", (unsigned)gSent,
                  (unsigned)gWant);
    stopAll();
    return;
  }

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
