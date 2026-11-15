// GENERATED from plugins/SQ-80/src/SQ80.cpp by make_public.py. Do not edit here.
// SQ-80 — Ensoniq SQ-80 cross-wave synth for the MPC.
//
// DSP: the engine lifted from esp32-sq80/src/synth/ (8 voices x 3 DOC oscillators from the
// 256 KB wave ROM, SQ-80 4-pole low-pass, 4 envelopes, 3 LFOs, 15-source mod matrix). This
// file is the MPC-host glue: it exposes the patch fields as plugin parameters, keeps them in
// sync with the engine's program both ways, feeds MIDI in and audio out.
//
// The engine instance is per plugin (member eng_), so several SQ-80s can run at once. The
// wave ROM and factory banks are shared read-only globals (sq80::waveBank, banks::), loaded
// once. Runs the DOC emulation at the MPC's 44100 (sq80::kSampleRate); pitch/timing exact.
#include "mpc_plugin.h"
#include "mpc_resources.h"

#include "compat.hpp"
#include "engine.hpp"
#include "wavebank.hpp"
#include "banks.hpp"
#include "program.hpp"

#include <vector>
#include <string>
#include <functional>
#include <cmath>
#include <atomic>
#include <cstring>

using sq80::Program;

namespace {
constexpr int kMaxFrames = 256;   // engine renders <=256 frames per call
constexpr int kLead = 2;          // leading params before the patch table: Volume, Program

// The 75 SQ-80 wave names, as a choice list (so OSC Wave shows "BRASS", not a number).
const std::vector<std::string>& waveNames()
{
    static const std::vector<std::string> n = []{
        std::vector<std::string> v; v.reserve(sq80::kWaveCount);
        for (int i = 0; i < sq80::kWaveCount; ++i) v.push_back(sq80::kWaveNames[i]);
        return v;
    }();
    return n;
}

// The 16 mod-matrix sources (LFO1..PRESS, OFF), as a choice list for the routing params.
const std::vector<std::string>& modSrcNames()
{
    static const std::vector<std::string> n = []{
        std::vector<std::string> v; v.reserve(sq80::kModSrcCount);
        for (int i = 0; i < sq80::kModSrcCount; ++i)
            v.push_back(i == sq80::M_OFF ? "OFF" : sq80::kModSrcNames[i]);
        return v;
    }();
    return n;
}

struct PDesc {
    enum Kind { Int, Bool, Choice };
    const char* id;
    const char* name;
    Kind kind;
    int lo, hi;
    std::function<int(const Program&)>  get;
    std::function<void(Program&, int)>  set;
    std::vector<std::string> choices {};
};

std::vector<PDesc> buildTable()
{
    std::vector<PDesc> t;
    static char ids[256][12];
    static char nms[256][16];
    int s = 0;
    auto mkId = [&](const char* fmt, int a) { std::snprintf(ids[s], 12, fmt, a); return ids[s]; };
    auto mkNm = [&](const char* fmt, int a) { std::snprintf(nms[s], 16, fmt, a); return nms[s]; };

    for (int i = 0; i < 3; ++i) { int P = i + 1;
        t.push_back({mkId("o%d_oct", P),  mkNm("OSC%d Oct", P),  PDesc::Int, -3, 5,  [i](const Program& p){return p.osc[i].oct;},   [i](Program& p,int v){p.osc[i].oct=(int8_t)v;}}); ++s;
        t.push_back({mkId("o%d_semi", P), mkNm("OSC%d Semi", P), PDesc::Int, 0, 11,  [i](const Program& p){return p.osc[i].semi;},  [i](Program& p,int v){p.osc[i].semi=(uint8_t)v;}}); ++s;
        t.push_back({mkId("o%d_fine", P), mkNm("OSC%d Fine", P), PDesc::Int, 0, 31,  [i](const Program& p){return p.osc[i].fine;},  [i](Program& p,int v){p.osc[i].fine=(uint8_t)v;}}); ++s;
        t.push_back({mkId("o%d_wave", P), mkNm("OSC%d Wave", P), PDesc::Choice, 0, 74, [i](const Program& p){return p.osc[i].wave;}, [i](Program& p,int v){p.osc[i].wave=(uint8_t)v;}, waveNames()}); ++s;
        t.push_back({mkId("o%d_lvl", P),  mkNm("OSC%d Level", P),PDesc::Int, 0, 63,  [i](const Program& p){return p.osc[i].level;}, [i](Program& p,int v){p.osc[i].level=(uint8_t)v;}}); ++s;
        t.push_back({mkId("o%d_on", P),   mkNm("OSC%d On", P),   PDesc::Bool,0, 1,   [i](const Program& p){return p.osc[i].enable;},[i](Program& p,int v){p.osc[i].enable=v!=0;}}); ++s;
    }
    for (int i = 0; i < 4; ++i) { int P = i + 1;
        t.push_back({mkId("e%d_l1", P), mkNm("ENV%d L1", P), PDesc::Int, -63, 63, [i](const Program& p){return p.env[i].l1;}, [i](Program& p,int v){p.env[i].l1=(int8_t)v;}}); ++s;
        t.push_back({mkId("e%d_l2", P), mkNm("ENV%d L2", P), PDesc::Int, -63, 63, [i](const Program& p){return p.env[i].l2;}, [i](Program& p,int v){p.env[i].l2=(int8_t)v;}}); ++s;
        t.push_back({mkId("e%d_l3", P), mkNm("ENV%d L3", P), PDesc::Int, -63, 63, [i](const Program& p){return p.env[i].l3;}, [i](Program& p,int v){p.env[i].l3=(int8_t)v;}}); ++s;
        t.push_back({mkId("e%d_t1", P), mkNm("ENV%d T1", P), PDesc::Int, 0, 63,   [i](const Program& p){return p.env[i].t1;}, [i](Program& p,int v){p.env[i].t1=(uint8_t)v;}}); ++s;
        t.push_back({mkId("e%d_t2", P), mkNm("ENV%d T2", P), PDesc::Int, 0, 63,   [i](const Program& p){return p.env[i].t2;}, [i](Program& p,int v){p.env[i].t2=(uint8_t)v;}}); ++s;
        t.push_back({mkId("e%d_t3", P), mkNm("ENV%d T3", P), PDesc::Int, 0, 63,   [i](const Program& p){return p.env[i].t3;}, [i](Program& p,int v){p.env[i].t3=(uint8_t)v;}}); ++s;
        t.push_back({mkId("e%d_t4", P), mkNm("ENV%d T4", P), PDesc::Int, 0, 63,   [i](const Program& p){return p.env[i].t4;}, [i](Program& p,int v){p.env[i].t4=(uint8_t)v;}}); ++s;
    }
    for (int i = 0; i < 3; ++i) { int P = i + 1;
        t.push_back({mkId("lfo%d_rate", P),  mkNm("LFO%d Rate", P),  PDesc::Int, 0, 63, [i](const Program& p){return p.lfo[i].freq;},  [i](Program& p,int v){p.lfo[i].freq=(uint8_t)v;}}); ++s;
        t.push_back({mkId("lfo%d_wave", P),  mkNm("LFO%d Wave", P),  PDesc::Choice,0,3, [i](const Program& p){return p.lfo[i].wave;},  [i](Program& p,int v){p.lfo[i].wave=(uint8_t)v;}, {"Tri","Saw","Sqr","Noise"}}); ++s;
        t.push_back({mkId("lfo%d_depth", P), mkNm("LFO%d Depth", P), PDesc::Int, 0, 63, [i](const Program& p){return p.lfo[i].l1;},    [i](Program& p,int v){p.lfo[i].l1=(uint8_t)v;}}); ++s;
        t.push_back({mkId("lfo%d_delay", P), mkNm("LFO%d Delay", P), PDesc::Int, 0, 63, [i](const Program& p){return p.lfo[i].delay;}, [i](Program& p,int v){p.lfo[i].delay=(uint8_t)v;}}); ++s;
    }
    t.push_back({"flt_fc",  "Cutoff",    PDesc::Int, 0, 127, [](const Program& p){return p.fc;},   [](Program& p,int v){p.fc=(uint8_t)v;}}); ++s;
    t.push_back({"flt_q",   "Reso",      PDesc::Int, 0, 31,  [](const Program& p){return p.q;},    [](Program& p,int v){p.q=(uint8_t)v;}}); ++s;
    t.push_back({"flt_kbd", "Key Track", PDesc::Int, 0, 63,  [](const Program& p){return p.kbd;},  [](Program& p,int v){p.kbd=(uint8_t)v;}}); ++s;
    t.push_back({"dca4",    "Final Env", PDesc::Int, 0, 63,  [](const Program& p){return p.dca4;}, [](Program& p,int v){p.dca4=(uint8_t)v;}}); ++s;
    t.push_back({"glide",   "Glide",     PDesc::Int, 0, 63,  [](const Program& p){return p.glide;},[](Program& p,int v){p.glide=(uint8_t)v;}}); ++s;
    t.push_back({"pan",     "Pan",       PDesc::Int, 0, 15,  [](const Program& p){return p.pan;},  [](Program& p,int v){p.pan=(uint8_t)v;}}); ++s;
    t.push_back({"mono",    "Mono",      PDesc::Bool,0, 1,   [](const Program& p){return p.mono;}, [](Program& p,int v){p.mono=v!=0;}}); ++s;
    t.push_back({"sync",    "Sync",      PDesc::Bool,0, 1,   [](const Program& p){return p.sync;}, [](Program& p,int v){p.sync=v!=0;}}); ++s;
    t.push_back({"am",      "AM",        PDesc::Bool,0, 1,   [](const Program& p){return p.am;},   [](Program& p,int v){p.am=v!=0;}}); ++s;

    // ---- Mod-matrix routing (appended 2026-11-15; parameter order above is frozen) ----
    // Every destination takes a source (one of the 15 SQ-80 modulators, or OFF) and a
    // bipolar depth. Oscillator pitch, oscillator level (DCA) and the filter have two
    // routes each; pan has one; each LFO has one source that scales its depth.
    const std::vector<std::string>& src = modSrcNames();
    const int lastSrc = sq80::kModSrcCount - 1;
    auto clampSrc = [](int v) { const int hi = sq80::kModSrcCount - 1; return (uint8_t)(v < 0 ? 0 : v > hi ? hi : v); };
    for (int i = 0; i < 3; ++i) { int P = i + 1;
        // OSC pitch modulation
        t.push_back({mkId("o%d_m1s", P), mkNm("OSC%d Mod1 Src", P), PDesc::Choice, 0, lastSrc, [i](const Program& p){return p.osc[i].fmSrc1;}, [i,clampSrc](Program& p,int v){p.osc[i].fmSrc1=clampSrc(v);}, src}); ++s;
        t.push_back({mkId("o%d_m1a", P), mkNm("OSC%d Mod1 Amt", P), PDesc::Int, -63, 63,       [i](const Program& p){return p.osc[i].fmAmt1;}, [i](Program& p,int v){p.osc[i].fmAmt1=(int8_t)v;}}); ++s;
        t.push_back({mkId("o%d_m2s", P), mkNm("OSC%d Mod2 Src", P), PDesc::Choice, 0, lastSrc, [i](const Program& p){return p.osc[i].fmSrc2;}, [i,clampSrc](Program& p,int v){p.osc[i].fmSrc2=clampSrc(v);}, src}); ++s;
        t.push_back({mkId("o%d_m2a", P), mkNm("OSC%d Mod2 Amt", P), PDesc::Int, -63, 63,       [i](const Program& p){return p.osc[i].fmAmt2;}, [i](Program& p,int v){p.osc[i].fmAmt2=(int8_t)v;}}); ++s;
    }
    for (int i = 0; i < 3; ++i) { int P = i + 1;
        // DCA (oscillator level) modulation
        t.push_back({mkId("d%d_m1s", P), mkNm("DCA%d Mod1 Src", P), PDesc::Choice, 0, lastSrc, [i](const Program& p){return p.osc[i].amSrc1;}, [i,clampSrc](Program& p,int v){p.osc[i].amSrc1=clampSrc(v);}, src}); ++s;
        t.push_back({mkId("d%d_m1a", P), mkNm("DCA%d Mod1 Amt", P), PDesc::Int, -63, 63,       [i](const Program& p){return p.osc[i].amAmt1;}, [i](Program& p,int v){p.osc[i].amAmt1=(int8_t)v;}}); ++s;
        t.push_back({mkId("d%d_m2s", P), mkNm("DCA%d Mod2 Src", P), PDesc::Choice, 0, lastSrc, [i](const Program& p){return p.osc[i].amSrc2;}, [i,clampSrc](Program& p,int v){p.osc[i].amSrc2=clampSrc(v);}, src}); ++s;
        t.push_back({mkId("d%d_m2a", P), mkNm("DCA%d Mod2 Amt", P), PDesc::Int, -63, 63,       [i](const Program& p){return p.osc[i].amAmt2;}, [i](Program& p,int v){p.osc[i].amAmt2=(int8_t)v;}}); ++s;
    }
    // Filter cutoff modulation
    t.push_back({"flt_m1s", "Flt Mod1 Src", PDesc::Choice, 0, lastSrc, [](const Program& p){return p.fcSrc1;}, [clampSrc](Program& p,int v){p.fcSrc1=clampSrc(v);}, src}); ++s;
    t.push_back({"flt_m1a", "Flt Mod1 Amt", PDesc::Int, -63, 63,       [](const Program& p){return p.fcAmt1;}, [](Program& p,int v){p.fcAmt1=(int8_t)v;}}); ++s;
    t.push_back({"flt_m2s", "Flt Mod2 Src", PDesc::Choice, 0, lastSrc, [](const Program& p){return p.fcSrc2;}, [clampSrc](Program& p,int v){p.fcSrc2=clampSrc(v);}, src}); ++s;
    t.push_back({"flt_m2a", "Flt Mod2 Amt", PDesc::Int, -63, 63,       [](const Program& p){return p.fcAmt2;}, [](Program& p,int v){p.fcAmt2=(int8_t)v;}}); ++s;
    // Pan modulation
    t.push_back({"pan_ms",  "Pan Mod Src",  PDesc::Choice, 0, lastSrc, [](const Program& p){return p.panSrc;}, [clampSrc](Program& p,int v){p.panSrc=clampSrc(v);}, src}); ++s;
    t.push_back({"pan_ma",  "Pan Mod Amt",  PDesc::Int, -63, 63,       [](const Program& p){return p.panAmt;}, [](Program& p,int v){p.panAmt=(int8_t)v;}}); ++s;
    // LFO depth modulation source
    for (int i = 0; i < 3; ++i) { int P = i + 1;
        t.push_back({mkId("lfo%d_ms", P), mkNm("LFO%d Mod Src", P), PDesc::Choice, 0, lastSrc, [i](const Program& p){return p.lfo[i].mod;}, [i,clampSrc](Program& p,int v){p.lfo[i].mod=clampSrc(v);}, src}); ++s;
    }

    // ---- Remaining patch fields (appended 2026-11-15, after the routing block) ----
    // Envelope velocity / keyboard scaling and second release
    for (int i = 0; i < 4; ++i) { int P = i + 1;
        t.push_back({mkId("e%d_lv", P),  mkNm("ENV%d Vel Lvl", P),   PDesc::Int, 0, 63, [i](const Program& p){return p.env[i].lv;},    [i](Program& p,int v){p.env[i].lv=(uint8_t)v;}}); ++s;
        t.push_back({mkId("e%d_lvx", P), mkNm("ENV%d Vel Curve", P), PDesc::Choice, 0, 1, [i](const Program& p){return p.env[i].lvExp ? 1 : 0;}, [i](Program& p,int v){p.env[i].lvExp=v!=0;}, {"Lin","Exp"}}); ++s;
        t.push_back({mkId("e%d_t1v", P), mkNm("ENV%d Vel Atk", P),   PDesc::Int, 0, 63, [i](const Program& p){return p.env[i].t1v;},   [i](Program& p,int v){p.env[i].t1v=(uint8_t)v;}}); ++s;
        t.push_back({mkId("e%d_tk", P),  mkNm("ENV%d Key Trk", P),   PDesc::Int, 0, 63, [i](const Program& p){return p.env[i].tk;},    [i](Program& p,int v){p.env[i].tk=(uint8_t)v;}}); ++s;
        t.push_back({mkId("e%d_t4r", P), mkNm("ENV%d 2nd Rel", P),   PDesc::Bool, 0, 1, [i](const Program& p){return p.env[i].t4r;},   [i](Program& p,int v){p.env[i].t4r=v!=0;}}); ++s;
    }
    // LFO final level (after the delay), key-on reset, humanize
    for (int i = 0; i < 3; ++i) { int P = i + 1;
        t.push_back({mkId("lfo%d_l2", P),  mkNm("LFO%d L2", P),    PDesc::Int, 0, 63, [i](const Program& p){return p.lfo[i].l2;},    [i](Program& p,int v){p.lfo[i].l2=(uint8_t)v;}}); ++s;
        t.push_back({mkId("lfo%d_rst", P), mkNm("LFO%d Reset", P), PDesc::Bool, 0, 1, [i](const Program& p){return p.lfo[i].reset;}, [i](Program& p,int v){p.lfo[i].reset=v!=0;}}); ++s;
        t.push_back({mkId("lfo%d_hum", P), mkNm("LFO%d Human", P), PDesc::Bool, 0, 1, [i](const Program& p){return p.lfo[i].human;}, [i](Program& p,int v){p.lfo[i].human=v!=0;}}); ++s;
    }
    // Voice behaviour switches
    t.push_back({"v_rst", "Voice Restart", PDesc::Bool, 0, 1, [](const Program& p){return p.voiceRestart;}, [](Program& p,int v){p.voiceRestart=v!=0;}}); ++s;
    t.push_back({"e_rst", "Env Restart",   PDesc::Bool, 0, 1, [](const Program& p){return p.envRestart;},   [](Program& p,int v){p.envRestart=v!=0;}}); ++s;
    t.push_back({"o_rst", "Osc Restart",   PDesc::Bool, 0, 1, [](const Program& p){return p.oscRestart;},   [](Program& p,int v){p.oscRestart=v!=0;}}); ++s;
    t.push_back({"cycle", "Env Cycle",     PDesc::Bool, 0, 1, [](const Program& p){return p.cycle;},        [](Program& p,int v){p.cycle=v!=0;}}); ++s;
    return t;
}

// Shared read-only ROM: load the wave bank and the factory/user banks exactly once,
// however many plugin instances exist.
void ensureRomLoaded()
{
    static std::atomic<bool> done{false};
    static std::atomic<bool> busy{false};
    if (done.load()) return;
    bool expected = false;
    if (busy.compare_exchange_strong(expected, true))
    {
        if (!done.load())
        {
            sq80compat::setContentDir(mpc::contentFolder());
            sq80::waveBank.init();
            banks::init();
            done.store(true);
        }
        busy.store(false);
    }
    else { while (!done.load()) {} }   // another instance is loading; wait
}
}  // namespace

