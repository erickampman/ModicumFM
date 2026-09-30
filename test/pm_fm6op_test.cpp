// pm_fm6op_test — the FM6Op engine on its own (built through the factory + module specs):
//   1. the algorithm table's invariants (modulators outrank their targets, so one descending pass
//      evaluates each algorithm; feedback indices in range; at least one carrier);
//   2. the role analysis (carriers / modulator depth) on a few known algorithms;
//   3. every algorithm sounds after a note, stays finite and within ±1 (carrier normalisation);
//   4. feedback reaches the sound, including algorithm 4's cross-operator path (Op4 → Op6).

#include "FM6OpAlgorithms.hpp"
#include "Module.hpp"
#include "ModuleFactory.hpp"
#include "ModuleInitSpecs.hpp"
#include "PitchConverter.hpp"
#include "pm_addresses.h"
#include "pm_midi.h"

#include <cmath>
#include <cstdio>
#include <memory>

static int g_fails = 0;
#define CHECK(cond, msg)                                                                           \
   do {                                                                                            \
      if (!(cond)) {                                                                               \
         std::printf("FAIL: %s\n", (msg));                                                         \
         ++g_fails;                                                                                \
      }                                                                                            \
   } while (0)

static constexpr uint32_t kN = 512;

static std::unique_ptr<Module> makeFM(ModuleFactory& factory, PitchConverter& pc) {
   for (const auto& s : getModuleInitSpecs()) {
      if (s.id != fm6Op1Address) continue;
      auto mod = factory.createModule(s.id, s.constantsGroup, s.initProc, s.deinitProc,
                                      s.processProc, s.midiProc, s.paramProc, s.resetProc,
                                      s.sampleRateProc);
      mod->setPitchConverter(&pc);
      return mod;
   }
   return nullptr;
}

static void set(Module* m, uint8_t item, float v) { m->params[item - 1] = v; } // items start at 1

// Note on, render `blocks`, return peak |L| (and whether every sample was finite).
static float play(Module* m, int blocks, bool& finite, float* sumOut = nullptr) {
   m->reset();
   m->midiProc(pm::makeNoteOn(57, 0xFFFF), m);
   float peak = 0.0f, sum = 0.0f;
   finite = true;
   static uint64_t callID = 1;
   for (int b = 0; b < blocks; ++b) {
      m->processProc(callID++, kN, m);
      for (uint32_t i = 0; i < kN; ++i) {
         const float v = m->output.first->data[i];
         if (!std::isfinite(v)) finite = false;
         peak = std::fmax(peak, std::fabs(v));
         sum += std::fabs(v);
      }
   }
   if (sumOut) *sumOut = sum;
   return peak;
}

int main() {
   // --- 1: table invariants -----------------------------------------------------------------
   for (int a = 0; a < kFM6OpAlgorithms; ++a) {
      const FM6OpAlgo& al = kFM6OpAlgos[a];
      bool ordered = true;
      for (int dst = 0; dst < kFM6OpCount; ++dst)
         for (int src = 0; src <= dst; ++src)
            if (al.modMask[dst] & (1u << src)) ordered = false;
      CHECK(ordered, "every modulator has a higher index than its target");
      CHECK(al.fbDst < kFM6OpCount && al.fbSrc < kFM6OpCount, "feedback ops in range");
      CHECK(al.carrierMask != 0, "at least one carrier");
      const FM6OpRoles r = fm6OpRoles(a);
      for (int op = 0; op < kFM6OpCount; ++op)
         CHECK(r.depth[op] != kFM6OpUnreachable, "every operator reaches the output");
   }

   // --- 2: roles on known algorithms (0-based index = DX7 number - 1) --------------------------
   {
      const FM6OpRoles r1 = fm6OpRoles(0);   // 2>1, 6>5>4>3
      CHECK(r1.carrierCount == 2 && r1.isCarrier(0) && r1.isCarrier(2), "alg 1: carriers 1, 3");
      CHECK(r1.depth[1] == 1 && r1.depth[3] == 1 && r1.depth[4] == 2 && r1.depth[5] == 3,
            "alg 1: modulator depths 1/1/2/3");
      CHECK(r1.maxDepth == 3, "alg 1: max depth 3");
      const FM6OpRoles r16 = fm6OpRoles(15);  // 2>1, 4>3>1, 6>5>1
      CHECK(r16.carrierCount == 1 && r16.depth[5] == 2 && r16.depth[3] == 2, "alg 16 depths");
      const FM6OpRoles r32 = fm6OpRoles(31);  // all carriers
      CHECK(r32.carrierCount == 6 && r32.maxDepth == 0, "alg 32: six carriers");
   }

   // --- 3 + 4: the engine -----------------------------------------------------------------------
   ModuleFactory factory(48000.0, kN);
   PitchConverter pc(440.0);
   auto fmMod = makeFM(factory, pc);
   Module* fm = fmMod.get();
   CHECK(fm != nullptr, "FM6Op spec present");
   if (!fm) { std::printf("ABORT\n"); return 1; }

   const uint8_t levels[] = {FM6OpParamItemOp1Level, FM6OpParamItemOp2Level, FM6OpParamItemOp3Level,
                             FM6OpParamItemOp4Level, FM6OpParamItemOp5Level, FM6OpParamItemOp6Level};
   for (uint8_t l : levels) set(fm, l, 1.0f);
   set(fm, FM6OpParamItemOutputLevel, 1.0f);
   set(fm, FM6OpParamItemFeedback, 0.7f);

   int sounding = 0;
   float worst = 0.0f;
   for (int a = 0; a < kFM6OpAlgorithms; ++a) {
      set(fm, FM6OpParamItemAlgorithm, static_cast<float>(a));
      bool finite = false;
      const float peak = play(fm, 8, finite);
      CHECK(finite, "output finite");
      if (peak > 0.05f) ++sounding;
      worst = std::fmax(worst, peak);
   }
   CHECK(sounding == kFM6OpAlgorithms, "all 32 algorithms sound");
   CHECK(worst <= 1.0001f, "carrier-normalised output stays within ±1");
   std::printf("algorithms sounding: %d/32, worst peak %.3f\n", sounding, worst);

   // Algorithm 4: feedback is Op4 → Op6. With Op1-3 silent the output is Op4 (carrier) modulated
   // by the 6>5>4 stack, so the feedback amount must change it.
   set(fm, FM6OpParamItemAlgorithm, 3.0f);
   for (uint8_t l : {FM6OpParamItemOp1Level, FM6OpParamItemOp2Level, FM6OpParamItemOp3Level})
      set(fm, l, 0.0f);
   bool finite = false;
   float sumNoFb = 0.0f, sumFb = 0.0f;
   set(fm, FM6OpParamItemFeedback, 0.0f);
   play(fm, 8, finite, &sumNoFb);
   set(fm, FM6OpParamItemFeedback, 1.0f);
   play(fm, 8, finite, &sumFb);
   CHECK(finite && std::fabs(sumFb - sumNoFb) > 0.01f * sumNoFb, "alg 4 cross-feedback changes the sound");
   std::printf("alg 4 |x| sum: fb 0 %.1f, fb 1 %.1f\n", sumNoFb, sumFb);

   if (g_fails == 0)
      std::printf("ALL PASS\n");
   return g_fails ? 1 : 0;
}
