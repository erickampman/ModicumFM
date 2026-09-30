//
//  FM4OpModule.hpp
//
//  4-operator phase-modulation (DX7-style) voice, in ONE module. Ported from
//  PolygraphModular's FM4OpModule (AUv3) — DSP unchanged; only the MIDI path is
//  retargeted from MIDIUniversalMessage to pm::UmpMessage.
//
//  Op->op modulation and feedback are audio-rate and per-sample, so the four
//  operators live in this module's process loop (not as separate block-processed
//  modules). 8 algorithms + one feedback path on the top operator; each operator
//  selects among the harmonic-stack wavetables (sine.cpp) and has a per-op DADSR
//  envelope plus feed-forward Amp/Freq CV inputs. The fundamental is the played
//  note (or Pitch CV), transposed by global Pitch/Fine, with a through-zero linear
//  FM input added on top.
//

#pragma once
#include "Module.hpp"

const void        InitFM4OpModule(uint32_t count, Module* module);
const void        DeinitFM4OpModule(Module* module);
const void        ResetFM4OpModule(Module* module);
const void        SampleRateFM4OpModule(Module* module, double newSampleRate);
void              MIDIFM4OpModule(const pm::UmpMessage& message, Module* module);
const KPBuffPair* ProcessFM4OpModule(uint64_t processCallID, uint32_t count, Module* module);
