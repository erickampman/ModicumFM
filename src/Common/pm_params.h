// pm_params.h — portable replacement for PolygraphModular's ParamGroupData.h.
//
// The original is Objective-C (NSString*/NSArray in the param-constants struct, NS_ENUM
// throughout), so rather than vendor it we provide the same type NAMES the framework expects,
// with plain C++ types (const char*, float, uint32). The per-module parameter-ITEM enums are
// added here as each module is ported. The actual per-module constant DATA (names/ranges) is
// vendored/converted separately, in pm_param_data.cpp.

#ifndef PM_PARAMS_H
#define PM_PARAMS_H

#include <cstddef>
#include <cstdint>

#include "pm_compat.h"

// First param enum value is 1 (0 is "unspecified"); enums double as array indices.
#define PARAM_ENUM_TO_INDEX(e) ((e) - 1)

// Module kinds — the high byte of a param address. Values must stay stable (they are the
// module identity in every param clap_id). Mirrors PolygraphModular's ModuleParamKind.
enum ModuleParamKind : NSUInteger {
   ModuleParamKindUnspecified = 0,
   ModuleParamKindGlobal      = 1,
   ModuleParamKindPitchConverter,
   ModuleParamKindVelocityConverter,
   ModuleParamKindSlew,
   ModuleParamKindSinOsc,
   ModuleParamKindTriSawOsc,
   ModuleParamKindNoise,
   ModuleParamKindADR,
   ModuleParamKindADSR,
   ModuleParamKindLFO, /* 0x0A */
   ModuleParamKindButterworthLowPassFilter,
   ModuleParamKindChebyshevLowPassFilter,
   ModuleParamKindButterworthBandpassFilter,
   ModuleParamKindMultimodeFilter,
   ModuleParamKindBiquadFilter,
   ModuleParamKindFormantFilter, /* 0x10 */
   ModuleParamKindVariableSlopeFilter,
   ModuleParamKindRezNotchFilter,
   ModuleParamKindSineWarp,
   ModuleParamKindPolynomial,
   ModuleParamKindScaleOffset,
   ModuleParamKindLevel,
   ModuleParamKindComparator,
   ModuleParamKindThresholdImpulse,
   ModuleParamKindDeltaImpulse,
   ModuleParamKindOneShot,
   ModuleParamKindSampleAndHold,
   ModuleParamKindSequencer,
   ModuleParamKindDiscretizer,
   ModuleParamKindEDODiscretizer,
   ModuleParamKindCombine,
   ModuleParamKindQuadCombine, /* 0x20 */
   ModuleParamKindQuadStereoMixer,
   ModuleParamKindXFade,
   ModuleParamKindPitchBendConverter,
   ModuleParamKindControlChangeConverter,
   ModuleParamKindChannelPressure,
   ModuleParamKindNoteGate,
   ModuleParamKindLogicGate,
   ModuleParamKindClockDivider,
   ModuleParamKindSampleDelay,
   ModuleParamKindFinalOutput,
   ModuleParamKindTunnelInput,
   ModuleParamKindTunnelOutput,
   ModuleParamKindChorus,
   ModuleParamKindFlanger,
   ModuleParamKindPhaser,
   ModuleParamKindDelay, /* 0x30 */
   ModuleParamKindGlobalDelay,
   ModuleParamKindGlobalReverb,
   ModuleParamKindGlobalEnhance,
   ModuleParamKindMIDI,
   ModuleParamKindNode,
   ModuleParamKindValue,
   ModuleParamKindOverdrive,
   ModuleParamKindSplitLevel,
   ModuleParamKindMIDINoteNumberConverter,
   ModuleParamKindUnsplit,
   ModuleParamKindPan,
   ModuleParamKindIFSOsc,
   ModuleParamKindIFSEnv,
   ModuleParamKindIFSLFO,
   ModuleParamKindIFSConv,
   ModuleParamKindIFSRes,
   ModuleParamKindIFSGran,
   ModuleParamKindMandOsc,
   ModuleParamKindJuliaGran,
   ModuleParamKindClockGran,
   ModuleParamKindCVMixer, // combine mod sources (add/multiply, per-input enable)
   ModuleParamKindFM6Op,
   ModuleParamKindSharedLFO,
};

