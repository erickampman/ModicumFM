// EnvelopeGraph.hpp — a graphical (D)ADSR editor: draws the envelope's shape and lets you drag its
// points instead of turning five knobs. Used for each FM operator (with Delay) and the filter
// envelope (without).
//
// Time axis: one shared axis for all stages, compressed with u = log(1 + t / 10 ms) so a 5 ms
// attack and a 10 s release are both visible, fitted to the widget, with a minimum width per
// stage so every point stays grabbable. A fixed-width plateau stands for the sustain.
//
// Points (drag):
//   Delay    end of the flat start           sideways = delay time   (operators only)
//   Attack   the peak                        sideways = attack time
//   Decay    the corner into the sustain     sideways = decay time, up/down = sustain level
//   Release  the end                         sideways = release time
// Ctrl = fine (1/10 speed), like the knobs. Shift-click or double-click resets the point to its
// default(s). Dragging is relative (pixels → change in compressed time), so the axis re-fitting
// under the mouse as times change never makes a point jump. A readout names the point and its
// value while hovering or dragging. Edits go through ParamEditCallback (begin/change/end per
// param, so hosts record automation like any knob).
//
// Relative mode (the Voice tab's shared Loudness / Brightness envelopes): the bound params are
// MULTIPLIERS on a base envelope (times, effective result capped like the engine's) and an OFFSET on its sustain. The graph draws the
// effective envelope — base × multiplier, sustain base offset toward 1 (+) or 0 (-) — and a drag
// moves the effective point and stores the multiplier / offset that produce it. setBase() feeds
// in the base envelope (a representative operator's). Disabled: greyed out with a message.

#ifndef MODICUMFM_ENVELOPEGRAPH_HPP
#define MODICUMFM_ENVELOPEGRAPH_HPP

#include "NanoVG.hpp"
#include "ParamEdit.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

START_NAMESPACE_DGL

class EnvelopeGraph : public NanoSubWidget
{
public:
    enum Slot { kDelay = 0, kAttack, kDecay, kSustain, kRelease, kSlotCount };
    static constexpr uint32_t kNoParam = 0xffffffffu;

    EnvelopeGraph(Widget* parent, modicumfm::ParamEditCallback* cb)
        : NanoSubWidget(parent), fCb(cb)
    {
        fHasFont = loadSharedResources();
    }

    // Bind a slot to a plugin param (leave kDelay unbound for a plain ADSR).
    void setSlot(Slot s, uint32_t paramId, float min, float max, float def)
    {
        fSlots[s] = {paramId, min, max, def, def};
    }
    // Host / preset update; returns false if `paramId` isn't one of ours.
    bool setParamValue(uint32_t paramId, float v)
    {
        for (auto& sl : fSlots)
            if (sl.id == paramId) { sl.value = clamp(v, sl.min, sl.max); repaint(); return true; }
        return false;
    }
    void setAccentColor(Color c) { fAccent = c; }
    void setUiScale(float s)     { fUiScale = s; }

    // A slot's current param value (for a host reading an operator envelope's settings).
    float value(Slot s) const { return fSlots[s].value; }

