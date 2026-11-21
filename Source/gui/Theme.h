// Nachhall - colours, fonts and look & feel
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace theme
{
const juce::Colour background { 0xff14171c };
const juce::Colour panel { 0xff1b1f26 };
const juce::Colour panelLight { 0xff242a33 };
const juce::Colour outline { 0xff2f3643 };
const juce::Colour text { 0xffdfe4ec };
const juce::Colour textDim { 0xff8b95a6 };
const juce::Colour yellow { 0xfff2c14e };   // Space / decay time / Post EQ
const juce::Colour blue { 0xff4fa8ff };     // Decay Rate EQ
const juce::Colour teal { 0xff74d3c6 };     // knob arcs
const juce::Colour red { 0xffe5534b };
const juce::Colour green { 0xff6fcf7f };
const juce::Colour display { 0xff0e1115 };
const juce::Colour grid { 0xff222833 };

inline juce::Font font (float height, bool bold = false)
{
    return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
}
} // namespace theme

class NachhallLookAndFeel : public juce::LookAndFeel_V4
{
public:
    NachhallLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos, float rotaryStartAngle,
                           float rotaryEndAngle, juce::Slider&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour, bool isMouseOverButton,
                               bool isButtonDown) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool isMouseOverButton, bool isButtonDown) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    juce::Font getPopupMenuFont() override;
    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
    void drawTooltip (juce::Graphics&, const juce::String& text, int width, int height) override;
    juce::Font getAlertWindowMessageFont() override;
    juce::Font getAlertWindowTitleFont() override;
    juce::Font getLabelFont (juce::Label&) override;
};
