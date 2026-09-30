// pm_twinfm_test — the TwinFM voice end to end through the VoiceManager's default patch
// (dual FM4Op → mixer → LPF (ADSR 1 + channel pressure) → VCA (ADSR 2) → Chorus → Final):
//   1. a note sounds, at the note's pitch (operators 2-4 muted → operator 1 is a pure sine);
//   2. full pitch bend raises it by the bend range (default 4 quarter-tones = 2 semitones);
//   3. channel pressure opens the filter (pressure → LPF cutoff mod 2);
//   4. the voice frees after release.

#include "VoiceManager.hpp"
#include "pm_addresses.h"
#include "pm_midi.h"
#include "pm_params.h"

#include <cmath>
#include <cstdio>
#include <cstring>

static int g_fails = 0;
#define CHECK(cond, msg)                                                                           \
   do {                                                                                            \
      if (!(cond)) {                                                                               \
         std::printf("FAIL: %s\n", (msg));                                                         \
         ++g_fails;                                                                                \
      }                                                                                            \
   } while (0)

static constexpr double kSR = 48000.0;
static constexpr uint32_t kN = 512;

static void setVMParam(VoiceManager& vm, MIDIParamItem item, float v) {
   vm.setVoiceManagerParameter(MODULE_PARAM_ADDRESS(ModuleParamKindMIDI, ModuleInstance1, item), v);
}

static VoiceManager* makeVM() {
   auto* vm = new VoiceManager(kSR, kN);
   setVMParam(*vm, MIDIParamItemVoiceCount, 10);
   setVMParam(*vm, MIDIParamItemChannelFilterValue, 0);
   setVMParam(*vm, MIDIParamItemGroupFilterValue, 0);
   setVMParam(*vm, MIDIParamItemVoiceStealAlgorithm, 0);
   setVMParam(*vm, MIDIParamItemFreeRun, 0);
   setVMParam(*vm, MIDIParamItemLegato, 0);
   // Chorus fully dry so the measurements see the raw voice.
   vm->setParameter(MODULE_PARAM_ADDRESS(ModuleParamKindChorus, ModuleInstance1, ChorusParamItemMix), 0.0f);
   return vm;
}

// Mute operators 2-4 on both engines (algorithm 0: 4>3>2>1, carrier = op 1) → a pure sine.
static void pureSine(VoiceManager& vm) {
   for (uint8_t inst : {ModuleInstance1, ModuleInstance2}) {
      vm.setParameter(MODULE_PARAM_ADDRESS(ModuleParamKindFM4Op, inst, FM4OpParamItemAlgorithm), 0.0f);
      vm.setParameter(MODULE_PARAM_ADDRESS(ModuleParamKindFM4Op, inst, FM4OpParamItemFeedback), 0.0f);
      vm.setParameter(MODULE_PARAM_ADDRESS(ModuleParamKindFM4Op, inst, FM4OpParamItemOp2Level), 0.0f);
      vm.setParameter(MODULE_PARAM_ADDRESS(ModuleParamKindFM4Op, inst, FM4OpParamItemOp3Level), 0.0f);
      vm.setParameter(MODULE_PARAM_ADDRESS(ModuleParamKindFM4Op, inst, FM4OpParamItemOp4Level), 0.0f);
   }
}

struct Block { float L[kN], R[kN]; };

static void render(VoiceManager& vm, Block& b) {
   std::memset(b.L, 0, sizeof(b.L));
   std::memset(b.R, 0, sizeof(b.R));
   vm.process(kN, b.L, b.R);
}

// Frequency from rising zero crossings over `blocks` blocks (after the caller lets it settle).
static double measureHz(VoiceManager& vm, int blocks) {
   Block b;
   float prev = 0.0f;
   int crossings = 0;
   for (int k = 0; k < blocks; ++k) {
      render(vm, b);
      for (uint32_t i = 0; i < kN; ++i) {
         if (prev <= 0.0f && b.L[i] > 0.0f) ++crossings;
         prev = b.L[i];
      }
   }
   return crossings * kSR / (blocks * double(kN));
}

static double measureRms(VoiceManager& vm, int blocks) {
   Block b;
   double sum = 0.0;
   for (int k = 0; k < blocks; ++k) {
      render(vm, b);
      for (uint32_t i = 0; i < kN; ++i) sum += double(b.L[i]) * b.L[i];
   }
   return std::sqrt(sum / (blocks * double(kN)));
}

int main() {
   Block b;

   // --- 1 + 2: pitch, then full pitch bend -------------------------------------------------
   {
      VoiceManager* vm = makeVM();
      pureSine(*vm);
      vm->handleMIDIMessage(pm::makeNoteOn(69, 0x8000));   // A440
      for (int k = 0; k < 10; ++k) render(*vm, b);          // settle past the attack
      const double hz = measureHz(*vm, 94);                 // ~1 s
      CHECK(std::fabs(hz - 440.0) < 3.0, "note 69 plays at ~440 Hz");

      vm->handleMIDIMessage(pm::fromMidi1Bytes(0xE0, 0x7F, 0x7F));   // full bend up
      for (int k = 0; k < 4; ++k) render(*vm, b);
      const double bentHz = measureHz(*vm, 94);
      const double expect = 440.0 * std::pow(2.0, 2.0 / 12.0);        // +2 semitones
      CHECK(std::fabs(bentHz - expect) < 4.0, "full pitch bend raises ~2 semitones");
      std::printf("pitch: %.1f Hz, bent %.1f Hz (expect %.1f)\n", hz, bentHz, expect);
      delete vm;
   }

   // --- 3: channel pressure opens the filter ------------------------------------------------
   {
      VoiceManager* vm = makeVM();   // default operator levels → bright, harmonic-rich tone
      const auto lpf = [](uint8_t item) {
         return MODULE_PARAM_ADDRESS(ModuleParamKindButterworthLowPassFilter, ModuleInstance1, item);
      };
      vm->setParameter(lpf(LPFParamItemPitch), -2.0f);          // cutoff 110 Hz — mostly closed
      vm->setParameter(lpf(LPFParamItemFreqModLevel2), 6.0f);   // full pressure → +6 octaves
      vm->handleMIDIMessage(pm::makeNoteOn(57, 0x8000));        // A220
      for (int k = 0; k < 10; ++k) render(*vm, b);
      const double closed = measureRms(*vm, 20);
      vm->handleMIDIMessage(pm::fromMidi1Bytes(0xD0, 0x7F, 0x00));   // full channel pressure
      for (int k = 0; k < 4; ++k) render(*vm, b);
      const double open = measureRms(*vm, 20);
      CHECK(open > closed * 2.0, "channel pressure opens the filter");
      std::printf("pressure: rms closed %.4f, open %.4f\n", closed, open);

      // --- 4: release frees the voice ---
      vm->handleMIDIMessage(pm::makeNoteOff(57));
      for (int k = 0; k < 400; ++k) render(*vm, b);   // ~4.3 s: amp release (1 s) + ring-out
      CHECK(vm->activeCount() == 0, "voice frees after release");
      delete vm;
   }

   if (g_fails == 0)
      std::printf("ALL PASS\n");
   return g_fails ? 1 : 0;
}
