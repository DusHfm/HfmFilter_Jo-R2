// Nachhall - editor
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "gui/Controls.h"
#include "gui/EqDisplay.h"
#include "gui/MidiLearnOverlay.h"
#include "gui/PresetBrowser.h"
#include "gui/Theme.h"

class IOPanel : public juce::Component
{
public:
    explicit IOPanel (juce::AudioProcessorValueTreeState& state);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    Knob inGain, inPan, outGain, outPan;
};

class MainPanel : public juce::Component, private juce::Timer
{
public:
    static constexpr int kWidth = 1000;
    static constexpr int kHeight = 640;

    explicit MainPanel (NachhallAudioProcessor& p);
    ~MainPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void importIR();
    void loadPresetFile (const juce::File& f);
    void importIRFile (const juce::File& f);

    // provided by the editor
    std::function<void()> onToggleFullScreen;
    std::function<void (int)> onSetEditorWidth;
    std::function<juce::Rectangle<int>()> getDisplayArea;

private:
    void timerCallback() override;
    void togglePresetBrowser();
    void stepPreset (int delta);
    void savePreset();
    void showHelpMenu();
    void showMidiMenu();
    void showSizeMenu();
    void updateButtonStates();
    juce::Array<juce::File> userPresetFiles() const;

    struct UndoListener : public juce::MouseListener
    {
        explicit UndoListener (juce::UndoManager& u) : um (u) {}
        void mouseDown (const juce::MouseEvent&) override { um.beginNewTransaction(); }
        juce::UndoManager& um;
    };

    NachhallAudioProcessor& proc;
    juce::AudioProcessorValueTreeState& apvts;
    UndoListener undoListener;

    // top bar
    juce::TextButton prevPreset { "<" }, nextPreset { ">" }, presetName, savePresetButton { "Save" }, irButton { "IR Import" };
    juce::TextButton undoButton { "Undo" }, redoButton { "Redo" }, aButton { "A" }, bButton { "B" }, copyButton { "A > B" }, helpButton { "?" };
    juce::TextButton fullScreenButton { "Full" };

    // knobs
    Knob decayRate, predelay, predelayOffset, character, thickness, distance, brightness, width, ducking, mix;
    SpaceKnob space;
    ChoiceButton predelaySync;
    juce::TextButton lockMixButton { "Lock Mix" };
    StyleSelector style;
    FreezeButton freeze;
    GateButton gate;

    // display + bottom bar
    EqDisplay display;
    juce::TextButton analyzerButton, pianoButton { "Piano" }, meterButton { "Meter" }, autoGainButton { "EQ Auto Gain" };
    juce::TextButton ioButton { "In / Out" }, bypassButton { "Bypass" };
    juce::TextButton midiLearnButton { "MIDI Learn" }, midiMenuButton { "v" }, sizeButton { "Size" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> autoGainAttachment, bypassAttachment;
    IOPanel ioPanel;
    PresetBrowser browser;
    MidiLearnOverlay midiOverlay;

    std::unique_ptr<juce::FileChooser> chooser;
    juce::String statusText;
    int statusCountdown = 0;
    int lastPdSync = -1;
};

class NachhallAudioProcessorEditor : public juce::AudioProcessorEditor, public juce::FileDragAndDropTarget
{
public:
    explicit NachhallAudioProcessorEditor (NachhallAudioProcessor&);
    ~NachhallAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

    void toggleFullScreen();
    void setEditorWidth (int width);

private:
    NachhallAudioProcessor& proc;
    NachhallLookAndFeel lnf;
    std::unique_ptr<MainPanel> main;
    juce::TooltipWindow tooltips { this, 600 };
    bool sizeInitialised = false;
    bool fullScreen = false;
    int widthBeforeFullScreen = 1000;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NachhallAudioProcessorEditor)
};
