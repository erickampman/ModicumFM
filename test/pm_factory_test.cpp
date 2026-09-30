// pm_factory_test — builds the whole TwinFM module set through ModuleFactory + the
// getModuleInitSpecs() registry, places them in a ModuleMap (as a Voice will), and confirms an
// FM4Op among them sounds when sent a note. Proves the factory/spec glue off Apple.

#include "Module.hpp"
#include "ModuleFactory.hpp"
#include "ModuleInitSpecs.hpp"
#include "ModuleMap.hpp"
#include "PitchConverter.hpp"
#include "pm_addresses.h"
#include "pm_midi.h"

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

int main() {
   const auto& specs = getModuleInitSpecs();
   CHECK(specs.size() == 14, "14 module specs");

   ModuleFactory factory(48000.0, 512);
   ModuleMap map;
   map.reserveModules(specs.size());
   PitchConverter pc(440.0);

   for (const auto& s : specs) {
      auto mod = factory.createModule(s.id, s.constantsGroup, s.initProc, s.deinitProc,
                                      s.processProc, s.midiProc, s.paramProc, s.resetProc,
                                      s.sampleRateProc);
      mod->setPitchConverter(&pc);
      map.add(s.id, std::move(mod));
   }
   CHECK(map.allModules().size() == 14, "14 modules built into the map");

   // Every module of the TwinFM voice is present.
   const AUParameterAddress expected[] = {
      pitchConverter1Address, pitchBend1Address, combine2Address, fm4Op1Address, fm4Op2Address,
      mixer1Address, lpf1Address, adsr1Address, channelPressure1Address, combine1Address,
      adsr2Address, chorus1Address, sharedLFO1Address, finalAddress};
   for (AUParameterAddress a : expected)
      CHECK(map.get(a) != nullptr, "voice module present");

   // A factory-built FM4Op (pitch input unpatched → follows the MIDI note) sounds after a note-on.
   Module* fm = map.get(fm4Op1Address);
   CHECK(fm && fm->processProc && fm->midiProc, "fm4Op has process + MIDI procs");
   fm->midiProc(pm::makeNoteOn(69, 0x8000), fm);
   float peak = 0.0f;
   for (int b = 0; b < 4; ++b) {
      fm->processProc(1 + b, 512, fm);
      for (int i = 0; i < 512; ++i)
         peak = std::fmax(peak, std::fabs(fm->output.first->data[i]));
   }
   CHECK(peak > 0.01f, "factory-built fm4Op sounds after note-on");

   if (g_fails == 0)
      std::printf("ALL PASS\n");
   return g_fails ? 1 : 0;
}
