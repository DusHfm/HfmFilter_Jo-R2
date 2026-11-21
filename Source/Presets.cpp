#include "Presets.h"
#include "Params.h"

#include "dsp/RoomModels.h"

namespace
{
// Space values of the room models (index / 14)
constexpr float kAmbience = 0.0f, kSmallRoom = 1.0f / 14.0f, kRoom = 2.0f / 14.0f, kStudio = 3.0f / 14.0f, kChamber = 4.0f / 14.0f,
                kLargeRoom = 5.0f / 14.0f, kSmallHall = 6.0f / 14.0f, kHall = 7.0f / 14.0f, kConcertHall = 8.0f / 14.0f,
                kLargeHall = 9.0f / 14.0f, kChurch = 10.0f / 14.0f, kArena = 11.0f / 14.0f, kCathedral = 12.0f / 14.0f,
                kHugeSpace = 13.0f / 14.0f, kAbyss = 1.0f;

using V = std::vector<std::pair<juce::String, float>>;

// helpers to describe bands compactly
V postBand (int n, int shape, float freq, float gain = 0.0f, float q = 1.0f, int slope = 3)
{
    const juce::String p = "p" + juce::String (n);
    return { { p + "Used", 1.0f }, { p + "On", 1.0f }, { p + "Shape", (float) shape }, { p + "Freq", freq },
             { p + "Gain", gain },  { p + "Q", q },     { p + "Slope", (float) slope } };
}

V decayBand (int n, int shape, float freq, float ratePercent, float q = 1.0f)
{
    const juce::String p = "d" + juce::String (n);
    return { { p + "Used", 1.0f }, { p + "On", 1.0f }, { p + "Shape", (float) shape },
             { p + "Freq", freq }, { p + "Rate", ratePercent }, { p + "Q", q } };
}

V join (std::initializer_list<V> parts)
{
    V out;
    for (const auto& p : parts)
        out.insert (out.end(), p.begin(), p.end());
    return out;
}

enum
{
    Bell = 0,
    LowShelf = 1,
    HighShelf = 2,
    LowCut = 3,
    HighCut = 4,
    Notch = 3
};
} // namespace

// The factory presets are written in the units of Nachhall 1.0 (15 room models, Brightness and
// Thickness 0 ... 100 %) and converted once.
std::vector<FactoryPreset> upgraded (std::vector<FactoryPreset> presets)
{
    for (auto& p : presets)
        for (auto& v : p.values)
            v.second = params::upgradeLegacyValue (v.first, v.second);
    return presets;
}

