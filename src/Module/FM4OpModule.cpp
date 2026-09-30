//
//  FM4OpModule.cpp
//
//  Full per-sample 4-op PM engine: all 8 algorithms + feedback + amp/freq CV
//  inputs; operators use the harmonic-stack wavetables (sine.cpp). A small master
//  gate handles note-off. PMU port of PM's FM4OpModule — only the MIDI proc
//  differs (pm::UmpMessage note predicates instead of MIDIUniversalMessage).
//

#include "FM4OpModule.hpp"
#include "sine.hpp"           // harmonic-stack wavetables (sine.cpp), shared with LFO
#include "PitchConverter.hpp" // pitch CV (octaves re A440) -> Hz
#include "pm_midi.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

// --- Operator waveforms (harmonic-stack wavetables) -------------------------

// Five single-cycle tables selected per operator by the Wave param.
static const double* const kFMTables[5] = {
    sinValues, sin1_2Values, sin1_3Values, sin1_3_5Values, sin1_2_3_4Values
};
static double kFMGain[5] = { 1.0, 1.0, 1.0, 1.0, 1.0 };
static bool   kFMGainReady = false;

// Per-table normalisation: switching waveform shouldn't jump level, and a
// modulator's peak (its modulation index) must mean the same across waveforms.
static void fmInitGains() {
    if (kFMGainReady) { return; }
    const int n = (int)sinValueMultiplier + 1;
    for (int t = 0; t < 5; ++t) {
        double peak = 1e-9;
        for (int i = 0; i < n; ++i) { const double a = std::fabs(kFMTables[t][i]); if (a > peak) { peak = a; } }
        kFMGain[t] = 1.0 / peak;
    }
    kFMGainReady = true;
}

// Wrapped, normalised lookup. p is in CYCLES (phase + modulation), any real.
static inline double fmWave(int wave, double p) {
    if (wave < 0) { wave = 0; } else if (wave > 4) { wave = 4; }
    p -= std::floor(p);                          // wrap to [0,1)
    const double* tbl = kFMTables[wave];
    const double idx  = p * sinValueMultiplier;
    const int    ii   = (int)idx;
    const double fr   = idx - (double)ii;
    const double b    = tbl[ii];
    return (b + (tbl[ii + 1] - b) * fr) * kFMGain[wave];
}

// --- Algorithms -------------------------------------------------------------

// Operators are indexed 0..3 (= op1..op4). Bit i in a mask refers to op index i.
// modMask[c] = which ops modulate op c. carrierMask = which ops sum to output.
// Feedback is always on op index 3 (the "top" operator).
struct FMAlgo {
    uint8_t modMask[4];
    uint8_t carrierMask;
};

static const FMAlgo kFMAlgos[8] = {
    { { 0b0010, 0b0100, 0b1000, 0 }, 0b0001 },  // 0: 4>3>2>1              (carrier 1)
    { { 0b0010, 0b1100, 0,      0 }, 0b0001 },  // 1: (4,3)>2>1            (carrier 1)
    { { 0b0110, 0b1000, 0,      0 }, 0b0001 },  // 2: 4>2>1, 3>1           (carrier 1)
    { { 0b1110, 0,      0,      0 }, 0b0001 },  // 3: (2,3,4)>1            (carrier 1)
    { { 0b0010, 0,      0b1000, 0 }, 0b0101 },  // 4: 2>1, 4>3             (carriers 1,3)
    { { 0b1000, 0b1000, 0b1000, 0 }, 0b0111 },  // 5: 4>(1,2,3)            (carriers 1,2,3)
    { { 0b0010, 0b0100, 0,      0 }, 0b1001 },  // 6: 3>2>1 + 4            (carriers 1,4)
    { { 0,      0,      0,      0 }, 0b1111 },  // 7: 1,2,3,4 parallel     (additive)
};

// --- Params (contiguous per-op block: 13 items each) ------------------------

static inline uint8_t opItem(int oi, int offset) {
    return (uint8_t)(FM4OpParamItemOp1AmpInput + oi * 13 + offset);
}
enum { kAmpIn = 0, kFreqIn, kRatio, kFine, kWave, kLevel, kAmpDepth, kFreqDepth,
       kDelay, kAttack, kDecay, kSustain, kRelease };

// --- Per-op DADSR envelope --------------------------------------------------

