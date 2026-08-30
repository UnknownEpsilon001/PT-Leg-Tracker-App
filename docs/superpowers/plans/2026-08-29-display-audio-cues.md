# Display Audio Cues Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Play spoken Thai prompts from the SD card through the CYD's GPIO 26 speaker output on four events — boot, session start, session stop, and rep target reached — with an on/off switch on the settings screen.

**Architecture:** A new `audio` module on the display board owns a DAC channel created through the IDF 5 `dac_continuous` driver and one heap buffer allocated at boot. A cue request loads the whole `.wav` into that buffer 4 KB per `loop()` call, then plays it from RAM, so the card is never read while audio is being emitted. WAV header parsing is split into a separate hardware-free `wav` unit so it can be test-driven on a PC. Cues are raised from the sketch: boot at the end of `setup()`, start from the running edge in `onLinkState()`, stop and target from `onLinkDone()`.

**Tech Stack:** Arduino ESP32 core 3.3.11 (IDF 5.x), `esp_driver_dac` (`dac_continuous`), `SD.h` on VSPI, LVGL 8.4.0, ArduinoJson. Host tests build with `g++` 14.2.0 (present at `/c/gcc-14.2.0/bin/g++`).

**Spec:** `docs/superpowers/specs/2026-08-29-display-audio-cues-design.md`

## Global Constraints

- **Board:** display (CYD) only. The controller sketch changes only through the byte-identical `settings.h` that `sync-shared.ps1` copies.
- **DAC channel mask is `DAC_CHANNEL_MASK_CH1` and nothing else.** CH0 is GPIO 25, which is the touchscreen clock on this board. Enabling both channels trades the speaker for a dead panel.
- **Do not use `i2s_set_dac_mode`.** On core 3.3.11 it exists only under `driver/deprecated/`. Every CYD audio example in circulation uses it; none are a model for this code.
- **Sample rate is exactly 22050 Hz**, 8-bit unsigned, mono. `dac_continuous_config_t::freq_hz` has a 19.6 kHz floor with `DAC_DIGI_CLK_SRC_DEFAULT`.
- **`audio.cpp` never touches LVGL.** The UI does not know the module exists.
- **`audioLoop()` never blocks for more than about a millisecond** — at most one 4 KB card read or one non-blocking DMA write per call.
- **No change to the link protocol.** `link.h`, `link.cpp` and `link-protocol.md` are not touched by any task in this plan.
- **Silence is never an acceptable failure mode.** Every rejected file prints one serial line naming the file and the field that failed.
- **Bilingual comments.** Every non-trivial comment gets an English line and a `// [TH]` line, matching every file in `firmware/`.
- **No AI attribution anywhere**, commit messages included.
- **Bench switches are `0` in every commit:** `UI_TEST_HOOK`, `LINK_OVER_USB`. `flash.ps1 -Check` enforces this.
- **Pull the controller↔CYD UART jumpers before any USB flash.** Left connected, the board being flashed is driven by a board that is not in reset, which fakes boot-mode and checksum failures.

### Hardware gate — read before Task 3

The user has an 8 Ω speaker and **no confirmed amplifier**. GPIO 26 is a DAC pin sourcing a few milliamps; an 8 Ω voice coil across it is close to a short, and the DC bias on that coil is the second problem.

**Task 3 Step 1 is the amp check. No speaker is connected to anything until it passes.** Tasks 1 and 2 are pure software and can be completed before any of this is known.

---

## File Structure

**Created:**

| File | Responsibility |
|---|---|
| `firmware/pt-leg-display/wav.h` | WAV header contract: `WavInfo`, `WavError`, `wavParse()`. No Arduino includes. |
| `firmware/pt-leg-display/wav.cpp` | Chunk-walking parser. Pure byte logic — compiles for both ESP32 and PC. |
| `firmware/tests/wav_test.cpp` | Host tests for the parser. Plain asserts, no framework. |
| `firmware/tests/run-tests.ps1` | Builds and runs the host tests with `g++`. |
| `firmware/pt-leg-display/audio.h` | Cue enum and the five-function module API. |
| `firmware/pt-leg-display/audio.cpp` | DAC channel, heap buffer, load/play state machine, cue→filename table. |
| `firmware/tools/make-sounds.ps1` | `ffmpeg` wrapper producing files in the one accepted format. |

**Modified:**

| File | Change |
|---|---|
| `firmware/pt-leg-display/settings.h` | `bool soundOn` added. **Shared file** — sync to the controller in the same commit. |
| `firmware/pt-leg-controller/settings.h` | Receives the sync. No controller code reads the field. |
| `firmware/pt-leg-display/settingsfile.cpp` | `soundOn` in defaults, load, and save. |
| `firmware/pt-leg-display/ui.h` / `ui.cpp` | One `lv_switch` row in `colSettings`; read in `saveClicked`, set in `uiOpenSettings`. |
| `firmware/pt-leg-display/pt-leg-display.ino` | `audioBegin`/`audioLoop`, the three cue trigger points, `gLast` initializer. |
| `firmware/pin-layout.html` | GPIO 26 added to the display board's tables, bilingual. |
| `firmware/README.md` | `/sounds` layout, `make-sounds.ps1`, `run-tests.ps1`, the audio bench checklist. |

**Why `wav` is split from `audio`:** the parser is the only part of this feature with branching logic and no hardware dependency. Split out, it gets real red-green tests on a PC; folded into `audio.cpp` it would be reachable only by flashing a board and listening. `audio.cpp` is then almost entirely hardware plumbing, which is honest about what can and cannot be automatically verified.

---

## Task 1: WAV parser and the host test harness

**Files:**
- Create: `firmware/pt-leg-display/wav.h`
- Create: `firmware/pt-leg-display/wav.cpp`
- Test: `firmware/tests/wav_test.cpp`
- Create: `firmware/tests/run-tests.ps1`

**Interfaces:**
- Consumes: nothing.
- Produces: `WavInfo{uint32_t dataOffset; uint32_t dataBytes;}`, `enum class WavError`, `const char* wavErrorText(WavError)`, `WavError wavParse(const uint8_t* hdr, size_t hdrLen, uint32_t fileLen, uint32_t maxData, WavInfo& out)`, `WAV_RATE == 22050`, `WAV_SCAN_BYTES == 256`. Task 4 calls `wavParse`; Task 2's tool must emit files it accepts.

- [ ] **Step 1: Write the header**

Create `firmware/pt-leg-display/wav.h`:

```cpp
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
```

- [ ] **Step 2: Write the failing tests**

Create `firmware/tests/wav_test.cpp`:

