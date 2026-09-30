// SharedLFOModule.hpp — a low-frequency control oscillator whose phase is SHARED across every
// active voice, so all voices read the exact same LFO value (phase-locked). Same shapes as the
// plain LFO (Sine / Triangle / Square / Impulse), exponential rate, stereo L/R phase offset, but
// no modulation inputs (a single shared phase can't reconcile per-voice modulation) — only a Clock
// input that locks rate + phase to a clock (see LFOClockSync.hpp) — and a "Free Run" boolean:
//   Free Run on  = continuous — the phase advances every chunk, never auto-resets.
//   Free Run off = voices-active-only — the phase advances only while a voice is sounding and
//                  resets to 0 when the instrument falls silent, so every phrase starts at phase 0.
//
// The shared phase lives in SharedLFOState (a ModuleSharedState). beginBlock() (called once per
// engine chunk by VoiceManager before any voice is processed) marks the chunk dirty; the first
// voice to pull the module renders it into a shared buffer and every voice copies that buffer into
// its own output. See SharedLFOModule.cpp.

#ifndef SharedLFOModule_hpp
#define SharedLFOModule_hpp

#include "Module.hpp"
#include "ModuleSharedState.hpp"

const KPBuffPair* ProcessSharedLFOModule(uint64_t processCallID, uint32_t count, Module* module);

// The sharedStateCreateProc this module lists in its ModuleInitSpec.
ModuleSharedState* CreateSharedLFOState();

#endif /* SharedLFOModule_hpp */
