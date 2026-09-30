// ChorusModule.hpp — modulated stereo delay (ModDelayLine, pre/post LPF, feedback). BaseDelay
// plus a ModDelay CV input (e.g. an LFO) scaled by ModDelayLevel. See .cpp. Ported from
// PolygraphModular.

#pragma once
#include "Module.hpp"

const void InitChorusModule(uint32_t count, Module* module);
const void DeinitChorusModule(Module* module);
const void SampleRateChorusModule(Module* module, double newSampleRate);
const KPBuffPair* ProcessChorusModule(uint64_t processCallID, uint32_t count, Module* module);
const void ParamChorus(Module* module, AUParameterAddress address, AUValue value);