```cpp
#include "../pt-leg-display/wav.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

static int failures = 0;
static int checks = 0;

static void check(bool ok, const char* what) {
  ++checks;
  if (!ok) {
    ++failures;
    printf("FAIL: %s\n", what);
  }
}

static void put16(std::vector<uint8_t>& v, uint16_t x) {
  v.push_back((uint8_t)(x & 0xFF));
  v.push_back((uint8_t)((x >> 8) & 0xFF));
}

static void put32(std::vector<uint8_t>& v, uint32_t x) {
  v.push_back((uint8_t)(x & 0xFF));
  v.push_back((uint8_t)((x >> 8) & 0xFF));
  v.push_back((uint8_t)((x >> 16) & 0xFF));
  v.push_back((uint8_t)((x >> 24) & 0xFF));
}

static void putTag(std::vector<uint8_t>& v, const char* t) {
  for (int i = 0; i < 4; ++i) v.push_back((uint8_t)t[i]);
}

struct Fmt {
  uint16_t format = 1;
  uint16_t channels = 1;
  uint16_t bits = 8;
  uint32_t rate = 22050;
};

// Builds a syntactically complete WAV. extraChunk, when given, is inserted
// between fmt and data — which is exactly what ffmpeg does with LIST.
static std::vector<uint8_t> makeWav(uint32_t dataBytes, Fmt f = Fmt{},
                                    const char* extraChunk = nullptr, uint32_t extraSize = 0) {
  std::vector<uint8_t> v;
  putTag(v, "RIFF");
  put32(v, 0);  // patched below
  putTag(v, "WAVE");

  putTag(v, "fmt ");
  put32(v, 16);
  put16(v, f.format);
  put16(v, f.channels);
  put32(v, f.rate);
  put32(v, f.rate * f.channels * (f.bits / 8));
  put16(v, (uint16_t)(f.channels * (f.bits / 8)));
  put16(v, f.bits);

  if (extraChunk) {
    putTag(v, extraChunk);
    put32(v, extraSize);
    for (uint32_t i = 0; i < extraSize; ++i) v.push_back(0xAA);
    if (extraSize & 1u) v.push_back(0);  // chunks are word-aligned
  }

  putTag(v, "data");
  put32(v, dataBytes);
  for (uint32_t i = 0; i < dataBytes; ++i) v.push_back(0x80);

  uint32_t riff = (uint32_t)v.size() - 8;
  v[4] = (uint8_t)(riff & 0xFF);
  v[5] = (uint8_t)((riff >> 8) & 0xFF);
  v[6] = (uint8_t)((riff >> 16) & 0xFF);
  v[7] = (uint8_t)((riff >> 24) & 0xFF);
  return v;
}

static WavError run(const std::vector<uint8_t>& v, WavInfo& out, uint32_t maxData = 48u * 1024u) {
  size_t n = v.size() < WAV_SCAN_BYTES ? v.size() : WAV_SCAN_BYTES;
  return wavParse(v.data(), n, (uint32_t)v.size(), maxData, out);
}

int main() {
  WavInfo info{};

  // A canonical 44-byte-header file is accepted, and the offsets point at the
  // samples rather than at the header.
  {
    auto v = makeWav(100);
    check(run(v, info) == WavError::Ok, "canonical file accepted");
    check(info.dataOffset == 44, "canonical data offset is 44");
    check(info.dataBytes == 100, "canonical data length is 100");
  }

  // The whole reason the parser walks chunks instead of seeking to 44.
  {
    auto v = makeWav(64, Fmt{}, "LIST", 26);
    check(run(v, info) == WavError::Ok, "LIST chunk before data accepted");
    check(info.dataOffset == 44 + 8 + 26, "data offset skips the LIST chunk");
    check(info.dataBytes == 64, "data length correct past a LIST chunk");
  }

  // An odd chunk size is followed by a pad byte. Ignoring it desynchronises
  // every chunk after it.
  {
    auto v = makeWav(64, Fmt{}, "LIST", 25);
    check(run(v, info) == WavError::Ok, "odd-sized chunk accepted");
    check(info.dataOffset == 44 + 8 + 25 + 1, "data offset accounts for the pad byte");
  }

  {
    Fmt f;
    f.bits = 16;
    auto v = makeWav(100, f);
    check(run(v, info) == WavError::NotEightBit, "16-bit rejected");
  }
  {
    Fmt f;
    f.channels = 2;
    auto v = makeWav(100, f);
    check(run(v, info) == WavError::NotMono, "stereo rejected");
  }
  {
    Fmt f;
    f.rate = 44100;
    auto v = makeWav(100, f);
    check(run(v, info) == WavError::WrongRate, "44100 Hz rejected");
  }
  {
    Fmt f;
    f.format = 3;  // IEEE float
    auto v = makeWav(100, f);
    check(run(v, info) == WavError::NotPcm, "non-PCM rejected");
  }
  {
    auto v = makeWav(100);
    v[0] = 'X';
    check(run(v, info) == WavError::NotRiff, "non-RIFF rejected");
  }
  {
    auto v = makeWav(100);
    v[8] = 'X';
    check(run(v, info) == WavError::NotWave, "non-WAVE rejected");
  }
  {
    // The header claims 100 samples; the file holds 20.
    auto v = makeWav(100);
    v.resize(v.size() - 80);
    check(run(v, info) == WavError::Truncated, "data past end of file rejected");
  }
  {
    auto v = makeWav(100);
    check(run(v, info, 50) == WavError::TooBig, "data larger than the buffer rejected");
  }
  {
    auto v = makeWav(0);
    check(run(v, info) == WavError::NoData, "empty data chunk rejected");
  }
  {
    std::vector<uint8_t> v;
    putTag(v, "RIFF");
    put32(v, 4);
    putTag(v, "WAVE");
    check(run(v, info) == WavError::NoFmt, "header with no chunks rejected");
  }

  // Every error must have text; a switch that falls through to "unknown" is a
  // rejection nobody can act on.
  for (int e = 0; e <= (int)WavError::TooBig; ++e) {
    check(strcmp(wavErrorText((WavError)e), "unknown") != 0, "every WavError has text");
  }

  printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
```

- [ ] **Step 3: Write the test runner**

Create `firmware/tests/run-tests.ps1`:

```powershell
# [TH] คอมไพล์และรันชุดทดสอบที่รันบนคอมพิวเตอร์ได้ ไม่ต้องใช้บอร์ด
#
# Builds and runs the host-side firmware tests.
#
# Only hardware-free code is testable this way. wav.cpp parses bytes and knows
# nothing about the DAC, the card or LVGL, so it compiles for a PC. Everything
# else in pt-leg-display needs the board and is covered by the bench checklist
# in firmware/README.md instead — do not pretend otherwise by adding stubs.
# [TH] ทดสอบแบบนี้ได้เฉพาะโค้ดที่ไม่ยุ่งกับฮาร์ดแวร์ ไฟล์ wav.cpp อ่านแต่ไบต์
#      ไม่รู้จัก DAC การ์ด หรือ LVGL จึงคอมไพล์บนคอมพิวเตอร์ได้
#      ส่วนที่เหลือต้องใช้บอร์ดจริง และใช้รายการตรวจบนโต๊ะใน firmware/README.md แทน
#      อย่าสร้างตัวจำลองขึ้นมาเพื่อให้ดูเหมือนทดสอบได้
#
#   .\firmware\tests\run-tests.ps1
param([string]$Compiler = "g++")
$ErrorActionPreference = "Stop"

$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$outDir = Join-Path $here "..\.build\test-bin"
New-Item -ItemType Directory -Force $outDir | Out-Null
$exe = Join-Path $outDir "wav_test.exe"

& $Compiler -std=c++17 -Wall -Wextra -Werror -O1 `
    (Join-Path $here "wav_test.cpp") `
    (Join-Path $here "..\pt-leg-display\wav.cpp") `
    -o $exe
if ($LASTEXITCODE -ne 0) { throw "compile failed" }