class SQ80 : public mpc::Plugin
{
public:
    SQ80() : mpc::Plugin(makeInfo()), table_(buildTable())
    {
        ensureRomLoaded();                       // wave ROM + factory/user banks (shared, once)
        routingStart_ = table_.size();
        for (size_t k = 0; k < table_.size(); ++k)
            if (std::strcmp(table_[k].id, "o1_m1s") == 0) { routingStart_ = k; break; }
        extrasStart_ = table_.size();
        for (size_t k = 0; k < table_.size(); ++k)
            if (std::strcmp(table_[k].id, "e1_lv") == 0) { extrasStart_ = k; break; }
        const int nprog = banks::count();        // presets + bank D + bank C + user bank

        addParam(mpc::floatParam("volume",  "Volume",  -40.0f, 6.0f, -6.0f, "dB", 1));
        addParam(mpc::intParam  ("program", "Program", 0, nprog > 0 ? nprog - 1 : 255, 0));
        const Program& init = sq80::preset(0);
        for (const PDesc& d : table_)
        {
            int def = d.get(init);
            switch (d.kind)
            {
                case PDesc::Int:    addParam(mpc::intParam(d.id, d.name, d.lo, d.hi, def)); break;
                case PDesc::Bool:   addParam(mpc::boolParam(d.id, d.name, def != 0)); break;
                case PDesc::Choice: addParam(mpc::choiceParam(d.id, d.name, d.choices, def)); break;
            }
        }

        // Import every factory/user patch as an MPC program, so they appear by name in the
        // MPC preset browser ("P1 BASIC", "D07 HUSH", "C23 ...", "U01 ...").
        sq80::Program bp;
        char label[40];
        for (int idx = 0; idx < nprog; ++idx)
        {
            if (!banks::get(idx, bp)) continue;
            banks::label(idx, label, sizeof label);
            std::vector<float> pv;
            pv.reserve((size_t)numParams());
            pv.push_back(-6.0f);           // volume
            pv.push_back((float)idx);      // program selector
            for (const PDesc& d : table_) pv.push_back((float)d.get(bp));
            addProgram(label, std::move(pv));
        }
    }

