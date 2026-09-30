// pm_module_specs.cpp — the TwinFM module registry. getModuleInitSpecs() lists exactly the modules
// the fixed voice instantiates (wired in VoiceManager::buildDefaultPatch). Each entry: {address,
// param group, init/deinit/process/midi/param/reset/sampleRate procs[, shared-state create proc]}.

#include "ModuleInitSpecs.hpp"

#include "ADSRModule.hpp"
#include "ChannelPressureModule.hpp"
#include "ChorusModule.hpp"
#include "CombineModule.hpp"
#include "FM4OpModule.hpp"
#include "FinalModule.hpp"
#include "LPFilterModule.hpp"
#include "PitchBendConverterModule.hpp"
#include "PitchConverterModule.hpp"
#include "QuadStereoMixerModule.hpp"
#include "SharedLFOModule.hpp"
#include "pm_addresses.h"
#include "pm_param_data.h"

const std::vector<ModuleInitSpec>& getModuleInitSpecs() {
   static const std::vector<ModuleInitSpec> specs = {
      // Pitch: note + bend, summed by Combine 2 (Add).
      {pitchConverter1Address, &pitchConverterModuleConstantsGroup, InitPitchConverter,
       DeinitPitchConverter, ProcessPitchConverter, MIDIPitchConverter, nullptr, nullptr, nullptr},
      {pitchBend1Address, &pitchBendConverterConstantsGroup, InitPitchBendConverter,
       DeinitPitchBendConverter, ProcessPitchBendConverter, MIDIPitchBendConverter, nullptr, nullptr, nullptr},
      {combine2Address, &combineConstantsGroup, nullptr, nullptr, ProcessCombineModule, nullptr,
       nullptr, nullptr, nullptr},
      // FM engines A and B.
      {fm4Op1Address, &fm4OpConstantsGroup, InitFM4OpModule, DeinitFM4OpModule, ProcessFM4OpModule,
       MIDIFM4OpModule, nullptr, ResetFM4OpModule, SampleRateFM4OpModule},
      {fm4Op2Address, &fm4OpConstantsGroup, InitFM4OpModule, DeinitFM4OpModule, ProcessFM4OpModule,
       MIDIFM4OpModule, nullptr, ResetFM4OpModule, SampleRateFM4OpModule},
      {mixer1Address, &quadStereoMixerConstantsGroup, nullptr, nullptr, ProcessQuadStereoMixerModule,
       nullptr, nullptr, nullptr, nullptr},
      // Filter + its envelope and pressure mod.
      {lpf1Address, &lpfConstantsGroup, InitLPFilterModule, DeinitLPFilterModule,
       ProcessLPFilterModule, nullptr, nullptr, nullptr, SampleRateLPFilterModule},
      {adsr1Address, &adsrConstantsGroup, InitADSRModule, DeinitADSRModule, ProcessADSRModule,
       MIDIProcADSRModule, ParamADSR, nullptr, SampleRateADSRModule},
      {channelPressure1Address, &channelPressureConstantsGroup, InitChannelPressure,
       DeinitChannelPressure, ProcessChannelPressure, MIDIChannelPressure, nullptr, nullptr, nullptr},
      // VCA + amp envelope.
      {combine1Address, &combineConstantsGroup, nullptr, nullptr, ProcessCombineModule, nullptr,
       nullptr, nullptr, nullptr},
      {adsr2Address, &adsrConstantsGroup, InitADSRModule, DeinitADSRModule, ProcessADSRModule,
       MIDIProcADSRModule, ParamADSR, nullptr, SampleRateADSRModule},
      // Chorus; one LFO phase shared by every voice so all the per-voice choruses sweep together.
      {chorus1Address, &chorusConstantsGroup, InitChorusModule, DeinitChorusModule, ProcessChorusModule,
       nullptr, ParamChorus, nullptr, SampleRateChorusModule},
      {sharedLFO1Address, &sharedLFOConstantsGroup, nullptr, nullptr, ProcessSharedLFOModule,
       nullptr, nullptr, nullptr, nullptr, CreateSharedLFOState},
      {finalAddress, &finalConstantsGroup, InitFinalModule, DeinitFinalModule, ProcessFinalModule,
       MIDIFinalModule, nullptr, nullptr, SampleRateFinalModule},
   };
   return specs;
}
