// pm_adsr_test — runs the real vendored ADSR envelope off Apple. A MIDI note-on drives the
// attack (envelope rises); a note-off drives the release (envelope decays toward 0).

#include "ADSRModule.hpp"
#include "Module.hpp"
#include "pm_midi.h"
#include "pm_param_data.h"
#include "pm_params.h"

#include <cstdio>

static int g_fails = 0;
#define CHECK(cond, msg)                                                                           \
   do {                                                                                            \
      if (!(cond)) {                                                                               \
         std::printf("FAIL: %s\n", (msg));                                                         \
         ++g_fails;                                                                                \
      }                                                                                            \
   } while (0)

int main() {
   const double sr = 48000.0;
   const uint32_t N = 512;
   const AUParameterAddress id = MODULE_PARAM_ADDRESS(ModuleParamKindADSR, ModuleInstance1, 0);

   Module m(id, sr, N, &adsrConstantsGroup, InitADSRModule, DeinitADSRModule, ProcessADSRModule,
            MIDIProcADSRModule, ParamADSR, nullptr, SampleRateADSRModule);
   m.addParams(&adsrConstantsGroup);

   // Push fast envelope times into the helper the way Voice::setParameter does (via paramProc).
   auto setParam = [&](ADSRParamItem item, float v) {
      m.params[item - 1] = v;
      ParamADSR(&m, MODULE_PARAM_FROM_BASE_AND_ITEM(id, item), v);
   };
   setParam(ADSRParamItemAttack, 0.005f);
   setParam(ADSRParamItemDecay, 0.05f);
   setParam(ADSRParamItemSustain, 0.5f);
   setParam(ADSRParamItemRelease, 0.05f);

   // Note on → attack.
   pm::UmpMessage on = pm::makeNoteOn(69, 0x8000);
   MIDIProcADSRModule(on, &m);
   float peak = 0.0f;
   for (uint32_t b = 0; b < 3; ++b) {
      ProcessADSRModule(1 + b, N, &m);
      for (uint32_t i = 0; i < N; ++i)
         peak = std::fmax(peak, m.output.first->data[i]);
   }
   CHECK(peak > 0.4f, "note-on drives attack (envelope rises)");
   CHECK(peak <= 1.01f, "envelope bounded to [0,1]");

   // Note off → release.
   pm::UmpMessage off = pm::makeNoteOff(69);
   MIDIProcADSRModule(off, &m);
   for (uint32_t b = 0; b < 20; ++b)
      ProcessADSRModule(100 + b, N, &m);
   const float finalVal = m.output.first->data[N - 1];
   CHECK(finalVal < 0.05f, "note-off drives release (envelope decays to ~0)");

   std::printf("ADSR: peak=%.3f, released=%.4f\n", peak, finalVal);
   if (g_fails == 0)
      std::printf("ALL PASS\n");
   return g_fails ? 1 : 0;
}
