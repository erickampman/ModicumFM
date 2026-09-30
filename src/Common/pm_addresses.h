// pm_addresses.h — the FeM voice's module + input addresses. Rather than vendor the large
// ExtensionParameterAddresses.h, we define just the ones the fixed patch needs. Each is
// MODULE_PARAM_ADDRESS(kind, instance, item) — the same packed value used as the CLAP clap_id.
//
// Voice: FM6Op -> LPF (ADSR 1 + channel pressure) -> Chorus. The operator envelopes are the amp
// envelope (DX-style), so there is no separate VCA.

#ifndef PM_ADDRESSES_H
#define PM_ADDRESSES_H

#include "ParamAddressMacros.h"
#include "pm_compat.h"
#include "pm_params.h"

#define FEM_ADDR(name, kind, inst, item) \
   inline constexpr AUParameterAddress name = MODULE_PARAM_ADDRESS(kind, inst, item);

// Pitch: note pitch + pitch bend, summed (Combine 2, Add) into the FM6Op pitch input.
FEM_ADDR(pitchConverter1Address,  ModuleParamKindPitchConverter, ModuleInstance1, 0)
FEM_ADDR(pitchBend1Address,       ModuleParamKindPitchBendConverter, ModuleInstance1, 0)
FEM_ADDR(pitchBend1RangeAddress,  ModuleParamKindPitchBendConverter, ModuleInstance1, PitchBendConverterParamItemQuarterTones)
FEM_ADDR(combine2Address,         ModuleParamKindCombine, ModuleInstance2, 0)
FEM_ADDR(combine2Input1Address,   ModuleParamKindCombine, ModuleInstance2, CombineParamItemInput1)
FEM_ADDR(combine2Input2Address,   ModuleParamKindCombine, ModuleInstance2, CombineParamItemInput2)
FEM_ADDR(combine2TypeAddress,     ModuleParamKindCombine, ModuleInstance2, CombineParamItemType)

// The FM engine.
FEM_ADDR(fm6Op1Address,           ModuleParamKindFM6Op, ModuleInstance1, 0)
FEM_ADDR(fm6Op1PitchInputAddress, ModuleParamKindFM6Op, ModuleInstance1, FM6OpParamItemPitchInput)

// Low-pass filter: cutoff mod 1 = filter envelope (ADSR 1), cutoff mod 2 = channel pressure.
FEM_ADDR(lpf1Address,             ModuleParamKindButterworthLowPassFilter, ModuleInstance1, 0)
FEM_ADDR(lpf1InputAddress,        ModuleParamKindButterworthLowPassFilter, ModuleInstance1, LPFParamItemInput)
FEM_ADDR(lpf1FreqModInputAddress, ModuleParamKindButterworthLowPassFilter, ModuleInstance1, LPFParamItemModFreqInput)
FEM_ADDR(lpf1FreqModInput2Address, ModuleParamKindButterworthLowPassFilter, ModuleInstance1, LPFParamItemModFreqInput2)
FEM_ADDR(adsr1Address,            ModuleParamKindADSR, ModuleInstance1, 0)
FEM_ADDR(channelPressure1Address, ModuleParamKindChannelPressure, ModuleInstance1, 0)

// Chorus, delay-modulated by one LFO shared across voices.
FEM_ADDR(chorus1Address,          ModuleParamKindChorus, ModuleInstance1, 0)
FEM_ADDR(chorus1InputAddress,     ModuleParamKindChorus, ModuleInstance1, ChorusParamItemInput)
FEM_ADDR(chorus1ModInputAddress,  ModuleParamKindChorus, ModuleInstance1, ChorusParamItemModDelayInput)
FEM_ADDR(sharedLFO1Address,       ModuleParamKindSharedLFO, ModuleInstance1, 0)

FEM_ADDR(finalAddress,            ModuleParamKindFinalOutput, ModuleInstance1, 0)
FEM_ADDR(finalInputAddress,       ModuleParamKindFinalOutput, ModuleInstance1, FinalOutputParamItemInput)

#undef FEM_ADDR

#endif // PM_ADDRESSES_H