// Linear attack into exponential decay/release. Note-off (via MIDI) -> release.
enum FMEGStage : uint8_t { EGIdle, EGDelay, EGAttack, EGDecay, EGSustain, EGRelease };
struct FMEG {
    FMEGStage stage = EGIdle;
    double    value = 0.0;
    double    delayLeft = 0.0;
};

static inline void egTrigger(FMEG& e, double delaySamples) {
    e.value = 0.0;
    if (delaySamples > 0.5) { e.stage = EGDelay; e.delayLeft = delaySamples; }
    else                    { e.stage = EGAttack; }
}
static inline void egRelease(FMEG& e) {
    if (e.stage != EGIdle) { e.stage = EGRelease; }
}
// Legato retrigger: re-attack from the CURRENT level (no reset to 0, no delay).
static inline void egTriggerLegato(FMEG& e) { e.stage = EGAttack; }
static inline bool egSustaining(const FMEG& e) {
    return e.stage == EGAttack || e.stage == EGDecay || e.stage == EGSustain;
}
static inline double egTick(FMEG& e, double atkInc, double decCoef, double sus, double relCoef) {
    switch (e.stage) {
        case EGIdle:    return 0.0;
        case EGDelay:   if ((e.delayLeft -= 1.0) <= 0.0) { e.stage = EGAttack; } return 0.0;
        case EGAttack:  e.value += atkInc; if (e.value >= 1.0) { e.value = 1.0; e.stage = EGDecay; } return e.value;
        case EGDecay:   e.value += decCoef * (sus - e.value); if (e.value <= sus + 1.0e-4) { e.value = sus; e.stage = EGSustain; } return e.value;
        case EGSustain: return sus;
        case EGRelease: e.value += relCoef * (0.0 - e.value); if (e.value <= 1.0e-4) { e.value = 0.0; e.stage = EGIdle; } return e.value;
    }
    return 0.0;
}

// --- State ------------------------------------------------------------------

struct FM4OpState {
    double sampleRate = 44100.0;

    double phase[2][4]    = { { 0.0 }, { 0.0 } };  // [channel][op], 0..1
    double prevOut[2][4]  = { { 0.0 }, { 0.0 } };  // y[n-1] per op, per channel
    double prevOut2[2][4] = { { 0.0 }, { 0.0 } };  // y[n-2]; feedback op reads the AVERAGE of the
                                                   // two (single-sample feedback self-oscillates)
    FMEG   eg[4];                                  // per-op amplitude envelope (shared L/R)

    double  f0       = 440.0; // Hz from the played note (Pitch-input fallback)
    float   velAmp   = 1.0f;
    float   prevGate = 0.0f;  // for Gate-input edge detection
    uint8_t lastNote = 255;   // for same-pitch legato smoothing

    // Oversampling decimator (only used when Oversample != Off): the operator bank runs at N× the
    // sample rate and this FIR lowpass removes the images above the base Nyquist before downsampling.
    // Per-channel circular history of the N× stream; one shared write index; a cached windowed-sinc
    // kernel redesigned when the factor changes. Adds ~(FM_OS_TAPS-1)/(2N) samples of latency.
    static constexpr int FM_OS_TAPS = 128;
    double osHist[2][FM_OS_TAPS] = { {0.0}, {0.0} };
    int    osWrite = 0;
    double osKernel[FM_OS_TAPS] = {0.0};
    int    osKernelFactor = 0;   // factor the kernel is currently designed for (0 = none)
};

// Windowed-sinc lowpass for decimating an N×-oversampled stream back to the base rate: cutoff at the
// base Nyquist (0.5/N cycles/sample of the oversampled stream), Blackman window, unity DC gain.
static void fmDesignDecimationKernel(double* k, int taps, int osFactor) {
    const double fc = 0.5 / osFactor;
    const int    M  = taps - 1;
    double sum = 0.0;
    for (int n = 0; n < taps; ++n) {
        const double x = (double)n - M / 2.0;
        const double sinc = (x == 0.0) ? 2.0 * fc : std::sin(2.0 * M_PI * fc * x) / (M_PI * x);
        const double w = 0.42 - 0.5 * std::cos(2.0 * M_PI * n / M) + 0.08 * std::cos(4.0 * M_PI * n / M);
        k[n] = sinc * w;
        sum += k[n];
    }
    for (int n = 0; n < taps; ++n) k[n] /= sum;
}

