// AlgorithmDiagram.hpp — drawing of FM6Op's 32 algorithms, plus the two widgets that show them:
//
//   AlgorithmView    the Engine panel's diagram of the current algorithm; click opens the picker,
//                    the mouse wheel steps to the previous / next algorithm
//   AlgorithmPicker  a full-window overlay with all 32 algorithms as thumbnails; click one to
//                    choose it, click outside (or press Esc) to close
//
// Layout follows the classic six-operator chart: carriers sit on the bottom row on an output bus,
// modulators are stacked above the operators they modulate, and the feedback loop is drawn on
// the side of the operator it feeds. Box positions are hand-placed per algorithm (kLayout) — an
// automatic layout tangles on algorithms like 16, 18 and 22. Routing, carriers and the feedback
// path themselves come from the engine's table (FM6OpAlgorithms.hpp), so they can't drift apart.

#ifndef MODICUMFM_ALGORITHMDIAGRAM_HPP
#define MODICUMFM_ALGORITHMDIAGRAM_HPP

#include "NanoVG.hpp"
#include "FM6OpAlgorithms.hpp"

#include <cmath>
#include <functional>
#include <string>

START_NAMESPACE_DGL

namespace algo_diagram {

// Grid position of each operator (x column, y level; 0 = the carrier row), Op1..Op6.
struct Pos { float x, y; };
inline constexpr Pos kLayout[kFM6OpAlgorithms][kFM6OpCount] = {
    {{0,0},{0,1},{1,0},{1,1},{1,2},{1,3}},          //  1  2>1, 6>5>4>3
    {{0,0},{0,1},{1,0},{1,1},{1,2},{1,3}},          //  2
    {{0,0},{0,1},{0,2},{1,0},{1,1},{1,2}},          //  3  3>2>1, 6>5>4
    {{0,0},{0,1},{0,2},{1,0},{1,1},{1,2}},          //  4
    {{0,0},{0,1},{1,0},{1,1},{2,0},{2,1}},          //  5  2>1, 4>3, 6>5
    {{0,0},{0,1},{1,0},{1,1},{2,0},{2,1}},          //  6
    {{0,0},{0,1},{1.5f,0},{1,1},{2,1},{2,2}},       //  7  2>1, (4,5)>3, 6>5
    {{0,0},{0,1},{1.5f,0},{1,1},{2,1},{2,2}},       //  8
    {{0,0},{0,1},{1.5f,0},{1,1},{2,1},{2,2}},       //  9
    {{0,0},{0,1},{0,2},{1.5f,0},{1,1},{2,1}},       // 10  3>2>1, (5,6)>4
    {{0,0},{0,1},{0,2},{1.5f,0},{1,1},{2,1}},       // 11
    {{0,0},{0,1},{2,0},{1,1},{2,1},{3,1}},          // 12  2>1, (4,5,6)>3
    {{0,0},{0,1},{2,0},{1,1},{2,1},{3,1}},          // 13
    {{0,0},{0,1},{1.5f,0},{1.5f,1},{1,2},{2,2}},    // 14  2>1, 4>3, (5,6)>4
    {{0,0},{0,1},{1.5f,0},{1.5f,1},{1,2},{2,2}},    // 15
    {{1,0},{0,1},{1,1},{1,2},{2,1},{2,2}},          // 16  (2,3,5)>1, 4>3, 6>5
    {{1,0},{0,1},{1,1},{1,2},{2,1},{2,2}},          // 17
    {{1,0},{0,1},{1,1},{2,1},{2,2},{2,3}},          // 18  (2,3,4)>1, 6>5>4
    {{0,0},{0,1},{0,2},{1,0},{2,0},{1.5f,1}},       // 19  3>2>1, 6>(4,5)
    {{0,0},{1,0},{0.5f,1},{2.5f,0},{2,1},{3,1}},    // 20  3>(1,2), (5,6)>4
    {{0,0},{1,0},{0.5f,1},{2,0},{3,0},{2.5f,1}},    // 21  3>(1,2), 6>(4,5)
    {{0,0},{0,1},{1,0},{2,0},{3,0},{2,1}},          // 22  2>1, 6>(3,4,5)
    {{0,0},{1,0},{1,1},{2,0},{3,0},{2.5f,1}},       // 23  1, 3>2, 6>(4,5)
    {{0,0},{1,0},{2,0},{3,0},{4,0},{3,1}},          // 24  1, 2, 6>(3,4,5)
    {{0,0},{1,0},{2,0},{3,0},{4,0},{3.5f,1}},       // 25  1, 2, 3, 6>(4,5)
    {{0,0},{1,0},{1,1},{2.5f,0},{2,1},{3,1}},       // 26  1, 3>2, (5,6)>4
    {{0,0},{1,0},{1,1},{2.5f,0},{2,1},{3,1}},       // 27
    {{0,0},{0,1},{1,0},{1,1},{1,2},{2,0}},          // 28  2>1, 5>4>3, 6
    {{0,0},{1,0},{2,0},{2,1},{3,0},{3,1}},          // 29  1, 2, 4>3, 6>5
    {{0,0},{1,0},{2,0},{2,1},{2,2},{3,0}},          // 30  1, 2, 5>4>3, 6
    {{0,0},{1,0},{2,0},{3,0},{4,0},{4,1}},          // 31  1..5, 6>5
    {{0,0},{1,0},{2,0},{3,0},{4,0},{5,0}},          // 32  all carriers
};

struct Style {
    Color accent  { 60, 200, 90 };
    float fontSize = 11.0f;       // operator numbers; 0 = no numbers (tiny thumbnails)
    float line     = 1.4f;
    bool  feedbackActive = true;  // draw the feedback loop bright (else dimmed)
    uint8_t highlightMask = 0;    // operators to ring (bit i = Op i+1), e.g. a macro's targets
    float maxPitch = 1e9f;        // cap on grid spacing (px) so a big box doesn't zoom the diagram
};

inline Color withA(Color c, float a) { c.alpha = a; return c; }

// Draw algorithm `a` (0-based) fitted into the box (x, y, w, h).
inline void draw(NanoVG& vg, int a, float x, float y, float w, float h, const Style& st)
{
    if (a < 0) a = 0;
    if (a >= kFM6OpAlgorithms) a = kFM6OpAlgorithms - 1;
    const FM6OpAlgo& al  = kFM6OpAlgos[a];
    const Pos*       pos = kLayout[a];

    float maxX = 0, maxY = 0;
    for (int o = 0; o < kFM6OpCount; ++o) { maxX = std::fmax(maxX, pos[o].x); maxY = std::fmax(maxY, pos[o].y); }

    // Grid pitch: fit (maxX + 1) columns and (maxY + 1) levels plus room for the output bus and
    // the feedback loop on the right. Columns and levels may be spaced differently (so wide and
    // tall algorithms both fill the box) but by no more than 1.6:1; boxes are ~60% of the
    // smaller spacing.
    const float cols = maxX + 1.6f, rows = maxY + 1.5f;
    float pX = w / cols, pY = h / rows;
    pX = std::fmin(std::fmin(pX, 1.6f * pY), st.maxPitch);
    pY = std::fmin(std::fmin(pY, 1.6f * pX), st.maxPitch);
    const float pitch = std::fmin(pX, pY);
    const float box   = pitch * 0.6f;
    const float gridW = (maxX + 1) * pX, gridH = (maxY + 1) * pY;
    const float ox    = x + (w - gridW - 0.6f * pX) * 0.5f + pX * 0.5f;          // col 0 centre
    const float oy    = y + (h - gridH - 0.5f * pY) * 0.5f + gridH - pY * 0.5f;  // row 0 centre
    const auto cx = [&](int o) { return ox + pos[o].x * pX; };
    const auto cy = [&](int o) { return oy - pos[o].y * pY; };

    const Color dim = Color(150, 156, 166);
    vg.lineCap(NanoVG::ROUND);

    // Modulation links: from the modulator's bottom to the target's top.
    vg.strokeColor(dim); vg.strokeWidth(st.line);
    for (int dst = 0; dst < kFM6OpCount; ++dst)
        for (int src = 0; src < kFM6OpCount; ++src)
            if (al.modMask[dst] & (1u << src))
            {
                vg.beginPath();
                vg.moveTo(cx(src), cy(src) + box * 0.5f);
                vg.lineTo(cx(dst), cy(dst) - box * 0.5f);
                vg.stroke();
            }

    // Output bus under the carriers.
    float busL = 1e9f, busR = -1e9f;
    const float busY = oy + box * 0.5f + pitch * 0.22f;
    for (int o = 0; o < kFM6OpCount; ++o)
        if (al.carrierMask & (1u << o))
        {
            busL = std::fmin(busL, cx(o)); busR = std::fmax(busR, cx(o));
            vg.beginPath(); vg.moveTo(cx(o), cy(o) + box * 0.5f); vg.lineTo(cx(o), busY); vg.stroke();
        }
    vg.strokeColor(withA(st.accent, 0.9f));
    vg.beginPath(); vg.moveTo(busL - box * 0.3f, busY); vg.lineTo(busR + box * 0.3f, busY); vg.stroke();

    // Feedback: out of the source's right side, up/down to the destination, into its right side.
    {
        const int   s = al.fbSrc, d = al.fbDst;
        const float loopX = std::fmax(cx(s), cx(d)) + box * 0.5f + pitch * 0.18f;
        const float topY  = cy(d) - box * 0.5f - pitch * 0.14f;
        vg.strokeColor(st.feedbackActive ? withA(st.accent, 0.95f) : withA(dim, 0.55f));
        vg.beginPath();
        vg.moveTo(cx(s) + box * 0.5f, cy(s));
        vg.lineTo(loopX, cy(s));
        vg.lineTo(loopX, topY);
        vg.lineTo(cx(d), topY);
        vg.lineTo(cx(d), cy(d) - box * 0.5f);
        vg.stroke();
    }

    // Operator boxes: carriers filled with the accent, modulators outlined.
    for (int o = 0; o < kFM6OpCount; ++o)
    {
        const bool carrier = al.carrierMask & (1u << o);
        vg.beginPath();
        vg.roundedRect(cx(o) - box * 0.5f, cy(o) - box * 0.5f, box, box, box * 0.18f);
        vg.fillColor(carrier ? withA(st.accent, 0.85f) : Color(34, 37, 43)); vg.fill();
        vg.strokeColor(carrier ? st.accent : dim); vg.strokeWidth(st.line); vg.stroke();
        if (st.highlightMask & (1u << o))
        {
            const float g = box * 0.16f;
            vg.beginPath();
            vg.roundedRect(cx(o) - box * 0.5f - g, cy(o) - box * 0.5f - g, box + 2 * g, box + 2 * g,
                           box * 0.26f);
            vg.strokeColor(Color(244, 247, 250)); vg.strokeWidth(st.line * 1.4f); vg.stroke();
        }
        if (st.fontSize > 0.0f)
        {
            const char label[2] = { static_cast<char>('1' + o), 0 };
            vg.fontSize(st.fontSize);
            vg.textAlign(NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE);
            vg.fillColor(carrier ? Color(16, 20, 18) : Color(220, 224, 230));
            vg.text(cx(o), cy(o) + 0.5f, label, nullptr);
        }
    }
}

} // namespace algo_diagram

