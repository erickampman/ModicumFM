// Label.hpp — a static text caption (e.g. the "Op 1".."Op 4" row captions in the FM panel).
// Left-aligned and vertically centred in its box, DPF's bundled DejaVu Sans like the GroupBox
// titles. Not interactive.

#ifndef TWINFM_LABEL_HPP
#define TWINFM_LABEL_HPP

#include "NanoVG.hpp"

#include <string>

START_NAMESPACE_DGL

class Label : public NanoSubWidget
{
public:
    explicit Label(Widget* parent, const std::string& text = {})
        : NanoSubWidget(parent), fText(text)
    {
        fHasFont = loadSharedResources();
    }

    void setText(const std::string& t) { fText = t; repaint(); }
    void setColor(Color c)             { fColor = c; }
    void setUiScale(float s)           { fUiScale = s; }

protected:
    void onNanoDisplay() override
    {
        if (!fHasFont || fText.empty())
            return;
        fontFace(NANOVG_DEJAVU_SANS_TTF);
        fontSize(13.0f * fUiScale);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fillColor(fColor);
        text(0.0f, getHeight() * 0.5f, fText.c_str(), nullptr);
    }

private:
    std::string fText;
    Color       fColor   = Color(170, 176, 186);
    float       fUiScale = 1.0f;
    bool        fHasFont = false;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Label)
};

END_NAMESPACE_DGL

#endif // TWINFM_LABEL_HPP
