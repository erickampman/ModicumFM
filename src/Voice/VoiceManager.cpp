//
//  VoiceManager.mm
//
//  Created by Eric Kampman on 4/13/25.
//

#include "pm_midi.h"
#include "pm_addresses.h"
#include "VoiceManager.hpp"
#include <algorithm>

bool VoiceManager::loggingEnabled = false;

VoiceManager::VoiceManager(double sampleRate, uint32_t blockSize)
{
	moduleFactory = std::make_unique<ModuleFactory>(sampleRate, blockSize);
	initialize(sampleRate);
}

void VoiceManager::initialize(double sampleRate) {
//    LOG_VM("VoiceManager::initialize entrypoint");
    referenceVoice = std::make_unique<Voice>(sampleRate);
	voices.reserve(MaxVoices);
	for (size_t i = 0; i < MaxVoices; ++i) {
		auto voice = std::make_unique<Voice>(sampleRate);
		voice->id = static_cast<uint32_t>(voiceIdCounter++);
		// Optional: attach oscillator/envelope here via ModuleFactory
		voices.push_back(std::move(voice));
	}
	initializeVoices(&pitchConverter);
}

void VoiceManager::initializeVoices(PitchConverter *pitchConverter)
{
    referenceVoice->initializeModules(moduleFactory.get(), pitchConverter, &sharedStateRegistry);
    buildDefaultPatch(*referenceVoice);
    
	for (auto& voice : voices) {
		voice->initializeModules(moduleFactory.get(), pitchConverter, &sharedStateRegistry);
		buildDefaultPatch(*voice);
	}

    // The CC converter is the Timbre source: CC74, tracked per channel, with MIDI 2.0 per-note
    // controller #74 (and CLAP Brightness) per note. Fixed, so it is part of the patch, not a param.
    setParameter(MODULE_PARAM_FROM_BASE_AND_ITEM(timbre1Address, CCParamItemNumber), float(pm::kRegPerNoteBrightness));
    setParameter(MODULE_PARAM_FROM_BASE_AND_ITEM(timbre1Address, CCParamItemPerNote), 1.0f);
}

void VoiceManager::reset() {
    sustainPedalActive = false;
    pedalSustainedVoiceIDs.clear();
    allNotesOff();
    for (auto& voice : voices) {
        voice->reset();
    }
    sharedStateRegistry.reset();   // soft reset: clears state, keeps objects alive
}

// This occurs at a bad time in initialization. I want to defer this until
// later, with default patches.
#define HAS_DEFAULT_PATCH   1
void VoiceManager::buildDefaultPatch(Voice& voice)
{
#if HAS_DEFAULT_PATCH
    // ModicumFM voice:
    //   PitchConverter --> Slew (mono glide) + PitchBend --(Combine 2, Add)--> FM6Op pitch
    //   FM6Op --> LPF  (cutoff mod 1 = ADSR 1 filter env, cutoff mod 2 = pressure, mod 3 = timbre)
    //   Pressure and Timbre (CC74) also --> FM6Op brightness / level mod inputs (Expression)
    //   --> Chorus (delay mod = SharedLFO, one phase across all voices) --> Final
    // No VCA: the carriers' own envelopes shape the amplitude (DX-style), and the voice frees once
    // the Final output falls silent. The FM6Op takes the note gate from MIDI directly (gate input
    // unpatched); patching the pitch input makes it follow the summed pitch instead of the raw MIDI
    // note, which is what lets pitch bend reach it.
    Module* pitchConverterModule = voice.getModule(pitchConverter1Address);
    Module* slewModule           = voice.getModule(slew1Address);
    Module* pitchBendModule      = voice.getModule(pitchBend1Address);
    Module* pitchSumModule       = voice.getModule(combine2Address);
    Module* fmModule             = voice.getModule(fm6Op1Address);
    Module* filterEnvModule      = voice.getModule(adsr1Address);
    Module* pressureModule       = voice.getModule(channelPressure1Address);
    Module* timbreModule         = voice.getModule(timbre1Address);
    Module* lpfModule            = voice.getModule(lpf1Address);
    Module* lfoModule            = voice.getModule(sharedLFO1Address);
    Module* chorusModule         = voice.getModule(chorus1Address);
    Module* finalModule          = voice.getModule(finalAddress);

    //                   upstream              -> downstream       (downstream input port)
    ++(finalModule->activeCount);
    voice.connectModules(chorusModule,         finalModule,     finalInputAddress);
    voice.connectModules(lpfModule,            chorusModule,    chorus1InputAddress);
    voice.connectModules(lfoModule,            chorusModule,    chorus1ModInputAddress);
    voice.connectModules(fmModule,             lpfModule,       lpf1InputAddress);
    voice.connectModules(filterEnvModule,      lpfModule,       lpf1FreqModInputAddress);
    voice.connectModules(pressureModule,       lpfModule,       lpf1FreqModInput2Address);
    // Expression: Pressure -> brightness, carrier level (and the cutoff, above); Timbre (CC74) ->
    // brightness and the cutoff. The plugin's Expression knobs are these inputs' levels, all 0 by
    // default, so the connections are silent until a knob moves.
    voice.connectModules(pressureModule,       fmModule,        fm6Op1BrightModInputAddress);
    voice.connectModules(pressureModule,       fmModule,        fm6Op1LevelModInputAddress);
    voice.connectModules(timbreModule,         fmModule,        fm6Op1BrightMod2InputAddress);
    voice.connectModules(timbreModule,         lpfModule,       lpf1FreqModInput3Address);
    voice.connectModules(pitchSumModule,       fmModule,        fm6Op1PitchInputAddress);
    // The glide slews the note pitch only: the bend (and Pitch 7.25, applied in the bend converter)
    // joins after it, so a bend during a glide is not smeared.
    voice.connectModules(slewModule,           pitchSumModule,  combine2Input1Address);
    voice.connectModules(pitchConverterModule, slewModule,      slew1InputAddress);
    voice.connectModules(pitchBendModule,      pitchSumModule,  combine2Input2Address);
    voice.setParameter(combine2TypeAddress, static_cast<float>(ParamItemCombineType_Add));
#else
    // do nothing
#endif
}

