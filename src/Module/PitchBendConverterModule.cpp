//
//  PitchBendConverterModule.cpp
//
//  MIDI 2.0 pitch bend → a bipolar octaves-re-A440 signal (patch into an oscillator's pitch input).
//  Ported from PolygraphModular's PitchBendConverterModule (AUv3); the 32-bit bend value arrives in
//  pm::UmpMessage::data32. The QuarterTones param is the total bend range in quarter-tones.
//

#include "PitchBendConverterModule.hpp"
#include "pm_midi.h"
#include <algorithm>

static inline float MIDIToPitchDelta(uint32_t pitchWord32, float bendRangeSemitones = 2.0f) {
    // MIDI 2.0 pitch bend range is 0..0xFFFFFFFF (0..4294967295).
    constexpr float scale = 1.0f / 4294967295.0f;

    // Normalize to [0,1], then shift to [-1,+1].
    float normalized = static_cast<float>(pitchWord32) * scale;
    float signedBend = (normalized - 0.5f) * 2.0f;

    // Apply semitone bend range and convert to 1-per-octave units (1 semitone = 1/12).
    return signedBend * (bendRangeSemitones / 12.0f);
}

struct PitchBendConverterHelperStruct {
    float curValue = 0.0f;
};

const void InitPitchBendConverter(uint32_t /*count*/, Module* module)
{
    module->state = new PitchBendConverterHelperStruct();
}

const void DeinitPitchBendConverter(Module* module)
{
    delete static_cast<PitchBendConverterHelperStruct*>(module->state);
    module->state = nullptr;
}

const KPBuffPair* ProcessPitchBendConverter(uint64_t /*processCallID*/, uint32_t count, Module* module)
{
    auto* state = static_cast<PitchBendConverterHelperStruct*>(module->state);
    float* outL = module->output.first->data;
    float* outR = module->output.second->data;

    std::fill_n(outL, count, state->curValue);
    std::fill_n(outR, count, state->curValue);

    return &module->output;
}

void MIDIPitchBendConverter(const pm::UmpMessage& message, Module* module)
{
    auto* state = static_cast<PitchBendConverterHelperStruct*>(module->state);

    if (pm::isChannelVoice2(message) && message.status == pm::kStatusPitchBend) {
        const float bendValues = module->paramByEnum(PitchBendConverterParamItemQuarterTones);
        // The param is in quarter tones, so divide by 2 to get semitones.
        state->curValue = MIDIToPitchDelta(message.data32, bendValues * 0.5f);
    }
}
