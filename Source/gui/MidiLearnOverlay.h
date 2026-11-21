// Nachhall - MIDI Learn mode: dims the interface, shows the controller number of every control,
// click a control and move a knob on the MIDI controller to connect them
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../PluginProcessor.h"
#include "Theme.h"

class MidiLearnOverlay : public juce::Component, private juce::Timer
{
public:
    struct Target
    {
        juce::Component* component;
        juce::String paramID;
    };

    explicit MidiLearnOverlay (NachhallAudioProcessor& processor);
    ~MidiLearnOverlay() override { stopTimer(); }

    void setTargets (std::vector<Target> t) { targets = std::move (t); }
    std::function<void()> onClose;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void visibilityChanged() override;

private:
    void timerCallback() override { repaint(); }
    juce::Rectangle<int> areaFor (const Target& t) const;

    NachhallAudioProcessor& proc;
    std::vector<Target> targets;
    juce::Rectangle<int> closeArea;
};
