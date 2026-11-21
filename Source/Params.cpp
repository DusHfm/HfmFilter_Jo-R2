#include "Params.h"

namespace ids
{
juce::String decay (int bandIndex, const char* field) { return "d" + juce::String (bandIndex + 1) + field; }
juce::String post (int bandIndex, const char* field) { return "p" + juce::String (bandIndex + 1) + field; }
} // namespace ids

namespace params
{
namespace
{
    using Layout = juce::AudioProcessorValueTreeState::ParameterLayout;
    using FloatAttr = juce::AudioParameterFloatAttributes;

    juce::NormalisableRange<float> logRange (float minV, float maxV)
    {
        return { minV, maxV,
                 [] (float start, float end, float v) { return start * std::pow (end / start, v); },
                 [] (float start, float end, float x) { return std::log (juce::jlimit (start, end, x) / start) / std::log (end / start); },
                 [] (float start, float end, float x) { return juce::jlimit (start, end, x); } };
    }

    juce::String pct (float v) { return juce::String (juce::roundToInt (v)) + " %"; }

    float parseNumber (const juce::String& t) { return t.retainCharacters ("0123456789.,-+").replaceCharacter (',', '.').getFloatValue(); }

    juce::String formatDb (float v, int decimals = 1)
    {
        const auto s = juce::String (std::abs (v), decimals);
        return (v > 0.0005f ? "+" : (v < -0.0005f ? "-" : "")) + s + " dB";
    }

    juce::String formatPan (float v)
    {
        const int p = juce::roundToInt (v * 100.0f);
        if (p == 0)
            return "C";
        return (p < 0 ? "L " : "R ") + juce::String (std::abs (p));
    }

    float parsePan (const juce::String& t)
    {
        const auto s = t.trim().toUpperCase();
        if (s.startsWith ("C"))
            return 0.0f;
        const float v = parseNumber (s) / 100.0f;
        return s.startsWith ("L") ? -std::abs (v) : std::abs (v);
    }

    juce::String formatMs (float ms)
    {
        if (ms >= 1000.0f)
            return juce::String (ms / 1000.0f, 2) + " s";
        if (ms >= 100.0f)
            return juce::String (juce::roundToInt (ms)) + " ms";
        return juce::String (ms, 1) + " ms";
    }

    float parseMs (const juce::String& t)
    {
        const float v = parseNumber (t);
        const auto s = t.trim().toLowerCase();
        if (s.endsWith ("s") && ! s.endsWith ("ms"))
            return v * 1000.0f;
        return v;
    }

    // Short parameter names (max. 8 characters) for small hardware displays such as NI MASCHINE.
    // JUCE's VST3 wrapper asks for getName (8) to fill the VST3 "shortTitle".
    juce::String shortNameFor (const juce::String& id)
    {
        static const char* const table[][2] = {
            { "space", "Space" },         { "decayRate", "Decay" },      { "mix", "Mix" },
            { "predelay", "Predelay" },   { "character", "Charactr" },   { "distance", "Distance" },
            { "brightness", "Bright" },   { "width", "Width" },          { "thickness", "Thick" },
            { "style", "Style" },         { "ducking", "Ducking" },      { "freeze", "Freeze" },
            { "gate", "Gate" },           { "gateHold", "GateHold" },    { "predelaySync", "PD Sync" },
            { "predelayOffset", "PD Offs" }, { "inGain", "In Lvl" },     { "inPan", "In Pan" },
            { "outGain", "Out Lvl" },     { "outPan", "Out Pan" },       { "gateSync", "GateSync" },
            { "eqAutoGain", "EQ Auto" },  { "bypass", "Bypass" },
        };
        for (const auto& e : table)
            if (id == e[0])
                return e[1];
        // band parameters: "d1Freq" -> "D1 Freq", "p3Gain" -> "P3 Gain"
        if (id.length() >= 3 && (id[0] == 'd' || id[0] == 'p') && juce::CharacterFunctions::isDigit (id[1]))
            return juce::String::charToString (juce::CharacterFunctions::toUpperCase (id[0])) + juce::String::charToString (id[1]) + " "
                   + id.substring (2);
        return {};
    }

    juce::String shorten (const juce::String& full, const juce::String& shortName, int maxLen)
    {
        if (full.length() <= maxLen || shortName.isEmpty())
            return full.substring (0, maxLen);
        return shortName.substring (0, maxLen);
    }

    struct NhFloat : public juce::AudioParameterFloat
    {
        using juce::AudioParameterFloat::AudioParameterFloat;
        juce::String getName (int maxLen) const override
        {
            return shorten (juce::AudioParameterFloat::getName (1024), shortNameFor (getParameterID()), maxLen);
        }
    };

