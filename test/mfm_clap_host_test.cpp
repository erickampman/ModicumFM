// test/mfm_clap_host_test.cpp — drives the ACTUAL built ModicumFM.clap through the CLAP C API, with no
// DAW and no GUI. This is the end-to-end proof of the thing that matters most here: that a MIDI 2.0
// NoteOn carrying a 7.9-pitch attribute survives the plugin's note port, reaches the FM engine, and
// comes back out at the right (microtonal) frequency, and that pitch bend (channel, MIDI 2.0 per-note,
// registered controller 7) and Pitch 7.25 move it by the right amount. Ported from IFSynth's
// ifs_clap_host_test.
//
// It is also the only check that exercises note-port dialect negotiation — the reason this plugin
// implements CLAP directly instead of using DPF's wrapper, which drops the event outright and
// advertises only the MIDI/MPE dialects.
//
// The pitch assertion is a RATIO between two renderings of the same note that differ only in the
// 7.9 fraction (0 vs 256 = half a semitone), which is the quantity the MIDI 2.0 attribute controls.
// That makes it robust to the pitch estimator locking onto a harmonic: an octave error cancels in
// the ratio, so only the fractional pitch shows up. Each measurement gets a fresh plugin instance.
//
// Usage: mfm-clap-host-test /path/to/ModicumFM.clap
// Prints "ALL PASS" on success.

#include <clap/clap.h>

#include <dlfcn.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

#include "ModicumFMParams.h"   // parameter ids/enums; header-only, nothing is linked from it

using namespace modicumfm;

