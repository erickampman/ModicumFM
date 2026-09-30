// SharedLFOModule.cpp — see SharedLFOModule.hpp.

#include "SharedLFOModule.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "LFOClockSync.hpp"
#include "pm_params.h"   // SharedLFOParamItem*, LFOParamWave*, LFOParamLRPhase*, MODULE_BUFF_SIZE

static constexpr double TWO_PI = 2.0 * std::numbers::pi_v<double>;

// Waveform value at phase ph in [0,1) — identical to the plain LFO's shapes (see LFOModule.cpp).
static inline float sharedLfoWave(int wave, double ph, double delta) {
    switch (wave) {
        case LFOParamWaveTriangle:
            if (ph < 0.25) return (float)(4.0 * ph);
            if (ph < 0.75) return (float)(2.0 - 4.0 * ph);
            return (float)(4.0 * ph - 4.0);
        case LFOParamWaveSquare:  return ph < 0.5   ? 1.0f : -1.0f;
        case LFOParamWaveImpulse: return ph < delta ? 1.0f :  0.0f;
        default: /* Sine */       return (float)std::sin(ph * TWO_PI);
    }
}

// One shared LFO per placed module, seen by every voice's copy (see ModuleSharedState.hpp).
//
// Rendering is LAZY: beginBlock() only marks the chunk dirty; the FIRST voice to pull the module
// this chunk renders it (with its own params and Clock input, no latency), and every later voice
// just copies the result — so every voice reads bit-identical samples. With a Clock patched, the
// clock is read from that first voice's copy of the upstream patch; a DAW-synced Clock is identical
// in every voice, while an Internal-BPM Clock only is if its per-voice copies agree.
struct SharedLFOState : ModuleSharedState {
    // Shared phase, advanced once per chunk.
    double phaseL = 0.0, phaseR = 0.0;

    // The chunk rendered by the first voice and copied out by every voice this chunk.
    float bufL[MODULE_BUFF_SIZE] = {};
    float bufR[MODULE_BUFF_SIZE] = {};
    uint32_t renderedCount = 0;
    bool     needsRender   = false;   // set by beginBlock; cleared by the first voice to render
    uint32_t pendingFrames = 0;       // this chunk's length (to advance unrendered free-run chunks)

    // Last-used settings, so a free-running chunk that no voice pulled can still advance the phase.
    bool   freeRun   = false;         // true = continuous; false = voices-active-only (reset on idle)
    double lastDelta = 1.0 / 44100.0; // cycles/sample of the last rendered chunk

    LFOClockSync clock;               // Clock-input rate/phase lock (see LFOClockSync.hpp)

    void reset() override {
        phaseL = phaseR = 0.0; renderedCount = 0; needsRender = false; pendingFrames = 0;
        clock.reset();
    }

    void beginBlock(uint32_t activeVoices, uint32_t frameCount) override {
        // Last chunk ran but no voice pulled the LFO: in free-run, keep time anyway.
        if (needsRender && freeRun) {
            phaseL += lastDelta * pendingFrames;
            phaseL -= std::floor(phaseL);
        }
        pendingFrames = std::min<uint32_t>(frameCount, (uint32_t)MODULE_BUFF_SIZE);
        renderedCount = 0;

        // Voices-active-only + silent: reset the phase and hold at 0 (no voice reads the buffer
        // this chunk). The next note's chunk then renders from phase 0 — every phrase starts at 0.
        // The clock's measured period is kept, but its ÷N grouping restarts with the phrase.
        if (!freeRun && activeVoices == 0) {
            phaseL = phaseR = 0.0;
            clock.restartGroup();
            needsRender = false;
            return;
        }
        needsRender = true;
    }

    void render(Module* module, uint64_t processCallID, uint32_t count) {
        const float freqParam = module->paramByEnum(SharedLFOParamItemPitch);   // octaves; 0 = 1 Hz
        const int   wave      = (int)(module->paramByEnum(SharedLFOParamItemWave) + 0.5f);
        freeRun = module->paramByEnum(SharedLFOParamItemFreeRun) >= 0.5f;
        double phaseShift = 0.0;
        switch (static_cast<LFOParamLRPhase>((int)module->paramByEnum(SharedLFOParamItemPhase))) {
            case LFOParamLRPhase60:  phaseShift = 1.0 / 6.0; break;
            case LFOParamLRPhase90:  phaseShift = 1.0 / 4.0; break;
            case LFOParamLRPhase120: phaseShift = 1.0 / 3.0; break;
            case LFOParamLRPhase180: phaseShift = 1.0 / 2.0; break;
            default: break;   // LFOParamLRPhase0
        }

        const bool clockConn = module->isInputConnected(SharedLFOParamItemClockInput);
        const float* clk = module->retrieveInput(SharedLFOParamItemClockInput, processCallID, count)->first->data;
        const LFOClockRatio clockRatio = lfoClockRatioForIndex(module->paramByEnum(SharedLFOParamItemClockRatio));
        if (!clockConn && clock.edgeCount) clock.reset();

        const double knobDelta = std::exp2((double)freqParam) * module->mFrequencyScale;   // base 1 Hz -> 2^octaves
        const uint32_t n = std::min(count, pendingFrames);

        // Re-establish the L/R phase offset at the chunk boundary (matches the plain LFO), then
        // advance both channels independently.
        phaseR = phaseL + phaseShift; if (phaseR >= 1.0) phaseR -= 1.0;
        double delta = knobDelta;
        for (uint32_t i = 0; i < n; ++i) {
            if (clockConn) {                                   // slewed rate + phase lock
                clock.edge(clk[i], clockRatio, knobDelta, phaseL);
                delta = clock.step(knobDelta);
            }
            bufL[i] = sharedLfoWave(wave, phaseL, delta);
            bufR[i] = sharedLfoWave(wave, phaseR, delta);
            phaseL += delta; if (phaseL >= 1.0) phaseL -= 1.0;
            phaseR += delta; if (phaseR >= 1.0) phaseR -= 1.0;
        }
        lastDelta     = delta;
        renderedCount = n;
        needsRender   = false;
    }
};

ModuleSharedState* CreateSharedLFOState() { return new SharedLFOState(); }

const KPBuffPair* ProcessSharedLFOModule(uint64_t processCallID, uint32_t count, Module* module) {
    float* outL = module->output.first->data;
    float* outR = module->output.second->data;

    auto* st = static_cast<SharedLFOState*>(module->sharedState);
    if (!st) {
        std::fill_n(outL, count, 0.0f);
        std::fill_n(outR, count, 0.0f);
        return &module->output;
    }

    // First voice to pull this chunk renders the shared buffer; the rest just copy it.
    if (st->needsRender) st->render(module, processCallID, count);

    for (uint32_t i = 0; i < count; ++i) {
        outL[i] = (i < st->renderedCount) ? st->bufL[i] : 0.0f;
        outR[i] = (i < st->renderedCount) ? st->bufR[i] : 0.0f;
    }
    return &module->output;
}