    static mpc::PluginInfo makeInfo()
    {
        mpc::PluginInfo i;
        i.name = "ESQUE80"; i.version = 900; i.vendor = "Lichthaus";
        i.uniqueId = MPC_FOURCC('L','s','E','8');
        i.isInstrument = true; i.numInputs = 0; i.numOutputs = 2;
        return i;
    }

    void prepare(double sampleRate, int maxBlockSize) override
    {
        if (!started_)
        {
            ensureRomLoaded();
            eng_.init();
            started_ = true;
            applyAllParamsToProgram();
        }
        (void)maxBlockSize;
        double outSR = sampleRate > 0.0 ? sampleRate : 44100.0;
        ratio_ = (double)sq80::kSampleRate / outSR;   // engine input samples per output sample
        {
            const float fc = 16000.0f, Q = 0.70710678f;            // Butterworth reconstruction LP
            float w0 = 6.2831853f * fc / (float)outSR;
            float cw = std::cos(w0), sw = std::sin(w0), al = sw / (2.0f * Q);
            float a0 = 1.0f + al;
            ab0_ = ((1.0f - cw) * 0.5f) / a0; ab1_ = (1.0f - cw) / a0; ab2_ = ab0_;
            aa1_ = (-2.0f * cw) / a0;         aa2_ = (1.0f - al) / a0;
        }
        engBuf_.assign((size_t)kMaxFrames * 2, 0);
        resetResampler();
    }

