// TextButton.hpp — a small flat push button with a text caption (e.g. the Character panel's
// "Reset"). Calls its callback on a left click inside it.

#ifndef MODICUMFM_TEXTBUTTON_HPP
#define MODICUMFM_TEXTBUTTON_HPP

#include "NanoVG.hpp"

#include <functional>
#include <string>

START_NAMESPACE_DGL

class TextButton : public NanoSubWidget
{
public:
    TextButton(Widget* parent, std::string caption, std::function<void()> onClick)
        : NanoSubWidget(parent), fCaption(std::move(caption)), fOnClick(std::move(onClick))
    {
        fHasFont = loadSharedResources();
    }
    void setAccentColor(Color c) { fAccent = c; }
    void setUiScale(float s)     { fUiScale = s; }

protected:
    void onNanoDisplay() override
    {
        const float w = getWidth(), h = getHeight();
        beginPath(); roundedRect(0.5f, 0.5f, w - 1.0f, h - 1.0f, h * 0.25f);
        fillColor(fPressed ? Color(48, 54, 62) : (fHover ? Color(40, 45, 52) : Color(30, 33, 39))); fill();
        strokeColor(fHover ? fAccent : Color(70, 76, 86)); strokeWidth(1.0f); stroke();
        if (!fHasFont) return;
        fontFace(NANOVG_DEJAVU_SANS_TTF); fontSize(12.0f * fUiScale);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fillColor(fHover ? Color(236, 240, 244) : Color(196, 202, 210));
        text(w * 0.5f, h * 0.5f, fCaption.c_str(), nullptr);
    }
    bool onMouse(const MouseEvent& ev) override
    {
        if (ev.button != 1) return false;
        if (ev.press)
        {
            if (!contains(ev.pos)) return false;
            fPressed = true; repaint();
            return true;
        }
        if (fPressed)
        {
            fPressed = false; repaint();
            if (contains(ev.pos)) fOnClick();
            return true;
        }
        return false;
    }
    bool onMotion(const MotionEvent& ev) override
    {
        const bool in = contains(ev.pos);
        if (in != fHover) { fHover = in; repaint(); }
        return false;
    }

private:
    std::string           fCaption;
    std::function<void()> fOnClick;
    Color fAccent { 60, 200, 90 };
    float fUiScale = 1.0f;
    bool  fHasFont = false, fHover = false, fPressed = false;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TextButton)
};

END_NAMESPACE_DGL

#endif // MODICUMFM_TEXTBUTTON_HPP
