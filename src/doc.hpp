// Ensoniq DOC 5503 oscillator, per-sample algorithm taken from MAME's es5503.cpp (R. Belmont,
// BSD-3-Clause), reduced to what the SQ-80 uses: free-run and one-shot modes, 8-bit samples
// multiplied by an 8-bit volume, a zero byte halts the oscillator, table sizes 256..32768 and
// resolution 0..7. Sync and AM are done by the engine with SQ-80 semantics (OSC1 -> OSC2).
#pragma once
#include <stdint.h>

namespace sq80 {

// SQ-80: 8 MHz quartz, 24 oscillators enabled -> (8e6 / 8) / (24 + 2) = 38461 Hz
constexpr float kDocClock = 8000000.0f;
constexpr int kDocOscsEnabled = 24;
constexpr float kSampleRate = (kDocClock / 8.0f) / (kDocOscsEnabled + 2);  // 38461 Hz, the real DOC rate; the plugin resamples to the host rate

struct DocOsc {
  uint32_t acc = 0;       // 24-bit phase accumulator (held in 32 bits, wraps by subtraction)
  uint32_t ptr = 0;       // wavetable start in the bank, aligned to the table size
  const uint8_t* table = nullptr;  // where the table's bytes are read from (cache or bank)
  uint32_t sizemask = 255;
  uint16_t freq = 0;      // frequency register
  int16_t vol = 0;        // volume register (0..255; AM drives it signed, -52..255)
  uint8_t sizeCode = 0;   // table size = 256 << sizeCode
  uint8_t res = 0;        // address resolution 0..7
  uint8_t shift = 9;      // 9 + res - sizeCode
  bool oneShot = false;
  bool halted = true;
  bool wrapped = false;   // table end reached during the last step (used for SYNC)
  uint8_t rawByte = 0x80; // last byte read (used for AM)

  inline void setTable(const uint8_t* bytes, uint32_t pointer, uint8_t size, uint8_t resolution, bool once) {
    sizeCode = size & 7;
    res = resolution & 7;
    sizemask = (256u << sizeCode) - 1;
    ptr = pointer & ~sizemask;
    table = bytes;
    shift = (uint8_t)(9 + res - sizeCode);
    oneShot = once;
  }
  inline void start() { acc = 0; halted = false; wrapped = false; }

  // One output sample: returns sample * volume (range -32640..32385), 0 when halted.
  inline int32_t step() {
    wrapped = false;
    if (halted) return 0;
    const uint32_t altram = acc >> shift;
    const uint32_t idx = altram & sizemask;
    acc += freq;
    const uint8_t b = table[idx];
    rawByte = b;
    if (b == 0) {  // a zero byte halts the oscillator in every mode
      halted = true;
      return 0;
    }
    const int32_t out = (int32_t)(int8_t)(b ^ 0x80) * (int32_t)vol;
    if (altram >= sizemask) {
      wrapped = true;
      if (oneShot) halted = true;
      else acc -= (sizemask << shift);  // loop, keeping the fractional phase
    }
    return out;
  }
};

}  // namespace sq80
