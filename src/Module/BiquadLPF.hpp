// BiquadLPF.hpp — a single RBJ low-pass biquad stage. Shared by CascadeLPF (Chorus/Flanger's
// feedback-path pre-filter). Ported from PolygraphModular verbatim.

#ifndef BiquadLPF_h
#define BiquadLPF_h

#pragma once
#include <cmath>

class BiquadLPF {
    float a1 = 0, a2 = 0;
    float b0 = 0, b1 = 0, b2 = 0;
    float z1 = 0, z2 = 0;

public:
    void configure(float cutoffHz, float sampleRate, float Q = 0.707f) {
        float omega = 2.0f * M_PI * cutoffHz / sampleRate;
        float sinOmega = sinf(omega);
        float cosOmega = cosf(omega);
        float alpha = sinOmega / (2.0f * Q);

        float a0 = 1.0f + alpha;

        b0 = (1.0f - cosOmega) / 2.0f / a0;
        b1 = (1.0f - cosOmega) / a0;
        b2 = b0;
        a1 = -2.0f * cosOmega / a0;
        a2 = (1.0f - alpha) / a0;
    }

    float process(float input) {
        float output = b0 * input + z1;
        z1 = b1 * input - a1 * output + z2;
        z2 = b2 * input - a2 * output;
        return output;
    }

    void reset() {
        z1 = z2 = 0.0f;
    }
};

#endif /* BiquadLPF_h */
