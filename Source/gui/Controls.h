// Nachhall - knobs and buttons
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Theme.h"

class NachhallAudioProcessor;

// Parameter access from the GUI with proper host gestures
struct ParamHandle
{
    ParamHandle (juce::AudioProcessorValueTreeState& s, const juce::String& id)
        : param (s.getParameter (id)), raw (s.getRawParameterValue (id))
    {
        jassert (param != nullptr && raw != nullptr);
    }

    float get() const noexcept { return raw->load(); }
    void set (float realValue) { param->setValueNotifyingHost (param->convertTo0to1 (realValue)); }
    void begin() { param->beginChangeGesture(); }
    void end() { param->endChangeGesture(); }
    void setWithGesture (float realValue)
    {
        begin();
        set (realValue);
        end();
    }
    juce::String text() const { return param->getCurrentValueAsText(); }
    float defaultValue() const { return param->convertFrom0to1 (param->getDefaultValue()); }
    void setNormalisedWithGesture (float normalised)
    {
        begin();
        param->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, normalised));
        end();
    }
    void resetToDefault() { setNormalisedWithGesture (param->getDefaultValue()); }

    juce::RangedAudioParameter* param;
    std::atomic<float>* raw;
};

// Parses typed values like FabFilter: "1k", "A4", "C#2+13" (frequencies), "2x" (= +6 dB),
// "50%" (= middle position for parameters that are not percentages) or plain numbers with units.
bool parseTypedValue (juce::RangedAudioParameter& p, const juce::String& text, float& normalisedOut);

// Small text field for typing exact values (double-click on a knob)
class InlineValueEditor : public juce::TextEditor
{
public:
    InlineValueEditor();
    void begin (ParamHandle* h, juce::Rectangle<int> area);

private:
    void commit();
    void dismiss();
    ParamHandle* handle = nullptr;
};

// Rotary slider like a FabFilter knob: reacts to the drag speed, Shift = fine (drag + wheel),
// Ctrl/Cmd + click = default value, double click = type a value
class FineSlider : public juce::Slider
{
public:
    FineSlider();
    std::function<void()> onDoubleClick, onResetRequest;

    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override;

private:
    bool resetClick = false;
};

class Knob : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& title,
          juce::Colour colour = theme::teal, bool bipolar = false);

    void paint (juce::Graphics&) override;
    void resized() override;
    void setTitle (const juce::String& t)
    {
        title = t;
        repaint();
    }
    void setTooltip (const juce::String& t) { slider.setTooltip (t); }

    FineSlider slider;
    const juce::String& getParamID() const noexcept { return paramID; }

private:
    ParamHandle handle;
    juce::String paramID;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    juce::String title;
    InlineValueEditor valueEditor;
};

//==============================================================================
class SpaceKnob : public juce::Component, private juce::Timer
{
public:
    explicit SpaceKnob (juce::AudioProcessorValueTreeState& state);
    ~SpaceKnob() override { stopTimer(); }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;

    FineSlider slider;

private:
    void timerCallback() override;
    int dotAt (juce::Point<float> p) const;
    juce::Point<float> dotPosition (int index) const;
    float dotRadius (int index) const;

    juce::AudioProcessorValueTreeState& apvts;
    ParamHandle space, decayRate, freeze, style;
    InlineValueEditor valueEditor;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    juce::Rectangle<float> knobArea;
    int hoverDot = -1;
    int lastStyle = -1;
    float lastSpace = -1.0f, lastRate = -1.0f;
    bool lastFreeze = false;
};

//==============================================================================
class StyleSelector : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    explicit StyleSelector (juce::AudioProcessorValueTreeState& state);
    ~StyleSelector() override { stopTimer(); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    ParamHandle style;
    int last = -1;
};

class FreezeButton : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    explicit FreezeButton (juce::AudioProcessorValueTreeState& state);
    ~FreezeButton() override { stopTimer(); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    ParamHandle freeze;
    bool wasOnAtDown = false;
    juce::uint32 downTime = 0;
    bool last = false;
};

class GateButton : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    explicit GateButton (juce::AudioProcessorValueTreeState& state);
    ~GateButton() override { stopTimer(); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    void showSyncMenu();
    ParamHandle gate, hold, sync;
    float startHold = 250.0f;
    int startSync = 0;
    bool dragging = false;
    juce::String lastText;
    bool lastOn = false;
};

class ChoiceButton : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    ChoiceButton (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& prefix);
    ~ChoiceButton() override { stopTimer(); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    ParamHandle choice;
    juce::String prefix;
    int last = -1;
};
