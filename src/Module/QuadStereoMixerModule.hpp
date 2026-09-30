// QuadStereoMixerModule.hpp — 4 stereo inputs, each with its own Level and Pan (balance panning,
// no crossfeed). Stateless. See .cpp. Ported from PolygraphModular.

#pragma once
#include "Module.hpp"

const KPBuffPair* ProcessQuadStereoMixerModule(uint64_t processCallID, uint32_t count, Module* module);
