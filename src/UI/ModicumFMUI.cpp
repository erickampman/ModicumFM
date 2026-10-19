// ModicumFMUI.cpp — UI side. Under the top strip (logo, wordmark, preset menu, Voice | Operators
// tabs, help). The Operators tab (the default) shows:
//
//   FM Engine (algorithm diagram +  │ Operators: 6 rows (role caption, Wave, Ratio, Level,
//     Feedback; Pitch/Fine/Detune)  │   envelope graph)
//   Low-Pass Filter                 │
//   Filter Envelope (graph)         │
//   Response (velocity) | Chorus | Global (Voices, MPE, Bend Range, Quality, Master)
//
// The Voice tab swaps the Operators panel for the simple layer, Character: the role-based macros
// Brightness, Bite, Grit, Body, and the shared Loudness / Brightness envelopes, which scale every
// carrier's / modulator's envelope and draw a representative one; a Reset button returns all of
// these to neutral. Hovering a macro or envelope rings the operators it acts on in the FM Engine
// diagram. Under Character, Expression: what pressure (Bright / Cutoff / Level) and timbre, CC74
// (Bright / Cutoff) do, as a grid of amounts; beside it, Pitch: Glide (time + Always / Legato; mono
// only, greyed in poly) and Vibrato (rate, depth, the depth the mod wheel adds, and Shared / Per Note
// LFO phase). Every other panel — FM Engine included — shows on both
// tabs.
//
// Every param is a knob unless it's an enum (dropdown). The operator captions show each
// operator's role in the current algorithm (carrier, or modulator N hops from the output) —
// the same role analysis the planned macros use (docs/ROADMAP.md). The UI is the callback for
// all controls, routing by param index. Each operator's Ratio is a RatioKnob, which also edits
// that operator's Fine param (no Fine knob of its own). Envelopes are EnvelopeGraphs (draggable
// points) rather than knobs. The algorithm is shown as a diagram (AlgorithmView); clicking it
// opens a full-window AlgorithmPicker of all 32.

#include "DistrhoUI.hpp"
#include "OpenGL.hpp"     // glClear for the stencil (see onNanoDisplay)

#include "src/DistrhoUIInternal.hpp" // DPF's UIExporter (DPF_ROOT_DIR/distrho is on the include path)

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "AlgorithmDiagram.hpp"
#include "CPPTypes.hpp"         // DEFAULT_SAMPLE_RATE (the UIExporter's nominal rate below)
#include "DistrhoPluginInfo.h"
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
#include "PresetDump.hpp" // dev-only JSON patch dumper (no-op unless MODICUMFM_PRESET_DUMP=1)
#include "ParamEdit.hpp"
#include "PresetKeys.hpp"
#include "RatioKnob.hpp"
#include "TabBar.hpp"
#include "TextButton.hpp"
#include "TitleText.hpp"
#include "ModicumFMParams.h"
#include "FM6OpAlgorithms.hpp" // algorithm table + operator roles (shared with the engine)
#include "plugin_gui.h"         // the C seam to src/plugin.cpp (the raw CLAP plugin)

#include "extra/WebView.hpp" // native WebView overlay (WKWebView/WebKitGTK/WebView2), for Help.
                              // Must be included at global scope (see DPF's EmbedExternalUI
                              // example); WebView.hpp opens the DISTRHO namespace itself.

START_NAMESPACE_DISTRHO

using namespace modicumfm;
using DGL_NAMESPACE::AlgorithmPicker;
using DGL_NAMESPACE::AlgorithmView;
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
using DGL_NAMESPACE::TabBar;
using DGL_NAMESPACE::TextButton;
using DGL_NAMESPACE::TitleText;

#if MODICUMFM_PRESET_DUMP
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

