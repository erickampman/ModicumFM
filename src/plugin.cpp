// src/plugin.cpp — ModicumFM: the CLAP plugin (host glue + DSP) for a 6-operator FM synth built on
// the vendored PolygraphModular voice engine. The voice is wired in VoiceManager::buildDefaultPatch:
//   note + bend → FM6Op → LPF (filter ADSR + pressure) → chorus (shared LFO) → Final (master level).
//
// ---------------------------------------------------------------------------------------------
// WHY THIS IS RAW CLAP AND NOT dpf_add_plugin
// ---------------------------------------------------------------------------------------------
// DPF's generated CLAP wrapper cannot receive MIDI 2.0 7.9-pitch note events: it advertises only
// CLAP_NOTE_DIALECT_MIDI | CLAP_NOTE_DIALECT_MIDI_MPE (dpf/distrho/src/DistrhoPluginCLAP.cpp) and
// drops CLAP_EVENT_MIDI2 on the floor ("case CLAP_EVENT_MIDI2: break;"). ModicumFM must accept them,
// so it implements the clap_plugin_entry itself, as PlainOldSynth, StruckSynth and IFSynth do, and
// uses DPF only for the GUI (UIExporter + NanoVG, see src/UI/ModicumFMUI.cpp).
//
// The MIDI 2.0 path, end to end:
//   note-ports extension advertises CLAP | MIDI | MIDI2, preferring MIDI2
//     → host sends CLAP_EVENT_MIDI2 with a raw UMP
//     → pm::fromClapMidi2() decodes the two UMP words (pm_midi.cpp decodeUmp)
//     → UmpMessage carries attributeType == kAttrPitch7_9 and the 16-bit attribute
//     → PitchConverterModule's MIDI proc turns it into octaves via
//       PitchConverter::Pitch79ToOctaveOffset()  (note + fraction/512, relative to A440)
//     → that CV drives the FM engine's pitch, so the whole voice tracks the microtonal pitch.
// A MIDI-1.0 host still works: the same port advertises the MIDI dialect and CLAP note events are
// converted to UMP (pm::fromClapNote / pm::fromClapMidi1).

#include <clap/clap.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

#include "ModicumFMParams.h"
#include "VoiceManager.hpp"
#include "clap_ump.h"
#include "pm_addresses.h"
#include "pm_midi.h"
#include "pm_params.h"
#include "plugin_gui.h"

#ifndef MODICUMFM_VERSION_STRING
#define MODICUMFM_VERSION_STRING "0.0.0"
#endif

namespace {

using namespace modicumfm;

// ---------------------------------------------------------------------------------------------
// Parameter plumbing
// ---------------------------------------------------------------------------------------------

// CLAP parameter ids. DPF's generated wrapper used the 0-based parameter index as the clap_id
// (DistrhoPluginCLAP.cpp: "info->id = index"), so this raw entry point keeps exactly that mapping:
// CLAP_INVALID_ID is UINT32_MAX, not 0, and renumbering would silently break the automation in
// every existing host session. (PlainOldSynth uses index+1 only because it was never DPF-built.)
// Values are keyed by symbol in state files, so those stay stable regardless.
inline constexpr uint32_t paramIdForIndex(uint32_t index) { return index; }

inline int paramIndexForId(clap_id id) {
    return id < uint32_t(kParamCount) ? int(id) : -1;
}

inline int paramIndexForSymbol(const char* symbol) {
    for (uint32_t i = 0; i < uint32_t(kParamCount); ++i) {
        if (std::strcmp(kParams[i].symbol, symbol) == 0) return int(i);
    }
    return -1;
}

// VoiceManager's own parameters (polyphony, MIDI filtering, legato) do not live in the per-voice
// module graph, so they are addressed through the reserved MIDI kind.
constexpr AUParameterAddress midiParam(MIDIParamItem item) {
    return MODULE_PARAM_ADDRESS(ModuleParamKindMIDI, ModuleInstance1, item);
}

// Engine chunk length inside process(). Modules read their mod inputs (filter envelope, pressure,
// LFO) once per chunk, so this is the control rate: 64 samples ≈ 1.3 ms at 48 kHz keeps a snappy
// filter envelope smooth, where a full MODULE_BUFF_SIZE (512) chunk would step every ~11 ms.
constexpr uint32_t kControlChunk = 64;

inline float clampParam(uint32_t index, double value) {
    const ParamSpec& s = kParams[index];
    if (!(value == value)) return s.def;                       // NaN → default
    if (value < double(s.min)) return s.min;
    if (value > double(s.max)) return s.max;
    return float(value);
}

// A knob drag on the UI thread: begin_gesture → set (repeatedly) → end_gesture. These travel to the
// audio thread (or to params.flush when the plugin is idle) through this lock-free ring and come out
// as real CLAP param events, so host automation records the move instead of it being invisible.
struct GuiEdit {
    enum Kind : uint8_t { Begin = 0, Value, End };
    uint8_t  kind;
    uint32_t index;
    double   value;
};

class GuiEditRing {
public:
    bool push(const GuiEdit& edit) {
        const uint32_t t = tail.load(std::memory_order_relaxed);
        const uint32_t next = (t + 1) & (kCapacity - 1);
        if (next == head.load(std::memory_order_acquire)) return false;   // full: drop, never block
        buffer[t] = edit;
        tail.store(next, std::memory_order_release);
        return true;
    }
    bool pop(GuiEdit& out) {
        const uint32_t h = head.load(std::memory_order_relaxed);
        if (h == tail.load(std::memory_order_acquire)) return false;
        out = buffer[h];
        head.store((h + 1) & (kCapacity - 1), std::memory_order_release);
        return true;
    }

private:
    static constexpr uint32_t kCapacity = 512;   // power of two
    GuiEdit buffer[kCapacity];
    std::atomic<uint32_t> head{0};   // consumer
    std::atomic<uint32_t> tail{0};   // producer (UI thread)
};

struct Plugin {
    const clap_host_t* host = nullptr;
    clap_plugin_t      clap{};
    void*              gui = nullptr;          // the shell's per-instance GUI state
    double             sampleRate = DEFAULT_SAMPLE_RATE;
    std::unique_ptr<VoiceManager> voiceManager;