// Render ONE sample of the 4-op bank for channel `ch`: advance each op's phase by phInc[oi], apply
// the algorithm's modulation routing + averaged feedback on the top op, and sum the carriers. Used
// by both the 1× path (phInc = freq/sr) and the oversampled path (phInc = freq/(sr·N), called N×).
static inline double fmRenderStep(FM4OpState* st, int ch, const FMAlgo& algo, const int* wave,
                                  const double phInc[4], const double amp[4], double feedback) {
    double opOut[4];
    for (int oi = 3; oi >= 0; --oi) {
        st->phase[ch][oi] += phInc[oi];
        st->phase[ch][oi] -= std::floor(st->phase[ch][oi]);
        double mod = 0.0;
        const uint8_t mm = algo.modMask[oi];
        for (int src = 0; src < 4; ++src) { if (mm & (1 << src)) { mod += opOut[src]; } }
        if (oi == 3) { mod += feedback * 0.5 * (st->prevOut[ch][3] + st->prevOut2[ch][3]); }
        opOut[oi] = fmWave(wave[oi], st->phase[ch][oi] + mod) * amp[oi];
    }
    double s = 0.0;
    for (int oi = 0; oi < 4; ++oi) { if (algo.carrierMask & (1 << oi)) { s += opOut[oi]; } }
    for (int oi = 0; oi < 4; ++oi) { st->prevOut2[ch][oi] = st->prevOut[ch][oi]; st->prevOut[ch][oi] = opOut[oi]; }
    return s;
}

// --- Lifecycle --------------------------------------------------------------

const void InitFM4OpModule(uint32_t /*count*/, Module* module) {
    fmInitGains();
    auto* st = new FM4OpState();
    st->sampleRate = module->mSampleRate;
    module->state = st;
}

const void DeinitFM4OpModule(Module* module) {
    delete static_cast<FM4OpState*>(module->state);
    module->state = nullptr;
}

const void ResetFM4OpModule(Module* module) {
    if (auto* st = static_cast<FM4OpState*>(module->state)) {
        for (int oi = 0; oi < 4; ++oi) {
            st->phase[0][oi] = st->phase[1][oi] = 0.0;
            st->prevOut[0][oi]  = st->prevOut[1][oi]  = 0.0;
            st->prevOut2[0][oi] = st->prevOut2[1][oi] = 0.0;
            st->eg[oi] = FMEG();
        }
        st->prevGate = 0.0f;
        st->lastNote = 255;
        for (int ch = 0; ch < 2; ++ch) std::fill_n(st->osHist[ch], FM4OpState::FM_OS_TAPS, 0.0);
        st->osWrite = 0;
    }
}

const void SampleRateFM4OpModule(Module* module, double newSampleRate) {
    if (auto* st = static_cast<FM4OpState*>(module->state)) { st->sampleRate = newSampleRate; }
}

// --- MIDI -------------------------------------------------------------------