& $exe
if ($LASTEXITCODE -ne 0) { throw "tests failed" }
Write-Host "tests passed" -ForegroundColor Green
```

- [ ] **Step 4: Run the tests to verify they fail**

```bash
powershell -File firmware/tests/run-tests.ps1
```

Expected: **compile failure** — `wav.cpp` does not exist yet, so the link step reports undefined references to `wavParse` and `wavErrorText`.

- [ ] **Step 5: Write the parser**

Create `firmware/pt-leg-display/wav.cpp`:

```cpp
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
```

- [ ] **Step 6: Run the tests to verify they pass**

```bash
powershell -File firmware/tests/run-tests.ps1
```

Expected: `29 checks, 0 failures` then `tests passed`, preceded by a `SKIP:` line about `firmware/.build/sounds/start.wav` (Task 2 supplies that file). `-Werror` is on, so any warning is also a failure.

- [ ] **Step 7: Commit**

```bash
git add firmware/pt-leg-display/wav.h firmware/pt-leg-display/wav.cpp firmware/tests/wav_test.cpp firmware/tests/run-tests.ps1
git commit -m "feat(firmware): WAV header parser with host tests"
```

---

## Task 2: The sound conversion tool

Placed before the audio module on purpose: Task 4 cannot be bench-tested without a file that Task 1's parser accepts, and hand-rolling one is how a bad file becomes the first thing you debug.

**Files:**
- Create: `firmware/tools/make-sounds.ps1`
- Modify: `firmware/tests/wav_test.cpp` (one check that a real tool output parses)

**Interfaces:**
- Consumes: `wavParse` and `WAV_RATE` from Task 1.
- Produces: `/sounds/boot.wav`, `start.wav`, `stop.wav`, `target.wav` in the accepted format. Task 4 reads these filenames from the card.

- [ ] **Step 1: Write the tool**

Create `firmware/tools/make-sounds.ps1`:

```powershell
# [TH] แปลงไฟล์เสียงที่อัดมาให้เป็นรูปแบบเดียวที่เฟิร์มแวร์ยอมรับ
#
# Converts a recording into the one format pt-leg-display accepts, and refuses
# to write a file it would reject: 22050 Hz, 8-bit unsigned, mono, no metadata
# chunks, silence-padded at both ends.
#
#   .\firmware\tools\make-sounds.ps1 -In .\raw\start.m4a -Out .\sd\sounds\start.wav
#   .\firmware\tools\make-sounds.ps1 -In .\raw -Out .\sd\sounds        # whole folder
#
# The padding is not cosmetic. The DAC holds the last code it was given, so a
# clip that ends on a loud sample leaves the output there until the channel is
# disabled, and disabling it then steps the output audibly. Both ends are padded
# with 0x80, which is silence for unsigned 8-bit and the DAC's rest value.
# [TH] การเติมความเงียบหัวท้ายไม่ใช่เรื่องความสวยงาม
#      DAC จะค้างค่าสุดท้ายที่ได้รับไว้ ถ้าคลิปจบด้วยตัวอย่างที่ดัง เอาต์พุตจะค้างอยู่ตรงนั้น
#      พอปิดช่องสัญญาณก็จะกระโดดจนได้ยินเป็นเสียงป๊อก จึงเติม 0x80 ทั้งสองด้าน
#      ซึ่งเป็นความเงียบของรูปแบบ 8 บิตไม่มีเครื่องหมาย และเป็นค่าพักของ DAC
param(
    [Parameter(Mandatory = $true)][string]$In,
    [Parameter(Mandatory = $true)][string]$Out,
    [int]$PadMs = 20,
    [int]$MaxBytes = 49152,   # must match AUDIO_BUF_BYTES in audio.cpp
    [switch]$NoNormalise
)
$ErrorActionPreference = "Stop"

if (-not (Get-Command ffmpeg -CommandType Application -ErrorAction SilentlyContinue)) {
    throw "ffmpeg was not found in PATH. Install it before running this script."
}

$rate = 22050
$pad = ($PadMs / 1000.0).ToString([System.Globalization.CultureInfo]::InvariantCulture)

# Speech recorded on a phone lands well below full scale, and there is no gain
# stage anywhere after this point — the firmware plays the samples as they are.
# Loudness is decided here or not at all.
# [TH] เสียงพูดที่อัดจากโทรศัพท์มักเบากว่าระดับเต็มมาก และหลังจากขั้นตอนนี้ไม่มีการปรับความดังอีกเลย
#      เฟิร์มแวร์เล่นตัวอย่างเสียงตามที่เป็น ความดังจึงถูกกำหนดที่นี่เท่านั้น
$filters = @("aresample=$rate", "aformat=sample_fmts=u8:channel_layouts=mono")
if (-not $NoNormalise) { $filters = @("loudnorm=I=-16:TP=-1.5:LRA=11") + $filters }
$filters += "adelay=${PadMs}|${PadMs}"
$filters += "apad=pad_dur=$pad"
$chain = $filters -join ","

function Convert-One([string]$src, [string]$dst) {
    $dir = Split-Path -Parent $dst
    if ($dir -and -not (Test-Path $dir)) { New-Item -ItemType Directory -Force $dir | Out-Null }

    # -map_metadata -1 and -fflags +bitexact keep ffmpeg from writing a LIST
    # chunk. The parser walks chunks and would cope, but only inside its 256-byte
    # scan window — not writing the chunk at all removes the question.
    # [TH] สองตัวเลือกนี้กัน ffmpeg ไม่ให้เขียนชังก์ LIST
    #      ตัวแยกไล่อ่านชังก์อยู่แล้วจึงรับมือได้ แต่รับได้เฉพาะใน 256 ไบต์แรก
    #      การไม่เขียนชังก์นั้นตั้งแต่ต้นตัดปัญหาทิ้งไปเลย
    & ffmpeg -hide_banner -loglevel error -y -i $src `
        -af $chain -ac 1 -ar $rate -acodec pcm_u8 `
        -map_metadata -1 -fflags +bitexact $dst
    if ($LASTEXITCODE -ne 0) { throw "ffmpeg failed on $src" }

    $size = (Get-Item $dst).Length
    $samples = $size - 44
    $seconds = [math]::Round($samples / $rate, 2)
    if ($samples -gt $MaxBytes) {
        Remove-Item $dst -Force
        throw ("$src is ${seconds}s = $samples bytes of audio, over the ${MaxBytes}-byte " +
               "buffer. Record a shorter prompt; the firmware would reject this file.")
    }
    Write-Host ("{0,-16} {1,6}s  {2,6} bytes" -f (Split-Path -Leaf $dst), $seconds, $samples)
}

if (Test-Path $In -PathType Container) {
    Get-ChildItem $In -File | ForEach-Object {
        Convert-One $_.FullName (Join-Path $Out ($_.BaseName + ".wav"))
    }
} else {
    Convert-One $In $Out
}
Write-Host "done" -ForegroundColor Green
```

- [ ] **Step 2: Generate a real file and check it parses**

Generated audio lives under `firmware/.build/`, which `.gitignore` already covers — so nothing here can be committed by accident and no `.gitignore` change is needed.

Record or synthesise any short clip as `firmware/.build/raw/start.m4a`, then:

```bash
powershell -File firmware/tools/make-sounds.ps1 -In firmware/.build/raw/start.m4a -Out firmware/.build/sounds/start.wav
```

Expected: one line reporting duration and byte count, then `done`. A clip over 2.23 s must be **refused with the over-buffer message**, not written — test that too by feeding it a long recording.

- [ ] **Step 3: Add a test that the tool's real output parses**

Append to `main()` in `firmware/tests/wav_test.cpp`, before the `printf`:

```cpp
  // A file produced by tools/make-sounds.ps1, when one is present. This is the
  // loop that matters: the tool's output is fed through the same parser the
  // firmware uses, so a tool change that breaks the format fails here rather
  // than on the bench.
  {
    FILE* f = fopen("firmware/.build/sounds/start.wav", "rb");
    if (f) {
      std::vector<uint8_t> v;
      uint8_t chunk[4096];
      size_t n;
      while ((n = fread(chunk, 1, sizeof(chunk), f)) > 0) v.insert(v.end(), chunk, chunk + n);
      fclose(f);
      WavError e = run(v, info);
      check(e == WavError::Ok, "make-sounds.ps1 output is accepted by the parser");
      if (e != WavError::Ok) printf("       parser said: %s\n", wavErrorText(e));
    } else {
      printf("SKIP: firmware/.build/sounds/start.wav absent; run tools/make-sounds.ps1 to cover this\n");
    }
  }