    std::atomic<double> paramValues[kParamCount];
    std::atomic<bool>   paramDirtyForGui[kParamCount];   // host changed it → tell the open editor
    std::atomic<bool>   guiEditing[kParamCount];         // user is dragging it → don't fight them
    float               appliedValues[kParamCount]{};    // what the engine currently has

    GuiEditRing guiEdits;
    std::string scratch;    // state-save buffer; reused so save() need not allocate

    // CLAP note-ons the engine may still be playing, so CLAP_EVENT_NOTE_END can be reported when it
    // stops (audio thread only). Fixed capacity: the audio thread never allocates. A note that does
    // not fit is simply not reported.
    struct HostNote {
        pm::UmpMessage on;                         // what the engine matches against (holdsNote)
        int32_t noteId; int16_t port, channel, key; // echoed back exactly as the host sent them
    };
    std::array<HostNote, 128> hostNotes{};
    uint32_t hostNoteCount = 0;

    uint32_t reportedVoiceCount = 0;   // main thread: the voice count the host last heard about
};

Plugin* self(const clap_plugin_t* plugin) { return static_cast<Plugin*>(plugin->plugin_data); }

void requestMainThread(Plugin* plugin) {
    if (plugin->host != nullptr && plugin->host->request_callback != nullptr)
        plugin->host->request_callback(plugin->host);
}

// ---------------------------------------------------------------------------------------------
// Parameter → engine
// ---------------------------------------------------------------------------------------------

void setEngine(Plugin* plugin, AUParameterAddress address, float value) {
    if (plugin->voiceManager && address != 0)
        plugin->voiceManager->setParameter(address, value);
}

void setVoiceManager(Plugin* plugin, AUParameterAddress address, float value) {
    if (plugin->voiceManager && address != 0)
        plugin->voiceManager->setVoiceManagerParameter(address, value);
}

float paramOf(const Plugin* plugin, uint32_t index) {
    return float(plugin->paramValues[index].load(std::memory_order_relaxed));
}

// FM6Op item for engine offset k (kFmAlgorithm .. kFmOversample).
uint8_t fmEngineItem(uint32_t k) {
    switch (k) {
    case kFmAlgorithm:  return FM6OpParamItemAlgorithm;
    case kFmFeedback:   return FM6OpParamItemFeedback;
    case kFmPitch:      return FM6OpParamItemPitch;
    case kFmFine:       return FM6OpParamItemFine;
    case kFmDetune:     return FM6OpParamItemDetune;
    case kFmOversample: return FM6OpParamItemOversample;
    default:            return 0;
    }
}

// FM6Op item for operator item j. FM6Op's six 13-item operator blocks share one layout, so
// operator o's item is Op1's item + 13·o.
uint8_t fmOpItem(uint32_t op, uint32_t j) {
    static constexpr uint8_t kOp1Items[kFmOpCount] = {
        FM6OpParamItemOp1Wave,   FM6OpParamItemOp1Ratio,  FM6OpParamItemOp1Fine,
        FM6OpParamItemOp1Level,  FM6OpParamItemOp1Delay,  FM6OpParamItemOp1Attack,
        FM6OpParamItemOp1Decay,  FM6OpParamItemOp1Sustain, FM6OpParamItemOp1Release};
    constexpr uint8_t kOpStride = FM6OpParamItemOp2AmpInput - FM6OpParamItemOp1AmpInput;
    return static_cast<uint8_t>(kOp1Items[j] + op * kOpStride);
}

// Map a host parameter index onto its VoiceManager module item address (0 = handled in applyParam).
AUParameterAddress vmAddress(uint32_t index) {
    // FM engine.
    if (index >= fmParam(0) && index < fmParam(kFmEngineParamCount)) {
        const uint32_t k    = index - fmParam(0);
        const uint8_t  item = k < kFmOpBase
                            ? fmEngineItem(k)
                            : fmOpItem((k - kFmOpBase) / kFmOpCount, (k - kFmOpBase) % kFmOpCount);
        return MODULE_PARAM_ADDRESS(ModuleParamKindFM6Op, ModuleInstance1, item);
    }

    const auto fm     = [](uint8_t item) { return MODULE_PARAM_ADDRESS(ModuleParamKindFM6Op, ModuleInstance1, item); };
    const auto lpf    = [](uint8_t item) { return MODULE_PARAM_ADDRESS(ModuleParamKindButterworthLowPassFilter, ModuleInstance1, item); };
    const auto adsr   = [](uint8_t item) { return MODULE_PARAM_ADDRESS(ModuleParamKindADSR, ModuleInstance1, item); };
    const auto chorus = [](uint8_t item) { return MODULE_PARAM_ADDRESS(ModuleParamKindChorus, ModuleInstance1, item); };
    switch (index) {
    case kParamBendRange:      return pitchBend1RangeAddress;
    case kParamMpe:            return MODULE_PARAM_ADDRESS(ModuleParamKindPitchBendConverter, ModuleInstance1, PitchBendConverterParamItemMPE);
    case kParamMasterLevel:    return MODULE_PARAM_ADDRESS(ModuleParamKindFinalOutput, ModuleInstance1, FinalOutputParamItemLevel);
    case kParamLpfCutoff:      return lpf(LPFParamItemPitch);
    case kParamLpfResonance:   return lpf(LPFParamItemQ);
    case kParamLpfEnvAmount:   return lpf(LPFParamItemFreqModLevel);    // mod 1 = filter env
    case kParamLpfPressure:    return lpf(LPFParamItemFreqModLevel2);   // mod 2 = pressure
    case kParamFiltAttack:     return adsr(ADSRParamItemAttack);
    case kParamFiltDecay:      return adsr(ADSRParamItemDecay);
    case kParamFiltSustain:    return adsr(ADSRParamItemSustain);
    case kParamFiltRelease:    return adsr(ADSRParamItemRelease);
    case kParamChorusMix:      return chorus(ChorusParamItemMix);
    case kParamChorusRate:     return MODULE_PARAM_ADDRESS(ModuleParamKindSharedLFO, ModuleInstance1, SharedLFOParamItemPitch);
    case kParamChorusDepth:    return chorus(ChorusParamItemModDelayLevel);
    case kParamChorusDelay:    return chorus(ChorusParamItemBaseDelay);
    case kParamChorusFeedback: return chorus(ChorusParamItemFeedback);
    case kParamVelFloor:       return fm(FM6OpParamItemVelFloor);
    case kParamVelCurve:       return fm(FM6OpParamItemVelCurve);
    case kParamBrightness:     return fm(FM6OpParamItemBrightness);
    case kParamBite:           return fm(FM6OpParamItemBite);
    case kParamGrit:           return fm(FM6OpParamItemGrit);
    case kParamBody:           return fm(FM6OpParamItemBody);
    case kParamTouch:          return fm(FM6OpParamItemTouch);
    case kParamKeyTrack:       return fm(FM6OpParamItemKeyTrack);
    case kParamLoudAttack:     return fm(FM6OpParamItemLoudAttack);
    case kParamLoudDecay:      return fm(FM6OpParamItemLoudDecay);
    case kParamLoudSustain:    return fm(FM6OpParamItemLoudSustain);
    case kParamLoudRelease:    return fm(FM6OpParamItemLoudRelease);
    case kParamBrightAttack:   return fm(FM6OpParamItemBrightAttack);
    case kParamBrightDecay:    return fm(FM6OpParamItemBrightDecay);
    case kParamBrightSustain:  return fm(FM6OpParamItemBrightSustain);
    case kParamBrightRelease:  return fm(FM6OpParamItemBrightRelease);
    default:                   return 0;
    }
}

bool isMonoMode(const Plugin* plugin) {
    return int(paramOf(plugin, kParamVoiceMode) + 0.5f) == 0;   // 0 = Mono, 1 = poly
}

constexpr uint32_t kPolyVoices = 10;

// Voice mode: Mono = 1 voice + VoiceManager legato (last-note note-stack), the filter envelope
// and the FM engine in true legato (no re-attack while a note is held). Poly = 10 voices.
void applyVoiceMode(Plugin* plugin) {
    const bool mono = isMonoMode(plugin);
    setVoiceManager(plugin, midiParam(MIDIParamItemVoiceCount), mono ? 1.0f : float(kPolyVoices));
    setVoiceManager(plugin, midiParam(MIDIParamItemLegato),     mono ? 1.0f : 0.0f);
    const float legato = mono ? 2.0f : 0.0f;   // 0 = Steal, 2 = LegatoTrue
    setEngine(plugin, MODULE_PARAM_ADDRESS(ModuleParamKindADSR, ModuleInstance1, ADSRParamItemLegato), legato);
    setEngine(plugin, MODULE_PARAM_ADDRESS(ModuleParamKindFM6Op, ModuleInstance1, FM6OpParamItemRetrigger), legato);
    requestMainThread(plugin);   // the voice count may have changed: tell the host (voice-info)
}

// One host parameter → the engine: the voice mode sets several things, everything else is a single
// module write.
void applyParam(Plugin* plugin, uint32_t index, float value) {
    if (!plugin->voiceManager) return;
    if (index == uint32_t(kParamVoiceMode)) { applyVoiceMode(plugin); return; }
    const AUParameterAddress a = vmAddress(index);
    if (a != 0) setEngine(plugin, a, value);
}

// Push every host parameter into the engine and record what the engine now has.
void applyAllParams(Plugin* plugin) {
    for (uint32_t i = 0; i < uint32_t(kParamCount); ++i) {
        const float value = paramOf(plugin, i);
        plugin->appliedValues[i] = value;
        applyParam(plugin, i, value);
    }
}

// Called once per process() before rendering: picks up anything the UI thread changed. Comparing
// against appliedValues keeps the per-block cost at zero when nothing moved.
void applyPendingParams(Plugin* plugin) {
    for (uint32_t i = 0; i < uint32_t(kParamCount); ++i) {
        const float value = paramOf(plugin, i);
        if (value != plugin->appliedValues[i]) {
            plugin->appliedValues[i] = value;
            applyParam(plugin, i, value);
        }
    }
}

void notifyHostValuesChanged(Plugin* plugin) {
    if (plugin->host == nullptr) return;
    const auto* params = static_cast<const clap_host_params_t*>(
        plugin->host->get_extension(plugin->host, CLAP_EXT_PARAMS));
    if (params != nullptr && params->rescan != nullptr)
        params->rescan(plugin->host, CLAP_PARAM_RESCAN_VALUES);
}

// Store a value that originated on the GUI or from a state file (i.e. NOT from the host).
void storeParam(Plugin* plugin, uint32_t index, double value) {
    const float clamped = clampParam(index, value);
    plugin->paramValues[index].store(clamped, std::memory_order_relaxed);
    plugin->paramDirtyForGui[index].store(true, std::memory_order_release);
}

// ---------------------------------------------------------------------------------------------
// CLAP events
// ---------------------------------------------------------------------------------------------

void processEvent(Plugin* plugin, const clap_event_header_t* header) {
    if (header->space_id != CLAP_CORE_EVENT_SPACE_ID) return;

    switch (header->type) {
    case CLAP_EVENT_NOTE_ON: {
        const auto* note = reinterpret_cast<const clap_event_note_t*>(header);
        if (!plugin->voiceManager) break;
        const pm::UmpMessage on = pm::fromClapNote(*note, true);
        plugin->voiceManager->handleMIDIMessage(on);
        if (plugin->hostNoteCount < plugin->hostNotes.size())
            plugin->hostNotes[plugin->hostNoteCount++] =
                { on, note->note_id, note->port_index, note->channel, note->key };
        break;
    }
    case CLAP_EVENT_NOTE_OFF:
    case CLAP_EVENT_NOTE_CHOKE: {
        const auto* note = reinterpret_cast<const clap_event_note_t*>(header);
        if (plugin->voiceManager)
            plugin->voiceManager->handleMIDIMessage(pm::fromClapNote(*note, false));
        break;
    }
    case CLAP_EVENT_NOTE_EXPRESSION: {   // per-note tuning / pressure / brightness, CLAP dialect (Bitwig)
        const auto* expression = reinterpret_cast<const clap_event_note_expression_t*>(header);
        if (!plugin->voiceManager) break;
        const int key = expression->key >= 0 ? expression->key
                                             : plugin->voiceManager->keyForNoteId(expression->note_id);
        if (const auto message = pm::fromClapNoteExpression(*expression, key))
            plugin->voiceManager->handleMIDIMessage(*message);
        break;
    }
    case CLAP_EVENT_MIDI: {          // MIDI 1.0, three bytes
        const auto* midi = reinterpret_cast<const clap_event_midi_t*>(header);
        if (plugin->voiceManager)
            plugin->voiceManager->handleMIDIMessage(pm::fromClapMidi1(*midi));
        break;
    }
    case CLAP_EVENT_MIDI2: {         // MIDI 2.0 UMP — this is where 7.9-pitch notes arrive
        const auto* midi = reinterpret_cast<const clap_event_midi2_t*>(header);
        if (plugin->voiceManager)
            plugin->voiceManager->handleMIDIMessage(pm::fromClapMidi2(*midi));
        break;
    }
    case CLAP_EVENT_PARAM_VALUE: {
        const auto* event = reinterpret_cast<const clap_event_param_value_t*>(header);
        const int index = paramIndexForId(event->param_id);
        if (index < 0) break;
        const uint32_t i = uint32_t(index);
        const float clamped = clampParam(i, event->value);
        plugin->paramValues[i].store(clamped, std::memory_order_relaxed);
        plugin->paramDirtyForGui[i].store(true, std::memory_order_release);
        if (plugin->voiceManager) {
            plugin->appliedValues[i] = clamped;
            applyParam(plugin, i, clamped);
        }
        requestMainThread(plugin);
        break;
    }
    default:
        break;
    }
}

// CLAP_EVENT_NOTE_END for every host note the engine has stopped playing. A voice rings on
// after note-off, so this is usually long after the note-off: the moment the voice falls silent, or
// is stolen, or (mono) the note leaves the held stack. Hosts such as Bitwig keep a note's per-voice
// modulation and expressions alive until they see it. `time` is the frame the check runs at.
void reportEndedNotes(Plugin* plugin, const clap_output_events_t* out, uint32_t time) {
    for (uint32_t i = 0; i < plugin->hostNoteCount; ) {
        const Plugin::HostNote& note = plugin->hostNotes[i];
        if (plugin->voiceManager && plugin->voiceManager->holdsNote(note.on)) { ++i; continue; }
        if (out != nullptr && out->try_push != nullptr) {
            clap_event_note_t event{};
            event.header.size     = sizeof(event);
            event.header.time     = time;
            event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
            event.header.type     = CLAP_EVENT_NOTE_END;
            event.note_id    = note.noteId;
            event.port_index = note.port;
            event.channel    = note.channel;
            event.key        = note.key;
            event.velocity   = 0.0;
            out->try_push(out, &event.header);
        }
        plugin->hostNotes[i] = plugin->hostNotes[--plugin->hostNoteCount];   // order does not matter
    }
}

// GUI edits → CLAP param/gesture events. Called from process() and from params.flush() (those two
// are mutually exclusive per the CLAP spec, which is what makes the SPSC ring sufficient).
void emitGuiEdits(Plugin* plugin, const clap_output_events_t* out) {
    GuiEdit edit;
    while (plugin->guiEdits.pop(edit)) {
        if (edit.index >= uint32_t(kParamCount)) continue;
        if (out == nullptr || out->try_push == nullptr) continue;
        const clap_id id = clap_id(paramIdForIndex(edit.index));

        if (edit.kind == GuiEdit::Value) {
            clap_event_param_value_t event{};
            event.header.size     = sizeof(event);
            event.header.time     = 0;
            event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
            event.header.type     = CLAP_EVENT_PARAM_VALUE;
            event.param_id        = id;
            event.value           = edit.value;
            out->try_push(out, &event.header);
        } else {
            clap_event_param_gesture_t event{};
            event.header.size     = sizeof(event);
            event.header.time     = 0;
            event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
            event.header.type     = (edit.kind == GuiEdit::Begin) ? CLAP_EVENT_PARAM_GESTURE_BEGIN
                                                                 : CLAP_EVENT_PARAM_GESTURE_END;
            event.param_id        = id;
            out->try_push(out, &event.header);
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Plugin lifecycle
// ---------------------------------------------------------------------------------------------

bool plug_init(const clap_plugin_t*) { return true; }

void plug_destroy(const clap_plugin_t* plugin) { delete self(plugin); }

bool plug_activate(const clap_plugin_t* plugin, double sampleRate, uint32_t, uint32_t) {
    Plugin* p = self(plugin);
    p->sampleRate = sampleRate > 0.0 ? sampleRate : DEFAULT_SAMPLE_RATE;
    p->voiceManager = std::make_unique<VoiceManager>(p->sampleRate, MODULE_BUFF_SIZE);

    // VoiceManager's own params are not zero-initialised — set the ones we never expose. Voice count
    // and legato are driven by the Voices parameter (applyVoiceMode), pushed below.
    setVoiceManager(p, midiParam(MIDIParamItemChannelFilterValue), 0.0f);
    setVoiceManager(p, midiParam(MIDIParamItemGroupFilterValue),   0.0f);
    setVoiceManager(p, midiParam(MIDIParamItemVoiceStealAlgorithm), float(VoiceStealAlgorithmOldest));
    setVoiceManager(p, midiParam(MIDIParamItemFreeRun),            0.0f);

    // Fixed voicing that isn't exposed as a param: full stereo width out of the Final stage (the
    // engine's detune and the chorus make the width), and the chorus LFO's right channel 90° ahead
    // of the left for a wide stereo sweep.
    setEngine(p, MODULE_PARAM_ADDRESS(ModuleParamKindFinalOutput, ModuleInstance1, FinalOutputParamItemSpread), 1.0f);
    setEngine(p, MODULE_PARAM_ADDRESS(ModuleParamKindSharedLFO, ModuleInstance1, SharedLFOParamItemPhase),
              float(LFOParamLRPhase90));

    // Every parameter: this is what the DPF build did from sampleRateChanged()/the constructor.
    applyAllParams(p);
    return true;
}

void plug_deactivate(const clap_plugin_t* plugin) {
    Plugin* p = self(plugin);
    p->voiceManager.reset();
    p->hostNoteCount = 0;   // deactivation ends every note; the host does not expect NOTE_END for them
}

bool plug_start_processing(const clap_plugin_t*) { return true; }

void plug_stop_processing(const clap_plugin_t*) {}

void plug_reset(const clap_plugin_t* plugin) {
    Plugin* p = self(plugin);
    if (p->voiceManager) p->voiceManager->reset();
}

uint32_t currentVoiceCount(const Plugin* plugin) { return isMonoMode(plugin) ? 1 : kPolyVoices; }

void plug_on_main_thread(const clap_plugin_t* plugin) {
    Plugin* p = self(plugin);

    // voice-info: CLAP asks for changed() whenever the voice count changes (Mono/Poly switch).
    const uint32_t voices = currentVoiceCount(p);
    if (voices != p->reportedVoiceCount && p->host != nullptr) {
        p->reportedVoiceCount = voices;
        const auto* info = static_cast<const clap_host_voice_info_t*>(
            p->host->get_extension(p->host, CLAP_EXT_VOICE_INFO));
        if (info != nullptr && info->changed != nullptr) info->changed(p->host);
    }

    if (p->gui == nullptr) return;
    for (uint32_t i = 0; i < uint32_t(kParamCount); ++i) {
        if (!p->paramDirtyForGui[i].exchange(false, std::memory_order_acq_rel)) continue;
        if (p->guiEditing[i].load(std::memory_order_relaxed)) continue;   // the user is dragging it
        mfm_gui_push_param(p->gui, i, p->paramValues[i].load(std::memory_order_relaxed));
    }
}

clap_process_status plug_process(const clap_plugin_t* plugin, const clap_process_t* process) {
    Plugin* p = self(plugin);

    const clap_input_events_t* in = process->in_events;
    const uint32_t numEvents = in != nullptr ? in->size(in) : 0;

    emitGuiEdits(p, process->out_events);

    const uint32_t frames = process->frames_count;
    float* left = nullptr;
    float* right = nullptr;
    for (uint32_t i = 0; i < process->audio_outputs_count; ++i) {
        const clap_audio_buffer_t& out = process->audio_outputs[i];
        if (out.data32 == nullptr || out.channel_count < 2) continue;
        left  = out.data32[0];
        right = out.data32[1];
        break;
    }

    // No usable output (or not activated): still consume the events so notes are not lost.
    if (left == nullptr || right == nullptr || !p->voiceManager) {
        for (uint32_t i = 0; i < numEvents; ++i) processEvent(p, in->get(in, i));
        return CLAP_PROCESS_CONTINUE;
    }

    applyPendingParams(p);

    std::memset(left,  0, sizeof(float) * frames);
    std::memset(right, 0, sizeof(float) * frames);

    // Sample-accurate render: apply every event at its own frame offset, rendering forward to the
    // next event (capped at one control chunk) so notes land on the sample the host asked for instead
    // of being quantized to the top of the block.
    uint32_t nextEvent = 0;
    for (uint32_t offset = 0; offset < frames; ) {
        while (nextEvent < numEvents) {
            const clap_event_header_t* header = in->get(in, nextEvent);
            const uint32_t eventTime = header->time < frames ? header->time : frames - 1;
            if (eventTime > offset) break;
            processEvent(p, header);
            ++nextEvent;
        }

        uint32_t end = frames;
        if (nextEvent < numEvents) {
            const clap_event_header_t* header = in->get(in, nextEvent);
            const uint32_t eventTime = header->time < frames ? header->time : frames - 1;
            if (eventTime > offset && eventTime < end) end = eventTime;
        }
        if (end - offset > kControlChunk) end = offset + kControlChunk;

        p->voiceManager->process(end - offset, left + offset, right + offset);
        reportEndedNotes(p, process->out_events, end - 1);
        offset = end;
    }

    for (; nextEvent < numEvents; ++nextEvent) processEvent(p, in->get(in, nextEvent));

    return CLAP_PROCESS_CONTINUE;
}

// ---------------------------------------------------------------------------------------------
// audio-ports extension — one stereo output
// ---------------------------------------------------------------------------------------------

uint32_t audio_ports_count(const clap_plugin_t*, bool isInput) { return isInput ? 0 : 1; }

bool audio_ports_get(const clap_plugin_t*, uint32_t index, bool isInput, clap_audio_port_info_t* info) {
    if (isInput || index != 0) return false;
    info->id = 0;
    std::strncpy(info->name, "Output", sizeof(info->name) - 1);
    info->channel_count = 2;
    info->flags         = CLAP_AUDIO_PORT_IS_MAIN;
    info->port_type     = CLAP_PORT_STEREO;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}

const clap_plugin_audio_ports_t kAudioPorts = { audio_ports_count, audio_ports_get };

// ---------------------------------------------------------------------------------------------
// note-ports extension — one input port that prefers MIDI 2.0
// ---------------------------------------------------------------------------------------------
// Advertising (and preferring) CLAP_NOTE_DIALECT_MIDI2 is what makes a MIDI-2.0-capable host send
// raw UMP notes, which is the only way a 7.9-pitch NoteOn reaches the oscillators unchanged. The
// CLAP and MIDI-1.0 dialects stay advertised so MIDI-1.0-only hosts still deliver notes.
uint32_t note_ports_count(const clap_plugin_t*, bool isInput) { return isInput ? 1 : 0; }

bool note_ports_get(const clap_plugin_t*, uint32_t index, bool isInput, clap_note_port_info_t* info) {
    if (!isInput || index != 0) return false;
    info->id = 0;
    info->supported_dialects = CLAP_NOTE_DIALECT_CLAP
                             | CLAP_NOTE_DIALECT_MIDI
                             | CLAP_NOTE_DIALECT_MIDI2;   // <-- the MIDI 2.0 half of the fix
    info->preferred_dialect  = CLAP_NOTE_DIALECT_MIDI2;
    std::strncpy(info->name, "Notes", sizeof(info->name) - 1);
    return true;
}

const clap_plugin_note_ports_t kNotePorts = { note_ports_count, note_ports_get };

// ---------------------------------------------------------------------------------------------
// params extension
// ---------------------------------------------------------------------------------------------

void formatParamValue(uint32_t index, double value, char* out, uint32_t size) {
    const ParamSpec& spec = kParams[index];
    if (const char* const* choices = choicesFor(index)) {
        int count = 0;
        while (choices[count] != nullptr) ++count;
        int choice = int(value + 0.5);
        choice = std::clamp(choice, 0, count - 1);
        std::snprintf(out, size, "%s", choices[choice]);
    } else if (spec.integer) {
        std::snprintf(out, size, "%d", int(value + (value < 0.0 ? -0.5 : 0.5)));
    } else if (std::strcmp(spec.unit, "s") == 0) {
        // Sub-second times read better in ms; %.4g keeps the value → text → value → text
        // round-trip stable, which CLAP hosts and validators check (params_text_to_value parses the
        // "ms" suffix back).
        if (value < 1.0) std::snprintf(out, size, "%.4g ms", value * 1000.0);
        else             std::snprintf(out, size, "%.4g s", value);
    } else if (spec.unit[0] != '\0') {
        std::snprintf(out, size, "%.4g %s", value, spec.unit);
    } else {
        std::snprintf(out, size, "%.4g", value);
    }
}

uint32_t params_count(const clap_plugin_t*) { return uint32_t(kParamCount); }

bool params_get_info(const clap_plugin_t*, uint32_t index, clap_param_info_t* info) {
    if (index >= uint32_t(kParamCount)) return false;
    const ParamSpec& spec = kParams[index];
    std::memset(info, 0, sizeof(*info));
    info->id            = clap_id(paramIdForIndex(index));
    info->flags         = CLAP_PARAM_IS_AUTOMATABLE;
    if (spec.integer) info->flags |= CLAP_PARAM_IS_STEPPED;
    if (choicesFor(index) != nullptr) info->flags |= CLAP_PARAM_IS_ENUM;
    info->min_value     = spec.min;
    info->max_value     = spec.max;
    info->default_value = spec.def;
    std::strncpy(info->name, spec.name, sizeof(info->name) - 1);
    std::strncpy(info->module, "ModicumFM", sizeof(info->module) - 1);
    return true;
}

bool params_get_value(const clap_plugin_t* plugin, clap_id id, double* out) {
    const int index = paramIndexForId(id);
    if (index < 0) return false;
    *out = self(plugin)->paramValues[index].load(std::memory_order_relaxed);
    return true;
}

bool params_value_to_text(const clap_plugin_t*, clap_id id, double value, char* out, uint32_t size) {
    const int index = paramIndexForId(id);
    if (index < 0) return false;
    formatParamValue(uint32_t(index), value, out, size);
    return true;
}

bool params_text_to_value(const clap_plugin_t*, clap_id id, const char* text, double* out) {
    const int index = paramIndexForId(id);
    if (index < 0) return false;

    // Accept the dropdown label as well as a number, so hosts that round-trip text work.
    if (const char* const* choices = choicesFor(uint32_t(index))) {
        int count = 0;
        while (choices[count] != nullptr) ++count;
        for (int i = 0; i < count; ++i) {
            if (std::strcmp(choices[i], text) == 0) { *out = double(i); return true; }
        }
    }
    // Plain number, or the "<number> ms" form formatParamValue() prints for sub-second times. Only
    // seconds params are scaled: Chorus Depth and Delay are in ms already, and print that unit.
    char* end = nullptr;
    const double parsed = std::strtod(text, &end);
    if (end == text) return false;                  // not a number
    if (end != nullptr && std::strcmp(kParams[index].unit, "s") == 0) {
        while (*end == ' ') ++end;
        if (end[0] == 'm' && end[1] == 's') {
            *out = parsed / 1000.0;
            return true;
        }
    }
    *out = parsed;
    return true;
}

void params_flush(const clap_plugin_t* plugin, const clap_input_events_t* in, const clap_output_events_t* out) {
    Plugin* p = self(plugin);
    if (in != nullptr) {
        const uint32_t count = in->size(in);
        for (uint32_t i = 0; i < count; ++i) processEvent(p, in->get(in, i));
    }
    emitGuiEdits(p, out);
}

const clap_plugin_params_t kParamsExt = {
    params_count, params_get_info, params_get_value,
    params_value_to_text, params_text_to_value, params_flush,
};

// ---------------------------------------------------------------------------------------------
// state extension
// ---------------------------------------------------------------------------------------------
// A tiny line-based text format rather than JSON, so the DSP side has no parser dependency:
//     ModicumFM 1
//     <symbol> <value>
//     ...
// Parameters are keyed by symbol, so adding/reordering parameters can never misinterpret an old
// state file; unknown symbols are ignored.

bool streamWrite(const clap_ostream_t* stream, const void* data, uint64_t size) {
    const auto* bytes = static_cast<const uint8_t*>(data);
    for (uint64_t done = 0; done < size; ) {
        const int64_t written = stream->write(stream, bytes + done, size - done);
        if (written <= 0) return false;
        done += uint64_t(written);
    }
    return true;
}

bool state_save(const clap_plugin_t* plugin, const clap_ostream_t* stream) {
    Plugin* p = self(plugin);
    p->scratch.clear();
    p->scratch += "ModicumFM 1\n";
    char line[128];
    for (uint32_t i = 0; i < uint32_t(kParamCount); ++i) {
        std::snprintf(line, sizeof(line), "%s %.9g\n", kParams[i].symbol,
                      p->paramValues[i].load(std::memory_order_relaxed));
        p->scratch += line;
    }
    return streamWrite(stream, p->scratch.data(), p->scratch.size());
}

// Host sessions saved while ModicumFM was built with dpf_add_plugin carry DPF's auto-generated state
// blob — NUL-separated tokens, because DPF writes "\xff" separators and then replaces them:
//     __dpf_parameters_begin__ \0 symbol \0 value \0 ... __dpf_parameters_end__ \0 \xfe
// DPF emitted that even with DISTRHO_PLUGIN_WANT_STATE 0, so parse it and keep those sessions
// restoring correctly across the switch to the raw entry point. Returns true if anything matched.
bool stateLoadDpfParameters(Plugin* p, const std::string& text) {
    size_t pos = text.find("__dpf_parameters_begin__");
    if (pos == std::string::npos) return false;
    pos += std::strlen("__dpf_parameters_begin__");
    if (pos < text.size() && text[pos] == '\0') ++pos;

    bool any = false;
    while (pos < text.size()) {
        const size_t keyEnd = text.find('\0', pos);
        if (keyEnd == std::string::npos) break;
        const std::string key = text.substr(pos, keyEnd - pos);
        pos = keyEnd + 1;
        if (key.empty() || key == "__dpf_parameters_end__") break;

        const size_t valueEnd = text.find('\0', pos);
        if (valueEnd == std::string::npos) break;
        const std::string value = text.substr(pos, valueEnd - pos);
        pos = valueEnd + 1;

        const int index = paramIndexForSymbol(key.c_str());
        if (index >= 0) {
            storeParam(p, uint32_t(index), std::strtod(value.c_str(), nullptr));
            any = true;
        }
    }
    return any;
}

bool state_load(const clap_plugin_t* plugin, const clap_istream_t* stream) {
    Plugin* p = self(plugin);

    std::string text;
    char buffer[512];
    for (;;) {
        const int64_t read = stream->read(stream, buffer, sizeof(buffer));
        if (read < 0) return false;
        if (read == 0) break;
        text.append(buffer, size_t(read));
    }

    // Empty data, or data that is not ours, is a failure — not a silent "loaded defaults".
    // Idempotent hosts and validators both rely on this distinction.
    const size_t firstBreak = text.find('\n');
    const std::string header = text.substr(0, firstBreak == std::string::npos ? text.size() : firstBreak);
    if (header.rfind("ModicumFM", 0) != 0) {
        if (!stateLoadDpfParameters(p, text)) return false;
        notifyHostValuesChanged(p);
        requestMainThread(p);
        return true;
    }

    // Only store the values here. state_load runs on the main thread, and applying them would
    // rewire the voice graph under a running process(). If the plugin is active,
    // applyPendingParams() picks them up at the top of the next block; if not, activate() applies
    // everything.
    size_t pos = firstBreak == std::string::npos ? text.size() : firstBreak + 1;
    while (pos < text.size()) {
        size_t lineEnd = text.find('\n', pos);
        if (lineEnd == std::string::npos) lineEnd = text.size();
        const std::string line = text.substr(pos, lineEnd - pos);
        pos = lineEnd + 1;

        const size_t space = line.find(' ');
        if (space == std::string::npos) continue;
        const int index = paramIndexForSymbol(line.substr(0, space).c_str());
        if (index < 0) continue;                       // unknown symbol: ignore, stay compatible
        storeParam(p, uint32_t(index), std::strtod(line.c_str() + space + 1, nullptr));
    }

    notifyHostValuesChanged(p);
    requestMainThread(p);
    return true;
}

const clap_plugin_state_t kStateExt = { state_save, state_load };

// ---------------------------------------------------------------------------------------------
// gui + timer extensions (the actual UI lives in the GUI shell)
// ---------------------------------------------------------------------------------------------

void plug_on_timer(const clap_plugin_t* plugin, clap_id timerId) {
    mfm_gui_on_timer(plugin, timerId);
}

const clap_plugin_timer_support_t kTimerExt = { plug_on_timer };

// ---------------------------------------------------------------------------------------------
// voice-info
// ---------------------------------------------------------------------------------------------
// Tells the host (Bitwig) how many voices to expect, and that it may overlap notes on the same key:
// with note ids the engine keeps them apart (pm::isSameNote), so each gets its own voice.
bool voice_info_get(const clap_plugin_t* plugin, clap_voice_info_t* info) {
    Plugin* p = self(plugin);
    const uint32_t voices = currentVoiceCount(p);
    p->reportedVoiceCount = voices;
    info->voice_count    = voices;
    info->voice_capacity = kPolyVoices;
    info->flags          = CLAP_VOICE_INFO_SUPPORTS_OVERLAPPING_NOTES;
    return true;
}

const clap_plugin_voice_info_t kVoiceInfoExt = { voice_info_get };

const void* plug_get_extension(const clap_plugin_t*, const char* id) {
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS)    == 0) return &kAudioPorts;
    if (std::strcmp(id, CLAP_EXT_NOTE_PORTS)     == 0) return &kNotePorts;
    if (std::strcmp(id, CLAP_EXT_PARAMS)         == 0) return &kParamsExt;
    if (std::strcmp(id, CLAP_EXT_STATE)          == 0) return &kStateExt;
    if (std::strcmp(id, CLAP_EXT_GUI)            == 0) return mfm_gui_extension();
    if (std::strcmp(id, CLAP_EXT_TIMER_SUPPORT)  == 0) return &kTimerExt;
    if (std::strcmp(id, CLAP_EXT_VOICE_INFO)     == 0) return &kVoiceInfoExt;
    return nullptr;
}

// ---------------------------------------------------------------------------------------------
// descriptor + factory + entry
// ---------------------------------------------------------------------------------------------

const char* kFeatures[] = {
    CLAP_PLUGIN_FEATURE_INSTRUMENT,
    CLAP_PLUGIN_FEATURE_SYNTHESIZER,
    CLAP_PLUGIN_FEATURE_STEREO,
    nullptr,
};

// The id is the one DPF's generated wrapper used (DISTRHO_PLUGIN_CLAP_ID), so host sessions saved
// before the switch to this entry point still find the plugin.
const clap_plugin_descriptor_t kDescriptor = {
    .clap_version = CLAP_VERSION_INIT,
    .id           = "com.unlikelyware.modicumfm",
    .name         = "ModicumFM",
    .vendor       = "Unlikelyware",
    .url          = "https://www.unlikelyware.com",
    .manual_url   = "",
    .support_url  = "",
    .version      = MODICUMFM_VERSION_STRING,
    .description  = "A 6-operator FM synth built on the PolygraphModular voice engine.",
    .features     = kFeatures,
};

const clap_plugin_t* factory_create(const clap_plugin_factory_t*, const clap_host_t* host, const char* id) {
    if (std::strcmp(id, kDescriptor.id) != 0) return nullptr;

    auto* p = new (std::nothrow) Plugin();
    if (p == nullptr) return nullptr;
    p->host = host;
    for (uint32_t i = 0; i < uint32_t(kParamCount); ++i) {
        p->paramValues[i].store(kParams[i].def, std::memory_order_relaxed);
        p->paramDirtyForGui[i].store(false, std::memory_order_relaxed);
        p->guiEditing[i].store(false, std::memory_order_relaxed);
        p->appliedValues[i] = kParams[i].def;
    }
    p->clap = clap_plugin_t{
        .desc             = &kDescriptor,
        .plugin_data      = p,
        .init             = plug_init,
        .destroy          = plug_destroy,
        .activate         = plug_activate,
        .deactivate       = plug_deactivate,
        .start_processing = plug_start_processing,
        .stop_processing  = plug_stop_processing,
        .reset            = plug_reset,
        .process          = plug_process,
        .get_extension    = plug_get_extension,
        .on_main_thread   = plug_on_main_thread,
    };
    return &p->clap;
}

uint32_t factory_count(const clap_plugin_factory_t*) { return 1; }

const clap_plugin_descriptor_t* factory_get(const clap_plugin_factory_t*, uint32_t index) {
    return index == 0 ? &kDescriptor : nullptr;
}

const clap_plugin_factory_t kFactory = { factory_count, factory_get, factory_create };

bool entry_init(const char*) { return true; }

void entry_deinit() {}

const void* entry_get_factory(const char* factoryId) {
    if (std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) == 0) return &kFactory;
    return nullptr;
}

} // namespace