void VoiceManager::handleMIDIMessage(const pm::UmpMessage& message) {
	// Note on (and maybe off) require special handling, for
	// Voice allocation.
    if (VoiceManager::loggingEnabled) {
        std::string descStr = pm::toString(message);
        LOG_VM("VoiceManager::handleMIDIMessage %s ", descStr.c_str());
    }
    
    if (pm::isChannelMessage(message)) {
        uint32_t channel = uint32_t(voiceManagerParamByEnum(MIDIParamItemChannelFilterValue));
        if (channel) {
            channel -= 1;
            if (channel != message.channel) {
                return;
            }
        }
    }
    
    uint32_t group = uint32_t(voiceManagerParamByEnum(MIDIParamItemGroupFilterValue));
    if (group) {
        group -= 1;
        if (message.group != group) {
            return;
        }
    }
    
	if (message.messageType == pm::kUmpTypeChannelVoice2) {
		switch (message.status) {
		case pm::kStatusNoteOn:
			noteOn(message);
            return;
        case pm::kStatusNoteOff:
            noteOff(message);
            return;
        case pm::kStatusControlChange:
            // Intercept CC 64 (sustain pedal) for voice-lifecycle management.
            // Fall through so CCConverter modules also receive it.
            if (message.ccIndex == 64) {
                bool newState = message.ccData >= (UINT32_MAX / 2u + 1u);
                handleSustainPedalChange(newState);
            }
            // Channel mode messages: handled here, not passed on to the modules.
            if (message.ccIndex == 123) { allNotesOffOnChannel(message.group, message.channel); return; }
            if (message.ccIndex == 120) { allSoundOffOnChannel(message.group, message.channel); return; }
            break;
		default:
			break;
		}
	}

	for (auto& voice : voices) {
		if (voice->wantsMIDIMessage(message, voice->id)) {
			voice->handleMIDIMessage(message);
		}
	}
}

bool VoiceManager::freeRunning() {
    AUValue freeRun = voiceManagerParamByEnum(MIDIParamItemFreeRun);
    AUValue voiceCount = voiceManagerParamByEnum(MIDIParamItemVoiceCount);
    return (freeRun >= 0.5f && voiceCount == 1);
}

