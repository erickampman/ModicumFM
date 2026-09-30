// pm_selftest — exercises the Phase 1b foundation: the spec-derived UMP MIDI layer, the
// CLAP→UMP decoders, and the vendored AU-free leaf files. Prints "ALL PASS".

#include "CPPTypes.hpp"
#include "KPBuffPairHelper.hpp"
#include "ParamAddressMacros.h"
#include "clap_ump.h"
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
   // --- param address (kind<<16 | instance<<8 | item) round-trip ---
   uint32_t addr = MODULE_PARAM_ADDRESS(0x42, 3, 7);
   CHECK(MODULE_KIND_FROM_ADDRESS(addr) == 0x42, "addr kind");
   CHECK(MODULE_INSTANCE_FROM_ADDRESS(addr) == 3, "addr instance");
   CHECK(MODULE_ITEM_FROM_ADDRESS(addr) == 7, "addr item");

   // --- KPBuffPair static buffers ---
   CHECK(KPBuffPairHelper::zeroes()->first->data[0] == 0.0f, "zeroes");
   CHECK(KPBuffPairHelper::ones()->second->data[MODULE_BUFF_SIZE - 1] == 1.0f, "ones");

   // --- UMP predicates ---
   auto on = pm::makeNoteOn(69, 0x8000);
   CHECK(pm::isNoteOn(on) && !pm::isNoteOff(on) && on.note == 69, "makeNoteOn");
   auto off = pm::makeNoteOff(69);
   CHECK(pm::isNoteOff(off) && !pm::isNoteOn(off), "makeNoteOff");
   auto on0 = pm::makeNoteOn(60, 1);
   on0.velocity = 0; // note-on with velocity 0 must read as note-off (spec convention)
   CHECK(pm::isNoteOff(on0), "vel0 == noteOff");

   // --- decode a raw MIDI 2.0 note-on UMP word pair ---
   uint32_t words[2] = {(0x4u << 28) | (0x9u << 20) | (69u << 8) | 0u, (0xC000u << 16)};
   auto dec = pm::decodeUmp(words);
   CHECK(pm::isNoteOn(dec) && dec.note == 69 && dec.velocity == 0xC000, "decodeUmp note-on");

   // --- microtonal: attribute type 3 (7.9), note 69 + fraction 256 ---
   uint16_t attr = (uint16_t(69) << 9) | 256;
   double f = pm::pitchAttributeToFrequency(attr);
   double expect = pm::noteToFrequency(69) * std::pow(2.0, 256.0 / 6144.0);
   CHECK(std::fabs(f - expect) < 1e-6, "pitch attribute");

   // --- CLAP note event → UMP ---
   clap_event_note_t ce{};
   ce.channel = 1;
   ce.key = 64;
   ce.velocity = 1.0;
   auto cu = pm::fromClapNote(ce, true);
   CHECK(pm::isNoteOn(cu) && cu.note == 64 && cu.channel == 1 && cu.velocity == 65535,
         "fromClapNote on");
   CHECK(pm::isNoteOff(pm::fromClapNote(ce, false)), "fromClapNote off");

   // --- CLAP midi2 raw words → UMP ---
   clap_event_midi2_t cm{};
   cm.data[0] = words[0];
   cm.data[1] = words[1];
   auto cmu = pm::fromClapMidi2(cm);
   CHECK(pm::isNoteOn(cmu) && cmu.note == 69, "fromClapMidi2");

   if (g_fails == 0)
      std::printf("ALL PASS\n");
   return g_fails ? 1 : 0;
}
