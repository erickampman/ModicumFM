// pm_addresses.h — the TwinFM voice's module + input addresses. Rather than vendor the large
// ExtensionParameterAddresses.h, we define just the ones the fixed patch needs. Each is
// MODULE_PARAM_ADDRESS(kind, instance, item) — the same packed value used as the CLAP clap_id.
//
// Voice: dual FM4Op -> stereo mixer -> LPF (ADSR 1 + channel pressure) -> VCA (ADSR 2) -> Chorus.

#ifndef PM_ADDRESSES_H
#define PM_ADDRESSES_H

#include "ParamAddressMacros.h"
#include "pm_compat.h"
#include "pm_params.h"

#define TWINFM_ADDR(name, kind, inst, item) \
   inline constexpr AUParameterAddress name = MODULE_PARAM_ADDRESS(kind, inst, item);

// Pitch: note pitch + pitch bend, summed (Combine 2, Add) into both FM4Op pitch inputs.
TWINFM_ADDR(pitchConverter1Address,  ModuleParamKindPitchConverter, ModuleInstance1, 0)
TWINFM_ADDR(pitchBend1Address,       ModuleParamKindPitchBendConverter, ModuleInstance1, 0)
TWINFM_ADDR(pitchBend1RangeAddress,  ModuleParamKindPitchBendConverter, ModuleInstance1, PitchBendConverterParamItemQuarterTones)
TWINFM_ADDR(combine2Address,         ModuleParamKindCombine, ModuleInstance2, 0)
TWINFM_ADDR(combine2Input1Address,   ModuleParamKindCombine, ModuleInstance2, CombineParamItemInput1)
TWINFM_ADDR(combine2Input2Address,   ModuleParamKindCombine, ModuleInstance2, CombineParamItemInput2)
TWINFM_ADDR(combine2TypeAddress,     ModuleParamKindCombine, ModuleInstance2, CombineParamItemType)

// The two FM engines (A = instance 1, B = instance 2).
TWINFM_ADDR(fm4Op1Address,           ModuleParamKindFM4Op, ModuleInstance1, 0)
TWINFM_ADDR(fm4Op1PitchInputAddress, ModuleParamKindFM4Op, ModuleInstance1, FM4OpParamItemPitchInput)
TWINFM_ADDR(fm4Op2Address,           ModuleParamKindFM4Op, ModuleInstance2, 0)
TWINFM_ADDR(fm4Op2PitchInputAddress, ModuleParamKindFM4Op, ModuleInstance2, FM4OpParamItemPitchInput)

// Stereo mixer: engine A on input 1, engine B on input 2 (level + pan each).
TWINFM_ADDR(mixer1Address,           ModuleParamKindQuadStereoMixer, ModuleInstance1, 0)
TWINFM_ADDR(mixer1Input1Address,     ModuleParamKindQuadStereoMixer, ModuleInstance1, QuadStereoMixerParamItemInput1)
TWINFM_ADDR(mixer1Input2Address,     ModuleParamKindQuadStereoMixer, ModuleInstance1, QuadStereoMixerParamItemInput2)

// Low-pass filter: cutoff mod 1 = filter envelope (ADSR 1), cutoff mod 2 = channel pressure.
TWINFM_ADDR(lpf1Address,             ModuleParamKindButterworthLowPassFilter, ModuleInstance1, 0)
TWINFM_ADDR(lpf1InputAddress,        ModuleParamKindButterworthLowPassFilter, ModuleInstance1, LPFParamItemInput)
TWINFM_ADDR(lpf1FreqModInputAddress, ModuleParamKindButterworthLowPassFilter, ModuleInstance1, LPFParamItemModFreqInput)
TWINFM_ADDR(lpf1FreqModInput2Address, ModuleParamKindButterworthLowPassFilter, ModuleInstance1, LPFParamItemModFreqInput2)
TWINFM_ADDR(adsr1Address,            ModuleParamKindADSR, ModuleInstance1, 0)
TWINFM_ADDR(channelPressure1Address, ModuleParamKindChannelPressure, ModuleInstance1, 0)

// VCA: Combine 1 (Multiply) = filter output x amp envelope (ADSR 2).
TWINFM_ADDR(combine1Address,         ModuleParamKindCombine, ModuleInstance1, 0)
TWINFM_ADDR(combine1Input1Address,   ModuleParamKindCombine, ModuleInstance1, CombineParamItemInput1)
TWINFM_ADDR(combine1Input2Address,   ModuleParamKindCombine, ModuleInstance1, CombineParamItemInput2)
TWINFM_ADDR(combine1TypeAddress,     ModuleParamKindCombine, ModuleInstance1, CombineParamItemType)
TWINFM_ADDR(adsr2Address,            ModuleParamKindADSR, ModuleInstance2, 0)

// Chorus, delay-modulated by one LFO shared across voices.
TWINFM_ADDR(chorus1Address,          ModuleParamKindChorus, ModuleInstance1, 0)
TWINFM_ADDR(chorus1InputAddress,     ModuleParamKindChorus, ModuleInstance1, ChorusParamItemInput)
TWINFM_ADDR(chorus1ModInputAddress,  ModuleParamKindChorus, ModuleInstance1, ChorusParamItemModDelayInput)
TWINFM_ADDR(sharedLFO1Address,       ModuleParamKindSharedLFO, ModuleInstance1, 0)

TWINFM_ADDR(finalAddress,            ModuleParamKindFinalOutput, ModuleInstance1, 0)
TWINFM_ADDR(finalInputAddress,       ModuleParamKindFinalOutput, ModuleInstance1, FinalOutputParamItemInput)

#undef TWINFM_ADDR

#endif // PM_ADDRESSES_H