// Module instance — the middle byte of a param address (1..24).
enum ModuleInstance : NSUInteger {
   ModuleInstance1 = 1,
   ModuleInstance2, ModuleInstance3, ModuleInstance4, ModuleInstance5, ModuleInstance6,
   ModuleInstance7, ModuleInstance8, ModuleInstance9, ModuleInstance10, ModuleInstance11,
   ModuleInstance12, ModuleInstance13, ModuleInstance14, ModuleInstance15, ModuleInstance16,
   ModuleInstance17, ModuleInstance18, ModuleInstance19, ModuleInstance20, ModuleInstance21,
   ModuleInstance22, ModuleInstance23, ModuleInstance24,
};

enum ModuleItem : NSUInteger {
   ModuleItemUnspecified = 0,
};

// --- per-module parameter-item enums (added as each module is ported) --------------------

enum OscSyncType : NSUInteger {
   OscSyncTypeDown = 0,
   OscSyncTypeUp,
   OscSyncTypeBoth,
};

enum SinOscParamItem : NSUInteger {
   SinOscParamItemPitch = 1,
   SinOscParamItemPitchFine,
   SinOscParamItemPitchInput,
   SinOscParamItemModPitch,
   SinOscParamItemModPitch2,
   SinOscParamItemModPhase,
   SinOscParamItemFreqModLevel,
   SinOscParamItemPhaseModLevel,
   SinOscParamItemFreqModLevel2,
   SinOscParamItemModSync,
   SinOscParamItemModSyncType,
   SinOscParamItemDetune,   // stereo detune, cents (splits L/R by ±half); 0 = mono
   SinOscParamItemWaveform, // 0 = Sine, 1 = Triangle, 2 = Saw (band-limited via PolyBLEP/BLAMP)
};

// Waveform selector values for SinOscParamItemWaveform.
enum OscWaveform : int {
   OscWaveformSine     = 0,
   OscWaveformTriangle = 1,
   OscWaveformSaw      = 2,
};

// Level — a simple gain stage: output = input × level. Used to scale the sine osc's audible mix
// level *after* its raw output has been tapped for the IFS hard-sync input.
enum LevelParamItem : NSUInteger {
   LevelParamItemInput = 1, // connection
   LevelParamItemLevel,     // 0..1
};

// IFS fractal oscillator — trimmed v1 set (PM's modulation matrix is deferred). PitchInput is a
// connection; the five fractal-shape items + smoothing + iterations drive the wavetable.
enum IFSOscParamItem : NSUInteger {
   IFSOscParamItemPitchInput = 1, // connection: pitch from the voice chain
   IFSOscParamItemPitch,          // coarse tune (semitones)
   IFSOscParamItemPitchFine,      // fine tune (cents)
   IFSOscParamItemIFS1Squash,     // left-region self-similar vertical scale
   IFSOscParamItemIFS2Squash,     // right-region self-similar vertical scale
   IFSOscParamItemBx,             // split point (x)
   IFSOscParamItemBy,             // junction height (y)
   IFSOscParamItemStartEndY,      // shared endpoint level
   IFSOscParamItemSmoothing,      // 0 = Catmull-Rom, 1 = piecewise-linear
   IFSOscParamItemIterations,     // picker index: 0 → 3 iters, 1 → 4 iters
   // Modulation inputs (connection ports) + their bipolar depth levels. Pitch mod is applied
   // per-sample (wavetable-compatible); the shape mods switch IFSOsc to the chunked eval path.
   IFSOscParamItemPitchModInput,
   IFSOscParamItemPitchModLevel,
   IFSOscParamItemBxModInput,
   IFSOscParamItemBxModLevel,
   IFSOscParamItemByModInput,
   IFSOscParamItemByModLevel,
   IFSOscParamItemSquash1ModInput,
   IFSOscParamItemSquash1ModLevel,
   IFSOscParamItemSquash2ModInput,
   IFSOscParamItemSquash2ModLevel,
   IFSOscParamItemDetune,         // stereo detune, cents (splits L/R by ±half); 0 = mono
   IFSOscParamItemSyncInput,      // connection: hard-sync source (stereo, from the sine osc)
   IFSOscParamItemSyncMode,       // 0 Off, 1 Up, 2 Down, 3 Both (zero-crossing of the sync input)
   IFSOscParamItemLevel,          // output mix level
};

