// FeMUI.cpp — UI side. Under the top strip (logo, wordmark, preset menu, help):
//
//   FM Engine (Algorithm, Feedback, │ Operators: 6 rows (role caption, Wave, Ratio, Level,
//     Pitch / Fine / Detune)        │   envelope graph)
//   Low-Pass Filter                 │
//   Filter Envelope (graph)         │
//   Response (velocity) | Chorus | Global (Voices, Bend Range, Quality, Master)
//
// Every param is a knob unless it's an enum (dropdown). The operator captions show each
// operator's role in the current algorithm (carrier, or modulator N hops from the output) —
// the same role analysis the planned macros use (docs/ROADMAP.md). The UI is the callback for
// all controls, routing by param index. Each operator's Ratio is a RatioKnob, which also edits
// that operator's Fine param (no Fine knob of its own). Envelopes are EnvelopeGraphs (draggable
// points) rather than knobs.

#include "DistrhoUI.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "Dropdown.hpp"
#include "EnvelopeGraph.hpp"
#include "EmbeddedResources.hpp" // build-time codegen from Resources/* (see dpf/utils/res2c.py)
#include "GeneratedPresets.h"    // build-time codegen from Presets/*.json (see tools/gen_presets.py)
#include "GroupBox.hpp"
#include "HelpButton.hpp"
#include "Knob.hpp"
#include "Label.hpp"
#include "Logo.hpp"
#include "PopupMenu.hpp"
#include "PresetDump.hpp" // dev-only JSON patch dumper (no-op unless FEM_PRESET_DUMP=1)
#include "ParamEdit.hpp"
#include "PresetKeys.hpp"
#include "RatioKnob.hpp"
#include "TitleText.hpp"
#include "FeMParams.h"
#include "FM6OpAlgorithms.hpp" // algorithm table + operator roles (shared with the engine)

START_NAMESPACE_DISTRHO

#include "extra/WebView.hpp" // native WebView overlay (WKWebView/WebKitGTK/WebView2), for Help

using namespace fem;
using DGL_NAMESPACE::Color;
using DGL_NAMESPACE::Dropdown;
using DGL_NAMESPACE::EnvelopeGraph;
using DGL_NAMESPACE::GroupBox;
using DGL_NAMESPACE::HelpButton;
using DGL_NAMESPACE::Knob;
using DGL_NAMESPACE::Label;
using DGL_NAMESPACE::Logo;
using DGL_NAMESPACE::NanoSubWidget;
using DGL_NAMESPACE::PopupMenu;
using DGL_NAMESPACE::RatioKnob;
using DGL_NAMESPACE::SubWidget;
using DGL_NAMESPACE::TitleText;

#if FEM_PRESET_DUMP
// Dev-only "DUMP" button: a plain left-click writes the patch JSON. A button (not a keystroke or
// modifier-click) so it works identically in every host on macOS and Linux — Ctrl-click is a
// right-click on macOS, and hosts often don't forward key events to a plugin UI at all.
class DumpButton : public NanoSubWidget
{
public:
    DumpButton(Widget* parent, std::function<void()> onClick)
        : NanoSubWidget(parent), fOnClick(std::move(onClick)) { fHasFont = loadSharedResources(); }
protected:
    void onNanoDisplay() override
    {
        const float w = getWidth(), h = getHeight();
        beginPath(); roundedRect(0, 0, w, h, h * 0.22f);
        fillColor(Color(70, 44, 44)); fill();
        strokeColor(Color(205, 120, 120)); strokeWidth(1.0f); stroke();
        if (fHasFont)
        {
            fontFace(NANOVG_DEJAVU_SANS_TTF); textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
            fontSize(h * 0.6f); fillColor(Color(235, 205, 205)); // scale text with the button
            text(w * 0.5f, h * 0.5f, "DUMP", nullptr);
        }
    }
    bool onMouse(const MouseEvent& ev) override
    {
        if (ev.button == 1 && ev.press
            && ev.pos.getX() >= 0 && ev.pos.getX() < getWidth()
            && ev.pos.getY() >= 0 && ev.pos.getY() < getHeight())
        {
            fOnClick();
            return true;
        }
        return false;
    }
private:
    std::function<void()> fOnClick;
    bool fHasFont = false;
};
#endif

