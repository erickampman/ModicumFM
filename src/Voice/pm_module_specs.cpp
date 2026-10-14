// pm_module_specs.cpp — the ModicumFM module registry. getModuleInitSpecs() lists exactly the modules
// the fixed voice instantiates (wired in VoiceManager::buildDefaultPatch). Each entry: {address,
// param group, init/deinit/process/midi/param/reset/sampleRate procs[, shared-state create proc]}.

#include "ModuleInitSpecs.hpp"

#include "ADSRModule.hpp"
#include "CCConverterModule.hpp"
#include "ChannelPressureModule.hpp"
#include "ChorusModule.hpp"
#include "CombineModule.hpp"
#include "FM6OpModule.hpp"
#include "FinalModule.hpp"
#include "LPFilterModule.hpp"
#include "PitchBendConverterModule.hpp"
#include "PitchConverterModule.hpp"
#include "SharedLFOModule.hpp"
#include "pm_addresses.h"
#include "pm_param_data.h"

const std::vector<ModuleInitSpec>& getModuleInitSpecs() {
   static const std::vector<ModuleInitSpec> specs = {
      // Pitch: note + bend, summed by Combine 2 (Add).
      {pitchConverter1Address, &pitchConverterModuleConstantsGroup, InitPitchConverter,
       DeinitPitchConverter, ProcessPitchConverter, MIDIPitchConverter, nullptr, nullptr, nullptr},
      {pitchBend1Address, &pitchBendConverterConstantsGroup, InitPitchBendConverter,
       DeinitPitchBendConverter, ProcessPitchBendConverter, MIDIPitchBendConverter, nullptr,
       ResetPitchBendConverter, nullptr},
      {combine2Address, &combineConstantsGroup, nullptr, nullptr, ProcessCombineModule, nullptr,
       nullptr, nullptr, nullptr},
      // The FM engine.
      {fm6Op1Address, &fm6OpConstantsGroup, InitFM6OpModule, DeinitFM6OpModule, ProcessFM6OpModule,
       MIDIFM6OpModule, nullptr, ResetFM6OpModule, SampleRateFM6OpModule},
      // Filter + its envelope and pressure mod.
      {lpf1Address, &lpfConstantsGroup, InitLPFilterModule, DeinitLPFilterModule,
       ProcessLPFilterModule, nullptr, nullptr, ResetLPFilterModule, SampleRateLPFilterModule},
      {adsr1Address, &adsrConstantsGroup, InitADSRModule, DeinitADSRModule, ProcessADSRModule,
       MIDIProcADSRModule, ParamADSR, ADSRModuleReset, SampleRateADSRModule},
      {channelPressure1Address, &channelPressureConstantsGroup, InitChannelPressure,
       DeinitChannelPressure, ProcessChannelPressure, MIDIChannelPressure, nullptr,
       ResetChannelPressure, nullptr},
      {timbre1Address, &ccConverterConstantsGroup, InitCCConverter, DeinitCCConverter,
       ProcessCCConverter, MIDICCConverter, nullptr, ResetCCConverter, nullptr},
      // Chorus; one LFO phase shared by every voice so all the per-voice choruses sweep together.
      {chorus1Address, &chorusConstantsGroup, InitChorusModule, DeinitChorusModule, ProcessChorusModule,
       nullptr, ParamChorus, ResetChorusModule, SampleRateChorusModule},
      {sharedLFO1Address, &sharedLFOConstantsGroup, nullptr, nullptr, ProcessSharedLFOModule,
       nullptr, nullptr, nullptr, nullptr, CreateSharedLFOState},
      {finalAddress, &finalConstantsGroup, InitFinalModule, DeinitFinalModule, ProcessFinalModule,
       MIDIFinalModule, nullptr, nullptr, SampleRateFinalModule},
   };
   return specs;
}