// The current algorithm, drawn large in the Engine panel.
class AlgorithmView : public NanoSubWidget
{
public:
    // onPick: open the picker. onStep(+1/-1): mouse wheel.
    AlgorithmView(Widget* parent, std::function<void()> onPick, std::function<void(int)> onStep)
        : NanoSubWidget(parent), fOnPick(std::move(onPick)), fOnStep(std::move(onStep))
    {
        fHasFont = loadSharedResources();
    }
    void setAlgorithm(int a)        { fAlgo = a; repaint(); }
    void setFeedbackActive(bool on) { if (fFbOn != on) { fFbOn = on; repaint(); } }
    void setHighlight(uint8_t mask) { if (fHighlight != mask) { fHighlight = mask; repaint(); } }
    void setAccentColor(Color c)    { fAccent = c; }
    void setUiScale(float s)        { fUiScale = s; }

protected:
    void onNanoDisplay() override
    {
        const float w = getWidth(), h = getHeight(), s = fUiScale;
        beginPath(); roundedRect(0.5f, 0.5f, w - 1.0f, h - 1.0f, 5.0f);
        fillColor(Color(20, 22, 27)); fill();
        strokeColor(fHover ? Color(90, 98, 110) : Color(46, 50, 58)); strokeWidth(1.0f); stroke();
        if (!fHasFont) return;
        fontFace(NANOVG_DEJAVU_SANS_TTF);

        // Caption: "Algorithm 12" + a chevron hinting that it opens a chooser.
        const float capH = 18.0f * s;
        fontSize(12.0f * s); textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fillColor(Color(176, 182, 190));
        const std::string cap = "Algorithm " + std::to_string(fAlgo + 1);
        text(8.0f * s, capH * 0.5f + 3.0f * s, cap.c_str(), nullptr);
        const float chx = w - 14.0f * s, chy = capH * 0.5f + 3.0f * s;
        beginPath(); moveTo(chx - 4 * s, chy - 2 * s); lineTo(chx + 4 * s, chy - 2 * s); lineTo(chx, chy + 3 * s);
        closePath(); fillColor(Color(150, 160, 172)); fill();

        algo_diagram::Style st;
        st.accent = fAccent; st.fontSize = 11.0f * s; st.line = 1.4f * s; st.feedbackActive = fFbOn;
        st.highlightMask = fHighlight;
        st.maxPitch = 46.0f * s;
        const float pad = 6.0f * s;
        algo_diagram::draw(*this, fAlgo, pad, capH + pad * 0.5f, w - 2 * pad, h - capH - 1.5f * pad, st);
    }