namespace {

int gFailures = 0;

void check(bool ok, const std::string& what) {
    std::printf("  [%s] %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) ++gFailures;
}

constexpr double kSampleRate = 48000.0;

// --- minimal host -------------------------------------------------------------------------------
void hostNoop(const clap_host_t*) {}
const void* hostGetExtension(const clap_host_t*, const char*) { return nullptr; }

// --- minimal event list -------------------------------------------------------------------------
struct EventList {
    const clap_event_header_t** events = nullptr;
    uint32_t count = 0;

    static uint32_t size(const clap_input_events_t* list) {
        return static_cast<const EventList*>(list->ctx)->count;
    }
    static const clap_event_header_t* get(const clap_input_events_t* list, uint32_t index) {
        return static_cast<const EventList*>(list->ctx)->events[index];
    }
};

// --- pitch estimate -----------------------------------------------------------------------------
// Normalized autocorrelation over the sustained part of the note. The first lag that comes near the
// best correlation is the fundamental period (later ones are its multiples); climb to that peak's
// top and interpolate it with a parabola, since a whole-sample lag reads sharp by up to a few
// percent.
double estimatePitch(const std::vector<float>& samples, double minHz, double maxHz) {
    const size_t start = samples.size() * 3 / 10;     // skip the attack transient
    const size_t count = samples.size() - start;
    if (count < 256) return 0.0;
    const size_t minLag = std::max<size_t>(2, size_t(kSampleRate / maxHz));
    const size_t maxLag = std::min(count - 1, size_t(kSampleRate / minHz));
    if (maxLag <= minLag) return 0.0;

    double best = 0.0;
    std::vector<double> correlation(maxLag + 1, 0.0);
    for (size_t lag = minLag; lag <= maxLag; ++lag) {
        double dot = 0.0, energyA = 0.0, energyB = 0.0;
        for (size_t i = 0; i + lag < count; ++i) {
            const double a = samples[start + i];
            const double b = samples[start + i + lag];
            dot += a * b;
            energyA += a * a;
            energyB += b * b;
        }
        correlation[lag] = dot / (std::sqrt(energyA * energyB) + 1e-30);
        best = std::max(best, correlation[lag]);
    }
    if (best <= 0.0) return 0.0;
    for (size_t lag = minLag; lag <= maxLag; ++lag) {
        if (correlation[lag] < 0.95 * best) continue;
        while (lag < maxLag && correlation[lag + 1] > correlation[lag]) ++lag;
        double period = double(lag);
        if (lag > minLag && lag < maxLag) {
            const double y0 = correlation[lag - 1], y1 = correlation[lag], y2 = correlation[lag + 1];
            const double denom = y0 - 2.0 * y1 + y2;
            if (denom < 0.0) period += 0.5 * (y0 - y2) / denom;
        }
        return kSampleRate / period;
    }
    return 0.0;
}

// --- state save/load plumbing -------------------------------------------------------------------
struct StateSink {
    std::string bytes;
    static int64_t write(const clap_ostream_t* stream, const void* buffer, uint64_t size) {
        static_cast<StateSink*>(stream->ctx)->bytes.append(static_cast<const char*>(buffer), size_t(size));
        return int64_t(size);
    }
};

struct StateSource {
    std::string bytes;
    size_t pos = 0;
    static int64_t read(const clap_istream_t* stream, void* buffer, uint64_t size) {
        auto* s = static_cast<StateSource*>(stream->ctx);
        const size_t remaining = s->bytes.size() - s->pos;
        const size_t n = std::min<size_t>(size_t(size), remaining);
        std::memcpy(buffer, s->bytes.data() + s->pos, n);
        s->pos += n;
        return int64_t(n);
    }
};

// sendParam()/readParam() are defined after render(), below.

// --- the library + plugin under test ------------------------------------------------------------
struct Library {
    void* handle = nullptr;
    const clap_plugin_entry_t* entry = nullptr;
    const clap_plugin_factory_t* factory = nullptr;
    const clap_plugin_descriptor_t* descriptor = nullptr;
    clap_host_t host{};
};

bool openLibrary(const std::string& path, Library& lib) {
    std::string binary = path;
    if (path.size() > 5 && path.compare(path.size() - 5, 5, ".clap") == 0) {
        const size_t slash = path.find_last_of('/');
        const std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
        binary = path + "/Contents/MacOS/" + name.substr(0, name.size() - 5);
    }
    lib.handle = dlopen(binary.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (lib.handle == nullptr) lib.handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (lib.handle == nullptr) return false;

    lib.entry = static_cast<const clap_plugin_entry_t*>(dlsym(lib.handle, "clap_entry"));
    if (lib.entry == nullptr || !lib.entry->init(path.c_str())) return false;

    lib.factory = static_cast<const clap_plugin_factory_t*>(
        lib.entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
    if (lib.factory == nullptr) return false;
    lib.descriptor = lib.factory->get_plugin_descriptor(lib.factory, 0);
    if (lib.descriptor == nullptr) return false;

    lib.host = clap_host_t{};
    lib.host.clap_version = CLAP_VERSION_INIT;
    lib.host.name = "mfm-clap-host-test";
    lib.host.vendor = "test";
    lib.host.url = "";
    lib.host.version = "1";
    lib.host.get_extension = hostGetExtension;
    lib.host.request_restart = hostNoop;
    lib.host.request_process = hostNoop;
    lib.host.request_callback = hostNoop;
    return true;
}

struct Instance {
    const clap_plugin_t* plugin = nullptr;

    bool create(Library& lib) {
        plugin = lib.factory->create_plugin(lib.factory, &lib.host, lib.descriptor->id);
        return plugin != nullptr && plugin->init(plugin)
            && plugin->activate(plugin, kSampleRate, 1, 4096);
    }
    void destroy() {
        if (plugin != nullptr) { plugin->deactivate(plugin); plugin->destroy(plugin); }
        plugin = nullptr;
    }
};

struct StereoAudio {
    std::vector<float> left, right;
};

// Collects what a plugin pushes to out_events: every NOTE_END with the absolute frame it arrived at,
// and whether any block's events went backwards in time (CLAP requires them in order).
struct OutputSink {
    struct End { clap_event_note_t event; uint64_t frame; };
    std::vector<End> ends;
    uint64_t blockStart = 0;
    uint32_t lastTime = 0;
    bool outOfOrder = false;
    static bool tryPush(const clap_output_events_t* list, const clap_event_header_t* header) {
        auto* sink = static_cast<OutputSink*>(list->ctx);
        if (header->time < sink->lastTime) sink->outOfOrder = true;
        sink->lastTime = header->time;
        if (header->space_id == CLAP_CORE_EVENT_SPACE_ID && header->type == CLAP_EVENT_NOTE_END)
            sink->ends.push_back({ *reinterpret_cast<const clap_event_note_t*>(header),
                                   sink->blockStart + header->time });
        return true;
    }
};

StereoAudio renderStereo(const clap_plugin_t* plugin, uint32_t frames,
                         const clap_event_header_t** events, uint32_t eventCount,
                         OutputSink* sink = nullptr) {
    StereoAudio audio;
    audio.left.assign(frames, 0.0f);
    audio.right.assign(frames, 0.0f);

    EventList holder;
    holder.events = events;
    holder.count = eventCount;
    clap_input_events_t in{};
    in.ctx = &holder;
    in.size = EventList::size;
    in.get = EventList::get;

    float* channels[2] = { audio.left.data(), audio.right.data() };
    clap_audio_buffer_t out{};
    out.data32 = channels;
    out.data64 = nullptr;
    out.channel_count = 2;
    out.latency = 0;
    out.constant_mask = 0;

    clap_process_t process{};
    process.steady_time = -1;
    process.frames_count = frames;
    process.transport = nullptr;
    process.audio_inputs = nullptr;
    process.audio_inputs_count = 0;
    process.audio_outputs = &out;
    process.audio_outputs_count = 1;
    process.in_events = &in;
    clap_output_events_t outEvents{};
    outEvents.ctx = sink;
    outEvents.try_push = OutputSink::tryPush;
    process.out_events = sink != nullptr ? &outEvents : nullptr;
    if (sink != nullptr) sink->lastTime = 0;

    plugin->process(plugin, &process);
    return audio;
}

std::vector<float> render(const clap_plugin_t* plugin, uint32_t frames,
                         const clap_event_header_t** events, uint32_t eventCount) {
    return renderStereo(plugin, frames, events, eventCount).left;
}

// Send one parameter value and render a tiny block so the plugin stores/applies it.
void sendParam(const clap_plugin_t* plugin, uint32_t index, double value) {
    clap_event_param_value_t event{};
    event.header.size = sizeof(event);
    event.header.time = 0;
    event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    event.header.type = CLAP_EVENT_PARAM_VALUE;
    event.param_id = index;
    event.value = value;
    const clap_event_header_t* headers[] = { &event.header };
    render(plugin, 64, headers, 1);
}

double readParam(const clap_plugin_t* plugin, uint32_t index) {
    const auto* params = static_cast<const clap_plugin_params_t*>(
        plugin->get_extension(plugin, CLAP_EXT_PARAMS));
    if (params == nullptr || params->get_value == nullptr) return -1.0;
    double value = 0.0;
    return params->get_value(plugin, index, &value) ? value : -1.0;
}

// Install a patch that is clean to measure pitch on: algorithm 1 with only Op1 (a carrier) sounding
// a plain sine at ratio 1, held at full sustain; no L/R detune, no feedback, the chorus dry and the
// filter wide open. The note's pitch (and the 7.9 attribute) is the only variable left.
void installTestPatch(const clap_plugin_t* plugin) {
    struct Holder { clap_event_param_value_t event{}; };
    std::vector<Holder> storage;

    auto add = [&storage](uint32_t index, double value) {
        Holder holder{};
        holder.event.header.size = sizeof(clap_event_param_value_t);
        holder.event.header.time = 0;
        holder.event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        holder.event.header.type = CLAP_EVENT_PARAM_VALUE;
        holder.event.param_id = index;   // the plugin's clap_id is the 0-based parameter index
        holder.event.value = value;
        storage.push_back(holder);
    };

    add(kParamVoiceMode, 1.0);       // poly
    add(kParamAlgorithm, 0.0);       // DX7 algorithm 1: Op1 is a carrier
    add(kParamFeedback, 0.0);
    add(kParamPitch, 0.0);
    add(kParamFine, 0.0);
    add(kParamDetune, 0.0);          // no L/R detune: one unambiguous fundamental
    for (uint32_t op = 0; op < kFmOperators; ++op)
        add(fmOpParam(op, kOpLevel), op == 0 ? 0.9 : 0.0);   // Op1 alone, unmodulated
    add(fmOpParam(0, kOpWave), 0.0);    // sine
    add(fmOpParam(0, kOpRatio), 1.0);
    add(fmOpParam(0, kOpFine), 0.0);
    add(fmOpParam(0, kOpSustain), 1.0); // still ringing when measured
    add(kParamLpfCutoff, 5.4);       // wide open
    add(kParamLpfResonance, 1.0);
    add(kParamLpfEnvAmount, 0.0);
    add(kParamLpfPressure, 0.0);
    add(kParamChorusMix, 0.0);       // dry: the chorus's delay modulation would wobble the pitch

    std::vector<const clap_event_header_t*> headers;
    headers.reserve(storage.size());
    for (Holder& holder : storage) headers.push_back(&holder.event.header);
    render(plugin, 256, headers.data(), uint32_t(headers.size()));
}

// A MIDI 2.0 channel-voice-2 NoteOn UMP with a 7.9-pitch attribute (attributeType 3).
clap_event_midi2_t makeUmpNoteOn(uint8_t note, uint16_t velocity, uint8_t attributeType,
                                 uint16_t attribute) {
    const uint32_t words[2] = {
        (0x4u << 28) | (0x9u << 20) | (uint32_t(note) << 8) | uint32_t(attributeType),
        (uint32_t(velocity) << 16) | uint32_t(attribute),
    };
    clap_event_midi2_t event{};
    event.header.size = sizeof(event);
    event.header.time = 0;
    event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    event.header.type = CLAP_EVENT_MIDI2;
    event.port_index = 0;
    std::memcpy(event.data, words, sizeof(words));
    return event;
}

clap_event_note_t makeClapNoteOn(int16_t key, double velocity) {
    clap_event_note_t event{};
    event.header.size = sizeof(event);
    event.header.time = 0;
    event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    event.header.type = CLAP_EVENT_NOTE_ON;
    event.port_index = 0;
    event.key = key;
    event.channel = 0;
    event.velocity = velocity;
    return event;
}

clap_event_midi_t makeMidi1(uint8_t status, uint8_t data1, uint8_t data2) {
    clap_event_midi_t event{};
    event.header.size = sizeof(event);
    event.header.time = 0;
    event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    event.header.type = CLAP_EVENT_MIDI;
    event.port_index = 0;
    event.data[0] = status; event.data[1] = data1; event.data[2] = data2;
    return event;
}

// Render events (a note, plus anything that shapes it) through a fresh instance with the test patch,
// and report the measured pitch. `setup` is sent as parameter values after the test patch.
double pitchOfEvents(Library& lib, const clap_event_header_t** events, uint32_t eventCount,
                     double expectedHz, const char* what,
                     std::initializer_list<std::pair<uint32_t, double>> setup = {}) {
    Instance inst;
    if (!inst.create(lib)) {
        check(false, std::string(what) + ": plugin instance created");
        return 0.0;
    }
    installTestPatch(inst.plugin);
    for (const auto& [index, value] : setup) sendParam(inst.plugin, index, value);

    const std::vector<float> audio = render(inst.plugin, uint32_t(kSampleRate * 1.5), events, eventCount);

    bool finite = true;
    float peak = 0.0f;
    for (float s : audio) {
        if (!std::isfinite(s)) finite = false;
        peak = std::max(peak, std::fabs(s));
    }
    const double pitch = estimatePitch(audio, expectedHz * 0.4, expectedHz * 2.4);
    std::printf("        %-34s expected ~%8.3f Hz | measured %8.3f Hz | peak %.4f\n",
                what, expectedHz, pitch, double(peak));

    check(finite, std::string(what) + ": output is finite");
    check(peak > 0.01f, std::string(what) + ": produces audio");
    inst.destroy();
    return pitch;
}

double pitchOfNote(Library& lib, const clap_event_header_t* noteEvent, double expectedHz,
                   const char* what) {
    const clap_event_header_t* events[] = { noteEvent };
    return pitchOfEvents(lib, events, 1, expectedHz, what);
}

// Op1 release (seconds) for tests that measure pitch after note-off: the tail outlasts the render.
constexpr double kLongRelease = 10.0;

clap_event_note_expression_t makeNoteExpression(clap_note_expression id, int32_t noteId, int16_t key,
                                                 double value) {
    clap_event_note_expression_t event{};
    event.header.size = sizeof(event);
    event.header.time = 0;
    event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    event.header.type = CLAP_EVENT_NOTE_EXPRESSION;
    event.expression_id = id;
    event.note_id = noteId;
    event.port_index = 0;
    event.channel = 0;
    event.key = key;
    event.value = value;
    return event;
}

// A MIDI 2.0 per-note pitch bend UMP (status 0x6). value is the 32-bit bend, 0x80000000 = centre.
clap_event_midi2_t makeUmpPerNoteBend(uint8_t note, uint32_t value) {
    clap_event_midi2_t event = makeUmpNoteOn(note, 0, 0, 0);
    event.data[0] = (0x4u << 28) | (0x6u << 20) | (uint32_t(note) << 8);
    event.data[1] = value;
    return event;
}

// A MIDI 2.0 registered controller (RPN) UMP: bank, index, 32-bit value.
clap_event_midi2_t makeUmpRegController(uint8_t bank, uint8_t index, uint32_t value) {
    clap_event_midi2_t event = makeUmpNoteOn(0, 0, 0, 0);
    event.data[0] = (0x4u << 28) | (0x2u << 20) | (uint32_t(bank) << 8) | index;
    event.data[1] = value;
    return event;
}

// The 32-bit bend value for `semitones` of a ±range bend. Clamped: a full-scale bend computes 2^32,
// which wraps to 0 (full down) in a uint32_t.
uint32_t bendValue(double semitones, double range) {
    const double v = 2147483648.0 + semitones / range * 2147483648.0;
    return uint32_t(std::clamp(std::llround(v), 0LL, 4294967295LL));
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s /path/to/ModicumFM.clap\n", argv[0]);
        return 2;
    }

    Library lib;
    check(openLibrary(argv[1], lib), "dlopen + clap_entry + factory + descriptor");
    if (lib.handle == nullptr) { std::printf("FAILURES: %d\n", gFailures); return 1; }

    std::printf("descriptor: id=%s name=%s vendor=%s\n",
                lib.descriptor->id, lib.descriptor->name, lib.descriptor->vendor);
    check(std::strcmp(lib.descriptor->id, "com.unlikelyware.modicumfm") == 0, "descriptor id");
    check(std::strcmp(lib.descriptor->name, "ModicumFM") == 0, "descriptor name");

    // --- audio ports -----------------------------------------------------------------------------
    {
        Instance inst;
        check(inst.create(lib), "plugin instance created");
        const auto* ports = static_cast<const clap_plugin_audio_ports_t*>(
            inst.plugin->get_extension(inst.plugin, CLAP_EXT_AUDIO_PORTS));
        check(ports != nullptr, "audio-ports extension present");
        if (ports != nullptr) {
            check(ports->count(inst.plugin, true) == 0, "no audio inputs");
            check(ports->count(inst.plugin, false) == 1, "one audio output");
            clap_audio_port_info_t info{};
            if (ports->get(inst.plugin, 0, false, &info))
                check(info.channel_count == 2, "output is stereo");
            else
                check(false, "audio port info readable");
        }
        inst.destroy();
    }

    // --- the note port must offer (and prefer) the MIDI 2.0 dialect ------------------------------
    // This is the negotiation that makes a MIDI-2.0-capable host send raw UMP notes at all; without
    // it CLAP_EVENT_MIDI2 never arrives and the 7.9-pitch path is dead code.
    {
        Instance inst;
        check(inst.create(lib), "plugin instance created (note ports)");
        const auto* notes = static_cast<const clap_plugin_note_ports_t*>(
            inst.plugin->get_extension(inst.plugin, CLAP_EXT_NOTE_PORTS));
        check(notes != nullptr, "note-ports extension present");
        if (notes != nullptr) {
            check(notes->count(inst.plugin, true) == 1, "one note input port");
            clap_note_port_info_t info{};
            if (notes->get(inst.plugin, 0, true, &info)) {
                check((info.supported_dialects & CLAP_NOTE_DIALECT_CLAP) != 0,
                      "advertises the CLAP note dialect");
                check((info.supported_dialects & CLAP_NOTE_DIALECT_MIDI) != 0,
                      "advertises the MIDI 1.0 dialect");
                check((info.supported_dialects & CLAP_NOTE_DIALECT_MIDI2) != 0,
                      "advertises the MIDI 2.0 dialect");
                check(info.preferred_dialect == CLAP_NOTE_DIALECT_MIDI2,
                      "prefers the MIDI 2.0 dialect");
            } else {
                check(false, "note port info readable");
            }
        }
        inst.destroy();
    }

    // --- the gui extension is wired (DPF is now the GUI layer only) ------------------------------
    {
        Instance inst;
        check(inst.create(lib), "plugin instance created (gui)");
        const auto* gui = static_cast<const clap_plugin_gui_t*>(
            inst.plugin->get_extension(inst.plugin, CLAP_EXT_GUI));
        check(gui != nullptr, "gui extension present");
        if (gui != nullptr) {
            const char* api = nullptr;
            bool floating = true;
            check(gui->get_preferred_api(inst.plugin, &api, &floating) && api != nullptr && !floating,
                  "gui prefers an embedded (non-floating) window API");
            check(gui->is_api_supported(inst.plugin, api, false), "preferred gui API is supported");
            if (gui->create(inst.plugin, api, false)) {
                uint32_t w = 0, h = 0;
                check(gui->get_size(inst.plugin, &w, &h) && w > 0 && h > 0,
                      "gui reports a default size");
                gui->destroy(inst.plugin);
            } else {
                check(false, "gui create() succeeds for the preferred API");
            }
        }
        check(inst.plugin->get_extension(inst.plugin, CLAP_EXT_TIMER_SUPPORT) != nullptr,
              "timer-support extension present (pumps the UI event loop)");
        inst.destroy();
    }

    // --- params extension ------------------------------------------------------------------------
    {
        Instance inst;
        check(inst.create(lib), "plugin instance created (params)");
        const auto* params = static_cast<const clap_plugin_params_t*>(
            inst.plugin->get_extension(inst.plugin, CLAP_EXT_PARAMS));
        check(params != nullptr, "params extension present");
        if (params != nullptr) {
            check(params->count(inst.plugin) == uint32_t(kParamCount), "parameter count matches");
            clap_param_info_t info{};
            if (params->get_info(inst.plugin, kParamVoiceMode, &info)) {
                check(info.id == kParamVoiceMode, "param id is the 0-based index (DPF-compatible)");
                check((info.flags & CLAP_PARAM_IS_ENUM) != 0, "Voices is flagged as an enum");
                char text[128] = {};
                if (params->value_to_text(inst.plugin, kParamVoiceMode, 0.0, text, sizeof(text)))
                    check(std::strcmp(text, "Mono") == 0, "enum value_to_text returns the label");
                if (params->value_to_text(inst.plugin, kParamVoiceMode, 3.0, text, sizeof(text)))
                    check(std::strcmp(text, "Poly 32") == 0, "Voices offers Poly 32");
                else
                    check(false, "value_to_text works");
            } else {
                check(false, "param info readable");
            }
            if (params->get_info(inst.plugin, fmOpParam(2, kOpWave), &info))
                check((info.flags & CLAP_PARAM_IS_ENUM) != 0, "Op3 Wave is flagged as an enum");
            if (params->get_info(inst.plugin, kParamMasterLevel, &info))
                check((info.flags & CLAP_PARAM_IS_ENUM) == 0, "Master is not an enum");
        }
        inst.destroy();
    }

    // --- state save/load, including the legacy DPF-blob fallback ---------------------------------
    // The raw entry point implements CLAP state itself, so this is real coverage: a round trip must
    // restore values, and the NUL-separated blob DPF's generated wrapper used to write must still
    // load (that is what keeps sessions saved before this switch working).
    {
        Instance inst;
        check(inst.create(lib), "plugin instance created (state)");
        const auto* state = static_cast<const clap_plugin_state_t*>(
            inst.plugin->get_extension(inst.plugin, CLAP_EXT_STATE));
        check(state != nullptr && state->save != nullptr && state->load != nullptr,
              "state extension present");

        if (state != nullptr) {
            sendParam(inst.plugin, kParamFiltDecay, 5.0);
            check(std::fabs(readParam(inst.plugin, kParamFiltDecay) - 5.0) < 1e-6,
                  "parameter set before saving");

            StateSink sink;
            clap_ostream_t out{};
            out.ctx = &sink;
            out.write = StateSink::write;
            check(state->save(inst.plugin, &out), "state save succeeds");
            check(!sink.bytes.empty(), "state save produced bytes");

            sendParam(inst.plugin, kParamFiltDecay, 1.0);   // clobber it
            check(std::fabs(readParam(inst.plugin, kParamFiltDecay) - 1.0) < 1e-6,
                  "parameter changed after saving");

            StateSource source;
            source.bytes = sink.bytes;
            clap_istream_t in{};
            in.ctx = &source;
            in.read = StateSource::read;
            check(state->load(inst.plugin, &in), "state load succeeds");
            check(std::fabs(readParam(inst.plugin, kParamFiltDecay) - 5.0) < 1e-6,
                  "state round trip restores the value");

            // DPF's old auto-state: NUL-separated "__dpf_parameters_begin__" pairs, then \xfe.
            std::string legacy = "__dpf_parameters_begin__";
            legacy.push_back('\0');
            legacy += "fdec";               legacy.push_back('\0');
            legacy += "3";                legacy.push_back('\0');
            legacy += "__dpf_parameters_end__";
            legacy.push_back('\0');
            legacy.push_back('\xfe');
            StateSource legacySource;
            legacySource.bytes = legacy;
            clap_istream_t legacyIn{};
            legacyIn.ctx = &legacySource;
            legacyIn.read = StateSource::read;
            check(state->load(inst.plugin, &legacyIn), "legacy DPF state blob loads");
            check(std::fabs(readParam(inst.plugin, kParamFiltDecay) - 3.0) < 1e-6,
                  "legacy DPF state blob restores the value");

            // Data that is neither format must be rejected, not silently defaulted.
            StateSource junk;
            junk.bytes = "not our state at all";
            clap_istream_t junkIn{};
            junkIn.ctx = &junk;
            junkIn.read = StateSource::read;
            check(!state->load(inst.plugin, &junkIn), "foreign state data is rejected");

            // state_load on an active plugin only stores the values (it runs on the main thread);
            // they must still reach the engine at the top of the next process() block.
            installTestPatch(inst.plugin);
            auto peakAfterLoading = [&](const char* text) {
                StateSource s;
                s.bytes = text;
                clap_istream_t sIn{};
                sIn.ctx = &s;
                sIn.read = StateSource::read;
                if (!state->load(inst.plugin, &sIn)) return -1.0f;
                clap_event_note_t note = makeClapNoteOn(60, 1.0);
                const clap_event_header_t* events[] = { &note.header };
                float peak = 0.0f;
                for (float v : render(inst.plugin, 4096, events, 1)) peak = std::max(peak, std::fabs(v));
                return peak;
            };
            check(peakAfterLoading("ModicumFM 1\nop1lvl 0\n") < 1e-4f,
                  "state loaded while active reaches the engine (silenced)");
            check(peakAfterLoading("ModicumFM 1\nop1lvl 0.9\n") > 0.01f,
                  "state loaded while active reaches the engine (audible again)");
        }
        inst.destroy();
    }

    // --- MIDI 2.0 7.9-pitch, end to end through the real plugin -----------------------------------
    std::printf("MIDI 2.0 7.9-pitch notes through the real plugin:\n");
    const double kC4Hz = 261.6255653;
    const uint16_t kNote = 60;
    const uint16_t attributeZero = uint16_t((kNote << 9) | 0);
    const uint16_t attributeQuarterTone = uint16_t((kNote << 9) | 256);   // 256/512 semitone

    const clap_event_midi2_t noteOnZero = makeUmpNoteOn(uint8_t(kNote), 0xFFFF, 0x3, attributeZero);
    const clap_event_midi2_t noteOnQuarter = makeUmpNoteOn(uint8_t(kNote), 0xFFFF, 0x3, attributeQuarterTone);

    const double hzZero = pitchOfNote(lib, &noteOnZero.header, kC4Hz, "7.9 fraction 0 (C4)");
    const double hzQuarter = pitchOfNote(lib, &noteOnQuarter.header,
                                         kC4Hz * std::pow(2.0, 0.5 / 12.0),
                                         "7.9 fraction 256 (+50c)");

    if (hzZero > 0.0 && hzQuarter > 0.0) {
        const double ratio = hzQuarter / hzZero;
        const double expectedRatio = std::pow(2.0, 0.5 / 12.0);   // 1.029302
        const double errorPercent = 100.0 * std::fabs(ratio - expectedRatio) / expectedRatio;
        std::printf("        ratio %.6f (expected %.6f, error %.3f%%)\n",
                    ratio, expectedRatio, errorPercent);
        check(errorPercent < 1.5,
              "the 7.9 fraction moves the measured pitch by half a semitone");
    } else {
        check(false, "both 7.9-pitch notes produced a measurable pitch");
    }

    // --- pitch bend through the real plugin -------------------------------------------------------
    // Proves the bend CV reaches the FM engine's pitch (through Combine 2) at full depth. Measured
    // against the unbent C4 above, so only the interval matters.
    std::printf("Pitch bend through the real plugin:\n");
    auto checkInterval = [&](double hz, double semitones, const char* what) {
        const double expected = std::pow(2.0, semitones / 12.0);
        check(hzZero > 0.0 && std::fabs(hz / hzZero - expected) / expected < 0.003, what);
    };
    const double up1 = kC4Hz * std::pow(2.0, 1.0 / 12.0);
    const double up2 = kC4Hz * std::pow(2.0, 2.0 / 12.0);
    {
        const clap_event_midi2_t noteOn = makeUmpNoteOn(uint8_t(kNote), 0xFFFF, 0, 0);
        // No registered controller 7: per-note bend uses the channel's range, ±2.
        const clap_event_midi2_t bend = makeUmpPerNoteBend(uint8_t(kNote), 0xC0000000u);  // half way up
        const clap_event_header_t* events[] = { &noteOn.header, &bend.header };
        checkInterval(pitchOfEvents(lib, events, 2, up1, "per-note bend +1"), 1.0,
                      "with no RC 7, a half-way per-note bend is +1 semitone");
        // The Bend Range param is the channel range, and so the per-note range too: ±4 → +2.
        checkInterval(pitchOfEvents(lib, events, 2, up2, "per-note bend, Bend Range 4 st",
                                    {{kParamBendRange, 8.0}}), 2.0,
                      "Bend Range sets the per-note range when RC 7 has not: half way at ±4 is +2");
    }
    {
        // Registered controller 7 can also widen the range: ±48, then +3.
        const clap_event_midi2_t range = makeUmpRegController(0, 7, 48u << 25);
        const clap_event_midi2_t noteOn = makeUmpNoteOn(uint8_t(kNote), 0xFFFF, 0, 0);
        const clap_event_midi2_t bend = makeUmpPerNoteBend(uint8_t(kNote), bendValue(3.0, 48.0));
        const clap_event_header_t* events[] = { &range.header, &noteOn.header, &bend.header };
        checkInterval(pitchOfEvents(lib, events, 3, kC4Hz * std::pow(2.0, 3.0 / 12.0),
                                    "per-note bend +3 at RC7 = 48"), 3.0,
                      "registered controller 7 = 48 widens the per-note range: +3 of ±48");
    }
    {
        // A bend sent for another key must not touch the sounding note.
        const clap_event_midi2_t noteOn = makeUmpNoteOn(uint8_t(kNote), 0xFFFF, 0, 0);
        const clap_event_midi2_t bend = makeUmpPerNoteBend(uint8_t(kNote + 1), 0xFFFFFFFFu);
        const clap_event_header_t* events[] = { &noteOn.header, &bend.header };
        checkInterval(pitchOfEvents(lib, events, 2, kC4Hz, "per-note bend, other key"), 0.0,
                      "a per-note bend for another key leaves C4 alone");
    }
    {
        // Notes match on note + channel + group: the same key's per-note bend on channel 2 is for a
        // different note.
        const clap_event_midi2_t noteOn = makeUmpNoteOn(uint8_t(kNote), 0xFFFF, 0, 0);
        clap_event_midi2_t bend = makeUmpPerNoteBend(uint8_t(kNote), 0xFFFFFFFFu);
        bend.data[0] |= 1u << 16;   // channel 2
        const clap_event_header_t* events[] = { &noteOn.header, &bend.header };
        checkInterval(pitchOfEvents(lib, events, 2, kC4Hz, "per-note bend, other channel"), 0.0,
                      "a per-note bend on another channel leaves C4 alone");
    }
    {
        // KeyboardEDO's setup: announce a ±2 per-note range (registered controller 7), then bend
        // half way up, which it means as +1 semitone.
        const clap_event_midi2_t range = makeUmpRegController(0, 7, 2u << 25);
        const clap_event_midi2_t noteOn = makeUmpNoteOn(uint8_t(kNote), 0xFFFF, 0, 0);
        const clap_event_midi2_t bend = makeUmpPerNoteBend(uint8_t(kNote), 0xC0000000u);  // half way up
        const clap_event_header_t* events[] = { &range.header, &noteOn.header, &bend.header };
        checkInterval(pitchOfEvents(lib, events, 3, up1, "per-note bend at RC7 = 2"), 1.0,
                      "registered controller 7 sets the per-note range: half way at ±2 is +1 semitone");
    }
    {
        const clap_event_midi_t bend = makeMidi1(0xE0, 0x00, 0x60);   // half way up: +1 of ±2
        const clap_event_midi_t noteOn = makeMidi1(0x90, uint8_t(kNote), 100);
        const clap_event_header_t* events[] = { &bend.header, &noteOn.header };
        checkInterval(pitchOfEvents(lib, events, 2, up1, "MIDI 1.0 channel bend +1"), 1.0,
                      "a MIDI 1.0 channel bend of +1 semitone moves C4 to C#4");
    }
    {
        // Mono legato: C4 is bent +1 per note, then D4 slides in while C4 is held. The per-note bend
        // belonged to C4, so D4 must sound unbent (it ramps there rather than stepping).
        const clap_event_midi2_t noteOn = makeUmpNoteOn(uint8_t(kNote), 0xFFFF, 0, 0);
        const clap_event_midi2_t bend = makeUmpPerNoteBend(uint8_t(kNote), 0xC0000000u);
        clap_event_midi2_t legato = makeUmpNoteOn(uint8_t(kNote + 2), 0xFFFF, 0, 0);
        legato.header.time = uint32_t(kSampleRate * 0.1);
        const clap_event_header_t* events[] = { &noteOn.header, &bend.header, &legato.header };
        checkInterval(pitchOfEvents(lib, events, 3, up2, "mono legato to D4 after a bend",
                                    {{kParamVoiceMode, 0.0}}), 2.0,
                      "a mono legato note starts with its own (centred) per-note bend");
    }

    // --- Pitch 7.25 (registered per-note controller #3) -------------------------------------------
    // An absolute pitch for the sounding note, exact at any size (unlike a bend, clamped to its
    // range); both bends then offset from it.
    std::printf("Pitch 7.25 through the real plugin:\n");
    {
        const clap_event_midi2_t noteOn = makeUmpNoteOn(uint8_t(kNote), 0xFFFF, 0, 0);
        clap_event_midi2_t tune = makeUmpNoteOn(uint8_t(kNote), 0, 0, 0);
        tune.data[0] = (0x4u << 28) | (0x0u << 20) | (uint32_t(kNote) << 8) | 3u;
        tune.data[1] = uint32_t((kNote + 7) * 33554432.0 + 0.5 * 33554432.0);   // G4 + 50 cents
        {
            const clap_event_header_t* events[] = { &noteOn.header, &tune.header };
            checkInterval(pitchOfEvents(lib, events, 2, kC4Hz * std::pow(2.0, 7.5 / 12.0),
                                        "Pitch 7.25 = 67.5"), 7.5,
                          "Pitch 7.25 retunes the sounding C4 to 67.5 (G4 + 50 cents)");
        }
        {
            const clap_event_midi2_t bend = makeUmpPerNoteBend(uint8_t(kNote), 0xC0000000u);  // +1
            const clap_event_header_t* events[] = { &noteOn.header, &tune.header, &bend.header };
            checkInterval(pitchOfEvents(lib, events, 3, kC4Hz * std::pow(2.0, 8.5 / 12.0),
                                        "Pitch 7.25 = 67.5, bend +1"), 8.5,
                          "a per-note bend offsets from the Pitch 7.25 pitch");
        }
        {
            // Not persistent: the next note-on on that key starts at its own pitch.
            clap_event_midi2_t noteOff = makeUmpNoteOn(uint8_t(kNote), 0, 0, 0);
            noteOff.data[0] = (0x4u << 28) | (0x8u << 20) | (uint32_t(kNote) << 8);
            clap_event_midi2_t again = noteOn;
            noteOff.header.time = 100;
            again.header.time = 200;
            const clap_event_header_t* events[] = { &noteOn.header, &tune.header,
                                                    &noteOff.header, &again.header };
            checkInterval(pitchOfEvents(lib, events, 4, kC4Hz, "Pitch 7.25, then a new note-on"), 0.0,
                          "Pitch 7.25 lasts only for the sounding note");
        }
    }

    // --- MPE switch through the real plugin ---------------------------------------------------------
    // The same member-channel bend with the switch off and on: the parameter must reach the engine, and
    // it must be a two-state Off/On enum.
    std::printf("MPE switch:\n");
    {
        Instance probe;
        if (probe.create(lib)) {
            const auto* params = static_cast<const clap_plugin_params_t*>(
                probe.plugin->get_extension(probe.plugin, CLAP_EXT_PARAMS));
            clap_param_info_t info{};
            char text[64] = {};
            check(params != nullptr && params->get_info(probe.plugin, kParamMpe, &info)
                      && (info.flags & CLAP_PARAM_IS_ENUM) != 0 && info.default_value == 0.0
                      && params->value_to_text(probe.plugin, kParamMpe, 1.0, text, sizeof(text))
                      && std::string(text) == "On",
                  "MPE is an Off/On enum parameter, off by default");
            probe.destroy();
        }
        // Semitones a list of MIDI 1.0 events (ending in a note-on) sounds above the unbent C4.
        auto semitonesOf = [&](bool mpe, std::vector<clap_event_midi_t> midi) {
            Instance inst;
            if (!inst.create(lib)) return 0.0;
            installTestPatch(inst.plugin);
            sendParam(inst.plugin, kParamMpe, mpe ? 1.0 : 0.0);
            sendParam(inst.plugin, fmOpParam(0, kOpRelease), kLongRelease);   // a long tail to measure
            std::vector<const clap_event_header_t*> events;
            for (const auto& e : midi) events.push_back(&e.header);
            const std::vector<float> audio = render(inst.plugin, uint32_t(kSampleRate * 1.5),
                                                    events.data(), uint32_t(events.size()));
            inst.destroy();
            return 12.0 * std::log2(estimatePitch(audio, 100.0, 2000.0) / hzZero);
        };
        const clap_event_midi_t memberBend = makeMidi1(0xE1, 0x00, 0x44);   // 8192 + 512, channel 2
        const clap_event_midi_t memberNote = makeMidi1(0x91, uint8_t(kNote), 100);
        const double off = semitonesOf(false, { memberBend, memberNote });
        const double on  = semitonesOf(true,  { memberBend, memberNote });
        std::printf("        member bend: %.3f st with MPE off, %.3f st with MPE on\n", off, on);
        check(std::fabs(off - 0.125) < 0.02 && std::fabs(on - 3.0) < 0.02,
              "MPE on reads a member-channel bend at ±48 (+3 st), off at ±2 (+0.125 st)");

        // The master channel (1) bends every member note, at its own ±2, on top of the member bend.
        const clap_event_midi_t masterBend = makeMidi1(0xE0, 0x00, 0x60);   // half way up: +1 of ±2
        const double both = semitonesOf(true, { masterBend, memberBend, memberNote });
        std::printf("        master +1 and member +3: %.3f st\n", both);
        check(std::fabs(both - 4.0) < 0.02, "MPE: the master channel's bend adds to a member note's");

        // RPN 0 sets a channel's bend range: ±12 on channel 1, then half way up is +6.
        const double rpn = semitonesOf(false, { makeMidi1(0xB0, 101, 0), makeMidi1(0xB0, 100, 0),
                                                makeMidi1(0xB0, 6, 12),  makeMidi1(0xB0, 38, 0),
                                                masterBend, makeMidi1(0x90, uint8_t(kNote), 100) });
        std::printf("        RPN 0 = 12, half way up: %.3f st\n", rpn);
        check(std::fabs(rpn - 6.0) < 0.02, "RPN 0 sets the channel bend range (±12: half way is +6)");

        // Released notes (MPE §3.3.3: control of a note ceases once Note Off has occurred). Op1's
        // release is lengthened (kLongRelease), so the pitch is measured in the ringing tail.
        // Note-off at 0.05 s, then the bend at 0.1 s.
        auto at = [](clap_event_midi_t e, double seconds) {
            e.header.time = uint32_t(seconds * kSampleRate);
            return e;
        };
        const clap_event_midi_t memberOff = at(makeMidi1(0x81, uint8_t(kNote), 0), 0.05);
        const double frozen = semitonesOf(true, { memberNote, memberOff,
                                                  at(makeMidi1(0xE1, 0x7F, 0x7F), 0.1) });   // full up
        std::printf("        MPE member bend after note-off: %.3f st\n", frozen);
        check(std::fabs(frozen) < 0.02, "MPE: a member-channel bend after note-off does not move the released note");
        const double master = semitonesOf(true, { memberNote, memberOff, at(masterBend, 0.1) });
        std::printf("        MPE master bend after note-off: %.3f st\n", master);
        check(std::fabs(master - 1.0) < 0.02, "MPE: the master channel still bends a released note");
        const double wheel = semitonesOf(false, { makeMidi1(0x90, uint8_t(kNote), 100),
                                                  at(makeMidi1(0x80, uint8_t(kNote), 0), 0.05),
                                                  at(masterBend, 0.1) });
        std::printf("        MPE off, channel bend after note-off: %.3f st\n", wheel);
        check(std::fabs(wheel - 1.0) < 0.02, "MPE off: a pitch wheel still bends a releasing note");
    }
    {
        // A released note ignores a MIDI 2.0 per-note bend addressed by key; a CLAP note named by its
        // id keeps taking expression until NOTE_END.
        const clap_event_midi2_t noteOn = makeUmpNoteOn(uint8_t(kNote), 0xFFFF, 0, 0);
        clap_event_midi2_t noteOff = makeUmpNoteOn(uint8_t(kNote), 0, 0, 0);
        noteOff.data[0] = (0x4u << 28) | (0x8u << 20) | (uint32_t(kNote) << 8);
        noteOff.header.time = uint32_t(0.05 * kSampleRate);
        clap_event_midi2_t bend = makeUmpPerNoteBend(uint8_t(kNote), 0xC0000000u);   // +1
        bend.header.time = uint32_t(0.1 * kSampleRate);
        const clap_event_header_t* events[] = { &noteOn.header, &noteOff.header, &bend.header };
        checkInterval(pitchOfEvents(lib, events, 3, kC4Hz, "per-note bend after note-off",
                                    {{fmOpParam(0, kOpRelease), kLongRelease}}), 0.0,
                      "a released note ignores a per-note bend addressed by key");

        clap_event_note_t clapOn = makeClapNoteOn(int16_t(kNote), 1.0);
        clapOn.note_id = 5;
        clap_event_note_t clapOff = clapOn;
        clapOff.header.type = CLAP_EVENT_NOTE_OFF;
        clapOff.header.time = uint32_t(0.05 * kSampleRate);
        clap_event_note_expression_t tuning =
            makeNoteExpression(CLAP_NOTE_EXPRESSION_TUNING, 5, int16_t(kNote), 2.0);
        tuning.header.time = uint32_t(0.1 * kSampleRate);
        const clap_event_header_t* clapEvents[] = { &clapOn.header, &clapOff.header, &tuning.header };
        checkInterval(pitchOfEvents(lib, clapEvents, 3, kC4Hz * std::pow(2.0, 2.0 / 12.0),
                                    "CLAP tuning after note-off", {{fmOpParam(0, kOpRelease), kLongRelease}}), 2.0,
                      "a released CLAP note still takes a tuning expression by id");
    }

    // --- note end + voice-info ---------------------------------------------------------------------
    // A note rings on through the carriers' release after note-off; CLAP_EVENT_NOTE_END must come
    // when the voice falls silent, not at the note-off, and only for notes that arrived as CLAP note
    // events.
    std::printf("Note end and voice info:\n");
    {
        Instance inst;
        check(inst.create(lib), "fresh instance");
        installTestPatch(inst.plugin);
        sendParam(inst.plugin, fmOpParam(0, kOpRelease), 0.3);   // Op1 (the carrier): 0.3 s release

        clap_event_note_t noteOn = makeClapNoteOn(int16_t(kNote), 1.0);
        noteOn.note_id = 42;
        clap_event_note_t noteOff = noteOn;
        noteOff.header.type = CLAP_EVENT_NOTE_OFF;
        noteOff.header.time = 64;
        const clap_event_midi_t midiOn = makeMidi1(0x90, 72, 100);
        const clap_event_midi_t midiOff = [] { auto e = makeMidi1(0x80, 72, 0); e.header.time = 64; return e; }();
        const clap_event_header_t* first[] = { &noteOn.header, &midiOn.header, &noteOff.header, &midiOff.header };

        OutputSink sink;
        const uint32_t block = 512;
        for (uint32_t b = 0; b < uint32_t(4.0 * kSampleRate) / block; ++b) {
            sink.blockStart = uint64_t(b) * block;
            if (b == 0) renderStereo(inst.plugin, block, first, 4, &sink);
            else        renderStereo(inst.plugin, block, nullptr, 0, &sink);
        }
        check(sink.ends.size() == 1, "exactly one NOTE_END, for the CLAP note only (got "
                                         + std::to_string(sink.ends.size()) + ")");
        if (!sink.ends.empty()) {
            const auto& end = sink.ends[0];
            std::printf("        NOTE_END at %.3f s (note-off at %.4f s)\n",
                        double(end.frame) / kSampleRate, 64.0 / kSampleRate);
            check(end.event.note_id == 42 && end.event.key == int16_t(kNote) && end.event.channel == 0
                      && end.event.port_index == 0,
                  "NOTE_END echoes the note-on's id, key, channel and port");
            check(double(end.frame) / kSampleRate > 0.1, "NOTE_END waits for the release, not the note-off");
        }
        check(!sink.outOfOrder, "output events stay in time order within each block");

        const auto* voiceInfo = static_cast<const clap_plugin_voice_info_t*>(
            inst.plugin->get_extension(inst.plugin, CLAP_EXT_VOICE_INFO));
        check(voiceInfo != nullptr, "voice-info extension is offered");
        if (voiceInfo != nullptr) {
            clap_voice_info_t info{};
            check(voiceInfo->get(inst.plugin, &info) && info.voice_count == 10 && info.voice_capacity == 32
                      && (info.flags & CLAP_VOICE_INFO_SUPPORTS_OVERLAPPING_NOTES) != 0,
                  "poly: 10 voices by default, overlapping notes supported");
            sendParam(inst.plugin, kParamVoiceMode, 2.0);
            check(voiceInfo->get(inst.plugin, &info) && info.voice_count == 20 && info.voice_capacity == 32,
                  "Poly 20: 20 voices");
            sendParam(inst.plugin, kParamVoiceMode, 3.0);
            check(voiceInfo->get(inst.plugin, &info) && info.voice_count == 32 && info.voice_capacity == 32,
                  "Poly 32: 32 voices");
            sendParam(inst.plugin, kParamVoiceMode, 0.0);
            check(voiceInfo->get(inst.plugin, &info) && info.voice_count == 1 && info.voice_capacity == 32,
                  "mono: 1 voice");
        }
        inst.destroy();
    }

    // --- Voices: Poly 10 / 20 / 32 -------------------------------------------------------------------
    // 32 held CLAP notes: Poly 10 steals 22 of them (each steal ends its note), Poly 20 steals 12,
    // Poly 32 holds them all.
    {
        const auto stolen = [&](double voices) {
            Instance inst;
            if (!inst.create(lib)) return -1;
            installTestPatch(inst.plugin);
            sendParam(inst.plugin, kParamVoiceMode, voices);
            std::vector<clap_event_note_t> ons;
            for (int i = 0; i < 32; ++i) {
                clap_event_note_t on = makeClapNoteOn(int16_t(40 + i), 1.0);
                on.note_id = 100 + i;
                ons.push_back(on);
            }
            std::vector<const clap_event_header_t*> list;
            for (const auto& on : ons) list.push_back(&on.header);
            OutputSink sink;
            const uint32_t block = 512;
            for (uint32_t b = 0; b < uint32_t(0.5 * kSampleRate) / block; ++b) {
                sink.blockStart = uint64_t(b) * block;
                if (b == 0) renderStereo(inst.plugin, block, list.data(), uint32_t(list.size()), &sink);
                else        renderStereo(inst.plugin, block, nullptr, 0, &sink);
            }
            inst.destroy();
            return int(sink.ends.size());
        };
        const int poly10 = stolen(1.0), poly20 = stolen(2.0), poly32 = stolen(3.0);
        check(poly10 == 22, "Poly 10: 32 held notes steal 22 voices (got " + std::to_string(poly10) + ")");
        check(poly20 == 12, "Poly 20: 32 held notes steal 12 voices (got " + std::to_string(poly20) + ")");
        check(poly32 == 0,  "Poly 32: 32 held notes all keep a voice (got " + std::to_string(poly32) + ")");
    }

    // --- CLAP note expressions + note ids, as Bitwig sends them ---------------------------------------
    // Tuning arrives as Pitch 7.25 (an absolute pitch), so it is exact far beyond the ±2 bend range.
    std::printf("CLAP note expressions (Bitwig):\n");
    {
        clap_event_note_t noteOn = makeClapNoteOn(int16_t(kNote), 1.0);
        noteOn.note_id = 7;
        const clap_event_note_expression_t tuning =
            makeNoteExpression(CLAP_NOTE_EXPRESSION_TUNING, 7, int16_t(kNote), 24.0);
        const clap_event_header_t* events[] = { &noteOn.header, &tuning.header };
        checkInterval(pitchOfEvents(lib, events, 2, kC4Hz * 4.0, "tuning +24 (key + id)"), 24.0,
                      "a +24 semitone tuning expression sounds two octaves up");
    }
    {
        clap_event_note_t noteOn = makeClapNoteOn(int16_t(kNote), 1.0);
        noteOn.note_id = 11;
        const clap_event_note_expression_t tuning =
            makeNoteExpression(CLAP_NOTE_EXPRESSION_TUNING, 11, -1, 7.0);   // id only, no key
        const clap_event_header_t* events[] = { &noteOn.header, &tuning.header };
        checkInterval(pitchOfEvents(lib, events, 2, kC4Hz * std::pow(2.0, 7.0 / 12.0),
                                    "tuning +7 (id only)"), 7.0,
                      "an expression naming only the note id reaches that note (+7 semitones)");
    }
    {
        // The same key with another note id is another note: the expression must not reach it.
        clap_event_note_t noteOn = makeClapNoteOn(int16_t(kNote), 1.0);
        noteOn.note_id = 12;
        const clap_event_note_expression_t tuning =
            makeNoteExpression(CLAP_NOTE_EXPRESSION_TUNING, 13, int16_t(kNote), 7.0);
        const clap_event_header_t* events[] = { &noteOn.header, &tuning.header };
        checkInterval(pitchOfEvents(lib, events, 2, kC4Hz, "tuning for another note id"), 0.0,
                      "a tuning expression for another note id leaves C4 alone");
    }

    // --- a MIDI 2.0 note-on with velocity 0 is a note-on (M2-104 4.2.8) ------------------------------
    // It used to count as a note-off, so the note never sounded. Vel Floor "Fixed" (4) ignores
    // velocity, so the note is audible; at the default 0% floor velocity 0 plays at zero level.
    std::printf("MIDI 2.0 velocity 0:\n");
    {
        const clap_event_midi2_t noteOn = makeUmpNoteOn(uint8_t(kNote), 0, 0, 0);
        const clap_event_header_t* events[] = { &noteOn.header };
        checkInterval(pitchOfEvents(lib, events, 1, kC4Hz, "MIDI 2.0 note-on, velocity 0",
                                    {{kParamVelFloor, 4.0}}), 0.0,
                      "a MIDI 2.0 note-on with velocity 0 plays its note");
    }

    // --- mono glide ------------------------------------------------------------------------------
    // Glide is a constant rate in seconds per octave on the note pitch only (PitchConverter -> Slew
    // -> Combine 2); the bend joins after it. Its minimum is off, and poly never glides.
    std::printf("Mono glide:\n");
    {
        constexpr double kA3 = 220.0, kA4 = 440.0;
        const auto noteOn  = [](uint8_t note) { return makeMidi1(0x90, note, 100); };
        const auto seconds = [](double s) { return uint32_t(s * kSampleRate); };
        // Render `events` then `skip` seconds, and measure the pitch over the next `window` seconds.
        const auto measure = [&](const clap_plugin_t* plugin, std::initializer_list<const clap_event_header_t*> events,
                                 double skip, double window, double expectedHz, const char* what) {
            std::vector<const clap_event_header_t*> list(events);
            if (skip > 0.0) { render(plugin, seconds(skip), list.data(), uint32_t(list.size())); list.clear(); }
            const std::vector<float> audio = render(plugin, seconds(window), list.data(), uint32_t(list.size()));
            const double hz = estimatePitch(audio, expectedHz * 0.5, expectedHz * 2.0);
            std::printf("        %-34s expected ~%8.3f Hz | measured %8.3f Hz\n", what, expectedHz, hz);
            return hz;
        };
        const auto near = [](double hz, double expected, double tol) { return std::fabs(hz / expected - 1.0) < tol; };
        const auto glideInstance = [&](Instance& inst, double voices, double glide) {
            if (!inst.create(lib)) return false;
            installTestPatch(inst.plugin);
            sendParam(inst.plugin, kParamVoiceMode, voices);
            sendParam(inst.plugin, kParamGlide, glide);
            return true;
        };

        {   // 1 s/oct, A3 then A4 held legato: halfway (0.5 s) is half an octave up, and it arrives
            // after a second. The first note snaps to its pitch rather than sliding up from 0.
            Instance inst;
            check(glideInstance(inst, 0.0, 1.0), "glide: plugin instance created");
            const clap_event_midi_t a3 = noteOn(57), a4 = noteOn(69);
            const double first = measure(inst.plugin, {&a3.header}, 0.0, 0.25, kA3, "first note (A3), no slide in");
            check(near(first, kA3, 0.01), "glide: the first note starts at its own pitch");
            // The window is analysed from 30% in, so 0.45 + 0.3·0.1 .. 0.55 s: centred near 0.51 oct.
            const double mid = measure(inst.plugin, {&a4.header}, 0.45, 0.1, kA3 * std::exp2(0.51), "A3 -> A4 at 0.5 s");
            check(near(mid, kA3 * std::exp2(0.51), 0.03), "glide: halfway through 1 s/oct is half an octave up");
            const double end = measure(inst.plugin, {}, 0.6, 0.25, kA4, "A3 -> A4 after 1.3 s");
            check(near(end, kA4, 0.01), "glide: arrives at the new note");
            inst.destroy();
        }
        {   // A bend during a glide is not smeared by it: full up (+2 st at the default range) on the
            // held note sounds at once, while a note change would take a second per octave.
            Instance inst;
            check(glideInstance(inst, 0.0, 1.0), "glide bend: plugin instance created");
            const clap_event_midi_t a3 = noteOn(57), bend = makeMidi1(0xE0, 0x7F, 0x7F);
            measure(inst.plugin, {&a3.header}, 0.0, 0.25, kA3, "A3 before the bend");
            const double bent = measure(inst.plugin, {&bend.header}, 0.0, 0.1, kA3 * std::exp2(2.0 / 12.0),
                                        "A3 bent +2 st, first 0.1 s");
            check(near(bent, kA3 * std::exp2(2.0 / 12.0), 0.01), "glide: a bend bypasses the glide");
            inst.destroy();
        }
        {   // Glide at its minimum is off: A3 then A4 lands on A4 at once.
            Instance inst;
            check(glideInstance(inst, 0.0, kParams[kParamGlide].min), "glide off: plugin instance created");
            const clap_event_midi_t a3 = noteOn(57), a4 = noteOn(69);
            measure(inst.plugin, {&a3.header}, 0.0, 0.25, kA3, "A3, glide off");
            const double hz = measure(inst.plugin, {&a4.header}, 0.0, 0.1, kA4, "A4 at once, glide off");
            check(near(hz, kA4, 0.01), "glide: the minimum is off");
            inst.destroy();
        }
        {   // Poly never glides, even with Glide up: the voice that played A3 (released, still ringing
            // under the long release) takes A4 without sliding.
            Instance inst;
            check(glideInstance(inst, 1.0, 1.0), "glide poly: plugin instance created");
            const clap_event_midi_t a3 = noteOn(57), a4 = noteOn(69);
            measure(inst.plugin, {&a3.header}, 0.0, 0.25, kA3, "A3, poly");
            const clap_event_midi_t off = makeMidi1(0x80, 57, 0);
            const std::vector<const clap_event_header_t*> offList = {&off.header};
            render(inst.plugin, seconds(1.0), const_cast<const clap_event_header_t**>(offList.data()), 1);
            const double hz = measure(inst.plugin, {&a4.header}, 0.0, 0.1, kA4, "A4 at once, poly");
            check(near(hz, kA4, 0.01), "glide: poly does not glide");
            inst.destroy();
        }
    }

    // --- glide mode and vibrato (the Pitch panel) -------------------------------------------------
    std::printf("Glide mode and vibrato:\n");
    {
        constexpr double kA3 = 220.0, kA4 = 440.0;
        const auto seconds = [](double s) { return uint32_t(s * kSampleRate); };
        const auto near = [](double hz, double expected, double tol) { return std::fabs(hz / expected - 1.0) < tol; };
        const auto make = [&](Instance& inst, std::initializer_list<std::pair<uint32_t, double>> setup) {
            if (!inst.create(lib)) return false;
            installTestPatch(inst.plugin);
            for (const auto& [index, value] : setup) sendParam(inst.plugin, index, value);
            return true;
        };
        // Render `events` + `secs`, then the pitch over the last `window` seconds of it.
        const auto pitchAfter = [&](const clap_plugin_t* plugin, std::vector<const clap_event_header_t*> events,
                                    double secs, double window, double expectedHz) {
            if (secs > window) { render(plugin, seconds(secs - window), events.data(), uint32_t(events.size())); events.clear(); }
            const std::vector<float> audio = render(plugin, seconds(window), events.data(), uint32_t(events.size()));
            return estimatePitch(audio, expectedHz * 0.5, expectedHz * 2.0);
        };
        // Pitch track: 30 ms windows every 10 ms over `secs`; min, max and how often it crosses its mean.
        struct Track { double lo = 1e9, hi = 0.0; int crossings = 0; };
        const auto track = [&](const clap_plugin_t* plugin, double secs) {
            const std::vector<float> audio = render(plugin, seconds(secs), nullptr, 0);
            std::vector<double> hz;
            const size_t win = seconds(0.03), hop = seconds(0.01);
            for (size_t at = 0; at + win <= audio.size(); at += hop)
                hz.push_back(estimatePitch(std::vector<float>(audio.begin() + long(at), audio.begin() + long(at + win)),
                                           kA3 * 0.7, kA3 * 1.4));
            Track t;
            double mean = 0.0;
            for (double h : hz) { t.lo = std::min(t.lo, h); t.hi = std::max(t.hi, h); mean += h; }
            mean /= double(hz.size());
            for (size_t i = 1; i < hz.size(); ++i)
                if ((hz[i - 1] < mean) != (hz[i] < mean)) ++t.crossings;
            return t;
        };
        const clap_event_midi_t a3 = makeMidi1(0x90, 57, 100), a4 = makeMidi1(0x90, 69, 100);
        const clap_event_midi_t a3off = makeMidi1(0x80, 57, 0);

        {   // Always: a note after the last one was released still glides.
            Instance inst;
            check(make(inst, {{kParamVoiceMode, 0.0}, {kParamGlide, 1.0}, {kParamGlideMode, 0.0}}), "glide mode: instance");
            pitchAfter(inst.plugin, {&a3.header}, 0.3, 0.2, kA3);
            pitchAfter(inst.plugin, {&a3off.header}, 0.1, 0.05, kA3);
            const double hz = pitchAfter(inst.plugin, {&a4.header}, 0.15, 0.1, kA4);
            std::printf("        Always, A3 released then A4: %.3f Hz at ~0.12 s (A4 is %.0f)\n", hz, kA4);
            check(hz > kA3 * 1.02 && hz < kA4 * 0.9, "glide mode Always: glides from a released note");
            inst.destroy();
        }
        {   // Legato: a note after the last one was released jumps; one played over a held note glides.
            Instance inst;
            check(make(inst, {{kParamVoiceMode, 0.0}, {kParamGlide, 1.0}, {kParamGlideMode, 1.0}}), "glide mode: instance");
            pitchAfter(inst.plugin, {&a3.header}, 0.3, 0.2, kA3);
            pitchAfter(inst.plugin, {&a3off.header}, 0.1, 0.05, kA3);
            const double jumped = pitchAfter(inst.plugin, {&a4.header}, 0.15, 0.1, kA4);
            std::printf("        Legato, A3 released then A4: %.3f Hz\n", jumped);
            check(near(jumped, kA4, 0.01), "glide mode Legato: a detached note does not glide");
            const double slid = pitchAfter(inst.plugin, {&a3.header}, 0.15, 0.1, kA3);   // over held A4
            std::printf("        Legato, A3 over held A4: %.3f Hz at ~0.12 s\n", slid);
            check(slid < kA4 * 0.98 && slid > kA3 * 1.1, "glide mode Legato: a note over a held one glides");
            inst.destroy();
        }
        {   // Vibrato: Depth 0 and the wheel at 0 leave the pitch steady.
            Instance inst;
            check(make(inst, {{kParamVibRate, 5.0}}), "vibrato: instance");
            render(inst.plugin, seconds(0.2), std::vector<const clap_event_header_t*>{&a3.header}.data(), 1);
            const Track t = track(inst.plugin, 1.0);
            std::printf("        no vibrato: %.2f .. %.2f Hz\n", t.lo, t.hi);
            check(near(t.lo, kA3, 0.003) && near(t.hi, kA3, 0.003), "vibrato: off by default");
            inst.destroy();
        }
        {   // Depth 100 ct at 5 Hz: the pitch swings about a semitone each way, ~10 mean crossings a second.
            Instance inst;
            check(make(inst, {{kParamVibRate, 5.0}, {kParamVibDepth, 100.0}}), "vibrato depth: instance");
            render(inst.plugin, seconds(0.2), std::vector<const clap_event_header_t*>{&a3.header}.data(), 1);
            const Track t = track(inst.plugin, 1.0);
            std::printf("        Depth 100 ct, 5 Hz: %.2f .. %.2f Hz, %d crossings in 1 s\n", t.lo, t.hi, t.crossings);
            check(t.hi > kA3 * std::exp2(0.8 / 12.0) && t.hi < kA3 * std::exp2(1.1 / 12.0), "vibrato: depth up ~1 st");
            check(t.lo < kA3 * std::exp2(-0.8 / 12.0) && t.lo > kA3 * std::exp2(-1.1 / 12.0), "vibrato: depth down ~1 st");
            check(t.crossings >= 8 && t.crossings <= 12, "vibrato: rate 5 Hz");   // 2.5 / 10 Hz give ~5 / ~20
            inst.destroy();
        }
        {   // Wheel 100 ct: nothing with the wheel down, a semitone each way with it full up (CC 1 = 127).
            Instance inst;
            check(make(inst, {{kParamVibRate, 5.0}, {kParamVibWheel, 100.0}}), "vibrato wheel: instance");
            render(inst.plugin, seconds(0.2), std::vector<const clap_event_header_t*>{&a3.header}.data(), 1);
            const Track down = track(inst.plugin, 0.5);
            const clap_event_midi_t wheel = makeMidi1(0xB0, 1, 127);
            render(inst.plugin, seconds(0.1), std::vector<const clap_event_header_t*>{&wheel.header}.data(), 1);
            const Track up = track(inst.plugin, 1.0);
            std::printf("        Wheel 100 ct: down %.2f .. %.2f Hz, up %.2f .. %.2f Hz\n", down.lo, down.hi, up.lo, up.hi);
            check(near(down.lo, kA3, 0.003) && near(down.hi, kA3, 0.003), "vibrato: wheel down, none");
            check(up.hi > kA3 * std::exp2(0.8 / 12.0) && up.lo < kA3 * std::exp2(-0.8 / 12.0), "vibrato: wheel up, ~1 st");
            inst.destroy();
        }
        {   // Mono (one voice, legato engine settings): Depth 50 ct swings half a semitone each way.
            Instance inst;
            check(make(inst, {{kParamVoiceMode, 0.0}, {kParamGlide, kParams[kParamGlide].min},
                              {kParamVibRate, 5.0}, {kParamVibDepth, 50.0}}), "vibrato mono: instance");
            render(inst.plugin, seconds(0.2), std::vector<const clap_event_header_t*>{&a3.header}.data(), 1);
            const Track t = track(inst.plugin, 1.0);
            std::printf("        mono, Depth 50 ct: %.2f .. %.2f Hz\n", t.lo, t.hi);
            check(t.hi > kA3 * std::exp2(0.35 / 12.0) && t.hi < kA3 * std::exp2(0.6 / 12.0), "vibrato: works in mono");
            inst.destroy();
        }
    }

    // --- vibrato Sync: Shared vs Per Note ----------------------------------------------------------
    // Mono, Depth 100 ct at 5 Hz. A3 at 0 s, released at 0.1 s (still ringing, so the shared LFO keeps
    // running), A3 again at 0.13 s. Shared: the phase runs on (0.65 cycle, the sine's low half), so
    // the new note starts flat. Per Note: it restarts at 0 and rises, so the new note starts sharp.
    // A note played over a held one (fingered legato) does not restart it.
    std::printf("Vibrato sync:\n");
    {
        constexpr double kA3 = 220.0, kA4 = 440.0;
        const auto seconds = [](double s) { return uint32_t(s * kSampleRate); };
        const clap_event_midi_t a3 = makeMidi1(0x90, 57, 100), a3off = makeMidi1(0x80, 57, 0);
        const clap_event_midi_t a4 = makeMidi1(0x90, 69, 100);
        const auto startOfNote = [&](double sync, bool legato, double expectedHz) {
            Instance inst;
            if (!inst.create(lib)) { check(false, "vibrato sync: instance"); return 0.0; }
            installTestPatch(inst.plugin);
            for (const auto& [index, value] : std::initializer_list<std::pair<uint32_t, double>>{
                     {kParamVoiceMode, 0.0}, {kParamVibRate, 5.0}, {kParamVibDepth, 100.0}, {kParamVibSync, sync}})
                sendParam(inst.plugin, index, value);
            const clap_event_header_t* on[] = { &a3.header };
            const clap_event_header_t* off[] = { &a3off.header };
            const clap_event_header_t* again[] = { legato ? &a4.header : &a3.header };
            render(inst.plugin, seconds(0.1), on, 1);
            render(inst.plugin, seconds(0.03), legato ? nullptr : off, legato ? 0 : 1);
            // A 40 ms window, analysed from 12 ms in: phase 0.06..0.2 after a restart.
            const std::vector<float> audio = render(inst.plugin, seconds(0.04), again, 1);
            inst.destroy();
            return estimatePitch(audio, expectedHz * 0.7, expectedHz * 1.4);
        };
        const double shared  = startOfNote(0.0, false, kA3);
        const double perNote = startOfNote(1.0, false, kA3);
        const double legato  = startOfNote(1.0, true,  kA4);
        std::printf("        new A3 at 0.13 s: Shared %.2f Hz, Per Note %.2f Hz; Per Note legato A4 %.2f Hz\n",
                    shared, perNote, legato);
        check(shared < kA3 * std::exp2(-0.5 / 12.0), "vibrato sync Shared: the phase runs on (starts flat)");
        check(perNote > kA3 * std::exp2(0.5 / 12.0), "vibrato sync Per Note: restarts at note-on (starts sharp)");
        check(legato < kA4 * std::exp2(-0.5 / 12.0), "vibrato sync Per Note: a legato note does not restart it");
    }

    // --- the MIDI-1.0 / CLAP dialects still work -------------------------------------------------
    std::printf("Other note dialects (must keep working):\n");
    {
        const clap_event_note_t clapNote = makeClapNoteOn(int16_t(kNote), 1.0);
        const double hz = pitchOfNote(lib, &clapNote.header, kC4Hz, "CLAP note-on (C4)");
        check(hz > 0.0, "CLAP note dialect reaches the engine");
    }
    {
        const clap_event_midi_t midi1 = makeMidi1(0x90, uint8_t(kNote), 100);
        const double hz = pitchOfNote(lib, &midi1.header, kC4Hz, "MIDI 1.0 note-on (C4)");
        check(hz > 0.0, "MIDI 1.0 dialect reaches the engine");
    }

    if (lib.handle != nullptr) {
        lib.entry->deinit();
        dlclose(lib.handle);
    }

    if (gFailures == 0) {
        std::printf("ALL PASS\n");
        return 0;
    }
    std::printf("FAILURES: %d\n", gFailures);
    return 1;
}
