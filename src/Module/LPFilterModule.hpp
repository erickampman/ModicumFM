//
//  LPFilterModule.hpp — per-voice 24 dB/oct Butterworth low-pass filter (port of PM's
//  LPFilterModule). In FeM it sits after the engine mixer, before the VCA. Cutoff is an
//  absolute pitch (fed through the voice's shared PitchConverter); Q adds resonance. Freq mod
//  input 1 carries the filter envelope, input 2 channel pressure; the Q mod input is unused.
//

#pragma once
#include "Module.hpp"

const void InitLPFilterModule(uint32_t count, Module* module);
const void DeinitLPFilterModule(Module* module);
const void SampleRateLPFilterModule(Module* module, double newSampleRate);
const KPBuffPair* ProcessLPFilterModule(uint64_t processCallID, uint32_t count, Module* module);