void MIDIFM4OpModule(const pm::UmpMessage& message, Module* module) {
    auto* st = static_cast<FM4OpState*>(module->state);
    if (!st) { return; }

    // A patched Gate input takes over envelope gating (the patch should run
    // independently of MIDI). MIDI still sets the base freq (used only when the
    // Pitch input is unpatched).
    const bool gateConn = module->isInputConnected(FM4OpParamItemGateInput);

    if (pm::isNoteOn(message)) {
        const uint8_t note = (uint8_t)message.note;
        const bool samePitch = (st->lastNote != 255 && st->lastNote == note);
        st->lastNote = note;
        st->f0 = 440.0 * std::exp2(((double)note - 69.0) / 12.0);
        if (!gateConn) {
            st->velAmp = (float)message.velocity / 65535.0f;   // MIDI 2.0 16-bit
            const int legato = module->getParamValueAsInt(FM4OpParamItemRetrigger);
            // A voice-legato slide (mono, note routed to the still-sounding voice) sets
            // module->legatoContinuation — the same signal the simple oscillators use to skip their
            // phase reset. FM 4 Op honours it too: even in Steal mode a legato slide re-attacks
            // smoothly from the current level and keeps phase, instead of snapping the envelope to 0
            // and resetting phase (an amplitude step plus a phase click). Genuine polyphonic steal
            // (new note on an idle/stolen voice, legatoContinuation false) still hard-retriggers.
            const bool smoothRetrigger = samePitch || module->legatoContinuation;
            for (int oi = 0; oi < 4; ++oi) {
                if (legato == 1) {                       // LegatoRetrigger: re-attack from current
                    egTriggerLegato(st->eg[oi]);
                } else if (legato == 2) {                // LegatoTrue: never restart the attack on a slide
                    if (smoothRetrigger) {
                        // Legato slide/continuation (same pitch, or the VM routed this note into a
                        // still-ringing voice): do NOT re-attack. If an intervening note-off had begun
                        // a release, resume toward sustain from the current level (EGDecay) so the slide
                        // neither restarts the attack nor keeps fading out. Still-sustaining: continue.
                        if (st->eg[oi].stage == EGRelease) { st->eg[oi].stage = EGDecay; }
                    } else if (!egSustaining(st->eg[oi])) {
                        egTriggerLegato(st->eg[oi]);     // fresh (non-legato) note on an idle/released EG
                    }
                } else if (smoothRetrigger) {            // Steal, same pitch OR legato slide: smooth (no click)
                    egTriggerLegato(st->eg[oi]);
                } else {                                 // Steal, new pitch: hard retrigger
                    egTrigger(st->eg[oi], (double)module->getParamValue(opItem(oi, kDelay)) * st->sampleRate);
                    st->phase[0][oi] = st->phase[1][oi] = 0.0; st->prevOut[0][oi] = st->prevOut[1][oi] = 0.0; st->prevOut2[0][oi] = st->prevOut2[1][oi] = 0.0;  // clean attack
                }
            }
        }
    } else if (pm::isNoteOff(message)) {
        if (!gateConn) { for (int oi = 0; oi < 4; ++oi) { egRelease(st->eg[oi]); } }
    }
}

// --- Process ----------------------------------------------------------------

