#include "engine.hpp"

#include "compat.hpp"
#include <math.h>
#include <string.h>

#include "banks.hpp"

namespace sq80 {

Engine engine;

namespace {
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
uint32_t rng = 0x9E3779B9;
inline float frand() {  // -1..1
  rng = rng * 1664525u + 1013904223u;
  return (float)(int32_t)rng / 2147483648.0f;
}
inline uint32_t urand() {
  rng = rng * 1664525u + 1013904223u;
  return rng;
}

// Velocity curves: VEL is nearly linear, VEL-X rises late (fits of the SQ-80 curves).
inline float velLinear(int vel) { return vel < 1 ? 1.0f : (float)vel; }
inline float velExp(int vel) { return 127.0f * powf(vel / 127.0f, 3.4f); }

// Piecewise-linear-in-dB level curves read off the SQ-80's DCA and DCA4 laws.
struct Anchor { int i; float g; };
const Anchor kDcaAnchors[] = {{0, 1e-6f}, {1, 0.000217f}, {4, 0.000695f}, {16, 0.00488f}, {40, 0.00504f},
                              {48, 0.00513f}, {64, 0.00626f}, {80, 0.00934f}, {96, 0.01565f}, {112, 0.0286f},
                              {128, 0.0416f}, {144, 0.0638f}, {160, 0.085f}, {176, 0.1257f}, {192, 0.2494f},
                              {208, 0.371f}, {224, 0.586f}, {240, 0.818f}, {255, 1.0f}};
const Anchor kAmpAnchors[] = {{0, 1e-6f}, {1, 0.00388f}, {16, 0.00835f}, {32, 0.0177f}, {48, 0.0375f}, {64, 0.0693f},
                              {80, 0.1158f}, {96, 0.1625f}, {112, 0.264f}, {128, 0.379f}, {144, 0.5008f},
                              {160, 0.5907f}, {176, 0.7078f}, {192, 0.8445f}, {208, 1.002f}, {224, 1.2273f},
                              {240, 1.471f}, {255, 2.0f}};
void buildCurve(float* out, const Anchor* a, int n) {
  for (int k = 0; k + 1 < n; ++k) {
    float l0 = logf(a[k].g), l1 = logf(a[k + 1].g);
    for (int i = a[k].i; i <= a[k + 1].i; ++i) {
      float t = (float)(i - a[k].i) / (a[k + 1].i - a[k].i);
      out[i] = expf(l0 + (l1 - l0) * t);
    }
  }
  out[0] = 0;
}

// DOC frequency register from a pitch in 1/256 semitone (F = 28 * 2^(p/3072)); pitches past
// the register's range fold down by octaves as the SQ-80 does.
inline uint16_t pitchToFreq(int32_t pitch) {
  while (pitch >= 0x8000) pitch -= 0xC00;
  if (pitch < 0) pitch = 0;
  float F = 28.0f * exp2f((float)(pitch - 16) / 3072.0f);
  return (uint16_t)(F < 1.0f ? 1.0f : (F > 65535.0f ? 65535.0f : F));
}
}  // namespace

// ---- events ------------------------------------------------------------------------------
void Engine::push(uint8_t type, uint8_t a, uint8_t b, int part) {
  uint32_t next = (qHead_ + 1) % kQueueLen;
  if (next == qTail_) return;
  queue_[qHead_] = {type, a, b, (uint8_t)(part < 0 ? 0xFF : (part & 7))};
  qHead_ = next;
}
void Engine::noteOn(uint8_t note, uint8_t vel, int part) { push(1, note, vel, resolvePart(part)); }
void Engine::noteOff(uint8_t note, int part) { push(2, note, 0, resolvePart(part)); }
void Engine::allNotesOff(int part) { push(3, 0, 0, part == -2 ? -1 : resolvePart(part)); }
void Engine::setController(ModSrc src, uint8_t v, int part) { push(4, (uint8_t)src, v > 127 ? 127 : v, resolvePart(part)); }
void Engine::setPolyPressure(uint8_t note, uint8_t v, int part) { push(5, note, v > 127 ? 127 : v, resolvePart(part)); }
void Engine::setBend(int16_t raw, int part) { parts_[resolvePart(part)].bend = raw; }
void Engine::cvNoteOn(int slot, uint8_t note, uint8_t vel) { push(7, (uint8_t)((note & 0x7F) | ((slot & 1) << 7)), vel, edit_); }
void Engine::cvNoteOff(int slot) { push(8, (uint8_t)(slot & 1), 0, edit_); }
void Engine::cvPitch(int slot, float semis) { cvPitch_[slot & 1] = semis; }
void Engine::setCc(uint8_t cc, uint8_t v, int part) { push(6, cc, v, resolvePart(part)); }
void Engine::loadProgram(int i) { loadPartProgram(edit_, i); }
void Engine::loadPartProgram(int part, int i) {
  part &= 7;
  int n = banks::count();
  if (n <= 0) return;
  Program p;
  int idx = ((i % n) + n) % n;
  if (!banks::get(idx, p)) return;
  parts_[part].progIdx = idx;
  parts_[part].prog = p;
  recacheWaves();
  setupChanged();
}
void Engine::setProgram(const Program& p) {
  parts_[edit_].prog = p;
  recacheWaves();
}
void Engine::cacheCurrentWaves() { recacheWaves(); }
// The table cache holds the waves of the parts in use: part 0 alone in OMNI/POLY, all eight
// in MULTI/MONO (edit part first, so it gets the fast RAM). Refilling moves tables that
// sounding voices read, so it only happens when the set of waves changed, after a release.
void Engine::recacheWaves() {
  uint8_t waves[kParts * kOscs];
  int n = 0;
  int partCount = mode_ >= 2 ? kParts : 1;
  for (int k = 0; k < partCount; ++k) {
    int p = mode_ >= 2 ? (edit_ + k) % kParts : 0;
    for (int o = 0; o < kOscs; ++o) {
      uint8_t w = parts_[p].prog.osc[o].wave;
      bool have = false;
      for (int i = 0; i < n; ++i) if (waves[i] == w) { have = true; break; }
      if (!have) waves[n++] = w;
    }
  }
  if (n == cachedCount_ && memcmp(waves, cachedWaves_, n) == 0) return;
  push(3, 0, 0, -1);  // every part: voices still sounding read the old tables
  waveBank.cacheProgram(waves, n);
  memcpy(cachedWaves_, waves, n);
  cachedCount_ = n;
}
void Engine::setEditPart(int part) {
  part &= 7;
  if (mode_ < 2) part = 0;  // OMNI / POLY: only part 0 sounds
  if (part == edit_) return;
  edit_ = part;
  recacheWaves();
  setupChanged();
}
void Engine::setMode(int mode) {
  mode = clampi(mode, 0, 3);
  if (mode == mode_) return;
  push(3, 0, 0, -1);
  mode_ = mode;
  if (mode_ < 2) edit_ = 0;
  recacheWaves();
  setupChanged();
}
void Engine::setPartChannel(int part, int ch) {
  part &= 7;
  ch = clampi(ch, 0, 16);
  if (parts_[part].channel == ch) return;
  parts_[part].channel = (uint8_t)ch;
  setupChanged();
}

void Engine::init() {
  buildCurve(dcaAmp_, kDcaAnchors, sizeof kDcaAnchors / sizeof kDcaAnchors[0]);
  buildCurve(ampShape_, kAmpAnchors, sizeof kAmpAnchors / sizeof kAmpAnchors[0]);
  // SQ-80 filter cutoff frequencies: four exponential segments between anchor frequencies.
  {
    const float anchors[4] = {309.662f, 2231.04f, 13638.128f, 45767.85f};
    const int counts[4] = {65, 65, 65, 64};
    float hz[256];
    float start = 42.394f;
    for (int seg = 0; seg < 4; ++seg) {
      float* out = hz + 64 * seg;
      if (seg > 0) start = out[0];
      float step = logf(anchors[seg] / start) / 63.0f;
      for (int i = 0; i < counts[seg]; ++i) out[i] = 0.985f * expf(i * step) * start;
    }
    for (int c = 0; c < 256; ++c) {
      float w = hz[c] / kSampleRate;
      if (w > 0.49f) w = 0.49f;
      w *= 6.2831853f;
      float t = tanf(w / 4.0f);
      float d = t - 1.0f;
      t = 2.0f * t / (d * d);
      float p = (t - 1.0f) / (t + 1.0f);
      cutoffP_[c] = p;
      cutoffA_[c] = 1.5775966f * expf(-0.8038842f * p);
      cutoffB_[c] = -0.0152733f * expf(3.5556539f * p);
    }
    resoAmt_[0] = 0;
    for (int r = 1; r < 32; ++r) {
      float x = r / 31.0f;
      resoAmt_[r] = 1.016f * (-0.572035f * x * x + 1.6159f * x - 0.0459156f);
    }
    for (int r = 0; r < 32; ++r) resoGain_[r] = expf(sqrtf(r / 31.0f) * logf(0.33f));
    fltPole_ = powf(0.01f, 1.0f / (0.5f * 0.01794f * kSampleRate + 1.0f));
  }
  for (auto& v : voices_) for (float& s : v.src) s = 0;
  for (int p = 0; p < kParts; ++p) parts_[p].channel = (uint8_t)(p + 1);  // MULTI default: part n on channel n
  cachedCount_ = 0;
  void (*cb)() = onSetupChanged;
  onSetupChanged = nullptr;  // defaults are not worth saving
  for (int p = 0; p < kParts; ++p) loadPartProgram(p, 0);
  onSetupChanged = cb;
}

int Engine::activeVoices() const {
  int n = 0;
  for (int i = 0; i < kVoices; ++i) n += voices_[i].active;
  return n;
}

// ---- envelopes ---------------------------------------------------------------------------
// Levels in -126..126 (panel x2), segment times from the manual's chart; LV scales the levels
// by velocity, T1V shortens T1, TK shortens T2/T3 for higher keys (all in index units).
void Engine::envStage(Env& e, const EnvP& p, int stage) {
  e.stage = stage;
  float lvl = 0;
  int idx = 0;
  switch (stage) {
    case 1: lvl = p.l1 * 2.0f * e.levelScale; idx = p.t1 - e.t1vIdx; break;
    case 2: lvl = p.l2 * 2.0f * e.levelScale; idx = p.t2 - e.tkIdx; break;
    case 3: lvl = p.l3 * 2.0f * e.levelScale; idx = p.t3 - e.tkIdx; break;
    case 4: e.target = e.value; e.rate = 0; return;
    case 5: lvl = p.t4r ? e.value * 0.5f - 12.0f : 0.0f; idx = p.t4; break;
    case 6: lvl = 0; idx = 39; break;
    default: e.stage = 0; e.value = e.target = e.rate = 0; return;
  }
  e.target = lvl;
  float secs = kEnvTimeSec[clampi(idx, 0, 63)];
  float samples = secs * kSampleRate;
  e.rate = samples < 1.0f ? (e.target - e.value) : (e.target - e.value) / samples;
}

void Engine::envStart(Env& e, const EnvP& p, uint8_t vel, int note, bool restart) {
  if (restart || e.stage == 0) e.value = 0;
  float curve = p.lvExp ? velExp(vel) : velLinear(vel);
  e.levelScale = ((p.lv * curve) / 32.0f + (63 - p.lv) * 4.0f) / 256.0f;
  e.t1vIdx = (p.t1v * vel) >> 7;
  int k = 2 * (note - 36);
  e.tkIdx = (p.tk * (k < 0 ? 0 : k)) / 256;
  envStage(e, p, 1);
}

void Engine::envTick(Env& e, const EnvP& p, bool cycle, int samples) {
  if (e.stage == 0 || e.stage == 4) return;
  e.value += e.rate * samples;
  bool done = e.rate >= 0 ? e.value >= e.target : e.value <= e.target;
  if (!done) return;
  e.value = e.target;
  switch (e.stage) {
    case 1: envStage(e, p, 2); break;
    case 2: envStage(e, p, 3); break;
    case 3:
      if (e.value == 0.0f) envStage(e, p, 0);          // a zero sustain level ends the envelope
      else envStage(e, p, cycle ? 5 : 4);
      break;
    case 5: envStage(e, p, p.t4r ? 6 : 0); break;
    case 6: envStage(e, p, 0); break;
    default: break;
  }
}

// ---- LFOs --------------------------------------------------------------------------------
// FREQ 7..63 = 0.33 .. 18.6 Hz linear (0.3266 Hz per step), 0..6 linear down to 0; the delay
// ramp L1 -> L2 moves DELAY units per "EMU step" of 85 ms; HUMAN adds 0..2 FREQ units of
// jitter per EMU step; output = wave(-128..127) * (fade*2 + depth mod) / 256.
void Engine::lfoTick(Lfo& l, const LfoP& p, const float* src, int samples) {
  float dt = samples / kSampleRate;
  l.emuClock += dt;
  if (l.emuClock >= 0.0853f) {
    l.emuClock -= 0.0853f;
    if (p.delay) {
      float step = (float)p.delay;
      if (l.fade < p.l2) l.fade = fminf((float)p.l2, l.fade + step);
      else if (l.fade > p.l2) l.fade = fmaxf((float)p.l2, l.fade - step);
    }
    if (p.human) {
      int r = urand() % 3;
      l.humanOfs = (l.humanOfs + (r == 0 ? -1 : (r == 1 ? 1 : 0)));
      l.humanOfs = clampi(l.humanOfs, 0, 2);
    } else {
      l.humanOfs = 0;
    }
  }
  int f = p.freq + l.humanOfs;
  float hz = f >= 7 ? (f - 6) * 0.32653f : f * 0.04665f;
  l.phase += hz * dt;
  if (l.phase >= 1.0f) {
    l.phase -= floorf(l.phase);
    l.noise = frand() * 127.0f;
  }
  float depth = l.fade * 2.0f;
  if (p.mod < kModSrcCount) depth += 0;  // depth modulation added below with the program's amount
  depth += src[p.mod < kModSrcCount ? p.mod : M_OFF] * 63.0f / 63.0f;  // SQ-80: full source = full depth
  depth = clampf(depth, 0.0f, 255.0f);
  int b = (int)(l.phase * 256.0f) & 255;
  float w;
  switch (p.wave) {
    case LFO_SAW: w = (float)(int8_t)(b + 0x80); break;
    case LFO_SQR: w = b < 0x80 ? 127.0f : 0.0f; break;
    case LFO_NOISE: w = l.noise; break;
    default: w = (float)(((b < 0x80 ? b : 255 - b) - 0x40) * 2); break;  // TRI, -128..126
  }
  l.out = w * depth / 256.0f;
}

// ---- filter (SQ-80 4-pole low-pass) ------------------------------------------------------
void Engine::filterSet(Filter& f, int cutoff, int reso, bool immediate) {
  cutoff = clampi(cutoff, 0, 255);
  reso = clampi(reso, 0, 31);
  float p = cutoffP_[cutoff];
  float k = (cutoffA_[cutoff] + cutoffB_[cutoff]) * resoAmt_[reso];
  f.pT = p;
  f.kT = k;
  f.gainT = resoGain_[reso];
  if (immediate) {
    f.p = p;
    f.k = k;
    f.g = (p + 1.0f) * 0.5f;
    f.gain = f.gainT;
  }
}

// ---- voices ------------------------------------------------------------------------------
float Engine::modSum(const Voice& v, uint8_t s1, int8_t a1, uint8_t s2, int8_t a2) const {
  float m = 0;
  if (s1 < kModSrcCount) m += v.src[s1] * a1;
  if (s2 < kModSrcCount) m += v.src[s2] * a2;
  return m;  // source (0..127 / -126..126) x amount (-63..63)
}

Engine::Voice* Engine::allocVoice(uint8_t note, int part) {
  if (parts_[part].prog.voiceRestart)
    for (auto& v : voices_) if (v.active && v.note == note && v.part == part) return &v;
  for (auto& v : voices_) if (!v.active) return &v;
  Voice* pick = nullptr;
  float lowest = 1e9f;
  for (auto& v : voices_) if (!v.gate && fabsf(v.env[3].value) < lowest) { lowest = fabsf(v.env[3].value); pick = &v; }
  if (!pick) {
    uint32_t oldest = 0xFFFFFFFF;
    for (auto& v : voices_) if (v.age < oldest) { oldest = v.age; pick = &v; }
  }
  return pick;
}

// Wavesample lookup for the key, octave normalisation into 12..35 by moving the DOC
// resolution, base pitch in 1/256 semitone (the SQ-80 OS's pitch arithmetic).
void Engine::setupOsc(Voice& v, int o) {
  const Program& prog_ = parts_[v.part].prog;
  const OscP& op = prog_.osc[o];
  int semitone = op.oct * 12 + op.semi;
  int k = clampi(v.note + semitone, 0, 127);
  const WaveSample& ws = waveBank.pick(op.wave, k);
  v.ws[o] = &ws;
  uint8_t reg = ws.reg;
  if (prog_.am && o == 1) reg = (reg & 0x7F) | (prog_.osc[0].wave < 75 ? (waveBank.pick(prog_.osc[0].wave, clampi(v.note + prog_.osc[0].oct * 12 + prog_.osc[0].semi, 0, 127)).reg & 0x80) : 0);  // AM bug
  int s = semitone + ws.semiOfs + 12;
  if (s < 12) {
    for (int n = 0; n < 3; ++n) { reg++; s += 12; if (s >= 12) break; }
  } else if (s >= 36) {
    reg--;
    s -= 12;
  }
  uint32_t bank = ((reg & 0x40) >> 5) | (reg >> 7);
  uint32_t addr = (bank << 16) + ((uint32_t)ws.page << 8);
  uint8_t sizeCode = (reg >> 3) & 7;
  uint8_t res = reg & 7;
  bool oneShot = op.wave >= 54;
  DocOsc& d = v.osc[o];
  d.setTable(waveBank.tableBytes(addr & ~((256u << sizeCode) - 1), 256u << sizeCode), addr, sizeCode, res,
             oneShot && !(prog_.am && o == 1));
  v.oscPitch[o] = (v.note + s) * 256 + op.fine * 8 + ws.fineOfs;
}

void Engine::triggerVoice(Voice& v, uint8_t note, uint8_t vel, bool legato, int glideFrom) {
  const Program& P = parts_[v.part].prog;
  bool wasActive = v.active;
  v.note = note;
  v.gate = true;
  v.age = ++ageCounter_;
  v.active = true;
  v.tick = 0;
  if (!legato) v.vel = vel;
  // glide: constant-time portamento from the previous key (poly) or the held key (mono)
  if (P.glide && glideFrom >= 0 && glideFrom != note) {
    v.glideOfs = (float)((glideFrom - note) * 256);
    v.glideRate = fabsf(v.glideOfs) / (kEnvTimeSec[P.glide] * kSampleRate);
  } else {
    v.glideOfs = 0;
    v.glideRate = 0;
  }
  if (legato) {
    for (int o = 0; o < kOscs; ++o) setupOsc(v, o);
    return;
  }
  bool restartEnv = P.envRestart || !wasActive;
  for (int i = 0; i < 4; ++i) envStart(v.env[i], P.env[i], vel, note, restartEnv);
  for (int i = 0; i < 3; ++i) {
    Lfo& l = v.lfo[i];
    if (P.lfo[i].reset || !wasActive) l.phase = 0;
    l.fade = P.lfo[i].l1;
    l.emuClock = 0;
  }
  for (int o = 0; o < kOscs; ++o) {
    const WaveSample* before = v.ws[o];
    setupOsc(v, o);
    bool oneShot = P.osc[o].wave >= 54;
    if (P.oscRestart || !wasActive || v.osc[o].halted || oneShot || v.ws[o] != before) v.osc[o].start();
  }
  if (!wasActive) {
    v.flt = Filter{};
    v.volL = v.volR = 0;
    v.dcX1 = v.dcY1 = 0;
  }
  v.dcBlock = P.sync;
}

void Engine::startNote(uint8_t note, uint8_t vel, int cvSlot, int part) {
  Part& pt = parts_[part];
  if (pt.heldCount < 16) pt.held[pt.heldCount++] = note;
  Voice* v;
  // a mono program, or any part in MONO mode, plays on the voice with the part's number
  if (pt.prog.mono || mode_ == 3) {
    v = &voices_[part];
    bool legato = v->active && v->gate && v->part == part && pt.heldCount > 1;
    int from = legato ? v->note : -1;
    if (!legato) v->part = (uint8_t)part;
    triggerVoice(*v, note, vel, legato, from);
  } else {
    // a CV slot re-gated while its voice still sounds restarts that voice
    v = nullptr;
    if (cvSlot >= 0)
      for (auto& c : voices_) if (c.active && c.cvSlot == cvSlot) { v = &c; break; }
    if (!v) v = allocVoice(note, part);
    if (v) { v->part = (uint8_t)part; triggerVoice(*v, note, vel, false, pt.lastKey); }
  }
  if (v) v->cvSlot = (int8_t)cvSlot;
  pt.lastKey = note;
}

void Engine::releaseVoice(Voice& v) {
  const Program& P = parts_[v.part].prog;
  v.gate = false;
  if (P.cycle) return;
  for (int i = 0; i < 4; ++i)
    if (v.env[i].stage != 0 && v.env[i].stage < 5) envStage(v.env[i], P.env[i], 5);
}

void Engine::drainEvents() {
  while (qTail_ != qHead_) {
    Event e = queue_[qTail_];
    qTail_ = (qTail_ + 1) % kQueueLen;
    int part = e.part == 0xFF ? -1 : (e.part & 7);
    Part& pt = parts_[part < 0 ? 0 : part];
    switch (e.type) {
      case 1: startNote(e.a, e.b, -1, part < 0 ? edit_ : part); break;
      case 7: startNote(e.a & 0x7F, e.b, (e.a >> 7) & 1, part < 0 ? edit_ : part); break;
      case 8:
        for (auto& v : voices_) if (v.active && v.gate && v.cvSlot == (int8_t)e.a) releaseVoice(v);
        break;
      case 2: {
        for (int i = 0; i < pt.heldCount; ++i)
          if (pt.held[i] == e.a) { memmove(pt.held + i, pt.held + i + 1, pt.heldCount - i - 1); --pt.heldCount; break; }
        if (pt.prog.mono || mode_ == 3) {
          Voice& v = voices_[part < 0 ? 0 : part];
          if (v.active && v.part == (part < 0 ? 0 : part) && v.note == e.a) {
            if (pt.heldCount > 0) triggerVoice(v, pt.held[pt.heldCount - 1], v.vel, true, v.note);
            else releaseVoice(v);
          }
        } else {
          for (auto& v : voices_) if (v.active && v.gate && v.note == e.a && v.part == part) releaseVoice(v);
        }
        break;
      }
      case 3:
        for (int p = 0; p < kParts; ++p) if (part < 0 || p == part) parts_[p].heldCount = 0;
        for (auto& v : voices_) if (v.active && v.gate && (part < 0 || v.part == part)) releaseVoice(v);
        break;
      case 4: pt.ctrl[e.a] = e.b; break;
      case 5:
        for (auto& v : voices_) if (v.active && v.note == e.a && v.part == part) v.polyPress = e.b;
        break;
      case 6:
        if (e.a == 7) pt.cc7 = e.b;
        else if (e.a == 8) pt.cc8 = e.b;
        else if (e.a == 11) pt.cc11 = e.b;
        else if (e.a == 74) pt.cc74 = e.b;
        break;
      default: break;
    }
  }
}

// ---- per-block control update ------------------------------------------------------------
void Engine::controlTick(Voice& v, int n) {
  const Part& pt = parts_[v.part];
  const Program& P = pt.prog;
  const float* ctrl_ = pt.ctrl;
  const int16_t bend_ = pt.bend;
  const uint8_t cc7_ = pt.cc7, cc8_ = pt.cc8, cc11_ = pt.cc11, cc74_ = pt.cc74;
  // modulation sources (SQ-80 internal units: controllers 0..127, envelopes -126..126)
  for (int i = 0; i < 4; ++i) envTick(v.env[i], P.env[i], P.cycle, n);
  for (int i = 0; i < 4; ++i) v.src[M_ENV1 + i] = v.env[i].value;
  v.src[M_VEL] = velLinear(v.vel);
  v.src[M_VEL2] = velExp(v.vel);
  v.src[M_KYBD] = (float)v.note;
  v.src[M_KYBD2] = v.note <= 32 ? -128.0f : (v.note >= 95 ? 124.0f : (float)((v.note - 64) * 4));
  v.src[M_WHEEL] = ctrl_[M_WHEEL];
  v.src[M_PEDAL] = ctrl_[M_PEDAL];
  v.src[M_XCTRL] = ctrl_[M_XCTRL];
  v.src[M_PRESS] = fmaxf(ctrl_[M_PRESS], (float)v.polyPress);
  v.src[M_OFF] = 0;
  for (int i = 0; i < 3; ++i) {
    lfoTick(v.lfo[i], P.lfo[i], v.src, n);
    v.src[M_LFO1 + i] = v.lfo[i].out;
  }

  // glide and pitch bend, in 1/256 semitone
  if (v.glideOfs != 0.0f) {
    float step = v.glideRate * n;
    if (v.glideOfs > 0) v.glideOfs = fmaxf(0.0f, v.glideOfs - step);
    else v.glideOfs = fminf(0.0f, v.glideOfs + step);
  }
  float pitchMod = v.glideOfs + (float)bend_ * 2.0f / 32.0f;  // bend range 2 semitones
  if (v.cvSlot >= 0) pitchMod += cvPitch_[v.cvSlot & 1] * 256.0f;

  // oscillators
  for (int o = 0; o < kOscs; ++o) {
    const OscP& op = P.osc[o];
    DocOsc& d = v.osc[o];
    bool source = o == 0 && P.osc[1].enable && (P.sync || P.am);
    if (!op.enable && !source) {
      d.vol = 0;
      d.halted = true;
      continue;
    }
    float fm = modSum(v, op.fmSrc1, op.fmAmt1, op.fmSrc2, op.fmAmt2);
    int32_t pitch = v.oscPitch[o] + (int32_t)(pitchMod + fm);
    d.freq = pitchToFreq(pitch - 16);
    if (P.am && o == 1) continue;  // volume comes from OSC1's samples
    int32_t level = (int32_t)(modSum(v, op.amSrc1, op.amAmt1, op.amSrc2, op.amAmt2) * 4.0f) + ((int32_t)op.level << 9);
    if (P.am && o == 0) level = 0x7F00;
    int idx = clampi(level >> 7, 0, 255);
    float amp = dcaAmp_[idx];
    if (idx < 255) {
      float frac = (level & 0x7F) / 128.0f;
      amp += (dcaAmp_[idx + 1] - amp) * frac;
    }
    if (level <= 0) amp = 0;
    d.vol = (int16_t)(amp * 255.0f + 0.5f);
  }

  // filter: cutoff index 0..255 = FREQ*2 + (mods + key tracking + CC74) / 64
  float fmod = modSum(v, P.fcSrc1, P.fcAmt1, P.fcSrc2, P.fcAmt2);
  fmod += (float)v.note * P.kbd * 2.0f;
  fmod += ((int)cc74_ - 64) * 192.0f;
  filterSet(v.flt, P.fc * 2 + (int)(fmod / 64.0f), P.q, v.tick == 0);

  // DCA4: level index from FINAL VOL, expression and ENV4, through the DCA4 curve; pan law
  float lvl = (float)P.dca4 * 1.0320f + cc11_ * 0.5f;
  lvl = lvl * (v.env[3].value * 2.0f) / 64.0f;
  float vol = (float)cc7_ + modSum(v, M_OFF, 0, M_OFF, 0) / 64.0f;
  float levelF = lvl * vol / 127.0f;
  int li = clampi((int)levelF, 0, 255);
  float gain = ampShape_[li] * 1.22756f;
  float pan = (P.pan - 8) * 8.0f + modSum(v, P.panSrc, P.panAmt, M_OFF, 0) / 128.0f + ((int)cc8_ - 64);
  float pp = clampf(pan, -63.0f, 63.0f) / 63.0f;
  float s = sqrtf(1.0f / (2.0f * pp * pp + 2.0f));
  v.volLT = gain * (s - s * pp);
  v.volRT = gain * (s * pp + s);
  if (v.tick == 0) { v.volL = v.volLT; v.volR = v.volRT; }

  if (v.tick < 1000000) ++v.tick;
  if (!v.gate && v.env[3].stage == 0) v.active = false;
}

// ---- audio -------------------------------------------------------------------------------
void Engine::render(int16_t* out, int frames) {
  uint32_t t0 = micros();
  drainEvents();
  static float mixL[256], mixR[256];
  if (frames > 256) frames = 256;
  memset(mixL, 0, frames * sizeof(float));
  memset(mixR, 0, frames * sizeof(float));
  const float kOscScale = 1.0f / (127.0f * 255.0f);
  const float pole = fltPole_;

  for (int base = 0; base < frames; base += kBlock) {
    int n = frames - base < kBlock ? frames - base : kBlock;
    for (int vi = 0; vi < kVoices; ++vi) {
      Voice& v = voices_[vi];
      if (!v.active) continue;
      controlTick(v, n);
      if (!v.active) continue;
      const Program& P = parts_[v.part].prog;
      DocOsc& o1 = v.osc[0];
      DocOsc& o2 = v.osc[1];
      DocOsc& o3 = v.osc[2];
      Filter& f = v.flt;
      // coefficient smoothing (9 ms pole) once per block instead of per sample
      {
        const float a = 1.0f - powf(pole, (float)n);
        f.p += (f.pT - f.p) * a;
        f.k += (f.kT - f.k) * a;
        f.gain += (f.gainT - f.gain) * a;
        f.g = (1.0f + f.p) * 0.5f;
      }
      const bool osc1Mix = P.osc[0].enable && !P.am;
      const bool sync = P.sync, am = P.am, dcBlock = v.dcBlock;
      // amp ramp over this block (about 1 ms per block; the SQ-80's DCA4 ramps 2.5-10 ms)
      const float dL = (v.volLT - v.volL) / n, dR = (v.volRT - v.volR) / n;
      // filter state in registers for the block
      float s0 = f.s[0], s1 = f.s[1], s2 = f.s[2], s3 = f.s[3], s4 = f.s[4];
      const float fg = f.g, fp = f.p, fk = f.k, fgain = f.gain, tIn = fk * 1.075f + 1.0f;
      float volL = v.volL, volR = v.volR, dcX1 = v.dcX1, dcY1 = v.dcY1;
      float* mL = mixL + base;
      float* mR = mixR + base;
      for (int i = 0; i < n; ++i) {
        int32_t q1 = o1.step();
        if (sync && o1.wrapped) o2.acc = 0;
        if (am) {  // OSC1's sample sets OSC2's amplitude: 0.4 + 0.6 * s / 127
          int32_t sm = (int32_t)o1.rawByte - 128;
          o2.vol = (int16_t)(102 + ((sm * 154) >> 7));
        }
        int32_t q2 = o2.step();
        int32_t q3 = o3.step();
        float x = (float)((osc1Mix ? q1 : 0) + q2 + q3) * kOscScale;  // each oscillator -1..1
        if (dcBlock) {
          float y = x - dcX1 + 0.9975f * dcY1;
          dcX1 = x;
          dcY1 = y;
          x = y;
        }
        // SQ-80 4-pole low-pass
        const float a4 = s3 * fg - s4 * fp;
        const float a3 = s2 * fg - s3 * fp;
        const float a2 = s1 * fg - s2 * fp;
        const float a1 = s0 * fg - s1 * fp;
        float y = x * tIn - s4 * fk;
        s0 = y;
        y = y * fg + a1; s1 = y;
        y = y * fg + a2; s2 = y;
        y = y * fg + a3; s3 = y;
        y = y * fg + a4; s4 = y;
        // VCA saturation (SAT 0: drive 0.73), then DCA4 gain and pan
        float u = y * fgain * 0.73f;
        if (u > 1.41421f) u = 1.0f;
        else if (u < -1.41421f) u = -1.0f;
        else u = 1.06066f * u * (1.0f - u * u * (1.0f / 6.0f));
        volL += dL;
        volR += dR;
        mL[i] += u * volL;
        mR[i] += u * volR;
      }
      f.s[0] = s0; f.s[1] = s1; f.s[2] = s2; f.s[3] = s3; f.s[4] = s4;
      v.volL = volL; v.volR = volR; v.dcX1 = dcX1; v.dcY1 = dcY1;
    }
  }
  for (int i = 0; i < frames; ++i) {
    float l = mixL[i] * 0.8f, r = mixR[i] * 0.8f;
    if (l > 1.0f || l < -1.0f) l = l / (1.0f + fabsf(l)) * 2.0f * 0.5f + (l > 0 ? 0.5f : -0.5f);
    if (r > 1.0f || r < -1.0f) r = r / (1.0f + fabsf(r)) * 2.0f * 0.5f + (r > 0 ? 0.5f : -0.5f);
    l = clampf(l, -1.0f, 1.0f);
    r = clampf(r, -1.0f, 1.0f);
    out[2 * i] = (int16_t)(l * 32000.0f);
    out[2 * i + 1] = (int16_t)(r * 32000.0f);
  }
  blocks_ = blocks_ + 1;
  renderUs_ = micros() - t0;
}

}  // namespace sq80
