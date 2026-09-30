#include "pm_param_data.h"

// LPF — 24 dB/oct Butterworth. Cutoff default 5 octaves above A440 (~14 kHz, near-open); Q = 1
// (flat Butterworth). Input + mod inputs are connection ports (value 0); mod levels default 0.
static const ModuleParamConstants lpfConstants[] = {
   {"input",         LPFParamItemInput,          0, 0, nullptr,  0.0f,  0.0f,  0.0f, 0}, // connection
   {"frequency",     LPFParamItemPitch,          0, 0, nullptr, -6.0f,  5.4f,  5.0f, 0},
   {"freqModLevel",  LPFParamItemFreqModLevel,   0, 0, nullptr, -8.0f,  8.0f,  0.0f, 0},
   {"q",             LPFParamItemQ,              0, 0, nullptr,  1.0f, 20.0f,  1.0f, 0},
   {"freqModInput",  LPFParamItemModFreqInput,   0, 0, nullptr,  0.0f,  0.0f,  0.0f, 0}, // connection
   {"freqModInput2", LPFParamItemModFreqInput2,  0, 0, nullptr,  0.0f,  0.0f,  0.0f, 0}, // connection
   {"freqModLevel2", LPFParamItemFreqModLevel2,  0, 0, nullptr, -8.0f,  8.0f,  0.0f, 0},
   {"qModInput",     LPFParamItemModQInput,      0, 0, nullptr,  0.0f,  0.0f,  0.0f, 0}, // connection
   {"qModLevel",     LPFParamItemModQInputLevel, 0, 0, nullptr,-19.0f, 19.0f,  0.0f, 0},
};
const ModuleParamConstantsGroup lpfConstantsGroup = {
   const_cast<char*>("lpf"),
   sizeof(lpfConstants) / sizeof(lpfConstants[0]),
   lpfConstants,
};

// ADSR — 7 params. Times in seconds; sustain is a level 0..1.
static const ModuleParamConstants adsrConstants[] = {
   {"attack",    ADSRParamItemAttack,    0, 0, nullptr, 0.001f, 10.0f, 0.01f, 0},
   {"decay",     ADSRParamItemDecay,     0, 0, nullptr, 0.001f, 10.0f, 1.00f, 0},
   {"sustain",   ADSRParamItemSustain,   0, 0, nullptr, 0.0f, 1.0f, 0.5f, 0},
   {"release",   ADSRParamItemRelease,   0, 0, nullptr, 0.001f, 10.0f, 1.00f, 0},
   {"gateInput", ADSRParamItemGateInput, 0, 0, nullptr, 0.0f, 3.4e38f, 0.0f, 0},
   {"legato",    ADSRParamItemLegato,    0, 0, nullptr, 0.0f, 2.0f, 0.0f, 0},
   {"linear",    ADSRParamItemLinear,    0, 0, nullptr, 0.0f, 1.0f, 0.0f, 0},
};

const ModuleParamConstantsGroup adsrConstantsGroup = {
   const_cast<char*>("adsr"),
   sizeof(adsrConstants) / sizeof(adsrConstants[0]),
   adsrConstants,
};

// Combine — 4-quadrant multiplier (the VCA). Type defaults to Multiply, Level 1.0.
static const ModuleParamConstants combineConstants[] = {
   {"input1", CombineParamItemInput1, 0, 0, nullptr, 0.0f, 0.0f, 0.0f, 0},
   {"input2", CombineParamItemInput2, 0, 0, nullptr, 0.0f, 0.0f, 0.0f, 0},
   {"input3", CombineParamItemInput3, 0, 0, nullptr, 0.0f, 0.0f, 0.0f, 0},
   {"type", CombineParamItemType, 0, 0, nullptr, (float)ParamItemCombineType_Add,
    (float)ParamItemCombineType_Multiply, (float)ParamItemCombineType_Multiply, 0},
   {"level", CombineParamItemLevel, 0, 0, nullptr, 0.0f, 1.0f, 1.0f, 0},
};
const ModuleParamConstantsGroup combineConstantsGroup = {
   const_cast<char*>("combine"), sizeof(combineConstants) / sizeof(combineConstants[0]),
   combineConstants};

