// pm_modicumfm_test — the ModicumFM voice end to end through the VoiceManager's default patch
// (FM6Op → LPF (ADSR 1 + channel pressure) → Chorus → Final):
//   1. a note sounds, at the note's pitch (operators 2-6 muted → operator 1 is a pure sine);
//   2. full pitch bend raises it by the bend range (default 4 quarter-tones = 2 semitones);
//   3. channel pressure opens the filter (pressure → LPF cutoff mod 2);
//   4. the voice frees after release;
//   5. CC 123 (All Notes Off) and CC 120 (All Sound Off) act on their own channel only;
//   6. Expression: pressure and timbre (CC74) do nothing at amount 0, and each amount moves its
//      target (brightness, cutoff, carrier level).

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

static AUParameterAddress fm(uint8_t item) {
   return MODULE_PARAM_ADDRESS(ModuleParamKindFM6Op, ModuleInstance1, item);
}

// Mute operators 2-6 (algorithm 1: 2>1, 6>5>4>3; carriers 1, 3) → op 1 alone, a pure sine.
static void pureSine(VoiceManager& vm) {
   vm.setParameter(fm(FM6OpParamItemAlgorithm), 0.0f);
   vm.setParameter(fm(FM6OpParamItemFeedback), 0.0f);
   for (uint8_t lvl : {FM6OpParamItemOp2Level, FM6OpParamItemOp3Level, FM6OpParamItemOp4Level,
                       FM6OpParamItemOp5Level, FM6OpParamItemOp6Level})
      vm.setParameter(fm(lvl), 0.0f);
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

// Brightness: RMS of the first difference over RMS of the signal (rises with upper harmonics).
static double measureBrightness(VoiceManager& vm, int blocks) {
   Block b;
   double sum = 0.0, diff = 0.0;
   float prev = 0.0f;
   for (int k = 0; k < blocks; ++k) {
      render(vm, b);
      for (uint32_t i = 0; i < kN; ++i) {
         sum += double(b.L[i]) * b.L[i];
         diff += double(b.L[i] - prev) * (b.L[i] - prev);
         prev = b.L[i];
      }
   }
   return sum > 0.0 ? std::sqrt(diff / sum) : 0.0;
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
      VoiceManager* vm = makeVM();
      vm->setParameter(fm(FM6OpParamItemOp2Level), 0.8f);        // Op2 → Op1 hard: harmonic-rich tone
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
      for (int k = 0; k < 400; ++k) render(*vm, b);   // ~4.3 s: operator release (0.3 s) + ring-out
      CHECK(vm->activeCount() == 0, "voice frees after release");
      delete vm;
   }

   // --- 5: channel mode messages (ported from PlainOldSynth a3b94c7) --------------------------
   {   // CC 123 releases only its channel's notes.
      VoiceManager* vm = makeVM();
      vm->handleMIDIMessage(pm::fromMidi1Bytes(0x91, 60, 100));   // channel 2
      vm->handleMIDIMessage(pm::fromMidi1Bytes(0x92, 64, 100));   // channel 3
      for (int k = 0; k < 4; ++k) render(*vm, b);
      vm->handleMIDIMessage(pm::fromMidi1Bytes(0xB1, 123, 0));
      for (int k = 0; k < 400; ++k) render(*vm, b);   // past the release
      CHECK(vm->activeCount() == 1, "CC 123 on channel 2 releases channel 2's note only");
      delete vm;
   }
   {   // ...including a note the sustain pedal is holding.
      VoiceManager* vm = makeVM();
      vm->handleMIDIMessage(pm::fromMidi1Bytes(0xB0, 64, 127));
      vm->handleMIDIMessage(pm::fromMidi1Bytes(0x90, 60, 100));
      vm->handleMIDIMessage(pm::fromMidi1Bytes(0x80, 60, 0));
      for (int k = 0; k < 50; ++k) render(*vm, b);
      CHECK(vm->activeCount() == 1, "the pedal is holding the note");
      vm->handleMIDIMessage(pm::fromMidi1Bytes(0xB0, 123, 0));
      for (int k = 0; k < 400; ++k) render(*vm, b);
      CHECK(vm->activeCount() == 0, "CC 123 releases a pedal-held note");
      delete vm;
   }
   {   // CC 120 cuts its channel's voice at once, release included; another channel plays on.
      VoiceManager* vm = makeVM();
      pureSine(*vm);
      vm->handleMIDIMessage(pm::fromMidi1Bytes(0x91, 60, 100));   // C4, channel 2
      vm->handleMIDIMessage(pm::fromMidi1Bytes(0x92, 69, 100));   // A4, channel 3
      for (int k = 0; k < 10; ++k) render(*vm, b);
      vm->handleMIDIMessage(pm::fromMidi1Bytes(0xB1, 120, 0));
      CHECK(vm->activeCount() == 1, "CC 120 on channel 2 drops its voice immediately");
      const double hz = measureHz(*vm, 47);                       // ~0.5 s: A4 alone, no C4 tail
      CHECK(std::fabs(hz - 440.0) < 3.0, "after CC 120 only A4 sounds");
      std::printf("all sound off: remaining %.1f Hz\n", hz);
      delete vm;
   }
   {   // CC 120 must not make a voice forget other channels' bends (a full Voice::reset would).
      VoiceManager* vm = makeVM();
      pureSine(*vm);
      setVMParam(*vm, MIDIParamItemVoiceCount, 1);
      vm->handleMIDIMessage(pm::fromMidi1Bytes(0xE4, 0x00, 0x60));   // channel 5: +1 st at ±2
      vm->handleMIDIMessage(pm::fromMidi1Bytes(0x91, 60, 100));
      for (int k = 0; k < 4; ++k) render(*vm, b);
      vm->handleMIDIMessage(pm::fromMidi1Bytes(0xB1, 120, 0));
      vm->handleMIDIMessage(pm::fromMidi1Bytes(0x94, 69, 100));      // A4 on channel 5
      for (int k = 0; k < 10; ++k) render(*vm, b);
      const double hz = measureHz(*vm, 94);
      const double expect = 440.0 * std::pow(2.0, 1.0 / 12.0);
      CHECK(std::fabs(hz - expect) < 3.0, "after CC 120 the voice still knows channel 5's bend");
      std::printf("all sound off keeps channel bends: %.1f Hz (expect %.1f)\n", hz, expect);
      delete vm;
   }

   // --- 6: Expression -------------------------------------------------------------------------
   const auto lpf = [](uint8_t item) {
      return MODULE_PARAM_ADDRESS(ModuleParamKindButterworthLowPassFilter, ModuleInstance1, item);
   };
   const pm::UmpMessage fullPressure = pm::fromMidi1Bytes(0xD0, 0x7F, 0x00);
   const pm::UmpMessage fullTimbre   = pm::fromMidi1Bytes(0xB0, 74, 0x7F);
   // A note with one modulator (Op2 → Op1), then `msgs`; returns {brightness, rms} after them.
   const auto play = [&](std::initializer_list<std::pair<AUParameterAddress, float>> setup,
                         std::initializer_list<pm::UmpMessage> msgs) {
      VoiceManager* vm = makeVM();
      pureSine(*vm);
      vm->setParameter(fm(FM6OpParamItemOp2Level), 0.3f);
      for (const auto& [address, value] : setup) vm->setParameter(address, value);
      vm->handleMIDIMessage(pm::makeNoteOn(57, 0x8000));
      for (int k = 0; k < 10; ++k) render(*vm, b);
      for (const auto& m : msgs) vm->handleMIDIMessage(m);
      for (int k = 0; k < 4; ++k) render(*vm, b);
      const double bright = measureBrightness(*vm, 10);
      const double rms = measureRms(*vm, 10);
      delete vm;
      return std::make_pair(bright, rms);
   };
   {
      const auto [b0, r0] = play({}, {});
      const auto [b1, r1] = play({}, {fullPressure, fullTimbre});
      CHECK(std::fabs(b1 - b0) < 1e-3 * b0 && std::fabs(r1 - r0) < 1e-3 * r0,
            "Expression: at amount 0, pressure and timbre change nothing");

      const auto [pb, pr] = play({{fm(FM6OpParamItemBrightModLevel), 1.0f}}, {fullPressure});
      CHECK(pb > b0 * 1.15, "Press>Bright: full pressure brightens");
      const auto [tb, tr] = play({{fm(FM6OpParamItemBrightMod2Level), 1.0f}}, {fullTimbre});
      CHECK(tb > b0 * 1.15, "Tmb>Bright: full timbre brightens");
      const auto [nb, nr] = play({{fm(FM6OpParamItemBrightModLevel), -1.0f}}, {fullPressure});
      CHECK(nb < b0 * 0.8, "Press>Bright negative: full pressure darkens");
      std::printf("expression brightness: rest %.3f, pressure %.3f, timbre %.3f, pressure(-) %.3f\n",
                  b0, pb, tb, nb);

      const auto [lb0, lr0] = play({{fm(FM6OpParamItemLevelModLevel), 1.0f}}, {});
      const auto [lb1, lr1] = play({{fm(FM6OpParamItemLevelModLevel), 1.0f}}, {fullPressure});
      CHECK(lr0 < 0.01 * r0 && std::fabs(lr1 - r0) < 0.02 * r0,
            "Press>Level +1: silent unpressed, full level at full pressure");
      const auto [db, dr] = play({{fm(FM6OpParamItemLevelModLevel), -1.0f}}, {fullPressure});
      CHECK(dr < 0.01 * r0, "Press>Level -1: full pressure silences");
      std::printf("expression level: rest %.4f, +1 unpressed %.4f, +1 pressed %.4f, -1 pressed %.4f\n",
                  r0, lr0, lr1, dr);

      const auto [cb0, cr0] = play({{lpf(LPFParamItemPitch), -2.0f}, {lpf(LPFParamItemFreqModLevel3), 6.0f}}, {});
      const auto [cb1, cr1] = play({{lpf(LPFParamItemPitch), -2.0f}, {lpf(LPFParamItemFreqModLevel3), 6.0f}},
                                   {fullTimbre});
      CHECK(cr1 > cr0 * 2.0, "Tmb>Cutoff: full timbre opens the filter");
      std::printf("expression cutoff: closed %.4f, timbre open %.4f\n", cr0, cr1);
   }

   if (g_fails == 0)
      std::printf("ALL PASS\n");
   return g_fails ? 1 : 0;
}
