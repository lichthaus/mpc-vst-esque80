// SQ-80 program (patch) model with the front-panel ranges, plus the 102-byte Program Control
// Block codec used by SysEx single/all program dumps (Musician's Manual, appendix 5).
#pragma once
#include <stdint.h>

namespace sq80 {

constexpr int kWaveCount = 75;
extern const char* const kWaveNames[kWaveCount];

enum ModSrc : uint8_t {
  M_LFO1, M_LFO2, M_LFO3, M_ENV1, M_ENV2, M_ENV3, M_ENV4, M_VEL, M_VEL2, M_KYBD, M_KYBD2,
  M_WHEEL, M_PEDAL, M_XCTRL, M_PRESS, M_OFF, kModSrcCount
};
extern const char* const kModSrcNames[kModSrcCount];

enum LfoWave : uint8_t { LFO_TRI, LFO_SAW, LFO_SQR, LFO_NOISE };

struct EnvP {
  int8_t l1, l2, l3;   // -63..63
  uint8_t lv;          // 0..63 velocity level control
  bool lvExp;          // LV curve: false = linear (L), true = exponential (X)
  uint8_t t1v;         // 0..63 velocity attack control
  uint8_t t1, t2, t3, t4;  // 0..63 (see kEnvTimeSec)
  bool t4r;            // second release ("R")
  uint8_t tk;          // 0..63 keyboard decay scaling
};

struct LfoP {
  uint8_t freq;        // 0..63
  bool reset, human;
  uint8_t wave;        // LfoWave
  uint8_t l1, delay, l2;  // 0..63
  uint8_t mod;         // ModSrc for LFO depth
};

struct OscP {
  int8_t oct;          // -3..5
  uint8_t semi;        // 0..11
  uint8_t fine;        // 0..31 (about 3 cents each)
  uint8_t wave;        // 0..74
  uint8_t fmSrc1, fmSrc2;
  int8_t fmAmt1, fmAmt2;  // pitch mod depth -63..63
  uint8_t level;       // DCA level 0..63
  bool enable;         // DCA OUTPUT
  uint8_t amSrc1, amSrc2;
  int8_t amAmt1, amAmt2;  // DCA mod depth
};

struct Program {
  char name[7];
  EnvP env[4];
  LfoP lfo[3];
  OscP osc[3];
  uint8_t dca4;        // ENV4 mod depth (final volume) 0..63
  uint8_t fc, q, kbd;  // filter 0..127, 0..31, 0..63
  uint8_t fcSrc1, fcSrc2;
  int8_t fcAmt1, fcAmt2;
  bool am, sync, mono, voiceRestart, envRestart, oscRestart, cycle;
  uint8_t glide;       // 0..63
  uint8_t pan;         // 0..15
  uint8_t panSrc;
  int8_t panAmt;
  // split / layer (stored, not yet used by the engine)
  bool splitLayer, layer, split, splitDir;
  uint8_t splitLayerProg, layerProg, splitProg, splitPoint;
};

extern const float kEnvTimeSec[64];  // manual p. 64

void decodePcb(const uint8_t* pcb, Program& p);
void encodePcb(const Program& p, uint8_t* pcb);
void blankProgram(Program& p);

int presetCount();
const Program& preset(int i);

}  // namespace sq80