const std::vector<FactoryPreset>& factoryPresets()
{
    static const std::vector<FactoryPreset> presets = upgraded ({
        { "Default", "Basics", {} },

        // ---------------------------------------------------------------- Rooms
        { "Small Drum Room", "Rooms",
          join ({ { { "space", 0.11f }, { "character", 35 }, { "distance", 30 }, { "brightness", 55 }, { "thickness", 60 },
                    { "width", 80 }, { "predelay", 2 }, { "mix", 25 } },
                  postBand (1, LowCut, 80.0f, 0, 0.7f, 1) }) },
        { "Vocal Booth", "Rooms", { { "space", 0.03f }, { "distance", 20 }, { "brightness", 45 }, { "character", 20 }, { "mix", 18 } } },
        { "Live Room", "Rooms", { { "space", 0.22f }, { "character", 45 }, { "distance", 45 }, { "brightness", 55 }, { "width", 90 }, { "mix", 25 } } },
        { "Wooden Studio", "Rooms",
          join ({ { { "space", kStudio }, { "brightness", 40 }, { "character", 40 }, { "thickness", 55 }, { "mix", 22 } },
                  decayBand (1, HighShelf, 4000.0f, 70.0f, 0.7f) }) },
        { "Chamber", "Rooms", { { "space", kChamber }, { "character", 50 }, { "distance", 50 }, { "mix", 25 } } },
        { "Bright Room", "Rooms", { { "space", 0.18f }, { "brightness", 80 }, { "character", 30 }, { "mix", 22 } } },
        { "Snare Room", "Rooms",
          join ({ { { "space", kChamber }, { "decayRate", 70 }, { "thickness", 75 }, { "character", 55 }, { "distance", 25 }, { "brightness", 60 } },
                  postBand (1, LowCut, 100.0f, 0, 0.7f, 3), postBand (2, Bell, 200.0f, 2.0f, 1.0f) }) },

        // ---------------------------------------------------------------- Halls
        { "Small Hall", "Halls", { { "space", kSmallHall }, { "distance", 45 }, { "predelay", 12 } } },
        { "Concert Hall", "Halls", { { "space", kConcertHall }, { "distance", 55 }, { "character", 40 }, { "brightness", 45 }, { "predelay", 20 } } },
        { "Large Hall", "Halls", { { "space", kLargeHall }, { "distance", 60 }, { "predelay", 25 } } },
        { "Vocal Hall", "Halls",
          join ({ { { "space", kHall }, { "decayRate", 90 }, { "predelay", 60 }, { "brightness", 55 }, { "character", 45 }, { "ducking", 6 } },
                  postBand (1, LowCut, 150.0f, 0, 0.7f, 2), postBand (2, HighShelf, 8000.0f, -3.0f, 0.7f) }) },
        { "Dark Hall", "Halls", { { "space", 0.55f }, { "brightness", 25 }, { "character", 35 }, { "distance", 60 } } },
        { "Bright Hall", "Halls", { { "space", kHall }, { "brightness", 80 }, { "character", 45 } } },
        { "Orchestral Hall", "Halls",
          join ({ { { "space", 0.6f }, { "distance", 70 }, { "character", 35 }, { "width", 60 }, { "predelay", 15 } },
                  decayBand (1, LowShelf, 200.0f, 130.0f, 0.7f) }) },

        // ---------------------------------------------------------------- Large spaces
        { "Church", "Large Spaces", { { "space", kChurch }, { "distance", 60 }, { "brightness", 45 } } },
        { "Cathedral", "Large Spaces", { { "space", kCathedral }, { "distance", 70 }, { "brightness", 40 }, { "predelay", 30 } } },
        { "Arena", "Large Spaces", { { "space", kArena }, { "distance", 65 }, { "character", 55 }, { "predelay", 40 } } },
        { "Huge Space", "Large Spaces", { { "space", kHugeSpace }, { "distance", 80 }, { "brightness", 45 }, { "character", 50 } } },
        { "Endless", "Large Spaces", { { "space", kAbyss }, { "decayRate", 300 }, { "character", 60 }, { "mix", 35 } } },

        // ---------------------------------------------------------------- Plates
        { "Classic Plate", "Plates", { { "style", 2 }, { "space", 0.45f }, { "brightness", 60 }, { "character", 40 }, { "thickness", 55 }, { "predelay", 15 }, { "mix", 25 } } },
        { "Vocal Plate", "Plates",
          join ({ { { "style", 2 }, { "space", 0.4f }, { "brightness", 65 }, { "predelay", 40 }, { "ducking", 4 } },
                  postBand (1, LowCut, 200.0f, 0, 0.7f, 2) }) },
        { "Snare Plate", "Plates", { { "style", 2 }, { "space", 0.3f }, { "decayRate", 80 }, { "brightness", 70 }, { "thickness", 70 } } },
        { "Dark Plate", "Plates", { { "style", 2 }, { "space", 0.5f }, { "brightness", 25 } } },
        { "Long Plate", "Plates", { { "style", 2 }, { "space", 0.7f }, { "character", 50 } } },

        // ---------------------------------------------------------------- Vintage
        { "80s Hall", "Vintage", { { "style", 1 }, { "space", 0.55f }, { "character", 60 }, { "brightness", 45 }, { "predelay", 20 } } },
        { "Vintage Room", "Vintage", { { "style", 1 }, { "space", 0.25f }, { "character", 45 } } },
        { "Digital Chamber", "Vintage", { { "style", 1 }, { "space", 0.35f }, { "brightness", 55 }, { "character", 55 } } },
        { "Lush Vintage Space", "Vintage", { { "style", 1 }, { "space", 0.8f }, { "character", 70 }, { "width", 120 } } },

        // ---------------------------------------------------------------- Instruments
        { "Vocal Ambience", "Instruments", { { "space", 0.08f }, { "mix", 20 }, { "brightness", 60 }, { "predelay", 5 } } },
        { "Piano Hall", "Instruments",
          join ({ { { "space", 0.55f }, { "distance", 55 }, { "brightness", 45 }, { "character", 30 } },
                  decayBand (1, HighShelf, 6000.0f, 70.0f, 0.7f) }) },
        { "Guitar Room", "Instruments", { { "space", 0.25f }, { "character", 40 }, { "width", 100 } } },
        { "Pad Wash", "Instruments", { { "space", 0.9f }, { "decayRate", 200 }, { "character", 85 }, { "brightness", 55 }, { "width", 150 }, { "mix", 40 } } },
        { "Kick Room", "Instruments",
          join ({ { { "space", kSmallRoom }, { "decayRate", 80 }, { "thickness", 70 }, { "brightness", 40 }, { "mix", 15 } },
                  decayBand (1, LowShelf, 120.0f, 60.0f, 0.7f) }) },

        // ---------------------------------------------------------------- Special
        { "Gated Snare", "Special", { { "space", 0.35f }, { "decayRate", 150 }, { "gate", 1 }, { "gateHold", 180 }, { "thickness", 80 }, { "brightness", 65 } } },
        { "Ducked Vocal Hall", "Special", { { "space", 0.55f }, { "ducking", 12 }, { "predelay", 50 } } },
        { "Freeze Pad", "Special", { { "space", 0.9f }, { "character", 70 }, { "mix", 50 } } },
        { "Chorus Space", "Special", { { "space", 0.6f }, { "character", 100 } } },
        { "Mono Room", "Special", { { "space", 0.2f }, { "width", 0 } } },
        { "Wide Ambience", "Special", { { "space", 0.1f }, { "width", 160 } } },
        { "Telephone Verb", "Special",
          join ({ { { "space", 0.4f }, { "brightness", 70 } }, postBand (1, LowCut, 400.0f, 0, 0.7f, 3), postBand (2, HighCut, 3000.0f, 0, 0.7f, 3) }) },
        { "Tamed Ringing", "Special",
          join ({ { { "space", 0.6f } }, decayBand (1, Notch, 400.0f, 100.0f, 4.0f), decayBand (2, Bell, 2500.0f, 60.0f, 1.5f) }) },
        { "Synced Echo Space", "Special", { { "space", 0.55f }, { "predelaySync", 2 }, { "character", 60 } } },
    });
    return presets;
}