    struct NhChoice : public juce::AudioParameterChoice
    {
        using juce::AudioParameterChoice::AudioParameterChoice;
        juce::String getName (int maxLen) const override
        {
            return shorten (juce::AudioParameterChoice::getName (1024), shortNameFor (getParameterID()), maxLen);
        }
    };

    struct NhBool : public juce::AudioParameterBool
    {
        using juce::AudioParameterBool::AudioParameterBool;
        juce::String getName (int maxLen) const override
        {
            return shorten (juce::AudioParameterBool::getName (1024), shortNameFor (getParameterID()), maxLen);
        }
    };

    void addFloat (Layout& layout, const juce::String& id, const juce::String& name, juce::NormalisableRange<float> range, float def,
                   std::function<juce::String (float)> toText, std::function<float (const juce::String&)> fromText,
                   const juce::String& label = {}, bool automatable = true)
    {
        auto attr = FloatAttr()
                        .withStringFromValueFunction ([toText] (float v, int) { return toText (v); })
                        .withValueFromStringFunction ([fromText] (const juce::String& s) { return fromText (s); })
                        .withLabel (label)
                        .withAutomatable (automatable);
        layout.add (std::make_unique<NhFloat> (juce::ParameterID { id, 1 }, name, range, def, attr));
    }

    void addPercent (Layout& layout, const juce::String& id, const juce::String& name, float def, float maxV = 100.0f)
    {
        addFloat (layout, id, name, juce::NormalisableRange<float> (0.0f, maxV, 0.01f), def, pct, parseNumber, "%");
    }

    // -100 ... +100 % with 0 % in the middle (Brightness, Thickness - as in Pro-R 2)
    void addBipolar (Layout& layout, const juce::String& id, const juce::String& name)
    {
        addFloat (layout, id, name, juce::NormalisableRange<float> (-100.0f, 100.0f, 0.01f), 0.0f,
                  [] (float v)
                  {
                      const int r = juce::roundToInt (v);
                      return (r > 0 ? "+" : "") + juce::String (r) + " %";
                  },
                  parseNumber, "%");
    }

    void addChoice (Layout& layout, const juce::String& id, const juce::String& name, const juce::StringArray& choices, int def, bool automatable = true)
    {
        layout.add (std::make_unique<NhChoice> (juce::ParameterID { id, 1 }, name, choices, def,
                                                 juce::AudioParameterChoiceAttributes().withAutomatable (automatable)));
    }

