// FeMParams.h — the plugin's parameter table, shared by the DSP and UI.
//
// Voice: one FM6Op engine → LPF (filter ADSR + channel pressure on cutoff) → chorus (shared LFO)
// → master level. Pitch bend reaches the engine. The operator envelopes shape the amplitude.
//
// The engine's operator blocks are laid out identically, so operator o's item j is fmOpParam(o, j)
// (see the helpers below). Ranges and defaults mirror the module constants in pm_param_data.cpp
// unless noted.
//
// APPEND-ONLY: DPF exposes each param's index as its host-facing id, so saved sessions and
// automation key off these positions. New params (macros, templates, … — see docs/ROADMAP.md)
// go immediately before kParamCount; never reorder or remove existing ones.

#ifndef FEM_PARAMS_H
#define FEM_PARAMS_H

#include <cstdint>

namespace fem {

// Offsets within the FM engine block.
enum FmEngineOffset {
   kFmAlgorithm = 0,  // 0..31 operator routing (DX7 algorithms 1..32)
   kFmFeedback,       // feedback amount on the algorithm's feedback operator, 0..1
   kFmPitch,          // coarse transpose, semitones
   kFmFine,           // fine tune, cents
   kFmDetune,         // L/R stereo detune, 0..1
   kFmOversample,     // "Quality": 0 Normal (off), 1 High (2x), 2 Best (4x) — shown under Global
   kFmOpBase          // first operator item; operators follow, kFmOpCount items each
};
// Offsets within one operator (6 per engine).
enum FmOpOffset {
   kOpWave = 0,       // 0 Sine, 1 1+2, 2 1+3, 3 1+3+5, 4 1-4 (harmonic stacks)
   kOpRatio,          // frequency ratio to the note
   kOpFine,           // cents
   kOpLevel,          // output (carrier) / modulation index (modulator), 0..1
   kOpDelay,          // envelope: delay, attack, decay, sustain, release
   kOpAttack,
   kOpDecay,
   kOpSustain,
   kOpRelease,
   kFmOpCount
};
constexpr uint32_t kFmOperators        = 6;
constexpr uint32_t kFmEngineParamCount = kFmOpBase + kFmOperators * kFmOpCount; // 60

#define FEM_FM_OP_PARAMS(O) \
   kParamOp##O##Wave, kParamOp##O##Ratio, kParamOp##O##Fine, kParamOp##O##Level, \
   kParamOp##O##Delay, kParamOp##O##Attack, kParamOp##O##Decay, \
   kParamOp##O##Sustain, kParamOp##O##Release

enum ParamIndex {
   // Global
   kParamVoiceMode = 0,  // 0 = Mono (legato), 1 = 10-voice poly
   kParamBendRange,      // pitch-bend range in quarter-tones, 0 (off) .. 8 (4 semitones)
   kParamMasterLevel,
   // FM engine
   kParamAlgorithm, kParamFeedback, kParamPitch, kParamFine, kParamDetune, kParamOversample,
   FEM_FM_OP_PARAMS(1), FEM_FM_OP_PARAMS(2), FEM_FM_OP_PARAMS(3),
   FEM_FM_OP_PARAMS(4), FEM_FM_OP_PARAMS(5), FEM_FM_OP_PARAMS(6),
   // Low-pass filter
   kParamLpfCutoff,      // octaves re 440 Hz
   kParamLpfResonance,
   kParamLpfEnvAmount,   // filter-envelope depth, octaves
   kParamLpfPressure,    // channel-pressure depth, octaves
   kParamFiltAttack,
   kParamFiltDecay,
   kParamFiltSustain,
   kParamFiltRelease,
   // Chorus
   kParamChorusMix,
   kParamChorusRate,     // shared LFO rate, octaves re 1 Hz
   kParamChorusDepth,    // delay-mod depth (ms per unit LFO)
   kParamChorusDelay,    // base delay, ms
   kParamChorusFeedback,
   // Response (appended)
   kParamVelFloor,       // velocity → level y-intercept: 0% / 25% / 50% / 75% / 100% (fixed)
   kParamVelCurve,       // velocity → level shape: Concave / Linear / Convex
   // (append new params here)
   kParamCount
};

#undef FEM_FM_OP_PARAMS

static_assert(kParamLpfCutoff - kParamAlgorithm == kFmEngineParamCount, "engine block size");
static_assert(kParamOp2Wave - kParamOp1Wave == kFmOpCount, "operator block size");

// Engine-level param at offset k (kFmAlgorithm .. kFmOversample, or an operator item).
constexpr uint32_t fmParam(uint32_t offset)
{
   return kParamAlgorithm + offset;
}
// Operator 0..5 (Op 1..6).
constexpr uint32_t fmOpParam(uint32_t op, uint32_t item)
{
   return fmParam(kFmOpBase + op * kFmOpCount + item);
}

struct ParamSpec {
   const char* name;
   const char* symbol;
   const char* unit;
   float       min;
   float       max;
   float       def;
   bool        logarithmic;
   bool        integer;
};

#define FEM_FM_OP_SPECS(O, LVL) \
   {"Op" #O " Wave",    "op" #O "wave", "",   0.0f,   4.0f,  0.0f,   false, true }, \
   {"Op" #O " Ratio",   "op" #O "rat",  "",   0.5f,  16.0f,  1.0f,   true,  false}, \
   {"Op" #O " Fine",    "op" #O "fine", "ct", -100.0f, 100.0f, 0.0f, false, false}, \
   {"Op" #O " Level",   "op" #O "lvl",  "",   0.0f,   1.0f,  LVL,    false, false}, \
   {"Op" #O " Delay",   "op" #O "dly",  "s",  0.0f,   5.0f,  0.0f,   false, false}, \
   {"Op" #O " Attack",  "op" #O "att",  "s",  0.001f, 20.0f, 0.005f, true,  false}, \
   {"Op" #O " Decay",   "op" #O "dec",  "s",  0.001f, 30.0f, 0.3f,   true,  false}, \
   {"Op" #O " Sustain", "op" #O "sus",  "",   0.0f,   1.0f,  0.7f,   false, false}, \
   {"Op" #O " Release", "op" #O "rel",  "s",  0.001f, 30.0f, 0.3f,   true,  false}

static constexpr ParamSpec kParams[kParamCount] = {
   // Global
   {"Voices",       "voices",   "",   0.0f,   1.0f,   1.0f,  false, true },
   {"Bend Range",   "bend",     "",   0.0f,   8.0f,   4.0f,  false, true },
   {"Master",       "master",   "",   0.0f,   1.0f,   0.7f,  false, false},
   // FM engine. Init voice: algorithm 1, Op2 gently modulating Op1; the rest silent.
   {"Algorithm",    "algo",     "",   0.0f,  31.0f,   0.0f,  false, true },
   {"Feedback",     "fb",       "",   0.0f,   1.0f,   0.0f,  false, false},
   {"Pitch",        "pitch",    "st", -24.0f, 24.0f,  0.0f,  false, true },
   {"Fine",         "fine",     "ct", -100.0f, 100.0f, 0.0f, false, false},
   {"Detune",       "detune",   "",   0.0f,   1.0f,   0.1f,  false, false},
   {"Quality",      "ovs",      "",   0.0f,   2.0f,   0.0f,  false, true },
   FEM_FM_OP_SPECS(1, 0.9f), FEM_FM_OP_SPECS(2, 0.35f), FEM_FM_OP_SPECS(3, 0.0f),
   FEM_FM_OP_SPECS(4, 0.0f), FEM_FM_OP_SPECS(5, 0.0f),  FEM_FM_OP_SPECS(6, 0.0f),
   // Low-pass filter (defaults fully open and unmodulated: the FM tone passes untouched)
   {"Cutoff",       "cutoff",   "oct", -6.0f,  5.4f,  5.4f,  false, false},
   {"Resonance",    "reso",     "",   1.0f,  20.0f,   1.0f,  false, false},
   {"Env Amount",   "fenvamt",  "oct", -8.0f,  8.0f,  0.0f,  false, false},
   {"Pressure",     "pressure", "oct", -8.0f,  8.0f,  0.0f,  false, false},
   {"F Attack",     "fatt",     "s",  0.001f, 10.0f,  0.005f, true, false},
   {"F Decay",      "fdec",     "s",  0.001f, 10.0f,  0.5f,  true,  false},
   {"F Sustain",    "fsus",     "",   0.0f,   1.0f,   0.4f,  false, false},
   {"F Release",    "frel",     "s",  0.001f, 10.0f,  0.5f,  true,  false},
   // Chorus
   {"Mix",          "chmix",    "",   0.0f,   1.0f,   0.3f,  false, false},
   {"Rate",         "chrate",   "",  -5.0f,   3.0f,  -1.0f,  false, false},
   {"Depth",        "chdepth",  "ms", 0.0f,   1.0f,   0.5f,  false, false},
   {"Delay",        "chdelay",  "ms", 5.0f,  30.0f,  10.0f,  false, false},
   {"Feedback",     "chfb",     "",   0.0f,   0.99f,  0.2f,  false, false},
   // Response (defaults = full, linear velocity response: TwinFM's behaviour)
   {"Velocity Floor", "velfloor", "", 0.0f,   4.0f,   0.0f,  false, true },
   {"Velocity Curve", "velcurve", "", 0.0f,   2.0f,   1.0f,  false, true },
};

#undef FEM_FM_OP_SPECS

} // namespace fem

#endif // FEM_PARAMS_H