// IFS fractal envelope — 3-transform shape (blue + green interior points), phase over Duration.
enum IFSEnvParamItem : NSUInteger {
   IFSEnvParamItemGateInput = 1, // connection (unused in v1 voice; MIDI drives the gate)
   IFSEnvParamItemDuration,      // full-traversal time, seconds
   IFSEnvParamItemStartEndY,     // level at both ends
   IFSEnvParamItemBlueX,         // first interior point x [0,1]
   IFSEnvParamItemBlueY,         // first interior point y [-1,1]
   IFSEnvParamItemGreenX,        // second interior point x [0,1]
   IFSEnvParamItemGreenY,        // second interior point y [-1,1]
   IFSEnvParamItemIFS1Squash,
   IFSEnvParamItemIFS2Squash,
   IFSEnvParamItemIFS3Squash,
   IFSEnvParamItemSmoothing,
   IFSEnvParamItemIterations,    // picker index: 0 → 1 iteration
   IFSEnvParamItemHoldMode,      // 0 None, 1 Blue, 2 Green, 3 Loop
   IFSEnvParamItemLegatoMode,    // 0 NoteSteal, 1 FindMatchingY
   IFSEnvParamItemScaleRelease,  // 0 Off (run full cycle) / 1 On: an early note-off (before the hold
                                 // point, hold modes 1/2 only) jumps to the hold point and scales the
                                 // release tail to start at the current level — see IFSEnvModule.cpp.
};

// IFS fractal LFO — free-running mod source, 2-transform shape (same family as IFSOsc). Trimmed
// v1 set (no mod matrix / FM / reset / L-R phase).
enum IFSLFOParamItem : NSUInteger {
   IFSLFOParamItemPitch = 1,    // rate, octaves, -7..7, 0 = 1 Hz
   IFSLFOParamItemBx,
   IFSLFOParamItemBy,
   IFSLFOParamItemIFS1Squash,
   IFSLFOParamItemIFS2Squash,
   IFSLFOParamItemStartEndY,
   IFSLFOParamItemSmoothing,
   IFSLFOParamItemIterations,   // picker index: 0 → 1 iteration
};

// CV mixer — combines an LFO input, an Env input, and a MIDI CC into one output. Each input can
// be enabled/disabled (a disabled input is SKIPPED, not treated as 0); Mode adds or multiplies
// the enabled inputs. No per-input level (depth lives on the destination mod-input side).
enum CVMixerParamItem : NSUInteger {
   CVMixerParamItemLFOInput = 1, // connection port (from an IFSLFO)
   CVMixerParamItemEnvInput,     // connection port (from an IFSEnv)
   CVMixerParamItemEnableLFO,    // 0/1
   CVMixerParamItemEnableEnv,    // 0/1
   CVMixerParamItemEnableCC,     // 0/1
   CVMixerParamItemCCNumber,     // 1..127 (default 1 = mod wheel)
   CVMixerParamItemMode,         // 0 = add, 1 = multiply
};

// 24 dB/oct Butterworth low-pass. Item 1 is the audio input; Pitch is the (absolute) cutoff in
// octaves through the shared PitchConverter (0 = base A440); Q adds resonance. The Freq/Q mod
// inputs + their levels exist for a future LPF-mod panel (dormant until connected).
enum LPFParamItem : NSUInteger {
   LPFParamItemInput = 1,       // audio input
   LPFParamItemPitch,           // cutoff, octaves (0 = 440 Hz)
   LPFParamItemFreqModLevel,    // first freq mod depth
   LPFParamItemQ,               // resonance, 1..20
   LPFParamItemModFreqInput,    // first freq modulation input (connection)
   LPFParamItemModFreqInput2,   // second freq modulation input (connection)
   LPFParamItemFreqModLevel2,   // second freq mod depth
   LPFParamItemModQInput,       // q modulation input (connection)
   LPFParamItemModQInputLevel,  // q mod depth
   LPFParamItemModFreqInput3,   // third freq modulation input (connection; appended)
   LPFParamItemFreqModLevel3,   // third freq mod depth
};

enum ADSRParamItem : NSUInteger {
   ADSRParamItemAttack = 1,
   ADSRParamItemDecay,
   ADSRParamItemSustain,
   ADSRParamItemRelease,
   ADSRParamItemGateInput,
   ADSRParamItemLegato, // 0=Steal, 1=LegatoRetrigger, 2=LegatoTrue
   ADSRParamItemLinear, // 0=exponential, 1=linear
};

enum ParamItemCombineType : NSInteger {
   ParamItemCombineType_Add = 0,
   ParamItemCombineType_Multiply,
};

enum CombineParamItem : NSUInteger {
   CombineParamItemInput1 = 1,
   CombineParamItemInput2,
   CombineParamItemInput3,
   CombineParamItemType,
   CombineParamItemLevel,
};

