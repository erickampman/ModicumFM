// ModDelayLine.hpp — a modulatable delay line with Catmull-Rom-ish (Lagrange) fractional-delay
// interpolation. Shared by ChorusModule and FlangerModule. Ported from PolygraphModular verbatim
// (pure portable C++, no changes needed).

#ifndef ModDelayLine_hpp
#define ModDelayLine_hpp

#include <vector>
#include <algorithm>
#include <cmath>

class ModDelayLine {
public:
    ModDelayLine(double sampleRate, int maxSamples = 8192)
    : buffer(maxSamples, 0.0f), size(maxSamples), writeIndex(0), sampleRate(sampleRate) {}

    void setSampleRate(double sr) {
        sampleRate = sr;
    }

    size_t capacity() const {
        return buffer.size();
    }

    void reset() {
        clear();
    }

    void clear() {
        std::fill(buffer.begin(), buffer.end(), 0.0f);
        writeIndex = 0;
    }

    float process(float input, float delayMs) {
        // Clamp: lower bound 2 so indexP1 (writeIndex-intDelay+1) never reads unwritten data.
        const float delaySamples = std::clamp(delayMs * sampleRate / 1000.0f, 2.0f, size - 4.0f);
        int intDelay = (int)delaySamples;
        float frac = delaySamples - intDelay;

        // Fan taps toward older (more-delayed) samples so frac=0 -> index0, frac->1 -> indexM1.
        int indexP1 = (writeIndex - intDelay + 1 + size) % size;  // 1 newer  (context)
        int index0  = (writeIndex - intDelay     + size) % size;  // anchor
        int indexM1 = (writeIndex - intDelay - 1 + size) % size;  // 1 older  (frac->1)
        int indexM2 = (writeIndex - intDelay - 2 + size) % size;  // 2 older  (context)

        // Lagrange interp: at frac=0 -> x0; at frac=1 -> x1.
        // Map: xm1=newer-context, x0=anchor, x1=older-target, x2=older-context.
        float xm1 = buffer[indexP1];
        float x0  = buffer[index0];
        float x1  = buffer[indexM1];
        float x2  = buffer[indexM2];

        buffer[writeIndex] = input;
        writeIndex = (writeIndex + 1) % size;

        return lagrangeInterp(frac, xm1, x0, x1, x2);
    }

private:
    std::vector<float> buffer;
    int size;
    int writeIndex;
    float sampleRate;

    static float lagrangeInterp(float frac, float xm1, float x0, float x1, float x2) {
        float c0 = x0;
        float c1 = 0.5f * (x1 - xm1);
        float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        float c3 = -0.5f * xm1 + 1.5f * x0 - 1.5f * x1 + 0.5f * x2;

        return ((c3 * frac + c2) * frac + c1) * frac + c0;
    }
};

#endif /* ModDelayLine_hpp */