class FeMUI : public UI,
              public Knob::Callback,
              public Dropdown::Callback,
              public PopupMenu::Callback,
              public ParamEditCallback
{
public:
    FeMUI()
        : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT)
    {
        // Lock the aspect ratio to the design ratio so no host can hand the plugin a
        // disproportionate canvas (layout() stretches rows to fill whatever it's given).
        setGeometryConstraints(DISTRHO_UI_DEFAULT_WIDTH * 4 / 5, DISTRHO_UI_DEFAULT_HEIGHT * 4 / 5,
                               true, false);

        // Per-module accent colours, following PM's category scheme.
        fEngineGroup   = makeGroup("FM Engine", colSound());
        fOpGroup       = makeGroup("Operators", colSound());
        fLpfGroup      = makeGroup("Low-Pass Filter", colFilter());
        fFiltEnvGroup  = makeGroup("Filter Envelope", colFilter());
        fResponseGroup = makeGroup("Response", colEnv());
        fChorusGroup   = makeGroup("Chorus", colNeutral());
        fGlobalGroup   = makeGroup("Global", colComb());

        for (uint32_t o = 0; o < kFmOperators; ++o)
            fOpLabel[o] = std::make_unique<Label>(fOpGroup.get());
        updateOpRoles(static_cast<int>(kParams[kParamAlgorithm].def));

        for (uint32_t i = 0; i < kParamCount; ++i)
        {
            if (opItem(i) == kOpFine || isEnvelopeParam(i))
                continue; // edited through its operator's RatioKnob / an EnvelopeGraph
            if (opItem(i) == kOpRatio)
            {
                const uint32_t op = (i - fmOpParam(0, kOpRatio)) / kFmOpCount;
                auto rk = std::make_unique<RatioKnob>(groupFor(i), this, i, fmOpParam(op, kOpFine));
                rk->setLabel(labelFor(i));
                rk->setAccentColor(accentFor(i));
                rk->configure(kParams[i].min, kParams[i].max, kParams[i].logarithmic, kParams[i].def);
                rk->setRatio(kParams[i].def);
                fRatioKnob[op] = rk.get();
                fKnobs[i] = std::move(rk);
                continue;
            }
            if (isDropdownParam(i))
            {
                auto d = std::make_unique<Dropdown>(groupFor(i), this);
                d->setId(i);
                d->setLabel(labelFor(i));
                d->setChoices(choicesFor(i));
                d->setIndex(static_cast<int>(kParams[i].def));
                fDropdowns[i] = std::move(d);
                continue;
            }
            const ParamSpec& s = kParams[i];
            auto knob = std::make_unique<Knob>(groupFor(i), this);
            knob->setId(i);
            knob->setLabel(labelFor(i));
            knob->setAccentColor(accentFor(i));
            knob->configure(s.min, s.max, s.logarithmic, s.def);
            knob->setRealValue(s.def, false);
            knob->setFormatter(formatterFor(i));
            fKnobs[i] = std::move(knob);
        }

        // Envelope graphs: one per operator (with Delay) and the filter envelope (without).
        for (uint32_t o = 0; o < kFmOperators; ++o)
            fOpEnv[o] = makeEnvelope(fOpGroup.get(), colSound(), fmOpParam(o, kOpDelay),
                                     fmOpParam(o, kOpAttack), fmOpParam(o, kOpDecay),
                                     fmOpParam(o, kOpSustain), fmOpParam(o, kOpRelease));
        fFiltEnv = makeEnvelope(fFiltEnvGroup.get(), colFilter(), EnvelopeGraph::kNoParam,
                                kParamFiltAttack, kParamFiltDecay, kParamFiltSustain,
                                kParamFiltRelease);

#if FEM_PRESET_DUMP
        fDumpBtn = std::make_unique<DumpButton>(this, [this] {
            const std::string p = dumpPresetToFile(fMirror);
            std::fprintf(stderr, "[FeM] patch %s %s\n",
                         p.empty() ? "dump FAILED" : "dumped to", p.c_str());
        });
#endif
        // Logo: raw PNG bytes embedded at build time (see EmbeddedResources.hpp).
        fLogo = std::make_unique<Logo>(this, EmbeddedResources::Unlikelyware_Final_ColorData,
                                        EmbeddedResources::Unlikelyware_Final_ColorDataSize);

        // Wordmark: "Fe" + "M" in two accent colours sampled from the logo, Almendra (embedded,
        // matches the unlikelyware.com brand face).
        fTitleText = std::make_unique<TitleText>(this, EmbeddedResources::Almendra_RegularData,
                                                  EmbeddedResources::Almendra_RegularDataSize,
                                                  Color(222, 184, 135), Color(200, 109, 84));

        // Preset selector: applies to the whole patch, so it's parented to the UI itself. It
        // shares the generic Dropdown/PopupMenu plumbing; kPresetDropdownId tells it apart.
        fPresetDropdown = std::make_unique<Dropdown>(this, this);
        fPresetDropdown->setId(kPresetDropdownId);
        fPresetDropdown->setLabel("Preset");
        {
            std::vector<std::string> names;
            for (int i = 0; i < kPresetCount; ++i) names.push_back(kPresets[i].name);
            fPresetDropdown->setChoices(names);
        }
        fPresetDropdown->setPlaceholder("--");
        fPresetDropdown->setIndex(-1); // no preset selected until the user picks one

        // Help: toggles a native WebView overlay showing the bundled help.html.
        fHelpButton = std::make_unique<HelpButton>(this, [this] { toggleHelp(); });

        fMenu = std::make_unique<PopupMenu>(this, this); // created last → draws on top

        layout();
#if FEM_PRESET_DUMP
        for (uint32_t i = 0; i < kParamCount; ++i) fMirror[i] = kParams[i].def;
#endif
    }

    ~FeMUI() override
    {
        if (fHelpWebView != nullptr)
            webViewDestroy(fHelpWebView);
    }