void VoiceManager::noteOn(const pm::UmpMessage& msg) {
    LOG_VM("VoiceManager::noteOn entrypoint");
    uint32_t maxVoices = uint32_t(voiceManagerParamByEnum(MIDIParamItemVoiceCount));

    // needed?
    if (freeRunning()) {
        Voice* voice = voices[0].get();
        // Optionally: voice->copyParamsFrom(*referenceVoice); if needed
        voice->handleMIDIMessage(msg);  // retune / retrigger as you see fit
        return;
    }

    // Legato mode: when in mono (1 voice), skip the steal path and route
    // the NoteOn directly to the voice that is still SOUNDING — Active (a note
    // is held) OR Deactivating (the previous note was released but its envelope
    // is still ringing out).  Routing a detached-but-still-ringing note here
    // (instead of letting it fall through to the steal path) is what keeps the
    // oscillator on the same voice so it can retune without a phase-reset click
    // (see Voice::handleMIDIMessage / Module::legatoContinuation). The ADSR and
    // FM6Op modules decide independently (via their own legato param) whether
    // to retrigger the envelope or let it continue. A fully Inactive voice is
    // silent, so a fresh note there resets phase harmlessly — fall through to
    // normal allocation for that case.
    uint32_t legatoMode = uint32_t(voiceManagerParamByEnum(MIDIParamItemLegato));
    if (legatoMode > 0) {
        for (auto& v : voices) {
            if (v && (v->voiceState() == VoiceState::Active
                   || v->voiceState() == VoiceState::Deactivating)) {
                v->handleMIDIMessage(msg);
                return;
            }
        }
        // No sounding voice: fall through to normal allocation below.
    }

    Voice* voice = findVoiceForMessage(msg);
    bool stolen = false;
    if (!voice) {
        voice = findFreeVoice();
    }
	if (!voice) {
		voice = stealVoice(msg); // implement envelope-aware stealing
		if (!voice) return;   // out of voices
        stolen = true;
	}
    if (stolen) {
        // If this voice was pedal-sustained, cancel the deferred NoteOff —
        // startSteal() also clears pedalSustained on the VoiceStateManager.
        auto it = std::find(pedalSustainedVoiceIDs.begin(), pedalSustainedVoiceIDs.end(), voice->id);
        if (it != pedalSustainedVoiceIDs.end()) {
            pedalSustainedVoiceIDs.erase(it);
        }
        voice->startSteal(msg);
    }
    voice->copyParamsFrom(*referenceVoice);
	voice->handleMIDIMessage(msg);
}

void VoiceManager::noteOff(const pm::UmpMessage& msg) {
    LOG_VM("VoiceManager::noteOff entrypoint");

    // FreeRun and legato modes bypass sustain pedal logic:
    // FreeRun ignores note lifecycle entirely; legato has its own held-note stack.
    bool bypass = freeRunning() ||
                  (uint32_t(voiceManagerParamByEnum(MIDIParamItemLegato)) > 0);

    for (auto& voice : voices) {
        if (!voice->wantsMIDIMessage(msg, voice->id)) continue;

        if (!bypass && sustainPedalActive) {
            // Pedal held — defer the NoteOff. Only add to list if not already there
            // (guards against duplicate entries if multiple buffers send the same NoteOff).
            if (!voice->voiceStateManager.isPedalSustained()) {
                voice->voiceStateManager.setPedalSustained(true);
                voice->syncNoteReleased();   // pedal-held counts as released (MPE §3.3.3)
                pedalSustainedVoiceIDs.push_back(voice->id);
            }
        } else {
            voice->handleMIDIMessage(msg);
        }
    }
}

void VoiceManager::handleSustainPedalChange(bool newState) {
    if (newState == sustainPedalActive) return;
    sustainPedalActive = newState;

    if (!newState && !pedalSustainedVoiceIDs.empty()) {
        // Pedal released — dispatch deferred NoteOffs to all sustained voices.
        for (uint32_t voiceID : pedalSustainedVoiceIDs) {
            for (auto& voice : voices) {
                if (voice->id == voiceID && voice->voiceStateManager.isPedalSustained()) {
                    voice->voiceStateManager.setPedalSustained(false);
                    // Built from the voice's own note-on so it carries that note's channel and group,
                    // which note-off matching requires.
                    const auto& current = voice->voiceStateManager.currentNote();
                    if (auto noteOff = current ? pm::makeNoteOffMsgFromOnMsg(*current) : std::nullopt) {
                        voice->handleMIDIMessage(*noteOff);
                    }
                }
            }
        }
        pedalSustainedVoiceIDs.clear();
    }
}

