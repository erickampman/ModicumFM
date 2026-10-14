// ChorusModule.cpp — see .hpp.

#include "ChorusModule.hpp"
#include "ModDelayLine.hpp"
#include "CascadeLPF.hpp"
#include "pm_params.h"

#include <algorithm>
#include <cmath>
#include <cfloat>

struct ChorusState {
    ModDelayLine delayL;
    ModDelayLine delayR;

    CascadeLPF preLPF_L, preLPF_R;   // feedback-path pre-filter (the only filter actually used)

    float smoothedModL = 0.0f;
    float smoothedModR = 0.0f;

    float feedbackL = 0.0f;
    float feedbackR = 0.0f;

    double sampleRate = 44100.0;
    float  nyquist    = 22050.0f;   // with a small safety margin applied elsewhere
    float  maxDelayMs = 0.0f;       // derived from capacity and sampleRate
    float  smoothingCoeff = 0.99f;  // coeff in y = c*y + (1-c)*x ; recomputed per SR
    float  preCutoffHz  = 4000.0f;

    ChorusState(double sr) : delayL(sr, 8192), delayR(sr, 8192) {
        sampleRate = sr;
        nyquist    = float(0.5 * sr * 0.98); // 2% margin
        // Clamp cutoff to Nyquist just in case
        const float preHz  = std::min(preCutoffHz,  nyquist);
        preLPF_L.configure(preHz,  sr);
        preLPF_R.configure(preHz,  sr);
        // capacity is 8192 here; if the delay exposes capacity(), use that dynamically.
        maxDelayMs = 1000.0f * 8192.0f / float(sr);
        setSmoothingTimeMs(0.2f); // ~0.2ms de-zipper (SR invariant): removes stepped-mod clicks
                                  // while leaving delay-mod bandwidth open to ~800 Hz.
    }

    void setSmoothingTimeMs(float tauMs) {
        // smoothingCoeff = exp(-1 / (tauSec * fs)) ; tauSec = tauMs/1000
        smoothingCoeff = std::exp(-1000.0f / (tauMs * float(sampleRate)));
    }
};

const void InitChorusModule(uint32_t /*count*/, Module* module) {
    module->state = new ChorusState(1.0 / module->mFrequencyScale);
}

const void DeinitChorusModule(Module* module) {
    delete static_cast<ChorusState*>(module->state);
    module->state = nullptr;
}

const void ResetChorusModule(Module* module) {
    auto* st = static_cast<ChorusState*>(module->state);
    if (!st) return;
    st->delayL.clear();
    st->delayR.clear();
    st->preLPF_L.reset();
    st->preLPF_R.reset();
    st->smoothedModL = st->smoothedModR = 0.0f;
    st->feedbackL = st->feedbackR = 0.0f;
}

const void SampleRateChorusModule(Module* module, double newSampleRate)
{
    auto* st = static_cast<ChorusState*>(module->state);
    if (!st) return;

    st->sampleRate = newSampleRate;
    st->nyquist    = float(0.5 * newSampleRate * 0.98f);

    // Reconfigure feedback pre-filter with SR + clamp to Nyquist.
    const float preHz  = std::min(st->preCutoffHz,  st->nyquist);
    st->preLPF_L.configure(preHz,  newSampleRate);
    st->preLPF_R.configure(preHz,  newSampleRate);

    // Update delay lines for SR.
    st->delayL.setSampleRate(newSampleRate);
    st->delayR.setSampleRate(newSampleRate);

    // Update max delay (ms) allowed by buffer at this SR.
    st->maxDelayMs = 1000.0f * 8192.0f / float(newSampleRate);

    // Keep smoothing TIME constant across SR.
    st->setSmoothingTimeMs(0.2f);

    // Flush feedback to avoid old-rate energy.
    st->feedbackL = 0.0f;
    st->feedbackR = 0.0f;
}

const KPBuffPair* ProcessChorusModule(uint64_t processCallID, uint32_t count, Module* module) {
    if (!module->isInputConnected(ChorusParamItemInput)) {
        return KPBuffPairHelper::zeroes();
    }

    auto input = module->retrieveInput(ChorusParamItemInput,      processCallID, count);
    auto mod   = module->retrieveInput(ChorusParamItemModDelayInput, processCallID, count);

    AUValue baseDelayMs        = module->paramByEnum(ChorusParamItemBaseDelay);
    AUValue mixParam           = std::clamp(module->paramByEnum(ChorusParamItemMix), 0.0f, 1.0f);
    AUValue feedbackParam      = std::clamp(module->paramByEnum(ChorusParamItemFeedback), 0.0f, 0.99f);
    AUValue modDelayLevelParam = module->paramByEnum(ChorusParamItemModDelayLevel);

    float dryMix = 1.0f - mixParam;
    float wetMix = mixParam;

    auto* state = static_cast<ChorusState*>(module->state);

    float* inL  = input->first->data;
    float* inR  = input->second->data;
    float* modL = mod->first->data;
    float* modR = mod->second->data;
    float* outL = module->output.first->data;
    float* outR = module->output.second->data;
    float* end  = inL + count;

    const float smoothingCoeff = state->smoothingCoeff; // SR-aware now

    for (; inL < end; ++inL, ++inR, ++modL, ++modR, ++outL, ++outR) {
        state->smoothedModL = smoothingCoeff * state->smoothedModL + (1.0f - smoothingCoeff) * (*modL);
        state->smoothedModR = smoothingCoeff * state->smoothedModR + (1.0f - smoothingCoeff) * (*modR);

        float delayLms = baseDelayMs + state->smoothedModL * modDelayLevelParam;
        float delayRms = baseDelayMs + state->smoothedModR * modDelayLevelParam;

        // Clamp per current SR/buffer capacity to avoid out-of-range reads.
        delayLms = std::clamp(delayLms, 0.0f, state->maxDelayMs);
        delayRms = std::clamp(delayRms, 0.0f, state->maxDelayMs);

        // LPF applied only to the feedback path, not the main signal.
        float inputL = *inL + state->preLPF_L.process(state->feedbackL * feedbackParam);
        float inputR = *inR + state->preLPF_R.process(state->feedbackR * feedbackParam);

        float delayedL = state->delayL.process(inputL, delayLms);
        float delayedR = state->delayR.process(inputR, delayRms);

        // Denormal guard: flush feedback to zero if it falls below the normal range.
        state->feedbackL = (std::abs(delayedL) < FLT_MIN) ? 0.0f : delayedL;
        state->feedbackR = (std::abs(delayedR) < FLT_MIN) ? 0.0f : delayedR;

        *outL = dryMix * (*inL) + wetMix * delayedL;
        *outR = dryMix * (*inR) + wetMix * delayedR;
    }

    return &module->output;
}

const void ParamChorus(Module* /*module*/, AUParameterAddress /*address*/, AUValue /*value*/) {
    // No cached scalars to resync — every param is read fresh each block in Process().
}
