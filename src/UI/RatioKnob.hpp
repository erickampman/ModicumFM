// RatioKnob.hpp — an operator's frequency-ratio knob that does the job of Ratio + Fine in one
// control. Same look as Knob (it reuses Knob's drawing) but its own interaction:
//
//   drag            snaps through the harmonic ratios 0.5, 1, 2, … 16 (no accidental clangs)
//   Shift-drag      moves the ratio continuously (2 cents per pixel; Ctrl+Shift 0.2) — reaches
//                   inharmonic ratios (1.41, 3.5, …) and slight detunings for beating
//   wheel           next / previous harmonic ratio; Shift+wheel nudges by 2 cents
//   double-click    snaps to the nearest harmonic ratio and clears Fine; again resets to 1
//
// The operator's separate Fine param (cents) has no knob of its own any more. It still exists for
// presets / automation, shows in the readout when non-zero ("2.00 +7c"), and double-click clears
// it. Both params are edited through ParamEditCallback so the host sees begin/change/end gestures.

#ifndef MODICUMFM_RATIOKNOB_HPP
#define MODICUMFM_RATIOKNOB_HPP

#include "Knob.hpp"
#include "ParamEdit.hpp"

#include <cmath>
#include <cstdio>
#include <string>

START_NAMESPACE_DGL

class RatioKnob : public Knob
{
public:
    // The base Knob's own callback is never used: all of its event-handler entry points are
    // overridden below, so it is constructed with none.
    RatioKnob(Widget* parent, modicumfm::ParamEditCallback* cb, uint32_t ratioId, uint32_t fineId)
        : Knob(parent, nullptr), fCb(cb), fRatioId(ratioId), fFineId(fineId)
    {
        setId(ratioId);
        setFormatter([this](float r) { return formatReadout(r); });
    }

    // Host / preset updates.
    void setRatio(float r)       { fRatio = clampRatio(r); setRealValue(fRatio, false); repaint(); }
    void setFineCents(float c)   { fFineCents = c; repaint(); }

    static constexpr float kMinRatio = 0.5f, kMaxRatio = 16.0f;

protected:
    bool onMouse(const MouseEvent& ev) override
    {
        if (ev.button != 1)
            return false;
        if (ev.press)
        {
            if (!contains(ev.pos))
                return false;
            if (fLastClickTime > 0 && ev.time > fLastClickTime && ev.time - fLastClickTime <= 300)
            {
                fLastClickTime = 0;
                resetOnDoubleClick();
                return true;
            }
            fLastClickTime = ev.time;
            fDragging = true;
            fLastY = ev.pos.getY();
            seedAccumulators(ev.mod);
            fCb->paramEditBegin(fRatioId);
            return true;
        }
        if (fDragging)
        {
            fDragging = false;
            fCb->paramEditEnd(fRatioId);
            return true;
        }
        return false;
    }

    bool onMotion(const MotionEvent& ev) override
    {
        if (!fDragging)
            return false;
        const double dy = fLastY - ev.pos.getY();   // drag up = higher
        fLastY = ev.pos.getY();
        if (isFine(ev.mod) != fFineMode)
            seedAccumulators(ev.mod);               // Shift pressed/released mid-drag
        if (fFineMode)
        {
            fCents += dy * ((ev.mod & kModifierControl) ? 0.2 : 2.0);
            fCents = std::fmax(std::log2(kMinRatio) * 1200.0, std::fmin(std::log2(kMaxRatio) * 1200.0, fCents));
            commitRatio(static_cast<float>(std::exp2(fCents / 1200.0)));
        }
        else
        {
            fStepPos += dy / kPixelsPerStep;
            fStepPos = std::fmax(0.0, std::fmin(double(kSnapCount - 1), fStepPos));
            commitRatio(kSnaps[static_cast<int>(std::lround(fStepPos))]);
        }
        return true;
    }