protected:
    /* -- host -> UI ------------------------------------------------------- */
    void parameterChanged(uint32_t index, float value) override
    {
        if (index >= kParamCount)
            return;
        recordMirror(index, value);
        if (opItem(index) == kOpRatio || opItem(index) == kOpFine)
        {
            RatioKnob* rk = fRatioKnob[(index - fmOpParam(0, 0)) / kFmOpCount];
            if (opItem(index) == kOpRatio) rk->setRatio(value);
            else                           rk->setFineCents(value);
            return;
        }
        if (fEnvFor[index])    { fEnvFor[index]->setParamValue(index, value); return; }
        if (fKnobs[index])     { fKnobs[index]->setRealValue(value, false); fKnobs[index]->repaint(); }
        if (fDropdowns[index]) fDropdowns[index]->setIndex(static_cast<int>(value));
        if (index == kParamAlgorithm) updateOpRoles(static_cast<int>(value));
    }

    /* -- knob -> host ----------------------------------------------------- */
    void knobDragStarted(Knob* k) override  { editParameter(k->getId(), true); }
    void knobDragFinished(Knob* k) override { editParameter(k->getId(), false); }
    void knobValueChanged(Knob* k, float v) override
    {
        const uint32_t id = k->getId();
        if (kParams[id].integer) v = std::round(v); // e.g. engine Pitch in whole semitones
        setParameterValue(id, v);
        recordMirror(id, v);
    }

    /* -- composite widgets -> host (RatioKnob) ---------------------------- */
    void paramEditBegin(uint32_t id) override { editParameter(id, true); }
    void paramEditEnd(uint32_t id) override   { editParameter(id, false); }
    void paramEditChange(uint32_t id, float v) override
    {
        setParameterValue(id, v);
        recordMirror(id, v);
    }

    /* -- dropdown + menu -------------------------------------------------- */
    void dropdownClicked(Dropdown* d) override
    {
        int x, y, w, h;
        d->boxRect(x, y, w, h); // y = box top, h = box height (window-absolute)
        fMenu->openFor(d->getId(), d->choices(), d->index(), x, y + h, y, w,
                       static_cast<int>(getHeight()));
    }
    void menuSelected(PopupMenu* menu, int index) override
    {
        const uint32_t id = menu->tag();
        if (id == kPresetDropdownId) { applyPreset(index); return; }
        editParameter(id, true);
        setParameterValue(id, static_cast<float>(index));
        editParameter(id, false);
        recordMirror(id, static_cast<float>(index));
        if (fDropdowns[id]) fDropdowns[id]->setIndex(index);
        if (id == kParamAlgorithm) updateOpRoles(index);
    }

    // Operator captions: "Op N" plus its role in `algorithm` — "out" for a carrier, "mod K" for a
    // modulator K hops from the output — with carriers in the engine accent colour.
    void updateOpRoles(int algorithm)
    {
        const FM6OpRoles roles = fm6OpRoles(algorithm);
        for (uint32_t o = 0; o < kFmOperators; ++o)
        {
            const uint8_t d = roles.depth[o];
            fOpLabel[o]->setText("Op " + std::to_string(o + 1)
                                 + (d == 0 ? "  out" : "  mod " + std::to_string(d)));
            fOpLabel[o]->setColor(d == 0 ? colSound() : Color(170, 176, 186));
        }
    }

    // Apply every param the selected preset has a value for (looked up by "module.item" key via
    // kPresetKeys — the same mapping PresetDump.hpp uses to write these files). Params the
    // preset doesn't mention are left at their current value.
    void applyPreset(int index)
    {
        if (index < 0 || index >= kPresetCount)
            return;
        const PresetEntry& preset = kPresets[index];
        for (uint32_t i = 0; i < kParamCount; ++i)
        {
            const std::string key = std::string(kPresetKeys[i].module) + "." + kPresetKeys[i].item;
            for (int j = 0; j < preset.count; ++j)
            {
                if (key == preset.values[j].key)
                {
                    const float v = preset.values[j].value;
                    editParameter(i, true);
                    setParameterValue(i, v);
                    editParameter(i, false);
                    parameterChanged(i, v);
                    break;
                }
            }
        }
        if (fPresetDropdown) fPresetDropdown->setIndex(index);
    }

    /* -- drawing / layout ------------------------------------------------- */
    void onNanoDisplay() override
    {
        beginPath(); rect(0, 0, getWidth(), getHeight()); fillColor(24, 26, 31); fill();
    }
    void onResize(const ResizeEvent& ev) override
    {
        UI::onResize(ev);
        layout();
        if (fHelpWebView != nullptr)
            resizeHelpWebView();
    }

    // Two jobs on the idle tick:
    //  1. Some Linux hosts (notably REAPER via pugl) don't apply the UI's initial size until the
    //     event loop starts pumping, so the window loads mis-sized until a manual resize.
    //     Re-assert the default size once on the first tick to force correct geometry.
    //  2. WebViews need regular pumping to process their own event loop / message callbacks.
    void uiIdle() override
    {
        if (!fInitialSizeNudged)
        {
            fInitialSizeNudged = true;
            setSize(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT);
        }
        if (fHelpWebView != nullptr)
            webViewIdle(fHelpWebView);
    }