class ModicumFMUI : public UI,
              public Knob::Callback,
              public Dropdown::Callback,
              public PopupMenu::Callback,
              public TabBar::Callback,
              public ParamEditCallback
{
public:
    ModicumFMUI()
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
        // Voice tab panels (same places as FM Engine / Operators; hidden on the Operators tab).
        fCharGroup     = makeGroup("Character", colSound());
        fCharGroup->hide();
        fExprGroup     = makeGroup("Expression", colEnv());   // under Character, Voice tab
        fExprGroup->hide();
        fPitchGroup    = makeGroup("Pitch", colComb());       // right of Expression, Voice tab
        fPitchGroup->hide();

        for (uint32_t o = 0; o < kFmOperators; ++o)
            fOpLabel[o] = std::make_unique<Label>(fOpGroup.get());
        updateOpRoles(static_cast<int>(kParams[kParamAlgorithm].def));

        for (uint32_t i = 0; i < kParamCount; ++i)
        {
            if (opItem(i) == kOpFine || isEnvelopeParam(i) || isSharedEnvParam(i) || i == kParamAlgorithm)
                continue; // edited through a RatioKnob / an EnvelopeGraph / the AlgorithmView
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
                d->setChoices(choicesForWidget(i));
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

        // Algorithm diagram (click → picker, wheel → step).
        fAlgoView = std::make_unique<AlgorithmView>(
            fEngineGroup.get(), [this] { fAlgoPicker->open(fAlgorithm); },
            [this](int d) { chooseAlgorithm(std::clamp(fAlgorithm + d, 0, kFM6OpAlgorithms - 1)); });
        fAlgoView->setAccentColor(colSound());
        fAlgoView->setAlgorithm(fAlgorithm);
        fAlgoView->setFeedbackActive(kParams[kParamFeedback].def > 0.0f);

        // Voice | Operators (opens on Operators).
        fViewTab = std::make_unique<TabBar>(this, this);
        fViewTab->setTabs({"Voice", "Operators"});
        fViewTab->setAccentColor(colSound());
        fViewTab->setActive(kTabOperators);

        // Envelope graphs: one per operator (with Delay) and the filter envelope (without).
        for (uint32_t o = 0; o < kFmOperators; ++o)
            fOpEnv[o] = makeEnvelope(fOpGroup.get(), colSound(), fmOpParam(o, kOpDelay),
                                     fmOpParam(o, kOpAttack), fmOpParam(o, kOpDecay),
                                     fmOpParam(o, kOpSustain), fmOpParam(o, kOpRelease));
        fFiltEnv = makeEnvelope(fFiltEnvGroup.get(), colFilter(), EnvelopeGraph::kNoParam,
                                kParamFiltAttack, kParamFiltDecay, kParamFiltSustain,
                                kParamFiltRelease);

        // Voice tab: shared (relative) envelopes + Reset.
        fLoudEnv = makeEnvelope(fCharGroup.get(), colSound(), EnvelopeGraph::kNoParam, kParamLoudAttack,
                                kParamLoudDecay, kParamLoudSustain, kParamLoudRelease);
        fBrightEnv = makeEnvelope(fCharGroup.get(), colSound(), EnvelopeGraph::kNoParam, kParamBrightAttack,
                                  kParamBrightDecay, kParamBrightSustain, kParamBrightRelease);
        fLoudEnv->setRelative(true);
        fBrightEnv->setRelative(true);
        fExprPressLabel = std::make_unique<Label>(fExprGroup.get(), "Pressure");
        fExprTmbLabel   = std::make_unique<Label>(fExprGroup.get(), "Timbre (CC 74)");
        fGlideLabel     = std::make_unique<Label>(fPitchGroup.get(), "Glide");
        fVibLabel       = std::make_unique<Label>(fPitchGroup.get(), "Vibrato");
        fLoudLabel   = std::make_unique<Label>(fCharGroup.get(), "Loudness envelope  (all carriers)");
        fBrightLabel = std::make_unique<Label>(fCharGroup.get(), "Brightness envelope  (all modulators)");
        fCharReset = std::make_unique<TextButton>(fCharGroup.get(), "Reset", [this] { resetCharacter(); });
        fCharReset->setAccentColor(colSound());
        updateSharedEnvBases();

#if MODICUMFM_PRESET_DUMP
        fDumpBtn = std::make_unique<DumpButton>(this, [this] {
            const std::string p = dumpPresetToFile(fMirror);
            std::fprintf(stderr, "[ModicumFM] patch %s %s\n",
                         p.empty() ? "dump FAILED" : "dumped to", p.c_str());
        });
#endif
        // Logo: raw PNG bytes embedded at build time (see EmbeddedResources.hpp).
        fLogo = std::make_unique<Logo>(this, EmbeddedResources::Unlikelyware_Final_ColorData,
                                        EmbeddedResources::Unlikelyware_Final_ColorDataSize);

        // Wordmark: "Modicum" + "FM" in two accent colours sampled from the logo, Almendra (embedded,
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

        // Overlays, created last so they draw (and take events) on top.
        fAlgoPicker = std::make_unique<AlgorithmPicker>(this, [this](int a) { chooseAlgorithm(a); });
        fAlgoPicker->setAccentColor(colSound());
        fMenu = std::make_unique<PopupMenu>(this, this);

#if MODICUMFM_PRESET_DUMP
        for (uint32_t i = 0; i < kParamCount; ++i) fMirror[i] = kParams[i].def;
#endif
        // DPF is only the GUI layer here: UIExporter set uiData->dspPtr (our clap_plugin_t) before
        // calling createUI(), so the seam in plugin_gui.h is already usable. Replaying every value
        // through parameterChanged() syncs the knobs, dropdowns, graphs and the dump mirror, so an
        // editor opened on a host-restored patch starts in sync instead of showing the defaults.
        fPlugin = static_cast<const clap_plugin_t*>(getPluginInstancePointer());
        if (fPlugin != nullptr)
        {
            for (uint32_t i = 0; i < kParamCount; ++i)
                parameterChanged(i, static_cast<float>(mfm_param_value(fPlugin, i)));
        }

        updateGlideEnabled();
        layout();
    }

    ~ModicumFMUI() override
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
        if (fEnvFor[index])
        {
            fEnvFor[index]->setParamValue(index, value);
            if (opItem(index) >= 0) updateSharedEnvBases(); // an operator envelope feeds the shared graphs
            return;
        }
        if (fKnobs[index])     { fKnobs[index]->setRealValue(value, false); fKnobs[index]->repaint(); }
        if (fDropdowns[index]) fDropdowns[index]->setIndex(static_cast<int>(value));
        if (index == kParamVoiceMode) updateGlideEnabled();
        if (index == kParamAlgorithm) showAlgorithm(static_cast<int>(value));
        if (index == kParamFeedback)  showFeedbackActive(value > 0.0f);
        if (opItem(index) >= 0)       updateSharedEnvBases(); // representative envelopes may change
        if (opItem(index) == kOpRatio) updateMacroHighlight(); // Grit's target can move
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
        if (id == kParamFeedback) showFeedbackActive(v > 0.0f);
        if (opItem(id) == kOpLevel) updateSharedEnvBases();
    }

    /* -- composite widgets -> host (RatioKnob) ---------------------------- */
    void paramEditBegin(uint32_t id) override { editParameter(id, true); }
    void paramEditEnd(uint32_t id) override   { editParameter(id, false); }
    void paramEditChange(uint32_t id, float v) override
    {
        setParameterValue(id, v);
        recordMirror(id, v);
        if (opItem(id) >= 0) updateSharedEnvBases();
    }

    /* -- dropdown + menu -------------------------------------------------- */
    void dropdownClicked(Dropdown* d) override
    {
        int x, y, w, h;
        d->boxRect(x, y, w, h); // y = box top, h = box height (window-absolute)
        fMenu->openFor(d->getId(), d->choices(), d->index(), x, y + h, y, w,
                       static_cast<int>(getHeight()));
    }
    // A click outside the plugin window never reaches us, so treat losing focus to the host (or
    // another app) as "clicked elsewhere" and close any open menu. Clicks inside the window are
    // handled by the menu itself.
    void uiFocus(bool focus, DGL_NAMESPACE::CrossingMode mode) override
    {
        UI::uiFocus(focus, mode);
        if (!focus && fMenu != nullptr)
            fMenu->dismiss();
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
        if (id == kParamVoiceMode) updateGlideEnabled();
    }

    // Glide only does anything in Mono (index 0 of the Voices dropdown); grey it out otherwise.
    void updateGlideEnabled()
    {
        const bool mono = fDropdowns[kParamVoiceMode] && fDropdowns[kParamVoiceMode]->index() == 0;
        if (fKnobs[kParamGlide])         fKnobs[kParamGlide]->setEnabled(mono);
        if (fDropdowns[kParamGlideMode]) fDropdowns[kParamGlideMode]->setEnabled(mono);
    }

    // Algorithm chosen in the UI (picker or wheel) → host, as one gesture.
    void chooseAlgorithm(int a)
    {
        if (a == fAlgorithm) return;
        editParameter(kParamAlgorithm, true);
        setParameterValue(kParamAlgorithm, static_cast<float>(a));
        editParameter(kParamAlgorithm, false);
        recordMirror(kParamAlgorithm, static_cast<float>(a));
        showAlgorithm(a);
    }
    void showAlgorithm(int a)
    {
        fAlgorithm = std::clamp(a, 0, kFM6OpAlgorithms - 1);
        fAlgoView->setAlgorithm(fAlgorithm);
        updateOpRoles(fAlgorithm);
        updateMacroHighlight();
        updateSharedEnvBases();
    }
    void showFeedbackActive(bool on)
    {
        fAlgoView->setFeedbackActive(on);
    }

    /* -- Voice | Operators tabs --------------------------------------------- */
    static constexpr int kTabVoice = 0, kTabOperators = 1;
    void tabSelected(TabBar*, int index) override
    {
        const bool voice = index == kTabVoice;
        fOpGroup->setVisible(!voice);
        fCharGroup->setVisible(voice);
        fExprGroup->setVisible(voice);
        fPitchGroup->setVisible(voice);
        fHoverMacro = -1;
        updateMacroHighlight();
        repaint();
    }

    // Hovering a Character macro / envelope rings, in the FM Engine diagram, the operators it acts on.
    bool onMotion(const MotionEvent& ev) override
    {
        const bool handled = UI::onMotion(ev);
        int hover = -1;
        const Point<int> at(static_cast<int>(ev.pos.getX()), static_cast<int>(ev.pos.getY()));
        if (fCharGroup->isVisible())
        {
            for (uint32_t id : {kParamBrightness, kParamBite, kParamGrit, kParamBody})
                if (fKnobs[id]->getAbsoluteArea().contains(at))
                    hover = static_cast<int>(id);
            if (fLoudEnv->getAbsoluteArea().contains(at))   hover = kParamLoudAttack;
            if (fBrightEnv->getAbsoluteArea().contains(at)) hover = kParamBrightAttack;
        }
        if (hover != fHoverMacro) { fHoverMacro = hover; updateMacroHighlight(); }
        return handled;
    }

    // Operators a macro acts on, by role in the current algorithm (mirrors FM6OpModule's fmMacros).
    uint8_t macroTargets(int macro) const
    {
        const FM6OpRoles roles = fm6OpRoles(fAlgorithm);
        uint8_t carriers = 0, modulators = 0;
        int top = -1;
        float topRatio = -1.0f;
        for (int o = 0; o < kFM6OpCount; ++o)
        {
            if (roles.isCarrier(o)) { carriers |= 1u << o; continue; }
            modulators |= 1u << o;
            const float r = fRatioKnob[o]->getRealValue();
            if (r > topRatio) { topRatio = r; top = o; }
        }
        switch (macro)
        {
        case kParamBrightness: case kParamBite: case kParamBrightAttack: return modulators;
        case kParamBody: case kParamLoudAttack:                          return carriers;
        case kParamGrit:
        {
            const FM6OpAlgo& al = kFM6OpAlgos[fAlgorithm];
            uint8_t m = static_cast<uint8_t>((1u << al.fbDst) | (1u << al.fbSrc));
            if (top >= 0) m |= 1u << top;
            return m;
        }
        default: return 0;
        }
    }
    // The shared envelopes draw a representative operator's envelope (scaled): the loudest
    // carrier for Loudness, the strongest modulator for Brightness (lowest-numbered on ties).
    void updateSharedEnvBases()
    {
        if (!fLoudEnv) return;
        const FM6OpRoles roles = fm6OpRoles(fAlgorithm);
        int car = -1, mod = -1;
        float carL = -1.0f, modL = -1.0f;
        for (int o = 0; o < kFM6OpCount; ++o)
        {
            const float l = fKnobs[fmOpParam(o, kOpLevel)]->getRealValue();
            if (roles.isCarrier(o)) { if (l > carL) { carL = l; car = o; } }
            else                    { if (l > modL) { modL = l; mod = o; } }
        }
        const auto feed = [&](EnvelopeGraph* g, int o) {
            const EnvelopeGraph* src = fOpEnv[o].get();
            g->setBase(src->value(EnvelopeGraph::kAttack), src->value(EnvelopeGraph::kDecay),
                       src->value(EnvelopeGraph::kSustain), src->value(EnvelopeGraph::kRelease));
        };
        feed(fLoudEnv.get(), car);
        if (mod >= 0) { feed(fBrightEnv.get(), mod); fBrightEnv->setDisabled(false); }
        else          { fBrightEnv->setDisabled(true, "no modulators in this algorithm"); }
    }

    // Reset: every Character macro and shared-envelope param back to its neutral default, each as
    // its own begin/change/end edit.
    void resetCharacter()
    {
        for (uint32_t id : {kParamBrightness, kParamBite, kParamGrit, kParamBody,
                            kParamLoudAttack, kParamLoudDecay, kParamLoudSustain, kParamLoudRelease,
                            kParamBrightAttack, kParamBrightDecay, kParamBrightSustain, kParamBrightRelease})
        {
            const float def = kParams[id].def;
            editParameter(id, true);
            setParameterValue(id, def);
            editParameter(id, false);
            parameterChanged(id, def);
        }
    }

    void updateMacroHighlight()
    {
        fAlgoView->setHighlight(fHoverMacro >= 0 ? macroTargets(fHoverMacro) : 0);
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
        // NanoVG fills concave paths (the envelope graphs) through the stencil buffer and assumes it
        // starts at zero, but DPF clears only colour and depth each frame (puglOnDisplayPrepare). A
        // never-drawn region then holds garbage, and its first fill paints inverted bands (seen on
        // the first switch to the Voice tab). NanoVG defers its GL work to the end of the frame, so
        // clearing here still precedes every fill.
        glStencilMask(0xff);
        glClearStencil(0);
        glClear(GL_STENCIL_BUFFER_BIT);
        beginPath(); rect(0, 0, getWidth(), getHeight()); fillColor(Color(28, 20, 34)); fill();
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
#ifndef __APPLE__
        // Not on macOS: setSize() takes pixels, so on a Retina screen this would shrink the editor
        // to half its points inside the host's container. There the GUI shell's applySize() owns
        // the size, from the window's live backing scale.
        if (!fInitialSizeNudged)
        {
            fInitialSizeNudged = true;
            setSize(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT);
        }
#endif
        if (fHelpWebView != nullptr)
            webViewIdle(fHelpWebView);
    }