    bool onScroll(const ScrollEvent& ev) override
    {
        if (!contains(ev.pos))
            return false;
        const float dir = ev.delta.getY() > 0 ? 1.0f : (ev.delta.getY() < 0 ? -1.0f : 0.0f);
        if (dir == 0.0f)
            return false;
        float next;
        if (isFine(ev.mod))
            next = clampRatio(fRatio * std::exp2(dir * 2.0f / 1200.0f));
        else
        {
            // From an off-grid ratio, the first notch lands on the nearest harmonic in that
            // direction; from a harmonic, it moves one step.
            const int i = nearestSnap(fRatio);
            const bool towardNearest = !isSnapped(fRatio) && (kSnaps[i] - fRatio) * dir > 0.0f;
            next = kSnaps[towardNearest ? i : clampIndex(i + static_cast<int>(dir))];
        }
        fCb->paramEditBegin(fRatioId);
        commitRatio(next);
        fCb->paramEditEnd(fRatioId);
        return true;
    }

private:
    static constexpr int   kSnapCount = 17;
    static constexpr float kSnaps[kSnapCount] = {0.5f, 1, 2, 3, 4, 5, 6, 7, 8,
                                                 9, 10, 11, 12, 13, 14, 15, 16};
    static constexpr double kPixelsPerStep = 10.0;

    modicumfm::ParamEditCallback* fCb;
    uint32_t fRatioId, fFineId;
    float    fRatio = 1.0f, fFineCents = 0.0f;
    bool     fDragging = false, fFineMode = false;
    double   fLastY = 0.0, fStepPos = 0.0, fCents = 0.0;
    uint     fLastClickTime = 0;

    static bool  isFine(uint mod) { return (mod & kModifierShift) != 0; }
    static float clampRatio(float r) { return std::fmax(kMinRatio, std::fmin(kMaxRatio, r)); }
    static int   clampIndex(int i) { return i < 0 ? 0 : (i >= kSnapCount ? kSnapCount - 1 : i); }
    static int   nearestSnap(float r)
    {
        int best = 0;
        for (int i = 1; i < kSnapCount; ++i)
            if (std::fabs(std::log2(kSnaps[i] / r)) < std::fabs(std::log2(kSnaps[best] / r)))
                best = i;
        return best;
    }
    static bool isSnapped(float r) { return std::fabs(kSnaps[nearestSnap(r)] - r) < 1e-4f; }

    void seedAccumulators(uint mod)
    {
        fFineMode = isFine(mod);
        fCents    = std::log2(fRatio) * 1200.0;
        fStepPos  = nearestSnap(fRatio);
    }

    void commitRatio(float r)
    {
        r = clampRatio(r);
        if (r == fRatio)
            return;
        fRatio = r;
        setRealValue(r, false);
        fCb->paramEditChange(fRatioId, r);
        repaint();
    }

    void resetOnDoubleClick()
    {
        const bool alreadyClean = isSnapped(fRatio) && std::fabs(fFineCents) < 0.05f;
        if (std::fabs(fFineCents) >= 0.05f)
        {
            fCb->paramEditBegin(fFineId);
            fCb->paramEditChange(fFineId, 0.0f);
            fCb->paramEditEnd(fFineId);
            fFineCents = 0.0f;
        }
        fCb->paramEditBegin(fRatioId);
        commitRatio(alreadyClean ? 1.0f : kSnaps[nearestSnap(fRatio)]);
        fCb->paramEditEnd(fRatioId);
        repaint();
    }

    // "2.00" on a harmonic ratio, "1.414" off it; " +7c" when the hidden Fine is non-zero.
    std::string formatReadout(float r) const
    {
        char b[32];
        std::snprintf(b, sizeof(b), isSnapped(r) ? "%.2f" : "%.3f", r);
        std::string s = b;
        if (std::fabs(fFineCents) >= 0.05f)
        {
            std::snprintf(b, sizeof(b), " %+.0fc", fFineCents);
            s += b;
        }
        return s;
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RatioKnob)
};

END_NAMESPACE_DGL

#endif // MODICUMFM_RATIOKNOB_HPP
