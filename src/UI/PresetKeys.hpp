// PresetKeys.hpp — the plugin-param <-> preset-JSON-field mapping, shared by the dev-only JSON
// dumper (PresetDump.hpp) and the always-on preset loader (GeneratedPresets.h resolution in
// ModicumFMUI.cpp). Kept in its own always-compiled header (not gated by MODICUMFM_PRESET_DUMP)
// since preset loading must work in every build, not just dev builds.

#ifndef MODICUMFM_PRESETKEYS_HPP
#define MODICUMFM_PRESETKEYS_HPP

#include "ModicumFMParams.h"

namespace modicumfm {

struct PresetKey { const char* module; const char* item; };

// One entry per param, index-aligned to ParamIndex (FM items follow the FM6Op module constants).
static constexpr PresetKey kPresetKeys[kParamCount] = {
   {"Global", "voiceMode"},
   {"Global", "bendRange"},
   {"Final", "level"},
   {"FM6Op", "algorithm"},
   {"FM6Op", "feedback"},
   {"FM6Op", "pitch"},
   {"FM6Op", "fine"},
   {"FM6Op", "detune"},
   {"FM6Op", "oversample"},
   {"FM6Op", "op1Wave"},
   {"FM6Op", "op1Ratio"},
   {"FM6Op", "op1Fine"},
   {"FM6Op", "op1Level"},
   {"FM6Op", "op1Delay"},
   {"FM6Op", "op1Attack"},
   {"FM6Op", "op1Decay"},
   {"FM6Op", "op1Sustain"},
   {"FM6Op", "op1Release"},
   {"FM6Op", "op2Wave"},
   {"FM6Op", "op2Ratio"},
   {"FM6Op", "op2Fine"},
   {"FM6Op", "op2Level"},
   {"FM6Op", "op2Delay"},
   {"FM6Op", "op2Attack"},
   {"FM6Op", "op2Decay"},
   {"FM6Op", "op2Sustain"},
   {"FM6Op", "op2Release"},
   {"FM6Op", "op3Wave"},
   {"FM6Op", "op3Ratio"},
   {"FM6Op", "op3Fine"},
   {"FM6Op", "op3Level"},
   {"FM6Op", "op3Delay"},
   {"FM6Op", "op3Attack"},
   {"FM6Op", "op3Decay"},
   {"FM6Op", "op3Sustain"},
   {"FM6Op", "op3Release"},
   {"FM6Op", "op4Wave"},
   {"FM6Op", "op4Ratio"},
   {"FM6Op", "op4Fine"},
   {"FM6Op", "op4Level"},
   {"FM6Op", "op4Delay"},
   {"FM6Op", "op4Attack"},
   {"FM6Op", "op4Decay"},
   {"FM6Op", "op4Sustain"},
   {"FM6Op", "op4Release"},
   {"FM6Op", "op5Wave"},
   {"FM6Op", "op5Ratio"},
   {"FM6Op", "op5Fine"},
   {"FM6Op", "op5Level"},
   {"FM6Op", "op5Delay"},
   {"FM6Op", "op5Attack"},
   {"FM6Op", "op5Decay"},
   {"FM6Op", "op5Sustain"},
   {"FM6Op", "op5Release"},
   {"FM6Op", "op6Wave"},
   {"FM6Op", "op6Ratio"},
   {"FM6Op", "op6Fine"},
   {"FM6Op", "op6Level"},
   {"FM6Op", "op6Delay"},
   {"FM6Op", "op6Attack"},
   {"FM6Op", "op6Decay"},
   {"FM6Op", "op6Sustain"},
   {"FM6Op", "op6Release"},
   {"LPF", "frequency"},
   {"LPF", "q"},
   {"LPF", "envAmount"},
   {"LPF", "pressure"},
   {"FilterEnv", "attack"},
   {"FilterEnv", "decay"},
   {"FilterEnv", "sustain"},
   {"FilterEnv", "release"},
   {"Chorus", "mix"},
   {"Chorus", "rate"},
   {"Chorus", "depth"},
   {"Chorus", "baseDelay"},
   {"Chorus", "feedback"},
   {"FM6Op", "velFloor"},
   {"FM6Op", "velCurve"},
   {"FM6Op", "brightness"},
   {"FM6Op", "bite"},
   {"FM6Op", "grit"},
   {"FM6Op", "body"},
   {"FM6Op", "touch"},
   {"FM6Op", "keyTrack"},
   {"FM6Op", "loudAttack"},
   {"FM6Op", "loudDecay"},
   {"FM6Op", "loudSustain"},
   {"FM6Op", "loudRelease"},
   {"FM6Op", "brightAttack"},
   {"FM6Op", "brightDecay"},
   {"FM6Op", "brightSustain"},
   {"FM6Op", "brightRelease"},
   // MPE describes the controller, not the sound: factory presets leave it out, and the loader keeps
   // the current value for any key a preset does not mention.
   {"Global", "mpe"},
   // Expression amounts: sound, so presets store them (factory presets as 0).
   {"FM6Op", "pressBright"},
   {"FM6Op", "timbreBright"},
   {"LPF", "timbreCutoff"},
   {"FM6Op", "pressLevel"},
   // Mono glide: sound, so presets store it (factory presets at the minimum, which is off).
   {"Global", "glide"},
   // The rest of the Pitch panel: sound, so presets store them (factory presets: Always, 5 Hz, 0, 0).
   {"Global", "glideMode"},
   {"Vibrato", "rate"},
   {"Vibrato", "depth"},
   {"Vibrato", "wheel"},
   {"Vibrato", "sync"},
};

} // namespace modicumfm

#endif // MODICUMFM_PRESETKEYS_HPP