// Release every note a voice holds: the mono stack's silent notes first, then the sounding one, so
// releasing the sounding note never retriggers a stacked note. Note-offs are built from the
// note-ons themselves because note-off matching includes channel and group.
void VoiceManager::releaseAllNotes(Voice& voice) {
    std::vector<pm::UmpMessage> notes = voice.voiceStateManager.heldNotes();   // copy: mutated below
    const std::optional<pm::UmpMessage> current = voice.voiceStateManager.currentNote();
    if (current) {
        notes.erase(std::remove_if(notes.begin(), notes.end(),
                        [&current](const pm::UmpMessage& m) { return pm::isSameNote(m, *current); }),
                    notes.end());
        notes.push_back(*current);
    }
    for (const pm::UmpMessage& on : notes) {
        if (auto noteOff = pm::makeNoteOffMsgFromOnMsg(on)) voice.handleMIDIMessage(*noteOff);
    }
}

int VoiceManager::keyForNoteId(int32_t noteId) {
    if (noteId < 0) return -1;
    for (auto& voice : voices) {
        const auto& current = voice->voiceStateManager.currentNote();
        if (current && current->noteId == noteId) return current->note;
    }
    return -1;
}

bool VoiceManager::holdsNote(const pm::UmpMessage& noteOn) {
    for (auto& voice : voices)
        if (voice->voiceStateManager.holdsNote(noteOn)) return true;
    return false;
}

// Take a voice out of the sustain pedal's deferred list, so pedal-up does not send it a note-off for
// a note it no longer has.
void VoiceManager::dropPedalSustain(Voice& voice) {
    if (!voice.voiceStateManager.isPedalSustained()) return;
    voice.voiceStateManager.setPedalSustained(false);
    pedalSustainedVoiceIDs.erase(std::remove(pedalSustainedVoiceIDs.begin(), pedalSustainedVoiceIDs.end(), voice.id),
                                 pedalSustainedVoiceIDs.end());
}

// releaseAllNotes, limited to the notes played on one group/channel. A mono voice can hold notes
// from several channels; only this channel's leave the stack, and the sounding note is released
// last so it does not retrigger one of them.
void VoiceManager::releaseNotesOnChannel(Voice& voice, uint8_t group, uint8_t channel) {
    auto onChannel = [group, channel](const pm::UmpMessage& m) { return m.group == group && m.channel == channel; };
    std::vector<pm::UmpMessage> notes;
    const std::optional<pm::UmpMessage> current = voice.voiceStateManager.currentNote();
    for (const pm::UmpMessage& held : voice.voiceStateManager.heldNotes())
        if (onChannel(held) && !(current && pm::isSameNote(held, *current))) notes.push_back(held);
    if (current && onChannel(*current)) {
        dropPedalSustain(voice);   // the pedal was holding this very note
        notes.push_back(*current);
    }
    for (const pm::UmpMessage& on : notes) {
        if (auto noteOff = pm::makeNoteOffMsgFromOnMsg(on)) voice.handleMIDIMessage(*noteOff);
    }
}

// CC 123. Releases the channel's notes, the sustain pedal's included (hosts and senders use it to
// clear everything). The notes go into their release, as after any note-off.
void VoiceManager::allNotesOffOnChannel(uint8_t group, uint8_t channel) {
    for (auto& voice : voices) {
        if (voice->voiceStateManager.holdsNoteOnChannel(group, channel))
            releaseNotesOnChannel(*voice, group, channel);
    }
}

// CC 120. Silences the channel's voices at once, release tails included: the voice goes Inactive
// (an Inactive voice outputs nothing) and its FM engine (operator envelopes and phases), filter
// envelope, filter and chorus are reset so the next note starts clean. A full Voice::reset() would
// also clear the per-channel bend and pressure memory, which belongs to every channel, not just this
// one. (Ported from PlainOldSynth a3b94c7.)
void VoiceManager::allSoundOffOnChannel(uint8_t group, uint8_t channel) {
    static constexpr AUParameterAddress kSoundModules[] = {
        fm6Op1Address, adsr1Address, lpf1Address, chorus1Address };
    for (auto& voice : voices) {
        if (!voice->voiceStateManager.holdsNoteOnChannel(group, channel)) continue;
        dropPedalSustain(*voice);
        voice->voiceStateManager.silence();
        for (const AUParameterAddress address : kSoundModules)
            if (Module* module = voice->getModule(address)) module->reset();
    }
}

