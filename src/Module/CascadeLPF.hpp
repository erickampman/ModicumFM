// CascadeLPF.hpp — two cascaded BiquadLPF stages (steeper rolloff). Shared by ChorusModule and
// FlangerModule's feedback-path pre-filter. Ported from PolygraphModular verbatim.

#ifndef CascadeLPF_h
#define CascadeLPF_h

#include "BiquadLPF.hpp"

class CascadeLPF {
    BiquadLPF stage1;
    BiquadLPF stage2;

public:
    void configure(float cutoffHz, float sampleRate) {
        stage1.configure(cutoffHz, sampleRate);
        stage2.configure(cutoffHz, sampleRate);
    }

    float process(float x) {
        return stage2.process(stage1.process(x));
    }

    void reset() {
        stage1.reset();
        stage2.reset();
    }
};

#endif /* CascadeLPF_h */