    bool onMouse(const MouseEvent& ev) override
    {
        if (ev.button != 1 || !ev.press || !contains(ev.pos)) return false;
        fOnPick();
        return true;
    }
    bool onMotion(const MotionEvent& ev) override
    {
        const bool in = contains(ev.pos);
        if (in != fHover) { fHover = in; repaint(); }
        return false;
    }
    bool onScroll(const ScrollEvent& ev) override
    {
        if (!contains(ev.pos) || ev.delta.getY() == 0) return false;
        fOnStep(ev.delta.getY() > 0 ? -1 : +1); // wheel up = previous (lower number), like a list
        return true;
    }

private:
    std::function<void()>    fOnPick;
    std::function<void(int)> fOnStep;
    Color fAccent { 60, 200, 90 };
    int     fAlgo = 0;
    float   fUiScale = 1.0f;
    bool    fHasFont = false, fHover = false, fFbOn = false;
    uint8_t fHighlight = 0;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AlgorithmView)
};

// Full-window chooser: dim backdrop + a panel of 32 thumbnails (8 × 4).
class AlgorithmPicker : public NanoSubWidget
{
public:
    AlgorithmPicker(Widget* parent, std::function<void(int)> onChoose)
        : NanoSubWidget(parent), fOnChoose(std::move(onChoose))
    {
        fHasFont = loadSharedResources();
        hide();
    }
    void open(int current) { fCurrent = current; fHoverCell = -1; show(); repaint(); }
    // Hiding doesn't redraw what was underneath, so ask the whole window to repaint.
    void close()           { hide(); getTopLevelWidget()->repaint(); }
    void setAccentColor(Color c) { fAccent = c; }
    void setUiScale(float s)     { fUiScale = s; }

protected:
    void onNanoDisplay() override
    {
        beginPath(); rect(0, 0, getWidth(), getHeight());
        fillColor(Color(0, 0, 0, 0.55f)); fill();

        float px, py, pw, ph; panelRect(px, py, pw, ph);
        beginPath(); roundedRect(px, py, pw, ph, 8.0f);
        fillColor(Color(28, 31, 37)); fill();
        strokeColor(Color(70, 76, 86)); strokeWidth(1.0f); stroke();
        if (!fHasFont) return;
        fontFace(NANOVG_DEJAVU_SANS_TTF);

        const float s = fUiScale;
        fontSize(13.0f * s); textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fillColor(Color(200, 206, 214));
        text(px + 12.0f * s, py + 15.0f * s, "Choose an algorithm   (filled = heard, outlined = modulator)", nullptr);

        for (int a = 0; a < kFM6OpAlgorithms; ++a)
        {
            float cx, cy, cw, ch; cellRect(a, cx, cy, cw, ch);
            const bool cur = a == fCurrent, hov = a == fHoverCell;
            beginPath(); roundedRect(cx, cy, cw, ch, 5.0f);
            fillColor(cur ? Color(34, 52, 40) : (hov ? Color(38, 42, 50) : Color(22, 24, 29))); fill();
            strokeColor(cur ? fAccent : (hov ? Color(110, 118, 130) : Color(46, 50, 58)));
            strokeWidth(cur ? 1.6f : 1.0f); stroke();
            fontSize(11.0f * s); textAlign(ALIGN_LEFT | ALIGN_TOP);
            fillColor(cur ? fAccent : Color(176, 182, 190));
            text(cx + 5.0f * s, cy + 3.0f * s, std::to_string(a + 1).c_str(), nullptr);

            algo_diagram::Style st;
            st.accent = fAccent; st.fontSize = 8.5f * s; st.line = 1.1f * s;
            algo_diagram::draw(*this, a, cx + 4 * s, cy + 14 * s, cw - 8 * s, ch - 17 * s, st);
        }
    }