void VoiceManager::allNotesOff() {
    // Clear pedal state so no deferred NoteOffs linger after all-notes-off.
    sustainPedalActive = false;
    pedalSustainedVoiceIDs.clear();

	for (auto& voice : voices) {
        voice->voiceStateManager.setPedalSustained(false);
		if (voice->isActive()) {
            releaseAllNotes(*voice);
		}
	}
}

void VoiceManager::panic() {
    // Every voice, whatever the Group/Channel filter says: handleMIDIMessage()'s filter is bypassed
    // by talking to the voices directly.
    sustainPedalActive = false;
    pedalSustainedVoiceIDs.clear();
    for (auto& voice : voices) {
        voice->voiceStateManager.setPedalSustained(false);
        releaseAllNotes(*voice);
    }
}

void VoiceManager::drainCommandQueue()
{
    VoiceCommand cmd;
    while (commandQueue.pop(cmd)) {
        switch (cmd.type) {

        case VoiceCommandType::Connect: {
            for (auto& voice : voices) {
                Module *up = voice->getModule(cmd.upstreamID);
                Module *dn = voice->getModule(cmd.downstreamID);
                voice->connectModules(up, dn, cmd.inputAddress);
            }
            break;
        }

        case VoiceCommandType::ClearInput: {
            uint64_t baseAddr = MODULE_BASE_ADDRESS(cmd.inputAddress);
            uint8_t inputID   = static_cast<uint8_t>(MODULE_ITEM_FROM_ADDRESS(cmd.inputAddress));
            for (auto& voice : voices) {
                Module *mod = voice->getModule(baseAddr);
                if (mod) {
                    // Decrement upstream's activeCount before clearing the slot,
                    // completing the compound op that connectModules started.
                    Module* upstream = mod->inputs[inputID].upstream;
                    if (upstream && upstream->activeCount > 0) {
                        --(upstream->activeCount);
                    }
                    mod->removeInput(inputID);
                }
            }
            break;
        }
        }
    }
}

void VoiceManager::process(uint32_t frameCount, float *left, float *right)
{
    // Apply any pending connection changes before processing voices.
    drainCommandQueue();

    // necessary?
    std::fill_n(left, frameCount, 0.f);
    std::fill_n(right, frameCount, 0.f);

    // Advance any cross-voice per-chunk shared state exactly once, before the voices read it
    // (the chorus's SharedLFO advances its one shared phase here).
    sharedStateRegistry.beginBlock(activeCount(), frameCount);

    uint32_t maxVoices = uint32_t(voiceManagerParamByEnum(MIDIParamItemVoiceCount));
    
    for (size_t i = 0; i < voices.size(); ++i) {
        auto& v = voices[i];
        if (!v) continue;
        
        bool active = false;
        
        if (freeRunning() && i == 0) {  // only voice[0] potentially is free-runnning
            active = true;
        } else {
            if (v->isActive()) { active = true; }
        }

        if (active) {
            v->process(frameCount, left, right);
        }
	}
}

//uint32_t VoiceManager::activeCount() {
//    uint32_t activeCount = 0;
//    for (auto& voice : voices) {
//        if (voice->isActive()) {
//            activeCount++;
//        }
//    }
//    return activeCount;
//}
uint32_t VoiceManager::activeCount()
{
    uint32_t activeCount = 0;
    bool freeRun = freeRunning();

    for (size_t i = 0; i < voices.size(); ++i) {
        const auto& v = voices[i];
        if (!v) {
            continue;
        }

        if (v->isActive()) {
            ++activeCount;
        } else if (freeRun && i == 0) {
            // Voice 0 is considered logically active in free-run mode
            ++activeCount;
        }
    }

    return activeCount;
}


Voice* VoiceManager::findFreeVoice() {
    uint32_t max = voiceManagerParamByEnum(MIDIParamItemVoiceCount);
    if (activeCount() < max) {
        for (auto& voice : voices) {
            if (!voice->isActive())
                return voice.get();
        }
    }
    return nullptr;
}