// Final — output stage (DC block / level / M-S spread / clip).
static const ModuleParamConstants finalConstants[] = {
   {"input", FinalOutputParamItemInput, 0, 0, nullptr, 0.0f, 0.0f, 0.0f, 0},
   {"spread", FinalOutputParamItemSpread, 0, 0, nullptr, 0.0f, 1.0f, 0.7f, 0},
   {"level", FinalOutputParamItemLevel, 0, 0, nullptr, 0.0f, 1.0f, 0.7f, 0},
   {"clipLevel", FinalOutputParamItemClipLevel, 0, 0, nullptr, 0.0f, 1.0f, 1.0f, 0},
   {"filterDC", FinalOutputParamItemFilterDC, 0, 0, nullptr, 0.0f, 1.0f, 1.0f, 0},
};
const ModuleParamConstantsGroup finalConstantsGroup = {
   const_cast<char*>("final"), sizeof(finalConstants) / sizeof(finalConstants[0]), finalConstants};

// PitchConverter — MIDI note → pitch (1/octave). One placeholder param.
static const ModuleParamConstants pitchConverterConstants[] = {
   {"dummy", PitchConverterParamItemDummy, 0, 0, nullptr, 0.0f, 0.0f, 0.0f, 0},
};
const ModuleParamConstantsGroup pitchConverterModuleConstantsGroup = {
   const_cast<char*>("pitchConverter"),
   sizeof(pitchConverterConstants) / sizeof(pitchConverterConstants[0]), pitchConverterConstants};


// --- TwinFM voice modules (tables ported from PolygraphModularUnified; unit/valueStrings/
// paramInfo left 0 like the rest of this file) ---

