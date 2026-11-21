// Nachhall - factory presets
#pragma once

#include <juce_core/juce_core.h>
#include <utility>
#include <vector>

struct FactoryPreset
{
    juce::String name;
    juce::String category;
    std::vector<std::pair<juce::String, float>> values; // parameter id -> value (real units)
};

const std::vector<FactoryPreset>& factoryPresets();

// searchable tags of a factory preset (derived from its settings and name)
juce::StringArray factoryPresetTags (const FactoryPreset& preset);

// metadata of a preset file
struct PresetInfo
{
    juce::String author, description;
    juce::StringArray tags;
};