```

- [ ] **Step 4: Run the tests**

```bash
powershell -File firmware/tests/run-tests.ps1
```

Expected: `21 checks, 0 failures`. If the scratch file is absent the SKIP line prints and the count stays at 20 — that is a legitimate pass, but the tool has then not been verified end to end, so generate the file.

- [ ] **Step 5: Record the four real prompts**

Record these, one file each. Content is spoken Thai:

| File | Spoken |
|---|---|
| `boot.wav` | เครื่องพร้อมใช้งาน |
| `start.wav` | เริ่มการทำงาน |
| `stop.wav` | หยุดการทำงาน |
| `target.wav` | ครบตามเป้าหมายแล้ว |

```bash
powershell -File firmware/tools/make-sounds.ps1 -In firmware/.build/raw -Out firmware/.build/sounds
```

Copy the four results to `/sounds/` on the SD card. **Do not commit the `.wav` files** — they are card content, not source, and `logo_img.c` is the only place binary media belongs in this repo.

- [ ] **Step 6: Commit**

```bash
git add firmware/tools/make-sounds.ps1 firmware/tests/wav_test.cpp
git commit -m "feat(firmware): tool to convert recordings to the accepted WAV format"
```

---

## Task 3: The amp check, then the audio module skeleton

**This task's first step is a hardware measurement, not code.** Everything after it assumes a known-good output path.

**Files:**
- Create: `firmware/pt-leg-display/audio.h`
- Create: `firmware/pt-leg-display/audio.cpp`
- Modify: `firmware/pt-leg-display/pt-leg-display.ino` (`setup()`, `loop()`)

**Interfaces:**
- Consumes: `wav.h` from Task 1 (included, not yet called).
- Produces: `enum class Cue { Boot, Start, Stop, Target }`, `void audioBegin()`, `void audioLoop()`, `void audioPlay(Cue)`, `void audioSetEnabled(bool)`, `bool audioReady()`. Task 4 fills in playback; Task 6 calls `audioSetEnabled`; Task 7 calls `audioPlay`.

- [ ] **Step 1: The amp check — before a speaker touches anything**

With **no speaker connected**, flash a throwaway sketch that runs `dacWrite(26, 128);` in `setup()`, then measure with a multimeter:

| Probe | Reading | Means |
|---|---|---|
| GPIO 26 to GND | ~1.65 V DC | The DAC is working. Expected in all cases. |
| Speaker JST to GND | ~0 V DC | An amp is populated. **Proceed — connect the 8 Ω speaker.** |
| Speaker JST to GND | ~1.65 V DC | **No amp.** The JST is wired straight to GPIO 26. **Do not connect the speaker.** |

If there is no amp: fit a PAM8403 or MAX98357A module between GPIO 26 and the speaker, powered from 5 V, and record which board revision this was in `firmware/README.md`. No firmware in this plan changes either way.

Record the outcome before continuing. Every later step assumes the speaker is safely driven.

- [ ] **Step 2: Write the module header**

Create `firmware/pt-leg-display/audio.h`:

```cpp
#pragma once
#include <stdint.h>

// เสียงพูดภาษาไทยจากการ์ด SD ผ่านลำโพงของบอร์ดจอ
// Spoken Thai cues, played from /sounds on the SD card through the CYD's
// speaker output on GPIO 26.
//
// This module never touches LVGL and the UI does not know it exists: the sketch
// is the only caller. It opens the card directly, the way sdlog.cpp and
// settingsfile.cpp do — the rule that the card is not touched directly is a rule
// about ui.cpp, not about modules.
// [TH] โมดูลนี้ไม่ยุ่งกับ LVGL และฝั่ง UI ไม่รู้ว่ามีโมดูลนี้อยู่ มีแต่สเก็ตช์ที่เรียกใช้
//      โมดูลเปิดการ์ดเอง เหมือนที่ sdlog.cpp และ settingsfile.cpp ทำ
//      กติกาที่ห้ามแตะการ์ดโดยตรงเป็นกติกาของ ui.cpp ไม่ใช่ของโมดูล
//
// Audio is strictly additive: nothing here stops the machine, blocks a session,
// or changes what the screen shows. Every failure ends in silence plus one
// serial line, never in a stall.
// [TH] เสียงเป็นส่วนเสริมล้วนๆ ไม่มีอะไรในนี้ที่หยุดเครื่อง ขวางรอบการทำงาน หรือเปลี่ยนสิ่งที่จอแสดง
//      ความผิดพลาดทุกแบบจบที่ความเงียบพร้อมข้อความหนึ่งบรรทัด ไม่ใช่การค้าง

enum class Cue {
  Boot,    // เครื่องพร้อมใช้งาน — /sounds/boot.wav
  Start,   // เริ่มการทำงาน — /sounds/start.wav
  Stop,    // หยุดการทำงาน — /sounds/stop.wav
  Target,  // ครบตามเป้าหมายแล้ว — /sounds/target.wav
};

// Allocates the playback buffer and creates the DAC channel. Safe to call with
// no card in the slot.
// [TH] จองบัฟเฟอร์และสร้างช่องสัญญาณ DAC เรียกได้แม้ไม่มีการ์ดในช่อง
void audioBegin();

// Called every loop(). Does at most one 4 KB card read or one non-blocking DMA
// write per call, so it must never hold the loop for more than about a
// millisecond — LVGL and the link poll share it.
// [TH] เรียกทุกลูป ต่อหนึ่งครั้งจะอ่านการ์ดไม่เกิน 4 กิโลไบต์ หรือเขียน DMA แบบไม่รอหนึ่งครั้ง
//      จึงต้องไม่ยึดลูปไว้เกินราวหนึ่งมิลลิวินาที เพราะ LVGL และการอ่านลิงก์ใช้ลูปเดียวกัน
void audioLoop();

// Requests a cue. The newest request wins: one already loading or playing is
// abandoned, because the event that just happened is the one worth hearing.
// [TH] ขอเล่นเสียงหนึ่งรายการ คำขอล่าสุดชนะเสมอ รายการที่กำลังโหลดหรือเล่นอยู่จะถูกยกเลิก
//      เพราะเหตุการณ์ที่เพิ่งเกิดคือเรื่องที่ควรได้ยิน
void audioPlay(Cue c);

// From the settings screen. Off drops requests inside audioPlay(), before any
// card access.
// [TH] มาจากหน้าตั้งค่า ถ้าปิดอยู่ คำขอจะถูกทิ้งใน audioPlay() ก่อนแตะการ์ด
void audioSetEnabled(bool on);

// False when the buffer or the DAC channel could not be had at boot. The machine
// then runs exactly as it does today, silently.
// [TH] เป็นเท็จเมื่อจองบัฟเฟอร์หรือสร้างช่อง DAC ไม่สำเร็จตอนบูต
//      เครื่องจะทำงานเหมือนเดิมทุกอย่าง เพียงแต่ไม่มีเสียง
bool audioReady();
```

- [ ] **Step 3: Write the module — allocation and DAC only, no playback yet**

Create `firmware/pt-leg-display/audio.cpp`:

```cpp
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
  cfg.buf_size = 1024;  // 4 KB of DMA in total
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
```

- [ ] **Step 4: Wire it into the sketch**

In `firmware/pt-leg-display/pt-leg-display.ino`, add the include next to the others:

```cpp
#include "audio.h"
```

In `setup()`, immediately after the `storeBegin()` call:

```cpp
  audioBegin();
```

In `loop()`, next to `uiLoop()`:

```cpp
  audioLoop();
