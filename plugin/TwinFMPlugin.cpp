// TwinFMPlugin.cpp — DSP side: DPF Plugin wrapper around the vendored PolygraphModular
// voice engine. DPF hands us MIDI 1.0 events + a parameter interface and generates the CLAP
// (VST3/LV2) wrapper; all the synthesis is the VoiceManager the self-tests exercise.
//
//   MIDI events  → pm::UmpMessage → VoiceManager (polyphony / stealing / mono); pitch bend and
//                  channel pressure reach every voice's converter modules
//   params       → VoiceManager::setParameter() (propagates to every voice, live)
//   run()        → sample-accurate: splits at each MIDI event, renders in short control chunks
//
// The voice is wired in VoiceManager::buildDefaultPatch:
//   note + bend → FM4Op A, FM4Op B → mixer → LPF (filter ADSR + pressure) → VCA (amp ADSR)
//   → chorus (shared LFO) → Final (master level).

#include "DistrhoPlugin.hpp"

#include <algorithm>
#include <memory>

#include "TwinFMParams.h"

#include "ParamAddressMacros.h"
#include "VoiceManager.hpp"
#include "pm_addresses.h"
#include "pm_midi.h"
#include "pm_params.h"

START_NAMESPACE_DISTRHO

using namespace twinfm;

// Engine chunk length inside run(). Modules read their mod inputs (filter envelope, pressure,
// LFO) once per chunk, so this is the control rate: 64 samples ≈ 1.3 ms at 48 kHz keeps a snappy
// filter envelope smooth, where a full MODULE_BUFF_SIZE (512) chunk would step every ~11 ms.
static constexpr uint32_t kControlChunk = 64;

class TwinFMPlugin : public Plugin
{
public:
    TwinFMPlugin()
        : Plugin(kParamCount, 0, 0) // params, programs, states
    {
        for (uint32_t i = 0; i < kParamCount; ++i)
            fValues[i] = kParams[i].def;
        initVoiceManager();
    }

protected:
    /* -- information ------------------------------------------------------ */
    const char* getLabel()       const override { return "TwinFM"; }
    const char* getDescription() const override
    {
        return "A dual 4-operator FM synth built on the PolygraphModular voice engine.";
    }
    const char* getMaker()    const override { return "Unlikelyware"; }
    const char* getHomePage() const override { return "https://www.unlikelyware.com"; }
    const char* getLicense()  const override { return "MIT"; }
    uint32_t    getVersion()  const override { return d_version(0, 1, 0); }
    int64_t     getUniqueId() const override { return d_cconst('T', 'w', 'F', 'M'); }

    /* -- init ------------------------------------------------------------- */
    void initAudioPort(bool input, uint32_t index, AudioPort& port) override
    {
        port.groupId = kPortGroupStereo; // 0 inputs; two output channels form a stereo pair
        Plugin::initAudioPort(input, index, port);
    }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= kParamCount)
            return;
        const ParamSpec& s = kParams[index];
        parameter.hints      = kParameterIsAutomatable
                             | (s.logarithmic ? kParameterIsLogarithmic : 0u)
                             | (s.integer     ? kParameterIsInteger     : 0u);
        parameter.name       = s.name;
        parameter.symbol     = s.symbol;
        parameter.unit       = s.unit;
        parameter.ranges.min = s.min;
        parameter.ranges.max = s.max;
        parameter.ranges.def = s.def;
    }

    float getParameterValue(uint32_t index) const override
    {
        return index < kParamCount ? fValues[index] : 0.0f;
    }

    void setParameterValue(uint32_t index, float value) override
    {
        if (index >= kParamCount)
            return;
        fValues[index] = value;
        applyParam(index, value);
    }

    /* -- lifecycle -------------------------------------------------------- */
    void sampleRateChanged(double) override { initVoiceManager(); }
    void activate()               override { if (fVoiceManager) fVoiceManager->reset(); }

    /* -- process ---------------------------------------------------------- */
    void run(const float**, float** outputs, uint32_t frames,
             const MidiEvent* midiEvents, uint32_t midiEventCount) override
    {
        float* outL = outputs[0];
        float* outR = outputs[1];

        uint32_t pos = 0, ev = 0;
        while (pos < frames)
        {
            // Dispatch every MIDI event scheduled at or before the current frame.
            for (; ev < midiEventCount && midiEvents[ev].frame <= pos; ++ev)
                dispatchMidi(midiEvents[ev]);

            uint32_t next = frames;
            if (ev < midiEventCount && midiEvents[ev].frame < frames)
                next = midiEvents[ev].frame;

            renderSegment(outL, outR, pos, next);
            pos = next;
        }
    }