private:
    // Panels declared before their child controls so the children are destroyed first.
    std::unique_ptr<GroupBox>   fEngineGroup, fOpGroup;
    std::unique_ptr<GroupBox>   fLpfGroup, fFiltEnvGroup, fResponseGroup, fChorusGroup, fGlobalGroup;
    std::unique_ptr<GroupBox>   fCharGroup;                   // Voice tab
    std::unique_ptr<GroupBox>   fExprGroup;                   // Voice tab, under Character
    std::unique_ptr<GroupBox>   fPitchGroup;                  // Voice tab, right of Expression
    std::unique_ptr<Label>      fOpLabel[kFmOperators];
    std::unique_ptr<Knob>       fKnobs[kParamCount];          // (Ratio entries are RatioKnobs)
    RatioKnob*                  fRatioKnob[kFmOperators] = {}; // views into fKnobs, per operator
    std::unique_ptr<EnvelopeGraph> fOpEnv[kFmOperators], fFiltEnv;
    std::unique_ptr<AlgorithmView>   fAlgoView;
    std::unique_ptr<AlgorithmPicker> fAlgoPicker;
    std::unique_ptr<TabBar>     fViewTab;
    std::unique_ptr<EnvelopeGraph> fLoudEnv, fBrightEnv;   // Voice tab: shared envelopes
    std::unique_ptr<Label>      fLoudLabel, fBrightLabel;
    std::unique_ptr<Label>      fExprPressLabel, fExprTmbLabel;
    std::unique_ptr<Label>      fGlideLabel, fVibLabel;       // Pitch panel row captions
    std::unique_ptr<TextButton> fCharReset;
    int                         fHoverMacro = -1;     // macro param under the mouse, or -1
    int                         fAlgorithm = static_cast<int>(kParams[kParamAlgorithm].def);
    EnvelopeGraph*              fEnvFor[kParamCount] = {};     // param → the graph that edits it
    std::unique_ptr<Dropdown>   fDropdowns[kParamCount];
    std::unique_ptr<Logo>       fLogo;
    std::unique_ptr<TitleText>  fTitleText;
    std::unique_ptr<Dropdown>   fPresetDropdown;
    const clap_plugin_t*        fPlugin = nullptr;  // the raw CLAP plugin (see plugin_gui.h)
    std::unique_ptr<HelpButton> fHelpButton;
    WebViewHandle               fHelpWebView = nullptr;
    int                         fTopPad = 0; // set each layout() pass; read by the help-panel geometry
    std::unique_ptr<PopupMenu>  fMenu;
    // Sentinel dropdown id, distinct from every real ParamIndex, so dropdownClicked()/
    // menuSelected() can tell the preset selector apart from a per-param dropdown.
    static constexpr uint32_t kPresetDropdownId = kParamCount;
