#include "MidiLearnOverlay.h"

MidiLearnOverlay::MidiLearnOverlay (NachhallAudioProcessor& p) : proc (p)
{
    setComponentID ("midiLearn");
    setInterceptsMouseClicks (true, false);
}

juce::Rectangle<int> MidiLearnOverlay::areaFor (const Target& t) const
{
    if (t.component == nullptr || ! t.component->isShowing())
        return {};
    return getLocalArea (t.component->getParentComponent(), t.component->getBounds());
}

void MidiLearnOverlay::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.55f));

    const int armed = proc.midiLearnParam.load();
    for (const auto& t : targets)
    {
        const auto area = areaFor (t).toFloat().reduced (2.0f);
        if (area.isEmpty())
            continue;
        const int idx = proc.getParameterIndex (t.paramID);
        const int cc = proc.getControllerForParameter (idx);
        const bool isArmed = idx >= 0 && idx == armed;

        g.setColour (isArmed ? theme::red : theme::yellow.withAlpha (0.8f));
        g.drawRoundedRectangle (area, 5.0f, isArmed ? 3.0f : 1.5f);

        // balloon with the controller number
        const juce::String text = isArmed ? juce::String ("move a MIDI control") : (cc >= 0 ? "CC " + juce::String (cc) : juce::String ("-"));
        const auto font = theme::font (12.0f, true);
        const float w = juce::GlyphArrangement::getStringWidth (font, text) + 14.0f;
        auto balloon = juce::Rectangle<float> (area.getCentreX() - w * 0.5f, area.getY() + 4.0f, w, 18.0f);
        g.setColour (isArmed ? theme::red : (cc >= 0 ? theme::yellow : theme::panelLight));
        g.fillRoundedRectangle (balloon, 9.0f);
        g.setColour (isArmed || cc >= 0 ? juce::Colours::black : theme::text);
        g.setFont (font);
        g.drawText (text, balloon, juce::Justification::centred);
    }

    // header
    auto header = juce::Rectangle<int> (getWidth() / 2 - 330, 6, 660, 28);
    g.setColour (theme::panel);
    g.fillRoundedRectangle (header.toFloat(), 6.0f);
    g.setColour (theme::red);
    g.drawRoundedRectangle (header.toFloat(), 6.0f, 1.5f);
    g.setColour (theme::text);
    g.setFont (theme::font (13.0f));
    g.drawText (proc.midiEnabled.load() ? "MIDI LEARN: click a control, then move a knob or fader on your MIDI controller"
                                        : "MIDI LEARN (MIDI control is disabled in the MIDI menu)",
                header.withTrimmedRight (90).reduced (12, 0), juce::Justification::centredLeft);
    closeArea = header.removeFromRight (84).reduced (4);
    g.setColour (theme::panelLight);
    g.fillRoundedRectangle (closeArea.toFloat(), 4.0f);
    g.setColour (theme::text);
    g.drawText ("Close", closeArea, juce::Justification::centred);
}

void MidiLearnOverlay::mouseDown (const juce::MouseEvent& e)
{
    if (closeArea.contains (e.getPosition()))
    {
        if (onClose)
            onClose();
        return;
    }
    for (const auto& t : targets)
        if (areaFor (t).contains (e.getPosition()))
        {
            proc.midiLearnParam = proc.getParameterIndex (t.paramID);
            repaint();
            return;
        }
    proc.midiLearnParam = -1;
    repaint();
}

void MidiLearnOverlay::visibilityChanged()
{
    if (isVisible())
        startTimerHz (10);
    else
    {
        stopTimer();
        proc.midiLearnParam = -1;
    }
}