```

- [ ] **Step 5: Compile**

```bash
powershell -File firmware/tools/flash.ps1 -Action compile -Board display
```

Expected: builds clean. Note the reported flash usage — the display sketch is the large one, and `PartitionScheme=huge_app` is what makes it fit.

- [ ] **Step 6: Flash and bench-check**

Pull the UART jumpers, then:

```bash
powershell -File firmware/tools/flash.ps1 -Action upload -Board display
```

Reconnect the jumpers and power both boards. Pass criteria:

1. Serial prints `audio: 49152 byte buffer (2.23 s), DAC on GPIO 26`. A smaller buffer is a pass but worth noting — it means heap is tighter than expected.
2. **The touchscreen still works.** Open the settings screen, tap fields, use the keyboard. This is the CH1-mask check and the single most important line in this task.
3. No boot loop, no brownout, no change to anything on screen.

- [ ] **Step 7: Commit**

```bash
git add firmware/pt-leg-display/audio.h firmware/pt-leg-display/audio.cpp firmware/pt-leg-display/pt-leg-display.ino
git commit -m "feat(firmware): DAC channel and playback buffer on the display board"
```

---

## Task 4: Playback

**Files:**
- Modify: `firmware/pt-leg-display/audio.cpp`
- Modify: `firmware/pt-leg-display/pt-leg-display.ino` (boot cue at the end of `setup()`)

**Interfaces:**
- Consumes: `wavParse`, `wavErrorText`, `WavInfo`, `WAV_SCAN_BYTES` (Task 1); the buffer and DAC handle from Task 3.
- Produces: a working `audioPlay(Cue)` / `audioLoop()` pair. Task 7 relies on `audioPlay` being safe to call from a link callback.

- [ ] **Step 1: Add the state machine**

Replace the two stub functions in `firmware/pt-leg-display/audio.cpp` and add the state above them:

```cpp
// Idle ──audioPlay()──▶ Loading ──buffer full──▶ Playing ──drained──▶ Idle
// [TH] ว่าง ──ขอเล่น──▶ กำลังโหลด ──โหลดครบ──▶ กำลังเล่น ──เล่นจบ──▶ ว่าง
enum class AudioState { Idle, Loading, Playing };

static AudioState gState = AudioState::Idle;
static File gFile;
static uint32_t gWant = 0;   // ไบต์ที่ต้องโหลดทั้งหมด — total bytes to load
static uint32_t gHave = 0;   // โหลดมาแล้ว — bytes loaded so far
static uint32_t gSent = 0;   // ส่งให้ DAC แล้ว — bytes handed to the DAC
static bool gDacOn = false;

static constexpr size_t CHUNK_BYTES = 4096;

static const char* cuePath(Cue c) {
  switch (c) {
    case Cue::Boot: return "/sounds/boot.wav";
    case Cue::Start: return "/sounds/start.wav";
    case Cue::Stop: return "/sounds/stop.wav";
    case Cue::Target: return "/sounds/target.wav";
  }
  return nullptr;
}

// Always leaves the DAC on 0x80. The channel holds the last code it was given,
// so disabling on a loud sample steps the output and pops; 0x80 is silence for
// unsigned 8-bit and the converter's rest value.
// [TH] ปิดโดยทิ้งค่า 0x80 ไว้เสมอ ช่องสัญญาณจะค้างค่าสุดท้ายที่ได้รับ
//      ถ้าปิดตอนที่ค่ายังดังอยู่ เอาต์พุตจะกระโดดจนได้ยินเป็นเสียงป๊อก
//      ส่วน 0x80 คือความเงียบของรูปแบบ 8 บิตไม่มีเครื่องหมาย และเป็นค่าพักของตัวแปลง
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
  // for the next loop() rather than stalling LVGL here.
  // [TH] กำลังเล่น การตั้งเวลารอเป็น 0 ทำให้เป็นการเขียนแบบไม่รอและอาจไม่ครบ
  //      คือรับเท่าที่ตัวอธิบาย DMA ว่างแล้วบอกว่ารับไปเท่าไร ที่เหลือรอลูปถัดไป
  //      แทนที่จะยึด LVGL ไว้ตรงนี้
  size_t loaded = 0;
  esp_err_t err = dac_continuous_write(gDac, gBuf + gSent, gWant - gSent, &loaded, 0);
  if (err != ESP_OK && err != ESP_ERR_TIMEOUT) {
    Serial.printf("audio: dac_continuous_write failed (%d)\n", (int)err);
    stopAll();
    return;
  }
  gSent += (uint32_t)loaded;
  if (gSent < gWant) return;

  // Everything is in the DMA queue, not yet out of the speaker. Disabling now
  // would cut the tail off, so wait out the samples still in flight: 4 KB of DMA
  // at 22050 Hz is about 186 ms.
  // [TH] ข้อมูลเข้าคิว DMA ครบแล้ว แต่ยังไม่ออกลำโพงจนหมด ถ้าปิดตอนนี้จะตัดหางเสียงทิ้ง
  //      จึงต้องรอตัวอย่างที่ค้างอยู่ในคิว คือ 4 กิโลไบต์ที่ 22050 เฮิรตซ์ ราว 186 มิลลิวินาที
  static uint32_t drainStart = 0;
  if (drainStart == 0) drainStart = millis();
  if ((uint32_t)(millis() - drainStart) < 250) return;
  drainStart = 0;
  stopAll();
}
```

Add the two includes at the top of `audio.cpp`, after `<Arduino.h>`:

```cpp
#include <SD.h>
```

- [ ] **Step 2: Fix the drain timer's scope**

The `static uint32_t drainStart` above lives inside `audioLoop()`, so `stopAll()` called from `audioPlay()` cannot reset it — a cue interrupted during its drain would leave a stale timestamp. Move it to file scope next to `gSent` and clear it in `stopAll()`:

```cpp
static uint32_t gDrainStart = 0;
```

In `stopAll()`, add:

```cpp
  gDrainStart = 0;
```

and replace the three uses inside `audioLoop()` with `gDrainStart`.

- [ ] **Step 3: Raise the boot cue**

In `firmware/pt-leg-display/pt-leg-display.ino`, as the **last statement** of `setup()`:

```cpp
  // Last, so hearing it means the whole init path finished — not merely that the
  // DAC came up. It is the cheapest end-to-end self-test this board has.
  // [TH] วางไว้ท้ายสุด การได้ยินเสียงนี้จึงแปลว่าขั้นตอนเริ่มระบบผ่านครบ
  //      ไม่ใช่แค่ว่า DAC ทำงาน เป็นการทดสอบตั้งแต่ต้นจนจบที่ถูกที่สุดของบอร์ดนี้
  audioPlay(Cue::Boot);
```

- [ ] **Step 4: Compile**

```bash
powershell -File firmware/tools/flash.ps1 -Action compile -Board display
```

Expected: builds clean.

- [ ] **Step 5: Flash and bench-check**

Card must hold `/sounds/boot.wav` from Task 2. Jumpers off to flash, back on to run.

| Check | Pass |
|---|---|
| Boot cue | "เครื่องพร้อมใช้งาน" plays once on a real power cycle, after the UI is up. |
| Intelligibility | The words are recognisable. If not, re-record — this is the 8-bit quantisation risk the spec flags. |
| Pop | No click at the start or end of the clip. |
| Touch | Still works after the clip finishes. |
| Bad file | Copy a 16-bit WAV over `boot.wav`. Serial says `audio: /sounds/boot.wav rejected — not 8-bit`. No noise. Restore the good file after. |
| No card | Pull the card and boot. Serial says the file could not be opened; the machine runs normally. |
| Heap | `ESP.getFreeHeap()` is unchanged between two boots. |

- [ ] **Step 6: Commit**

```bash
git add firmware/pt-leg-display/audio.cpp firmware/pt-leg-display/pt-leg-display.ino
git commit -m "feat(firmware): play WAV cues from the SD card"
```

---

## Task 5: The `soundOn` setting

**Files:**
- Modify: `firmware/pt-leg-display/settings.h`
- Modify: `firmware/pt-leg-controller/settings.h` (via `sync-shared.ps1`, not by hand)
- Modify: `firmware/pt-leg-display/settingsfile.cpp`
- Modify: `firmware/pt-leg-display/pt-leg-display.ino` (`gLast` initializer)

**Interfaces:**
- Consumes: `audioSetEnabled(bool)` from Task 3.
- Produces: `DeviceSettings::soundOn` (bool, default `true`), persisted as the `soundOn` key in `/settings.json`. Task 6 reads and writes it through the UI.

- [ ] **Step 1: Add the field to the shared header**

Edit **`firmware/pt-leg-controller/settings.h`** — the controller's copy is the source of truth. `sync-shared.ps1` copies controller → display, so an edit made to the display's copy is silently overwritten by the very next sync, and `-Check` then reports "in sync" because both are back to the old version.

Inside `struct DeviceSettings`, after `targetReps`:

```cpp
  // เปิดเสียงพูดบนบอร์ดจอ — spoken cues on the display board.
  //
  // Positive sense everywhere: JSON key, struct field, switch label and API all
  // mean the same thing. A `muted` field behind a "เสียง" switch is an
  // inversion, and inversions get flipped by whoever touches them next.
  // [TH] ใช้ความหมายเชิงบวกทุกที่ ทั้งคีย์ใน JSON ฟิลด์ในโครงสร้าง ป้ายบนสวิตช์ และ API
  //      ถ้าเก็บเป็น "ปิดเสียง" แต่ป้ายเขียนว่า "เสียง" ค่าจะกลับด้าน
  //      และค่าที่กลับด้านจะถูกสลับผิดโดยคนที่มาแก้ทีหลัง
  //
  // The controller never reads this: the speaker is on the display board, so the
  // setting belongs to the board that owns the hardware. Carrying it in the
  // shared struct is the price of having one settings type, and it deliberately
  // does NOT appear in LinkSettings — it never crosses the wire.
  // [TH] ฝั่งคอนโทรลเลอร์ไม่เคยอ่านค่านี้ ลำโพงอยู่บนบอร์ดจอ ค่านี้จึงเป็นของบอร์ดที่มีฮาร์ดแวร์นั้น
  //      การมีค่านี้ในโครงสร้างที่ใช้ร่วมกันคือราคาของการมีชนิดข้อมูลตั้งค่าเพียงชนิดเดียว
  //      และจงใจไม่ใส่ไว้ใน LinkSettings ค่านี้จึงไม่เคยถูกส่งข้ามสาย
  bool soundOn;