// TODO: implement more advanced stealing (e.g. quietest, oldest)
Voice* VoiceManager::stealVoice(const pm::UmpMessage& msg) {
    Voice *stolen = nullptr;
    
//    for (auto& voice : voices) {
//        if (voice->voiceStateManager.canBeStolen()) {
//            stolen = voice.get(); // safe to reuse
//            LOG_VM("VoiceManager::stealVoice canBeStolen id(%d)", voice->id);
//            break;
//        }
//    }
//    if (!stolen) {
        uint8_t algorithm = uint8_t(voiceManagerParamByEnum(MIDIParamItemVoiceStealAlgorithm));
        switch (algorithm) {
        case VoiceStealAlgorithmOldest:
            stolen = oldestVoice();
            break;
        case VoiceStealAlgorithmLowest:
            stolen = lowestVoice();
            break;
        case VoiceStealAlgorithmHighest:
            stolen = highestVoice();
            break;
        case VoiceStealAlgorithmClosest:
            stolen = closest(msg);
            break;
        case VoiceStealAlgorithmFurthest:
            stolen = furthest(msg);
            break;
        }
        if (stolen) {
            LOG_VM("VoiceManager::stealVoice voice id(%d)", stolen->id);
        }
//    }
    return stolen;
}


Voice* VoiceManager::oldestVoice() {
    Voice* oldestVoice = nullptr;
    TimePoint oldestTime = Clock::now();

    for (auto& voice : voices) {
        if (!voice->voiceStateManager.canBeStolen()) continue;

        if (voice->lastUsed < oldestTime) {
            oldestTime = voice->lastUsed;
            oldestVoice = voice.get();
        }
    }
    return oldestVoice;
}
Voice* VoiceManager::lowestVoice() {
    Voice* ret = nullptr;

    for (auto& voice : voices) {
        if (!voice->voiceStateManager.canBeStolen()) continue;

        if (!ret) {
            ret = voice.get();
        } else {
            uint8_t currentNote = voice->currentNoteNumber;
            if (currentNote) {
                if (voice->currentNoteNumber < ret->currentNoteNumber) {
                    ret = voice.get();
                }
            }
        }
    }
    return ret;
}
Voice* VoiceManager::highestVoice() {
    Voice* ret = nullptr;

    for (auto& voice : voices) {
        if (!voice->voiceStateManager.canBeStolen()) continue;

        if (!ret) {
            ret = voice.get();
        } else {
            uint8_t currentNote = voice->currentNoteNumber;
            if (currentNote) {
                if (voice->currentNoteNumber > ret->currentNoteNumber) {
                    ret = voice.get();
                }
            }
        }
    }
    return ret;
}

Voice* VoiceManager::closest(const pm::UmpMessage& msg) {
    Voice* ret = nullptr;

    if (!pm::isNoteOn(msg)) {
        return ret;
    }
    uint8_t msgNote = msg.note;
    
    for (auto& voice : voices) {
        if (!voice->voiceStateManager.canBeStolen()) continue;

        if (!ret) {
            ret = voice.get();
        } else {
            uint8_t currentNote = voice->currentNoteNumber;
            if (currentNote) {
                uint8_t newDist = abs(voice->currentNoteNumber - msgNote);
                uint8_t oldDist = abs(ret->currentNoteNumber - msgNote);
                
                if (newDist < oldDist) {
                    ret = voice.get();
                }
            }
        }
    }
    return ret;
}

Voice* VoiceManager::furthest(const pm::UmpMessage& msg) {
    Voice* ret = nullptr;

    if (!pm::isNoteOn(msg)) {
        return ret;
    }
    uint8_t msgNote = msg.note;
    
    for (auto& voice : voices) {
        if (!voice->voiceStateManager.canBeStolen()) continue;

        if (!ret) {
            ret = voice.get();
        } else {
            uint8_t currentNote = voice->currentNoteNumber;
            if (currentNote) {
                uint8_t newDist = abs(voice->currentNoteNumber - msgNote);
                uint8_t oldDist = abs(ret->currentNoteNumber - msgNote);
                
                if (newDist > oldDist) {
                    ret = voice.get();
                }
            }
        }
    }
    return ret;
}