private:
    // Panels declared before their child controls so the children are destroyed first.
    std::unique_ptr<GroupBox>   fEngineGroup, fOpGroup;
    std::unique_ptr<GroupBox>   fLpfGroup, fFiltEnvGroup, fResponseGroup, fChorusGroup, fGlobalGroup;
    std::unique_ptr<Label>      fOpLabel[kFmOperators];
    std::unique_ptr<Knob>       fKnobs[kParamCount];          // (Ratio entries are RatioKnobs)
    RatioKnob*                  fRatioKnob[kFmOperators] = {}; // views into fKnobs, per operator
    std::unique_ptr<EnvelopeGraph> fOpEnv[kFmOperators], fFiltEnv;
    EnvelopeGraph*              fEnvFor[kParamCount] = {};     // param → the graph that edits it
    std::unique_ptr<Dropdown>   fDropdowns[kParamCount];
    std::unique_ptr<Logo>       fLogo;
    std::unique_ptr<TitleText>  fTitleText;
    std::unique_ptr<Dropdown>   fPresetDropdown;
    std::unique_ptr<HelpButton> fHelpButton;
    WebViewHandle               fHelpWebView = nullptr;
    int                         fTopPad = 0; // set each layout() pass; read by the help-panel geometry
    std::unique_ptr<PopupMenu>  fMenu;
    // Sentinel dropdown id, distinct from every real ParamIndex, so dropdownClicked()/
    // menuSelected() can tell the preset selector apart from a per-param dropdown.
    static constexpr uint32_t kPresetDropdownId = kParamCount;
#if FEM_PRESET_DUMP
    std::unique_ptr<DumpButton> fDumpBtn;