```

- [ ] **Step 2: Sync the shared file to the controller**

```bash
powershell -File firmware/sync-shared.ps1
```

Then confirm no drift remains:

```bash
powershell -File firmware/sync-shared.ps1 -Check
```

Expected: `shared files in sync`. **Do not edit `pt-leg-controller/settings.h` by hand** — the script is what keeps the two byte-identical, and a hand edit that differs by a comment fails the next `flash.ps1 -Check`.

- [ ] **Step 3: Add it to defaults, load and save**

In `firmware/pt-leg-display/settingsfile.cpp`:

In `settingsFileDefaults()`, after `s.targetReps = 10;`:

```cpp
  s.soundOn = true;
```

In `loadFrom()`, after the `out.targetReps` line:

```cpp
  // No clamp: a bool read through ArduinoJson's default operator is already
  // total, and an absent key means an older card, which should arrive with
  // sound on rather than silently mute.
  // [TH] ไม่ต้องบีบช่วงค่า เพราะค่าบูลีนที่อ่านผ่านตัวดำเนินการค่าตั้งต้นของ ArduinoJson ครอบคลุมอยู่แล้ว
  //      คีย์ที่หายไปแปลว่าเป็นการ์ดรุ่นเก่า ซึ่งควรมาพร้อมเสียงเปิด ไม่ใช่เงียบไปเฉยๆ
  out.soundOn = d["soundOn"] | true;
```

In `settingsFileSave()`, after `d["targetReps"] = s.targetReps;`:

```cpp
  d["soundOn"] = s.soundOn;
```

- [ ] **Step 4: Fix the `gLast` initializer**

In `firmware/pt-leg-display/pt-leg-display.ino`, the aggregate initializer at file scope currently reads:

```cpp
static DeviceSettings gLast = {"", "", "", "default", 8, 4, 4, 10, 10, 10};
```

Change it to:

```cpp
// The trailing `true` is soundOn. Leaving the initializer short would
// value-initialize it to false — sound off by default, from a line that does not
// mention sound.
// [TH] ค่า true ตัวท้ายคือ soundOn ถ้าปล่อยให้สั้นกว่าจำนวนฟิลด์ ค่านี้จะถูกตั้งเป็นเท็จโดยปริยาย
//      กลายเป็นปิดเสียงตั้งแต่ต้น จากบรรทัดที่ไม่ได้พูดถึงเสียงเลย
static DeviceSettings gLast = {"", "", "", "default", 8, 4, 4, 10, 10, 10, true};
```

- [ ] **Step 5: Apply the loaded value at boot**

In `setup()`, after the settings file is loaded and before `audioPlay(Cue::Boot)`:

```cpp
  audioSetEnabled(gLast.soundOn);
```

- [ ] **Step 6: Verify `onLinkSettingsReply` was not touched**

Read `onLinkSettingsReply()` and confirm it still assigns `gLast` **field by field** and contains no whole-struct assignment. This is load-bearing: `soundOn` is absent from `LinkSettings`, so field-by-field assignment is exactly why the user's setting survives a controller push. Add this comment above the field assignments:

```cpp
  // Assigned field by field, never as a whole struct. soundOn is deliberately
  // absent from LinkSettings, so it survives this only because nothing here
  // overwrites it — rewriting this as `gLast = built` would silently reset the
  // user's sound setting on every controller reconnect.
  // [TH] กำหนดค่าทีละฟิลด์ ไม่ใช่ทั้งโครงสร้าง ค่า soundOn จงใจไม่มีใน LinkSettings
  //      จึงรอดมาได้เพราะไม่มีอะไรตรงนี้เขียนทับ
  //      ถ้าเขียนใหม่เป็น gLast = built ค่าเสียงที่ผู้ใช้ตั้งไว้จะถูกล้างทุกครั้งที่คอนโทรลเลอร์เชื่อมต่อใหม่
```

- [ ] **Step 7: Compile both boards**

Both, because the shared header changed:

```bash
powershell -File firmware/tools/flash.ps1 -Action compile -Board both
```

Expected: both build clean, and `-Check` reports the shared files in sync.

- [ ] **Step 8: Commit**

```bash
git add firmware/pt-leg-display/settings.h firmware/pt-leg-controller/settings.h firmware/pt-leg-display/settingsfile.cpp firmware/pt-leg-display/pt-leg-display.ino
git commit -m "feat(firmware): soundOn setting persisted to the card"
```

---

## Task 6: The settings-screen switch

**Files:**
- Modify: `firmware/pt-leg-display/ui.cpp`

**Interfaces:**
- Consumes: `DeviceSettings::soundOn` (Task 5).
- Produces: nothing new in the header — the switch rides the existing `uiOpenSettings(const DeviceSettings&)` / `uiSetSettingsSaved(...)` pair, so `ui.h` gains no function.

- [ ] **Step 1: Add the switch handle**

In `firmware/pt-leg-display/ui.cpp`, add `swSound` to the settings-screen handle list (the line declaring `scrSettings`, `taSsid`, …):

```cpp
static lv_obj_t *scrSettings, *taSsid, *taPass, *taServer, *taCode, *kb, *ddScan, *colSettings,
    *swSound;
```

- [ ] **Step 2: Add a row builder next to `makeSpinRow`**

```cpp
// Same geometry as makeSpinRow so the rows line up, with a switch on the right
// instead of a spinbox and its two buttons.
// [TH] ขนาดและระยะเท่ากับ makeSpinRow เพื่อให้แถวเรียงตรงกัน
//      ต่างกันตรงที่ด้านขวาเป็นสวิตช์ ไม่ใช่ช่องตัวเลขกับปุ่มสองปุ่ม
static lv_obj_t* makeSwitchRow(lv_obj_t* parent, const char* label, bool on) {
  lv_obj_t* row = lv_obj_create(parent);
  lv_obj_set_size(row, lv_pct(100), 42);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_set_style_pad_all(row, 0, 0);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(row, LV_OBJ_FLAG_EVENT_BUBBLE);

  lv_obj_t* l = lv_label_create(row);
  lv_obj_set_style_text_font(l, &thai14, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(COL_MUTED), 0);
  lv_label_set_text(l, label);
  lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);

  lv_obj_t* sw = lv_switch_create(row);
  lv_obj_set_size(sw, 50, 28);
  lv_obj_align(sw, LV_ALIGN_RIGHT_MID, 0, 0);
  if (on) lv_obj_add_state(sw, LV_STATE_CHECKED);
  return sw;
}
```

- [ ] **Step 3: Build the row once, inside `buildSettings()`**

In `buildSettings()`, after the last `makeSpinRow(...)` call:

```cpp
  // "เสียง" and not "ปิดเสียง": positive sense matches the stored field, and the
  // word has no vowel stacked over a tone mark, which this LVGL build cannot
  // shape.
  // [TH] ใช้คำว่า "เสียง" ไม่ใช่ "ปิดเสียง" เพราะความหมายเชิงบวกตรงกับค่าที่เก็บไว้
  //      และคำนี้ไม่มีสระซ้อนกับวรรณยุกต์ ซึ่ง LVGL รุ่นนี้จัดวางให้ไม่ได้
  swSound = makeSwitchRow(col, "เสียง", true);