#if MODICUMFM_PRESET_DUMP
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

    static bool isSharedEnvParam(uint32_t i)
    {
        return i >= kParamLoudAttack && i <= kParamBrightRelease;
    }

    // The label lists live in ModicumFMParams.h so the CLAP params extension reports exactly the
    // same enums; this just adapts them to the widget's std::vector<std::string> shape.
    static std::vector<std::string> choicesForWidget(uint32_t i)
    {
        std::vector<std::string> v;
        for (const char* const* c = modicumfm::choicesFor(i); c != nullptr && *c != nullptr; ++c)
            v.emplace_back(*c);
        return v;
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
        // Expression grid: rows are the source (Pressure / Timbre), so knobs name only the target.
        case kParamPressBright: case kParamTmbBright:  return "Bright";
        case kParamLpfPressure: case kParamTmbCutoff:  return "Cutoff";
        case kParamPressLevel:                         return "Level";
        // Pitch panel: rows are Glide / Vibrato.
        case kParamGlide:     return "Time";
        case kParamGlideMode: return "Mode";
        case kParamVibRate:   return "Rate";
        case kParamVibDepth:  return "Depth";
        case kParamVibWheel:  return "Wheel";
        case kParamVibSync:   return "Sync";
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
        case kParamLpfCutoff: case kParamLpfResonance: case kParamLpfEnvAmount:
            return fLpfGroup.get();
        case kParamLpfPressure: case kParamPressBright: case kParamTmbBright: case kParamTmbCutoff:
        case kParamPressLevel:
            return fExprGroup.get();
        case kParamFiltAttack: case kParamFiltDecay: case kParamFiltSustain: case kParamFiltRelease:
            return fFiltEnvGroup.get();
        case kParamChorusMix: case kParamChorusRate: case kParamChorusDepth:
        case kParamChorusDelay: case kParamChorusFeedback:
            return fChorusGroup.get();
        case kParamVelFloor: case kParamVelCurve: case kParamTouch: case kParamKeyTrack:
            return fResponseGroup.get();
        case kParamBrightness: case kParamBite: case kParamGrit: case kParamBody:
            return fCharGroup.get();
        case kParamGlide: case kParamGlideMode: case kParamVibRate: case kParamVibDepth:
        case kParamVibWheel: case kParamVibSync:
            return fPitchGroup.get();
        default:
            return fGlobalGroup.get(); // Voices, MPE, Bend Range, Master
        }
    }

    Color accentFor(uint32_t i) const
    {
        const GroupBox* g = groupFor(i);
        if (g == fEngineGroup.get() || g == fOpGroup.get() || g == fCharGroup.get()) return colSound();
        if (g == fLpfGroup.get() || g == fFiltEnvGroup.get()) return colFilter();
        if (g == fResponseGroup.get() || g == fExprGroup.get()) return colEnv();
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
        if (index == kParamBrightness || index == kParamBody || index == kParamKeyTrack) // bipolar: signed, 0 = neutral
            return [](float v) { char b[12]; std::snprintf(b, sizeof(b), std::fabs(v) < 0.005f ? "0.00" : "%+.2f", v); return std::string(b); };
        if (index == kParamGlide) // seconds per octave; the minimum is off
            return [](float v) {
                char b[16];
                if (v <= kParams[kParamGlide].min * 1.001f) return std::string("Off");
                v < 1.0f ? std::snprintf(b, sizeof(b), "%.0f ms", v * 1000.0f)
                         : std::snprintf(b, sizeof(b), "%.2f s", v);
                return std::string(b);
            };
        if (index == kParamVibRate)      return fmt("%.2f Hz");
        if (index == kParamVibDepth || index == kParamVibWheel) return fmt("%.0f ct");
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
#if MODICUMFM_PRESET_DUMP
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
        return dir + "modicumfm-help.html";
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
        // Global. Module rows are sized for one knob (label + sphere + value) + the title strip; the
        // bottom row is taller, for two dropdowns stacked in one column.
        const int totalH = H - 2 * m - gap - topPad;
        const int rowH   = static_cast<int>(130 * scale);
        const int botH   = static_cast<int>(150 * scale);
        const int fmH    = totalH - botH;
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
        const float macroKnobD = std::fmax(40.0f, 60.0f * scale);
        for (uint32_t i = 0; i < kParamCount; ++i)
            if (fKnobs[i]) fKnobs[i]->setDiameter(opItem(i) >= 0 ? opKnobD
                                                  : (groupFor(i) == fCharGroup.get() ? macroKnobD : knobD),
                                                  txtScale);

        GroupBox* allGroups[] = { fEngineGroup.get(), fOpGroup.get(), fCharGroup.get(), fLpfGroup.get(),
                                  fFiltEnvGroup.get(), fResponseGroup.get(), fChorusGroup.get(),
                                  fGlobalGroup.get(), fExprGroup.get(), fPitchGroup.get() };
        for (GroupBox* g : allGroups)
            g->setUiScale(txtScale);
        for (uint32_t i = 0; i < kParamCount; ++i)
            if (fDropdowns[i]) fDropdowns[i]->setUiScale(txtScale);
        for (auto& l : fOpLabel)
            l->setUiScale(txtScale);
        for (auto& e : fOpEnv)
            e->setUiScale(txtScale);
        fFiltEnv->setUiScale(txtScale);
        fLoudEnv->setUiScale(txtScale);
        fBrightEnv->setUiScale(txtScale);
        fLoudLabel->setUiScale(txtScale);
        fBrightLabel->setUiScale(txtScale);
        fExprPressLabel->setUiScale(txtScale);
        fExprTmbLabel->setUiScale(txtScale);
        fGlideLabel->setUiScale(txtScale);
        fVibLabel->setUiScale(txtScale);
        fCharReset->setUiScale(txtScale);
        fAlgoView->setUiScale(txtScale);
        fAlgoPicker->setUiScale(txtScale);
        place(fAlgoPicker.get(), 0, 0, W, H);
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
            // Voice | Operators, right of the preset menu, level with its box.
            const int tw = static_cast<int>(200 * scale), th = static_cast<int>(26 * scale);
            fViewTab->setSize(static_cast<uint>(tw), static_cast<uint>(th));
            fViewTab->setAbsolutePos(titleX + titleW + 2 * gap + pw + static_cast<int>(16 * scale),
                                     m / 2 + topPad - th - static_cast<int>(4 * scale));
        }

        // Left column: FM Engine, Low-Pass Filter, Filter Envelope. Right: Operators.
        placeGroup(fEngineGroup.get(),  colL, fmY,                         leftW, engH);
        placeGroup(fLpfGroup.get(),     colL, fmY + engH + gap,            leftW, rowH);
        placeGroup(fFiltEnvGroup.get(), colL, fmY + engH + rowH + 2 * gap, leftW, rowH);
        placeGroup(fOpGroup.get(),      opX,  fmY, colL + fullW - opX, fmH);
        layoutEngine();
        layoutOperators();
        // Voice tab: Character where Operators is; under it Expression and Pitch (two rows each).
        const int exprH  = static_cast<int>(230 * scale);
        const int charH  = fmH - exprH - gap;
        const int rightW = colL + fullW - opX;
        const int pitchW = static_cast<int>(405 * scale);
        const int exprW  = rightW - pitchW - gap;
        placeGroup(fCharGroup.get(),  opX,  fmY, rightW, charH);
        placeGroup(fExprGroup.get(),  opX,  fmY + charH + gap, exprW, exprH);
        placeGroup(fPitchGroup.get(), opX + exprW + gap, fmY + charH + gap, pitchW, exprH);
        {
            // As Expression: a caption column, then Glide (Time, Mode) over Vibrato (Rate, Depth, Wheel,
            // Sync). A dropdown takes 1.3 knob cells.
            int cx, cy, cw, ch;
            fPitchGroup->contentBounds(cx, cy, cw, ch);
            const int capW  = static_cast<int>(60 * scale);
            const int cellW = std::min(static_cast<int>(110 * scale), static_cast<int>((cw - capW) / 4.3f));
            const int dropW = cw - capW - 3 * cellW;
            const int rowH2 = ch / 2;
            const int labH  = static_cast<int>(18 * scale);
            place(fGlideLabel.get(), cx, cy + (rowH2 - labH) / 2,         capW, labH);
            place(fVibLabel.get(),   cx, cy + rowH2 + (rowH2 - labH) / 2, capW, labH);
            place(fKnobs[kParamGlide].get(), cx + capW, cy, cellW, rowH2);
            place(fDropdowns[kParamGlideMode].get(), cx + capW + cellW, cy, dropW, rowH2);
            int col = 0;
            for (uint32_t id : {kParamVibRate, kParamVibDepth, kParamVibWheel})
                place(fKnobs[id].get(), cx + capW + (col++) * cellW, cy + rowH2, cellW, rowH2);
            place(fDropdowns[kParamVibSync].get(), cx + capW + 3 * cellW, cy + rowH2, dropW, rowH2);
        }
        {
            // Rows = source, columns = target: Pressure → Bright / Cutoff / Level, Timbre → Bright /
            // Cutoff. A caption column on the left; knob cells sized as in a module row.
            int cx, cy, cw, ch;
            fExprGroup->contentBounds(cx, cy, cw, ch);
            const int capW  = static_cast<int>(110 * scale);
            const int cellW = std::min(static_cast<int>(110 * scale), (cw - capW) / 3);
            const int rowH2 = ch / 2;
            const int labH  = static_cast<int>(18 * scale);
            place(fExprPressLabel.get(), cx, cy + (rowH2 - labH) / 2,         capW, labH);
            place(fExprTmbLabel.get(),   cx, cy + rowH2 + (rowH2 - labH) / 2, capW, labH);
            int col = 0;
            for (uint32_t id : {kParamPressBright, kParamLpfPressure, kParamPressLevel})
                place(fKnobs[id].get(), cx + capW + (col++) * cellW, cy, cellW, rowH2);
            col = 0;
            for (uint32_t id : {kParamTmbBright, kParamTmbCutoff})
                place(fKnobs[id].get(), cx + capW + (col++) * cellW, cy + rowH2, cellW, rowH2);
        }
        {
            int cx, cy, cw, ch;
            fCharGroup->contentBounds(cx, cy, cw, ch);
            const int rowH = std::min(ch, static_cast<int>(115 * scale));
            const int cellW = cw / 4;
            int i = 0;
            for (uint32_t id : {kParamBrightness, kParamBite, kParamGrit, kParamBody})
                place(fKnobs[id].get(), cx + (i++) * cellW, cy + static_cast<int>(10 * scale), cellW, rowH);
            // Reset: top-right corner of the panel's title strip.
            const int rbW = static_cast<int>(64 * scale), rbH = static_cast<int>(20 * scale);
            place(fCharReset.get(), opX + (colL + fullW - opX) - rbW - static_cast<int>(8 * scale),
                  fmY + static_cast<int>(4 * scale), rbW, rbH);
            // Shared envelopes, side by side under the knobs, each with a caption.
            const int gap2  = static_cast<int>(14 * scale);
            const int labH  = static_cast<int>(18 * scale);
            const int envY  = cy + static_cast<int>(10 * scale) + rowH + static_cast<int>(24 * scale);
            const int envH  = std::max(60, std::min(cy + ch - envY - labH - static_cast<int>(8 * scale),
                                                    static_cast<int>(150 * scale)));
            const int envW  = (cw - gap2) / 2;
            place(fLoudLabel.get(),   cx,                envY, envW, labH);
            place(fBrightLabel.get(), cx + envW + gap2,  envY, envW, labH);
            place(fLoudEnv.get(),     cx,                envY + labH, envW, envH);
            place(fBrightEnv.get(),   cx + envW + gap2,  envY + labH, envW, envH);
        }
        layoutKnobRow(fLpfGroup.get(), {kParamLpfCutoff, kParamLpfResonance, kParamLpfEnvAmount});
        {
            int cx, cy, cw, ch;
            fFiltEnvGroup->contentBounds(cx, cy, cw, ch);
            place(fFiltEnv.get(), cx, cy, cw, ch);
        }

        // Bottom row: Response (Vel Floor over Vel Curve, Touch, Key Track) | Chorus (5 knobs) |
        // Global (Voices over MPE, Bend Range over Quality, Master), widths roughly by control count.
        {
            const int avail = fullW - 2 * gap;
            const int respW = avail / 3, chW = avail / 3;
            int x = colL;
            placeGroup(fResponseGroup.get(), x, row1, respW, botH); x += respW + gap;
            placeGroup(fChorusGroup.get(),   x, row1, chW,   botH); x += chW + gap;
            placeGroup(fGlobalGroup.get(),   x, row1, colL + fullW - x, botH);
            layoutColumns(fResponseGroup.get(), {{kParamVelFloor, kParamVelCurve}, {kParamTouch}, {kParamKeyTrack}});
            layoutKnobRow(fChorusGroup.get(), {kParamChorusMix, kParamChorusRate, kParamChorusDepth,
                                               kParamChorusDelay, kParamChorusFeedback});
            layoutColumns(fGlobalGroup.get(), {{kParamVoiceMode, kParamMpe}, {kParamBendRange, kParamOversample},
                                               {kParamMasterLevel}});
        }

        // Help button: far right of the top strip (the dev DUMP button sits just left of it).
        const int helpW = static_cast<int>(34 * scale), helpH = static_cast<int>(28 * scale);
        fHelpButton->setSize(static_cast<uint>(helpW), static_cast<uint>(helpH));
        fHelpButton->setAbsolutePos(W - helpW - m, (topPad - helpH) / 2 + m / 2);