enum FinalOutputParamItem : NSUInteger {
   FinalOutputParamItemInput = 1,
   FinalOutputParamItemSpread,
   FinalOutputParamItemLevel,
   FinalOutputParamItemClipLevel,
   FinalOutputParamItemFilterDC,
};

enum SlewParamItem : NSUInteger {
   SlewParamItemInput = 1,
   SlewParamItemRise,
   SlewParamItemFall,
   SlewParamItemCurve,
   SlewParamItemLegatoOnly,  // 0/1: glide only into a note played while the previous one is held (appended)
};

enum PitchConverterParamItem : NSUInteger {
   PitchConverterParamItemDummy = 1,
};

// Referenced by Voice::connectTunnelPairs (no tunnel modules in the v1 patch, but it must compile).
enum TunnelOutParamItem : NSUInteger {
   TunnelOutParamItemInput = 1,
};

// VoiceManager's own params (polyphony / filtering / mode).
enum MIDIParamItem : NSUInteger {
   MIDIParamItemVoiceCount = 1,
   MIDIParamItemGroupFilterValue,   // 0 = disabled, 1..16 = group
   MIDIParamItemChannelFilterValue, // 0 = disabled, 1..16 = channel
   MIDIParamItemVoiceStealAlgorithm,
   MIDIParamItemFreeRun,
   MIDIParamItemLegato, // 0=Steal, 1=Legato
};

enum VoiceStealAlgorithm : NSUInteger {
   VoiceStealAlgorithmOldest = 0,
   VoiceStealAlgorithmLowest,
   VoiceStealAlgorithmHighest,
   VoiceStealAlgorithmClosest,
   VoiceStealAlgorithmFurthest,
};

// Per-parameter constants (portable): the source for CLAP clap_param_info generation.

// --- ModicumFM voice modules (ported from PolygraphModularUnified) ---
enum LFOParamWave : NSUInteger {
   LFOParamWaveSine = 0,
   LFOParamWaveTriangle,
   LFOParamWaveSquare,
   LFOParamWaveImpulse,
};

enum LFOParamLRPhase : NSUInteger {   // L/R phase offset choices (index 0..4)
   LFOParamLRPhase0 = 0,
   LFOParamLRPhase60,
   LFOParamLRPhase90,
   LFOParamLRPhase120,
   LFOParamLRPhase180,
};

enum PitchBendConverterParamItem : NSUInteger {
   PitchBendConverterParamItemQuarterTones = 1, // channel bend range, 0..8 quarter-tones; also the
                                                // per-note range until registered controller 7 sets one
   PitchBendConverterParamItemMPE,              // 0/1: MPE lower zone (see pm::kMpeMasterChannel)
};

enum ChannelPressureParamItem : NSUInteger {
   ChannelPressureParamItemDummy = 1,  // no params needed; one dummy slot for UI storage
   ChannelPressureParamItemMPE,        // 0/1: MPE lower zone (see pm::kMpeMasterChannel)
};

enum CCParamItem : NSUInteger {
   CCParamItemNumber = 1,   // indexed: CC controller number 0..127
   CCParamItemIsBoolean,    // 0/1: gate vs continuous
   CCParamItemInput,        // connection: when patched, CC scales it (VCA)
   CCParamItemAmount,       // CC scale (1 = unity)
   CCParamItemOffset,       // floor added to the scaled CC (0 = none)
   CCParamItemPerNote,      // 0/1: per-channel CC + same-index per-note controller
   CCParamItemMPE,          // 0/1: with Per Note, MPE lower zone
};

enum ChorusParamItem : NSUInteger {
   ChorusParamItemInput = 1,        // connection
   ChorusParamItemBaseDelay,        // ms
   ChorusParamItemMix,              // 0..1
   ChorusParamItemFeedback,         // 0..0.99
   ChorusParamItemModDelayInput,    // connection
   ChorusParamItemModDelayLevel,
};

enum SharedLFOParamItem : NSUInteger {
   SharedLFOParamItemPitch = 1,   // rate, octaves; 0 = 1 Hz
   SharedLFOParamItemWave,        // LFOParamWave selector
   SharedLFOParamItemPhase,       // LFOParamLRPhase: L/R stereo phase offset
   SharedLFOParamItemFreeRun,     // 0 = voices-active-only (reset on silence); 1 = continuous free-run
   SharedLFOParamItemClockInput,  // connection: clock pulses — when patched, locks rate+phase to the clock
   SharedLFOParamItemClockRatio,  // indexed (kLFOClockRatios): Auto / 1/16..3/4 / ×1..×16 of the clock rate
   SharedLFOParamItemPerVoice,    // 0/1 (appended): each voice its own phase, restarted at note-on
};