```

**This must go in `buildSettings()`, never in `uiOpenSettings()`.** The screen is built exactly once — see the comment above `buildSettings()`: rebuilding it per open leaked about 8.9 KB of LVGL's 48 KB pool each time, and the fifth open hit `LV_ASSERT_MALLOC`, whose handler is `while(1)`, a silent freeze with a dead touchscreen and no reboot.

- [ ] **Step 4: Set the value on open**

In `uiOpenSettings()`, next to the other `lv_*_set_value` calls:

```cpp
  if (cur.soundOn) lv_obj_add_state(swSound, LV_STATE_CHECKED);
  else lv_obj_clear_state(swSound, LV_STATE_CHECKED);
```

- [ ] **Step 5: Read it back on save**

In `saveClicked()`, after `s.targetReps = ...`:

```cpp
  s.soundOn = lv_obj_has_state(swSound, LV_STATE_CHECKED);
```

**This assignment is mandatory, not optional.** `saveClicked` declares a fresh `DeviceSettings s;`. `DeviceSettings` is not a POD — its `String` members are constructed — but its scalars are left **indeterminate**. Every existing scalar is assigned a few lines above, which is the only reason this works today. Leaving `soundOn` out would write an uninitialised bool into `gLast`, into `/settings.json`, and into `audioSetEnabled`.

- [ ] **Step 6: Apply it when saved**

In `firmware/pt-leg-display/pt-leg-display.ino`, inside `onSaveFromUi()`, after `gLast = s;`:

```cpp
  audioSetEnabled(s.soundOn);
```

- [ ] **Step 7: Compile**

```bash
powershell -File firmware/tools/flash.ps1 -Action compile -Board display
```

- [ ] **Step 8: Flash and bench-check**

| Check | Pass |
|---|---|
| Row renders | "เสียง" and a switch, aligned with the spinbox rows above it. Thai glyphs are correct, not boxes. |
| Off silences | Switch off, SAVE, power cycle. No boot cue. |
| Persisted | Read `/settings.json` on a PC. `"soundOn": false`. |
| On restores | Switch on, SAVE, power cycle. Boot cue plays. |
| Survives a controller push | With sound off, reboot the controller so it re-sends settings. Sound stays off. This is the field-by-field assignment from Task 5 Step 6 doing its job. |
| Five opens | Open and close the settings screen five times. No freeze. This is the `LV_ASSERT_MALLOC` trap; a freeze here means the row was built in the wrong function. |

- [ ] **Step 9: Commit**

```bash
git add firmware/pt-leg-display/ui.cpp firmware/pt-leg-display/pt-leg-display.ino
git commit -m "feat(firmware): sound switch on the settings screen"
```

---

## Task 7: The three session cues

**Files:**
- Modify: `firmware/pt-leg-display/pt-leg-display.ino` (`onLinkState`, `onLinkDone`)

**Interfaces:**
- Consumes: `audioPlay(Cue)` from Task 4.
- Produces: nothing consumed by later tasks.

- [ ] **Step 1: Add the start-edge state**

In `firmware/pt-leg-display/pt-leg-display.ino`, at file scope near the other `ui*` state:

```cpp
// Previous value of the running flag, for the start edge. The sentinel matters:
// the first state frame after boot seeds it without emitting, because a display
// restarted beside a running machine must not announce a start that already
// happened.
// [TH] ค่าก่อนหน้าของสถานะกำลังทำงาน ใช้จับขอบขาขึ้นตอนเริ่ม
//      ค่าพิเศษนี้สำคัญ ข้อความสถานะแรกหลังบูตจะใช้ตั้งค่าเริ่มต้นโดยไม่ส่งเสียง
//      เพราะจอที่รีสตาร์ตข้างเครื่องที่กำลังทำงานอยู่ ต้องไม่ประกาศการเริ่มที่เกิดไปแล้ว
static int8_t gPrevRunning = -1;  // -1 = ยังไม่เคยได้รับข้อความสถานะ
```

- [ ] **Step 2: Raise the start cue**

In `onLinkState()`, at the very end of the function, after `uiServer = s.server;`:

```cpp
  // Start has no one-shot message, so it must edge-detect, and both ways that
  // can go wrong are real.
  //
  // gLinkUp: a transition seen while the link is considered down is a reporting
  // artifact, not a session. Without this, a glitching jumper wire makes the
  // machine announce a start every time the watchdog clears and refills the
  // state.
  //
  // gPrevRunning < 0: the first frame after boot only seeds the value.
  // [TH] สถานะเริ่มไม่มีข้อความส่งครั้งเดียวรองรับ จึงต้องจับขอบเอง และพลาดได้จริงสองทาง
  //      gLinkUp: การเปลี่ยนสถานะที่เห็นตอนลิงก์ถือว่าขาด เป็นผลจากการรายงาน ไม่ใช่รอบการทำงานจริง
  //      ถ้าไม่มีเงื่อนไขนี้ สายจัมเปอร์ที่สัญญาณกระตุกจะทำให้เครื่องประกาศเริ่มทุกครั้งที่ตัวเฝ้าระวังล้างและเติมสถานะใหม่
  //      gPrevRunning < 0: ข้อความแรกหลังบูตใช้ตั้งค่าเริ่มต้นเท่านั้น
  if (gPrevRunning < 0) {
    gPrevRunning = s.running ? 1 : 0;
  } else {
    if (gLinkUp && s.running && gPrevRunning == 0) audioPlay(Cue::Start);
    gPrevRunning = s.running ? 1 : 0;
  }
```

- [ ] **Step 3: Raise the stop and target cues**

In `onLinkDone()`, at the end of the existing handler:

```cpp
  // Stop and target come from `done`, not from the running flag falling.
  //
  // The display cannot tell "the session ended" from "the link dropped and the
  // watchdog cleared the state", and a machine that says หยุดการทำงาน every time
  // a wire glitches is worse than one that says nothing. `done` is emitted
  // exactly once per session, is on the retransmit list, and carries the final
  // rep count — so the split below is decided from the numbers the controller
  // actually finished with, not from the last snapshot that happened to arrive.
  // [TH] เสียงหยุดและเสียงครบเป้าหมายมาจากข้อความ done ไม่ใช่จากสถานะกำลังทำงานที่ตกลง
  //      จอแยกไม่ออกระหว่าง "รอบจบแล้ว" กับ "ลิงก์ขาดจนตัวเฝ้าระวังล้างสถานะ"
  //      เครื่องที่พูดว่าหยุดการทำงานทุกครั้งที่สายกระตุก แย่กว่าเครื่องที่เงียบ
  //      ข้อความ done ถูกส่งครั้งเดียวต่อรอบ อยู่ในรายการส่งซ้ำ และพามาทั้งจำนวนครั้งสุดท้าย
  //      การแยกด้านล่างจึงตัดสินจากตัวเลขที่คอนโทรลเลอร์จบรอบมาจริง ไม่ใช่ภาพสถานะล่าสุดที่บังเอิญมาถึง
  //
  // uiTarget is the live target from the state stream. A target of 0 means "run
  // until STOP", so every end of such a session is a stop, never a target.
  // [TH] uiTarget คือค่าเป้าหมายสดจากกระแสข้อความสถานะ ค่า 0 แปลว่าทำไปจนกว่าจะกดหยุด
  //      รอบแบบนั้นจึงจบด้วยเสียงหยุดเสมอ ไม่มีทางเป็นเสียงครบเป้าหมาย
  //
  // Adding a fault cue later is one branch here: d.fault is already in hand.
  // [TH] ถ้าจะเพิ่มเสียงแจ้งความผิดพลาดภายหลัง ก็เพิ่มเงื่อนไขเดียวตรงนี้ เพราะ d.fault มีอยู่แล้ว
  if (uiTarget != 0 && d.reps >= uiTarget) audioPlay(Cue::Target);
  else audioPlay(Cue::Stop);

  // The session is over; the next start is a real edge again.
  // [TH] รอบจบแล้ว การเริ่มครั้งถัดไปจึงนับเป็นขอบขาขึ้นจริงอีกครั้ง
  gPrevRunning = 0;
