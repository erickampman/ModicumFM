// TitleText.hpp — the "FeM" wordmark: "Fe" and "M" drawn in two accent colors using the
// embedded Almendra display font (see EmbeddedResources.hpp / Resources/Almendra-Regular.ttf).
// Sits between the logo and the Preset dropdown in the top strip. Not interactive.

#ifndef FEM_TITLETEXT_HPP
#define FEM_TITLETEXT_HPP

#include "NanoVG.hpp"

#include <string>

START_NAMESPACE_DGL

class TitleText : public NanoSubWidget
{
public:
    TitleText(Widget* parent, const uchar* fontData, uint fontDataSize, Color colorA, Color colorB)
        : NanoSubWidget(parent), fColorA(colorA), fColorB(colorB)
    {
        createFontFromMemory("Almendra", fontData, fontDataSize, false);
    }

    void setFontSize(float pt) { fFontSize = pt; }

protected:
    void onNanoDisplay() override
    {
        fontFace("Almendra");
        fontSize(fFontSize);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        const float baseY = getHeight() * 0.5f;

        fillColor(fColorA);
        text(0.0f, baseY, fPartA.c_str(), nullptr);

        // Part B starts right where part A actually ended (measured, not assumed), so the two
        // colors read as one continuous word rather than two separately-kerned pieces.
        Rectangle<float> boundsA;
        textBounds(0.0f, baseY, fPartA.c_str(), nullptr, boundsA);
        const float xB = boundsA.getX() + boundsA.getWidth();

        fillColor(fColorB);
        text(xB, baseY, fPartB.c_str(), nullptr);
    }

private:
    Color fColorA, fColorB;
    float fFontSize = 48.0f;
    const std::string fPartA = "Fe";
    const std::string fPartB = "M";

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TitleText)
};

END_NAMESPACE_DGL

#endif // FEM_TITLETEXT_HPP