// called from VoiceManagerBridge.
void VoiceManager::connectModules(uint64_t upstreamID, uint64_t downstreamID, AUParameterAddress inputAddress)
{
    // referenceVoice is only ever accessed from the main thread, so it is safe
    // to mutate its Module::inputs directly here.
    Module *upstreamModule = referenceVoice->getModule(upstreamID);
    Module *downstreamModule = referenceVoice->getModule(downstreamID);
    referenceVoice->connectModules(upstreamModule, downstreamModule, inputAddress);

    // Polyphonic voices are processed on the render thread, so we must not
    // mutate their Module::inputs here.  Post a command to the SPSC queue;
    // the render thread will drain it at the top of process().
    VoiceCommand cmd;
    cmd.type         = VoiceCommandType::Connect;
    cmd.upstreamID   = upstreamID;
    cmd.downstreamID = downstreamID;
    cmd.inputAddress = inputAddress;
    commandQueue.push(cmd);
}

Voice* VoiceManager::findVoiceForMessage(const pm::UmpMessage& msg) {
    
    if (pm::isNoteOn(msg)) {
        uint32_t max = voiceManagerParamByEnum(MIDIParamItemVoiceCount);
        if (activeCount() >= max) {
            return nullptr;
        }
    }
	for (auto& voice : voices) {
        if (voice->wantsMIDIMessage(msg, 9999)) {
            return voice.get();
        }
	}
	return nullptr;
}

// Parameters

AUValue VoiceManager::voiceManagerParamAtIndex(size_t index)
{
    return params[index];
//        return params_[index];
}

AUValue VoiceManager::voiceManagerParamByEnum(NSUInteger paramEnum) {
    size_t index = PARAM_ENUM_TO_INDEX(paramEnum);
    return voiceManagerParamAtIndex(index);
}

void VoiceManager::setVoiceManagerParameter(AUParameterAddress address, AUValue value) {
//    if (!isVoiceManagerModule(address)) {
//        return;
//    }
    uint8_t item = MODULE_ITEM_FROM_ADDRESS(address);
    params[item - 1] = value;
}

/*
    The below are params distributed to modules
 */

void VoiceManager::setParameter(AUParameterAddress address, AUValue value)
{
    referenceVoice->setParameter(address, value);
    for (auto& vp : voices) {
        vp->setParameter(address, value);
    }

}
AUValue VoiceManager::getParameter(AUParameterAddress address) {
    return referenceVoice->getParameter(address);
}

void VoiceManager::setModSource(AUParameterAddress destInput, AUParameterAddress source)
{
    clearInput(destInput);                 // disconnect current source (safe if none)
    if (source != 0)                        // 0 = None
        connectModules(source, MODULE_BASE_ADDRESS(destInput), destInput);
}

void VoiceManager::clearInput(AUParameterAddress address)
{
    uint64_t baseAddr = MODULE_BASE_ADDRESS(address);
    uint8_t inputID = static_cast<uint8_t>(MODULE_ITEM_FROM_ADDRESS(address));

    // referenceVoice is main-thread-only; mutate directly.
    Module *module = referenceVoice->getModule(baseAddr);
    if (module) {
        // Decrement the upstream module's activeCount before the slot is
        // cleared, mirroring what Voice::connectModules did on connect.
        // (Module::removeInput has this commented out as "Done in Voice".)
        Module* upstream = module->inputs[inputID].upstream;
        if (upstream && upstream->activeCount > 0) {
            --(upstream->activeCount);
        }
        module->removeInput(inputID);
    }

    // Zero the AUParameter value so the state is persisted correctly.
    setParameter(address, 0);

    // Defer the per-voice removeInput to the render thread via the command queue.
    VoiceCommand cmd;
    cmd.type         = VoiceCommandType::ClearInput;
    cmd.upstreamID   = 0;
    cmd.downstreamID = 0;
    cmd.inputAddress = address;
    commandQueue.push(cmd);
}

void VoiceManager::updateSampleRate(double newSampleRate) {
    moduleFactory->updateSampleRate(newSampleRate); // if needed

    referenceVoice->updateSampleRate(newSampleRate);
    for (auto& voice : voices) {
        voice->updateSampleRate(newSampleRate);
    }
}


//bool VoiceManager::isVoiceManagerModule(AUParameterAddress address) {
//    uint8_t moduleKind = MODULE_KIND_FROM_ADDRESS(address);
//    switch (moduleKind) {
//    case ModuleParamKindMIDI:
//        return true;
//    default:
//        return false;
//    }
//}
//
