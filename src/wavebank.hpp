// Wave memory as the DOC sees it: a 256 KB bank of 8-bit samples (the SQ-80's WAVE 0/1 ROMs
// plus the WAVE 2/3 sockets) and, for each of the 75 waves, 16 key-range (key/8) wavesample
// records: ROM page, DOC wavetable register (bank bits, table size, resolution) and the
// semitone / fine pitch offsets of that sample. Loaded from /sq80 on the TF card or LittleFS;
// without data, all 75 waves are synthesized at boot from the manual's descriptions.
#pragma once
#include <stdint.h>

namespace sq80 {

constexpr uint32_t kBankSize = 256 * 1024;

struct WaveSample {
  uint8_t page;     // table start = (bank << 16) + (page << 8)
  uint8_t reg;      // bit 7 bank bit 0, bit 6 bank bit 1, bits 5-3 size code, bits 2-0 resolution
  int8_t semiOfs;   // semitone offset of the sample's root
  uint8_t fineOfs;  // 1/256 semitone
};

class WaveBank {
 public:
  bool init();
  const uint8_t* rom() const { return mem_; }
  const WaveSample& pick(int wave, int key) const {
    if (wave < 0 || wave >= 75) wave = 0;
    return tab_[wave][(key < 0 ? 0 : (key > 127 ? 127 : key)) >> 3];
  }
  bool fromStorage() const { return fromStorage_; }
  // Internal-RAM copies of the tables a program uses (PSRAM reads stall behind the display
  // DMA). cacheProgram() refills the cache for the three waves; tableBytes() returns the
  // cached copy or the bank itself.
  void cacheProgram(const uint8_t* waves, int count);
  const uint8_t* tableBytes(uint32_t addr, uint32_t size) const;
  uint32_t cachedBytes() const { return cacheUsed_; }

 private:
  bool load();
  void synthesize();
  uint32_t alloc(uint32_t bytes);
  WaveSample recordFor(uint32_t addr, uint8_t sizeCode, uint8_t res, float semis) const;
  uint8_t* mem_ = nullptr;
  WaveSample tab_[75][16];
  uint32_t top_ = 0;
  bool fromStorage_ = false;
  static constexpr uint32_t kCacheSize = 72 * 1024;
  static constexpr int kCacheMax = 48;
  struct CacheEntry { uint32_t addr, size, offset; };
  CacheEntry cache_[kCacheMax];
  int cacheCount_ = 0;
  uint32_t cacheUsed_ = 0;
  uint8_t* cacheMem_ = nullptr;
};

extern WaveBank waveBank;

}  // namespace sq80
