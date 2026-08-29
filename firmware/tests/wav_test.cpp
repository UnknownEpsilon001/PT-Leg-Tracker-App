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

  // A file produced by tools/make-sounds.ps1, when one is present. This is the
  // loop that matters: the tool's output is fed through the same parser the
  // firmware uses, so a tool change that breaks the format fails here rather
  // than on the bench.
  {
    FILE* f = fopen("scratch/sounds/start.wav", "rb");
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
      printf("SKIP: scratch/sounds/start.wav absent; run tools/make-sounds.ps1 to cover this\n");
    }
  }

  printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