    // Relative mode (see the header comment) and the base envelope it scales.
    void setRelative(bool r) { fRelative = r; repaint(); }
    void setBase(float attack, float decay, float sustain, float release)
    {
        fBase[kAttack] = attack; fBase[kDecay] = decay; fBase[kSustain] = sustain; fBase[kRelease] = release;
        repaint();
    }
    // Disabled: greyed out, no interaction, `message` shown instead of a readout.
    void setDisabled(bool d, const std::string& message = {})
    {
        if (d == fDisabled && message == fDisabledMsg) return;
        fDisabled = d; fDisabledMsg = message;
        if (d) { fDragPoint = fHoverPoint = -1; }
        repaint();
    }

protected:
    void onNanoDisplay() override
    {
        const Geometry g = geometry();
        const float w = getWidth(), h = getHeight();

        beginPath(); roundedRect(0.5f, 0.5f, w - 1.0f, h - 1.0f, 4.0f);
        fillColor(Color(20, 22, 27)); fill();
        strokeColor(Color(46, 50, 58)); strokeWidth(1.0f); stroke();

        // Envelope shape: (delay) flat → linear attack → exponential decay → sustain → exp release.
        // Stroked, then re-traced and closed along the baseline for the fill.
        const Color accent = fDisabled ? Color(104, 108, 116) : fAccent;
        traceShape(g);
        strokeColor(withA(accent, fDisabled ? 0.5f : 0.95f)); strokeWidth(1.6f); stroke();
        traceShape(g);
        closePath();
        fillColor(withA(accent, fDisabled ? 0.07f : 0.16f)); fill();
        if (fDisabled)
        {
            if (fHasFont && !fDisabledMsg.empty())
            {
                fontFace(NANOVG_DEJAVU_SANS_TTF); fontSize(12.0f * fUiScale);
                textAlign(ALIGN_CENTER | ALIGN_MIDDLE); fillColor(Color(150, 156, 166));
                text(w * 0.5f, getHeight() * 0.45f, fDisabledMsg.c_str(), nullptr);
            }
            return;
        }

        // Points.
        for (int p = 0; p < kPointCount; ++p)
        {
            if (p == kPtDelay && !hasDelay()) continue;
            float px, py;
            pointPos(g, p, px, py);
            const bool hot = (p == fDragPoint) || (fDragPoint < 0 && p == fHoverPoint);
            const bool idleDelay = p == kPtDelay && display(kDelay) <= 0.0f && !hot;
            beginPath(); circle(px, py, (hot ? 5.0f : 3.6f) * fUiScale);
            fillColor(hot ? Color(244, 247, 250) : (idleDelay ? withA(fAccent, 0.35f) : fAccent));
            fill();
        }

        // Readout for the hovered / dragged point.
        const int shown = fDragPoint >= 0 ? fDragPoint : fHoverPoint;
        if (fHasFont && shown >= 0)
        {
            fontFace(NANOVG_DEJAVU_SANS_TTF);
            fontSize(11.0f * fUiScale);
            textAlign(ALIGN_RIGHT | ALIGN_TOP);
            fillColor(Color(224, 228, 234));
            text(w - 5.0f * fUiScale, 3.0f * fUiScale, readout(shown).c_str(), nullptr);
        }
    }

    bool onMouse(const MouseEvent& ev) override
    {
        if (ev.button != 1 || fDisabled)
            return false;
        if (ev.press)
        {
            if (!contains(ev.pos))
                return false;
            const int p = hitTest(geometry(), ev.pos.getX(), ev.pos.getY());
            if (p < 0)
                return true; // our area, but not on a point
            const bool dbl = p == fLastClickPoint && fLastClickTime > 0 && ev.time > fLastClickTime
                          && ev.time - fLastClickTime <= 300;
            fLastClickTime = ev.time; fLastClickPoint = p;
            if (dbl || (ev.mod & kModifierShift))
            {
                resetPoint(p);
                fLastClickTime = 0;
                return true;
            }
            fDragPoint = p;
            fLastX = ev.pos.getX(); fLastY = ev.pos.getY();
            forEachSlot(p, [&](Slot s) { fCb->paramEditBegin(fSlots[s].id); });
            repaint();
            return true;
        }
        if (fDragPoint >= 0)
        {
            forEachSlot(fDragPoint, [&](Slot s) { fCb->paramEditEnd(fSlots[s].id); });
            fDragPoint = -1;
            repaint();
            return true;
        }
        return false;
    }

    bool onMotion(const MotionEvent& ev) override
    {
        if (fDisabled)
            return false;
        if (fDragPoint < 0)
        {
            // Hover: highlight the point under the mouse; never consume the event.
            const int p = contains(ev.pos) ? hitTest(geometry(), ev.pos.getX(), ev.pos.getY()) : -1;
            if (p != fHoverPoint) { fHoverPoint = p; repaint(); }
            return false;
        }
        const float speed = (ev.mod & kModifierControl) ? 0.1f : 1.0f;
        const float dx = static_cast<float>(ev.pos.getX() - fLastX);
        const float dy = static_cast<float>(ev.pos.getY() - fLastY);
        fLastX = ev.pos.getX(); fLastY = ev.pos.getY();

        const Slot timeSlot = pointTimeSlot(fDragPoint);
        if (dx != 0.0f)
        {
            // Move the EFFECTIVE time in compressed space; store the param that produces it.
            const float u = warp(display(timeSlot)) + dx * kWarpPerPixel / fUiScale * speed;
            const float t = fRelative ? std::fmin(unwarp(u), kRelMaxTime[timeSlot]) : unwarp(u);
            setSlotValue(timeSlot, fRelative ? t / std::fmax(1e-4f, fBase[timeSlot]) : t);
        }
        if (fDragPoint == kPtDecay && dy != 0.0f)
        {
            const Geometry g = geometry();
            const float span = std::fmax(10.0f, g.bottom - g.top);
            const float target = clamp(display(kSustain) - dy / span * speed, 0.0f, 1.0f);
            setSlotValue(kSustain, fRelative ? offsetFor(target) : target);
        }
        return true;
    }

private:
    struct Param { uint32_t id = kNoParam; float min = 0, max = 1, def = 0, value = 0; };
    enum Point { kPtDelay = 0, kPtAttack, kPtDecay, kPtRelease, kPointCount };
    struct Geometry { float x0, xDelay, xAttack, xDecay, xSustain, xRelease, top, bottom; };

