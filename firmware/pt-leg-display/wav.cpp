#include "wav.h"

#include <string.h>

// WAV is little-endian regardless of host. Reading byte by byte rather than
// casting to a uint32_t* also avoids an unaligned load, which the ESP32 traps.
// [TH] ไฟล์ WAV เก็บเลขแบบ little-endian เสมอ ไม่ขึ้นกับเครื่อง
//      การอ่านทีละไบต์แทนการแคสต์เป็นตัวชี้ยังเลี่ยงการอ่านข้ามขอบเขต ซึ่ง ESP32 จะดักเป็นข้อผิดพลาด
static uint32_t rd32(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t rd16(const uint8_t* p) {
  return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

const char* wavErrorText(WavError e) {
  switch (e) {
    case WavError::Ok: return "ok";
    case WavError::NotRiff: return "not a RIFF file";
    case WavError::NotWave: return "not a WAVE file";
    case WavError::NoFmt: return "no usable fmt chunk";
    case WavError::NoData: return "no data chunk, or it is empty";
    case WavError::NotPcm: return "not uncompressed PCM";
    case WavError::NotMono: return "not mono";
    case WavError::NotEightBit: return "not 8-bit";
    case WavError::WrongRate: return "wrong sample rate";
    case WavError::Truncated: return "data chunk runs past the end of the file";
    case WavError::TooBig: return "longer than the playback buffer";
  }
  return "unknown";
}

WavError wavParse(const uint8_t* hdr, size_t hdrLen, uint32_t fileLen, uint32_t maxData,
                  WavInfo& out) {
  if (hdrLen < 12 || fileLen < 12) return WavError::NotRiff;
  if (memcmp(hdr, "RIFF", 4) != 0) return WavError::NotRiff;
  if (memcmp(hdr + 8, "WAVE", 4) != 0) return WavError::NotWave;

  // The chunk list is walked rather than seeking to the canonical offset 44.
  // ffmpeg writes a LIST chunk between fmt and data, and a parser that assumed
  // 44 would play those metadata bytes as audio — a burst of noise whose cause
  // is invisible from the symptom.
  // [TH] ไล่อ่านรายการชังก์ ไม่ใช่กระโดดไปที่ตำแหน่ง 44 ตามรูปแบบมาตรฐาน
  //      เพราะ ffmpeg แทรกชังก์ LIST ไว้ระหว่าง fmt กับ data
  //      ตัวแยกที่เชื่อว่าอยู่ที่ 44 จะเอาข้อมูลกำกับไปเล่นเป็นเสียง กลายเป็นเสียงรบกวนที่หาสาเหตุไม่เจอ
  bool haveFmt = false;
  size_t p = 12;
  while (p + 8 <= hdrLen) {
    const uint8_t* id = hdr + p;
    uint32_t sz = rd32(hdr + p + 4);
    size_t body = p + 8;

    if (memcmp(id, "fmt ", 4) == 0) {
      if (sz < 16 || body + 16 > hdrLen) return WavError::NoFmt;
      if (rd16(hdr + body) != 1) return WavError::NotPcm;
      if (rd16(hdr + body + 2) != 1) return WavError::NotMono;
      if (rd32(hdr + body + 4) != WAV_RATE) return WavError::WrongRate;
      if (rd16(hdr + body + 14) != 8) return WavError::NotEightBit;
      haveFmt = true;
    } else if (memcmp(id, "data", 4) == 0) {
      // fmt always precedes data in a real file. Accepting data first would mean
      // accepting a file whose format was never checked.
      // [TH] ไฟล์จริงมีชังก์ fmt มาก่อน data เสมอ ถ้ายอมรับ data ก่อน
      //      เท่ากับยอมรับไฟล์ที่ยังไม่ได้ตรวจรูปแบบเลย
      if (!haveFmt) return WavError::NoFmt;
      if ((uint64_t)body + (uint64_t)sz > (uint64_t)fileLen) return WavError::Truncated;
      if (sz > maxData) return WavError::TooBig;
      if (sz == 0) return WavError::NoData;
      out.dataOffset = (uint32_t)body;
      out.dataBytes = sz;
      return WavError::Ok;
    }

    // Chunks are word-aligned: an odd size is followed by one pad byte that is
    // not counted in the size field. Skipping it desynchronises every chunk
    // after this one.
    // [TH] ชังก์เรียงตามขอบคำ ถ้าขนาดเป็นเลขคี่จะมีไบต์เติมอีกหนึ่งไบต์ที่ไม่นับรวมในช่องขนาด
    //      ถ้าไม่ข้ามไบต์นั้น ชังก์ถัดๆ ไปจะเลื่อนผิดตำแหน่งทั้งหมด
    uint64_t next = (uint64_t)body + (uint64_t)sz + (uint64_t)(sz & 1u);
    if (next > (uint64_t)hdrLen) break;
    p = (size_t)next;
  }

  return haveFmt ? WavError::NoData : WavError::NoFmt;
}
