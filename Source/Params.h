// Nachhall - parameter definitions
//
// The order of the parameters matters for NI Maschine: the hardware shows the plug-in
// parameters in pages of 8 knobs in exactly this order.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "dsp/ReverbEngine.h"

namespace ids
{
inline const juce::String space { "space" };
inline const juce::String decayRate { "decayRate" };
inline const juce::String mix { "mix" };
inline const juce::String predelay { "predelay" };
inline const juce::String character { "character" };
inline const juce::String distance { "distance" };
inline const juce::String brightness { "brightness" };
inline const juce::String width { "width" };
inline const juce::String thickness { "thickness" };
inline const juce::String style { "style" };
inline const juce::String ducking { "ducking" };
inline const juce::String freeze { "freeze" };
inline const juce::String gate { "gate" };
inline const juce::String gateHold { "gateHold" };
inline const juce::String predelaySync { "predelaySync" };
inline const juce::String predelayOffset { "predelayOffset" };
inline const juce::String inGain { "inGain" };
inline const juce::String inPan { "inPan" };
inline const juce::String outGain { "outGain" };
inline const juce::String outPan { "outPan" };
inline const juce::String gateSync { "gateSync" };
inline const juce::String eqAutoGain { "eqAutoGain" };
inline const juce::String bypass { "bypass" };

// band parameters: "d<n><Field>" (Decay Rate EQ) and "p<n><Field>" (Post EQ), n = 1..6
juce::String decay (int bandIndex, const char* field);
juce::String post (int bandIndex, const char* field);
} // namespace ids

namespace params
{
// (functions instead of static StringArrays: no static JUCE objects with leak detectors)
inline juce::StringArray styleChoices() { return { "Modern", "Vintage", "Plate" }; }
inline juce::StringArray predelaySyncChoices() { return { "Off", "1/4", "1/8", "1/16", "1/32" }; }
inline juce::StringArray gateSyncChoices() { return { "Off", "1/32", "1/16", "1/8", "1/4", "1/2", "1 Bar", "2 Bars" }; }
inline juce::StringArray decayShapeChoices() { return { "Bell", "Low Shelf", "High Shelf", "Notch" }; }
inline juce::StringArray postShapeChoices() { return { "Bell", "Low Shelf", "High Shelf", "Low Cut", "High Cut" }; }
inline juce::StringArray slopeChoices()
{
    return { "6 dB/oct", "12 dB/oct", "18 dB/oct", "24 dB/oct", "30 dB/oct", "36 dB/oct", "48 dB/oct", "72 dB/oct", "96 dB/oct" };
}
inline juce::StringArray placementChoices() { return { "Stereo", "Left", "Right", "Mid", "Side" }; }

constexpr float kDefaultBandFreqs[6] = { 80.0f, 250.0f, 800.0f, 2500.0f, 6000.0f, 12000.0f };

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

juce::String formatFrequency (float hz);
float parseFrequency (const juce::String& text);
juce::String noteNameForFrequency (float hz, bool withCents);
juce::String roomDescription (float space); // "Hall"

// Nachhall 1.0 stored Space for 15 room models and Brightness / Thickness as 0 ... 100 %.
// Since version 2 the room models and the scales follow Pro-R 2: this converts old values so that
// presets and projects keep their sound as far as possible.
constexpr int kStateVersion = 2;
float upgradeLegacyValue (const juce::String& paramID, float value);
void upgradeLegacyState (juce::ValueTree& state); // checks the "nhVersion" property itself

// Beats for the sync choices (quarter note = 1 beat)
double predelaySyncBeats (int choice);
double gateSyncBeats (int choice);

// Fast, lock-free access to all raw parameter values (audio thread)
class Cache
{
public:
    explicit Cache (juce::AudioProcessorValueTreeState& state);
    void read (nh::EngineParams& out, double bpm) const noexcept;

private:
    std::atomic<float>* get (const juce::String& id) const;
    juce::AudioProcessorValueTreeState& apvts;

    std::atomic<float>* space = nullptr;
    std::atomic<float>* decayRate = nullptr;
    std::atomic<float>* mix = nullptr;
    std::atomic<float>* predelay = nullptr;
    std::atomic<float>* character = nullptr;
    std::atomic<float>* distance = nullptr;
    std::atomic<float>* brightness = nullptr;
    std::atomic<float>* width = nullptr;
    std::atomic<float>* thickness = nullptr;
    std::atomic<float>* style = nullptr;
    std::atomic<float>* ducking = nullptr;
    std::atomic<float>* freeze = nullptr;
    std::atomic<float>* gate = nullptr;
    std::atomic<float>* gateHold = nullptr;
    std::atomic<float>* predelaySync = nullptr;
    std::atomic<float>* predelayOffset = nullptr;
    std::atomic<float>* inGain = nullptr;
    std::atomic<float>* inPan = nullptr;
    std::atomic<float>* outGain = nullptr;
    std::atomic<float>* outPan = nullptr;
    std::atomic<float>* gateSync = nullptr;
    std::atomic<float>* eqAutoGain = nullptr;
    std::atomic<float>* bypass = nullptr;

    struct DecayBand
    {
        std::atomic<float>*used, *on, *shape, *freq, *rate, *q;
    };
    struct PostBand
    {
        std::atomic<float>*used, *on, *shape, *freq, *gain, *q, *slope, *place;
    };
    DecayBand decayBands[nh::kNumDecayBands] {};
    PostBand postBands[nh::kNumPostBands] {};
};

} // namespace params
