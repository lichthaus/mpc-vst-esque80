#include "program.hpp"

#include <string.h>

namespace sq80 {

// Order as listed in the Musician's Manual (section 3, "The Waves"); the first 32 are the
// ESQ-1 set. Treat the numbering as provisional until checked against a real program dump.
const char* const kWaveNames[kWaveCount] = {
    "SAW",    "BELL",   "SINE",   "SQUARE", "PULSE",  "NOISE1", "NOISE2", "NOISE3",  // classic
    "BASS",   "PIANO",  "EL PNO", "VOICE1", "VOICE2", "KICK",   "REED",   "ORGAN",   // sampled
    "SYNTH1", "SYNTH2", "SYNTH3",                                                     // additive
    "FORMT1", "FORMT2", "FORMT3", "FORMT4", "FORMT5",                                 // formants
    "PULSE2", "SQR 2",  "4 OCTS", "PRIME",  "BASS 2", "E PNO2", "OCTAVE", "OCT+5",   // band limited
    "SAW 2",  "TRIANG", "REED 2", "REED 3", "GRIT 1", "GRIT 2", "GRIT 3", "GLINT1",  // SQ-80 waves
    "GLINT2", "GLINT3", "CLAV",   "BRASS",  "STRING", "DIGIT1", "DIGIT2", "BELL 2",
    "ALIEN",
    "BREATH", "VOICE3", "STEAM",  "METAL",  "CHIME",                                  // inharmonic loops
    "BOWING", "PICK 1", "PICK 2", "MALLET", "SLAP",   "PLINK",  "PLUCK",  "PLUNK",   // transients
    "CLICK",  "CHIFF",  "THUMP",
    "LOGDRM", "KICK 2", "SNARE",  "TOMTOM", "HI HAT",                                 // drums
    "DRUMS1", "DRUMS2", "DRUMS3", "DRUMS4", "DRUMS5",                                 // drum sets
};

const char* const kModSrcNames[kModSrcCount] = {"LFO1",  "LFO2",  "LFO3",  "ENV1",  "ENV2",  "ENV3",
                                               "ENV4",  "VEL",   "VEL-X", "KYBD",  "KYBD2", "WHEEL",
                                               "PEDAL", "XCTRL", "PRESS", "*OFF*"};

// Envelope time chart, Musician's Manual p. 64 (seconds for T = 0..63)
const float kEnvTimeSec[64] = {
    0.00f, 0.01f, 0.01f, 0.02f, 0.02f, 0.03f, 0.03f, 0.03f, 0.04f, 0.04f, 0.04f, 0.05f, 0.06f,
    0.06f, 0.07f, 0.08f, 0.09f, 0.10f, 0.11f, 0.13f, 0.14f, 0.16f, 0.18f, 0.20f, 0.23f, 0.25f,
    0.29f, 0.32f, 0.36f, 0.40f, 0.45f, 0.51f, 0.57f, 0.64f, 0.72f, 0.81f, 0.91f, 1.02f, 1.14f,
    1.28f, 1.44f, 1.61f, 1.81f, 2.03f, 2.28f, 2.56f, 2.87f, 3.23f, 3.62f, 4.06f, 4.56f, 5.12f,
    5.75f, 6.45f, 7.24f, 8.13f, 9.12f, 10.24f, 11.49f, 12.90f, 14.48f, 16.25f, 18.25f, 20.48f};

// ---- PCB codec --------------------------------------------------------------
// Layout (appendix 5): NAME 6, ENV1-4 10 bytes each, LFO1-3 4 bytes each, OSC1-3 10 bytes
// each, MISC 14 bytes = 102.
static inline int8_t sext7(uint8_t b) {
  int v = b & 0x7F;
  return (int8_t)(v & 0x40 ? v - 128 : v);
}
static inline int8_t clamp63(int v) { return (int8_t)(v < -63 ? -63 : (v > 63 ? 63 : v)); }

void decodePcb(const uint8_t* b, Program& p) {
  memcpy(p.name, b, 6);
  p.name[6] = 0;
  b += 6;
  for (int i = 0; i < 4; ++i, b += 10) {
    EnvP& e = p.env[i];
    e.l1 = clamp63((int8_t)b[0] / 2);
    e.l2 = clamp63((int8_t)b[1] / 2);
    e.l3 = clamp63((int8_t)b[2] / 2);
    e.t1 = b[3] & 0x3F;
    e.t2 = b[4] & 0x3F;
    e.t3 = b[5] & 0x3F;
    e.t4 = b[6] & 0x3F;
    e.t4r = b[6] & 0x80;
    e.lv = (b[7] >> 2) & 0x3F;
    e.lvExp = b[7] & 0x01;
    e.t1v = b[8] & 0x3F;
    e.tk = b[9] & 0x3F;
  }
  for (int i = 0; i < 3; ++i, b += 4) {
    LfoP& l = p.lfo[i];
    l.wave = b[0] >> 6;
    l.freq = b[0] & 0x3F;
    l.l1 = b[1] & 0x3F;
    l.l2 = b[2] & 0x3F;
    l.mod = (b[2] >> 6) | ((b[1] >> 6) << 2);  // low bits in the L2 byte, high bits in the L1 byte
    l.reset = b[3] & 0x80;
    l.human = b[3] & 0x40;
    l.delay = b[3] & 0x3F;
  }
  for (int i = 0; i < 3; ++i, b += 10) {
    OscP& o = p.osc[i];
    int st = b[0] & 0x7F;
    o.oct = (int8_t)(st / 12 - 3);
    o.semi = st % 12;
    o.fine = (b[1] >> 3) & 0x1F;
    o.fmSrc1 = b[2] & 0x0F;
    o.fmSrc2 = b[2] >> 4;
    o.fmAmt1 = clamp63((int8_t)b[3] / 2);
    o.fmAmt2 = clamp63((int8_t)b[4] / 2);
    o.wave = b[5] < kWaveCount ? b[5] : 0;
    o.enable = b[6] & 0x80;
    o.level = (b[6] & 0x7E) >> 1;
    o.amSrc1 = b[7] & 0x0F;
    o.amSrc2 = b[7] >> 4;
    o.amAmt1 = clamp63((int8_t)b[8] / 2);
    o.amAmt2 = clamp63((int8_t)b[9] / 2);
  }
  p.am = b[0] & 0x80;
  p.dca4 = (b[0] & 0x7E) >> 1;
  p.sync = b[1] & 0x80;
  p.fc = b[1] & 0x7F;
  p.q = b[2] & 0x1F;
  p.fcSrc1 = b[3] & 0x0F;
  p.fcSrc2 = b[3] >> 4;
  p.voiceRestart = b[4] & 0x80;
  p.fcAmt1 = sext7(b[4]);
  p.mono = b[5] & 0x80;
  p.fcAmt2 = sext7(b[5]);
  p.envRestart = b[6] & 0x80;
  p.kbd = (b[6] & 0x7E) >> 1;
  p.oscRestart = b[7] & 0x80;
  p.glide = b[7] & 0x3F;
  p.splitDir = b[8] & 0x80;
  p.splitPoint = b[8] & 0x7F;
  p.layer = b[9] & 0x80;
  p.layerProg = b[9] & 0x7F;
  p.split = b[10] & 0x80;
  p.splitProg = b[10] & 0x7F;
  p.splitLayer = b[11] & 0x80;
  p.splitLayerProg = b[11] & 0x7F;
  p.pan = b[12] >> 4;
  p.panSrc = b[12] & 0x0F;
  p.cycle = b[13] & 0x80;
  p.panAmt = sext7(b[13]);
}

void encodePcb(const Program& p, uint8_t* b) {
  memset(b, 0, 102);
  memcpy(b, p.name, 6);
  b += 6;
  for (int i = 0; i < 4; ++i, b += 10) {
    const EnvP& e = p.env[i];
    b[0] = (uint8_t)(int8_t)(e.l1 * 2);
    b[1] = (uint8_t)(int8_t)(e.l2 * 2);
    b[2] = (uint8_t)(int8_t)(e.l3 * 2);
    b[3] = e.t1 & 0x3F;
    b[4] = e.t2 & 0x3F;
    b[5] = e.t3 & 0x3F;
    b[6] = (e.t4 & 0x3F) | (e.t4r ? 0x80 : 0);
    b[7] = ((e.lv & 0x3F) << 2) | (e.lvExp ? 1 : 0);
    b[8] = e.t1v & 0x3F;
    b[9] = e.tk & 0x3F;
  }
  for (int i = 0; i < 3; ++i, b += 4) {
    const LfoP& l = p.lfo[i];
    b[0] = ((l.wave & 3) << 6) | (l.freq & 0x3F);
    b[1] = (((l.mod >> 2) & 3) << 6) | (l.l1 & 0x3F);
    b[2] = ((l.mod & 3) << 6) | (l.l2 & 0x3F);
    b[3] = (l.reset ? 0x80 : 0) | (l.human ? 0x40 : 0) | (l.delay & 0x3F);
  }
  for (int i = 0; i < 3; ++i, b += 10) {
    const OscP& o = p.osc[i];
    b[0] = (uint8_t)((o.oct + 3) * 12 + o.semi);
    b[1] = (o.fine & 0x1F) << 3;
    b[2] = (o.fmSrc1 & 0x0F) | ((o.fmSrc2 & 0x0F) << 4);
    b[3] = (uint8_t)(int8_t)(o.fmAmt1 * 2);
    b[4] = (uint8_t)(int8_t)(o.fmAmt2 * 2);
    b[5] = o.wave;
    b[6] = (o.enable ? 0x80 : 0) | ((o.level & 0x3F) << 1);
    b[7] = (o.amSrc1 & 0x0F) | ((o.amSrc2 & 0x0F) << 4);
    b[8] = (uint8_t)(int8_t)(o.amAmt1 * 2);
    b[9] = (uint8_t)(int8_t)(o.amAmt2 * 2);
  }
  b[0] = (p.am ? 0x80 : 0) | ((p.dca4 & 0x3F) << 1);
  b[1] = (p.sync ? 0x80 : 0) | (p.fc & 0x7F);
  b[2] = p.q & 0x1F;
  b[3] = (p.fcSrc1 & 0x0F) | ((p.fcSrc2 & 0x0F) << 4);
  b[4] = (p.voiceRestart ? 0x80 : 0) | (p.fcAmt1 & 0x7F);
  b[5] = (p.mono ? 0x80 : 0) | (p.fcAmt2 & 0x7F);
  b[6] = (p.envRestart ? 0x80 : 0) | ((p.kbd & 0x3F) << 1);
  b[7] = (p.oscRestart ? 0x80 : 0) | (p.glide & 0x3F);
  b[8] = (p.splitDir ? 0x80 : 0) | (p.splitPoint & 0x7F);
  b[9] = (p.layer ? 0x80 : 0) | (p.layerProg & 0x7F);
  b[10] = (p.split ? 0x80 : 0) | (p.splitProg & 0x7F);
  b[11] = (p.splitLayer ? 0x80 : 0) | (p.splitLayerProg & 0x7F);
  b[12] = ((p.pan & 0x0F) << 4) | (p.panSrc & 0x0F);
  b[13] = (p.cycle ? 0x80 : 0) | (p.panAmt & 0x7F);
}

void blankProgram(Program& p) {
  memset(&p, 0, sizeof p);
  strcpy(p.name, "BLANK ");
  for (int i = 0; i < 4; ++i) p.env[i] = {63, 63, 63, 0, false, 0, 0, 0, 0, 20, false, 0};
  for (int i = 0; i < 3; ++i) p.lfo[i] = {20, true, false, LFO_TRI, 0, 0, 63, M_WHEEL};
  for (int i = 0; i < 3; ++i) {
    p.osc[i] = {0, 0, 0, 0, M_OFF, M_OFF, 0, 0, 0, false, M_OFF, M_OFF, 0, 0};
  }
  p.osc[0].enable = true;
  p.osc[0].level = 63;
  p.dca4 = 63;
  p.fc = 127;
  p.fcSrc1 = p.fcSrc2 = M_OFF;
  p.panSrc = M_OFF;
  p.pan = 8;
  p.voiceRestart = true;
  p.envRestart = true;
}

// ---- presets ---------------------------------------------------------------
static Program g_presets[8];
static int g_presetCount = 0;

static void setName(Program& p, const char* n) {
  memset(p.name, ' ', 6);
  for (int i = 0; i < 6 && n[i]; ++i) p.name[i] = n[i];
  p.name[6] = 0;
}

static void buildPresets() {
  if (g_presetCount) return;
  Program p;

  // 0: BASIC — two detuned saws and a sub square, ENV2 sweeps the filter
  blankProgram(p);
  setName(p, "BASIC");
  p.osc[0] = {0, 0, 0, 0, M_LFO1, M_OFF, 4, 0, 63, true, M_OFF, M_OFF, 0, 0};
  p.osc[1] = {0, 0, 9, 0, M_LFO1, M_OFF, 4, 0, 52, true, M_OFF, M_OFF, 0, 0};
  p.osc[2] = {-1, 0, 0, 3, M_LFO1, M_OFF, 4, 0, 40, true, M_OFF, M_OFF, 0, 0};
  p.env[1] = {63, 30, 15, 20, false, 0, 0, 30, 40, 25, false, 10};
  p.env[3] = {63, 55, 48, 30, false, 20, 2, 28, 36, 30, false, 15};
  p.fc = 60; p.q = 8; p.kbd = 24;
  p.fcSrc1 = M_ENV2; p.fcAmt1 = 45; p.fcSrc2 = M_OFF;
  p.lfo[0] = {30, false, false, LFO_TRI, 0, 0, 0, M_WHEEL};
  g_presets[g_presetCount++] = p;

  // 1: STRING — SAW 2 + STRING with a BOWING attack, slow envelopes
  blankProgram(p);
  setName(p, "STRING");
  p.osc[0] = {0, 0, 2, 32, M_LFO1, M_OFF, 3, 0, 50, true, M_OFF, M_OFF, 0, 0};
  p.osc[1] = {0, 0, 27, 44, M_LFO1, M_OFF, 3, 0, 50, true, M_OFF, M_OFF, 0, 0};
  p.osc[2] = {0, 0, 0, 54, M_OFF, M_OFF, 0, 0, 0, true, M_ENV3, M_OFF, 50, 0};
  p.env[1] = {40, 25, 20, 10, false, 0, 10, 40, 40, 35, false, 0};
  p.env[2] = {63, 0, 0, 0, false, 0, 0, 22, 0, 10, false, 0};
  p.env[3] = {63, 60, 56, 25, false, 10, 24, 40, 40, 40, false, 10};
  p.fc = 55; p.q = 2; p.kbd = 30;
  p.fcSrc1 = M_ENV2; p.fcAmt1 = 35; p.fcSrc2 = M_WHEEL; p.fcAmt2 = 20;
  p.lfo[0] = {28, false, true, LFO_TRI, 0, 20, 25, M_WHEEL};
  p.voiceRestart = false;
  g_presets[g_presetCount++] = p;

  // 2: E PIANO — EL PNO + MALLET attack + GLINT shimmer, velocity on the filter
  blankProgram(p);
  setName(p, "E PNO");
  p.osc[0] = {0, 0, 0, 10, M_OFF, M_OFF, 0, 0, 56, true, M_OFF, M_OFF, 0, 0};
  p.osc[1] = {0, 0, 0, 57, M_OFF, M_OFF, 0, 0, 0, true, M_ENV3, M_OFF, 50, 0};
  p.osc[2] = {1, 0, 0, 39, M_OFF, M_OFF, 0, 0, 0, true, M_VEL2, M_OFF, 30, 0};
  p.env[1] = {63, 40, 10, 40, false, 0, 0, 20, 45, 20, false, 20};
  p.env[2] = {63, 0, 0, 20, false, 0, 0, 12, 0, 8, false, 0};
  p.env[3] = {63, 50, 20, 30, false, 0, 0, 30, 52, 22, false, 25};
  p.fc = 50; p.q = 0; p.kbd = 40;
  p.fcSrc1 = M_ENV2; p.fcAmt1 = 40; p.fcSrc2 = M_VEL; p.fcAmt2 = 25;
  p.lfo[0] = {30, false, false, LFO_TRI, 0, 0, 0, M_WHEEL};
  p.osc[0].fmSrc1 = M_LFO1; p.osc[0].fmAmt1 = 3;
  g_presets[g_presetCount++] = p;

  // 3: BELLS — BELL and BELL 2 with AM, long second release
  blankProgram(p);
  setName(p, "BELLS");
  p.osc[0] = {0, 0, 0, 1, M_OFF, M_OFF, 0, 0, 50, true, M_OFF, M_OFF, 0, 0};
  p.osc[1] = {1, 7, 0, 47, M_OFF, M_OFF, 0, 0, 45, true, M_OFF, M_OFF, 0, 0};
  p.osc[2] = {-1, 0, 0, 53, M_OFF, M_OFF, 0, 0, 30, true, M_OFF, M_OFF, 0, 0};
  p.env[1] = {63, 30, 10, 30, false, 0, 0, 25, 50, 30, false, 15};
  p.env[3] = {63, 45, 25, 30, false, 0, 0, 30, 55, 40, true, 20};
  p.fc = 70; p.q = 4; p.kbd = 35;
  p.fcSrc1 = M_ENV2; p.fcAmt1 = 30; p.fcSrc2 = M_OFF;
  p.am = true;
  g_presets[g_presetCount++] = p;

  // 4: BASS — BASS + SLAP attack + sub, mono with glide
  blankProgram(p);
  setName(p, "SLPBAS");
  p.osc[0] = {-1, 0, 0, 8, M_OFF, M_OFF, 0, 0, 60, true, M_OFF, M_OFF, 0, 0};
  p.osc[1] = {-1, 0, 0, 58, M_OFF, M_OFF, 0, 0, 0, true, M_ENV3, M_OFF, 55, 0};
  p.osc[2] = {-2, 0, 0, 2, M_OFF, M_OFF, 0, 0, 40, true, M_OFF, M_OFF, 0, 0};
  p.env[1] = {63, 20, 5, 40, false, 20, 0, 18, 30, 10, false, 10};
  p.env[2] = {63, 0, 0, 10, false, 0, 0, 10, 0, 5, false, 0};
  p.env[3] = {63, 58, 50, 20, false, 0, 0, 25, 40, 12, false, 10};
  p.fc = 40; p.q = 10; p.kbd = 20;
  p.fcSrc1 = M_ENV2; p.fcAmt1 = 45; p.fcSrc2 = M_VEL; p.fcAmt2 = 15;
  p.mono = true; p.glide = 12;
  g_presets[g_presetCount++] = p;

  // 5: DRUMS1 — the multisampled drum set, no pitch tracking
  blankProgram(p);
  setName(p, "DRUMS1");
  p.osc[0] = {0, 0, 0, 70, M_OFF, M_OFF, 0, 0, 63, true, M_OFF, M_OFF, 0, 0};
  p.env[3] = {63, 63, 63, 20, false, 0, 0, 0, 0, 30, false, 0};
  p.fc = 127; p.q = 0; p.kbd = 0;
  p.voiceRestart = false;
  g_presets[g_presetCount++] = p;
}

int presetCount() {
  buildPresets();
  return g_presetCount;
}

const Program& preset(int i) {
  buildPresets();
  if (i < 0) i = 0;
  if (i >= g_presetCount) i = g_presetCount - 1;
  return g_presets[i];
}

}  // namespace sq80