#define FM4OP_OP(N, LVL) \
   {"op" #N "AmpInput",  FM4OpParamItemOp##N##AmpInput,  0,  0, nullptr,    0.0f, 0.0f, 0.0f, 0}, \
   {"op" #N "FreqInput", FM4OpParamItemOp##N##FreqInput, 0,  0, nullptr,    0.0f, 0.0f, 0.0f, 0}, \
   {"op" #N "Ratio",     FM4OpParamItemOp##N##Ratio,     0,  0, nullptr,    0.5f, 16.0f, 1.0f, 0}, \
   {"op" #N "Fine",      FM4OpParamItemOp##N##Fine,      0, 0, nullptr,    -100.0f, 100.0f, 0.0f, 0}, \
   {"op" #N "Wave",      FM4OpParamItemOp##N##Wave,      0,  0, nullptr, 0.0f, 4.0f, 0.0f, 0}, \
   {"op" #N "Level",     FM4OpParamItemOp##N##Level,     0, 0, nullptr,    0.0f, 1.0f, LVL, 0}, \
   {"op" #N "AmpDepth",  FM4OpParamItemOp##N##AmpDepth,  0,  0, nullptr,    0.0f, 1.0f, 1.0f, 0}, \
   {"op" #N "FreqDepth", FM4OpParamItemOp##N##FreqDepth, 0,  0, nullptr,    0.0f, 1.0f, 0.0f, 0}, \
   {"op" #N "Delay",     FM4OpParamItemOp##N##Delay,     0,  0, nullptr,    0.0f, 5.0f, 0.0f, 0}, \
   {"op" #N "Attack",    FM4OpParamItemOp##N##Attack,    0,  0, nullptr,    0.001f, 20.0f, 0.005f, 0}, \
   {"op" #N "Decay",     FM4OpParamItemOp##N##Decay,     0,  0, nullptr,    0.001f, 30.0f, 0.3f, 0}, \
   {"op" #N "Sustain",   FM4OpParamItemOp##N##Sustain,   0, 0, nullptr,    0.0f, 1.0f, 0.70f, 0}, \
   {"op" #N "Release",   FM4OpParamItemOp##N##Release,   0,  0, nullptr,    0.001f, 30.0f, 0.3f, 0}

static const ModuleParamConstants fm4OpConstants[] = {
   {"algorithm",   FM4OpParamItemAlgorithm,   0,  0, nullptr,  0.0f, 7.0f, 0.0f, 0},
   {"feedback",    FM4OpParamItemFeedback,    0,  0, nullptr, 0.0f, 1.0f, 0.0f, 0},
   {"outputLevel", FM4OpParamItemOutputLevel, 0, 0, nullptr, 0.0f, 1.0f, 0.8f, 0},
   {"pitchInput",  FM4OpParamItemPitchInput,  0,  0, nullptr, 0.0f, 0.0f, 0.0f, 0},
   {"gateInput",   FM4OpParamItemGateInput,   0,  0, nullptr, 0.0f, 0.0f, 0.0f, 0},
   {"retrigger",   FM4OpParamItemRetrigger,   0,  0, nullptr, 0.0f, 2.0f, 0.0f, 0},
   {"detune",      FM4OpParamItemDetune,      0,  0, nullptr, 0.0f, 1.0f, 0.0f, 0},
   {"spread",      FM4OpParamItemSpread,      0,  0, nullptr, 0.0f, 1.0f, 1.0f, 0},
   FM4OP_OP(1, 0.9f),
   FM4OP_OP(2, 0.6f),
   FM4OP_OP(3, 0.5f),
   FM4OP_OP(4, 0.4f),
   {"pitch",         FM4OpParamItemPitch,         0, 0, nullptr, -48.0f, 48.0f, 0.0f, 0},
   {"fine",          FM4OpParamItemFine,          0, 0, nullptr, -100.0f, 100.0f, 0.0f, 0},
   {"fmInput",       FM4OpParamItemFMInput,       0,  0, nullptr, 0.0f, 0.0f, 0.0f, 0},
   {"fmLevel",       FM4OpParamItemFMLevel,       0,   0, nullptr, 0.0f, 2000.0f, 0.0f, 0},
   {"feedbackInput", FM4OpParamItemFeedbackInput, 0,  0, nullptr, 0.0f, 0.0f, 0.0f, 0},
   {"feedbackDepth", FM4OpParamItemFeedbackDepth, 0,  0, nullptr, 0.0f, 1.0f, 0.0f, 0},
   {"oversample",    FM4OpParamItemOversample,    0,  0, nullptr, 0.0f, 2.0f, 0.0f, 0},
};
#undef FM4OP_OP
const ModuleParamConstantsGroup fm4OpConstantsGroup = {
   const_cast<char*>("FM 4-Op"), sizeof(fm4OpConstants) / sizeof(fm4OpConstants[0]), fm4OpConstants};

static const ModuleParamConstants sharedLFOConstants[] = {
   {"frequency", SharedLFOParamItemPitch,   0,  0, nullptr,   -7.0f, 7.0f, 0.0f, 0},
   {"wave",      SharedLFOParamItemWave,    0,  0, nullptr,   0.0f, 3.0f, 1.0f, 0}, // Sine..Impulse; default Triangle
   {"lrPhase",   SharedLFOParamItemPhase,   0,  0, nullptr,  0.0f, 4.0f, 0.0f, 0},
   {"freeRun",   SharedLFOParamItemFreeRun, 0, 0, nullptr,    0.0f, 1.0f, 0.0f, 0}, // off = voices-active-only (reset on silence)
   {"clockInput", SharedLFOParamItemClockInput, 0, 0, nullptr,   0.0f, 0.0f, 0.0f, 0},
   {"clockRatio", SharedLFOParamItemClockRatio, 0, 0, nullptr, 0.0f, 13.0f, 0.0f, 0}, // default Auto
};
const ModuleParamConstantsGroup sharedLFOConstantsGroup = {
   const_cast<char*>("Shared LFO"), sizeof(sharedLFOConstants) / sizeof(sharedLFOConstants[0]), sharedLFOConstants};

static const ModuleParamConstants channelPressureConstants[] = {
   {"dummy", ChannelPressureParamItemDummy, 0, 0, nullptr, 0.0f, 0.0f, 0.0f, 0}, // hidden
};
const ModuleParamConstantsGroup channelPressureConstantsGroup = {
   const_cast<char*>("Channel Pressure"), sizeof(channelPressureConstants) / sizeof(channelPressureConstants[0]), channelPressureConstants};

static const ModuleParamConstants pitchBendConverterConstants[] = {
   {"quarterTones", PitchBendConverterParamItemQuarterTones, 0, 0, nullptr, 0.0f, 8.0f, 4.0f, 0},
};
const ModuleParamConstantsGroup pitchBendConverterConstantsGroup = {
   const_cast<char*>("Pitch Bend"), sizeof(pitchBendConverterConstants) / sizeof(pitchBendConverterConstants[0]), pitchBendConverterConstants};

static const ModuleParamConstants quadStereoMixerConstants[] = {
   {"input1", QuadStereoMixerParamItemInput1, 0, 0, nullptr, 0.0f, 0.0f, 0.0f, 0}, // connection
   {"input2", QuadStereoMixerParamItemInput2, 0, 0, nullptr, 0.0f, 0.0f, 0.0f, 0}, // connection
   {"input3", QuadStereoMixerParamItemInput3, 0, 0, nullptr, 0.0f, 0.0f, 0.0f, 0}, // connection
   {"input4", QuadStereoMixerParamItemInput4, 0, 0, nullptr, 0.0f, 0.0f, 0.0f, 0}, // connection
   {"level1", QuadStereoMixerParamItemLevel1, 0, 0, nullptr, 0.0f, 1.0f, 0.7f, 0},
   {"level2", QuadStereoMixerParamItemLevel2, 0, 0, nullptr, 0.0f, 1.0f, 0.7f, 0},
   {"level3", QuadStereoMixerParamItemLevel3, 0, 0, nullptr, 0.0f, 1.0f, 0.7f, 0},
   {"level4", QuadStereoMixerParamItemLevel4, 0, 0, nullptr, 0.0f, 1.0f, 0.7f, 0},
   {"pan1",   QuadStereoMixerParamItemPan1, 0, 0, nullptr, -1.0f, 1.0f, 0.0f, 0},
   {"pan2",   QuadStereoMixerParamItemPan2, 0, 0, nullptr, -1.0f, 1.0f, 0.0f, 0},
   {"pan3",   QuadStereoMixerParamItemPan3, 0, 0, nullptr, -1.0f, 1.0f, 0.0f, 0},
   {"pan4",   QuadStereoMixerParamItemPan4, 0, 0, nullptr, -1.0f, 1.0f, 0.0f, 0},
};
const ModuleParamConstantsGroup quadStereoMixerConstantsGroup = {
   const_cast<char*>("Quad Stereo Mixer"), sizeof(quadStereoMixerConstants) / sizeof(quadStereoMixerConstants[0]), quadStereoMixerConstants};

static const ModuleParamConstants chorusConstants[] = {
   {"input",         ChorusParamItemInput, 0, 0, nullptr, 0.0f, 0.0f, 0.0f, 0}, // connection
   {"baseDelay",     ChorusParamItemBaseDelay, 0, 0, nullptr, 5.0f, 30.0f, 10.0f, 0},
   {"mix",           ChorusParamItemMix, 0, 0, nullptr, 0.0f, 1.0f, 0.5f, 0},
   {"feedback",      ChorusParamItemFeedback, 0, 0, nullptr, 0.0f, 0.99f, 0.2f, 0},
   {"modInput",      ChorusParamItemModDelayInput, 0, 0, nullptr, 0.0f, 0.0f, 0.0f, 0}, // connection
   {"modDelayLevel", ChorusParamItemModDelayLevel, 0, 0, nullptr, 0.0f, 1.0f, 0.5f, 0},
};
const ModuleParamConstantsGroup chorusConstantsGroup = {
   const_cast<char*>("Chorus"), sizeof(chorusConstants) / sizeof(chorusConstants[0]), chorusConstants};