    void reset() override { resetResampler(); }

    void resetResampler()
    {
        phase_ = 0.0; primed_ = false; engPos_ = 0; engLen_ = 0;
        for (int k = 0; k < 4; ++k) { hL_[k] = 0.0f; hR_[k] = 0.0f; }
        az1L_ = az2L_ = az1R_ = az2R_ = 0.0f;
    }

    // One engine sample pair (at 38461 Hz), rendered on demand in blocks.
    inline void nextInput(float& l, float& r)
    {
        if (engPos_ >= engLen_) { int n = kMaxFrames; eng_.render(engBuf_.data(), n); engLen_ = n; engPos_ = 0; }
        l = engBuf_[2 * engPos_]     * (1.0f / 32768.0f);
        r = engBuf_[2 * engPos_ + 1] * (1.0f / 32768.0f);
        ++engPos_;
    }

    // 4-point cubic (Catmull-Rom) interpolation at t in [0,1) between p1 and p2.
    static inline float cubic(float p0, float p1, float p2, float p3, float t)
    {
        float a = p3 - p2 - p0 + p1, b = p0 - p1 - a, c = p2 - p0, d = p1;
        return ((a * t + b) * t + c) * t + d;
    }

    // SQ-80 analog output stage: a gentle VCA-style soft-clip (analog warmth) followed by a
    // 2-pole reconstruction low-pass (~16 kHz), which tames the raw DOC's harsh top end so the
    // timbre sits closer to the real instrument and Arturia's model. Fixed, no parameter.
    static constexpr float kDrive = 1.25f, kInvDrive = 0.8f;
    inline float analog(float x, float& z1, float& z2)
    {
        float xd = x * kDrive;
        if (xd > 1.5f) xd = 1.5f; else if (xd < -1.5f) xd = -1.5f;
        float s = (xd - 0.33333334f * xd * xd * xd) * kInvDrive;
        s += 1.0e-15f;   // anti-denormal DC floor (inaudible, keeps the LP state out of denormals)
        float y = ab0_ * s + z1;
        z1 = ab1_ * s - aa1_ * y + z2;
        z2 = ab2_ * s - aa2_ * y;
        return y;
    }

