#include "Theme.h"

NachhallLookAndFeel::NachhallLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, theme::background);
    setColour (juce::TextButton::buttonColourId, theme::panelLight);
    setColour (juce::TextButton::buttonOnColourId, theme::yellow.withAlpha (0.85f));
    setColour (juce::TextButton::textColourOffId, theme::text);
    setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    setColour (juce::PopupMenu::backgroundColourId, theme::panel);
    setColour (juce::PopupMenu::textColourId, theme::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, theme::yellow.withAlpha (0.25f));
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    setColour (juce::PopupMenu::headerTextColourId, theme::yellow);
    setColour (juce::Label::textColourId, theme::text);
    setColour (juce::TextEditor::backgroundColourId, theme::display);
    setColour (juce::TextEditor::textColourId, theme::text);
    setColour (juce::TextEditor::outlineColourId, theme::outline);
    setColour (juce::TextEditor::focusedOutlineColourId, theme::yellow);
    setColour (juce::AlertWindow::backgroundColourId, theme::panel);
    setColour (juce::AlertWindow::textColourId, theme::text);
    setColour (juce::AlertWindow::outlineColourId, theme::outline);
    setColour (juce::TooltipWindow::backgroundColourId, theme::panelLight);
    setColour (juce::TooltipWindow::textColourId, theme::text);
    setColour (juce::Slider::textBoxTextColourId, theme::text);
    setColour (juce::ComboBox::backgroundColourId, theme::panelLight);
    setColour (juce::ComboBox::textColourId, theme::text);
    setColour (juce::ComboBox::outlineColourId, theme::outline);
}

void NachhallLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos, float startAngle,
                                            float endAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height).reduced (3.0f);
    const float radius = std::min (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float angle = startAngle + sliderPos * (endAngle - startAngle);
    const auto arcColour = slider.findColour (juce::Slider::rotarySliderFillColourId);
    const bool hover = slider.isMouseOverOrDragging();
    const float lineW = std::max (2.0f, radius * 0.11f);
    const float arcRadius = radius - lineW * 0.5f;

    // track
    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
    g.setColour (theme::outline);
    g.strokePath (track, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // value arc (bipolar sliders start in the middle)
    const bool bipolar = slider.getProperties().getWithDefault ("bipolar", false);
    const float from = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;
    if (std::abs (angle - from) > 0.001f)
    {
        juce::Path arc;
        arc.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, std::min (from, angle), std::max (from, angle), true);
        g.setColour (arcColour.withAlpha (slider.isEnabled() ? 1.0f : 0.4f));
        g.strokePath (arc, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // body
    const float bodyR = arcRadius - lineW * 1.3f;
    juce::ColourGradient grad (theme::panelLight.brighter (hover ? 0.18f : 0.08f), centre.x, centre.y - bodyR,
                               theme::panel.darker (0.3f), centre.x, centre.y + bodyR, false);
    g.setGradientFill (grad);
    g.fillEllipse (centre.x - bodyR, centre.y - bodyR, bodyR * 2.0f, bodyR * 2.0f);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.drawEllipse (centre.x - bodyR, centre.y - bodyR, bodyR * 2.0f, bodyR * 2.0f, 1.0f);

    // pointer
    juce::Path p;
    const float pl = bodyR * 0.62f;
    const float pw = std::max (2.0f, bodyR * 0.09f);
    p.addRoundedRectangle (-pw * 0.5f, -bodyR + bodyR * 0.14f, pw, pl, pw * 0.5f);
    p.applyTransform (juce::AffineTransform::rotation (angle).translated (centre.x, centre.y));
    g.setColour (slider.isEnabled() ? theme::text : theme::textDim);
    g.fillPath (p);
}

void NachhallLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& bg, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = b.getToggleState();
    auto c = on ? b.findColour (juce::TextButton::buttonOnColourId) : bg;
    if (over)
        c = c.brighter (0.12f);
    if (down)
        c = c.darker (0.15f);
    g.setColour (c);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (on ? c.brighter (0.3f) : theme::outline);
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
}

void NachhallLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    g.setFont (getTextButtonFont (b, b.getHeight()));
    g.setColour (b.findColour (b.getToggleState() ? juce::TextButton::textColourOnId : juce::TextButton::textColourOffId)
                     .withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.5f));
    g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (4, 1), juce::Justification::centred, 1);
}

juce::Font NachhallLookAndFeel::getTextButtonFont (juce::TextButton&, int h)
{
    return theme::font (std::min (14.0f, (float) h * 0.55f));
}

juce::Font NachhallLookAndFeel::getPopupMenuFont() { return theme::font (14.0f); }

void NachhallLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    g.fillAll (theme::panel);
    g.setColour (theme::outline);
    g.drawRect (0, 0, w, h, 1);
}

void NachhallLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int w, int h)
{
    g.fillAll (theme::panelLight);
    g.setColour (theme::outline);
    g.drawRect (0, 0, w, h, 1);
    g.setColour (theme::text);
    g.setFont (theme::font (13.0f));
    g.drawFittedText (text, juce::Rectangle<int> (w, h).reduced (6, 2), juce::Justification::centredLeft, 4);
}

juce::Font NachhallLookAndFeel::getAlertWindowMessageFont() { return theme::font (14.0f); }
juce::Font NachhallLookAndFeel::getAlertWindowTitleFont() { return theme::font (17.0f, true); }
juce::Font NachhallLookAndFeel::getLabelFont (juce::Label&) { return theme::font (14.0f); }
