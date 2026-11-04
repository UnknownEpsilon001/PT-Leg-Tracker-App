#pragma once
#include <stddef.h>
#include <stdint.h>

// ตัวแยกส่วนหัวไฟล์ WAV ล้วนๆ ไม่ยุ่งกับฮาร์ดแวร์
// Pure byte-level WAV header parsing. No Arduino, no SD, no DAC: this unit is
// compiled both into the sketch and into firmware/tests, where it runs on a PC.
// It is the only part of the audio feature that can be tested without a board.
// [TH] ไม่มีการเรียกใช้ Arduino, SD หรือ DAC ไฟล์นี้จึงคอมไพล์ได้ทั้งบนบอร์ดและบนคอมพิวเตอร์
//      เป็นส่วนเดียวของฟีเจอร์เสียงที่ทดสอบได้โดยไม่ต้องมีบอร์ดจริง

// The one accepted sample rate. dac_continuous_config_t::freq_hz has a 19.6 kHz
// floor on the ESP32 with DAC_DIGI_CLK_SRC_DEFAULT, so a lower rate would mean
// switching to APLL for no gain.
// [TH] อัตราสุ่มที่ยอมรับค่าเดียว เพราะ freq_hz ของ dac_continuous มีขั้นต่ำ 19.6 กิโลเฮิรตซ์
//      บน ESP32 เมื่อใช้แหล่งสัญญาณนาฬิกาปกติ ถ้าต่ำกว่านี้ต้องเปลี่ยนไปใช้ APLL โดยไม่ได้อะไรเพิ่ม
static constexpr uint32_t WAV_RATE = 22050;

// How many leading bytes the caller must hand to wavParse. The chunk list is
// walked inside this window; a `data` chunk pushed past it by a large metadata
// chunk reads as NoData. make-sounds.ps1 strips metadata so this cannot happen
// to a file the tool produced.
// [TH] จำนวนไบต์แรกของไฟล์ที่ผู้เรียกต้องส่งมาให้ ตัวแยกจะไล่รายการชังก์อยู่ในช่วงนี้
//      ถ้าชังก์ข้อมูลถูกดันเลยช่วงนี้ไปเพราะมีชังก์ข้อมูลกำกับขนาดใหญ่ จะรายงานว่าไม่พบชังก์ข้อมูล
//      สคริปต์ make-sounds.ps1 ตัดข้อมูลกำกับออก ไฟล์ที่สร้างจากเครื่องมือนั้นจึงไม่มีทางเจอกรณีนี้
static constexpr size_t WAV_SCAN_BYTES = 256;

struct WavInfo {
  uint32_t dataOffset;  // ไบต์แรกของข้อมูลเสียง — byte offset of the first sample
  uint32_t dataBytes;   // ความยาวข้อมูลเสียง — length of the data chunk
};

// Every rejection is a distinct value so the serial line can name the field that
// failed. "It did not play" is not a diagnosis anyone can act on.
// [TH] เหตุผลที่ปฏิเสธแยกเป็นค่าคนละค่า เพื่อให้ข้อความบนพอร์ตอนุกรมบอกได้ว่าฟิลด์ไหนผิด
//      เพราะคำว่า "ไม่มีเสียง" อย่างเดียวเอาไปแก้อะไรไม่ได้
enum class WavError {
  Ok,
  NotRiff,
  NotWave,
  NoFmt,
  NoData,
  NotPcm,
  NotMono,
  NotEightBit,
  WrongRate,
  Truncated,
  TooBig,
};

const char* wavErrorText(WavError e);

// hdr/hdrLen: the first bytes of the file, up to WAV_SCAN_BYTES.
// fileLen:    the file's total size on the card.
// maxData:    the playback buffer's capacity.
// [TH] hdr/hdrLen คือไบต์แรกๆ ของไฟล์ fileLen คือขนาดไฟล์ทั้งหมด maxData คือขนาดบัฟเฟอร์
WavError wavParse(const uint8_t* hdr, size_t hdrLen, uint32_t fileLen, uint32_t maxData,
                  WavInfo& out);