#endif
    float                       fUiScale = 1.0f; // set each layout() pass
    bool                        fInitialSizeNudged = false; // one-shot size re-assert (see uiIdle)

    /* -- param classification --------------------------------------------- */

    // In-block offset of an FM engine param; false if `i` isn't one.
    static bool fmOffset(uint32_t i, uint32_t& k)
    {
        if (i < fmParam(0) || i >= fmParam(kFmEngineParamCount))
            return false;
        k = i - fmParam(0);
        return true;
    }
    // Operator item (kOpWave..kOpRelease) of an FM operator param; -1 otherwise.
    static int opItem(uint32_t i)
    {
        uint32_t k;
        if (!fmOffset(i, k) || k < kFmOpBase)
            return -1;
        return static_cast<int>((k - kFmOpBase) % kFmOpCount);
    }

    // Envelope times/levels are edited on an EnvelopeGraph, not knobs.
    static bool isEnvelopeParam(uint32_t i)
    {
        const int item = opItem(i);
        if (item == kOpDelay || item == kOpAttack || item == kOpDecay || item == kOpSustain
            || item == kOpRelease)
            return true;
        return i == kParamFiltAttack || i == kParamFiltDecay || i == kParamFiltSustain
            || i == kParamFiltRelease;
    }

    std::unique_ptr<EnvelopeGraph> makeEnvelope(GroupBox* parent, Color accent, uint32_t delay,
                                                 uint32_t attack, uint32_t decay, uint32_t sustain,
                                                 uint32_t release)
    {
        auto env = std::make_unique<EnvelopeGraph>(parent, this);
        env->setAccentColor(accent);
        const auto bind = [&](EnvelopeGraph::Slot slot, uint32_t id) {
            if (id == EnvelopeGraph::kNoParam) return;
            env->setSlot(slot, id, kParams[id].min, kParams[id].max, kParams[id].def);
            fEnvFor[id] = env.get();
        };
        bind(EnvelopeGraph::kDelay, delay);
        bind(EnvelopeGraph::kAttack, attack);
        bind(EnvelopeGraph::kDecay, decay);
        bind(EnvelopeGraph::kSustain, sustain);
        bind(EnvelopeGraph::kRelease, release);
        return env;
    }

    static bool isDropdownParam(uint32_t i)
    {
        if (i == kParamVoiceMode || i == kParamBendRange || i == kParamVelFloor || i == kParamVelCurve)
            return true;
        uint32_t k;
        if (!fmOffset(i, k))
            return false;
        return k == kFmAlgorithm || k == kFmOversample || opItem(i) == kOpWave;
    }

    static std::vector<std::string> choicesFor(uint32_t i)
    {
        if (i == kParamVoiceMode) return {"Mono", "Poly"};
        if (i == kParamBendRange)
            return {"Off", "0.5 st", "1 st", "1.5 st", "2 st", "2.5 st", "3 st", "3.5 st", "4 st"};
        if (i == kParamVelFloor) return {"0%", "25%", "50%", "75%", "Fixed"}; // level of the softest note
        if (i == kParamVelCurve) return {"Concave", "Linear", "Convex"};
        uint32_t k = 0;
        fmOffset(i, k);
        if (k == kFmAlgorithm)
        {
            // "12 · out 1,3": algorithm number, then its carriers (the rest are modulators).
            std::vector<std::string> names;
            for (int a = 0; a < kFM6OpAlgorithms; ++a)
            {
                std::string n = std::to_string(a + 1) + " \xC2\xB7 out ";
                for (int o = 0, first = 1; o < kFM6OpCount; ++o)
                    if (kFM6OpAlgos[a].carrierMask & (1u << o))
                    {
                        n += (first ? "" : ",") + std::to_string(o + 1);
                        first = 0;
                    }
                names.push_back(n);
            }
            return names;
        }
        if (k == kFmOversample) return {"Normal", "High", "Best"}; // oversampling Off / 2x / 4x
        return {"Sine", "1+2", "1+3", "1+3+5", "1-4"}; // operator wave (harmonic stacks)
    }

    // Short on-panel label (the host sees the full ParamSpec name, e.g. "A Op2 Ratio").
    static std::string labelFor(uint32_t i)
    {
        switch (opItem(i))
        {
        case kOpWave:    return "Wave";
        case kOpRatio:   return "Ratio";
        case kOpFine:    return "Fine";
        case kOpLevel:   return "Level";
        case kOpDelay:   return "Delay";
        case kOpAttack:  return "Attack";
        case kOpDecay:   return "Decay";
        case kOpSustain: return "Sustain";
        case kOpRelease: return "Release";
        default: break;
        }
        uint32_t k;
        if (fmOffset(i, k))
        {
            switch (k)
            {
            case kFmAlgorithm:  return "Algorithm";
            case kFmFeedback:   return "Feedback";
            case kFmPitch:      return "Pitch";
            case kFmFine:       return "Fine";
            case kFmDetune:     return "Detune";
            case kFmOversample: return "Quality";
            default: break;
            }
        }
        switch (i)
        {
        case kParamFiltAttack:  return "Attack";
        case kParamFiltDecay:   return "Decay";
        case kParamFiltSustain: return "Sustain";
        case kParamFiltRelease: return "Release";
        case kParamVelFloor:    return "Vel Floor";
        case kParamVelCurve:    return "Vel Curve";
        default: return kParams[i].name;
        }
    }

    // Module-category accent colours (PM-inspired).
    static Color colSound()   { return Color(60, 200, 90); }   // oscillators (FM engines) — green
    static Color colFilter()  { return Color(170, 112, 226); } // filter — purple
    static Color colEnv()     { return Color(220, 206, 72); }  // response (velocity) — muted yellow
    static Color colComb()    { return Color(242, 150, 46); }  // global — orange
    static Color colNeutral() { return Color(120, 190, 205); } // chorus — teal

    GroupBox* groupFor(uint32_t i) const
    {
        uint32_t k;
        if (fmOffset(i, k))
        {
            if (k == kFmOversample) return fGlobalGroup.get(); // "Quality": a CPU/sound setting
            return k < kFmOpBase ? fEngineGroup.get() : fOpGroup.get();
        }
        switch (i)
        {
        case kParamLpfCutoff: case kParamLpfResonance: case kParamLpfEnvAmount: case kParamLpfPressure:
            return fLpfGroup.get();
        case kParamFiltAttack: case kParamFiltDecay: case kParamFiltSustain: case kParamFiltRelease:
            return fFiltEnvGroup.get();
        case kParamChorusMix: case kParamChorusRate: case kParamChorusDepth:
        case kParamChorusDelay: case kParamChorusFeedback:
            return fChorusGroup.get();
        case kParamVelFloor: case kParamVelCurve:
            return fResponseGroup.get();
        default:
            return fGlobalGroup.get(); // Voices, Bend Range, Master
        }
    }

    Color accentFor(uint32_t i) const
    {
        const GroupBox* g = groupFor(i);
        if (g == fEngineGroup.get() || g == fOpGroup.get())   return colSound();
        if (g == fLpfGroup.get() || g == fFiltEnvGroup.get()) return colFilter();
        if (g == fResponseGroup.get())                        return colEnv();
        if (g == fChorusGroup.get())                          return colNeutral();
        return colComb();
    }

    static std::function<std::string(float)> formatterFor(uint32_t index)
    {
        const auto fmt = [](const char* f) {
            return [f](float v) { char b[24]; std::snprintf(b, sizeof(b), f, v); return std::string(b); };
        };
        if (index == kParamLpfCutoff) // octaves above A440 → cutoff frequency
            return [](float v) {
                const double f = 440.0 * std::exp2(v);
                char b[16];
                f >= 1000.0 ? std::snprintf(b, sizeof(b), "%.2f kHz", f / 1000.0)
                            : std::snprintf(b, sizeof(b), "%.0f Hz", f);
                return std::string(b);
            };
        if (index == kParamChorusRate) // octaves re 1 Hz → Hz
            return [](float v) { char b[16]; std::snprintf(b, sizeof(b), "%.2f Hz", std::exp2(v)); return std::string(b); };
        if (index == kParamDetune) // 0..1 → the engine's ±25 ct L/R split (FM6Op: ×50 ct total)
            return [](float v) { char b[16]; std::snprintf(b, sizeof(b), "%.1f ct", v * 50.0f); return std::string(b); };
        if (index == kParamChorusDelay)  return fmt("%.1f ms");
        if (index == kParamChorusDepth)  return fmt("%.2f ms");
        if (opItem(index) == kOpRatio)   return fmt("%.2f");
        const std::string u = kParams[index].unit;
        if (u == "st")  return [](float v) { char b[12]; const long s = std::lround(v); std::snprintf(b, sizeof(b), s ? "%+ld st" : "%ld st", s); return std::string(b); };
        if (u == "ct")  return fmt("%.1f ct");
        if (u == "oct") return fmt("%+.1f oct");
        if (u == "s")
            return [](float v) {
                char b[16];
                v < 1.0f ? std::snprintf(b, sizeof(b), "%.0f ms", v * 1000.0f)
                         : std::snprintf(b, sizeof(b), "%.2f s", v);
                return std::string(b);
            };
        return fmt("%.2f");
    }

    // Patch-value mirror for the dev-only JSON dumper: no-op (and no member) in release builds.