// ---------------------------------------------------------------------------------------------
// The C seam the GUI shell calls (see src/plugin_gui.h). These live in the same translation unit,
// so the anonymous-namespace Plugin above is directly visible.
// ---------------------------------------------------------------------------------------------
extern "C" {

const clap_host_t* mfm_plugin_host(const clap_plugin_t* plugin) { return self(plugin)->host; }

void  mfm_plugin_set_gui(const clap_plugin_t* plugin, void* gui) { self(plugin)->gui = gui; }
void* mfm_plugin_get_gui(const clap_plugin_t* plugin)           { return self(plugin)->gui; }

uint32_t mfm_param_count(void) { return uint32_t(kParamCount); }

double mfm_param_value(const clap_plugin_t* plugin, uint32_t index) {
    if (index >= uint32_t(kParamCount)) return 0.0;
    return self(plugin)->paramValues[index].load(std::memory_order_relaxed);
}

void mfm_param_begin_gesture(const clap_plugin_t* plugin, uint32_t index) {
    if (index >= uint32_t(kParamCount)) return;
    Plugin* p = self(plugin);
    p->guiEditing[index].store(true, std::memory_order_relaxed);
    p->guiEdits.push({ GuiEdit::Begin, index, 0.0 });
}

void mfm_param_set_from_gui(const clap_plugin_t* plugin, uint32_t index, double value) {
    if (index >= uint32_t(kParamCount)) return;
    Plugin* p = self(plugin);
    const float clamped = clampParam(index, value);
    p->paramValues[index].store(clamped, std::memory_order_relaxed);
    p->guiEdits.push({ GuiEdit::Value, index, double(clamped) });
}

void mfm_param_end_gesture(const clap_plugin_t* plugin, uint32_t index) {
    if (index >= uint32_t(kParamCount)) return;
    Plugin* p = self(plugin);
    p->guiEditing[index].store(false, std::memory_order_relaxed);
    p->guiEdits.push({ GuiEdit::End, index, 0.0 });
}

} // extern "C"

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry = {
    .clap_version = CLAP_VERSION_INIT,
    .init         = entry_init,
    .deinit       = entry_deinit,
    .get_factory  = entry_get_factory,
};
