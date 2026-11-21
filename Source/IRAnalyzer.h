// Nachhall - impulse response import: analyses a recorded IR and converts it into reverb settings
#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <utility>
#include <vector>

struct IRAnalysisResult
{
    bool ok = false;
    juce::String message;
    std::vector<std::pair<juce::String, float>> values; // parameter id -> value (real units)
};

IRAnalysisResult analyseImpulseResponse (const juce::AudioBuffer<float>& ir, double sampleRate);