    static constexpr float kTau          = 0.010f;  // warp knee: 10 ms
    static constexpr float kWarpPerPixel = 0.035f;  // ~230 px across 1 ms .. 30 s
    static constexpr int   kCurveSteps   = 16;

    modicumfm::ParamEditCallback* fCb;
    Param  fSlots[kSlotCount];
    bool   fRelative = false, fDisabled = false;
    std::string fDisabledMsg;
    float  fBase[kSlotCount] = { 0.0f, 0.005f, 0.3f, 0.7f, 0.3f };   // relative mode's base envelope
    // Relative mode: effective times are capped like the engine's (operator limits).
    static constexpr float kRelMaxTime[kSlotCount] = { 5.0f, 20.0f, 30.0f, 1.0f, 30.0f };
    Color  fAccent { 60, 200, 90 };
    float  fUiScale = 1.0f;
    bool   fHasFont = false;
    int    fDragPoint = -1, fHoverPoint = -1, fLastClickPoint = -1;
    double fLastX = 0, fLastY = 0;
    uint   fLastClickTime = 0;

    bool hasDelay() const { return fSlots[kDelay].id != kNoParam; }

    // The value the graph draws: the param itself, or (relative mode) base × multiplier for a
    // time / base offset by the param for the sustain.
    float display(Slot s) const
    {
        const float v = fSlots[s].value;
        if (!fRelative || s == kDelay) return v;
        if (s == kSustain)
        {
            const float b = fBase[kSustain];
            return v >= 0.0f ? b + (1.0f - b) * v : b * (1.0f + v);
        }
        return std::fmin(fBase[s] * v, kRelMaxTime[s]);
    }
    // Relative mode: the sustain offset that puts the effective sustain at `target`.
    float offsetFor(float target) const
    {
        const float b = fBase[kSustain];
        if (target >= b) return b < 0.9999f ? (target - b) / (1.0f - b) : 0.0f;
        return b > 1e-4f ? target / b - 1.0f : 0.0f;
    }

    static float clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
    static float warp(float t)   { return std::log1p(std::fmax(0.0f, t) / kTau); }
    static float unwarp(float u) { return kTau * std::expm1(std::fmax(0.0f, u)); }
    // Exponential fall from 1 at f = 0 to exactly 0 at f = 1 (the look of the engine's decays).
    static float expCurve(float f)
    {
        const float end = std::exp(-5.0f);
        return (std::exp(-5.0f * f) - end) / (1.0f - end);
    }
    static Color withA(Color c, float a) { c.alpha = a; return c; }

    Geometry geometry() const
    {
        const float s    = fUiScale;
        const float pad  = 7.0f * s;
        const float band = 15.0f * s;   // readout strip above the plot
        Geometry g;
        g.top    = band;
        g.bottom = getHeight() - pad;
        const float left = pad, width = getWidth() - 2.0f * pad;
        const float susW = 0.16f * width;
        const float minW = 7.0f * s;

        const int   n = hasDelay() ? 4 : 3;
        const float u[4] = { hasDelay() ? warp(display(kDelay)) : 0.0f,
                             warp(display(kAttack)), warp(display(kDecay)),
                             warp(display(kRelease)) };
        const float uSum  = u[0] + u[1] + u[2] + u[3] + 1e-6f;
        const float avail = std::fmax(0.0f, width - susW - n * minW);
        const auto  segW  = [&](int i) { return minW + avail * u[i] / uSum; };

        g.x0       = left;
        g.xDelay   = hasDelay() ? g.x0 + segW(0) : g.x0;
        g.xAttack  = g.xDelay + segW(1);
        g.xDecay   = g.xAttack + segW(2);
        g.xSustain = g.xDecay + susW;
        g.xRelease = g.xSustain + segW(3);
        return g;
    }