private:
    std::unique_ptr<VoiceManager> fVoiceManager;
    float                         fValues[kParamCount];

    // FM4Op item for engine offset k (kFmAlgorithm .. kFmOversample).
    static uint8_t fmEngineItem(uint32_t k)
    {
        switch (k)
        {
        case kFmAlgorithm:  return FM4OpParamItemAlgorithm;
        case kFmFeedback:   return FM4OpParamItemFeedback;
        case kFmPitch:      return FM4OpParamItemPitch;
        case kFmFine:       return FM4OpParamItemFine;
        case kFmDetune:     return FM4OpParamItemDetune;
        case kFmOversample: return FM4OpParamItemOversample;
        default:            return 0;
        }
    }

    // FM4Op item for operator item j. FM4Op's four 13-item operator blocks share one layout, so
    // operator o's item is Op1's item + 13·o.
    static uint8_t fmOpItem(uint32_t op, uint32_t j)
    {
        static constexpr uint8_t kOp1Items[kFmOpCount] = {
            FM4OpParamItemOp1Wave,   FM4OpParamItemOp1Ratio,  FM4OpParamItemOp1Fine,
            FM4OpParamItemOp1Level,  FM4OpParamItemOp1Delay,  FM4OpParamItemOp1Attack,
            FM4OpParamItemOp1Decay,  FM4OpParamItemOp1Sustain, FM4OpParamItemOp1Release};
        constexpr uint8_t kOpStride = FM4OpParamItemOp2AmpInput - FM4OpParamItemOp1AmpInput;
        return static_cast<uint8_t>(kOp1Items[j] + op * kOpStride);
    }

    // Map a DPF param index onto its VoiceManager module item address (0 = handled elsewhere).
    static AUParameterAddress vmAddress(uint32_t index)
    {
        // FM engines A / B.
        const uint32_t fmFirst = fmParam(0, 0), fmEnd = fmParam(2, 0);
        if (index >= fmFirst && index < fmEnd)
        {
            const uint32_t rel    = index - fmFirst;
            const uint32_t engine = rel / kFmEngineParamCount;
            const uint32_t k      = rel % kFmEngineParamCount;
            const uint8_t  inst   = engine == 0 ? ModuleInstance1 : ModuleInstance2;
            const uint8_t  item   = k < kFmOpBase
                                  ? fmEngineItem(k)
                                  : fmOpItem((k - kFmOpBase) / kFmOpCount, (k - kFmOpBase) % kFmOpCount);
            return MODULE_PARAM_ADDRESS(ModuleParamKindFM4Op, inst, item);
        }

        const auto mixer  = [](uint8_t item) { return MODULE_PARAM_ADDRESS(ModuleParamKindQuadStereoMixer, ModuleInstance1, item); };
        const auto lpf    = [](uint8_t item) { return MODULE_PARAM_ADDRESS(ModuleParamKindButterworthLowPassFilter, ModuleInstance1, item); };
        const auto adsr   = [](uint8_t inst, uint8_t item) { return MODULE_PARAM_ADDRESS(ModuleParamKindADSR, inst, item); };
        const auto chorus = [](uint8_t item) { return MODULE_PARAM_ADDRESS(ModuleParamKindChorus, ModuleInstance1, item); };
        switch (index)
        {
        case kParamBendRange:      return pitchBend1RangeAddress;
        case kParamMasterLevel:    return MODULE_PARAM_ADDRESS(ModuleParamKindFinalOutput, ModuleInstance1, FinalOutputParamItemLevel);
        case kParamMixALevel:      return mixer(QuadStereoMixerParamItemLevel1);
        case kParamMixAPan:        return mixer(QuadStereoMixerParamItemPan1);
        case kParamMixBLevel:      return mixer(QuadStereoMixerParamItemLevel2);
        case kParamMixBPan:        return mixer(QuadStereoMixerParamItemPan2);
        case kParamLpfCutoff:      return lpf(LPFParamItemPitch);
        case kParamLpfResonance:   return lpf(LPFParamItemQ);
        case kParamLpfEnvAmount:   return lpf(LPFParamItemFreqModLevel);    // mod 1 = filter env
        case kParamLpfPressure:    return lpf(LPFParamItemFreqModLevel2);   // mod 2 = pressure
        case kParamFiltAttack:     return adsr(ModuleInstance1, ADSRParamItemAttack);
        case kParamFiltDecay:      return adsr(ModuleInstance1, ADSRParamItemDecay);
        case kParamFiltSustain:    return adsr(ModuleInstance1, ADSRParamItemSustain);
        case kParamFiltRelease:    return adsr(ModuleInstance1, ADSRParamItemRelease);
        case kParamAmpAttack:      return adsr(ModuleInstance2, ADSRParamItemAttack);
        case kParamAmpDecay:       return adsr(ModuleInstance2, ADSRParamItemDecay);
        case kParamAmpSustain:     return adsr(ModuleInstance2, ADSRParamItemSustain);
        case kParamAmpRelease:     return adsr(ModuleInstance2, ADSRParamItemRelease);
        case kParamChorusMix:      return chorus(ChorusParamItemMix);
        case kParamChorusRate:     return MODULE_PARAM_ADDRESS(ModuleParamKindSharedLFO, ModuleInstance1, SharedLFOParamItemPitch);
        case kParamChorusDepth:    return chorus(ChorusParamItemModDelayLevel);
        case kParamChorusDelay:    return chorus(ChorusParamItemBaseDelay);
        case kParamChorusFeedback: return chorus(ChorusParamItemFeedback);
        default:                   return 0;
        }
    }

    static bool isMono(float voiceModeValue)
    {
        return static_cast<int>(voiceModeValue + 0.5f) == 0; // 0 = Mono, 1 = poly
    }

    // Voice mode: Mono = 1 voice + VoiceManager legato (last-note note-stack), both envelopes and
    // both FM engines in true legato (no re-attack while a note is held). Poly = 10 voices.
    void applyVoiceMode(float value)
    {
        if (!fVoiceManager)
            return;
        const bool mono = isMono(value);
        setConfig(MIDIParamItemVoiceCount, mono ? 1.0f : 10.0f);
        setConfig(MIDIParamItemLegato,     mono ? 1.0f : 0.0f);
        const float legato = mono ? 2.0f : 0.0f;   // 0 = Steal, 2 = LegatoTrue
        for (uint8_t inst : {ModuleInstance1, ModuleInstance2})
        {
            fVoiceManager->setParameter(MODULE_PARAM_ADDRESS(ModuleParamKindADSR, inst, ADSRParamItemLegato), legato);
            fVoiceManager->setParameter(MODULE_PARAM_ADDRESS(ModuleParamKindFM4Op, inst, FM4OpParamItemRetrigger), legato);
        }
    }

    void applyParam(uint32_t index, float value)
    {
        if (!fVoiceManager)
            return;
        if (index == kParamVoiceMode) { applyVoiceMode(value); return; }
        const AUParameterAddress a = vmAddress(index);
        if (a != 0)
            fVoiceManager->setParameter(a, value);
    }

    // (Re)build the VoiceManager for the current sample rate and push all defaults into it.
    void initVoiceManager()
    {
        fVoiceManager = std::make_unique<VoiceManager>(getSampleRate(), MODULE_BUFF_SIZE);

        // VoiceManager params aren't zero-initialised. Voice count + legato are driven by the
        // Voices param (applyVoiceMode, pushed in the loop below); set the rest here.
        setConfig(MIDIParamItemChannelFilterValue, 0);
        setConfig(MIDIParamItemGroupFilterValue, 0);
        setConfig(MIDIParamItemVoiceStealAlgorithm, VoiceStealAlgorithmOldest);
        setConfig(MIDIParamItemFreeRun, 0);

        // Fixed voicing that isn't exposed as a param: full stereo width out of the Final stage
        // (the engines' detune/pan and the chorus make the width), and the chorus LFO's right
        // channel 90° ahead of the left for a wide stereo sweep.
        fVoiceManager->setParameter(
            MODULE_PARAM_ADDRESS(ModuleParamKindFinalOutput, ModuleInstance1, FinalOutputParamItemSpread), 1.0f);
        fVoiceManager->setParameter(
            MODULE_PARAM_ADDRESS(ModuleParamKindSharedLFO, ModuleInstance1, SharedLFOParamItemPhase),
            static_cast<float>(LFOParamLRPhase90));

        for (uint32_t i = 0; i < kParamCount; ++i)
            applyParam(i, fValues[i]);
    }

    void setConfig(MIDIParamItem item, float v)
    {
        fVoiceManager->setVoiceManagerParameter(
            MODULE_PARAM_ADDRESS(ModuleParamKindMIDI, ModuleInstance1, item), v);
    }

    void dispatchMidi(const MidiEvent& me)
    {
        const uint8_t* d = me.size > MidiEvent::kDataSize ? me.dataExt : me.data;
        const pm::UmpMessage m = pm::fromMidi1Bytes(d[0],
                                                    me.size > 1 ? d[1] : 0,
                                                    me.size > 2 ? d[2] : 0);
        if (pm::isChannelMessage(m))
            fVoiceManager->handleMIDIMessage(m);
    }

    // Render [start,end) in kControlChunk pieces; VoiceManager zeroes + sums voices.
    void renderSegment(float* outL, float* outR, uint32_t start, uint32_t end)
    {
        for (uint32_t p = start; p < end;)
        {
            const uint32_t chunk = std::min<uint32_t>(end - p, kControlChunk);
            fVoiceManager->process(chunk, outL + p, outR + p);
            p += chunk;
        }
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TwinFMPlugin)
};

Plugin* createPlugin()
{
    return new TwinFMPlugin();
}

END_NAMESPACE_DISTRHO