```

`onLinkDone` is declared `static void onLinkDone(const LinkDone& d)`, so `d` is correct as written. `uiTarget` and `gLinkUp` are both file-scope statics and are in scope in both handlers.

**Do not "fix" the staleness of `gLinkUp`.** It is recomputed in `loop()` *after* `gLink.poll()` has already delivered this frame's callbacks, so inside `onLinkState` it still holds the previous iteration's value. That is the behaviour the guard wants: on the first frame after a link recovery `gLinkUp` is still `false`, so the spurious start is suppressed. Recomputing it before the poll would defeat the guard entirely.

- [ ] **Step 4: Compile**

```bash
powershell -File firmware/tools/flash.ps1 -Action compile -Board display
```

- [ ] **Step 5: Flash and bench-check — the full matrix**

Both boards powered, jumpers reconnected, card holding all four sounds.

| # | Check | Pass |
|---|---|---|
| 1 | Start cue | Press panel START. "เริ่มการทำงาน" plays once. |
| 2 | Stop cue | Press STOP before the target. "หยุดการทำงาน" plays once; the target clip does not. |
| 3 | Target cue | Set the target to 2 and let it finish. "ครบตามเป้าหมายแล้ว" plays; the stop clip does not. |
| 4 | Target 0 | Set target 0, run, press STOP. Stop clip, not target. |
| 5 | Link drop | Pull a UART jumper mid-session and reseat it. **No start cue.** |
| 6 | Display reboot | Reset the display while the controller runs. **No start cue.** |
| 7 | Boot beside a running machine | Same as 6, then let the session end normally. Stop or target still fires correctly. |
| 8 | Cue against the SD write | The stop cue and the `sessions.csv` append fire together. Audio must not stutter **and** the row must still be written — check the file on a PC. |
| 9 | Heap | `ESP.getFreeHeap()` after 20 cues equals the value after the first. |
| 10 | Back-to-back cues | Trigger start then stop quickly. The second interrupts the first cleanly, no noise, no freeze. |

Check 8 is the one this whole architecture exists for. If it stutters, the preload is not doing its job and the finding belongs in the design doc before anything is tuned.

- [ ] **Step 6: Commit**

```bash
git add firmware/pt-leg-display/pt-leg-display.ino
git commit -m "feat(firmware): start, stop and target audio cues"
```

---

## Task 8: Documentation

**Files:**
- Modify: `firmware/pin-layout.html`
- Modify: `firmware/README.md`

**Interfaces:** none.

- [ ] **Step 1: Update the pin layout**

In `firmware/pin-layout.html`:

The display board's "What you wire" table currently lists UART RX (22), UART TX (27) and GND, and the prose above says the CYD "has exactly three free wires". Both must change — GPIO 26 is now wired.

Add a row to that table, bilingual, matching the existing `<span class="en">` / `<span class="th">` structure:

- Signal: `Speaker` / `ลำโพง`, Pin: `GPIO 26`, Goes to: `Onboard amp → speaker JST` / `วงจรขยายเสียงบนบอร์ด → ขั้วต่อลำโพง`

Change "exactly three free wires: two for the UART link and a ground" to four, naming the speaker. Both language spans.

Add a note recording which board revision was found in Task 3 Step 1, and — if no amp was populated — that an external module sits between the pin and the speaker.

- [ ] **Step 2: Update the README**

In `firmware/README.md`, add a section covering:

- The `/sounds` card layout and the four filenames.
- The accepted format: RIFF PCM, 8-bit unsigned, mono, 22050 Hz, under 48 KB of samples.
- `tools/make-sounds.ps1` usage, and that recording loudness is decided there because there is no gain stage afterwards.
- `tests/run-tests.ps1`: what it covers (the WAV parser) and, explicitly, what it cannot (anything touching the DAC, the card or LVGL). Note that `UI_TEST_HOOK` cannot hear anything either — every "sound plays" row needs ears on the bench.
- The bench matrix from Tasks 3–7, as a checklist in the style of the existing button-bench section.

- [ ] **Step 3: Verify the HTML still renders**

Open `firmware/pin-layout.html` in a browser. Both language columns present, table not broken, new row styled like its neighbours.

- [ ] **Step 4: Commit**

```bash
git add firmware/pin-layout.html firmware/README.md
git commit -m "docs(firmware): speaker pin and the audio bench checklist"
```

---

## Self-Review

**Spec coverage:**

| Spec section | Task |
|---|---|
| Module boundary and API | 3 |
| DAC configuration, CH1 mask, 22050 Hz | 3 |
| 0x80 padding and pop-free disable | 2 (tool pads), 4 (`dacOff`) |
| Chunk-walking parser, named rejections | 1 |
| Card layout and format | 1, 2 |
| `make-sounds.ps1` | 2 |
| Buffer budget and degrade ladder | 3 |
| Boot / start / stop / target triggers | 4 (boot), 7 (rest) |
| Start-edge guards | 7 |
| `soundOn` shared header, not in `LinkSettings` | 5 |
| Field-by-field `onLinkSettingsReply` protected | 5 Step 6 |
| Settings switch | 6 |
| Failure handling table | 3, 4 |
| Amp check (bench step 0) | 3 Step 1 |
| Bench matrix | 3, 4, 6, 7 |
| Docs | 8 |

No spec requirement is unclaimed.

**Type consistency:** `WavError`, `WavInfo`, `wavParse`, `wavErrorText`, `WAV_RATE`, `WAV_SCAN_BYTES` are defined in Task 1 and used with the same names and signatures in Tasks 2 and 4. `Cue`, `audioBegin`, `audioLoop`, `audioPlay`, `audioSetEnabled`, `audioReady` are defined in Task 3 and used unchanged in Tasks 4–7. `DeviceSettings::soundOn` is defined in Task 5 and used in Tasks 5 and 6. `AUDIO_BUF_BYTES` (49152) and `make-sounds.ps1 -MaxBytes` (49152) agree, and Task 3's comment says so.

**Two known-fragile spots, both flagged in-plan rather than left to be discovered:**

- Task 4 Step 2 exists because Step 1 writes a function-static drain timer that `stopAll()` cannot reach. It is split into its own step so the fix is not skimmed past.
- Task 6 Step 5 is mandatory: `saveClicked` builds a fresh `DeviceSettings` whose scalars are indeterminate, so an unassigned `soundOn` is garbage, not `false`.

**Honest limits of the test story:** only Task 1 and Task 2 Step 3 are automatically verifiable. Tasks 3–7 are gated on a compile plus a named bench check with an explicit pass criterion, because there is no host harness for DAC, SD or LVGL code and inventing stubs for them would test the stubs. `UI_TEST_HOOK` can drive the UI and read back what the firmware believes, but it cannot hear anything.
