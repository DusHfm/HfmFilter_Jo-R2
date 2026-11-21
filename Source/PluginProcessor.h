// Nachhall - plug-in processor (VST3 / AU / Standalone)
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Params.h"
#include "Presets.h"
#include "dsp/ReverbEngine.h"

//==============================================================================
// Lock-free single producer / single consumer tap for the spectrum analyser
class AnalyzerTap
{
public:
    static constexpr int kSize = 1 << 15;

    AnalyzerTap() : buffer ((size_t) kSize, 0.0f) {}

    void push (const float* data, int n) noexcept
    {
        const uint32_t w = writePos.load (std::memory_order_relaxed);
        for (int i = 0; i < n; ++i)
            buffer[(size_t) ((w + (uint32_t) i) & (kSize - 1))] = data[i];
        writePos.store (w + (uint32_t) n, std::memory_order_release);
    }

    void pushSilence (int n) noexcept
    {
        const uint32_t w = writePos.load (std::memory_order_relaxed);
        for (int i = 0; i < n; ++i)
            buffer[(size_t) ((w + (uint32_t) i) & (kSize - 1))] = 0.0f;
        writePos.store (w + (uint32_t) n, std::memory_order_release);
    }

    void readLatest (float* dst, int n) const noexcept
    {
        const uint32_t w = writePos.load (std::memory_order_acquire);
        for (int i = 0; i < n; ++i)
            dst[i] = buffer[(size_t) ((w - (uint32_t) n + (uint32_t) i) & (kSize - 1))];
    }

    uint32_t getWritePosition() const noexcept { return writePos.load (std::memory_order_acquire); }

private:
    std::vector<float> buffer;
    std::atomic<uint32_t> writePos { 0 };
};

//==============================================================================
class NachhallAudioProcessor : public juce::AudioProcessor
{
public:
    NachhallAudioProcessor();
    ~NachhallAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; } // MIDI Learn
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorParameter* getBypassParameter() const override;

    //==========================================================================
    // Editor API
    juce::UndoManager undoManager;
    juce::AudioProcessorValueTreeState apvts;

    AnalyzerTap tapPre, tapWet, tapOut;
    std::atomic<float> meterPeak[2] {};
    std::atomic<double> hostBpm { 120.0 };
    std::atomic<double> currentSampleRate { 48000.0 };

    // presets
    void loadFactoryPreset (int index);
    bool loadPresetFile (const juce::File& file);
    bool savePresetFile (const juce::File& file, const PresetInfo& info = {});
    bool saveAsDefaultPreset();
    static juce::File getDefaultPresetFile();
    static PresetInfo readPresetInfo (const juce::File& file);
    static bool writePresetInfo (const juce::File& file, const PresetInfo& info);
    bool isPresetModified() const; // parameters changed since the preset was loaded (message thread)
    static juce::File getUserPresetFolder();
    juce::String getCurrentPresetName() const
    {
        const juce::ScopedLock sl (stateLock);
        return currentPresetName;
    }
    void setCurrentPresetName (const juce::String& n)
    {
        const juce::ScopedLock sl (stateLock);
        currentPresetName = n;
    }

    // MIDI Learn: controller number -> parameter index (global, shared by all instances)
    static constexpr int kNumControllers = 128;
    std::atomic<int> midiLearnParam { -1 };     // armed parameter (index), -1 = none
    std::atomic<bool> midiEnabled { true };
    int getControllerForParameter (int paramIndex) const noexcept;
    int getParameterForController (int cc) const noexcept { return midiMap[(size_t) (cc & 127)].load(); }
    int getParameterIndex (const juce::String& paramID) const;
    void clearMidiMapping (int cc); // -1 = all
    void saveMidiMapping();
    void revertMidiMapping();

    // interface size for new instances (global)
    static void setDefaultEditorWidth (int width);

    // favourite presets (global, shared by all instances)
    static juce::StringArray getFavouritePresets();
    static void setFavouritePresets (const juce::StringArray& keys);

    // IR import (message thread)
    bool importImpulseResponse (const juce::File& file, juce::String& message);

    // Lock Mix: keeps the mix value when presets are loaded. The setting is remembered for new instances.
    bool isMixLocked() const noexcept { return lockMix.load(); }
    void setMixLocked (bool shouldLock);

    // A / B comparison
    int getABSlot() const noexcept { return abSlot.load(); }
    void selectABSlot (int slot);
    void copyCurrentToOtherSlot();

    // UI preferences (stored with the session)
    int analyzerMode = 0;   // 0 = Reverb+Post, 1 = Pre+Post, 2 = Off
    int displayRange = 1;   // 0 = 30 dB, 1 = 18 dB, 2 = 9 dB
    bool showPiano = false;
    bool showMeter = true;
    int editorWidth = 1000;

    // applies parameter values given in real units; unlisted parameters are reset to their defaults if requested
    void applyValues (const std::vector<std::pair<juce::String, float>>& values, bool resetOthers, bool respectMixLock);

private:
    void applyStateTree (const juce::ValueTree& tree, bool respectMixLock);
    void snapshotPresetState();
    std::vector<float> presetSnapshot;
    std::array<std::atomic<int>, kNumControllers> midiMap;

    params::Cache cache;
    nh::ReverbEngine engine;
    nh::EngineParams engineParams;
    std::vector<float> monoScratch;

    std::atomic<bool> lockMix { false };
    juce::String currentPresetName { "Default" };
    int currentProgram = 0;

    juce::ValueTree abStates[2];
    std::atomic<int> abSlot { 0 };
    juce::CriticalSection stateLock; // preset name + A/B slots (hosts may save/restore from any thread)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NachhallAudioProcessor)
};
