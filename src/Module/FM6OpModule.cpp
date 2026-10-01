//
//  FM6OpModule.cpp
//
//  Full per-sample 6-op PM engine: the 32 DX7-style algorithms (FM6OpAlgorithms.hpp) + one
//  feedback path per algorithm + amp/freq CV inputs; operators use the harmonic-stack wavetables
//  (sine.cpp). Grown from TwinFM's FM4OpModule: same envelopes, legato handling, stereo detune
//  and oversampling, generalised from 4 to kFM6OpCount operators.
//

#include "FM6OpModule.hpp"
#include "FM6OpAlgorithms.hpp"
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

// --- Params (contiguous per-op block: 13 items each) ------------------------

static_assert(FM6OpParamItemCount_ - 1 <= MODULE_MAX_PARAMS, "FM6Op items must fit Module::params");
constexpr int kOpStride = FM6OpParamItemOp2AmpInput - FM6OpParamItemOp1AmpInput;
static_assert(kOpStride == 13, "per-op block layout");

static inline uint8_t opItem(int oi, int offset) {
    return (uint8_t)(FM6OpParamItemOp1AmpInput + oi * kOpStride + offset);
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

static constexpr float kFMTouchPivot = 0.7f;

struct FM6OpState {
    double sampleRate = 44100.0;

    double phase[2][kFM6OpCount]    = { { 0.0 }, { 0.0 } };  // [channel][op], 0..1
    double prevOut[2][kFM6OpCount]  = { { 0.0 }, { 0.0 } };  // y[n-1] per op, per channel
    double prevOut2[2][kFM6OpCount] = { { 0.0 }, { 0.0 } };  // y[n-2]; feedback op reads the AVERAGE of the
                                                   // two (single-sample feedback self-oscillates)
    FMEG   eg[kFM6OpCount];                                  // per-op amplitude envelope (shared L/R)

    double  f0       = 440.0; // Hz from the played note (Pitch-input fallback)
    float   velAmp   = 1.0f;
    float   velNorm  = kFMTouchPivot; // raw note velocity 0..1, for Touch (pivot = no change)
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

// Render ONE sample of the 6-op bank for channel `ch`: advance each op's phase by phInc[oi], apply
// the algorithm's modulation routing + averaged feedback (fbSrc -> fbDst), and sum the carriers. Used
// by both the 1× path (phInc = freq/sr) and the oversampled path (phInc = freq/(sr·N), called N×).
static inline double fmRenderStep(FM6OpState* st, int ch, const FM6OpAlgo& algo, const int* wave,
                                  const double phInc[kFM6OpCount], const double amp[kFM6OpCount], double feedback) {
    double opOut[kFM6OpCount];
    for (int oi = kFM6OpCount - 1; oi >= 0; --oi) {
        st->phase[ch][oi] += phInc[oi];
        st->phase[ch][oi] -= std::floor(st->phase[ch][oi]);
        double mod = 0.0;
        const uint8_t mm = algo.modMask[oi];
        for (int src = 0; src < kFM6OpCount; ++src) { if (mm & (1 << src)) { mod += opOut[src]; } }
        if (oi == algo.fbDst) { mod += feedback * 0.5 * (st->prevOut[ch][algo.fbSrc] + st->prevOut2[ch][algo.fbSrc]); }
        opOut[oi] = fmWave(wave[oi], st->phase[ch][oi] + mod) * amp[oi];
    }
    double s = 0.0;
    for (int oi = 0; oi < kFM6OpCount; ++oi) { if (algo.carrierMask & (1 << oi)) { s += opOut[oi]; } }
    for (int oi = 0; oi < kFM6OpCount; ++oi) { st->prevOut2[ch][oi] = st->prevOut[ch][oi]; st->prevOut[ch][oi] = opOut[oi]; }
    return s;
}

// --- Lifecycle --------------------------------------------------------------

const void InitFM6OpModule(uint32_t /*count*/, Module* module) {
    fmInitGains();
    auto* st = new FM6OpState();
    st->sampleRate = module->mSampleRate;
    module->state = st;
}

const void DeinitFM6OpModule(Module* module) {
    delete static_cast<FM6OpState*>(module->state);
    module->state = nullptr;
}

const void ResetFM6OpModule(Module* module) {
    if (auto* st = static_cast<FM6OpState*>(module->state)) {
        for (int oi = 0; oi < kFM6OpCount; ++oi) {
            st->phase[0][oi] = st->phase[1][oi] = 0.0;
            st->prevOut[0][oi]  = st->prevOut[1][oi]  = 0.0;
            st->prevOut2[0][oi] = st->prevOut2[1][oi] = 0.0;
            st->eg[oi] = FMEG();
        }
        st->prevGate = 0.0f;
        st->lastNote = 255;
        for (int ch = 0; ch < 2; ++ch) std::fill_n(st->osHist[ch], FM6OpState::FM_OS_TAPS, 0.0);
        st->osWrite = 0;
    }
}

const void SampleRateFM6OpModule(Module* module, double newSampleRate) {
    if (auto* st = static_cast<FM6OpState*>(module->state)) { st->sampleRate = newSampleRate; }
}

// --- Velocity -> level -------------------------------------------------------

// The same curve family as PolygraphModular's VelocityConverter — level = floor + (1 - floor)·v^p
// for v = velocity in 0..1 — but with a few fixed choices instead of continuous Min/Curve params.
// Floor is the y-intercept (the level of the softest note; 100% ignores velocity). Concave (p < 1)
// gets loud early in the key travel; Convex (p > 1) saves the loudest levels for hard playing.
static constexpr double kFMVelFloor[5]    = { 0.0, 0.25, 0.5, 0.75, 1.0 };
static constexpr double kFMVelExponent[3] = { 0.5, 1.0, 2.0 };   // Concave, Linear, Convex

static float fmVelocityLevel(uint32_t velocity16, int floorSel, int curveSel) {
    const double floor = kFMVelFloor[std::min(4, std::max(0, floorSel))];
    const double p     = kFMVelExponent[std::min(2, std::max(0, curveSel))];
    const double v     = (double)velocity16 / 65535.0;   // MIDI 2.0 16-bit
    return (float)(floor + (1.0 - floor) * std::pow(v, p));
}

// --- Response: Touch and Key Track (constants above the State) ----------------------

// Touch (0..1): velocity also scales every modulator, ×2^(touch·2·(v - pivot)) — at full Touch a
// hard note (v = 1) is ~1.5× brighter and a soft one (v = 0.3) ~0.6× as bright; v = pivot is
// unchanged. Key Track (-1..1), by octaves from middle C: modulators ×2^(-kt·0.6·oct) and every
// operator's decay/release ×2^(-kt·0.5·oct) — at + higher notes are darker and die away faster,
// like most acoustic instruments; at - the reverse. Both are exactly neutral at 0.

// --- Shared (relative) envelopes -------------------------------------------------

// "Loudness" acts on every carrier's envelope, "Brightness" on every modulator's: attack, decay
// and release times are multiplied (×0.01..×1000 — wide enough to sharpen a 1.5 s pad attack to
// 15 ms or stretch a 1 ms pluck attack to a second), capped at the operator limits (attack 20 s,
// decay/release 30 s), and sustain is offset — + moves it toward full,
// - toward zero — so a plucked (sustain 0) operator can be made to hold. Scaling all operators
// of a role by the same factors keeps their relationships (E.Piano's tine stays shorter than its
// body). Exactly neutral at ×1 / 0.
static inline double fmSustainOffset(double sus, double offset) {
    return offset >= 0.0 ? sus + (1.0 - sus) * offset : sus * (1.0 + offset);
}

// --- Role-based macros --------------------------------------------------------

// The Character macros act on operators by ROLE in the current algorithm (FM6OpAlgorithms.hpp's
// fm6OpRoles), not by operator number, so each one means the same thing on every algorithm. All
// four are neutral at 0: every factor below is then exactly 1 and the sound is untouched.
//
//   Brightness  every modulator's level × 2^(1.5·b)   (index up/down ~1.5 octaves at the ends),
//               and carriers tilted toward their high-ratio members: ×2^(1.5·b·h), h = 0 for the
//               lowest-ratio carrier … 1 for the highest — so all-carrier ("drawbar") algorithms
//               brighten too; no effect on carriers that share one ratio
//   Bite        modulators' level ×(1+bite), attack ×(1-0.8·bite), decay ×(1-0.85·bite),
//               sustain ×(1-0.7·bite): a bigger but shorter burst of brightness at the start of the
//               note, over an unchanged carrier envelope
//   Grit        feedback + 0.8·grit, and the highest-ratio modulator's level × (1 + 1.5·grit)
//   Body        warm & wide (+) ↔ thin & focused (-), three parts:
//               · carrier levels tilted by ratio: ×2^(-body·pos), pos = -1 for the lowest-ratio
//                 carrier … +1 for the highest (no effect when the carriers share one ratio)
//               · (+ only) carriers spread apart in pitch, up to ±kFMBodySpreadCents across them
//               · stereo L/R detune: + adds up to kFMBodyWidenCents; - scales the Detune knob's
//                 spread down, to mono at -1
//
// Modulator levels may be pushed up to kFMMacroMaxLevel (index 1.5 × 2π) — above the Level
// knob's 1.0 — so Brightness still has somewhere to go on an already-bright patch.
static constexpr double kFMMacroMaxLevel = 1.5;
static constexpr double kFMBodySpreadCents = 8.0;
static constexpr double kFMBodyWidenCents  = 16.0;

struct FMMacros {
    FM6OpRoles roles;
    double brightScale = 1.0, bite = 0.0, grit = 0.0;
    int    gritOp = -1;                  // highest-ratio modulator, or -1
    double carrierScale[kFM6OpCount] = { 1, 1, 1, 1, 1, 1 };
    double carrierCents[kFM6OpCount] = { 0, 0, 0, 0, 0, 0 };   // Body's pitch spread
    double body = 0.0;
};

static FMMacros fmMacros(Module* module, const FM6OpAlgo& algo, const double ratio[kFM6OpCount]) {
    FMMacros m;
    m.roles       = fm6OpRoles(algo);
    const double bright = module->getParamValue(FM6OpParamItemBrightness);
    m.brightScale = std::exp2(1.5 * bright);
    m.bite        = std::min(1.0, std::max(0.0, (double)module->getParamValue(FM6OpParamItemBite)));
    m.grit        = std::min(1.0, std::max(0.0, (double)module->getParamValue(FM6OpParamItemGrit)));
    double lo = 1e9, hi = -1e9;
    for (int oi = 0; oi < kFM6OpCount; ++oi) {
        const double lr = std::log2(ratio[oi]);
        if (m.roles.isCarrier(oi)) { lo = std::min(lo, lr); hi = std::max(hi, lr); }
        else if (m.gritOp < 0 || ratio[oi] > ratio[m.gritOp]) { m.gritOp = oi; }
    }
    const double body = std::min(1.0, std::max(-1.0, (double)module->getParamValue(FM6OpParamItemBody)));
    m.body = body;
    if (body > 0.0 && m.roles.carrierCount > 1) {
        // Spread the carriers evenly across ±spread·body cents, in operator order.
        int k = 0;
        const double n = m.roles.carrierCount;
        for (int oi = 0; oi < kFM6OpCount; ++oi)
            if (m.roles.isCarrier(oi))
                m.carrierCents[oi] = body * kFMBodySpreadCents * (2.0 * k++ / (n - 1.0) - 1.0);
    }
    if ((body != 0.0 || bright != 0.0) && hi - lo > 1e-6) {
        for (int oi = 0; oi < kFM6OpCount; ++oi) {
            if (!m.roles.isCarrier(oi)) continue;
            const double pos = (std::log2(ratio[oi]) - 0.5 * (lo + hi)) / (0.5 * (hi - lo)); // -1..1
            m.carrierScale[oi] = std::exp2(-body * pos + 1.5 * bright * 0.5 * (pos + 1.0));
        }
    }
    return m;
}

// --- MIDI -------------------------------------------------------------------

void MIDIFM6OpModule(const pm::UmpMessage& message, Module* module) {
    auto* st = static_cast<FM6OpState*>(module->state);
    if (!st) { return; }

    // A patched Gate input takes over envelope gating (the patch should run
    // independently of MIDI). MIDI still sets the base freq (used only when the
    // Pitch input is unpatched).
    const bool gateConn = module->isInputConnected(FM6OpParamItemGateInput);

    if (pm::isNoteOn(message)) {
        const uint8_t note = (uint8_t)message.note;
        const bool samePitch = (st->lastNote != 255 && st->lastNote == note);
        st->lastNote = note;
        st->f0 = 440.0 * std::exp2(((double)note - 69.0) / 12.0);
        if (!gateConn) {
            st->velNorm = (float)message.velocity / 65535.0f;
            st->velAmp = fmVelocityLevel(message.velocity,
                                         module->getParamValueAsInt(FM6OpParamItemVelFloor),
                                         module->getParamValueAsInt(FM6OpParamItemVelCurve));
            const int legato = module->getParamValueAsInt(FM6OpParamItemRetrigger);
            // A voice-legato slide (mono, note routed to the still-sounding voice) sets
            // module->legatoContinuation — the same signal the simple oscillators use to skip their
            // phase reset. FM6Op honours it too: even in Steal mode a legato slide re-attacks
            // smoothly from the current level and keeps phase, instead of snapping the envelope to 0
            // and resetting phase (an amplitude step plus a phase click). Genuine polyphonic steal
            // (new note on an idle/stolen voice, legatoContinuation false) still hard-retriggers.
            const bool smoothRetrigger = samePitch || module->legatoContinuation;
            for (int oi = 0; oi < kFM6OpCount; ++oi) {
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
        if (!gateConn) { for (int oi = 0; oi < kFM6OpCount; ++oi) { egRelease(st->eg[oi]); } }
    }
}

// --- Process ----------------------------------------------------------------

const KPBuffPair* ProcessFM6OpModule(uint64_t processCallID, uint32_t count, Module* module) {
    auto* st = static_cast<FM6OpState*>(module->state);
    if (!st) { return KPBuffPairHelper::zeroes(); }

    float* outL = module->output.first->data;
    float* outR = module->output.second->data;

    const double sr    = st->sampleRate;

    // Oversample factor: 0=Off→1, 1→2×, 2→4×. Off keeps the exact original per-sample path.
    const int    osSel    = std::min(2, std::max(0, module->getParamValueAsInt(FM6OpParamItemOversample)));
    const int    osFactor = osSel == 0 ? 1 : (osSel == 1 ? 2 : 4);
    if (osFactor > 1 && st->osKernelFactor != osFactor) {
        fmDesignDecimationKernel(st->osKernel, FM6OpState::FM_OS_TAPS, osFactor);
        st->osKernelFactor = osFactor;
        for (int ch = 0; ch < 2; ++ch) std::fill_n(st->osHist[ch], FM6OpState::FM_OS_TAPS, 0.0);
        st->osWrite = 0;
    }

    const int    algoIdx  = std::min(kFM6OpAlgorithms - 1, std::max(0, module->getParamValueAsInt(FM6OpParamItemAlgorithm)));
    const FM6OpAlgo& algo = kFM6OpAlgos[algoIdx];
    // Carriers are summed, so scale by 1/count: switching algorithm doesn't jump the level.
    const double carrierNorm = 1.0 / std::max<int>(1, fm6OpRoles(algo).carrierCount);
    const double gritParam      = std::min(1.0, std::max(0.0, (double)module->getParamValue(FM6OpParamItemGrit)));
    const double baseFeedback   = std::min(1.0, module->getParamValue(FM6OpParamItemFeedback) + 0.8 * gritParam);
    const double feedbackDepth  = module->getParamValue(FM6OpParamItemFeedbackDepth);
    const double outLevel = module->getParamValue(FM6OpParamItemOutputLevel);

    // Global transpose (constant across the block): Pitch (semitones) + Fine (cents).
    const double pitchSemis   = module->getParamValue(FM6OpParamItemPitch);
    const double fineCents     = module->getParamValue(FM6OpParamItemFine);
    const double transposeFac  = std::exp2((pitchSemis + fineCents / 100.0) / 12.0);
    // Linear FM depth: FMInput (Hz per unit) is ADDED to the fundamental (through-zero).
    const double fmLevel       = module->getParamValue(FM6OpParamItemFMLevel);

    // Hoist per-op params (constant across the block); pull the CV input buffers.
    double ratioFine[kFM6OpCount], baseLevel[kFM6OpCount], ampDepth[kFM6OpCount], freqDepth[kFM6OpCount];
    double egAtkInc[kFM6OpCount], egDecCoef[kFM6OpCount], egSus[kFM6OpCount], egRelCoef[kFM6OpCount], delaySamp[kFM6OpCount];
    int    wave[kFM6OpCount];
    const float* ampIn[2][kFM6OpCount];   // [channel][op] — L/R read independently
    const float* freqIn[2][kFM6OpCount];
    double ratioRaw[kFM6OpCount];
    for (int oi = 0; oi < kFM6OpCount; ++oi) { ratioRaw[oi] = module->getParamValue(opItem(oi, kRatio)); }
    const FMMacros macros = fmMacros(module, algo, ratioRaw);
    // Response: Touch (velocity → modulators) and Key Track (note → modulators + decay times).
    const double touch     = std::min(1.0, std::max(0.0, (double)module->getParamValue(FM6OpParamItemTouch)));
    const double keyTrack  = std::min(1.0, std::max(-1.0, (double)module->getParamValue(FM6OpParamItemKeyTrack)));
    const double keyOct    = st->lastNote == 255 ? 0.0 : ((double)st->lastNote - 60.0) / 12.0;
    const double touchMod  = std::exp2(touch * 2.0 * ((double)st->velNorm - kFMTouchPivot));
    const double keyMod    = std::exp2(-keyTrack * 0.6 * keyOct);
    const double keyTime   = std::exp2(-keyTrack * 0.5 * keyOct);
    // Shared envelopes: [0] carriers (Loudness), [1] modulators (Brightness).
    const auto envMul = [&](uint8_t item) { return std::min(1000.0, std::max(0.01, (double)module->getParamValue(item))); };
    const double envAtk[2] = { envMul(FM6OpParamItemLoudAttack),  envMul(FM6OpParamItemBrightAttack) };
    const double envDec[2] = { envMul(FM6OpParamItemLoudDecay),   envMul(FM6OpParamItemBrightDecay) };
    const double envRel[2] = { envMul(FM6OpParamItemLoudRelease), envMul(FM6OpParamItemBrightRelease) };
    const double envSus[2] = { std::min(1.0, std::max(-1.0, (double)module->getParamValue(FM6OpParamItemLoudSustain))),
                               std::min(1.0, std::max(-1.0, (double)module->getParamValue(FM6OpParamItemBrightSustain))) };
    for (int oi = 0; oi < kFM6OpCount; ++oi) {
        const double ratio   = ratioRaw[oi];
        const double fineFac = std::exp2(module->getParamValue(opItem(oi, kFine)) / 1200.0);  // cents
        ratioFine[oi]        = ratio * fineFac * std::exp2(macros.carrierCents[oi] / 1200.0);
        baseLevel[oi]        = module->getParamValue(opItem(oi, kLevel));
        const bool modulator = !macros.roles.isCarrier(oi);
        if (modulator) {
            double lvl = baseLevel[oi] * macros.brightScale * (1.0 + macros.bite) * touchMod * keyMod;
            if (oi == macros.gritOp) { lvl *= 1.0 + 1.5 * macros.grit; }
            baseLevel[oi] = std::min(lvl, std::max(baseLevel[oi], kFMMacroMaxLevel));
        } else {
            // Carriers may be raised by Brightness/Body, but not past full level (or their own
            // setting, if already higher).
            baseLevel[oi] = std::min(baseLevel[oi] * macros.carrierScale[oi], std::max(baseLevel[oi], 1.0));
        }
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

        // DADSR: params are in SECONDS. Linear attack, exp decay/release. With no VCA after the
        // engine, the carriers' envelopes ARE the amp envelope, so Decay/Release mean the time to
        // fall 60 dB (not a one-e time constant, which would ring on ~7x longer than the knob says
        // and hold voices long after they're inaudible).
        constexpr double kLn1000 = 6.907755278982137;
        // Bite shortens the modulators' attack/decay and lowers their sustain (no-op at 0).
        // Shared envelopes scale by role (carrier → Loudness, modulator → Brightness).
        const double biteT = modulator ? macros.bite : 0.0;
        const int    role  = modulator ? 1 : 0;
        const double atkSec = std::min(20.0, module->getParamValue(opItem(oi, kAttack)) * (1.0 - 0.8 * biteT) * envAtk[role]);
        const double decSec = std::min(30.0, std::max(0.001, module->getParamValue(opItem(oi, kDecay)) * (1.0 - 0.85 * biteT) * keyTime * envDec[role]));
        const double relSec = std::min(30.0, std::max(0.001, module->getParamValue(opItem(oi, kRelease)) * keyTime * envRel[role]));
        egAtkInc[oi]  = 1.0 / std::max(1.0, atkSec * sr);
        egDecCoef[oi] = 1.0 - std::exp(-kLn1000 / (decSec * sr));
        egSus[oi]     = fmSustainOffset(module->getParamValue(opItem(oi, kSustain)) * (1.0 - 0.7 * biteT), envSus[role]);
        egRelCoef[oi] = 1.0 - std::exp(-kLn1000 / (relSec * sr));
        delaySamp[oi] = (double)module->getParamValue(opItem(oi, kDelay)) * sr;
    }

    // Fundamental pitch: the Pitch input CV (octaves re A440) when patched, else
    // the played MIDI note. The per-op Freq inputs add to it (pitch domain), then
    // Ratio multiplies to the op frequency.
    PitchConverter* pc = module->pitchConverter;
    const bool   usePitchIn = pc && module->isInputConnected(FM6OpParamItemPitchInput);
    const float* pitchPtr   = usePitchIn ? module->retrieveInput(FM6OpParamItemPitchInput, processCallID, count)->first->data : nullptr;

    // Gate input (overrides MIDI): rising edge triggers the EGs, falling releases.
    const bool   gateConn = module->isInputConnected(FM6OpParamItemGateInput);
    const float* gatePtr  = gateConn ? module->retrieveInput(FM6OpParamItemGateInput, processCallID, count)->first->data : nullptr;

    // Global linear-FM CV added to the fundamental, read in STEREO (L/R independent).
    const float* fmIn[2] = { nullptr, nullptr };
    if (module->isInputConnected(FM6OpParamItemFMInput)) {
        const KPBuffPair* p = module->retrieveInput(FM6OpParamItemFMInput, processCallID, count);
        fmIn[0] = p->first->data; fmIn[1] = p->second->data;
    }
    // Feedback-amount mod CV, read in STEREO too.
    const float* fbIn[2] = { nullptr, nullptr };
    if (module->isInputConnected(FM6OpParamItemFeedbackInput)) {
        const KPBuffPair* p = module->retrieveInput(FM6OpParamItemFeedbackInput, processCallID, count);
        fbIn[0] = p->first->data; fbIn[1] = p->second->data;
    }

    // Stereo: L/R detuned symmetrically; Spread = width (0 mono .. 1 full).
    // Body widens (+) or narrows (-) the stereo detune; neutral leaves it exactly as set.
    const double detuneKnob   = (double)module->getParamValue(FM6OpParamItemDetune) * 50.0;
    const double detuneCents  = macros.body > 0.0 ? detuneKnob + macros.body * kFMBodyWidenCents
                                                  : detuneKnob * (1.0 + macros.body);
    const double detuneFac[2] = { std::exp2(-detuneCents / 2.0 / 1200.0),
                                  std::exp2(+detuneCents / 2.0 / 1200.0) };
    const double spread       = (double)module->getParamValue(FM6OpParamItemSpread);

    for (uint32_t i = 0; i < count; ++i) {
        // Sample-accurate gate edges (gate level doubles as velocity).
        if (gatePtr) {
            const float g = gatePtr[i];
            if (g >= 0.5f && st->prevGate < 0.5f) {              // rising: trigger
                st->velAmp = std::min(1.0f, std::max(0.0f, g));
                st->velNorm = st->velAmp;
                for (int oi = 0; oi < kFM6OpCount; ++oi) { egTrigger(st->eg[oi], delaySamp[oi]); st->phase[0][oi] = st->phase[1][oi] = 0.0; st->prevOut[0][oi] = st->prevOut[1][oi] = 0.0; st->prevOut2[0][oi] = st->prevOut2[1][oi] = 0.0; }
                if (osFactor > 1) { for (int ch = 0; ch < 2; ++ch) std::fill_n(st->osHist[ch], FM6OpState::FM_OS_TAPS, 0.0); }  // clean decimator start
            } else if (g < 0.5f && st->prevGate >= 0.5f) {       // falling: release
                for (int oi = 0; oi < kFM6OpCount; ++oi) { egRelease(st->eg[oi]); }
            }
            st->prevGate = g;
        }

        // Fundamental base (shared): base note (Pitch CV or MIDI) → global transpose.
        const double fundBase = (usePitchIn ? pc->pitchToFrequency(pitchPtr[i]) : st->f0) * transposeFac;

        // Envelopes advance ONCE per sample (shared by both stereo channels).
        double egv[kFM6OpCount];
        for (int oi = 0; oi < kFM6OpCount; ++oi) {
            egv[oi] = egTick(st->eg[oi], egAtkInc[oi], egDecCoef[oi], egSus[oi], egRelCoef[oi]);
        }

        // Render the operator bank per channel: detuned frequency + independent Amp/FM input channels.
        // Per-op phase increment and amp are computed once per OUTPUT sample (CV/EG held across any
        // oversampling sub-steps), then fmRenderStep runs once (Off) or N× into the decimator.
        double phInc[2][kFM6OpCount], amp[2][kFM6OpCount], fb[2];
        for (int ch = 0; ch < 2; ++ch) {
            double fundHz = fundBase;
            if (fmIn[ch]) { fundHz += (double)fmIn[ch][i] * fmLevel; }
            double feedback = baseFeedback;
            if (fbIn[ch]) { feedback += (double)fbIn[ch][i] * feedbackDepth; }
            fb[ch] = feedback < 0.0 ? 0.0 : (feedback > 1.0 ? 1.0 : feedback);
            for (int oi = 0; oi < kFM6OpCount; ++oi) {
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
                st->osWrite = (st->osWrite + 1) % FM6OpState::FM_OS_TAPS;
            }
            for (int ch = 0; ch < 2; ++ch) {
                double acc = 0.0;
                int idx = st->osWrite;
                for (int t = 0; t < FM6OpState::FM_OS_TAPS; ++t) {
                    idx = (idx == 0 ? FM6OpState::FM_OS_TAPS - 1 : idx - 1);
                    acc += st->osKernel[t] * st->osHist[ch][idx];
                }
                chan[ch] = acc;
            }
        }

        // Stereo width (Spread): 0 = mono (mid only), 1 = full L/R.
        const double mid  = 0.5 * (chan[0] + chan[1]);
        const double side = 0.5 * (chan[0] - chan[1]) * spread;
        const double g    = (double)st->velAmp * outLevel * carrierNorm;
        outL[i] = (float)((mid + side) * g);
        outR[i] = (float)((mid - side) * g);
    }

    return &module->output;
}
