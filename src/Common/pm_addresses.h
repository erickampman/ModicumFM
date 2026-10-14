// pm_addresses.h — the ModicumFM voice's module + input addresses. Rather than vendor the large
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

#define MODICUMFM_ADDR(name, kind, inst, item) \
   inline constexpr AUParameterAddress name = MODULE_PARAM_ADDRESS(kind, inst, item);

// Pitch: note pitch + pitch bend, summed (Combine 2, Add) into the FM6Op pitch input.
MODICUMFM_ADDR(pitchConverter1Address,  ModuleParamKindPitchConverter, ModuleInstance1, 0)
MODICUMFM_ADDR(pitchBend1Address,       ModuleParamKindPitchBendConverter, ModuleInstance1, 0)
MODICUMFM_ADDR(pitchBend1RangeAddress,  ModuleParamKindPitchBendConverter, ModuleInstance1, PitchBendConverterParamItemQuarterTones)
MODICUMFM_ADDR(combine2Address,         ModuleParamKindCombine, ModuleInstance2, 0)
MODICUMFM_ADDR(combine2Input1Address,   ModuleParamKindCombine, ModuleInstance2, CombineParamItemInput1)
MODICUMFM_ADDR(combine2Input2Address,   ModuleParamKindCombine, ModuleInstance2, CombineParamItemInput2)
MODICUMFM_ADDR(combine2TypeAddress,     ModuleParamKindCombine, ModuleInstance2, CombineParamItemType)

// The FM engine.
MODICUMFM_ADDR(fm6Op1Address,           ModuleParamKindFM6Op, ModuleInstance1, 0)
MODICUMFM_ADDR(fm6Op1PitchInputAddress, ModuleParamKindFM6Op, ModuleInstance1, FM6OpParamItemPitchInput)

// Low-pass filter: cutoff mod 1 = filter envelope (ADSR 1), cutoff mod 2 = channel pressure.
MODICUMFM_ADDR(lpf1Address,             ModuleParamKindButterworthLowPassFilter, ModuleInstance1, 0)
MODICUMFM_ADDR(lpf1InputAddress,        ModuleParamKindButterworthLowPassFilter, ModuleInstance1, LPFParamItemInput)
MODICUMFM_ADDR(lpf1FreqModInputAddress, ModuleParamKindButterworthLowPassFilter, ModuleInstance1, LPFParamItemModFreqInput)
MODICUMFM_ADDR(lpf1FreqModInput2Address, ModuleParamKindButterworthLowPassFilter, ModuleInstance1, LPFParamItemModFreqInput2)
MODICUMFM_ADDR(adsr1Address,            ModuleParamKindADSR, ModuleInstance1, 0)
MODICUMFM_ADDR(channelPressure1Address, ModuleParamKindChannelPressure, ModuleInstance1, 0)
MODICUMFM_ADDR(lpf1FreqModInput3Address, ModuleParamKindButterworthLowPassFilter, ModuleInstance1, LPFParamItemModFreqInput3)

// Expression: Pressure (channel pressure above) and Timbre (CC74 / per-note #74, a CCConverter in
// Per Note mode) into the FM engine's brightness and level mod inputs and the filter's third cutoff
// mod input. The plugin's Expression knobs are those inputs' levels, all 0 by default.
MODICUMFM_ADDR(timbre1Address,          ModuleParamKindControlChangeConverter, ModuleInstance1, 0)
MODICUMFM_ADDR(fm6Op1BrightModInputAddress,  ModuleParamKindFM6Op, ModuleInstance1, FM6OpParamItemBrightModInput)
MODICUMFM_ADDR(fm6Op1BrightMod2InputAddress, ModuleParamKindFM6Op, ModuleInstance1, FM6OpParamItemBrightMod2Input)
MODICUMFM_ADDR(fm6Op1LevelModInputAddress,   ModuleParamKindFM6Op, ModuleInstance1, FM6OpParamItemLevelModInput)

// Chorus, delay-modulated by one LFO shared across voices.
MODICUMFM_ADDR(chorus1Address,          ModuleParamKindChorus, ModuleInstance1, 0)
MODICUMFM_ADDR(chorus1InputAddress,     ModuleParamKindChorus, ModuleInstance1, ChorusParamItemInput)
MODICUMFM_ADDR(chorus1ModInputAddress,  ModuleParamKindChorus, ModuleInstance1, ChorusParamItemModDelayInput)
MODICUMFM_ADDR(sharedLFO1Address,       ModuleParamKindSharedLFO, ModuleInstance1, 0)

MODICUMFM_ADDR(finalAddress,            ModuleParamKindFinalOutput, ModuleInstance1, 0)
MODICUMFM_ADDR(finalInputAddress,       ModuleParamKindFinalOutput, ModuleInstance1, FinalOutputParamItemInput)

#undef MODICUMFM_ADDR

#endif // PM_ADDRESSES_H