    void traceShape(const Geometry& g)
    {
        const float sus = display(kSustain);
        const auto  yAt = [&](float level) { return g.bottom - level * (g.bottom - g.top); };
        beginPath();
        moveTo(g.x0, g.bottom);
        lineTo(g.xDelay, g.bottom);
        lineTo(g.xAttack, g.top);
        for (int i = 1; i <= kCurveSteps; ++i)
        {
            const float f = float(i) / kCurveSteps;
            lineTo(g.xAttack + f * (g.xDecay - g.xAttack), yAt(sus + (1.0f - sus) * expCurve(f)));
        }
        lineTo(g.xSustain, yAt(sus));
        for (int i = 1; i <= kCurveSteps; ++i)
        {
            const float f = float(i) / kCurveSteps;
            lineTo(g.xSustain + f * (g.xRelease - g.xSustain), yAt(sus * expCurve(f)));
        }
    }

    void pointPos(const Geometry& g, int p, float& x, float& y) const
    {
        const float susY = g.bottom - display(kSustain) * (g.bottom - g.top);
        switch (p)
        {
        case kPtDelay:   x = g.xDelay;   y = g.bottom; break;
        case kPtAttack:  x = g.xAttack;  y = g.top;    break;
        case kPtDecay:   x = g.xDecay;   y = susY;     break;
        default:         x = g.xRelease; y = g.bottom; break;
        }
    }

    // Nearest point within reach; ties (points drawn on top of each other) go to the later stage,
    // which is the one that can move away.
    int hitTest(const Geometry& g, double mx, double my) const
    {
        const float reach = 11.0f * fUiScale;
        int best = -1; float bestD = reach * reach;
        for (int p = 0; p < kPointCount; ++p)
        {
            if (p == kPtDelay && !hasDelay()) continue;
            float px, py; pointPos(g, p, px, py);
            const float d = float((px - mx) * (px - mx) + (py - my) * (py - my));
            if (d <= bestD) { bestD = d; best = p; }
        }
        return best;
    }

    static Slot pointTimeSlot(int p)
    {
        switch (p)
        {
        case kPtDelay:  return kDelay;
        case kPtAttack: return kAttack;
        case kPtDecay:  return kDecay;
        default:        return kRelease;
        }
    }

    template <typename F> void forEachSlot(int p, F f) const
    {
        f(pointTimeSlot(p));
        if (p == kPtDecay) f(kSustain);
    }

    void setSlotValue(Slot s, float v)
    {
        Param& sl = fSlots[s];
        v = clamp(v, sl.min, sl.max);
        if (v == sl.value) return;
        sl.value = v;
        fCb->paramEditChange(sl.id, v);
        repaint();
    }

    void resetPoint(int p)
    {
        forEachSlot(p, [&](Slot s) {
            fCb->paramEditBegin(fSlots[s].id);
            setSlotValue(s, fSlots[s].def);
            fCb->paramEditEnd(fSlots[s].id);
        });
    }

    static std::string fmtTime(float t)
    {
        char b[24];
        t < 1.0f ? std::snprintf(b, sizeof(b), "%.0f ms", t * 1000.0f)
                 : std::snprintf(b, sizeof(b), "%.2f s", t);
        return b;
    }

    // In relative mode each value is followed by its multiplier / offset, e.g. "Release 900 ms ×3.0".
    std::string rel(Slot s) const
    {
        if (!fRelative) return "";
        char b[16];
        if (s == kSustain) std::snprintf(b, sizeof(b), " (%+.2f)", fSlots[s].value);
        else               std::snprintf(b, sizeof(b), fSlots[s].value < 9.95f ? " \xC3\x97%.2g" : " \xC3\x97%.0f",
                                         fSlots[s].value);
        return b;
    }

    std::string readout(int p) const
    {
        switch (p)
        {
        case kPtDelay:  return "Delay " + fmtTime(display(kDelay));
        case kPtAttack: return "Attack " + fmtTime(display(kAttack)) + rel(kAttack);
        case kPtDecay:
        {
            char b[16]; std::snprintf(b, sizeof(b), "%.2f", display(kSustain));
            return "Decay " + fmtTime(display(kDecay)) + rel(kDecay) + "  Sustain " + b + rel(kSustain);
        }
        default:        return "Release " + fmtTime(display(kRelease)) + rel(kRelease);
        }
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EnvelopeGraph)
};

END_NAMESPACE_DGL

#endif // MODICUMFM_ENVELOPEGRAPH_HPP
