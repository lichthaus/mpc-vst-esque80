// SQ-80 voice engine. 8 voices x 3 DOC oscillators from the wave bank, DCA1-3, the SQ-80
// 4-pole low-pass, DCA4 + pan, four envelopes, three LFOs, the 15-source modulation matrix.
// Laws (pitch, level curves, filter, LFO rate, envelope scaling) follow the behaviour of the
// SQ-80 as documented by the SQ8L port (docs/modules/*), re-implemented here.
#pragma once
#include <stdint.h>

#include "doc.hpp"
#include "program.hpp"
#include "wavebank.hpp"

namespace sq80 {

constexpr int kVoices = 8;
constexpr int kOscs = 3;
constexpr int kBlock = 32;  // control rate = 38461 / 32 = 1.2 kHz

constexpr int kParts = 8;   // MULTI / MONO parts (the SQ-80's eight tracks); OMNI and POLY use part 0

class Engine {
 public:
  void init();
  // The editor works on one part ("edit part"); program(), setProgram() and loadProgram()
  // address it. Each part has its own program, controllers, bend and held keys.
  Program& program() { return parts_[edit_].prog; }
  const Program& partProgram(int part) const { return parts_[part & 7].prog; }
  void setProgram(const Program& p);
  void loadProgram(int index);  // from banks::, into the edit part
  void loadPartProgram(int part, int index);
  int programIndex() const { return parts_[edit_].progIdx; }
  int partProgramIndex(int part) const { return parts_[part & 7].progIdx; }
  int editPart() const { return edit_; }
  void setEditPart(int part);
  int mode() const { return mode_; }         // 0 OMNI, 1 POLY, 2 MULTI, 3 MONO (one voice per part)
  void setMode(int mode);
  uint8_t partChannel(int part) const { return parts_[part & 7].channel; }  // MULTI: 0 off, 1-16
  void setPartChannel(int part, int ch);
  void (*onSetupChanged)() = nullptr;      // mode / channels / part programs / edit part changed
  void cacheCurrentWaves();  // after editing a wave from the UI

  // UI / MIDI thread (single producer); part -1 = the edit part
  void noteOn(uint8_t note, uint8_t vel, int part = -1);
  void noteOff(uint8_t note, int part = -1);
  void allNotesOff(int part = -2);  // -2 = every part
  void setController(ModSrc src, uint8_t value127, int part = -1);  // WHEEL, PEDAL, XCTRL, PRESS (0..127)
  void setPolyPressure(uint8_t note, uint8_t value127, int part = -1);
  void setBend(int16_t raw, int part = -1);  // -8192..8191
  // CV voices: two independent pitch/gate pairs on the edit part. Each slot's note starts a
  // voice that then follows cvPitch(slot) continuously (semitone offset from the note) while gated.
  void cvNoteOn(int slot, uint8_t note, uint8_t vel);
  void cvNoteOff(int slot);
  void cvPitch(int slot, float semitoneOffset);
  void setCc(uint8_t cc, uint8_t value, int part = -1);  // 7 volume, 8 balance, 11 expression, 74 cutoff

  // audio thread
  void render(int16_t* stereo, int frames);

  bool voiceActive(int v) const { return voices_[v].active; }
  bool voiceGate(int v) const { return voices_[v].gate; }
  int voiceNote(int v) const { return voices_[v].note; }
  int voicePart(int v) const { return voices_[v].part; }
  int activeVoices() const;
  uint32_t blocksRendered() const { return blocks_; }
  uint32_t renderMicros() const { return renderUs_; }

 private:
  struct Env {
    int stage = 0;  // 0 idle, 1->L1, 2->L2, 3->L3, 4 hold, 5 release, 6 second release
    float value = 0, target = 0, rate = 0;  // -126..126 units
    float levelScale = 1;
    int tkIdx = 0, t1vIdx = 0;
  };
  struct Lfo {
    float phase = 0, fade = 0, out = 0, noise = 0, emuClock = 0;
    int humanOfs = 0;
  };
  struct Filter {
    float s[5] = {0, 0, 0, 0, 0};
    float g = 0, k = 0, p = 0, gain = 1;
    float pT = 0, kT = 0, gainT = 1;
  };
  struct Voice {
    bool active = false, gate = false;
    uint8_t part = 0;
    int note = -1;
    uint32_t age = 0;
    uint8_t vel = 0;
    uint8_t polyPress = 0;
    int tick = 0;
    float glideOfs = 0, glideRate = 0;  // 1/256 semitone units
    int8_t cvSlot = -1;
    DocOsc osc[kOscs];
    const WaveSample* ws[kOscs] = {nullptr, nullptr, nullptr};
    int32_t oscPitch[kOscs] = {0, 0, 0};
    Env env[4];
    Lfo lfo[3];
    Filter flt;
    float volL = 0, volR = 0, volLT = 0, volRT = 0;
    float dcX1 = 0, dcY1 = 0;
    bool dcBlock = false;
    float src[kModSrcCount];
  };

  struct Part {
    Program prog{};
    int progIdx = 0;
    uint8_t channel = 0;                 // MULTI receive channel, 0 = off
    float ctrl[kModSrcCount] = {0};      // WHEEL / PEDAL / XCTRL / PRESS 0..127
    volatile int16_t bend = 0;
    uint8_t cc7 = 127, cc8 = 64, cc11 = 0, cc74 = 64;
    uint8_t held[16];
    int heldCount = 0;
    int lastKey = -1;
  };

  void drainEvents();
  void triggerVoice(Voice& v, uint8_t note, uint8_t vel, bool legato, int glideFrom);
  void startNote(uint8_t note, uint8_t vel, int cvSlot, int part);
  void releaseVoice(Voice& v);
  Voice* allocVoice(uint8_t note, int part);
  void setupOsc(Voice& v, int o);
  void envStart(Env& e, const EnvP& p, uint8_t vel, int note, bool restart);
  void envStage(Env& e, const EnvP& p, int stage);
  void envTick(Env& e, const EnvP& p, bool cycle, int samples);
  void recacheWaves();
  void setupChanged() { if (onSetupChanged) onSetupChanged(); }
  int resolvePart(int part) const { return part < 0 ? edit_ : (part & 7); }
  void lfoTick(Lfo& l, const LfoP& p, const float* src, int samples);
  void filterSet(Filter& f, int cutoff, int reso, bool immediate);
  void controlTick(Voice& v, int samples);
  float modSum(const Voice& v, uint8_t s1, int8_t a1, uint8_t s2, int8_t a2) const;

  Part parts_[kParts];
  int edit_ = 0;
  int mode_ = 0;
  uint8_t cachedWaves_[kParts * kOscs];
  int cachedCount_ = 0;
  Voice voices_[kVoices];
  uint32_t ageCounter_ = 0;
  volatile uint32_t blocks_ = 0;
  volatile uint32_t renderUs_ = 0;
  volatile float cvPitch_[2] = {0, 0};

  // tables built at init
  float dcaAmp_[256];     // DCA level index -> oscillator amplitude
  float ampShape_[256];   // DCA4 level index -> gain
  float cutoffP_[256];    // filter pole coefficient per cutoff index
  float cutoffA_[256], cutoffB_[256];  // feedback factors per cutoff index
  float resoAmt_[32], resoGain_[32];
  float fltPole_ = 0.98f;

  struct Event { uint8_t type, a, b, part; };
  static constexpr int kQueueLen = 128;
  Event queue_[kQueueLen];
  volatile uint32_t qHead_ = 0, qTail_ = 0;
  void push(uint8_t type, uint8_t a, uint8_t b, int part);
};

extern Engine engine;

}  // namespace sq80
