//
//  PitchBendConverterModule.hpp
//
//  MIDI 2.0 pitch bend → a bipolar octaves-re-A440 signal (bend range in quarter-tones). Ported
//  from PolygraphModular's PitchBendConverterModule (AUv3); MIDI is the pm::UmpMessage form.
//

#pragma once
#include "Module.hpp"

const void InitPitchBendConverter(uint32_t count, Module* module);
const void DeinitPitchBendConverter(Module* module);
const KPBuffPair* ProcessPitchBendConverter(uint64_t processCallID, uint32_t count, Module* module);
void MIDIPitchBendConverter(const pm::UmpMessage& message, Module* module);
