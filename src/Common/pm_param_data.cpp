#include "pm_param_data.h"

// LPF — 24 dB/oct Butterworth. Cutoff default 5 octaves above A440 (~14 kHz, near-open); Q = 1
// (flat Butterworth). Input + mod inputs are connection ports (value 0); mod levels default 0.
static const ModuleParamConstants lpfConstants[] = {
   {"input",         LPFParamItemInput,          0, 0, nullptr,  0.0f,  0.0f,  0.0f, 0}, // connection
   {"frequency",     LPFParamItemPitch,          0, 0, nullptr, -6.0f,  5.4f,  5.0f, 0},
   {"freqModLevel",  LPFParamItemFreqModLevel,   0, 0, nullptr, -12.0f, 12.0f, 0.0f, 0}, // filter env: covers the whole cutoff span
   {"q",             LPFParamItemQ,              0, 0, nullptr,  1.0f, 20.0f,  1.0f, 0},
   {"freqModInput",  LPFParamItemModFreqInput,   0, 0, nullptr,  0.0f,  0.0f,  0.0f, 0}, // connection
   {"freqModInput2", LPFParamItemModFreqInput2,  0, 0, nullptr,  0.0f,  0.0f,  0.0f, 0}, // connection
   {"freqModLevel2", LPFParamItemFreqModLevel2,  0, 0, nullptr, -8.0f,  8.0f,  0.0f, 0},
   {"qModInput",     LPFParamItemModQInput,      0, 0, nullptr,  0.0f,  0.0f,  0.0f, 0}, // connection
   {"qModLevel",     LPFParamItemModQInputLevel, 0, 0, nullptr,-19.0f, 19.0f,  0.0f, 0},
   {"freqModInput3", LPFParamItemModFreqInput3,  0, 0, nullptr,  0.0f,  0.0f,  0.0f, 0}, // connection
   {"freqModLevel3", LPFParamItemFreqModLevel3,  0, 0, nullptr, -8.0f,  8.0f,  0.0f, 0},
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


// --- ModicumFM voice modules (tables ported from PolygraphModularUnified; unit/valueStrings/
// paramInfo left 0 like the rest of this file) ---

#define FM6OP_OP(N, LVL) \
   {"op" #N "AmpInput",  FM6OpParamItemOp##N##AmpInput,  0,  0, nullptr,    0.0f, 0.0f, 0.0f, 0}, \
   {"op" #N "FreqInput", FM6OpParamItemOp##N##FreqInput, 0,  0, nullptr,    0.0f, 0.0f, 0.0f, 0}, \
   {"op" #N "Ratio",     FM6OpParamItemOp##N##Ratio,     0,  0, nullptr,    0.5f, 16.0f, 1.0f, 0}, \
   {"op" #N "Fine",      FM6OpParamItemOp##N##Fine,      0, 0, nullptr,    -100.0f, 100.0f, 0.0f, 0}, \
   {"op" #N "Wave",      FM6OpParamItemOp##N##Wave,      0,  0, nullptr, 0.0f, 4.0f, 0.0f, 0}, \
   {"op" #N "Level",     FM6OpParamItemOp##N##Level,     0, 0, nullptr,    0.0f, 1.0f, LVL, 0}, \
   {"op" #N "AmpDepth",  FM6OpParamItemOp##N##AmpDepth,  0,  0, nullptr,    0.0f, 1.0f, 1.0f, 0}, \
   {"op" #N "FreqDepth", FM6OpParamItemOp##N##FreqDepth, 0,  0, nullptr,    0.0f, 1.0f, 0.0f, 0}, \
   {"op" #N "Delay",     FM6OpParamItemOp##N##Delay,     0,  0, nullptr,    0.0f, 5.0f, 0.0f, 0}, \
   {"op" #N "Attack",    FM6OpParamItemOp##N##Attack,    0,  0, nullptr,    0.001f, 20.0f, 0.005f, 0}, \
   {"op" #N "Decay",     FM6OpParamItemOp##N##Decay,     0,  0, nullptr,    0.001f, 30.0f, 0.3f, 0}, \
   {"op" #N "Sustain",   FM6OpParamItemOp##N##Sustain,   0, 0, nullptr,    0.0f, 1.0f, 0.70f, 0}, \
   {"op" #N "Release",   FM6OpParamItemOp##N##Release,   0,  0, nullptr,    0.001f, 30.0f, 0.3f, 0}

static const ModuleParamConstants fm6OpConstants[] = {
   {"algorithm",   FM6OpParamItemAlgorithm,   0,  0, nullptr,  0.0f, 31.0f, 0.0f, 0},
   {"feedback",    FM6OpParamItemFeedback,    0,  0, nullptr, 0.0f, 1.0f, 0.0f, 0},
   {"outputLevel", FM6OpParamItemOutputLevel, 0, 0, nullptr, 0.0f, 1.0f, 0.8f, 0},
   {"pitchInput",  FM6OpParamItemPitchInput,  0,  0, nullptr, 0.0f, 0.0f, 0.0f, 0},
   {"gateInput",   FM6OpParamItemGateInput,   0,  0, nullptr, 0.0f, 0.0f, 0.0f, 0},
   {"retrigger",   FM6OpParamItemRetrigger,   0,  0, nullptr, 0.0f, 2.0f, 0.0f, 0},
   {"detune",      FM6OpParamItemDetune,      0,  0, nullptr, 0.0f, 1.0f, 0.0f, 0},
   {"spread",      FM6OpParamItemSpread,      0,  0, nullptr, 0.0f, 1.0f, 1.0f, 0},
   FM6OP_OP(1, 0.9f),
   FM6OP_OP(2, 0.35f),
   FM6OP_OP(3, 0.0f),
   FM6OP_OP(4, 0.0f),
   FM6OP_OP(5, 0.0f),
   FM6OP_OP(6, 0.0f),
   {"pitch",         FM6OpParamItemPitch,         0, 0, nullptr, -48.0f, 48.0f, 0.0f, 0},
   {"fine",          FM6OpParamItemFine,          0, 0, nullptr, -100.0f, 100.0f, 0.0f, 0},
   {"fmInput",       FM6OpParamItemFMInput,       0,  0, nullptr, 0.0f, 0.0f, 0.0f, 0},
   {"fmLevel",       FM6OpParamItemFMLevel,       0,   0, nullptr, 0.0f, 2000.0f, 0.0f, 0},
   {"feedbackInput", FM6OpParamItemFeedbackInput, 0,  0, nullptr, 0.0f, 0.0f, 0.0f, 0},
   {"feedbackDepth", FM6OpParamItemFeedbackDepth, 0,  0, nullptr, 0.0f, 1.0f, 0.0f, 0},
   {"oversample",    FM6OpParamItemOversample,    0,  0, nullptr, 0.0f, 2.0f, 0.0f, 0},
   {"velFloor",      FM6OpParamItemVelFloor,      0,  0, nullptr, 0.0f, 4.0f, 0.0f, 0}, // 0% = full velocity range
   {"velCurve",      FM6OpParamItemVelCurve,      0,  0, nullptr, 0.0f, 2.0f, 1.0f, 0}, // Linear
   {"brightness",    FM6OpParamItemBrightness,    0,  0, nullptr, -1.0f, 1.0f, 0.0f, 0}, // macros: neutral
   {"bite",          FM6OpParamItemBite,          0,  0, nullptr, 0.0f, 1.0f, 0.0f, 0},
   {"grit",          FM6OpParamItemGrit,          0,  0, nullptr, 0.0f, 1.0f, 0.0f, 0},
   {"body",          FM6OpParamItemBody,          0,  0, nullptr, -1.0f, 1.0f, 0.0f, 0},
   {"touch",         FM6OpParamItemTouch,         0,  0, nullptr, 0.0f, 1.0f, 0.0f, 0},  // response: neutral
   {"keyTrack",      FM6OpParamItemKeyTrack,      0,  0, nullptr, -1.0f, 1.0f, 0.0f, 0},
   {"loudAttack",    FM6OpParamItemLoudAttack,    0,  0, nullptr, 0.01f, 1000.0f, 1.0f, 0},  // shared envs: neutral
   {"loudDecay",     FM6OpParamItemLoudDecay,     0,  0, nullptr, 0.01f, 1000.0f, 1.0f, 0},
   {"loudSustain",   FM6OpParamItemLoudSustain,   0,  0, nullptr, -1.0f, 1.0f, 0.0f, 0},
   {"loudRelease",   FM6OpParamItemLoudRelease,   0,  0, nullptr, 0.01f, 1000.0f, 1.0f, 0},
   {"brightAttack",  FM6OpParamItemBrightAttack,  0,  0, nullptr, 0.01f, 1000.0f, 1.0f, 0},
   {"brightDecay",   FM6OpParamItemBrightDecay,   0,  0, nullptr, 0.01f, 1000.0f, 1.0f, 0},
   {"brightSustain", FM6OpParamItemBrightSustain, 0,  0, nullptr, -1.0f, 1.0f, 0.0f, 0},
   {"brightRelease", FM6OpParamItemBrightRelease, 0,  0, nullptr, 0.01f, 1000.0f, 1.0f, 0},
   {"brightModInput",  FM6OpParamItemBrightModInput,  0, 0, nullptr,  0.0f, 0.0f, 0.0f, 0}, // connection
   {"brightModLevel",  FM6OpParamItemBrightModLevel,  0, 0, nullptr, -1.0f, 1.0f, 0.0f, 0},
   {"brightMod2Input", FM6OpParamItemBrightMod2Input, 0, 0, nullptr,  0.0f, 0.0f, 0.0f, 0}, // connection
   {"brightMod2Level", FM6OpParamItemBrightMod2Level, 0, 0, nullptr, -1.0f, 1.0f, 0.0f, 0},
   {"levelModInput",   FM6OpParamItemLevelModInput,   0, 0, nullptr,  0.0f, 0.0f, 0.0f, 0}, // connection
   {"levelModLevel",   FM6OpParamItemLevelModLevel,   0, 0, nullptr, -1.0f, 1.0f, 0.0f, 0},
};
#undef FM6OP_OP
const ModuleParamConstantsGroup fm6OpConstantsGroup = {
   const_cast<char*>("FM 6-Op"), sizeof(fm6OpConstants) / sizeof(fm6OpConstants[0]), fm6OpConstants};

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
   {"mpe",   ChannelPressureParamItemMPE,   0, 0, nullptr, 0.0f, 1.0f, 0.0f, 0},
};
// CC converter — a chosen controller number → a held value; Per Note mode makes CC74 the Timbre source.
static const ModuleParamConstants ccConverterConstants[] = {
   {"ccNumber",    CCParamItemNumber,    0, 0, nullptr, 0.0f, 127.0f, 1.0f, 0},
   {"ccIsBoolean", CCParamItemIsBoolean, 0, 0, nullptr, 0.0f, 1.0f, 0.0f, 0},
   {"input",       CCParamItemInput,     0, 0, nullptr, 0.0f, 0.0f, 0.0f, 0}, // connection
   {"ccAmount",    CCParamItemAmount,    0, 0, nullptr, 0.0f, 1.0f, 1.0f, 0},
   {"ccOffset",    CCParamItemOffset,    0, 0, nullptr, 0.0f, 1.0f, 0.0f, 0},
   {"ccPerNote",   CCParamItemPerNote,   0, 0, nullptr, 0.0f, 1.0f, 0.0f, 0},
   {"ccMpe",       CCParamItemMPE,       0, 0, nullptr, 0.0f, 1.0f, 0.0f, 0},
};
const ModuleParamConstantsGroup ccConverterConstantsGroup = {
   const_cast<char*>("CC Converter"), sizeof(ccConverterConstants) / sizeof(ccConverterConstants[0]), ccConverterConstants};

const ModuleParamConstantsGroup channelPressureConstantsGroup = {
   const_cast<char*>("Channel Pressure"), sizeof(channelPressureConstants) / sizeof(channelPressureConstants[0]), channelPressureConstants};

static const ModuleParamConstants pitchBendConverterConstants[] = {
   {"quarterTones", PitchBendConverterParamItemQuarterTones, 0, 0, nullptr, 0.0f, 8.0f, 4.0f, 0},
   {"mpe",          PitchBendConverterParamItemMPE,          0, 0, nullptr, 0.0f, 1.0f, 0.0f, 0},
};
const ModuleParamConstantsGroup pitchBendConverterConstantsGroup = {
   const_cast<char*>("Pitch Bend"), sizeof(pitchBendConverterConstants) / sizeof(pitchBendConverterConstants[0]), pitchBendConverterConstants};

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
