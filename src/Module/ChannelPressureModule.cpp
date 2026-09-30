//
//  ChannelPressureModule.cpp
//
//  MIDI 2.0 channel pressure (aftertouch) → a held 0..1 control value. Ported from
//  PolygraphModular's ChannelPressureModule (AUv3); the 32-bit pressure value arrives in
//  pm::UmpMessage::data32.
//

#include "ChannelPressureModule.hpp"
#include "pm_midi.h"
#include <algorithm>

struct ChannelPressureHelperStruct {
    double curValue = 0.0; // 0..1
};

const void InitChannelPressure(uint32_t /*count*/, Module* module)
{
    module->state = new ChannelPressureHelperStruct();
}

const void DeinitChannelPressure(Module* module)
{
    delete static_cast<ChannelPressureHelperStruct*>(module->state);
    module->state = nullptr;
}

const KPBuffPair* ProcessChannelPressure(uint64_t /*processCallID*/, uint32_t count, Module* module)
{
    auto* state = static_cast<ChannelPressureHelperStruct*>(module->state);

    float* outL = module->output.first->data;
    float* outR = module->output.second->data;

    const float v = static_cast<float>(state->curValue);
    std::fill_n(outL, count, v);
    std::fill_n(outR, count, v);

    return &module->output;
}

void MIDIChannelPressure(const pm::UmpMessage& message, Module* module)
{
    auto* state = static_cast<ChannelPressureHelperStruct*>(module->state);

    if (pm::isChannelVoice2(message) && message.status == pm::kStatusChannelPressure) {
        // MIDI 2.0 channel pressure is 32-bit (0..0xFFFFFFFF). Normalize to 0..1.
        constexpr double kMidi2Norm = 1.0 / 4294967295.0;
        state->curValue = std::clamp(message.data32 * kMidi2Norm, 0.0, 1.0);
    }
}