    void parameterChanged(int index, float plainValue) override
    {
        if (!started_) return;   // program not ready until prepare()
        int k = index - kLead;
        if (k >= 0 && k < (int)table_.size())
            table_[(size_t)k].set(eng_.program(), (int)std::lround(plainValue));
    }

    // Extra state: int lastProgram, int layout version.
    // Version 2 = routing params exist; version 3 = the remaining patch fields exist too.
    void saveExtraState(std::vector<uint8_t>& out) override
    {
        const int v[2] = { lastProgram_, kStateVersion };
        out.resize(sizeof v);
        std::memcpy(out.data(), v, sizeof v);
    }
    void loadExtraState(const uint8_t* data, size_t size) override
    {
        if (size >= sizeof(int)) std::memcpy(&lastProgram_, data, sizeof(int));
        int version = 1;
        if (size >= 2 * sizeof(int)) std::memcpy(&version, data + sizeof(int), sizeof(int));
        // A project saved before some params existed has no values for them, so they would
        // sit at the first preset's values. Fill them from the patch that project used.
        if (version < kStateVersion)
        {
            const size_t first = version < 2 ? routingStart_ : extrasStart_;
            sq80::Program bp;
            if (lastProgram_ >= 0 && banks::get(lastProgram_, bp))
                for (size_t k = first; k < table_.size(); ++k)
                    set((int)(k + kLead), (float)table_[k].get(bp));
        }
    }

