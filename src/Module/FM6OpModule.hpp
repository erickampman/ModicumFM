//
//  FM6OpModule.hpp
//
//  6-operator phase-modulation (DX7-style) voice, in ONE module. Grown from TwinFM's FM4OpModule
//  (itself a port of PolygraphModular's): same per-op DADSR envelopes, harmonic-stack waves,
//  legato handling, stereo detune/spread and optional oversampling — now six operators routed by
//  the 32 DX7 algorithms in FM6OpAlgorithms.hpp, with that table's per-algorithm feedback path.
//
//  Op->op modulation and feedback are audio-rate and per-sample, so all six operators live in
//  this module's process loop. The fundamental is the played note (or Pitch CV), transposed by
//  global Pitch/Fine, with a through-zero linear FM input added on top.
//

#pragma once
#include "Module.hpp"

const void        InitFM6OpModule(uint32_t count, Module* module);
const void        DeinitFM6OpModule(Module* module);
const void        ResetFM6OpModule(Module* module);
const void        SampleRateFM6OpModule(Module* module, double newSampleRate);
void              MIDIFM6OpModule(const pm::UmpMessage& message, Module* module);
const KPBuffPair* ProcessFM6OpModule(uint64_t processCallID, uint32_t count, Module* module);