const KPBuffPair* ProcessFM4OpModule(uint64_t processCallID, uint32_t count, Module* module) {
    auto* st = static_cast<FM4OpState*>(module->state);
    if (!st) { return KPBuffPairHelper::zeroes(); }

    float* outL = module->output.first->data;
    float* outR = module->output.second->data;

    const double sr    = st->sampleRate;

    // Oversample factor: 0=Off→1, 1→2×, 2→4×. Off keeps the exact original per-sample path.
    const int    osSel    = std::min(2, std::max(0, module->getParamValueAsInt(FM4OpParamItemOversample)));
    const int    osFactor = osSel == 0 ? 1 : (osSel == 1 ? 2 : 4);
    if (osFactor > 1 && st->osKernelFactor != osFactor) {
        fmDesignDecimationKernel(st->osKernel, FM4OpState::FM_OS_TAPS, osFactor);
        st->osKernelFactor = osFactor;
        for (int ch = 0; ch < 2; ++ch) std::fill_n(st->osHist[ch], FM4OpState::FM_OS_TAPS, 0.0);
        st->osWrite = 0;
    }

    const int    algoIdx  = std::min(7, std::max(0, module->getParamValueAsInt(FM4OpParamItemAlgorithm)));
    const FMAlgo algo     = kFMAlgos[algoIdx];
    const double baseFeedback   = module->getParamValue(FM4OpParamItemFeedback);
    const double feedbackDepth  = module->getParamValue(FM4OpParamItemFeedbackDepth);
    const double outLevel = module->getParamValue(FM4OpParamItemOutputLevel);

    // Global transpose (constant across the block): Pitch (semitones) + Fine (cents).
    const double pitchSemis   = module->getParamValue(FM4OpParamItemPitch);
    const double fineCents     = module->getParamValue(FM4OpParamItemFine);
    const double transposeFac  = std::exp2((pitchSemis + fineCents / 100.0) / 12.0);
    // Linear FM depth: FMInput (Hz per unit) is ADDED to the fundamental (through-zero).
    const double fmLevel       = module->getParamValue(FM4OpParamItemFMLevel);

    // Hoist per-op params (constant across the block); pull the CV input buffers.
    double ratioFine[4], baseLevel[4], ampDepth[4], freqDepth[4];
    double egAtkInc[4], egDecCoef[4], egSus[4], egRelCoef[4], delaySamp[4];
    int    wave[4];
    const float* ampIn[2][4];   // [channel][op] — L/R read independently
    const float* freqIn[2][4];
    for (int oi = 0; oi < 4; ++oi) {
        const double ratio   = module->getParamValue(opItem(oi, kRatio));
        const double fineFac = std::exp2(module->getParamValue(opItem(oi, kFine)) / 1200.0);  // cents
        ratioFine[oi]        = ratio * fineFac;
        baseLevel[oi]        = module->getParamValue(opItem(oi, kLevel));
        ampDepth[oi]         = module->getParamValue(opItem(oi, kAmpDepth));
        freqDepth[oi]        = module->getParamValue(opItem(oi, kFreqDepth));
        wave[oi]             = module->getParamValueAsInt(opItem(oi, kWave));

        const uint8_t ampKey  = opItem(oi, kAmpIn);
        const uint8_t freqKey = opItem(oi, kFreqIn);
        if (module->isInputConnected(ampKey)) {
            const KPBuffPair* p = module->retrieveInput(ampKey, processCallID, count);
            ampIn[0][oi] = p->first->data; ampIn[1][oi] = p->second->data;   // stereo (mono source fills both)
        } else { ampIn[0][oi] = ampIn[1][oi] = nullptr; }
        if (module->isInputConnected(freqKey)) {
            const KPBuffPair* p = module->retrieveInput(freqKey, processCallID, count);
            freqIn[0][oi] = p->first->data; freqIn[1][oi] = p->second->data;
        } else { freqIn[0][oi] = freqIn[1][oi] = nullptr; }

        // DADSR: params are in SECONDS (like the ADSR module). Linear attack, exp decay/release.
        const double atkSec = module->getParamValue(opItem(oi, kAttack));
        const double decSec = module->getParamValue(opItem(oi, kDecay));
        const double relSec = module->getParamValue(opItem(oi, kRelease));
        egAtkInc[oi]  = 1.0 / std::max(1.0, atkSec * sr);
        egDecCoef[oi] = 1.0 - std::exp(-1.0 / (decSec * sr));
        egSus[oi]     = module->getParamValue(opItem(oi, kSustain));
        egRelCoef[oi] = 1.0 - std::exp(-1.0 / (relSec * sr));
        delaySamp[oi] = (double)module->getParamValue(opItem(oi, kDelay)) * sr;
    }

    // Fundamental pitch: the Pitch input CV (octaves re A440) when patched, else
    // the played MIDI note. The per-op Freq inputs add to it (pitch domain), then
    // Ratio multiplies to the op frequency.
    PitchConverter* pc = module->pitchConverter;
    const bool   usePitchIn = pc && module->isInputConnected(FM4OpParamItemPitchInput);
    const float* pitchPtr   = usePitchIn ? module->retrieveInput(FM4OpParamItemPitchInput, processCallID, count)->first->data : nullptr;

    // Gate input (overrides MIDI): rising edge triggers the EGs, falling releases.
    const bool   gateConn = module->isInputConnected(FM4OpParamItemGateInput);
    const float* gatePtr  = gateConn ? module->retrieveInput(FM4OpParamItemGateInput, processCallID, count)->first->data : nullptr;

    // Global linear-FM CV added to the fundamental, read in STEREO (L/R independent).
    const float* fmIn[2] = { nullptr, nullptr };
    if (module->isInputConnected(FM4OpParamItemFMInput)) {
        const KPBuffPair* p = module->retrieveInput(FM4OpParamItemFMInput, processCallID, count);
        fmIn[0] = p->first->data; fmIn[1] = p->second->data;
    }
    // Feedback-amount mod CV, read in STEREO too.
    const float* fbIn[2] = { nullptr, nullptr };
    if (module->isInputConnected(FM4OpParamItemFeedbackInput)) {
        const KPBuffPair* p = module->retrieveInput(FM4OpParamItemFeedbackInput, processCallID, count);
        fbIn[0] = p->first->data; fbIn[1] = p->second->data;
    }

    // Stereo: L/R detuned symmetrically; Spread = width (0 mono .. 1 full).
    const double detuneCents  = (double)module->getParamValue(FM4OpParamItemDetune) * 50.0;
    const double detuneFac[2] = { std::exp2(-detuneCents / 2.0 / 1200.0),
                                  std::exp2(+detuneCents / 2.0 / 1200.0) };
    const double spread       = (double)module->getParamValue(FM4OpParamItemSpread);

    for (uint32_t i = 0; i < count; ++i) {
        // Sample-accurate gate edges (gate level doubles as velocity).
        if (gatePtr) {
            const float g = gatePtr[i];
            if (g >= 0.5f && st->prevGate < 0.5f) {              // rising: trigger
                st->velAmp = std::min(1.0f, std::max(0.0f, g));
                for (int oi = 0; oi < 4; ++oi) { egTrigger(st->eg[oi], delaySamp[oi]); st->phase[0][oi] = st->phase[1][oi] = 0.0; st->prevOut[0][oi] = st->prevOut[1][oi] = 0.0; st->prevOut2[0][oi] = st->prevOut2[1][oi] = 0.0; }
                if (osFactor > 1) { for (int ch = 0; ch < 2; ++ch) std::fill_n(st->osHist[ch], FM4OpState::FM_OS_TAPS, 0.0); }  // clean decimator start
            } else if (g < 0.5f && st->prevGate >= 0.5f) {       // falling: release
                for (int oi = 0; oi < 4; ++oi) { egRelease(st->eg[oi]); }
            }
            st->prevGate = g;
        }

        // Fundamental base (shared): base note (Pitch CV or MIDI) → global transpose.
        const double fundBase = (usePitchIn ? pc->pitchToFrequency(pitchPtr[i]) : st->f0) * transposeFac;

        // Envelopes advance ONCE per sample (shared by both stereo channels).
        double egv[4];
        for (int oi = 0; oi < 4; ++oi) {
            egv[oi] = egTick(st->eg[oi], egAtkInc[oi], egDecCoef[oi], egSus[oi], egRelCoef[oi]);
        }

        // Render the operator bank per channel: detuned frequency + independent Amp/FM input channels.
        // Per-op phase increment and amp are computed once per OUTPUT sample (CV/EG held across any
        // oversampling sub-steps), then fmRenderStep runs once (Off) or N× into the decimator.
        double phInc[2][4], amp[2][4], fb[2];
        for (int ch = 0; ch < 2; ++ch) {
            double fundHz = fundBase;
            if (fmIn[ch]) { fundHz += (double)fmIn[ch][i] * fmLevel; }
            double feedback = baseFeedback;
            if (fbIn[ch]) { feedback += (double)fbIn[ch][i] * feedbackDepth; }
            fb[ch] = feedback < 0.0 ? 0.0 : (feedback > 1.0 ? 1.0 : feedback);
            for (int oi = 0; oi < 4; ++oi) {
                double freq = fundHz * ratioFine[oi] * detuneFac[ch];
                if (freqIn[ch][oi]) { freq *= std::exp2((double)freqIn[ch][oi][i] * freqDepth[oi]); }
                phInc[ch][oi] = freq / (sr * osFactor);
                double a = baseLevel[oi] * egv[oi];
                if (ampIn[ch][oi]) { a *= (double)ampIn[ch][oi][i] * ampDepth[oi]; }
                amp[ch][oi] = a;
            }
        }

        double chan[2];
        if (osFactor == 1) {
            chan[0] = fmRenderStep(st, 0, algo, wave, phInc[0], amp[0], fb[0]);
            chan[1] = fmRenderStep(st, 1, algo, wave, phInc[1], amp[1], fb[1]);
        } else {
            // Run the bank at N× into the per-channel FIR history, then decimate (one dot product).
            for (int k = 0; k < osFactor; ++k) {
                st->osHist[0][st->osWrite] = fmRenderStep(st, 0, algo, wave, phInc[0], amp[0], fb[0]);
                st->osHist[1][st->osWrite] = fmRenderStep(st, 1, algo, wave, phInc[1], amp[1], fb[1]);
                st->osWrite = (st->osWrite + 1) % FM4OpState::FM_OS_TAPS;
            }
            for (int ch = 0; ch < 2; ++ch) {
                double acc = 0.0;
                int idx = st->osWrite;
                for (int t = 0; t < FM4OpState::FM_OS_TAPS; ++t) {
                    idx = (idx == 0 ? FM4OpState::FM_OS_TAPS - 1 : idx - 1);
                    acc += st->osKernel[t] * st->osHist[ch][idx];
                }
                chan[ch] = acc;
            }
        }

        // Stereo width (Spread): 0 = mono (mid only), 1 = full L/R.
        const double mid  = 0.5 * (chan[0] + chan[1]);
        const double side = 0.5 * (chan[0] - chan[1]) * spread;
        const double g    = (double)st->velAmp * outLevel;
        outL[i] = (float)((mid + side) * g);
        outR[i] = (float)((mid - side) * g);
    }

    return &module->output;
}
