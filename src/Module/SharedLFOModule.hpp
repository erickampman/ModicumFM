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
//
// ModicumFM adds a Per Voice mode (SharedLFOParamItemPerVoice, appended; off by default): each
// voice's copy keeps its own phase (in module->mOmegaL/R, as PolygraphModular's per-voice LFO does)
// and ignores the shared buffer, and a note-on restarts it at 0 — the DX7's LFO key sync — except a
// fingered mono legato note (Module::legatoHeld), so the LFO runs on through a glide. Rate, wave and
// L/R phase as shared; the Clock input is not read in this mode.

#ifndef SharedLFOModule_hpp
#define SharedLFOModule_hpp

#include "Module.hpp"
#include "ModuleSharedState.hpp"

const KPBuffPair* ProcessSharedLFOModule(uint64_t processCallID, uint32_t count, Module* module);
void MIDISharedLFOModule(const pm::UmpMessage& message, Module* module);

// The sharedStateCreateProc this module lists in its ModuleInitSpec.
ModuleSharedState* CreateSharedLFOState();

#endif /* SharedLFOModule_hpp */
