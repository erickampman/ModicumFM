// TwinFMParams.h — the plugin's parameter table, shared by the DSP and UI.
//
// Voice: two FM4Op engines (A, B) → stereo mixer → LPF (filter ADSR + channel pressure on
// cutoff) → VCA (amp ADSR) → chorus (shared LFO) → master level. Pitch bend reaches both engines.
//
// The two engine blocks are laid out identically, so engine e's param with offset k is
// fmParam(e, k), and operator o's item j is fmOpParam(e, o, j) (see the helpers below). Ranges
// and defaults mirror the module constants in pm_param_data.cpp unless noted.

#ifndef TWINFM_PARAMS_H
#define TWINFM_PARAMS_H

#include <cstdint>

namespace twinfm {

// Offsets within one FM engine block.
enum FmEngineOffset {
   kFmAlgorithm = 0,  // 0..7 operator routing
   kFmFeedback,       // top-operator feedback, 0..1
   kFmPitch,          // coarse transpose, semitones
   kFmFine,           // fine tune, cents
   kFmDetune,         // L/R stereo detune, 0..1
   kFmOversample,     // 0 Off, 1 2x, 2 4x
   kFmOpBase          // first operator item; operators follow, kFmOpCount items each
};
// Offsets within one operator (4 per engine).
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
constexpr uint32_t kFmOperators        = 4;
constexpr uint32_t kFmEngineParamCount = kFmOpBase + kFmOperators * kFmOpCount; // 42

#define TWINFM_FM_OP_PARAMS(E, O) \
   kParam##E##Op##O##Wave, kParam##E##Op##O##Ratio, kParam##E##Op##O##Fine, kParam##E##Op##O##Level, \
   kParam##E##Op##O##Delay, kParam##E##Op##O##Attack, kParam##E##Op##O##Decay, \
   kParam##E##Op##O##Sustain, kParam##E##Op##O##Release
#define TWINFM_FM_ENGINE_PARAMS(E) \
   kParam##E##Algorithm, kParam##E##Feedback, kParam##E##Pitch, kParam##E##Fine, \
   kParam##E##Detune, kParam##E##Oversample, \
   TWINFM_FM_OP_PARAMS(E, 1), TWINFM_FM_OP_PARAMS(E, 2), TWINFM_FM_OP_PARAMS(E, 3), \
   TWINFM_FM_OP_PARAMS(E, 4)

enum ParamIndex {
   // Global
   kParamVoiceMode = 0,  // 0 = Mono (legato), 1 = 10-voice poly
   kParamBendRange,      // pitch-bend range in quarter-tones, 0 (off) .. 8 (4 semitones)
   kParamMasterLevel,
   // FM engines
   TWINFM_FM_ENGINE_PARAMS(A),
   TWINFM_FM_ENGINE_PARAMS(B),
   // Stereo mixer
   kParamMixALevel,
   kParamMixAPan,
   kParamMixBLevel,
   kParamMixBPan,
   // Low-pass filter
   kParamLpfCutoff,      // octaves re 440 Hz
   kParamLpfResonance,
   kParamLpfEnvAmount,   // filter-envelope depth, octaves
   kParamLpfPressure,    // channel-pressure depth, octaves
   kParamFiltAttack,
   kParamFiltDecay,
   kParamFiltSustain,
   kParamFiltRelease,
   // Amp envelope (VCA)
   kParamAmpAttack,
   kParamAmpDecay,
   kParamAmpSustain,
   kParamAmpRelease,
   // Chorus
   kParamChorusMix,
   kParamChorusRate,     // shared LFO rate, octaves re 1 Hz
   kParamChorusDepth,    // delay-mod depth (ms per unit LFO)
   kParamChorusDelay,    // base delay, ms
   kParamChorusFeedback,
   kParamCount
};

#undef TWINFM_FM_OP_PARAMS
#undef TWINFM_FM_ENGINE_PARAMS

static_assert(kParamBAlgorithm - kParamAAlgorithm == kFmEngineParamCount, "engine block size");
static_assert(kParamAOp2Wave - kParamAOp1Wave == kFmOpCount, "operator block size");

// Engine 0 = A, 1 = B.
constexpr uint32_t fmParam(uint32_t engine, uint32_t offset)
{
   return kParamAAlgorithm + engine * kFmEngineParamCount + offset;
}
// Operator 0..3 (Op 1..4).
constexpr uint32_t fmOpParam(uint32_t engine, uint32_t op, uint32_t item)
{
   return fmParam(engine, kFmOpBase + op * kFmOpCount + item);
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

#define TWINFM_FM_OP_SPECS(E, e, O, LVL) \
   {#E " Op" #O " Wave",    #e "op" #O "wave", "",   0.0f,   4.0f,  0.0f,   false, true }, \
   {#E " Op" #O " Ratio",   #e "op" #O "rat",  "",   0.5f,  16.0f,  1.0f,   true,  false}, \
   {#E " Op" #O " Fine",    #e "op" #O "fine", "ct", -100.0f, 100.0f, 0.0f, false, false}, \
   {#E " Op" #O " Level",   #e "op" #O "lvl",  "",   0.0f,   1.0f,  LVL,    false, false}, \
   {#E " Op" #O " Delay",   #e "op" #O "dly",  "s",  0.0f,   5.0f,  0.0f,   false, false}, \
   {#E " Op" #O " Attack",  #e "op" #O "att",  "s",  0.001f, 20.0f, 0.005f, true,  false}, \
   {#E " Op" #O " Decay",   #e "op" #O "dec",  "s",  0.001f, 30.0f, 0.3f,   true,  false}, \
   {#E " Op" #O " Sustain", #e "op" #O "sus",  "",   0.0f,   1.0f,  0.7f,   false, false}, \
   {#E " Op" #O " Release", #e "op" #O "rel",  "s",  0.001f, 30.0f, 0.3f,   true,  false}
#define TWINFM_FM_ENGINE_SPECS(E, e, FINE) \
   {#E " Algorithm",  #e "algo",  "",   0.0f,    7.0f,  0.0f, false, true }, \
   {#E " Feedback",   #e "fb",    "",   0.0f,    1.0f,  0.0f, false, false}, \
   {#E " Pitch",      #e "pitch", "st", -24.0f,  24.0f, 0.0f, false, true }, \
   {#E " Fine",       #e "fine",  "ct", -100.0f, 100.0f, FINE, false, false}, \
   {#E " Detune",     #e "detun", "",   0.0f,    1.0f,  0.0f, false, false}, \
   {#E " Oversample", #e "ovs",   "",   0.0f,    2.0f,  0.0f, false, true }, \
   TWINFM_FM_OP_SPECS(E, e, 1, 0.9f), TWINFM_FM_OP_SPECS(E, e, 2, 0.6f), \
   TWINFM_FM_OP_SPECS(E, e, 3, 0.5f), TWINFM_FM_OP_SPECS(E, e, 4, 0.4f)

static constexpr ParamSpec kParams[kParamCount] = {
   // Global
   {"Voices",       "voices",   "",   0.0f,   1.0f,   1.0f,  false, true },
   {"Bend Range",   "bend",     "",   0.0f,   8.0f,   4.0f,  false, true },
   {"Master",       "master",   "",   0.0f,   1.0f,   0.7f,  false, false},
   // FM engines (B is detuned a few cents by default so the two thicken each other)
   TWINFM_FM_ENGINE_SPECS(A, a, 0.0f),
   TWINFM_FM_ENGINE_SPECS(B, b, 7.0f),
   // Stereo mixer
   {"A Level",      "mixAlvl",  "",   0.0f,   1.0f,   0.7f,  false, false},
   {"A Pan",        "mixApan",  "",  -1.0f,   1.0f,  -0.3f,  false, false},
   {"B Level",      "mixBlvl",  "",   0.0f,   1.0f,   0.5f,  false, false},
   {"B Pan",        "mixBpan",  "",  -1.0f,   1.0f,   0.3f,  false, false},
   // Low-pass filter
   {"Cutoff",       "cutoff",   "oct", -6.0f,  5.4f,  3.0f,  false, false},
   {"Resonance",    "reso",     "",   1.0f,  20.0f,   1.0f,  false, false},
   {"Env Amount",   "fenvamt",  "oct", -8.0f,  8.0f,  2.0f,  false, false},
   {"Pressure",     "pressure", "oct", -8.0f,  8.0f,  2.0f,  false, false},
   {"F Attack",     "fatt",     "s",  0.001f, 10.0f,  0.005f, true, false},
   {"F Decay",      "fdec",     "s",  0.001f, 10.0f,  0.5f,  true,  false},
   {"F Sustain",    "fsus",     "",   0.0f,   1.0f,   0.4f,  false, false},
   {"F Release",    "frel",     "s",  0.001f, 10.0f,  0.5f,  true,  false},
   // Amp envelope
   {"Attack",       "aatt",     "s",  0.001f, 10.0f,  0.005f, true, false},
   {"Decay",        "adec",     "s",  0.001f, 10.0f,  0.3f,  true,  false},
   {"Sustain",      "asus",     "",   0.0f,   1.0f,   0.8f,  false, false},
   {"Release",      "arel",     "s",  0.001f, 10.0f,  0.5f,  true,  false},
   // Chorus
   {"Mix",          "chmix",    "",   0.0f,   1.0f,   0.3f,  false, false},
   {"Rate",         "chrate",   "",  -5.0f,   3.0f,  -1.0f,  false, false},
   {"Depth",        "chdepth",  "ms", 0.0f,   1.0f,   0.5f,  false, false},
   {"Delay",        "chdelay",  "ms", 5.0f,  30.0f,  10.0f,  false, false},
   {"Feedback",     "chfb",     "",   0.0f,   0.99f,  0.2f,  false, false},
};

#undef TWINFM_FM_OP_SPECS
#undef TWINFM_FM_ENGINE_SPECS

} // namespace twinfm

#endif // TWINFM_PARAMS_H