juce::StringArray factoryPresetTags (const FactoryPreset& preset)
{
    auto value = [&preset] (const char* id, float def)
    {
        for (const auto& v : preset.values)
            if (v.first == id)
                return v.second;
        return def;
    };
    juce::StringArray tags;
    tags.add (preset.category.toLowerCase());
    const int style = juce::roundToInt (value ("style", 0.0f));
    tags.add (style == 2 ? "plate" : (style == 1 ? "vintage" : "modern"));
    const float t60 = nh::interpolateRoom (value ("space", 0.5f)).t60 * value ("decayRate", 100.0f) * 0.01f;
    tags.add (t60 < 0.8f ? "short" : (t60 > 2.5f ? "long" : "medium"));
    const float bright = value ("brightness", 0.0f);
    if (bright <= -30.0f)
        tags.add ("dark");
    if (bright >= 30.0f)
        tags.add ("bright");
    const float width = value ("width", 70.0f);
    if (width < 1.0f)
        tags.add ("mono");
    if (width >= 110.0f)
        tags.add ("wide");
    if (value ("character", 30.0f) >= 80.0f)
        tags.add ("chorus");
    if (value ("gate", 0.0f) > 0.5f)
        tags.add ("gated");
    if (value ("ducking", 0.0f) > 0.5f)
        tags.add ("ducking");
    if (value ("predelaySync", 0.0f) > 0.5f)
        tags.add ("tempo sync");
    for (const char* word : { "vocal", "drum", "snare", "kick", "piano", "guitar", "pad", "orchestral" })
        if (preset.name.containsIgnoreCase (word))
            tags.add (word);
    return tags;
}