#if MODICUMFM_PRESET_DUMP
        if (fDumpBtn)
        {
            const int bw = static_cast<int>(58 * scale), bh = static_cast<int>(18 * scale);
            fDumpBtn->setSize(static_cast<uint>(bw), static_cast<uint>(bh));
            fDumpBtn->setAbsolutePos(W - helpW - gap - bw - m, (topPad - bh) / 2 + m / 2);
        }
#endif
    }

    // FM Engine panel: the algorithm diagram with Feedback beside it (the algorithm decides which
    // operator feeds back, and the diagram draws that loop), then the Tuning row — Pitch, Fine,
    // Detune.
    void layoutEngine()
    {
        int cx, cy, cw, ch;
        fEngineGroup->contentBounds(cx, cy, cw, ch);
        const int topH = static_cast<int>(ch * 0.58f);
        const int fbW  = static_cast<int>(84 * fUiScale);
        const int gap  = static_cast<int>(6 * fUiScale);
        place(fAlgoView.get(), cx, cy + gap / 2, cw - fbW - gap, topH - gap);
        place(fKnobs[kParamFeedback].get(), cx + cw - fbW, cy, fbW, topH);
        const int tuneY = cy + topH, tuneH = ch - topH, cellW = cw / 3;
        place(fKnobs[kParamPitch].get(),  cx,             tuneY, cellW,          tuneH);
        place(fKnobs[kParamFine].get(),   cx + cellW,     tuneY, cellW,          tuneH);
        place(fKnobs[kParamDetune].get(), cx + 2 * cellW, tuneY, cw - 2 * cellW, tuneH);
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

    // A pad-less panel: the listed controls (knob or dropdown) across the content area. Dropdowns
    // get 1.3× a knob's width so their text clears the chevron.
    void layoutKnobRow(GroupBox* group, std::initializer_list<uint32_t> ids)
    {
        int cx, cy, cw, ch;
        group->contentBounds(cx, cy, cw, ch);
        constexpr float kDropWeight = 1.3f;
        float units = 0.0f;
        for (uint32_t id : ids) units += fDropdowns[id] ? kDropWeight : 1.0f;
        float x = static_cast<float>(cx);
        for (uint32_t id : ids)
        {
            SubWidget* w = fKnobs[id] ? static_cast<SubWidget*>(fKnobs[id].get())
                                      : static_cast<SubWidget*>(fDropdowns[id].get());
            const float cellW = cw * (fDropdowns[id] ? kDropWeight : 1.0f) / units;
            // Dropdowns only need their label + box height; centre them in the knob-height cell.
            const int dh = fDropdowns[id] ? std::min(ch, static_cast<int>(46 * fUiScale)) : ch;
            place(w, static_cast<int>(x), cy + (ch - dh) / 2, static_cast<int>(cellW), dh);
            x += cellW;
        }
    }

    // Like layoutKnobRow, but each column may stack several dropdowns, one above the other (a knob
    // column holds one knob, full height).
    void layoutColumns(GroupBox* group, std::initializer_list<std::initializer_list<uint32_t>> columns)
    {
        int cx, cy, cw, ch;
        group->contentBounds(cx, cy, cw, ch);
        constexpr float kDropWeight = 1.3f;
        const auto weight = [this](std::initializer_list<uint32_t> col) {
            return fDropdowns[*col.begin()] ? kDropWeight : 1.0f;
        };
        float units = 0.0f;
        for (const auto& col : columns) units += weight(col);
        const int dropH = static_cast<int>(46 * fUiScale);
        const int vgap  = static_cast<int>(4 * fUiScale);
        float x = static_cast<float>(cx);
        for (const auto& col : columns)
        {
            const float cellW = cw * weight(col) / units;
            if (fKnobs[*col.begin()])
            {
                place(fKnobs[*col.begin()].get(), static_cast<int>(x), cy, static_cast<int>(cellW), ch);
            }
            else
            {
                const int n = static_cast<int>(col.size());
                const int dh = std::min(dropH, (ch - (n - 1) * vgap) / n);
                int y = cy + (ch - n * dh - (n - 1) * vgap) / 2;
                for (uint32_t id : col)
                {
                    place(fDropdowns[id].get(), static_cast<int>(x), y, static_cast<int>(cellW), dh);
                    y += dh + vgap;
                }
            }
            x += cellW;
        }
    }

    static void place(SubWidget* w, int x, int y, int width, int height)
    {
        w->setAbsolutePos(x, y);
        w->setSize(static_cast<uint>(std::max(width, 1)), static_cast<uint>(std::max(height, 1)));
    }

    void placeGroup(GroupBox* g, int x, int y, int w, int h) { place(g, x, y, w, h); }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ModicumFMUI)
};