    void addBool (Layout& layout, const juce::String& id, const juce::String& name, bool def, bool automatable = true)
    {
        layout.add (std::make_unique<NhBool> (juce::ParameterID { id, 1 }, name, def,
                                               juce::AudioParameterBoolAttributes().withAutomatable (automatable)));
    }
} // namespace

//==============================================================================
juce::String formatFrequency (float hz)
{
    if (hz >= 10000.0f)
        return juce::String (hz / 1000.0f, 1) + " kHz";
    if (hz >= 1000.0f)
        return juce::String (hz / 1000.0f, 2) + " kHz";
    if (hz >= 100.0f)
        return juce::String (juce::roundToInt (hz)) + " Hz";
    return juce::String (hz, 1) + " Hz";
}

juce::String noteNameForFrequency (float hz, bool withCents)
{
    if (hz <= 0.0f)
        return {};
    static const char* names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const double midi = 69.0 + 12.0 * std::log2 (hz / 440.0);
    const int n = (int) std::lround (midi);
    const int cents = (int) std::lround ((midi - n) * 100.0);
    const int octave = n / 12 - 1;
    juce::String s = juce::String (names[((n % 12) + 12) % 12]) + juce::String (octave);
    if (withCents && cents != 0)
        s << (cents > 0 ? " +" : " ") << cents;
    return s;
}

float parseFrequency (const juce::String& text)
{
    auto t = text.trim();
    // musical note, e.g. "A4", "C#2+13", "D#5 -20"
    if (t.isNotEmpty() && juce::String ("ABCDEFGabcdefg").containsChar (t[0]))
    {
        static const int semis[7] = { 9, 11, 0, 2, 4, 5, 7 }; // A B C D E F G
        const int letter = juce::String ("ABCDEFG").indexOfChar (juce::CharacterFunctions::toUpperCase (t[0]));
        int idx = 1;
        int semi = semis[letter];
        if (idx < t.length() && (t[idx] == '#' || t[idx] == 'b'))
        {
            semi += t[idx] == '#' ? 1 : -1;
            ++idx;
        }
        juce::String rest = t.substring (idx).trim();
        int octave = 4;
        double cents = 0.0;
        int signPos = -1;
        for (int i = 1; i < rest.length(); ++i)
            if (rest[i] == '+' || rest[i] == '-')
            {
                signPos = i;
                break;
            }
        if (signPos > 0)
        {
            octave = rest.substring (0, signPos).trim().getIntValue();
            cents = rest.substring (signPos).trim().getDoubleValue();
        }
        else if (rest.isNotEmpty())
            octave = rest.getIntValue();
        const double midi = (octave + 1) * 12 + semi + cents / 100.0;
        return (float) (440.0 * std::pow (2.0, (midi - 69.0) / 12.0));
    }
    float v = parseNumber (t);
    if (t.containsIgnoreCase ("k"))
        v *= 1000.0f;
    return v;
}

juce::String roomDescription (float space) { return nh::roomModels()[nh::nearestRoomIndex (space)].name; }

float upgradeLegacyValue (const juce::String& paramID, float value)
{
    if (paramID == ids::space)
        return nh::spaceForT60 (nh::legacySpaceToT60 (value)); // keep the displayed decay time
    if (paramID == ids::brightness || paramID == ids::thickness)
        return juce::jlimit (-100.0f, 100.0f, (value - 50.0f) * 2.0f);
    if (paramID == ids::width)
        return juce::jmin (value, 120.0f);
    return value;
}

void upgradeLegacyState (juce::ValueTree& state)
{
    if ((int) state.getProperty ("nhVersion", 1) >= kStateVersion)
        return;
    for (auto child : state)
        if (child.hasType ("PARAM") && child.hasProperty ("value"))
            child.setProperty ("value", upgradeLegacyValue (child.getProperty ("id").toString(), (float) child.getProperty ("value")), nullptr);
    state.setProperty ("nhVersion", kStateVersion, nullptr);
}

double predelaySyncBeats (int choice)
{
    switch (choice)
    {
        case 1: return 1.0;
        case 2: return 0.5;
        case 3: return 0.25;
        case 4: return 0.125;
        default: return 0.0;
    }
}

double gateSyncBeats (int choice)
{
    switch (choice)
    {
        case 1: return 0.125;
        case 2: return 0.25;
        case 3: return 0.5;
        case 4: return 1.0;
        case 5: return 2.0;
        case 6: return 4.0;
        case 7: return 8.0;
        default: return 0.0;
    }
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    Layout layout;

    // ---- Page 1 (Maschine): the eight most important controls
    addFloat (layout, ids::space, "Space", juce::NormalisableRange<float> (0.0f, 1.0f, 0.0001f), 0.5f,
              [] (float v) { return roomDescription (v) + " " + juce::String (nh::interpolateRoom (v).t60, 2) + " s"; },
              [] (const juce::String& s)
              {
                  // a room name ("Cathedral") or a decay time in seconds ("2.5 s")
                  for (int i = 0; i < nh::kNumRoomModels; ++i)
                      if (s.trim().equalsIgnoreCase (nh::roomModels()[i].name))
                          return nh::spaceForRoomIndex (i);
                  const float t = parseNumber (s);
                  return t > 0.0f ? nh::spaceForT60 (t) : 0.5f;
              });
    addFloat (layout, ids::decayRate, "Decay Rate", logRange (25.0f, 400.0f), 100.0f, pct, parseNumber, "%");
    addPercent (layout, ids::mix, "Mix", 22.5f); // start values as in Pro-R 2
    {
        juce::NormalisableRange<float> r (0.0f, 500.0f, 0.01f);
        r.setSkewForCentre (60.0f);
        addFloat (layout, ids::predelay, "Predelay", r, 0.0f, formatMs, parseMs, "ms");
    }
    addPercent (layout, ids::character, "Character", 30.0f);
    addPercent (layout, ids::distance, "Distance", 50.0f);
    addBipolar (layout, ids::brightness, "Brightness");
    addPercent (layout, ids::width, "Stereo Width", 70.0f, 120.0f);

    // ---- Page 2
    addBipolar (layout, ids::thickness, "Thickness");
    addChoice (layout, ids::style, "Style", styleChoices(), 0);
    addFloat (layout, ids::ducking, "Ducking", juce::NormalisableRange<float> (0.0f, 30.0f, 0.01f), 0.0f,
              [] (float v) { return v < 0.05f ? juce::String ("Off") : "-" + juce::String (v, 1) + " dB"; },
              [] (const juce::String& s) { return std::abs (parseNumber (s)); }, "dB");
    addBool (layout, ids::freeze, "Freeze", false);
    addBool (layout, ids::gate, "Auto Gate", false);
    addFloat (layout, ids::gateHold, "Gate Hold", logRange (10.0f, 3000.0f), 250.0f, formatMs, parseMs, "ms");
    addChoice (layout, ids::predelaySync, "Predelay Sync", predelaySyncChoices(), 0);
    addFloat (layout, ids::predelayOffset, "Predelay Offset", logRange (50.0f, 200.0f), 100.0f, pct, parseNumber, "%");

    // ---- Page 3
    addFloat (layout, ids::inGain, "Input Level", juce::NormalisableRange<float> (-36.0f, 36.0f, 0.01f), 0.0f,
              [] (float v) { return formatDb (v); }, parseNumber, "dB");
    addFloat (layout, ids::inPan, "Input Pan", juce::NormalisableRange<float> (-1.0f, 1.0f, 0.001f), 0.0f, formatPan, parsePan);
    addFloat (layout, ids::outGain, "Output Level", juce::NormalisableRange<float> (-36.0f, 36.0f, 0.01f), 0.0f,
              [] (float v) { return formatDb (v); }, parseNumber, "dB");
    addFloat (layout, ids::outPan, "Output Pan", juce::NormalisableRange<float> (-1.0f, 1.0f, 0.001f), 0.0f, formatPan, parsePan);
    addChoice (layout, ids::gateSync, "Gate Sync", gateSyncChoices(), 0);
    addBool (layout, ids::eqAutoGain, "EQ Auto Gain", true);
    addBool (layout, ids::bypass, "Bypass", false);

    // ---- Decay Rate EQ bands
    for (int b = 0; b < nh::kNumDecayBands; ++b)
    {
        const juce::String n = "Decay EQ " + juce::String (b + 1) + " ";
        addBool (layout, ids::decay (b, "Used"), n + "Used", false, false);
        addBool (layout, ids::decay (b, "On"), n + "Enabled", true);
        addChoice (layout, ids::decay (b, "Shape"), n + "Shape", decayShapeChoices(), 0);
        addFloat (layout, ids::decay (b, "Freq"), n + "Frequency", logRange (10.0f, 30000.0f), kDefaultBandFreqs[b],
                  [] (float v) { return formatFrequency (v); }, parseFrequency, "Hz");
        addFloat (layout, ids::decay (b, "Rate"), n + "Decay Rate", logRange (25.0f, 400.0f), 100.0f, pct, parseNumber, "%");
        addFloat (layout, ids::decay (b, "Q"), n + "Q", logRange (0.025f, 40.0f), 1.0f,
                  [] (float v) { return juce::String (v, v < 1.0f ? 3 : 2); }, parseNumber);
    }

    // ---- Post EQ bands
    for (int b = 0; b < nh::kNumPostBands; ++b)
    {
        const juce::String n = "Post EQ " + juce::String (b + 1) + " ";
        addBool (layout, ids::post (b, "Used"), n + "Used", false, false);
        addBool (layout, ids::post (b, "On"), n + "Enabled", true);
        addChoice (layout, ids::post (b, "Shape"), n + "Shape", postShapeChoices(), 0);
        addFloat (layout, ids::post (b, "Freq"), n + "Frequency", logRange (10.0f, 30000.0f), kDefaultBandFreqs[b],
                  [] (float v) { return formatFrequency (v); }, parseFrequency, "Hz");
        addFloat (layout, ids::post (b, "Gain"), n + "Gain", juce::NormalisableRange<float> (-30.0f, 30.0f, 0.01f), 0.0f,
                  [] (float v) { return formatDb (v); }, parseNumber, "dB");
        addFloat (layout, ids::post (b, "Q"), n + "Q", logRange (0.025f, 40.0f), 1.0f,
                  [] (float v) { return juce::String (v, v < 1.0f ? 3 : 2); }, parseNumber);
        addChoice (layout, ids::post (b, "Slope"), n + "Slope", slopeChoices(), 3);
        addChoice (layout, ids::post (b, "Place"), n + "Stereo Placement", placementChoices(), 0);
    }

    return layout;
}

//==============================================================================
Cache::Cache (juce::AudioProcessorValueTreeState& s) : apvts (s)
{
    space = get (ids::space);
    decayRate = get (ids::decayRate);
    mix = get (ids::mix);
    predelay = get (ids::predelay);
    character = get (ids::character);
    distance = get (ids::distance);
    brightness = get (ids::brightness);
    width = get (ids::width);
    thickness = get (ids::thickness);
    style = get (ids::style);
    ducking = get (ids::ducking);
    freeze = get (ids::freeze);
    gate = get (ids::gate);
    gateHold = get (ids::gateHold);
    predelaySync = get (ids::predelaySync);
    predelayOffset = get (ids::predelayOffset);
    inGain = get (ids::inGain);
    inPan = get (ids::inPan);
    outGain = get (ids::outGain);
    outPan = get (ids::outPan);
    gateSync = get (ids::gateSync);
    eqAutoGain = get (ids::eqAutoGain);
    bypass = get (ids::bypass);

    for (int b = 0; b < nh::kNumDecayBands; ++b)
    {
        auto& d = decayBands[b];
        d.used = get (ids::decay (b, "Used"));
        d.on = get (ids::decay (b, "On"));
        d.shape = get (ids::decay (b, "Shape"));
        d.freq = get (ids::decay (b, "Freq"));
        d.rate = get (ids::decay (b, "Rate"));
        d.q = get (ids::decay (b, "Q"));
    }
    for (int b = 0; b < nh::kNumPostBands; ++b)
    {
        auto& p = postBands[b];
        p.used = get (ids::post (b, "Used"));
        p.on = get (ids::post (b, "On"));
        p.shape = get (ids::post (b, "Shape"));
        p.freq = get (ids::post (b, "Freq"));
        p.gain = get (ids::post (b, "Gain"));
        p.q = get (ids::post (b, "Q"));
        p.slope = get (ids::post (b, "Slope"));
        p.place = get (ids::post (b, "Place"));
    }
}

std::atomic<float>* Cache::get (const juce::String& id) const
{
    auto* p = apvts.getRawParameterValue (id);
    jassert (p != nullptr);
    return p;
}

void Cache::read (nh::EngineParams& p, double bpm) const noexcept
{
    const double beatSeconds = 60.0 / juce::jlimit (20.0, 999.0, bpm > 0.0 ? bpm : 120.0);

    p.space = space->load();
    p.decayRate = decayRate->load() * 0.01f;
    p.mix = mix->load() * 0.01f;
    p.character = character->load() * 0.01f;
    p.distance = distance->load() * 0.01f;
    p.brightness = 0.5f + brightness->load() * 0.005f; // engine: 0 ... 1 with the neutral point at 0.5
    p.width = width->load() * 0.01f;
    p.thickness = 0.5f + thickness->load() * 0.005f;
    p.style = juce::jlimit (0, 2, (int) std::lround (style->load()));
    p.duckingDb = ducking->load();
    p.freeze = freeze->load() > 0.5f;
    p.gateOn = gate->load() > 0.5f;
    p.inGainDb = inGain->load();
    p.inPan = inPan->load();
    p.outGainDb = outGain->load();
    p.outPan = outPan->load();
    p.eqAutoGain = eqAutoGain->load() > 0.5f;
    p.bypass = bypass->load() > 0.5f;

    const int pdSync = (int) std::lround (predelaySync->load());
    if (pdSync > 0)
        p.predelayMs = (float) (predelaySyncBeats (pdSync) * beatSeconds * 1000.0 * predelayOffset->load() * 0.01);
    else
        p.predelayMs = predelay->load();
    p.predelayMs = juce::jlimit (0.0f, (float) (nh::ReverbEngine::kMaxPredelaySeconds * 1000.0 - 10.0), p.predelayMs);

    const int gSync = (int) std::lround (gateSync->load());
    p.gateHoldMs = gSync > 0 ? (float) (gateSyncBeats (gSync) * beatSeconds * 1000.0) : gateHold->load();

    for (int b = 0; b < nh::kNumDecayBands; ++b)
    {
        const auto& s = decayBands[b];
        auto& d = p.decayBands[b];
        d.used = s.used->load() > 0.5f;
        d.enabled = s.on->load() > 0.5f;
        d.shape = juce::jlimit (0, 3, (int) std::lround (s.shape->load()));
        d.freq = s.freq->load();
        d.rate = s.rate->load() * 0.01f;
        d.q = s.q->load();
    }
    for (int b = 0; b < nh::kNumPostBands; ++b)
    {
        const auto& s = postBands[b];
        auto& d = p.postBands[b];
        d.used = s.used->load() > 0.5f;
        d.enabled = s.on->load() > 0.5f;
        d.shape = juce::jlimit (0, 4, (int) std::lround (s.shape->load()));
        d.freq = s.freq->load();
        d.gain = s.gain->load();
        d.q = s.q->load();
        d.slope = juce::jlimit (0, nh::kNumSlopes - 1, (int) std::lround (s.slope->load()));
        d.placement = juce::jlimit (0, 4, (int) std::lround (s.place->load()));
    }
}

} // namespace params