enum FM6OpParamItem : NSUInteger {
   // Global
   FM6OpParamItemAlgorithm = 1,   // indexed: 0..31 (DX7 algorithms 1..32, see FM6OpAlgorithms.hpp)
   FM6OpParamItemFeedback,        // feedback amount (per-algorithm feedback op), 0..1
   FM6OpParamItemOutputLevel,     // master out
   FM6OpParamItemPitchInput,      // connection: fundamental pitch CV (oct re A440); MIDI note if unpatched
   FM6OpParamItemGateInput,       // connection: gate CV (rising=trigger, falling=release); overrides MIDI
   FM6OpParamItemRetrigger,       // indexed: 0=Steal, 1=LegatoRetrigger, 2=LegatoTrue
   FM6OpParamItemDetune,          // L/R stereo detune (0..1 -> cents)
   FM6OpParamItemSpread,          // stereo width (0=mono, 1=full L/R)
   // Operator 1 (13 items)
   FM6OpParamItemOp1AmpInput, FM6OpParamItemOp1FreqInput, FM6OpParamItemOp1Ratio,
   FM6OpParamItemOp1Fine, FM6OpParamItemOp1Wave, FM6OpParamItemOp1Level,
   FM6OpParamItemOp1AmpDepth, FM6OpParamItemOp1FreqDepth, FM6OpParamItemOp1Delay,
   FM6OpParamItemOp1Attack, FM6OpParamItemOp1Decay, FM6OpParamItemOp1Sustain, FM6OpParamItemOp1Release,
   // Operator 2 (13 items)
   FM6OpParamItemOp2AmpInput, FM6OpParamItemOp2FreqInput, FM6OpParamItemOp2Ratio,
   FM6OpParamItemOp2Fine, FM6OpParamItemOp2Wave, FM6OpParamItemOp2Level,
   FM6OpParamItemOp2AmpDepth, FM6OpParamItemOp2FreqDepth, FM6OpParamItemOp2Delay,
   FM6OpParamItemOp2Attack, FM6OpParamItemOp2Decay, FM6OpParamItemOp2Sustain, FM6OpParamItemOp2Release,
   // Operator 3 (13 items)
   FM6OpParamItemOp3AmpInput, FM6OpParamItemOp3FreqInput, FM6OpParamItemOp3Ratio,
   FM6OpParamItemOp3Fine, FM6OpParamItemOp3Wave, FM6OpParamItemOp3Level,
   FM6OpParamItemOp3AmpDepth, FM6OpParamItemOp3FreqDepth, FM6OpParamItemOp3Delay,
   FM6OpParamItemOp3Attack, FM6OpParamItemOp3Decay, FM6OpParamItemOp3Sustain, FM6OpParamItemOp3Release,
   // Operator 4 (13 items)
   FM6OpParamItemOp4AmpInput, FM6OpParamItemOp4FreqInput, FM6OpParamItemOp4Ratio,
   FM6OpParamItemOp4Fine, FM6OpParamItemOp4Wave, FM6OpParamItemOp4Level,
   FM6OpParamItemOp4AmpDepth, FM6OpParamItemOp4FreqDepth, FM6OpParamItemOp4Delay,
   FM6OpParamItemOp4Attack, FM6OpParamItemOp4Decay, FM6OpParamItemOp4Sustain, FM6OpParamItemOp4Release,
   // Operator 5 (13 items)
   FM6OpParamItemOp5AmpInput, FM6OpParamItemOp5FreqInput, FM6OpParamItemOp5Ratio,
   FM6OpParamItemOp5Fine, FM6OpParamItemOp5Wave, FM6OpParamItemOp5Level,
   FM6OpParamItemOp5AmpDepth, FM6OpParamItemOp5FreqDepth, FM6OpParamItemOp5Delay,
   FM6OpParamItemOp5Attack, FM6OpParamItemOp5Decay, FM6OpParamItemOp5Sustain, FM6OpParamItemOp5Release,
   // Operator 6 (13 items)
   FM6OpParamItemOp6AmpInput, FM6OpParamItemOp6FreqInput, FM6OpParamItemOp6Ratio,
   FM6OpParamItemOp6Fine, FM6OpParamItemOp6Wave, FM6OpParamItemOp6Level,
   FM6OpParamItemOp6AmpDepth, FM6OpParamItemOp6FreqDepth, FM6OpParamItemOp6Delay,
   FM6OpParamItemOp6Attack, FM6OpParamItemOp6Decay, FM6OpParamItemOp6Sustain, FM6OpParamItemOp6Release,
   FM6OpParamItemPitch,           // global coarse transpose (semitones)
   FM6OpParamItemFine,            // global fine tune (cents)
   FM6OpParamItemFMInput,         // connection: linear FM CV → added to fundamental Hz (× FMLevel)
   FM6OpParamItemFMLevel,         // FM input depth (Hz per unit input)
   FM6OpParamItemFeedbackInput,   // connection: feedback-amount mod CV (× FeedbackDepth)
   FM6OpParamItemFeedbackDepth,   // feedback-mod input depth
   FM6OpParamItemOversample,      // anti-alias quality: 0 = Off, 1 = 2×, 2 = 4× (CPU cost only when on)
   // New items go here (append-only: presets and the plugin param table key off these).
   FM6OpParamItemVelFloor,        // indexed: velocity-to-level y-intercept, 0% / 25% / 50% / 75% / 100% (fixed)
   FM6OpParamItemVelCurve,        // indexed: velocity-to-level shape, 0 Concave / 1 Linear / 2 Convex
   // Role-based macros (Character panel). Each is neutral at its default: no change to the sound.
   FM6OpParamItemBrightness,      // -1..1: scales every modulator's level (index)
   FM6OpParamItemBite,            //  0..1: faster modulator attack/decay, lower modulator sustain
   FM6OpParamItemGrit,            //  0..1: more feedback + the highest-ratio modulator pushed harder
   FM6OpParamItemBody,            // -1..1: tilts carrier levels toward low-ratio (+) or high-ratio (-) carriers
   // Response (neutral at default)
   FM6OpParamItemTouch,           //  0..1: velocity also scales the modulators (play harder = brighter)
   FM6OpParamItemKeyTrack,        // -1..1: + higher notes darker and faster-decaying; - the reverse
   // Shared (relative) envelopes: carriers ("Loudness") and modulators ("Brightness"). Times are
   // multipliers on every operator of that role (×0.01..×1000, 1 = unchanged; results capped at
   // the operator limits, attack 20 s, decay/release 30 s); sustain is an offset
   // (-1..1, 0 = unchanged; + toward full, - toward zero).
   FM6OpParamItemLoudAttack, FM6OpParamItemLoudDecay, FM6OpParamItemLoudSustain, FM6OpParamItemLoudRelease,
   FM6OpParamItemBrightAttack, FM6OpParamItemBrightDecay, FM6OpParamItemBrightSustain, FM6OpParamItemBrightRelease,
   // Expression mod inputs (appended). Brightness: every modulator's level × 2^(1.5·level·input),
   // the Brightness macro's modulator half, per sample. Level: carriers × a swell gain — level a ≥ 0:
   // 1 - a·(1 - input) (quiet until pressed); a < 0: 1 + a·input (pressing makes it quieter).
   FM6OpParamItemBrightModInput,  // connection (pressure)
   FM6OpParamItemBrightModLevel,  // -1..1
   FM6OpParamItemBrightMod2Input, // connection (timbre)
   FM6OpParamItemBrightMod2Level, // -1..1
   FM6OpParamItemLevelModInput,   // connection (pressure)
   FM6OpParamItemLevelModLevel,   // -1..1
   // Character (appended). 0..1: each note's own small random pitch and modulator-index offset,
   // drawn at note-on; 0 = every note identical.
   FM6OpParamItemRealism,
   FM6OpParamItemCount_           // one past the last item — must stay <= MODULE_MAX_PARAMS + 1
};

struct ModuleParamConstants {
   const char*               name;
   uint8_t                   item;
   AudioUnitParameterUnit    unit;
   AudioUnitParameterOptions flags;
   const char* const*        valueStrings; // null or null-terminated list (stepped params)
   AudioUnitParameterValue   minValue;
   AudioUnitParameterValue   maxValue;
   AudioUnitParameterValue   defValue;
   uint64_t                  paramInfo;
};

struct ModuleParamConstantsGroup {
   char*                       name;
   size_t                      count;
   const ModuleParamConstants* items;
};

#endif // PM_PARAMS_H
