//
//  SlewModule.mm
//
//  Created by Eric Kampman on 5/11/25.
//
//  A linear slew-rate limiter: Rise / Fall are seconds per +1 / -1 unit (per octave, on a pitch
//  signal). ModicumFM's glide (IFSynth's module, plus two things):
//    - Rise / Fall at or below the 0.1 ms floor pass the input straight through (glide off; also
//      how poly turns it off), rather than slewing at the floor rate.
//    - The first note-on after init or reset snaps to its pitch instead of sliding up from 0, so
//      the first glide starts from a real note. Later notes glide from wherever the output is.

#include "SlewModule.hpp"


inline float slewSample(float input, float prev, float risePerSample, float fallPerSample);


struct SlewModuleHelperStruct {
    float prevL;
    float prevR;
    bool  primed;   // a note has set the output since init / reset
    bool  snap;     // the next block starts at its input (the first note)

    explicit SlewModuleHelperStruct()
        : prevL(0), prevR(0), primed(false), snap(false) {}
};

const void InitSlewModule(uint32_t /*count*/, Module* module)
{
    auto* state = new SlewModuleHelperStruct();
    module->state = state;

}
const void DeinitSlewModule(Module* module)
{
    delete static_cast<SlewModuleHelperStruct*>(module->state);
    module->state = nullptr;
}

const void ResetSlewModule(Module* module)
{
    if (auto* state = static_cast<SlewModuleHelperStruct*>(module->state))
        *state = SlewModuleHelperStruct();
}

void MIDISlewModule(const pm::UmpMessage& message, Module* module)
{
    auto* state = static_cast<SlewModuleHelperStruct*>(module->state);
    if (pm::isNoteOn(message) && !state->primed) {
        state->snap   = true;
        state->primed = true;
    }
}

const KPBuffPair* ProcessSlewModule(uint64_t processCallID, uint32_t count, Module* module)
{
    const KPBuffPair* inputBuffer = module->retrieveInput(SlewParamItemInput, processCallID, count);

    AUValue rise = module->paramByEnum(SlewParamItemRise); // seconds for +1 unit
    AUValue fall = module->paramByEnum(SlewParamItemFall); // seconds for -1 unit

    // ✓ robust mapping: step_per_sample = (1/SR) / seconds_per_unit
    const float invSR   = static_cast<float>(module->mFrequencyScale); // 1.0 / mSampleRate
    const float minSecs = 1e-4f;  // 0.1 ms floor; at or below it, no slew

    auto* state = static_cast<SlewModuleHelperStruct*>(module->state);

    float* inL = inputBuffer->first->data;
    float* inR = inputBuffer->second->data;
    float* outL = module->output.first->data;
    float* outR = module->output.second->data;

    if ((rise <= minSecs && fall <= minSecs) || count == 0) {
        std::copy_n(inL, count, outL);
        std::copy_n(inR, count, outR);
        if (count > 0) { state->prevL = inL[count - 1]; state->prevR = inR[count - 1]; }
        state->snap = false;
        return &module->output;
    }
    if (state->snap) {
        state->prevL = inL[0];
        state->prevR = inR[0];
        state->snap  = false;
    }

    const float risePerSample = invSR / std::max(rise, minSecs);
    const float fallPerSample = invSR / std::max(fall, minSecs);

    float prevL = state->prevL, prevR = state->prevR;

    for (uint32_t i = 0; i < count; ++i) {
        prevL = slewSample(inL[i], prevL, risePerSample, fallPerSample);
        prevR = slewSample(inR[i], prevR, risePerSample, fallPerSample);
        outL[i] = prevL;
        outR[i] = prevR;
    }

    state->prevL = prevL;
    state->prevR = prevR;
    return &module->output;
}

inline float slewSample(float input, float prev, float risePerSample, float fallPerSample)
{
    float delta = input - prev;
    
    if (delta == 0) {
        prev = input;
    }
    else if (delta > 0) {
        prev += risePerSample;
        if (prev > input) { // overshoot
            prev = input;
        }
    } else {
        prev -= fallPerSample;
        if (prev < input) { // undershoot
            prev = input;
        }
    }
    
    return prev;
}