UI* createUI()
{
    return new ModicumFMUI();
}

// ================================================================================================
// CLAP `gui` extension
// ================================================================================================
// Modelled on DPF's own ClapUI (DistrhoPluginCLAP.cpp) and IFSynth's UI: gui_create() only
// allocates a holder, because UIExporter cannot be constructed until the host hands us a parent
// window in set_parent(). DPF is used purely as the GUI layer — the plugin itself is the raw CLAP
// plugin in src/plugin.cpp, so parameter traffic crosses the C seam in plugin_gui.h rather than
// DPF's plugin side.

namespace {

struct GuiState {
    const clap_plugin_t* plugin = nullptr;
    std::unique_ptr<UIExporter> exporter;
    uintptr_t parentWindow = 0;
    // The editor size in the HOST's units — logical points on macOS (CLAP cocoa), pixels on X11.
    // This, not the UIExporter's pixel size, is the source of truth: the pixel size is derived from
    // it with the live scale (applySize), so it can be re-derived when the window changes screens.
    uint32_t width  = DISTRHO_UI_DEFAULT_WIDTH;
    uint32_t height = DISTRHO_UI_DEFAULT_HEIGHT;
    double appliedScale = 0.0;   // the scale the UIExporter's pixel size was last derived with
    bool timerRegistered = false;
    clap_id timerId = 0;
};

#ifdef __APPLE__
extern "C" double mfm_gui_backing_scale(uintptr_t nsView);   // BackingScale_mac.mm
#endif

// Host units → UIExporter pixels. On macOS this is the backing scale of the window the editor is in
// RIGHT NOW, deliberately not UIExporter::getScaleFactor(): DPF caches that once, possibly from the
// wrong screen, and never updates it, while pugl maps mouse positions with the live value — see
// BackingScale_mac.mm. On X11, CLAP sizes are already physical pixels, so there is nothing to convert
// (matching DPF's own CLAP wrapper, which scales on macOS only).
double liveScale(const GuiState* gui)
{
#ifdef __APPLE__
    return mfm_gui_backing_scale(gui->parentWindow);
#else
    (void)gui;
    return 1.0;
#endif
}

// Size the UIExporter to gui->width x gui->height host units at the live scale. pugl re-reads the
// screen's scale while doing this and re-sends the pixel size, so it also re-syncs DPF after a move
// between screens even when the size in points did not change.
void applySize(GuiState* gui)
{
    if (!gui->exporter) return;
    const double scale = liveScale(gui);
    gui->appliedScale = scale;
    gui->exporter->setWindowSizeFromHost(uint(std::lround(gui->width  * scale)),
                                         uint(std::lround(gui->height * scale)));
}

#ifdef __APPLE__
constexpr const char* kWindowApi = CLAP_WINDOW_API_COCOA;
#else
constexpr const char* kWindowApi = CLAP_WINDOW_API_X11;
#endif

bool gui_is_api_supported(const clap_plugin_t*, const char* api, bool isFloating)
{
    return !isFloating && std::strcmp(api, kWindowApi) == 0;
}

bool gui_get_preferred_api(const clap_plugin_t*, const char** api, bool* isFloating)
{
    *api = kWindowApi;
    *isFloating = false;
    return true;
}

bool gui_create(const clap_plugin_t* plugin, const char* api, bool isFloating)
{
    if (!gui_is_api_supported(plugin, api, isFloating)) return false;
    auto* gui = new GuiState();
    gui->plugin = plugin;
    mfm_plugin_set_gui(plugin, gui);   // plugin.cpp owns the pointer and hands it back to us
    return true;
}

void gui_destroy(const clap_plugin_t* plugin)
{
    auto* gui = static_cast<GuiState*>(mfm_plugin_get_gui(plugin));
    mfm_plugin_set_gui(plugin, nullptr);
    if (gui == nullptr) return;

    if (gui->timerRegistered) {
        const clap_host_t* host = mfm_plugin_host(plugin);
        const auto* timer = static_cast<const clap_host_timer_support_t*>(
            host->get_extension(host, CLAP_EXT_TIMER_SUPPORT));
        if (timer != nullptr) timer->unregister_timer(host, gui->timerId);
    }
    delete gui;   // the UIExporter is destroyed here, which tears the UI down
}

// The UIExporter's edit/set callbacks. `ptr` is whatever was passed as UIExporter's first argument —
// we pass the clap_plugin_t, so these forward straight into the seam.
void onEditParam(void* ptr, uint32_t index, bool started)
{
    const auto* plugin = static_cast<const clap_plugin_t*>(ptr);
    if (started) mfm_param_begin_gesture(plugin, index);
    else         mfm_param_end_gesture(plugin, index);
}

void onSetParam(void* ptr, uint32_t index, float value)
{
    mfm_param_set_from_gui(static_cast<const clap_plugin_t*>(ptr), index, double(value));
}

bool gui_set_scale(const clap_plugin_t*, double) { return false; }   // logical size API (cocoa): no-op

bool gui_get_size(const clap_plugin_t* plugin, uint32_t* width, uint32_t* height)
{
    auto* gui = static_cast<GuiState*>(mfm_plugin_get_gui(plugin));
    if (gui == nullptr) return false;
    // Host units, straight from the source of truth. Converting the UIExporter's pixel size back
    // with DPF's cached scale is what made the host allocate a container of the wrong size.
    *width  = gui->width;
    *height = gui->height;
    return true;
}

bool gui_can_resize(const clap_plugin_t*) { return true; }

bool gui_get_resize_hints(const clap_plugin_t*, clap_gui_resize_hints_t* hints)
{
    // The UI locks its design ratio via setGeometryConstraints(), so tell the host the same.
    hints->can_resize_horizontally = true;
    hints->can_resize_vertically   = true;
    hints->preserve_aspect_ratio   = true;
    hints->aspect_ratio_width      = DISTRHO_UI_DEFAULT_WIDTH;
    hints->aspect_ratio_height     = DISTRHO_UI_DEFAULT_HEIGHT;
    return true;
}

bool gui_adjust_size(const clap_plugin_t*, uint32_t*, uint32_t*) { return true; }   // accept as-is

bool gui_set_size(const clap_plugin_t* plugin, uint32_t width, uint32_t height)
{
    auto* gui = static_cast<GuiState*>(mfm_plugin_get_gui(plugin));
    if (gui == nullptr) return false;
    gui->width  = width;
    gui->height = height;
    applySize(gui);
    return true;
}

bool gui_set_parent(const clap_plugin_t* plugin, const clap_window_t* window)
{
    auto* gui = static_cast<GuiState*>(mfm_plugin_get_gui(plugin));
    if (gui == nullptr || std::strcmp(window->api, kWindowApi) != 0) return false;

    // cocoa is a void*, x11 is already an integer handle — hence the different casts.
#ifdef __APPLE__
    gui->parentWindow = reinterpret_cast<uintptr_t>(window->cocoa);
#else
    gui->parentWindow = static_cast<uintptr_t>(window->x11);
#endif

    if (!gui->exporter) {
        gui->exporter.reset(new UIExporter(
            const_cast<clap_plugin_t*>(plugin),   // callbacksPtr → the edit/set callbacks above
            gui->parentWindow,                    // winId: the host's embed target
            DEFAULT_SAMPLE_RATE,                  // the UI does not need the real rate
            onEditParam, onSetParam,
            nullptr, nullptr, nullptr, nullptr,   // state / note / size / file callbacks: unused
            nullptr,                              // bundlePath
            const_cast<clap_plugin_t*>(plugin),   // dspPtr → UI::getPluginInstancePointer()
            0.0,                                  // scaleFactor: 0 = auto
            // kTypeClassic, not kTypeAuto: matches DPF's own CLAP wrapper for this embedded case.
            DGL_NAMESPACE::Application::kTypeClassic));
        // The UIExporter starts at DISTRHO_UI_DEFAULT_WIDTH x HEIGHT *pixels*; put it at the host-unit
        // size instead, at the scale of the window it is actually in.
        applySize(gui);
    }
    return true;
}

bool gui_set_transient(const clap_plugin_t*, const clap_window_t*) { return false; }   // embedded only

void gui_suggest_title(const clap_plugin_t*, const char*) {}

bool gui_show(const clap_plugin_t* plugin)
{
    auto* gui = static_cast<GuiState*>(mfm_plugin_get_gui(plugin));
    if (gui == nullptr || !gui->exporter) return false;

    if (!gui->timerRegistered) {
        const clap_host_t* host = mfm_plugin_host(plugin);
        const auto* timer = static_cast<const clap_host_timer_support_t*>(
            host->get_extension(host, CLAP_EXT_TIMER_SUPPORT));
        if (timer != nullptr && timer->register_timer(host, 16 /* ~60fps */, &gui->timerId))
            gui->timerRegistered = true;
    }
    return true;
}

bool gui_hide(const clap_plugin_t* plugin)
{
    auto* gui = static_cast<GuiState*>(mfm_plugin_get_gui(plugin));
    if (gui == nullptr || !gui->exporter) return false;

    if (gui->timerRegistered) {
        const clap_host_t* host = mfm_plugin_host(plugin);
        const auto* timer = static_cast<const clap_host_timer_support_t*>(
            host->get_extension(host, CLAP_EXT_TIMER_SUPPORT));
        if (timer != nullptr) timer->unregister_timer(host, gui->timerId);
        gui->timerRegistered = false;
    }
    return true;
}

const clap_plugin_gui_t kGui = {
    .is_api_supported  = gui_is_api_supported,
    .get_preferred_api = gui_get_preferred_api,
    .create            = gui_create,
    .destroy           = gui_destroy,
    .set_scale         = gui_set_scale,
    .get_size          = gui_get_size,
    .can_resize        = gui_can_resize,
    .get_resize_hints  = gui_get_resize_hints,
    .adjust_size       = gui_adjust_size,
    .set_size          = gui_set_size,
    .set_parent        = gui_set_parent,
    .set_transient     = gui_set_transient,
    .suggest_title     = gui_suggest_title,
    .show              = gui_show,
    .hide              = gui_hide,
};

} // namespace

extern "C" const clap_plugin_gui_t* mfm_gui_extension(void) { return &kGui; }

// Host automation → an open editor. Called on the main thread by plugin.cpp.
extern "C" void mfm_gui_push_param(void* guiPtr, uint32_t index, double value)
{
    auto* gui = static_cast<GuiState*>(guiPtr);
    if (gui != nullptr && gui->exporter)
        gui->exporter->parameterChanged(index, float(value));
}

// CLAP timer-support → pump the DPF/pugl event loop (see this file's header comment).
extern "C" void mfm_gui_on_timer(const clap_plugin_t* plugin, clap_id)
{
    auto* gui = static_cast<GuiState*>(mfm_plugin_get_gui(plugin));
    if (gui == nullptr || !gui->exporter) return;
    // The host window moved to a screen with a different backing scale (or the size was set before
    // it was in a window): re-derive the pixel size so drawing and mouse agree again.
    if (liveScale(gui) != gui->appliedScale) applySize(gui);
    gui->exporter->plugin_idle();
}

END_NAMESPACE_DISTRHO
