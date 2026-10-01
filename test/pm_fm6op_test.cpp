// pm_fm6op_test — the FM6Op engine on its own (built through the factory + module specs):
//   1. the algorithm table's invariants (modulators outrank their targets, so one descending pass
//      evaluates each algorithm; feedback indices in range; at least one carrier);
//   2. the role analysis (carriers / modulator depth) on a few known algorithms;
//   3. every algorithm sounds after a note, stays finite and within ±1 (carrier normalisation);
//   4. feedback reaches the sound, including algorithm 4's cross-operator path (Op4 → Op6);
//   5. velocity → level: Floor (y-intercept) and Concave / Linear / Convex curves;
//   6. the Character macros: neutral is bit-identical, and Brightness / Bite / Grit / Body move
//      the brightness the right way on the operators their role selects.

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
#include <vector>

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
static float play(Module* m, int blocks, bool& finite, float* sumOut = nullptr,
                  uint16_t velocity = 0xFFFF) {
   m->reset();
   m->midiProc(pm::makeNoteOn(57, velocity), m);
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

// Render a note (blocks × kN samples of L) for the macro tests.
static std::vector<float> renderNote(Module* m, int blocks) {
   m->reset();
   m->midiProc(pm::makeNoteOn(57, 0xFFFF), m);
   std::vector<float> out;
   static uint64_t callID = 100000;
   for (int b = 0; b < blocks; ++b) {
      m->processProc(callID++, kN, m);
      out.insert(out.end(), m->output.first->data, m->output.first->data + kN);
   }
   return out;
}
// Brightness proxy over [from, to): mean |x[n] - x[n-1]| / mean |x| — grows with high harmonics.
static double brightness(const std::vector<float>& x, size_t from, size_t to) {
   double d = 0, a = 0;
   for (size_t i = from + 1; i < to && i < x.size(); ++i) { d += std::fabs(x[i] - x[i - 1]); a += std::fabs(x[i]); }
   return a > 0 ? d / a : 0;
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

   // --- 5: velocity → level -------------------------------------------------------------------
   // Op1 alone (alg 32, no feedback) so peak level is just velocity gain × carrier level.
   set(fm, FM6OpParamItemAlgorithm, 31.0f);
   set(fm, FM6OpParamItemFeedback, 0.0f);
   for (uint8_t l : levels) set(fm, l, 0.0f);
   set(fm, FM6OpParamItemOp1Level, 1.0f);
   const auto velPeak = [&](float floorSel, float curveSel, uint16_t vel) {
      set(fm, FM6OpParamItemVelFloor, floorSel);
      set(fm, FM6OpParamItemVelCurve, curveSel);
      bool fin = false;
      return play(fm, 8, fin, nullptr, vel);
   };
   const uint16_t half = 0x8000;
   const float full    = velPeak(0, 1, 0xFFFF);
   const float linear  = velPeak(0, 1, half);
   const float concave = velPeak(0, 0, half);
   const float convex  = velPeak(0, 2, half);
   const float fixed   = velPeak(4, 1, half);
   const float floor50 = velPeak(2, 1, 0x0001);
   CHECK(std::fabs(linear / full - 0.5f) < 0.02f, "linear: half velocity = half level");
   CHECK(std::fabs(concave / full - 0.707f) < 0.02f, "concave: half velocity = sqrt(0.5)");
   CHECK(std::fabs(convex / full - 0.25f) < 0.02f, "convex: half velocity = 0.25");
   CHECK(std::fabs(fixed / full - 1.0f) < 0.01f, "Fixed floor ignores velocity");
   CHECK(std::fabs(floor50 / full - 0.5f) < 0.02f, "50% floor: softest note at half level");
   std::printf("velocity @half: linear %.3f concave %.3f convex %.3f fixed %.3f (full %.3f)\n",
               linear / full, concave / full, convex / full, fixed / full, full);

   // --- 6: Character macros ----------------------------------------------------------------------
   set(fm, FM6OpParamItemVelFloor, 0); set(fm, FM6OpParamItemVelCurve, 1);
   const auto macros = [&](float b, float bite, float grit, float body) {
      set(fm, FM6OpParamItemBrightness, b); set(fm, FM6OpParamItemBite, bite);
      set(fm, FM6OpParamItemGrit, grit);    set(fm, FM6OpParamItemBody, body);
   };
   // Algorithm 1 voice: Op2 (ratio 1, level 0.4, sustaining) modulates carrier Op1.
   set(fm, FM6OpParamItemAlgorithm, 0.0f);
   for (uint8_t l : levels) set(fm, l, 0.0f);
   set(fm, FM6OpParamItemOp1Level, 1.0f);
   set(fm, FM6OpParamItemOp2Level, 0.4f);
   const size_t late0 = 20 * kN, late1 = 30 * kN;   // ~0.2 s .. 0.32 s in (past attack + decay)
   macros(0, 0, 0, 0);
   const std::vector<float> neutral = renderNote(fm, 30);
   const double bNeutral = brightness(neutral, late0, late1);
   {
      // Neutral must be bit-identical to the macros never having existed: re-render and compare.
      const std::vector<float> again = renderNote(fm, 30);
      CHECK(again == neutral, "neutral macros: identical output");
   }
   macros(1, 0, 0, 0);
   const double bUp = brightness(renderNote(fm, 30), late0, late1);
   macros(-1, 0, 0, 0);
   const double bDown = brightness(renderNote(fm, 30), late0, late1);
   CHECK(bUp > bNeutral * 1.2 && bDown < bNeutral * 0.9, "Brightness moves modulator index");
   macros(0, 1, 0, 0);
   const double bBite = brightness(renderNote(fm, 30), late0, late1);
   CHECK(bBite < bNeutral * 0.9, "Bite: modulator brightness falls away after the attack");
   macros(0, 0, 1, 0);
   const double bGrit = brightness(renderNote(fm, 30), late0, late1);
   CHECK(bGrit > bNeutral * 1.2, "Grit: more feedback + harder top modulator = brighter");
   std::printf("macros (alg 1): neutral %.3f  bright+ %.3f  bright- %.3f  bite %.3f  grit %.3f\n",
               bNeutral, bUp, bDown, bBite, bGrit);

   // Algorithm 32 (all carriers, ratios 1 and 4): Brightness tilts toward the high carrier.
   set(fm, FM6OpParamItemAlgorithm, 31.0f);
   set(fm, FM6OpParamItemOp1Level, 0.6f);
   set(fm, FM6OpParamItemOp2Level, 0.3f);
   set(fm, FM6OpParamItemOp2Ratio, 4.0f);
   macros(0, 0, 0, 0);
   const std::vector<float> flat = renderNote(fm, 30);
   macros(1, 0, 0, 0);
   const double bDrawUp = brightness(renderNote(fm, 30), late0, late1);
   macros(-1, 0, 0, 0);
   const double bDrawDown = brightness(renderNote(fm, 30), late0, late1);
   CHECK(bDrawUp > brightness(flat, late0, late1) * 1.2 && bDrawDown < brightness(flat, late0, late1) * 0.8,
         "Brightness tilts all-carrier algorithms toward / away from the high carrier");
   // ...but not carriers that share one ratio.
   set(fm, FM6OpParamItemOp2Ratio, 1.0f);
   macros(0, 0, 0, 0);
   const std::vector<float> sameRatio = renderNote(fm, 30);
   macros(1, 0, 0, 0);
   CHECK(renderNote(fm, 30) == sameRatio, "Brightness leaves same-ratio carriers alone");
   set(fm, FM6OpParamItemOp2Ratio, 4.0f);
   set(fm, FM6OpParamItemOp1Level, 1.0f);
   set(fm, FM6OpParamItemOp2Level, 0.6f);
   macros(0, 0, 0, 1);
   const double bBody = brightness(renderNote(fm, 30), late0, late1);
   macros(0, 0, 0, -1);
   const double bThin = brightness(renderNote(fm, 30), late0, late1);
   const double bFlat = brightness(flat, late0, late1);
   CHECK(bBody < bFlat * 0.9 && bThin > bFlat * 1.1, "Body tilts toward the low (+) / high (-) carrier");
   std::printf("macros (alg 32): flat %.3f  body+ %.3f  body- %.3f\n", bFlat, bBody, bThin);
   // Body's width part: on a single-carrier patch (alg 16) it changes the L/R difference —
   // wider at +1, mono (when Detune is set) at -1.
   set(fm, FM6OpParamItemAlgorithm, 15.0f);
   set(fm, FM6OpParamItemOp2Ratio, 1.0f);
   set(fm, FM6OpParamItemDetune, 0.2f);                // 10 cents L/R
   // L/R pitch difference (Hz), from rising zero crossings over ~2 s.
   const auto lrSplitHz = [&]() {
      fm->reset();
      fm->midiProc(pm::makeNoteOn(57, 0xFFFF), fm);
      static uint64_t id = 900000;
      int cL = 0, cR = 0;
      float pL = 0, pR = 0;
      const int blocks = 188;
      for (int b = 0; b < blocks; ++b) {
         fm->processProc(id++, kN, fm);
         for (uint32_t i = 0; i < kN; ++i) {
            const float l = fm->output.first->data[i], r = fm->output.second->data[i];
            if (pL <= 0 && l > 0) ++cL;
            if (pR <= 0 && r > 0) ++cR;
            pL = l; pR = r;
         }
      }
      return (cR - cL) * 48000.0 / (blocks * double(kN));
   };
   macros(0, 0, 0, 0);  const double sNeutral = lrSplitHz();
   macros(0, 0, 0, 1);  const double sWide    = lrSplitHz();
   macros(0, 0, 0, -1); const double sMono    = lrSplitHz();
   // At 220 Hz: 10 ct → ~1.3 Hz, 26 ct → ~3.3 Hz, 0 ct → 0.
   CHECK(sWide > sNeutral * 1.8, "Body +: wider L/R detune on a single-carrier patch");
   CHECK(std::fabs(sMono) < 0.6, "Body -1: stereo detune cancelled (mono)");
   std::printf("body width (alg 16): L/R split neutral %.2f Hz  +1 %.2f Hz  -1 %.2f Hz\n", sNeutral, sWide, sMono);
   macros(0, 0, 0, 0);
   set(fm, FM6OpParamItemDetune, 0.0f);

   if (g_fails == 0)
      std::printf("ALL PASS\n");
   return g_fails ? 1 : 0;
}