#if FEM_PRESET_DUMP
    float fMirror[kParamCount];
    void recordMirror(uint32_t id, float v) { if (id < kParamCount) fMirror[id] = v; }
#else
    void recordMirror(uint32_t, float) {}
#endif

    /* -- help ------------------------------------------------------------- */

    // Help panel: a native WebView overlay covering everything below the top strip, so the
    // always-visible Help button up there can toggle it closed again.
    void helpPanelGeometry(int& x, int& y, int& w, int& h) const
    {
        const int m = 8;
        x = m;
        y = fTopPad + m;
        w = static_cast<int>(getWidth()) - 2 * m;
        h = static_cast<int>(getHeight()) - fTopPad - 2 * m;
    }

    void resizeHelpWebView()
    {
        int x, y, w, h;
        helpPanelGeometry(x, y, w, h);
        webViewResize(fHelpWebView, static_cast<uint>(w), static_cast<uint>(h), fUiScale);
    }

    static std::string helpFilePath()
    {
        const char* tmp = std::getenv("TMPDIR");
        std::string dir = (tmp && *tmp) ? tmp : "/tmp";
        if (!dir.empty() && dir.back() != '/') dir += '/';
        return dir + "fem-help.html";
    }

    // The WebView needs a real file:// URL, not an in-memory buffer, so the embedded HTML bytes
    // get written out here. Overwritten every time the panel opens, so it always matches
    // whatever's actually embedded in this build.
    static std::string writeHelpFile()
    {
        const std::string path = helpFilePath();
        FILE* const f = std::fopen(path.c_str(), "wb");
        if (f == nullptr)
            return "";
        std::fwrite(EmbeddedResources::helpData, 1, EmbeddedResources::helpDataSize, f);
        std::fclose(f);
        return path;
    }

    void toggleHelp()
    {
        if (fHelpWebView != nullptr)
        {
            webViewDestroy(fHelpWebView);
            fHelpWebView = nullptr;
            fHelpButton->setOpen(false);
            return;
        }

        const std::string path = writeHelpFile();
        if (path.empty())
            return;
        const std::string url = "file://" + path;

        int x, y, w, h;
        helpPanelGeometry(x, y, w, h);

        WebViewOptions options;
        options.offset.x = x;
        options.offset.y = y;
        fHelpWebView = webViewCreate(url.c_str(), getWindow().getNativeWindowHandle(),
                                      static_cast<uint>(w), static_cast<uint>(h), fUiScale, options);
        fHelpButton->setOpen(fHelpWebView != nullptr);
    }

    std::unique_ptr<GroupBox> makeGroup(const char* title, Color accent)
    {
        auto g = std::make_unique<GroupBox>(this);
        g->setTitle(title);
        g->setAccentColor(accent);
        return g;
    }

    /* -- layout ----------------------------------------------------------- */

    void layout()
    {
        const int W = static_cast<int>(getWidth());
        const int H = static_cast<int>(getHeight());
        const int m = 8, gap = 8;
        // Window scale: sizes knobs/text/strips so the UI reads the same on a logical-pixel
        // (Linux) or physical-pixel (Retina) canvas. The aspect ratio is locked.
        const float scale = std::fmin(static_cast<float>(W) / DISTRHO_UI_DEFAULT_WIDTH,
                                      static_cast<float>(H) / DISTRHO_UI_DEFAULT_HEIGHT);
        const float txtScale = std::fmax(0.72f, std::fmin(2.0f, scale));
        fUiScale = txtScale;

        // Top strip: logo, wordmark, preset dropdown; help (+ dev DUMP) on the right.
        const int logoH  = static_cast<int>(48 * scale);
        const int topPad = logoH + static_cast<int>(6 * scale);
        fTopPad = topPad;

        // Two columns over one bottom row. Left: FM Engine above the two filter panels (each one
        // knob row high); right: the Operators panel, full height. Bottom: Response | Chorus |
        // Global. Module rows are sized for one knob (label + sphere + value) + the title strip.
        const int totalH = H - 2 * m - gap - topPad;
        const int rowH   = static_cast<int>(130 * scale);
        const int fmH    = totalH - rowH;
        const int colL   = m;
        const int fullW  = W - 2 * m;
        const int fmY    = m + topPad;
        const int row1   = fmY + fmH + gap;
        const int leftW  = static_cast<int>(300 * scale);
        const int opX    = colL + leftW + gap;
        const int engH   = fmH - 2 * (rowH + gap);

        // Knob sizes: module knobs full size, operator knobs smaller so six rows fit.
        const float knobD = std::fmax(30.0f, 54.0f * scale);
        const float opKnobD = std::fmax(22.0f, 36.0f * scale);
        for (uint32_t i = 0; i < kParamCount; ++i)
            if (fKnobs[i]) fKnobs[i]->setDiameter(opItem(i) >= 0 ? opKnobD : knobD, txtScale);

        GroupBox* allGroups[] = { fEngineGroup.get(), fOpGroup.get(), fLpfGroup.get(),
                                  fFiltEnvGroup.get(), fResponseGroup.get(), fChorusGroup.get(),
                                  fGlobalGroup.get() };
        for (GroupBox* g : allGroups)
            g->setUiScale(txtScale);
        for (uint32_t i = 0; i < kParamCount; ++i)
            if (fDropdowns[i]) fDropdowns[i]->setUiScale(txtScale);
        for (auto& l : fOpLabel)
            l->setUiScale(txtScale);
        for (auto& e : fOpEnv)
            e->setUiScale(txtScale);
        fFiltEnv->setUiScale(txtScale);
        fMenu->setUiScale(txtScale);
        fPresetDropdown->setUiScale(txtScale);

        fLogo->setHeightPreservingAspect(static_cast<float>(logoH));
        fLogo->setAbsolutePos(m, m + (topPad - logoH) / 2);
        const int titleX = m + static_cast<int>(fLogo->getWidth()) + gap;
        const int titleW = static_cast<int>(230 * scale);
        fTitleText->setFontSize(48.0f * scale);
        fTitleText->setSize(static_cast<uint>(titleW), static_cast<uint>(topPad));
        fTitleText->setAbsolutePos(titleX, m);
        {
            const int pw = static_cast<int>(220 * scale);
            fPresetDropdown->setSize(static_cast<uint>(pw), static_cast<uint>(topPad));
            fPresetDropdown->setAbsolutePos(titleX + titleW + gap, m / 2);
        }

        // Left column: FM Engine, Low-Pass Filter, Filter Envelope. Right: Operators.
        placeGroup(fEngineGroup.get(),  colL, fmY,                         leftW, engH);
        placeGroup(fLpfGroup.get(),     colL, fmY + engH + gap,            leftW, rowH);
        placeGroup(fFiltEnvGroup.get(), colL, fmY + engH + rowH + 2 * gap, leftW, rowH);
        placeGroup(fOpGroup.get(),      opX,  fmY, colL + fullW - opX, fmH);
        layoutEngine();
        layoutOperators();
        layoutKnobRow(fLpfGroup.get(), {kParamLpfCutoff, kParamLpfResonance, kParamLpfEnvAmount, kParamLpfPressure});
        {
            int cx, cy, cw, ch;
            fFiltEnvGroup->contentBounds(cx, cy, cw, ch);
            place(fFiltEnv.get(), cx, cy, cw, ch);
        }

        // Bottom row: Response (2 dropdowns) | Chorus (5 knobs) | Global (2 dropdowns, Quality,
        // Master), widths roughly by control count.
        {
            const int avail = fullW - 2 * gap;
            const int respW = avail * 6 / 26, chW = avail * 11 / 26;
            int x = colL;
            placeGroup(fResponseGroup.get(), x, row1, respW, rowH); x += respW + gap;
            placeGroup(fChorusGroup.get(),   x, row1, chW,   rowH); x += chW + gap;
            placeGroup(fGlobalGroup.get(),   x, row1, colL + fullW - x, rowH);
            layoutKnobRow(fResponseGroup.get(), {kParamVelFloor, kParamVelCurve});
            layoutKnobRow(fChorusGroup.get(), {kParamChorusMix, kParamChorusRate, kParamChorusDepth,
                                               kParamChorusDelay, kParamChorusFeedback});
            layoutKnobRow(fGlobalGroup.get(), {kParamVoiceMode, kParamBendRange, kParamOversample,
                                               kParamMasterLevel});
        }

        // Help button: far right of the top strip (the dev DUMP button sits just left of it).
        const int helpW = static_cast<int>(34 * scale), helpH = static_cast<int>(28 * scale);
        fHelpButton->setSize(static_cast<uint>(helpW), static_cast<uint>(helpH));
        fHelpButton->setAbsolutePos(W - helpW - m, (topPad - helpH) / 2 + m / 2);
#if FEM_PRESET_DUMP
        if (fDumpBtn)
        {
            const int bw = static_cast<int>(58 * scale), bh = static_cast<int>(18 * scale);
            fDumpBtn->setSize(static_cast<uint>(bw), static_cast<uint>(bh));
            fDumpBtn->setAbsolutePos(W - helpW - gap - bw - m, (topPad - bh) / 2 + m / 2);
        }
#endif
    }

    // FM Engine panel: Algorithm across the top, then Feedback (it belongs to the algorithm: one
    // operator per algorithm feeds back), then the Tuning row — Pitch, Fine, Detune.
    void layoutEngine()
    {
        int cx, cy, cw, ch;
        fEngineGroup->contentBounds(cx, cy, cw, ch);
        const int dropH = static_cast<int>(46 * fUiScale);
        place(fDropdowns[kParamAlgorithm].get(), cx, cy, cw, dropH);
        const int rowH  = (ch - dropH) / 2;
        const int cellW = cw / 3;
        place(fKnobs[kParamFeedback].get(), cx, cy + dropH, cellW, rowH);
        const int tuneY = cy + dropH + rowH;
        place(fKnobs[kParamPitch].get(),  cx,             tuneY, cellW,             ch - dropH - rowH);
        place(fKnobs[kParamFine].get(),   cx + cellW,     tuneY, cellW,             ch - dropH - rowH);
        place(fKnobs[kParamDetune].get(), cx + 2 * cellW, tuneY, cw - 2 * cellW,    ch - dropH - rowH);
    }

    // Operators panel: six rows — role caption, Wave dropdown, Ratio (+ hidden Fine) and Level
    // knobs, then the envelope graph across the rest of the row.
    void layoutOperators()
    {
        int cx, cy, cw, ch;
        fOpGroup->contentBounds(cx, cy, cw, ch);
        const int labelW = static_cast<int>(80 * fUiScale);
        const int cellW  = static_cast<int>(78 * fUiScale);
        const int waveW  = static_cast<int>(cellW * 1.4f);
        const int rowH   = ch / static_cast<int>(kFmOperators);
        const int envPad = static_cast<int>(6 * fUiScale);
        for (uint32_t o = 0; o < kFmOperators; ++o)
        {
            const int y = cy + static_cast<int>(o) * rowH;
            int x = cx;
            place(fOpLabel[o].get(), x, y, labelW, rowH);                   x += labelW;
            place(fDropdowns[fmOpParam(o, kOpWave)].get(), x, y, waveW, rowH); x += waveW;
            place(fKnobs[fmOpParam(o, kOpRatio)].get(), x, y, cellW, rowH);    x += cellW;
            place(fKnobs[fmOpParam(o, kOpLevel)].get(), x, y, cellW, rowH);    x += cellW;
            place(fOpEnv[o].get(), x + envPad, y + envPad, cx + cw - x - envPad, rowH - 2 * envPad);
        }
    }

    // A pad-less panel: the listed controls (knob or dropdown) evenly across the content area.
    void layoutKnobRow(GroupBox* group, std::initializer_list<uint32_t> ids)
    {
        int cx, cy, cw, ch;
        group->contentBounds(cx, cy, cw, ch);
        const int n = static_cast<int>(ids.size());
        const int cellW = cw / n;
        int i = 0;
        for (uint32_t id : ids)
        {
            SubWidget* w = fKnobs[id] ? static_cast<SubWidget*>(fKnobs[id].get())
                                      : static_cast<SubWidget*>(fDropdowns[id].get());
            // Dropdowns only need their label + box height; centre them in the knob-height cell.
            const int dh = fDropdowns[id] ? std::min(ch, static_cast<int>(46 * fUiScale)) : ch;
            place(w, cx + i * cellW, cy + (ch - dh) / 2, cellW, dh);
            ++i;
        }
    }

    static void place(SubWidget* w, int x, int y, int width, int height)
    {
        w->setAbsolutePos(x, y);
        w->setSize(static_cast<uint>(std::max(width, 1)), static_cast<uint>(std::max(height, 1)));
    }

    void placeGroup(GroupBox* g, int x, int y, int w, int h) { place(g, x, y, w, h); }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FeMUI)
};

UI* createUI()
{
    return new FeMUI();
}

END_NAMESPACE_DISTRHO
