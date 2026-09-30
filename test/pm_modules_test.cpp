// pm_modules_test — brings up the small utility modules off Apple.
// PitchConverter is tested functionally (MIDI note → pitch output). Combine/Final need
// inputs wired (via Voice), so here they are checked to compile + run + produce finite output;
// their signal behaviour is exercised end-to-end once the voice engine lands.

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

   if (g_fails == 0)
      std::printf("ALL PASS\n");
   return g_fails ? 1 : 0;
}
