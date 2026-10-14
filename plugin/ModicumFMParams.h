// ModicumFMParams.h — the plugin's parameter table, shared by the DSP and UI.
//
// Voice: one FM6Op engine → LPF (filter ADSR + channel pressure on cutoff) → chorus (shared LFO)
// → master level. Pitch bend reaches the engine. The operator envelopes shape the amplitude.
//
// The engine's operator blocks are laid out identically, so operator o's item j is fmOpParam(o, j)
// (see the helpers below). Ranges and defaults mirror the module constants in pm_param_data.cpp
// unless noted.
//
// APPEND-ONLY: each param's index is its host-facing CLAP id (as it was under DPF), so saved sessions and
// automation key off these positions. New params (macros, templates, … — see docs/ROADMAP.md)
// go immediately before kParamCount; never reorder or remove existing ones.

#ifndef MODICUMFM_PARAMS_H
#define MODICUMFM_PARAMS_H

#include <cstdint>

namespace modicumfm {

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

#define MODICUMFM_FM_OP_PARAMS(O) \
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
   MODICUMFM_FM_OP_PARAMS(1), MODICUMFM_FM_OP_PARAMS(2), MODICUMFM_FM_OP_PARAMS(3),
   MODICUMFM_FM_OP_PARAMS(4), MODICUMFM_FM_OP_PARAMS(5), MODICUMFM_FM_OP_PARAMS(6),
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
   // Character: role-based macros (appended; each neutral at its default)
   kParamBrightness,     // -1..1  modulator levels
   kParamBite,           //  0..1  modulator envelopes faster / lower
   kParamGrit,           //  0..1  feedback + top modulator
   kParamBody,           // -1..1  carrier tilt, low (+) vs high (-) ratio
   // Response (appended; neutral at default)
   kParamTouch,          //  0..1  velocity → modulator levels (brightness)
   kParamKeyTrack,       // -1..1  note → modulator levels + decay/release times
   // Shared (relative) envelopes — Voice tab. Times ×0.01..×1000 (1 = unchanged), sustain -1..1 offset.
   kParamLoudAttack, kParamLoudDecay, kParamLoudSustain, kParamLoudRelease,         // carriers
   kParamBrightAttack, kParamBrightDecay, kParamBrightSustain, kParamBrightRelease, // modulators
   // (append new params here)
   kParamCount
};

#undef MODICUMFM_FM_OP_PARAMS

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

#define MODICUMFM_FM_OP_SPECS(O, LVL) \
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
   MODICUMFM_FM_OP_SPECS(1, 0.9f), MODICUMFM_FM_OP_SPECS(2, 0.35f), MODICUMFM_FM_OP_SPECS(3, 0.0f),
   MODICUMFM_FM_OP_SPECS(4, 0.0f), MODICUMFM_FM_OP_SPECS(5, 0.0f),  MODICUMFM_FM_OP_SPECS(6, 0.0f),
   // Low-pass filter (defaults fully open and unmodulated: the FM tone passes untouched)
   {"Cutoff",       "cutoff",   "oct", -6.0f,  5.4f,  5.4f,  false, false},
   {"Resonance",    "reso",     "",   1.0f,  20.0f,   1.0f,  false, false},
   {"Env Amount",   "fenvamt",  "oct", -12.0f, 12.0f, 0.0f,  false, false}, // ±12 oct: closed → fully open from any cutoff
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
   // Character macros (neutral defaults leave the sound unchanged)
   {"Brightness",   "bright",   "",  -1.0f,   1.0f,   0.0f,  false, false},
   {"Bite",         "bite",     "",   0.0f,   1.0f,   0.0f,  false, false},
   {"Grit",         "grit",     "",   0.0f,   1.0f,   0.0f,  false, false},
   {"Body",         "body",     "",  -1.0f,   1.0f,   0.0f,  false, false},
   // Response
   {"Touch",        "touch",    "",   0.0f,   1.0f,   0.0f,  false, false},
   {"Key Track",    "keytrack", "",  -1.0f,   1.0f,   0.0f,  false, false},
   // Shared envelopes (neutral: ×1 times, 0 sustain offset)
   {"Loud Attack",    "ldatt",  "x",  0.01f, 1000.0f,   1.0f,  true,  false},
   {"Loud Decay",     "lddec",  "x",  0.01f, 1000.0f,   1.0f,  true,  false},
   {"Loud Sustain",   "ldsus",  "",  -1.0f,   1.0f,   0.0f,  false, false},
   {"Loud Release",   "ldrel",  "x",  0.01f, 1000.0f,   1.0f,  true,  false},
   {"Bright Attack",  "bratt",  "x",  0.01f, 1000.0f,   1.0f,  true,  false},
   {"Bright Decay",   "brdec",  "x",  0.01f, 1000.0f,   1.0f,  true,  false},
   {"Bright Sustain", "brsus",  "",  -1.0f,   1.0f,   0.0f,  false, false},
   {"Bright Release", "brrel",  "x",  0.01f, 1000.0f,   1.0f,  true,  false},
};

#undef MODICUMFM_FM_OP_SPECS

// --- dropdowns -------------------------------------------------------------------------------
// One source of truth for "is this parameter stepped-with-labels, and what are the labels": the
// CLAP params extension (CLAP_PARAM_IS_ENUM + value/text conversion) and the UI dropdowns both read
// it from here, so a label can never drift between the host's generic panel and our own.

static constexpr const char* kVoiceModeChoices[] = { "Mono", "Poly", nullptr };
static constexpr const char* kBendRangeChoices[] = { "Off", "0.5 st", "1 st", "1.5 st", "2 st",
                                                     "2.5 st", "3 st", "3.5 st", "4 st", nullptr };
static constexpr const char* kVelFloorChoices[]  = { "0%", "25%", "50%", "75%", "Fixed", nullptr }; // level of the softest note
static constexpr const char* kVelCurveChoices[]  = { "Concave", "Linear", "Convex", nullptr };
static constexpr const char* kQualityChoices[]   = { "Normal", "High", "Best", nullptr };            // oversampling Off / 2x / 4x
static constexpr const char* kOpWaveChoices[]    = { "Sine", "1+2", "1+3", "1+3+5", "1-4", nullptr }; // harmonic stacks

// Operator o's Wave param, for any o.
constexpr bool isOpWaveParam(uint32_t index)
{
   return index >= kParamOp1Wave && index < kParamOp1Wave + kFmOperators * kFmOpCount
       && (index - kParamOp1Wave) % kFmOpCount == kOpWave;
}

// Null-terminated label list for a dropdown parameter, or nullptr when it is a plain value.
inline const char* const* choicesFor(uint32_t index)
{
   if (isOpWaveParam(index)) return kOpWaveChoices;
   switch (index) {
   case kParamVoiceMode:  return kVoiceModeChoices;
   case kParamBendRange:  return kBendRangeChoices;
   case kParamVelFloor:   return kVelFloorChoices;
   case kParamVelCurve:   return kVelCurveChoices;
   case kParamOversample: return kQualityChoices;
   default:               return nullptr;
   }
}

inline bool isDropdownParam(uint32_t index) { return choicesFor(index) != nullptr; }

} // namespace modicumfm

#endif // MODICUMFM_PARAMS_H
