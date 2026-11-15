#include "wavebank.hpp"

#include "compat.hpp"
#include <math.h>
#include <string.h>

#include "doc.hpp"
#include "program.hpp"

namespace sq80 {

WaveBank waveBank;

namespace {
constexpr float kPi = 3.14159265f;
inline float noteHz(float n) { return 440.0f * powf(2.0f, (n - 69.0f) / 12.0f); }
uint32_t rng = 0x2545F491;
inline float frand() {
  rng = rng * 1664525u + 1013904223u;
  return (float)(int32_t)rng / 2147483648.0f;
}
// Quantize to DOC bytes: offset binary, never 0x00 (that halts the oscillator); one-shots end
// with a zero terminator.
void quantize(const float* src, uint8_t* dst, uint32_t n, bool terminate) {
  float peak = 1e-6f;
  for (uint32_t i = 0; i < n; ++i) peak = fmaxf(peak, fabsf(src[i]));
  float g = 126.0f / peak;
  for (uint32_t i = 0; i < n; ++i) {
    int v = (int)lroundf(src[i] * g) + 128;
    dst[i] = (uint8_t)(v < 1 ? 1 : (v > 255 ? 255 : v));
  }
  if (terminate) dst[n - 1] = 0;
}
const uint8_t kRangeLow[6] = {0, 48, 60, 72, 84, 96};
const uint8_t kRangeHigh[6] = {47, 59, 71, 83, 95, 127};
}  // namespace

uint32_t WaveBank::alloc(uint32_t bytes) {
  uint32_t a = (top_ + bytes - 1) & ~(bytes - 1);
  if (a + bytes > kBankSize) return 0xFFFFFFFF;
  top_ = a + bytes;
  return a;
}

// A record for a table at `addr` whose pitch, played with the DOC pitch law
// (F = 56 * 2^(key/12) before the offsets), needs `semis` semitones of correction.
WaveSample WaveBank::recordFor(uint32_t addr, uint8_t sizeCode, uint8_t res, float semis) const {
  WaveSample r;
  r.page = (addr >> 8) & 0xFF;
  uint32_t bank = addr >> 16;
  r.reg = (uint8_t)(((bank & 1) << 7) | (((bank >> 1) & 1) << 6) | ((sizeCode & 7) << 3) | (res & 7));
  float fl = floorf(semis);
  r.semiOfs = (int8_t)fl;
  r.fineOfs = (uint8_t)((semis - fl) * 256.0f);
  return r;
}

const uint8_t* WaveBank::tableBytes(uint32_t addr, uint32_t size) const {
  for (int i = 0; i < cacheCount_; ++i)
    if (cache_[i].addr == addr && cache_[i].size == size) return cacheMem_ + cache_[i].offset;
  return mem_ + addr;
}

void WaveBank::cacheProgram(const uint8_t* waves, int count) {
  if (!cacheMem_) return;
  cacheCount_ = 0;
  cacheUsed_ = 0;
  for (int wi = 0; wi < count; ++wi) {
    int wave = waves[wi];
    if (wave < 0 || wave >= 75) continue;
    for (int k = 0; k < 16; ++k) {
      const WaveSample& ws = tab_[wave][k];
      uint32_t size = 256u << ((ws.reg >> 3) & 7);
      uint32_t bank = ((ws.reg & 0x40) >> 5) | (ws.reg >> 7);
      uint32_t addr = ((bank << 16) + ((uint32_t)ws.page << 8)) & ~(size - 1);
      bool have = false;
      for (int i = 0; i < cacheCount_; ++i) if (cache_[i].addr == addr && cache_[i].size == size) { have = true; break; }
      if (have) continue;
      if (cacheCount_ >= kCacheMax || cacheUsed_ + size > kCacheSize) continue;
      memcpy(cacheMem_ + cacheUsed_, mem_ + addr, size);
      cache_[cacheCount_++] = {addr, size, cacheUsed_};
      cacheUsed_ += size;
    }
  }
}

bool WaveBank::init() {
  cacheMem_ = nullptr;  // MPC: no PSRAM contention; tableBytes() reads mem_ directly
  if (!cacheMem_) Serial.println("[wave] no internal RAM for the table cache");
  mem_ = (uint8_t*)ps_malloc(kBankSize);
  if (!mem_) {
    Serial.println("[wave] PSRAM allocation failed");
    return false;
  }
  memset(mem_, 0x80, kBankSize);
  memset(tab_, 0, sizeof tab_);
  if (load()) {
    fromStorage_ = true;
  } else {
    uint32_t t0 = millis();
    synthesize();
    Serial.printf("[wave] synthesized bank: %lu bytes, %lu ms\n", (unsigned long)top_, (unsigned long)(millis() - t0));
  }
  return true;
}

// /sq80/wave0..3.bin at 0x00000/0x10000/0x20000/0x30000; /sq80/waves.txt: "W <wave>" then
// 16 lines "K <key/8> <page> <reg> <semiOfs> <fineOfs>".
bool WaveBank::load() {
  int roms = 0;
  for (int i = 0; i < 4; ++i) {
    char path[24];
    snprintf(path, sizeof path, "/sq80/wave%d.bin", i);
    FILE* file = sq80compat::openFile(path, "rb");
    if (!file) continue;
    size_t got = fread(mem_ + i * 0x10000, 1, 0x10000, file);
    fclose(file);
    if (got) ++roms;
  }
  if (!roms) return false;
  FILE* t = sq80compat::openFile("/sq80/waves.txt", "rb");
  if (!t) return false;
  int cur = -1, lines = 0;
  char line[160];
  while (fgets(line, sizeof line, t)) {
    char* s = line;
    while (*s == ' ' || *s == '\t') ++s;
    if (*s == '\0' || *s == '\n' || *s == '\r' || *s == '#') continue;
    if (*s == 'W') {
      cur = atoi(s + 1);
    } else if (*s == 'K' && cur >= 0 && cur < 75) {
      int k, page, reg, semi, fine;
      if (sscanf(s + 1, "%d %d %d %d %d", &k, &page, &reg, &semi, &fine) == 5 && k >= 0 && k < 16) {
        tab_[cur][k] = {(uint8_t)page, (uint8_t)reg, (int8_t)semi, (uint8_t)fine};
        ++lines;
      }
    }
  }
  fclose(t);
  return lines >= 75 * 16;
}

// ---- synthesized bank (no ROM data available) -------------------------------------------
namespace {
float harmonicAmp(int wave, int n, float f0) {
  auto formant = [&](float centre, float width) { float d = (n * f0 - centre) / width; return expf(-d * d) + 0.04f / n; };
  auto glint = [&](float centre) { float d = (n * f0 - centre) / (centre * 0.12f); return expf(-d * d) + (n == 1 ? 0.08f : 0.0f); };
  auto inSet = [&](const int* set, int cnt) { for (int i = 0; i < cnt; ++i) if (set[i] == n) return 1.0f; return 0.0f; };
  static const int synth1[] = {1, 2, 5, 8, 11, 14, 17, 20, 23, 26};
  static const int synth2[] = {1, 4, 7, 10, 13, 16, 19, 22, 25};
  static const int synth3[] = {1, 2, 3, 5, 7, 11, 13, 17, 19, 23};
  static const int fourOcts[] = {1, 2, 4, 8};
  static const int prime[] = {1, 3, 5, 7, 11};
  static const int organ[] = {1, 2, 4, 8, 16, 32};
  static const int bell[] = {1, 3, 7, 12, 19, 27};
  static const int bell2[] = {1, 4, 7, 11, 17, 24};
  switch (wave) {
    case 0: return 1.0f / n;
    case 1: return inSet(bell, 6) * powf(0.75f, n / 4.0f);
    case 2: return n == 1 ? 1.0f : 0.0f;
    case 3: return (n & 1) ? 1.0f / n : 0.0f;
    case 4: return n <= 32 ? 1.0f : 0.0f;
    case 8: return (1.0f / n) * (n == 3 || n == 4 ? 1.6f : 1.0f);
    case 9: return 1.0f / powf((float)n, 1.3f) * (n > 12 ? 0.3f : 1.0f);
    case 10: return n == 1 ? 1.0f : (n == 2 ? 0.25f : (n == 3 ? 0.08f : (n == 7 ? 0.12f : 0.0f)));
    case 11: return formant(750, 200) + 0.6f * formant(1150, 200);
    case 12: return formant(650, 180) + 0.6f * formant(1100, 220) + 0.2f * formant(2600, 300);
    case 13: return n <= 2 ? 1.0f / n : 0.0f;
    case 14: return (1.0f / powf((float)n, 0.7f)) * (n == 3 || n == 5 ? 1.5f : 1.0f) * (n > 20 ? 0.3f : 1.0f);
    case 15: return inSet(organ, 6);
    case 16: return inSet(synth1, 10);
    case 17: return inSet(synth2, 9);
    case 18: return inSet(synth3, 10);
    case 19: return formant(750, 150);
    case 20: return formant(1000, 180);
    case 21: return formant(1400, 220);
    case 22: return formant(1750, 260);
    case 23: return formant(2250, 300);
    case 24: return n <= 11 ? 1.0f : 0.0f;
    case 25: return (n & 1) && n <= 7 ? 1.0f / n : 0.0f;
    case 26: return inSet(fourOcts, 4);
    case 27: return inSet(prime, 5);
    case 28: return n <= 18 ? harmonicAmp(8, n, f0) : 0.0f;
    case 29: return n <= 9 ? harmonicAmp(10, n, f0) : 0.0f;
    case 30: return n <= 2 ? 1.0f : 0.0f;
    case 31: return n <= 3 ? 1.0f : 0.0f;
    case 32: return n <= 3 ? 1.0f / n : 0.0f;
    case 33: return (n & 1) ? 1.0f / (float)(n * n) : 0.0f;
    case 34: return 1.0f / powf((float)n, 0.5f) * (n == 2 ? 0.3f : 1.0f) * (n > 14 ? 0.2f : 1.0f);
    case 35: return 1.0f / n * (n >= 4 && n <= 7 ? 3.0f : 1.0f);
    case 36: return 1.0f / powf((float)n, 0.35f) * (n > 16 ? 0.0f : 1.0f);
    case 37: return 1.0f / powf((float)n, 0.3f) * (n > 28 ? 0.0f : 1.0f);
    case 38: return 1.0f / powf((float)n, 0.25f);
    case 39: return glint(6000);
    case 40: return glint(4000);
    case 41: return glint(2700);
    case 42: return (n % 4 == 0) ? 0.0f : 1.0f / powf((float)n, 0.8f);
    case 43: return 1.0f / powf((float)n, 0.8f) * (n > 24 ? 0.4f : 1.0f);
    case 44: return 1.0f / n * (n == 2 || n == 3 ? 1.3f : 1.0f) * (n > 30 ? 0.5f : 1.0f);
    case 47: return inSet(bell2, 6) * powf(0.8f, n / 4.0f);
    case 48: return formant(500, 120) + formant(1700, 150) + 0.5f * formant(3200, 200);
    default: return 1.0f / n;
  }
}
}  // namespace

void WaveBank::synthesize() {
  float* buf = (float*)ps_malloc(8192 * sizeof(float));
  if (!buf) return;
  top_ = 0;
  static float sineTab[256];
  for (int i = 0; i < 256; ++i) sineTab[i] = sinf(2.0f * kPi * i / 256.0f);

  // One 256-byte single cycle plays in tune with resolution 1; a loop of 2^k cycles with 1 + k.
  auto fillBuckets = [&](int wave, const WaveSample* perRange) {
    for (int b = 0; b < 16; ++b) {
      int key = b * 8 + 4;
      int r = 0;
      while (r < 5 && key > kRangeHigh[r]) ++r;
      tab_[wave][b] = perRange[r];
    }
  };
  auto single = [&](int wave, WaveSample* out) {
    for (int b = 0; b < 16; ++b) tab_[wave][b] = out[0];
  };
  (void)single;

  auto singleCycle = [&](int wave, auto ampFn) {
    WaveSample perRange[6];
    for (int r = 0; r < 6; ++r) {
      float fTop = noteHz(kRangeHigh[r]);
      float fMid = noteHz((kRangeLow[r] + kRangeHigh[r]) / 2);
      int hmax = (int)(0.45f * kSampleRate / fTop);
      if (hmax < 1) hmax = 1;
      if (hmax > 128) hmax = 128;
      for (int i = 0; i < 256; ++i) buf[i] = 0;
      for (int n = 1; n <= hmax; ++n) {
        float a = ampFn(n, fMid);
        if (a == 0.0f) continue;
        int ph = (n == 1) ? 0 : (int)(frand() * 128.0f) & 255;
        for (int i = 0; i < 256; ++i) buf[i] += a * sineTab[(n * i + ph) & 255];
      }
      uint32_t a = alloc(256);
      quantize(buf, mem_ + a, 256, false);
      perRange[r] = recordFor(a, 0, 1, 0.0f);
    }
    fillBuckets(wave, perRange);
  };
  for (int w = 0; w < 49; ++w) {
    if (w == 5 || w == 6 || w == 7 || w == 45 || w == 46) continue;
    singleCycle(w, [w](int n, float f0) { return harmonicAmp(w, n, f0); });
  }

  auto noiseCycle = [&](int wave, float lowpass, bool sparse) {  // 1024 bytes = 4 cycles
    float y = 0;
    for (int i = 0; i < 1024; ++i) {
      float x = frand();
      if (sparse && (i % 7)) x *= 0.2f;
      y += lowpass * (x - y);
      buf[i] = y;
    }
    uint32_t a = alloc(1024);
    quantize(buf, mem_ + a, 1024, false);
    WaveSample rec = recordFor(a, 2, 3, 0.0f);
    for (int b = 0; b < 16; ++b) tab_[wave][b] = rec;
  };
  noiseCycle(5, 0.08f, false);
  noiseCycle(6, 1.0f, false);
  noiseCycle(7, 0.6f, true);

  auto fmCycle = [&](int wave, float ratio, float index, float feedback) {
    WaveSample perRange[6];
    for (int r = 0; r < 6; ++r) {
      float idx = index * (r < 2 ? 1.0f : (r < 4 ? 0.6f : 0.25f));
      float fb = 0;
      for (int i = 0; i < 256; ++i) {
        float t = 2.0f * kPi * i / 256.0f;
        float m = sinf(ratio * t + feedback * fb);
        fb = m;
        buf[i] = sinf(t + idx * m);
      }
      uint32_t a = alloc(256);
      quantize(buf, mem_ + a, 256, false);
      perRange[r] = recordFor(a, 0, 1, 0.0f);
    }
    fillBuckets(wave, perRange);
  };
  fmCycle(45, 1.0f, 2.5f, 1.2f);
  fmCycle(46, 14.0f, 0.9f, 0.0f);

  auto loop = [&](int wave, auto fill) {  // 4096 bytes, 16 cycles -> resolution 5
    for (int i = 0; i < 4096; ++i) buf[i] = 0;
    fill(buf, 4096, 16.0f);
    uint32_t a = alloc(4096);
    quantize(buf, mem_ + a, 4096, false);
    WaveSample rec = recordFor(a, 4, 5, 0.0f);
    for (int b = 0; b < 16; ++b) tab_[wave][b] = rec;
  };
  auto partial = [](float* b, int n, float cyc, float amp, float ph) {
    for (int i = 0; i < n; ++i) b[i] += amp * sinf(2.0f * kPi * cyc * i / n + ph);
  };
  auto noiseInto = [](float* b, int n, float amp, float lp) {
    float y = 0;
    for (int i = 0; i < n; ++i) { y += lp * (frand() - y); b[i] += amp * y; }
  };
  loop(49, [&](float* b, int n, float c) { partial(b, n, c, 0.5f, 0); noiseInto(b, n, 0.9f, 0.15f); });
  loop(50, [&](float* b, int n, float c) { partial(b, n, c, 1, 0); partial(b, n, 2 * c, 0.5f, 1); partial(b, n, 3 * c, 0.35f, 2); partial(b, n, 4.03f * c, 0.2f, 0.5f); noiseInto(b, n, 0.15f, 0.05f); });
  loop(51, [&](float* b, int n, float c) { partial(b, n, c, 0.15f, 0); noiseInto(b, n, 1.0f, 0.5f); });
  loop(52, [&](float* b, int n, float c) { partial(b, n, c, 1, 0); partial(b, n, 2.37f * c, 0.7f, 1); partial(b, n, 3.81f * c, 0.6f, 2); partial(b, n, 5.33f * c, 0.5f, 3); partial(b, n, 7.1f * c, 0.4f, 1); partial(b, n, 9.7f * c, 0.3f, 2); });
  loop(53, [&](float* b, int n, float c) { partial(b, n, c, 1, 0); partial(b, n, 2.76f * c, 0.6f, 1); partial(b, n, 5.4f * c, 0.4f, 2); partial(b, n, 8.93f * c, 0.25f, 3); partial(b, n, 13.3f * c, 0.15f, 1); });

  // One-shots: with resolution = size code the DOC steps one byte per sample at F = 512,
  // which the pitch law reaches at 38.3 semitones; the record's offset moves that to rootKey.
  struct Shot { uint32_t addr; uint8_t sc; };
  Shot shots[75] = {};
  auto oneShot = [&](int wave, uint32_t n, int rootKey, auto fill) {
    for (uint32_t i = 0; i < n; ++i) buf[i] = 0;
    fill(buf, (int)n);
    uint32_t a = alloc(n);
    quantize(buf, mem_ + a, n, true);
    uint8_t sc = 0;
    while ((256u << sc) < n) ++sc;
    shots[wave] = {a, sc};
    WaveSample rec = recordFor(a, sc, sc, 38.3f - rootKey);
    for (int b = 0; b < 16; ++b) tab_[wave][b] = rec;
  };
  auto decay = [](int i, int n, float k) { return expf(-k * i / (float)n); };
  auto ks = [&](float* b, int n, float periodSamples, float damp) {
    int p = (int)periodSamples;
    if (p < 2) p = 2;
    float* ring = buf + 7000;
    for (int i = 0; i < p; ++i) ring[i] = frand();
    int idx = 0;
    for (int i = 0; i < n; ++i) {
      float v = ring[idx];
      float nx = ring[(idx + 1) % p];
      ring[idx] = damp * 0.5f * (v + nx);
      idx = (idx + 1) % p;
      b[i] = v;
    }
  };
  float fs = kSampleRate;
  oneShot(54, 4096, 60, [&](float* b, int n) { float y = 0; for (int i = 0; i < n; ++i) { y += 0.3f * (frand() - y); float saw = fmodf(i * 261.6f / fs, 1.0f) * 2 - 1; b[i] = (0.7f * y + 0.5f * saw * y) * decay(i, n, 5) * (i < 400 ? i / 400.0f : 1.0f); } });
  oneShot(55, 4096, 60, [&](float* b, int n) { ks(b, n, fs / 261.6f, 0.985f); for (int i = 0; i < n; ++i) b[i] *= decay(i, n, 4); });
  oneShot(56, 4096, 60, [&](float* b, int n) { ks(b, n, fs / 261.6f, 0.995f); float y = 0; for (int i = 0; i < n; ++i) { y += 0.6f * (frand() - y); b[i] = b[i] * decay(i, n, 3) + 0.8f * y * decay(i, n, 40); } });
  oneShot(57, 4096, 60, [&](float* b, int n) { for (int i = 0; i < n; ++i) { float t = i / fs; b[i] = (sinf(2 * kPi * 261.6f * t) + 0.5f * sinf(2 * kPi * 261.6f * 4.0f * t) * decay(i, n, 12)) * decay(i, n, 6); } });
  oneShot(58, 4096, 60, [&](float* b, int n) { float y = 0; for (int i = 0; i < n; ++i) { y += 0.5f * (frand() - y); float t = i / fs; b[i] = y * decay(i, n, 25) + 0.6f * sinf(2 * kPi * 130.8f * t) * decay(i, n, 10); } });
  oneShot(59, 4096, 60, [&](float* b, int n) { ks(b, n, fs / 523.3f, 0.97f); for (int i = 0; i < n; ++i) b[i] *= decay(i, n, 6); });
  oneShot(60, 4096, 60, [&](float* b, int n) { ks(b, n, fs / 261.6f, 0.99f); for (int i = 0; i < n; ++i) b[i] *= decay(i, n, 4); });
  oneShot(61, 4096, 60, [&](float* b, int n) { ks(b, n, fs / 392.0f, 0.98f); for (int i = 0; i < n; ++i) { float t = i / fs; b[i] = b[i] * decay(i, n, 5) + 0.4f * sinf(2 * kPi * 2093.0f * t) * decay(i, n, 30); } });
  oneShot(62, 1024, 60, [&](float* b, int n) { for (int i = 0; i < n; ++i) b[i] = (i < 40 ? (i & 4 ? 1.0f : -1.0f) : frand() * 0.3f) * decay(i, n, 30); });
  oneShot(63, 4096, 60, [&](float* b, int n) { float y = 0; for (int i = 0; i < n; ++i) { y += 0.25f * (frand() - y); b[i] = y * decay(i, n, 8) * (i < 300 ? i / 300.0f : 1.0f); } });
  oneShot(64, 4096, 60, [&](float* b, int n) { for (int i = 0; i < n; ++i) { float t = i / fs; b[i] = (sinf(2 * kPi * 80.0f * t) + 0.3f * frand()) * decay(i, n, 14); } });
  oneShot(65, 4096, 60, [&](float* b, int n) { for (int i = 0; i < n; ++i) { float t = i / fs; b[i] = (sinf(2 * kPi * 220.0f * t) + 0.6f * sinf(2 * kPi * 560.0f * t) * decay(i, n, 10)) * decay(i, n, 7); } });
  oneShot(66, 8192, 60, [&](float* b, int n) { float ph = 0; for (int i = 0; i < n; ++i) { float t = i / fs; float f = 45.0f + 110.0f * expf(-t * 25.0f); ph += 2 * kPi * f / fs; b[i] = sinf(ph) * decay(i, n, 6) + 0.4f * frand() * decay(i, n, 80); } });
  oneShot(67, 8192, 60, [&](float* b, int n) { float y = 0; for (int i = 0; i < n; ++i) { float t = i / fs; y += 0.7f * (frand() - y); b[i] = 0.8f * y * decay(i, n, 9) + 0.6f * sinf(2 * kPi * 185.0f * t) * decay(i, n, 20); } });
  oneShot(68, 8192, 60, [&](float* b, int n) { float ph = 0; for (int i = 0; i < n; ++i) { float t = i / fs; float f = 110.0f + 90.0f * expf(-t * 12.0f); ph += 2 * kPi * f / fs; b[i] = sinf(ph) * decay(i, n, 5) + 0.2f * frand() * decay(i, n, 30); } });
  oneShot(69, 4096, 60, [&](float* b, int n) { float y = 0, z = 0; for (int i = 0; i < n; ++i) { float x = frand(); y += 0.9f * (x - y); z = x - y; b[i] = z * decay(i, n, 12); } });

  // DRUMS1-5: key zones of the drum one-shots, each at its native rate near the zone's keys.
  auto drumSet = [&](int wave, const int* drums, const int* lows, int count) {
    for (int b = 0; b < 16; ++b) {
      int key = b * 8 + 4;
      int z = 0;
      for (int i = 0; i < count; ++i) if (key >= lows[i]) z = i;
      int nativeKey = (lows[z] < 36 ? 36 : lows[z]) + 2;
      const Shot& s = shots[drums[z]];
      tab_[wave][b] = recordFor(s.addr, s.sc, s.sc, 38.3f - nativeKey);
    }
  };
  { const int dr[] = {66, 67, 68, 65, 69}; const int lo[] = {0, 44, 52, 60, 68}; drumSet(70, dr, lo, 5); }
  { const int dr[] = {66, 68, 67, 69, 65}; const int lo[] = {0, 44, 56, 64, 72}; drumSet(71, dr, lo, 5); }
  { const int dr[] = {66, 67, 69, 68}; const int lo[] = {0, 48, 60, 68}; drumSet(72, dr, lo, 4); }
  { const int dr[] = {66, 69, 68}; const int lo[] = {0, 48, 60}; drumSet(73, dr, lo, 3); }
  { const int dr[] = {67, 65, 68, 69}; const int lo[] = {0, 48, 60, 72}; drumSet(74, dr, lo, 4); }

  free(buf);
}

}  // namespace sq80
