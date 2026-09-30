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
   ModuleParamKindFM4Op,
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

// --- TwinFM: FM4Op voice modules (ported from PolygraphModularUnified) ---
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

enum QuadStereoMixerParamItem : NSUInteger {
   QuadStereoMixerParamItemInput1 = 1,   // connection
   QuadStereoMixerParamItemInput2,       // connection
   QuadStereoMixerParamItemInput3,       // connection
   QuadStereoMixerParamItemInput4,       // connection
   QuadStereoMixerParamItemLevel1,
   QuadStereoMixerParamItemLevel2,
   QuadStereoMixerParamItemLevel3,
   QuadStereoMixerParamItemLevel4,
   QuadStereoMixerParamItemPan1,
   QuadStereoMixerParamItemPan2,
   QuadStereoMixerParamItemPan3,
   QuadStereoMixerParamItemPan4,
};

enum PitchBendConverterParamItem : NSUInteger {
   PitchBendConverterParamItemQuarterTones = 1, // indexed bend range, 0..8 quarter-tones
};

enum ChannelPressureParamItem : NSUInteger {
   ChannelPressureParamItemDummy = 1,  // no params needed; one dummy slot for UI storage
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
};

enum FM4OpParamItem : NSUInteger {
   // Global
   FM4OpParamItemAlgorithm = 1,   // indexed: 0..7 (operator routing)
   FM4OpParamItemFeedback,        // feedback amount (top op), 0..1
   FM4OpParamItemOutputLevel,     // master out
   FM4OpParamItemPitchInput,      // connection: fundamental pitch CV (oct re A440); MIDI note if unpatched
   FM4OpParamItemGateInput,       // connection: gate CV (rising=trigger, falling=release); overrides MIDI
   FM4OpParamItemRetrigger,       // indexed: 0=Steal, 1=LegatoRetrigger, 2=LegatoTrue
   FM4OpParamItemDetune,          // L/R stereo detune (0..1 -> cents)
   FM4OpParamItemSpread,          // stereo width (0=mono, 1=full L/R)
   // Operator 1 (13 items)
   FM4OpParamItemOp1AmpInput, FM4OpParamItemOp1FreqInput, FM4OpParamItemOp1Ratio,
   FM4OpParamItemOp1Fine, FM4OpParamItemOp1Wave, FM4OpParamItemOp1Level,
   FM4OpParamItemOp1AmpDepth, FM4OpParamItemOp1FreqDepth, FM4OpParamItemOp1Delay,
   FM4OpParamItemOp1Attack, FM4OpParamItemOp1Decay, FM4OpParamItemOp1Sustain, FM4OpParamItemOp1Release,
   // Operator 2 (13 items)
   FM4OpParamItemOp2AmpInput, FM4OpParamItemOp2FreqInput, FM4OpParamItemOp2Ratio,
   FM4OpParamItemOp2Fine, FM4OpParamItemOp2Wave, FM4OpParamItemOp2Level,
   FM4OpParamItemOp2AmpDepth, FM4OpParamItemOp2FreqDepth, FM4OpParamItemOp2Delay,
   FM4OpParamItemOp2Attack, FM4OpParamItemOp2Decay, FM4OpParamItemOp2Sustain, FM4OpParamItemOp2Release,
   // Operator 3 (13 items)
   FM4OpParamItemOp3AmpInput, FM4OpParamItemOp3FreqInput, FM4OpParamItemOp3Ratio,
   FM4OpParamItemOp3Fine, FM4OpParamItemOp3Wave, FM4OpParamItemOp3Level,
   FM4OpParamItemOp3AmpDepth, FM4OpParamItemOp3FreqDepth, FM4OpParamItemOp3Delay,
   FM4OpParamItemOp3Attack, FM4OpParamItemOp3Decay, FM4OpParamItemOp3Sustain, FM4OpParamItemOp3Release,
   // Operator 4 (13 items)
   FM4OpParamItemOp4AmpInput, FM4OpParamItemOp4FreqInput, FM4OpParamItemOp4Ratio,
   FM4OpParamItemOp4Fine, FM4OpParamItemOp4Wave, FM4OpParamItemOp4Level,
   FM4OpParamItemOp4AmpDepth, FM4OpParamItemOp4FreqDepth, FM4OpParamItemOp4Delay,
   FM4OpParamItemOp4Attack, FM4OpParamItemOp4Decay, FM4OpParamItemOp4Sustain, FM4OpParamItemOp4Release,
   // Appended (preset-safe: name-keyed loads, no-op defaults).
   FM4OpParamItemPitch,           // global coarse transpose (semitones)
   FM4OpParamItemFine,            // global fine tune (cents)
   FM4OpParamItemFMInput,         // connection: linear FM CV → added to fundamental Hz (× FMLevel)
   FM4OpParamItemFMLevel,         // FM input depth (Hz per unit input)
   FM4OpParamItemFeedbackInput,   // connection: feedback-amount mod CV (× FeedbackDepth)
   FM4OpParamItemFeedbackDepth,   // feedback-mod input depth
   FM4OpParamItemOversample,      // anti-alias quality: 0 = Off, 1 = 2×, 2 = 4× (CPU cost only when on)
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