    bool onMouse(const MouseEvent& ev) override
    {
        if (!isVisible()) return false;
        if (ev.press && ev.button == 1)
        {
            const int c = cellAt(ev.pos.getX(), ev.pos.getY());
            if (c >= 0) fOnChoose(c);
            close();
        }
        return true; // modal: swallow every click while open
    }
    bool onMotion(const MotionEvent& ev) override
    {
        if (!isVisible()) return false;
        const int c = cellAt(ev.pos.getX(), ev.pos.getY());
        if (c != fHoverCell) { fHoverCell = c; repaint(); }
        return true;
    }
    bool onScroll(const ScrollEvent&) override { return isVisible(); }
    bool onKeyboard(const KeyboardEvent& ev) override
    {
        if (!isVisible() || !ev.press) return false;
        if (ev.key == kKeyEscape) { close(); return true; }
        return false;
    }

private:
    std::function<void(int)> fOnChoose;
    Color fAccent { 60, 200, 90 };
    int   fCurrent = 0, fHoverCell = -1;
    float fUiScale = 1.0f;
    bool  fHasFont = false;

    static constexpr int kCols = 8, kRows = 4;

    void panelRect(float& x, float& y, float& w, float& h) const
    {
        const float s = fUiScale;
        w = std::fmin(getWidth() - 24.0f * s, 860.0f * s);
        h = std::fmin(getHeight() - 24.0f * s, 470.0f * s);
        x = (getWidth() - w) * 0.5f;
        y = (getHeight() - h) * 0.5f;
    }
    void cellRect(int a, float& x, float& y, float& w, float& h) const
    {
        const float s = fUiScale, gap = 6.0f * s, head = 30.0f * s, pad = 10.0f * s;
        float px, py, pw, ph; panelRect(px, py, pw, ph);
        w = (pw - 2 * pad - (kCols - 1) * gap) / kCols;
        h = (ph - head - pad - (kRows - 1) * gap) / kRows;
        x = px + pad + (a % kCols) * (w + gap);
        y = py + head + (a / kCols) * (h + gap);
    }
    int cellAt(double mx, double my) const
    {
        for (int a = 0; a < kFM6OpAlgorithms; ++a)
        {
            float x, y, w, h; cellRect(a, x, y, w, h);
            if (mx >= x && mx < x + w && my >= y && my < y + h) return a;
        }
        return -1;
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AlgorithmPicker)
};

END_NAMESPACE_DGL

#endif // MODICUMFM_ALGORITHMDIAGRAM_HPP
