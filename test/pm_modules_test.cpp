// pm_modules_test — brings up the small utility modules off Apple.
// PitchConverter is tested functionally (MIDI note → pitch output). Combine/Final need
// inputs wired (via Voice), so here they are checked to compile + run + produce finite output;
// their signal behaviour is exercised end-to-end once the voice engine lands.

#include "ChannelPressureModule.hpp"
#include "CombineModule.hpp"
#include "FinalModule.hpp"
#include "Module.hpp"
#include "PitchConverter.hpp"
#include "PitchConverterModule.hpp"
#include "pm_midi.h"
#include "pm_param_data.h"
#include "pm_params.h"

#include <cmath>
#include <cstdio>

static int g_fails = 0;
#define CHECK(cond, msg)                                                                           \
   do {                                                                                            \
      if (!(cond)) {                                                                               \
         std::printf("FAIL: %s\n", (msg));                                                         \
         ++g_fails;                                                                                \
      }                                                                                            \
   } while (0)

static bool finiteBuf(const float* b, uint32_t n) {
   for (uint32_t i = 0; i < n; ++i)
      if (!std::isfinite(b[i]))
         return false;
   return true;
}

int main() {
   const double sr = 48000.0;
   const uint32_t N = 512;

   // --- PitchConverter: MIDI note → pitch (1/octave). Functional. ---
   {
      const AUParameterAddress id =
         MODULE_PARAM_ADDRESS(ModuleParamKindPitchConverter, ModuleInstance1, 0);
      Module m(id, sr, N, &pitchConverterModuleConstantsGroup, InitPitchConverter,
               DeinitPitchConverter, ProcessPitchConverter, MIDIPitchConverter);
      m.addParams(&pitchConverterModuleConstantsGroup);
      PitchConverter pc(440.0);
      m.setPitchConverter(&pc);

      MIDIPitchConverter(pm::makeNoteOn(81, 0x8000), &m); // A5 = one octave above A4
      ProcessPitchConverter(1, N, &m);
      CHECK(std::fabs(m.output.first->data[0] - 1.0f) < 1e-4f, "PitchConverter A5 → pitch 1.0");

      MIDIPitchConverter(pm::makeNoteOn(69, 0x8000), &m); // A4 = reference
      ProcessPitchConverter(2, N, &m);
      CHECK(std::fabs(m.output.first->data[0] - 0.0f) < 1e-4f, "PitchConverter A4 → pitch 0.0");
   }

   // --- Combine (VCA): compiles + runs; Multiply type + level 1.0 from defaults. ---
   {
      const AUParameterAddress id = MODULE_PARAM_ADDRESS(ModuleParamKindCombine, ModuleInstance1, 0);
      Module m(id, sr, N, &combineConstantsGroup, nullptr, nullptr, ProcessCombineModule);
      m.addParams(&combineConstantsGroup);
      CHECK(m.getParamValueAsInt(CombineParamItemType) == ParamItemCombineType_Multiply,
            "Combine defaults to Multiply");
      ProcessCombineModule(1, N, &m);
      CHECK(finiteBuf(m.output.first->data, N), "Combine runs, finite output");
   }

   // --- Final (output stage): compiles + runs. ---
   {
      const AUParameterAddress id =
         MODULE_PARAM_ADDRESS(ModuleParamKindFinalOutput, ModuleInstance1, 0);
      Module m(id, sr, N, &finalConstantsGroup, InitFinalModule, DeinitFinalModule,
               ProcessFinalModule);
      m.addParams(&finalConstantsGroup);
      ProcessFinalModule(1, N, &m);
      CHECK(finiteBuf(m.output.first->data, N), "Final runs, finite output");
   }

   // --- ChannelPressure: released notes (MPE §3.3.3). Under MPE a released note keeps its member
   // channel's pressure and still follows the master's; with MPE off channel pressure moves it. ---
   {
      const AUParameterAddress id =
         MODULE_PARAM_ADDRESS(ModuleParamKindChannelPressure, ModuleInstance1, 0);
      auto valueAfter = [&](bool mpe, std::initializer_list<pm::UmpMessage> beforeRelease,
                            std::initializer_list<pm::UmpMessage> afterRelease) {
         Module m(id, sr, N, &channelPressureConstantsGroup, InitChannelPressure,
                  DeinitChannelPressure, ProcessChannelPressure, MIDIChannelPressure);
         m.addParams(&channelPressureConstantsGroup);
         m.params[ChannelPressureParamItemMPE - 1] = mpe ? 1.0f : 0.0f;
         uint64_t call = 1;
         for (const auto& msg : beforeRelease) MIDIChannelPressure(msg, &m);
         ProcessChannelPressure(call++, N, &m);
         m.noteReleased = true;   // as Voice::syncNoteReleased sets it after the note-off
         for (const auto& msg : afterRelease) MIDIChannelPressure(msg, &m);
         ProcessChannelPressure(call++, N, &m);   // N samples outlast the 2 ms ramp
         const float v = m.output.first->data[N - 1];
         DeinitChannelPressure(&m);
         return v;
      };
      const pm::UmpMessage memberOn   = pm::fromMidi1Bytes(0x91, 60, 100);
      const pm::UmpMessage memberHalf = pm::fromMidi1Bytes(0xD1, 64, 0);
      const pm::UmpMessage memberZero = pm::fromMidi1Bytes(0xD1, 0, 0);
      const pm::UmpMessage masterMore = pm::fromMidi1Bytes(0xD0, 96, 0);

      const float kept = valueAfter(true, {memberOn, memberHalf}, {memberZero});
      std::printf("ChannelPressure: MPE member pressure after release %.3f\n", kept);
      CHECK(std::fabs(kept - 0.5f) < 0.01f, "MPE: a released note keeps its member pressure");
      const float master = valueAfter(true, {memberOn, memberHalf}, {memberZero, masterMore});
      CHECK(std::fabs(master - 0.75f) < 0.01f, "MPE: the master channel's pressure still reaches a released note");
      const float off = valueAfter(false, {pm::fromMidi1Bytes(0x90, 60, 100), pm::fromMidi1Bytes(0xD0, 64, 0)},
                                   {pm::fromMidi1Bytes(0xD0, 0, 0)});
      CHECK(std::fabs(off) < 0.01f, "MPE off: channel pressure still moves a releasing note");
   }

   if (g_fails == 0)
      std::printf("ALL PASS\n");
   return g_fails ? 1 : 0;
}
