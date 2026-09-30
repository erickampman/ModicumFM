//
//  ChannelPressureModule.hpp
//
//  MIDI 2.0 channel pressure (aftertouch) → a held 0..1 control value. Ported from
//  PolygraphModular's ChannelPressureModule (AUv3); MIDI is the pm::UmpMessage form.
//

#pragma once
#include "Module.hpp"

const void InitChannelPressure(uint32_t count, Module* module);
const void DeinitChannelPressure(Module* module);
const KPBuffPair* ProcessChannelPressure(uint64_t processCallID, uint32_t count, Module* module);
void MIDIChannelPressure(const pm::UmpMessage& message, Module* module);