    void process(mpc::ProcessContext& ctx) override
    {
        mpc::ScopedDenormalsOff denormGuard;   // flush denormals from the analog LP feedback
        const float gain = std::pow(10.0f, get(0) / 20.0f);

        int prog = (int)std::lround(get(1));
        if (prog != lastProgram_)
        {
            eng_.loadProgram(prog);
            lastProgram_ = prog;
            const Program& p = eng_.program();
            for (size_t k = 0; k < table_.size(); ++k)
                set((int)(k + kLead), (float)table_[k].get(p));
        }

        for (int m = 0; m < ctx.numMidiEvents; ++m)
        {
            const mpc::MidiEvent& e = ctx.midi[m];
            const uint8_t st = (uint8_t)(e.data[0] & 0xF0);
            if (e.isNoteOn())            eng_.noteOn(e.data[1], e.data[2]);
            else if (e.isNoteOff())      eng_.noteOff(e.data[1]);
            else if (st == 0xE0)         eng_.setBend((int16_t)(((e.data[2] << 7) | e.data[1]) - 8192));
            else if (st == 0xB0 && e.data[1] == 1)  eng_.setController(sq80::M_WHEEL, e.data[2]);
            else if (st == 0xD0)         eng_.setController(sq80::M_PRESS, e.data[1]);
        }

        const int numFrames = ctx.numFrames;
        float* outL = ctx.outputs[0];
        float* outR = ctx.numOutputs > 1 ? ctx.outputs[1] : nullptr;

        if (!primed_)   // fill the 4-sample interpolation window
        {
            for (int k = 0; k < 4; ++k) { float l, r; nextInput(l, r); hL_[k] = l; hR_[k] = r; }
            primed_ = true;
        }
        for (int i = 0; i < numFrames; ++i)
        {
            const float t = (float)phase_;
            float l = cubic(hL_[0], hL_[1], hL_[2], hL_[3], t) * gain;
            float r = cubic(hR_[0], hR_[1], hR_[2], hR_[3], t) * gain;
            outL[i] = analog(l, az1L_, az2L_);
            if (outR) outR[i] = analog(r, az1R_, az2R_);
            phase_ += ratio_;
            while (phase_ >= 1.0)
            {
                phase_ -= 1.0;
                hL_[0]=hL_[1]; hL_[1]=hL_[2]; hL_[2]=hL_[3];
                hR_[0]=hR_[1]; hR_[1]=hR_[2]; hR_[2]=hR_[3];
                float nl, nr; nextInput(nl, nr); hL_[3] = nl; hR_[3] = nr;
            }
        }
    }

private:
    void applyAllParamsToProgram()
    {
        Program& p = eng_.program();
        for (size_t k = 0; k < table_.size(); ++k)
            table_[k].set(p, (int)std::lround(get((int)(k + kLead))));
    }

    static constexpr int kStateVersion = 3;

    sq80::Engine eng_;
    std::vector<PDesc> table_;
    size_t routingStart_ = 0;     // first mod-routing entry in table_ (set in the constructor)
    size_t extrasStart_ = 0;      // first "remaining patch field" entry in table_
    bool started_ = false;
    int  lastProgram_ = -1;

    // Resampler: the engine runs at 38461 Hz (sq80::kSampleRate); we cubic-resample to the
    // host rate so the aliasing/timbre matches the real SQ-80 and the esp32 build.
    std::vector<int16_t> engBuf_;
    int    engPos_ = 0, engLen_ = 0;
    double ratio_ = 38461.5384615 / 44100.0;
    double phase_ = 0.0;
    bool   primed_ = false;
    float  hL_[4] = {0,0,0,0}, hR_[4] = {0,0,0,0};

    // Analog output stage (2-pole reconstruction LP) biquad coeffs + per-channel state.
    float  ab0_ = 0, ab1_ = 0, ab2_ = 0, aa1_ = 0, aa2_ = 0;
    float  az1L_ = 0, az2L_ = 0, az1R_ = 0, az2R_ = 0;
};

MPC_PLUGIN_ENTRY(SQ80)
