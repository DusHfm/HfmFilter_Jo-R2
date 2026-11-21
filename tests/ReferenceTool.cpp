// Nachhall - reference measurement tool (developer tool, no JUCE)
//
// Compares Nachhall with recordings of another reverb (e.g. FabFilter Pro-R 2) that were made with
// the test signals and the measurement plan in reference/.
//
//   NachhallReference signals <dir>                    writes the two test signals (48 kHz, 24 bit)
//   NachhallReference plan <file.csv> <file.md>        writes the measurement plan (CSV for Excel + Markdown)
//   NachhallReference checkplan <file.csv>             checks a (filled-in) plan against the tool
//   NachhallReference nachhall <dir> [--extra codes]   renders every plan entry through Nachhall
//   NachhallReference analyze <dir> [<dir2>] [--md <file>] [--csv <file>]
//                                                      measures the recordings in <dir>, optionally side by
//                                                      side with <dir2>; writes a Markdown report
//
// Only the audio output is measured (black box), exactly like a user recording impulse responses.
#include "TestHelpers.h"
#include "WavIO.h"
#include "../Source/dsp/Tuning.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>

using namespace testing;

namespace
{
constexpr int kFs = 48000;
constexpr float kImpulse = 0.5f;

// signal A: impulse left, impulse right
constexpr double kLenA = 21.0, kImpL = 1.0, kImpR = 11.0, kWindow = 9.9;
// signal B: 1 kHz tone, loud noise burst, the same burst 40 dB quieter, impulse left
constexpr double kLenB = 29.0, kTone0 = 1.0, kTone1 = 3.0, kNoiseLoud = 8.0, kNoiseQuiet = 15.0, kNoiseLen = 0.5, kImpB = 22.0;
constexpr double kQuietDb = -40.0;

const double kOctaves[] = { 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0, 16000.0 };
constexpr int kNumOctaves = 8;
const double kToneBands[] = { 63.0, 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0, 16000.0 };
constexpr int kNumToneBands = 9;

//==============================================================================
// Measurement plan
struct Row
{
    std::string file;    // without .wav
    char signal = 'A';   // 'A' or 'B'
    std::string need;    // "ja" / "ja, falls vorhanden" / "optional"
    std::string setting; // what to change in Pro-R (German, shown to the user)
    std::string code;    // the same as machine readable settings (for the Nachhall render)
    std::string group;
};

// Pro-R 2 as measured with the first probe recordings: 6 room icons around the Space knob,
// Brightness and Thickness -100 ... +100 % (default 0 %), Stereo Width up to 120 %.
// The plan codes use the units Pro-R shows: seconds for Space, percent for everything else.
const double kIconTimes[] = { 0.20, 0.75, 1.85, 3.20, 5.20, 10.0 };
constexpr int kNumIcons = 6;
constexpr int kBaseIcon = 3; // 1.85 s
const double kBetweenTimes[] = { 0.40, 1.2, 2.4, 4.0, 7.0 };

std::string seconds (double t) // "1,85 s"
{
    char b[24];
    std::snprintf (b, sizeof (b), t < 1.0 ? "%.2f s" : (t < 10.0 ? "%.2f s" : "%.1f s"), t);
    std::string r = b;
    for (auto& c : r)
        if (c == '.')
            c = ',';
    return r;
}

std::string g (double v) // shortest number text for the plan codes
{
    char b[24];
    std::snprintf (b, sizeof (b), "%g", v);
    return b;
}

std::string signedPct (int v) // "-100", "+50", "0"
{
    return v > 0 ? "+" + std::to_string (v) : std::to_string (v);
}

std::string fileValue (int v) // m100, m050, p050, p100
{
    char b[16];
    std::snprintf (b, sizeof (b), "%s%03d", v < 0 ? "m" : "p", std::abs (v));
    return b;
}

std::vector<Row> plan()
{
    std::vector<Row> p;
    auto add = [&p] (const std::string& file, char sig, const std::string& need, const std::string& setting, const std::string& code, const std::string& group)
    {
        Row r;
        r.file = file;
        r.signal = sig;
        r.need = need;
        r.setting = setting;
        r.code = code;
        r.group = group;
        p.push_back (r);
    };
    char code[96], file[48];

    add ("K_Kalibrierung", 'A', "ja", "Mix 0 % (prüft, ob der Export sauber ist)", "mix=0", "Kalibrierung");
    add ("K_Kalibrierung_B", 'B', "ja", "Mix 0 % mit Signal B (prüft laut/leise durch die Kette)", "mix=0", "Kalibrierung");
    add ("B_Basis_A", 'A', "ja", "Basis, nichts ändern", "", "Basis");
    add ("B_Basis_B", 'B', "ja", "Basis, nichts ändern", "", "Basis");

    // rooms: the 6 icons and the points between them, sorted by decay time
    for (int i = 0; i < kNumIcons; ++i)
    {
        std::snprintf (code, sizeof (code), "space=%g", kIconTimes[i]);
        std::snprintf (file, sizeof (file), "R%02d", i + 1);
        add (file, 'A', "ja", "Space: Raum-Symbol " + std::to_string (i + 1) + " anklicken (" + seconds (kIconTimes[i]) + ")", code, "Räume");
        if (i < kNumIcons - 1)
        {
            const double t = kBetweenTimes[i];
            std::snprintf (code, sizeof (code), "space=%g", t);
            std::snprintf (file, sizeof (file), "RZ_%04dms", (int) std::lround (t * 1000.0));
            add (file, 'A', "ja", "Space auf " + seconds (t) + " stellen (zwischen Symbol " + std::to_string (i + 1) + " und " + std::to_string (i + 2) + ")", code, "Räume");
        }
    }

    for (const char* style : { "Vintage", "Plate" })
    {
        const std::string st = style, lc = st == "Vintage" ? "vintage" : "plate";
        add ("S_" + st + "_Klein", 'A', "optional", "Style " + st + " + Raum-Symbol 2 (" + seconds (kIconTimes[1]) + ")", "style=" + lc + ",space=" + g (kIconTimes[1]), "Styles");
        add ("S_" + st + "_Basis", 'A', "ja", "Style " + st, "style=" + lc, "Styles");
        add ("S_" + st + "_Groesster", 'A', "ja", "Style " + st + " + Raum-Symbol 6 (" + seconds (kIconTimes[5]) + ")", "style=" + lc + ",space=" + g (kIconTimes[5]), "Styles");
    }

    for (const auto& d : { std::make_pair (25, true), std::make_pair (50, false), std::make_pair (200, false), std::make_pair (400, true) })
    {
        std::snprintf (file, sizeof (file), "D_DecayRate_%03d", d.first);
        std::snprintf (code, sizeof (code), "decay=%d", d.first);
        add (file, 'A', d.second ? "ja" : "optional", "Decay Rate " + std::to_string (d.first) + " %", code, "Decay Rate");
    }

    // knobs: 0 ... 100 % with the base at 50 %, or -100 ... +100 % with the base at 0 %
    struct Knob { const char* prefix; const char* name; const char* key; bool bipolar; bool withB; };
    const Knob knobs[] = { { "H", "Brightness", "brightness", true, false }, { "C", "Character", "character", false, true },
                           { "E", "Distance", "distance", false, false }, { "T", "Thickness", "thickness", true, true } };
    for (const auto& k : knobs)
    {
        const struct { int v; bool required; } unipolar[] = { { 0, true }, { 25, false }, { 75, false }, { 100, true } };
        const struct { int v; bool required; } bipolar[] = { { -100, true }, { -50, false }, { 50, false }, { 100, true } };
        for (int j = 0; j < 4; ++j)
        {
            const int v = k.bipolar ? bipolar[j].v : unipolar[j].v;
            const bool required = k.bipolar ? bipolar[j].required : unipolar[j].required;
            std::snprintf (file, sizeof (file), "%s_%s_%s", k.prefix, k.name, k.bipolar ? fileValue (v).c_str() : (v < 10 ? "00" + std::to_string (v) : (v < 100 ? "0" + std::to_string (v) : std::to_string (v))).c_str());
            std::snprintf (code, sizeof (code), "%s=%d", k.key, v);
            const std::string setting = std::string (k.name) + " " + (k.bipolar ? signedPct (v) : std::to_string (v)) + " %";
            if (k.withB)
            {
                add (std::string (file) + "_A", 'A', required ? "ja" : "optional", setting, code, k.name);
                if (required)
                    add (std::string (file) + "_B", 'B', "ja", setting, code, k.name);
            }
            else
                add (file, 'A', required ? "ja" : "optional", setting, code, k.name);
        }
    }

    add ("W_Width_000", 'A', "ja", "Stereo Width 0 %", "width=0", "Stereo Width");
    add ("W_Width_050", 'A', "ja", "Stereo Width 50 %", "width=50", "Stereo Width");
    add ("W_Width_070", 'A', "optional", "Stereo Width 70 % (Standardwert)", "width=70", "Stereo Width");
    add ("W_Width_120", 'A', "ja", "Stereo Width 120 % (Maximum)", "width=120", "Stereo Width");
    add ("X_Predelay_100ms", 'A', "optional", "Predelay 100 ms (Sync aus)", "predelay=100", "Extras");
    add ("X_DecayEQ_HighShelf", 'A', "optional", "Decay-Rate-EQ: High Shelf, 6 kHz, Wert 200 %, Q 1 (Standard)", "decayeq=highshelf:6000:2:1", "Extras");
    add ("X_DecayEQ_LowShelf", 'A', "optional", "Decay-Rate-EQ: Low Shelf, 200 Hz, Wert 50 %, Q 1 (Standard)", "decayeq=lowshelf:200:0.5:1", "Extras");
    add ("X_PostEQ_Bell", 'A', "optional", "Character 0 % + Post-EQ: Bell, 1 kHz, +12 dB, Q 1 (Auto Gain aus)", "character=0,autogain=0,posteq=bell:1000:12:1", "Extras");
    add ("X_PostEQ_LowCut", 'A', "optional", "Character 0 % + Post-EQ: Low Cut, 400 Hz, 24 dB/Okt. (Auto Gain aus)", "character=0,autogain=0,posteq=lowcut:400:0:0.7:24", "Extras");
    return p;
}

//==============================================================================
// Test signals
uint32_t lcg (uint32_t& s) noexcept
{
    s = s * 1664525u + 1013904223u;
    return s;
}

Stereo makeSignal (char which)
{
    const size_t n = (size_t) ((which == 'A' ? kLenA : kLenB) * kFs);
    Stereo s;
    s.l.assign (n, 0.0f);
    s.r.assign (n, 0.0f);
    if (which == 'A')
    {
        s.l[(size_t) (kImpL * kFs)] = kImpulse;
        s.r[(size_t) (kImpR * kFs)] = kImpulse;
        return s;
    }
    auto fade = [] (double t, double len, double f) // raised-cosine fade in/out
    {
        if (t < f)
            return 0.5 - 0.5 * std::cos (nh::kPi * t / f);
        if (t > len - f)
            return 0.5 - 0.5 * std::cos (nh::kPi * (len - t) / f);
        return 1.0;
    };
    const size_t t0 = (size_t) (kTone0 * kFs), t1 = (size_t) (kTone1 * kFs);
    for (size_t i = t0; i < t1; ++i)
    {
        const double t = (double) (i - t0) / kFs;
        const float v = (float) (0.25 * fade (t, kTone1 - kTone0, 0.05) * std::sin (2.0 * nh::kPi * 1000.0 * t));
        s.l[i] = s.r[i] = v;
    }
    uint32_t seed = 0x5eed1234u;
    const size_t len = (size_t) (kNoiseLen * kFs);
    const float quiet = (float) std::pow (10.0, kQuietDb / 20.0);
    for (size_t i = 0; i < len; ++i)
    {
        const double t = (double) i / kFs;
        const float v = (float) (0.35 * fade (t, kNoiseLen, 0.01) * ((double) (lcg (seed) >> 8) / 8388608.0 - 1.0));
        s.l[(size_t) (kNoiseLoud * kFs) + i] = s.r[(size_t) (kNoiseLoud * kFs) + i] = v;
        s.l[(size_t) (kNoiseQuiet * kFs) + i] = s.r[(size_t) (kNoiseQuiet * kFs) + i] = v * quiet;
    }
    s.l[(size_t) (kImpB * kFs)] = kImpulse;
    return s;
}

std::string signalFile (char which) { return which == 'A' ? "Test-A-Impuls.wav" : "Test-B-Ton-Rauschen.wav"; }

//==============================================================================
// Plan entry -> Nachhall settings
std::vector<std::string> split (const std::string& s, char sep)
{
    std::vector<std::string> out;
    std::string cur;
    std::istringstream in (s);
    while (std::getline (in, cur, sep))
        out.push_back (cur);
    return out;
}

// Plan entry (Pro-R units) -> Nachhall settings. Nachhall's Brightness and Thickness run 0 ... 100 %
// with the neutral point at 50 %, Pro-R's -100 ... +100 % with the neutral point at 0 %.
bool nachhallParams (const std::string& code, nh::EngineParams& p, std::string& why)
{
    p = nh::EngineParams();
    p.style = nh::kStyleModern;
    p.space = nh::spaceForT60 ((float) kIconTimes[kBaseIcon - 1]);
    p.decayRate = 1.0f;
    p.predelayMs = 0.0f;
    p.character = p.distance = p.thickness = p.brightness = 0.5f;
    p.width = 1.0f;
    p.mix = 1.0f;
    p.duckingDb = 0.0f;
    p.gateOn = false;
    p.eqAutoGain = true;
    for (const auto& item : split (code, ','))
    {
        if (item.empty())
            continue;
        const auto kv = split (item, '=');
        if (kv.size() != 2)
            continue;
        const std::string& k = kv[0];
        const std::string& v = kv[1];
        if (k == "mix")
            p.mix = std::stof (v) * 0.01f;
        else if (k == "style")
            p.style = v == "vintage" ? nh::kStyleVintage : (v == "plate" ? nh::kStylePlate : nh::kStyleModern);
        else if (k == "space")
        {
            const float t = std::stof (v);
            if (t < 0.2f || t > 10.0f)
            {
                why = "Space outside 0.2 ... 10 s";
                return false;
            }
            p.space = nh::spaceForT60 (t);
        }
        else if (k == "decay")
            p.decayRate = std::stof (v) * 0.01f;
        else if (k == "predelay")
            p.predelayMs = std::stof (v);
        else if (k == "character")
            p.character = std::stof (v) * 0.01f;
        else if (k == "distance")
            p.distance = std::stof (v) * 0.01f;
        else if (k == "thickness")
            p.thickness = 0.5f + std::stof (v) * 0.005f;
        else if (k == "brightness")
            p.brightness = 0.5f + std::stof (v) * 0.005f;
        else if (k == "width")
            p.width = std::stof (v) * 0.01f;
        else if (k == "autogain")
            p.eqAutoGain = v != "0";
        else if (k == "decayeq")
        {
            const auto f = split (v, ':');
            auto& b = p.decayBands[0];
            b.used = b.enabled = true;
            b.shape = f[0] == "highshelf" ? nh::kDecayHighShelf : (f[0] == "lowshelf" ? nh::kDecayLowShelf : (f[0] == "notch" ? nh::kDecayNotch : nh::kDecayBell));
            b.freq = std::stof (f[1]);
            b.rate = std::stof (f[2]);
            b.q = std::stof (f[3]);
        }
        else if (k == "posteq")
        {
            const auto f = split (v, ':');
            auto& b = p.postBands[0];
            b.used = b.enabled = true;
            b.shape = f[0] == "lowcut" ? nh::kPostLowCut : (f[0] == "highcut" ? nh::kPostHighCut : nh::kPostBell);
            b.freq = std::stof (f[1]);
            b.gain = std::stof (f[2]);
            b.q = std::stof (f[3]);
            if (f.size() > 4)
            {
                const int dbPerOct = std::stoi (f[4]);
                for (int s = 0; s < nh::kNumSlopes; ++s)
                    if (nh::kSlopeDbPerOct[s] == dbPerOct)
                        b.slope = s;
            }
        }
    }
    return true;
}

Stereo renderNachhall (const nh::EngineParams& p, const Stereo& in)
{
    nh::ReverbEngine e;
    e.setParams (p);
    e.prepare (kFs, 256);
    Stereo out = in;
    for (size_t pos = 0; pos < out.l.size(); pos += 256)
    {
        const int n = (int) std::min<size_t> (256, out.l.size() - pos);
        e.process (out.l.data() + pos, out.r.data() + pos, n);
    }
    return out;
}

//==============================================================================
// Measurements
struct Response // stereo output after one impulse, starting at the impulse
{
    std::vector<float> l, r;
};

Response cut (const wav::Audio& a, double t0, double len, long offset)
{
    Response w;
    const long start = (long) std::lround (t0 * a.sampleRate) + offset;
    const long n = (long) (len * a.sampleRate);
    w.l.assign ((size_t) n, 0.0f);
    w.r.assign ((size_t) n, 0.0f);
    for (long i = 0; i < n; ++i)
    {
        const long k = start + i;
        if (k >= 0 && k < (long) a.size())
        {
            w.l[(size_t) i] = a.l[(size_t) k];
            w.r[(size_t) i] = a.r[(size_t) k];
        }
    }
    return w;
}

double sumEnergy (const Response& w, double fs, double t0, double t1)
{
    return energy (w.l, fs, t0, t1) + energy (w.r, fs, t0, t1);
}

// decay from the Schroeder integral of both channels; centre 0 = broadband
// mode 0: T30 (falls back to T20), mode 1: EDT
double decay (const Response& w, double fs, double centre, int mode)
{
    const auto yl = centre > 0.0 ? bandpass (w.l, fs, centre, 1.414) : w.l;
    const auto yr = centre > 0.0 ? bandpass (w.r, fs, centre, 1.414) : w.r;
    std::vector<double> edc (yl.size());
    double acc = 0.0;
    for (size_t i = yl.size(); i-- > 0;)
    {
        acc += (double) yl[i] * yl[i] + (double) yr[i] * yr[i];
        edc[i] = acc;
    }
    if (edc.empty() || edc[0] <= 0.0)
        return 0.0;
    double t0 = -1, t5 = -1, t10 = -1, t25 = -1, t35 = -1;
    for (size_t i = 0; i < edc.size(); ++i)
    {
        const double d = 10.0 * std::log10 (edc[i] / edc[0] + 1.0e-30);
        const double t = (double) i / fs;
        if (t0 < 0 && d <= -0.0001)
            t0 = t;
        if (t5 < 0 && d <= -5.0)
            t5 = t;
        if (t10 < 0 && d <= -10.0)
            t10 = t;
        if (t25 < 0 && d <= -25.0)
            t25 = t;
        if (t35 < 0 && d <= -35.0)
        {
            t35 = t;
            break;
        }
    }
    if (mode == 1)
        return t10 > 0 ? 6.0 * (t10 - std::max (0.0, t0)) : 0.0;
    if (t5 >= 0 && t35 > t5)
        return 2.0 * (t35 - t5);
    if (t5 >= 0 && t25 > t5)
        return 3.0 * (t25 - t5);
    return 0.0;
}

struct ImpulseFeatures
{
    double t30[kNumOctaves] {};
    double edt = 0, c50 = 0, c80 = 0, ts = 0, onsetMs = 0, levelDb = 0;
    double edtMid = 0; // early decay time, mean of the 500 Hz ... 2 kHz octaves
    double density[4] {}; // echo density 5-20, 20-50, 50-100, 100-200 ms
    double erCentroid = 0, tailCentroid = 0;
    double tone[kNumToneBands] {}; // octave band energy re 1 kHz (dB)
    double crossDb = 0, crossTailDb = 0, corrLR = 0;
};

ImpulseFeatures measureImpulse (const Response& w, double fs, bool leftInput, double gainDb)
{
    ImpulseFeatures f;
    for (int b = 0; b < kNumOctaves; ++b)
        f.t30[b] = decay (w, fs, kOctaves[b], 0);
    f.edt = decay (w, fs, 0.0, 1);
    f.edtMid = std::cbrt (std::max (1.0e-4, decay (w, fs, 500.0, 1)) * std::max (1.0e-4, decay (w, fs, 1000.0, 1)) * std::max (1.0e-4, decay (w, fs, 2000.0, 1)));
    const double total = sumEnergy (w, fs, 0.0, kWindow);
    f.levelDb = db (total / ((double) kImpulse * kImpulse)) - gainDb;
    f.c50 = db (sumEnergy (w, fs, 0.0, 0.05) / std::max (1.0e-30, sumEnergy (w, fs, 0.05, kWindow)));
    f.c80 = db (sumEnergy (w, fs, 0.0, 0.08) / std::max (1.0e-30, sumEnergy (w, fs, 0.08, kWindow)));
    std::vector<float> e (w.l.size());
    float peak = 0.0f;
    for (size_t i = 0; i < e.size(); ++i)
    {
        e[i] = std::sqrt (w.l[i] * w.l[i] + w.r[i] * w.r[i]);
        peak = std::max (peak, e[i]);
    }
    f.ts = centreTime (e, fs, kWindow) * 1000.0;
    for (size_t i = 0; i < e.size(); ++i)
        if (e[i] >= 0.1f * peak && peak > 0.0f)
        {
            f.onsetMs = 1000.0 * (double) i / fs;
            break;
        }
    const auto& same = leftInput ? w.l : w.r;
    const auto& other = leftInput ? w.r : w.l;
    f.density[0] = echoDensity (same, fs, 0.005, 0.02);
    f.density[1] = echoDensity (same, fs, 0.02, 0.05);
    f.density[2] = echoDensity (same, fs, 0.05, 0.1);
    f.density[3] = echoDensity (same, fs, 0.1, 0.2);
    std::vector<float> mono (w.l.size());
    for (size_t i = 0; i < mono.size(); ++i)
        mono[i] = 0.5f * (w.l[i] + w.r[i]);
    f.erCentroid = spectralCentroid (mono, fs, 0.0, 0.03);
    f.tailCentroid = spectralCentroid (mono, fs, 0.1, 0.4);
    double ref = 0.0;
    for (int b = 0; b < kNumToneBands; ++b)
    {
        const double eb = energy (bandpass (w.l, fs, kToneBands[b], 1.414), fs, 0.0, kWindow) + energy (bandpass (w.r, fs, kToneBands[b], 1.414), fs, 0.0, kWindow);
        f.tone[b] = eb;
        if (kToneBands[b] == 1000.0)
            ref = eb;
    }
    for (auto& t : f.tone)
        t = db (t / std::max (ref, 1.0e-30));
    f.crossDb = db (energy (other, fs, 0.0, kWindow) / std::max (1.0e-30, energy (same, fs, 0.0, kWindow)));
    f.crossTailDb = db (energy (other, fs, 0.15, kWindow) / std::max (1.0e-30, energy (same, fs, 0.15, kWindow)));
    f.corrLR = correlation (w.l, w.r, fs, 0.1, 0.6);
    return f;
}

struct Calibration
{
    bool found = false;
    long offset = 0;
    double gainDb = 0.0, residualDb = -300.0, rate = 0.0;
    int bits = 0;
    bool isFloat = false;
    std::string note;
    // signal B through the chain (Mix 0 %): the 40 dB step between the bursts must survive
    bool foundB = false;
    double stepDb = 0.0, toneGainDb = 0.0;
    std::string noteB;
};

std::string upper (std::string s)
{
    for (auto& c : s)
        c = (char) std::toupper ((unsigned char) c);
    return s;
}

bool loadRecording (const std::string& dir, const std::string& name, wav::Audio& a, std::string& error)
{
    for (const auto& n : { name + ".wav", name + ".WAV", upper (name) + ".WAV", name + ".Wav" })
        if (wav::read (dir + "/" + n, a, error))
            return true;
    return false;
}

Calibration calibrate (const std::string& dir)
{
    Calibration c;
    wav::Audio a;
    std::string err;
    if (! loadRecording (dir, "K_Kalibrierung", a, err))
    {
        c.note = "K_Kalibrierung.wav fehlt - Versatz 0 und Pegel 0 dB angenommen";
        return c;
    }
    c.found = true;
    c.rate = a.sampleRate;
    c.bits = a.bitsPerSample;
    c.isFloat = a.isFloat;
    const long expected = (long) std::lround (kImpL * a.sampleRate);
    long best = expected;
    float bestV = 0.0f;
    for (long i = std::max (0L, expected - (long) (0.5 * a.sampleRate)); i < std::min ((long) a.size(), expected + (long) (0.5 * a.sampleRate)); ++i)
        if (std::abs (a.l[(size_t) i]) > std::abs (bestV))
        {
            bestV = a.l[(size_t) i];
            best = i;
        }
    c.offset = best - expected;
    c.gainDb = 20.0 * std::log10 (std::max (1.0e-9, (double) std::abs (bestV) / kImpulse));
    // everything apart from the two impulses should be silent (otherwise: resampling, effects, dither ...)
    const double g = (double) bestV / kImpulse;
    double rest = 0.0;
    for (size_t i = 0; i < a.size(); ++i)
    {
        const long k = (long) i - c.offset;
        const double refL = k == (long) std::lround (kImpL * a.sampleRate) ? kImpulse * g : 0.0;
        const double refR = k == (long) std::lround (kImpR * a.sampleRate) ? kImpulse * g : 0.0;
        rest += (a.l[i] - refL) * (a.l[i] - refL) + (a.r[i] - refR) * (a.r[i] - refR);
    }
    c.residualDb = db (rest / std::max (1.0e-30, 2.0 * (kImpulse * g) * (kImpulse * g)));
    if (std::abs (a.sampleRate - kFs) > 0.5)
        c.note += "Abtastrate " + std::to_string ((int) a.sampleRate) + " Hz statt 48000. ";
    if (c.residualDb > -40.0)
        c.note += "Der Impuls kommt nicht sauber durch (Resampling, Effekt, Dither oder falsche Datei?). ";
    if (std::abs (c.gainDb) > 1.0)
        c.note += "Pegel weicht ab (z. B. Velocity im Sampler) - wird herausgerechnet. ";
    if (c.offset != 0)
        c.note += "Versatz wird herausgerechnet. ";

    wav::Audio b;
    if (loadRecording (dir, "K_Kalibrierung_B", b, err))
    {
        c.foundB = true;
        const double fs = b.sampleRate;
        const Response loud = cut (b, kNoiseLoud, kNoiseLen, c.offset);
        const Response quiet = cut (b, kNoiseQuiet, kNoiseLen, c.offset);
        c.stepDb = db (sumEnergy (loud, fs, 0.0, kNoiseLen) / std::max (1.0e-30, sumEnergy (quiet, fs, 0.0, kNoiseLen)));
        const Stereo in = makeSignal ('B');
        double eIn = 0.0;
        for (size_t i = (size_t) (kTone0 * kFs); i < (size_t) (kTone1 * kFs); ++i)
            eIn += (double) in.l[i] * in.l[i] + (double) in.r[i] * in.r[i];
        const Response tone = cut (b, kTone0, kTone1 - kTone0, c.offset);
        c.toneGainDb = db (sumEnergy (tone, fs, 0.0, kTone1 - kTone0) / std::max (1.0e-30, eIn));
        if (std::abs (c.stepDb + kQuietDb) > 0.3)
            c.noteB += "Laut/leise kommt nicht unverändert durch die Kette (Kompressor oder Limiter in MASCHINE?). ";
        if (std::abs (c.toneGainDb - c.gainDb) > 0.5)
            c.noteB += "Signal B wird anders verstärkt als Signal A (anderes Pad, andere Velocity?). ";
    }
    else
        c.noteB = "K_Kalibrierung_B.wav fehlt";
    return c;
}

struct FileResult
{
    std::string file;
    bool present = false;
    std::string problem;
    ImpulseFeatures left, right;
    double monoCorr = 0.0;
    // signal B
    bool isB = false;
    double toneSpread = 0, toneWidthHz = 0, nonlinDb = 0, nonlinDuringDb = 0, timeVarDb = 0, timeVar1kDb = 0;
    double loudGainDb = 0, quietGainDb = 0, toneGainDb = 0; // output energy re input energy (dB)
    bool hasTimeVar = false;
};

// spectral width of the 1 kHz tone in the tail (RMS deviation from 1 kHz within 900 ... 1100 Hz)
double toneWidth (const wav::Audio& a, long offset)
{
    std::vector<float> mono (a.size());
    for (size_t i = 0; i < mono.size(); ++i)
        mono[i] = 0.5f * (a.l[i] + a.r[i]);
    const double sh = (double) offset / a.sampleRate;
    size_t n = 0;
    const auto p = powerSpectrum (mono, a.sampleRate, kTone1 + 0.1 + sh, kNoiseLoud - 0.1 + sh, n);
    double num = 0.0, den = 0.0;
    for (size_t k = 0; k < p.size(); ++k)
    {
        const double f = (double) k * a.sampleRate / (double) n;
        if (f >= 900.0 && f <= 1100.0)
        {
            num += p[k] * (f - 1000.0) * (f - 1000.0);
            den += p[k];
        }
    }
    return den > 0.0 ? std::sqrt (num / den) : 0.0;
}

double differenceDb (const Response& x, const Response& ref, double fs, double centre)
{
    const auto xl = centre > 0.0 ? bandpass (x.l, fs, centre, 1.414) : x.l;
    const auto xr = centre > 0.0 ? bandpass (x.r, fs, centre, 1.414) : x.r;
    const auto rl = centre > 0.0 ? bandpass (ref.l, fs, centre, 1.414) : ref.l;
    const auto rr = centre > 0.0 ? bandpass (ref.r, fs, centre, 1.414) : ref.r;
    double d = 0.0, e = 0.0;
    for (size_t i = (size_t) (0.05 * fs); i < std::min (rl.size(), (size_t) (0.6 * fs)); ++i)
    {
        d += (double) (xl[i] - rl[i]) * (xl[i] - rl[i]) + (double) (xr[i] - rr[i]) * (xr[i] - rr[i]);
        e += (double) rl[i] * rl[i] + (double) rr[i] * rr[i];
    }
    return db (d / std::max (e, 1.0e-30));
}

double toneSpread (const wav::Audio& a, long offset)
{
    std::vector<float> mono (a.size());
    for (size_t i = 0; i < mono.size(); ++i)
        mono[i] = 0.5f * (a.l[i] + a.r[i]);
    const double sh = (double) offset / a.sampleRate;
    size_t n = 0;
    const auto p = powerSpectrum (mono, a.sampleRate, kTone1 + 0.1 + sh, kNoiseLoud - 0.1 + sh, n);
    const double near = bandPower (p, a.sampleRate, n, 990.0, 1010.0);
    const double all = bandPower (p, a.sampleRate, n, 500.0, 2000.0);
    return all > 0.0 ? 1.0 - near / all : 0.0;
}

FileResult analyzeAudio (const wav::Audio& a, const Row& row, const Calibration& cal, const std::map<std::string, Response>& leftResponses);

FileResult analyzeFile (const std::string& dir, const Row& row, const Calibration& cal, const std::map<std::string, Response>& leftResponses)
{
    wav::Audio a;
    std::string err;
    if (! loadRecording (dir, row.file, a, err))
    {
        FileResult r;
        r.file = row.file;
        return r;
    }
    return analyzeAudio (a, row, cal, leftResponses);
}

FileResult analyzeAudio (const wav::Audio& a, const Row& row, const Calibration& cal, const std::map<std::string, Response>& leftResponses)
{
    FileResult r;
    r.file = row.file;
    r.present = true;
    const double fs = a.sampleRate;
    const double need = row.signal == 'A' ? kLenA : kLenB;
    if ((double) a.size() / fs < need - 0.05)
        r.problem += "zu kurz (" + std::to_string ((int) ((double) a.size() / fs)) + " s statt " + std::to_string ((int) need) + " s). ";
    if (! a.isFloat)
    {
        size_t clipped = 0;
        for (size_t i = 0; i < a.size(); ++i)
            if (std::abs (a.l[i]) >= 0.9999f || std::abs (a.r[i]) >= 0.9999f)
                ++clipped;
        if (clipped > 0)
            r.problem += "übersteuert (" + std::to_string (clipped) + " Samples). ";
    }
    {
        const Response pre = cut (a, 0.0, 0.95, cal.offset);
        const Response first = cut (a, row.signal == 'A' ? kImpL : kTone0, 2.0, cal.offset);
        if (sumEnergy (pre, fs, 0.0, 0.95) > 1.0e-5 * sumEnergy (first, fs, 0.0, 2.0))
            r.problem += "Vorlauf nicht still (Rest vom vorherigen Export?). ";
    }
    if (row.signal == 'A')
    {
        const Response wl = cut (a, kImpL, kWindow, cal.offset);
        const Response wr = cut (a, kImpR, kWindow, cal.offset);
        r.left = measureImpulse (wl, fs, true, cal.gainDb);
        r.right = measureImpulse (wr, fs, false, cal.gainDb);
        Response m;
        m.l.resize (wl.l.size());
        m.r.resize (wl.l.size());
        for (size_t i = 0; i < m.l.size(); ++i)
        {
            m.l[i] = wl.l[i] + wr.l[i];
            m.r[i] = wl.r[i] + wr.r[i];
        }
        r.monoCorr = correlation (m.l, m.r, fs, 0.1, 0.6);
        return r;
    }
    r.isB = true;
    r.toneSpread = toneSpread (a, cal.offset);
    r.toneWidthHz = toneWidth (a, cal.offset);
    const Response loud = cut (a, kNoiseLoud, 6.5, cal.offset);
    const Response quiet = cut (a, kNoiseQuiet, 6.5, cal.offset);
    r.nonlinDb = db (sumEnergy (loud, fs, 0.6, 2.5)) - db (sumEnergy (quiet, fs, 0.6, 2.5)) + kQuietDb;
    r.nonlinDuringDb = db (sumEnergy (loud, fs, 0.1, 0.5)) - db (sumEnergy (quiet, fs, 0.1, 0.5)) + kQuietDb;
    {
        // absolute gains: output energy (burst + tail) re input energy of the burst
        const Stereo in = makeSignal ('B');
        auto inE = [&in] (double t0, double t1)
        {
            double e = 0.0;
            for (size_t i = (size_t) (t0 * kFs); i < (size_t) (t1 * kFs); ++i)
                e += (double) in.l[i] * in.l[i] + (double) in.r[i] * in.r[i];
            return e;
        };
        r.loudGainDb = db (sumEnergy (loud, fs, 0.0, 6.5) / inE (kNoiseLoud, kNoiseLoud + kNoiseLen)) - cal.gainDb;
        r.quietGainDb = db (sumEnergy (quiet, fs, 0.0, 6.5) / inE (kNoiseQuiet, kNoiseQuiet + kNoiseLen)) - cal.gainDb;
        const Response tone = cut (a, kTone0, kNoiseLoud - kTone0 - 0.1, cal.offset);
        r.toneGainDb = db (sumEnergy (tone, fs, 0.0, kNoiseLoud - kTone0 - 0.1) / inE (kTone0, kTone1)) - cal.gainDb;
    }
    const Response imp = cut (a, kImpB, kWindow - 3.0, cal.offset);
    r.left = measureImpulse (imp, fs, true, cal.gainDb);
    std::string partner = row.file;
    if (partner.size() > 2 && partner.substr (partner.size() - 2) == "_B")
        partner = partner.substr (0, partner.size() - 2) + "_A";
    const auto it = leftResponses.find (partner);
    if (it != leftResponses.end())
    {
        r.timeVarDb = differenceDb (imp, it->second, fs, 0.0);
        r.timeVar1kDb = differenceDb (imp, it->second, fs, 1000.0);
        r.hasTimeVar = true;
    }
    return r;
}

struct DirResult
{
    std::string dir, label;
    Calibration cal;
    std::map<std::string, FileResult> files;
    std::map<std::string, std::string> shown; // file -> displayed decay time / room name (from the filled-in plan)
};

std::vector<std::string> readCsvLine (const std::string& line, char sep)
{
    std::vector<std::string> out;
    std::string cur;
    bool quoted = false;
    for (size_t i = 0; i < line.size(); ++i)
    {
        const char ch = line[i];
        if (ch == '"')
        {
            if (quoted && i + 1 < line.size() && line[i + 1] == '"')
            {
                cur += '"';
                ++i;
            }
            else
                quoted = ! quoted;
        }
        else if (ch == sep && ! quoted)
        {
            out.push_back (cur);
            cur.clear();
        }
        else if (ch != '\r')
            cur += ch;
    }
    out.push_back (cur);
    return out;
}

void readShownValues (DirResult& d)
{
    // the filled-in plan lies in the recordings folder or one level above (reference/Messplan.csv)
    std::ifstream in (d.dir + "/Messplan.csv");
    if (! in)
        in.open (d.dir + "/../Messplan.csv");
    if (! in)
        return;
    std::string line;
    char sep = ';';
    bool header = true;
    while (std::getline (in, line))
    {
        if (header) // Excel may have saved it with ',' or tabs instead of ';'
        {
            header = false;
            const auto count = [&line] (char c) { return std::count (line.begin(), line.end(), c); };
            sep = count (';') >= count (',') ? (count (';') >= count ('\t') ? ';' : '\t') : (count (',') >= count ('\t') ? ',' : '\t');
            continue;
        }
        auto cols = readCsvLine (line, sep);
        if (cols.size() < 7)
            continue;
        std::string file = cols[1];
        if (file.size() > 4 && (file.substr (file.size() - 4) == ".wav" || file.substr (file.size() - 4) == ".WAV"))
            file = file.substr (0, file.size() - 4);
        cols[1] = file;
        std::string shown = cols[5];
        if (! cols[6].empty())
            shown += (shown.empty() ? "" : " s, ") + cols[6];
        else if (! shown.empty())
            shown += " s";
        if (! shown.empty())
            d.shown[cols[1]] = shown;
    }
}

DirResult analyzeDir (const std::string& dir, const std::string& label, const std::vector<Row>& p)
{
    DirResult d;
    d.dir = dir;
    d.label = label;
    d.cal = calibrate (dir);
    readShownValues (d);
    std::map<std::string, Response> leftResponses;
    for (const auto& row : p)
        if (row.signal == 'A')
        {
            wav::Audio a;
            std::string err;
            if (loadRecording (dir, row.file, a, err))
                leftResponses[row.file] = cut (a, kImpL, kWindow - 3.0, d.cal.offset);
        }
    for (const auto& row : p)
        d.files[row.file] = analyzeFile (dir, row, d.cal, leftResponses);
    return d;
}

//==============================================================================
// Report
std::string num (double v, int decimals)
{
    char b[32];
    std::snprintf (b, sizeof (b), "%.*f", decimals, v);
    std::string s = b;
    for (auto& c : s)
        if (c == '.')
            c = ',';
    return s;
}

std::string sec (double v) { return v > 0.0 ? num (v, v < 1.0 ? 2 : 1) : "–"; }

void writeReport (std::ostream& out, const std::vector<Row>& p, const std::vector<DirResult>& dirs)
{
    out << "# Messwerte\n\n";
    out << "Erzeugt mit `NachhallReference analyze`. Gemessen wird nur, was aus dem Plugin herauskommt.\n\n";
    out << "Abkürzungen: **T30** Nachhallzeit je Oktave (s) · **EDT** frühe Abklingzeit (s) · **C50** Klarheit (dB, mehr = näher/trockener) · "
           "**Ts** Schwerpunktzeit (ms, mehr = späterer Aufbau) · **Beginn** erste Reflexion (ms) · **Dichte** Echodichte 20–50 ms (1 = dicht wie Rauschen) · "
           "**Pegel** Gesamtenergie des Halls (dB re Impuls).\n\n";

    out << "## Kalibrierung\n\n| Quelle | Datei gefunden | Abtastrate | Format | Versatz (Samples) | Pegel (dB) | Rest (dB) | Hinweis |\n|---|---|---|---|---|---|---|---|\n";
    for (const auto& d : dirs)
        out << "| " << d.label << " | " << (d.cal.found ? "ja" : "nein") << " | " << (d.cal.found ? std::to_string ((int) d.cal.rate) : "–") << " | "
            << (d.cal.found ? (std::to_string (d.cal.bits) + (d.cal.isFloat ? " Bit float" : " Bit")) : "–") << " | " << d.cal.offset << " | "
            << num (d.cal.gainDb, 2) << " | " << (d.cal.found ? num (d.cal.residualDb, 0) : "–") << " | " << (d.cal.note.empty() ? "ok" : d.cal.note) << " |\n";
    out << "\n**Signal B durch die Kette** (Mix 0 %): Abstand lautes/leises Rauschen soll 40,0 dB sein, der Ton so verstärkt wie Signal A.\n\n"
           "| Quelle | Datei gefunden | Abstand laut/leise (dB) | Pegel Ton (dB) | Hinweis |\n|---|---|---|---|---|\n";
    for (const auto& d : dirs)
        out << "| " << d.label << " | " << (d.cal.foundB ? "ja" : "nein") << " | " << (d.cal.foundB ? num (d.cal.stepDb, 2) : "–") << " | "
            << (d.cal.foundB ? num (d.cal.toneGainDb, 2) : "–") << " | " << (d.cal.noteB.empty() ? "ok" : d.cal.noteB) << " |\n";

    out << "\n## Probleme mit Dateien\n\n";
    bool any = false;
    for (const auto& d : dirs)
        for (const auto& row : p)
        {
            const auto& f = d.files.at (row.file);
            if (f.present && ! f.problem.empty())
            {
                out << "- " << d.label << " `" << row.file << ".wav`: " << f.problem << "\n";
                any = true;
            }
        }
    if (! any)
        out << "Keine.\n";

    std::string group;
    for (const auto& row : p)
    {
        if (row.group == "Kalibrierung")
            continue;
        bool present = false;
        for (const auto& d : dirs)
            present = present || d.files.at (row.file).present;
        if (! present)
            continue;
        if (row.group != group)
        {
            group = row.group;
            out << "\n## " << group << "\n\n";
            out << "| Datei | Quelle | Anzeige | T30 125 | 250 | 500 | 1k | 2k | 4k | 8k | 16k | EDT | C50 | Ts | Beginn | Dichte | Pegel | Übersprechen | Korr. L/R | Mono‑Quelle |\n";
            out << "|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|\n";
        }
        for (const auto& d : dirs)
        {
            const auto& f = d.files.at (row.file);
            if (! f.present)
                continue;
            const auto& m = f.left;
            const auto shown = d.shown.count (row.file) ? d.shown.at (row.file) : std::string ("");
            out << "| " << row.file << (row.signal == 'B' ? " (Impuls aus B)" : "") << " | " << d.label << " | " << shown;
            for (int b = 0; b < kNumOctaves; ++b)
                out << " | " << sec (m.t30[b]);
            out << " | " << sec (m.edt) << " | " << num (m.c50, 1) << " | " << num (m.ts, 0) << " | " << num (m.onsetMs, 1) << " | " << num (m.density[1], 2)
                << " | " << num (m.levelDb, 1) << " | " << num (m.crossDb, 1) << " | " << num (m.corrLR, 2) << " | " << (row.signal == 'A' ? num (f.monoCorr, 2) : "–") << " |\n";
        }
    }

    out << "\n## Klangfarbe (Energie je Oktave, dB relativ zu 1 kHz, Impuls links)\n\n| Datei | Quelle |";
    for (double b : kToneBands)
        out << " " << (b >= 1000.0 ? num (b / 1000.0, 0) + "k" : num (b, 0)) << " |";
    out << " Reflexionen hell (Hz) | Fahne hell (Hz) |\n|---|---|";
    for (int b = 0; b < kNumToneBands + 2; ++b)
        out << "---|";
    out << "\n";
    for (const auto& row : p)
        if (row.signal == 'A' && row.group != "Kalibrierung")
            for (const auto& d : dirs)
            {
                const auto& f = d.files.at (row.file);
                if (! f.present)
                    continue;
                out << "| " << row.file << " | " << d.label << " |";
                for (double t : f.left.tone)
                    out << " " << num (t, 1) << " |";
                out << " " << num (f.left.erCentroid, 0) << " | " << num (f.left.tailCentroid, 0) << " |\n";
            }

    out << "\n## Signal B: Modulation und Dynamik\n\n"
           "**Ton verschmiert**: Anteil eines 1‑kHz‑Tons in der Hallfahne außerhalb ±10 Hz (0 % = keine Modulation). "
           "**Tonbreite**: wie breit dieser Ton in der Fahne ist (Hz, mehr = stärker moduliert). "
           "**Laut/leise**: Pegel des lauten Rauschens gegen das 40 dB leisere, während es spielt und in der Fahne (0 dB = linear, negativ = lautes wird leiser gemacht). "
           "**Zeitliche Änderung**: Unterschied zweier gleicher Impulse zu verschiedenen Zeiten, gesamt und nur bei 1 kHz (sehr negativ = statisch).\n\n"
           "| Datei | Quelle | Ton verschmiert | Tonbreite (Hz) | Laut/leise während (dB) | Laut/leise Fahne (dB) | Zeitliche Änderung (dB) | davon 1 kHz (dB) |\n"
           "|---|---|---|---|---|---|---|---|\n";
    for (const auto& row : p)
        if (row.signal == 'B' && row.group != "Kalibrierung")
            for (const auto& d : dirs)
            {
                const auto& f = d.files.at (row.file);
                if (! f.present)
                    continue;
                out << "| " << row.file << " | " << d.label << " | " << num (100.0 * f.toneSpread, 0) << " % | " << num (f.toneWidthHz, 1) << " | "
                    << num (f.nonlinDuringDb, 2) << " | " << num (f.nonlinDb, 2) << " | "
                    << (f.hasTimeVar ? num (f.timeVarDb, 1) + " | " + num (f.timeVar1kDb, 1) : std::string ("– (A‑Datei fehlt) | –")) << " |\n";
            }

    out << "\n## Fehlende Dateien\n\n";
    for (const auto& d : dirs)
    {
        std::vector<std::string> missing;
        for (const auto& row : p)
            if (! d.files.at (row.file).present && row.need != "optional")
                missing.push_back (row.file + (row.need == "ja" ? "" : " (falls vorhanden)"));
        out << "- " << d.label << ": ";
        if (missing.empty())
            out << "keine Pflicht‑Datei fehlt\n";
        else
        {
            for (size_t i = 0; i < missing.size(); ++i)
                out << (i ? ", " : "") << missing[i];
            out << "\n";
        }
    }
}

void writeCsv (std::ostream& out, const std::vector<Row>& p, const std::vector<DirResult>& dirs)
{
    out << "source,file,group,signal";
    for (double b : kOctaves)
        out << ",t30_" << (int) b;
    out << ",edt,c50,c80,ts,onset,dens5_20,dens20_50,dens50_100,level";
    for (double b : kToneBands)
        out << ",tone_" << (int) b;
    out << ",er_centroid,tail_centroid,cross,cross_tail,corr_lr,mono_corr,right_t30_1k,right_level,right_cross";
    out << ",tone_spread,tone_width,nl_during,nl_tail,tv,tv1k,loud_gain,quiet_gain,tone_gain,edt_mid,dens100_200\n";
    for (const auto& d : dirs)
        for (const auto& row : p)
        {
            const auto& f = d.files.at (row.file);
            if (! f.present)
                continue;
            const auto& m = f.left;
            out << d.label << "," << row.file << "," << row.group << "," << row.signal;
            char b[64];
            auto put = [&out, &b] (double v) { std::snprintf (b, sizeof (b), ",%.6g", v); out << b; };
            for (double t : m.t30)
                put (t);
            put (m.edt); put (m.c50); put (m.c80); put (m.ts); put (m.onsetMs);
            put (m.density[0]); put (m.density[1]); put (m.density[2]); put (m.levelDb);
            for (double t : m.tone)
                put (t);
            put (m.erCentroid); put (m.tailCentroid); put (m.crossDb); put (m.crossTailDb); put (m.corrLR); put (f.monoCorr);
            put (f.right.t30[3]); put (f.right.levelDb); put (f.right.crossDb);
            put (f.toneSpread); put (f.toneWidthHz); put (f.nonlinDuringDb); put (f.nonlinDb); put (f.timeVarDb); put (f.timeVar1kDb);
            put (f.loudGainDb); put (f.quietGainDb); put (f.toneGainDb);
            put (m.edtMid);
            put (m.density[3]);
            out << "\n";
        }
}

std::string baseName (const std::string& dir)
{
    const auto p = dir.find_last_of ("/\\");
    return p == std::string::npos ? dir : dir.substr (p + 1);
}

//==============================================================================
// Calibration (developer tool): fits Nachhall's model tables to the recordings of another reverb.
// Prints the fitted tables as C++ for the sources.
namespace fit
{
const Row* rowFor (const std::vector<Row>& p, const std::string& file)
{
    for (const auto& r : p)
        if (r.file == file)
            return &r;
    return nullptr;
}

// fast mode for the fits: signal A is rendered only up to the end of the left impulse window
bool& fastRender()
{
    static bool fast = false;
    return fast;
}

FileResult measureNachhall (const nh::EngineParams& ep, const Row& row)
{
    static const Stereo sigA = makeSignal ('A'), sigB = makeSignal ('B');
    static const Stereo sigAShort = []
    {
        Stereo s = makeSignal ('A');
        const size_t n = (size_t) ((kImpL + kWindow + 0.05) * kFs);
        s.l.resize (n);
        s.r.resize (n);
        return s;
    }();
    const bool shortA = fastRender() && row.signal == 'A';
    Stereo out = renderNachhall (ep, row.signal == 'A' ? (shortA ? sigAShort : sigA) : sigB);
    if (shortA) // pad, the right impulse window stays empty
    {
        out.l.resize ((size_t) (kLenA * kFs), 0.0f);
        out.r.resize ((size_t) (kLenA * kFs), 0.0f);
    }
    wav::Audio a;
    a.l = out.l;
    a.r = out.r;
    a.sampleRate = kFs;
    a.channels = 2;
    a.isFloat = true;
    a.bitsPerSample = 32;
    Calibration cal;
    cal.found = true;
    return analyzeAudio (a, row, cal, {});
}

double logMid (const double* t) { return (std::log (t[2]) + std::log (t[3]) + std::log (t[4])) / 3.0; }

void designCurve (int room, double* out)
{
    const float space = nh::spaceForRoomIndex (room);
    nh::DecayBandParams none[nh::kNumDecayBands];
    const auto ds = nh::ReverbEngine::makeDecaySettings (none, space, 1.0f, 0.5f, nh::kStyleModern);
    const double ref = nh::ReverbEngine::referenceDelayFor (nh::kStyleModern, space);
    const auto spec = nh::buildDecaySpec (ds, kFs, ref);
    float f[kNumOctaves], t[kNumOctaves];
    for (int b = 0; b < kNumOctaves; ++b)
        f[b] = (float) kOctaves[b];
    nh::decayTimeCurve (spec, kFs, ref, f, t, kNumOctaves);
    for (int b = 0; b < kNumOctaves; ++b)
        out[b] = t[b];
}

// small Nelder-Mead minimiser
template <int D, typename F>
void nelderMead (double* x, const double* step, F cost, int iterations)
{
    double pts[D + 1][D], val[D + 1];
    for (int i = 0; i <= D; ++i)
    {
        for (int k = 0; k < D; ++k)
            pts[i][k] = x[k] + (i == k + 1 ? step[k] : 0.0);
        val[i] = cost (pts[i]);
    }
    for (int it = 0; it < iterations; ++it)
    {
        int lo = 0, hi = 0;
        for (int i = 1; i <= D; ++i)
        {
            if (val[i] < val[lo])
                lo = i;
            if (val[i] > val[hi])
                hi = i;
        }
        int nh2 = lo;
        for (int i = 0; i <= D; ++i)
            if (i != hi && val[i] > val[nh2])
                nh2 = i;
        double c[D] = {};
        for (int i = 0; i <= D; ++i)
            if (i != hi)
                for (int k = 0; k < D; ++k)
                    c[k] += pts[i][k] / D;
        double r[D], e[D], q[D];
        for (int k = 0; k < D; ++k)
            r[k] = c[k] + (c[k] - pts[hi][k]);
        const double fr = cost (r);
        if (fr < val[lo])
        {
            for (int k = 0; k < D; ++k)
                e[k] = c[k] + 2.0 * (c[k] - pts[hi][k]);
            const double fe = cost (e);
            for (int k = 0; k < D; ++k)
                pts[hi][k] = fe < fr ? e[k] : r[k];
            val[hi] = std::min (fe, fr);
        }
        else if (fr < val[nh2])
        {
            for (int k = 0; k < D; ++k)
                pts[hi][k] = r[k];
            val[hi] = fr;
        }
        else
        {
            for (int k = 0; k < D; ++k)
                q[k] = c[k] + 0.5 * (pts[hi][k] - c[k]);
            const double fq = cost (q);
            if (fq < val[hi])
            {
                for (int k = 0; k < D; ++k)
                    pts[hi][k] = q[k];
                val[hi] = fq;
            }
            else
                for (int i = 0; i <= D; ++i)
                    if (i != lo)
                    {
                        for (int k = 0; k < D; ++k)
                            pts[i][k] = pts[lo][k] + 0.5 * (pts[i][k] - pts[lo][k]);
                        val[i] = cost (pts[i]);
                    }
        }
    }
    int best = 0;
    for (int i = 1; i <= D; ++i)
        if (val[i] < val[best])
            best = i;
    for (int k = 0; k < D; ++k)
        x[k] = pts[best][k];
    cost (x);
}

const char* const kRoomFiles[nh::kNumRoomModels] = { "R01", "RZ_0400ms", "R02", "RZ_1200ms", "R03", "RZ_2400ms",
                                                      "R04", "RZ_4000ms", "R05", "RZ_7000ms", "R06" };

void setRoom (nh::RoomModel& m, const double* v)
{
    using nh::clampv;
    m.t60Scale = (float) clampv (std::exp (v[0]), 0.5, 2.0);
    m.bassRatio = (float) clampv (std::exp (v[1]), 0.4, 2.5);
    m.hfRatio = (float) clampv (std::exp (v[2]), 0.05, 1.15);
    m.hfFreq = (float) clampv (std::exp (v[3]), 1200.0, 16000.0);
    m.airRatio = (float) clampv (std::exp (v[4]), 0.05, 1.3);
    m.airAbs = (float) clampv (std::exp (v[5]), 0.05, 4.0);
}

void printRooms()
{
    std::printf ("        // name           t60     scale   X       Y      Z      bass   hf     hfFreq   air    er     level  diff   erCut    build  airAbs lateLs lateHs early  lateMid lateAir primary\n");
    for (int i = 0; i < nh::kNumRoomModels; ++i)
    {
        const auto& m = nh::roomModels()[i];
        char name[32];
        std::snprintf (name, sizeof (name), "\"%s\",", m.name);
        std::printf ("        { %-15s %5.2ff, %5.3ff, %6.1ff, %6.1ff, %5.1ff, %4.2ff, %4.2ff, %7.1ff, %4.2ff, %5.3ff, %5.2ff, %5.3ff, %7.1ff, %5.3ff, %5.3ff, %5.2ff, %5.2ff, %5.3ff, %5.2ff, %5.2ff, %5.2ff },\n",
                     name, m.t60, m.t60Scale, m.dimX, m.dimY, m.dimZ, m.bassRatio, m.hfRatio, m.hfFreq, m.airRatio, m.erGain, m.levelDb, m.diffusion, m.erCutoff, m.buildScale,
                     m.airAbs, m.lateLsDb, m.lateHsDb, m.earlyTap, m.lateMidDb, m.lateAirDb, m.erPrimaryDb);
    }
}

int rooms (const DirResult& pro, const std::vector<Row>& plan, int iterations)
{
    static const double weight[kNumOctaves] = { 0.7, 0.8, 1.0, 1.5, 1.0, 1.0, 1.0, 0.5 };
    for (int i = 0; i < nh::kNumRoomModels; ++i)
    {
        const auto& tgt = pro.files.at (kRoomFiles[i]);
        const Row* row = rowFor (plan, kRoomFiles[i]);
        if (! tgt.present || row == nullptr)
            continue;
        nh::RoomModel& m = nh::roomModelsMutable()[i];
        for (int it = 0; it < iterations; ++it)
        {
            nh::EngineParams ep;
            std::string why;
            nachhallParams ("", ep, why);
            ep.space = nh::spaceForRoomIndex (i);
            const FileResult meas = measureNachhall (ep, *row);
            double design[kNumOctaves], want[kNumOctaves];
            designCurve (i, design);
            double err = 0.0;
            std::printf ("%-10s it %d:", kRoomFiles[i], it);
            for (int b = 0; b < kNumOctaves; ++b)
            {
                const double tm = std::max (1.0e-3, meas.left.t30[b]), tt = std::max (1.0e-3, tgt.left.t30[b]);
                want[b] = tt / tm * design[b];
                std::printf (" %+5.1f%%", 100.0 * (tm / tt - 1.0));
                err += weight[b] * std::log (tm / tt) * std::log (tm / tt);
            }
            std::printf ("  level %+5.2f dB  (rms %.3f)\n", meas.left.levelDb - tgt.left.levelDb, std::sqrt (err / kNumOctaves));
            double x[6] = { std::log (m.t60Scale), std::log (m.bassRatio), std::log (m.hfRatio), std::log (m.hfFreq), std::log (m.airRatio), std::log (m.airAbs) };
            const double step[6] = { 0.05, 0.1, 0.15, 0.15, 0.15, 0.3 };
            auto cost = [&] (const double* v)
            {
                setRoom (m, v);
                double d[kNumOctaves];
                designCurve (i, d);
                double c = 0.0;
                for (int b = 0; b < kNumOctaves; ++b)
                {
                    const double e = std::log (std::max (1.0e-4, d[b]) / std::max (1.0e-4, want[b]));
                    c += weight[b] * e * e;
                }
                return c + 0.002 * v[5] * v[5]; // keep the time-based HF loss near the measured one
            };
            nelderMead<6> (x, step, cost, 600);
            setRoom (m, x);
            m.levelDb += (float) (tgt.left.levelDb - meas.left.levelDb);
        }
    }
    printRooms();
    return 0;
}
// early part per room: ER level and late build-up spread -> clarity C50 and centre time Ts,
// ER low-pass -> brightness of the first 30 ms, level kept at the target
int early (const DirResult& pro, const std::vector<Row>& plan, int iterations, int firstRoom, int lastRoom)
{
    for (int i = std::max (0, firstRoom); i <= std::min (lastRoom, nh::kNumRoomModels - 1); ++i)
    {
        const auto& tgt = pro.files.at (kRoomFiles[i]);
        const Row* row = rowFor (plan, kRoomFiles[i]);
        if (! tgt.present || row == nullptr)
            continue;
        nh::RoomModel& m = nh::roomModelsMutable()[i];
        nh::EngineParams ep;
        std::string why;
        nachhallParams ("", ep, why);
        ep.space = nh::spaceForRoomIndex (i);
        for (int it = 0; it < iterations; ++it)
        {
            auto eval = [&] (const double* v, FileResult& out)
            {
                m.erGain = (float) nh::clampv (std::exp (v[0]), 0.02, 8.0);
                m.buildScale = (float) nh::clampv (std::exp (v[1]), 0.05, 6.0);
                out = measureNachhall (ep, *row);
                const double dc = (out.left.c50 - tgt.left.c50) / 1.0;
                const double dt = std::log (std::max (1.0, out.left.ts) / std::max (1.0, tgt.left.ts)) / 0.08;
                return dc * dc + dt * dt;
            };
            FileResult last;
            double x[2] = { std::log (m.erGain), std::log (m.buildScale) };
            const double step[2] = { 0.4, 0.4 };
            nelderMead<2> (x, step, [&] (const double* v) { return eval (v, last); }, 14);
            eval (x, last);
            m.erCutoff = (float) nh::clampv (m.erCutoff * std::pow (tgt.left.erCentroid / std::max (100.0, last.left.erCentroid), 1.5), 2000.0, 22000.0);
            m.levelDb += (float) (tgt.left.levelDb - last.left.levelDb);
            std::printf ("%-10s it %d: C50 %+6.2f / %+6.2f  Ts %5.0f / %5.0f  centroid %5.0f / %5.0f  level %+5.2f / %+5.2f  dens %.2f / %.2f -> er %.3f build %.3f cut %.0f\n",
                         kRoomFiles[i], it, last.left.c50, tgt.left.c50, last.left.ts, tgt.left.ts, last.left.erCentroid, tgt.left.erCentroid, last.left.levelDb,
                         tgt.left.levelDb, last.left.density[1], tgt.left.density[1], m.erGain, m.buildScale, m.erCutoff);
        }
    }
    printRooms();
    return 0;
}
// tone of the late reverb per room (low shelf 150 Hz, high shelf 2.5 kHz) -> tonal balance 63 Hz ... 8 kHz,
// level kept at the target
double toneAt (const ImpulseFeatures& f, double hz);
int tone (const DirResult& pro, const std::vector<Row>& plan, int firstRoom, int lastRoom)
{
    const double toneHz[8] = { 63, 125, 250, 500, 2000, 4000, 8000, 16000 };
    for (int i = std::max (0, firstRoom); i <= std::min (lastRoom, nh::kNumRoomModels - 1); ++i)
    {
        const auto& tgt = pro.files.at (kRoomFiles[i]);
        const Row* row = rowFor (plan, kRoomFiles[i]);
        if (! tgt.present || row == nullptr)
            continue;
        nh::RoomModel& m = nh::roomModelsMutable()[i];
        nh::EngineParams ep;
        std::string why;
        nachhallParams ("", ep, why);
        ep.space = nh::spaceForRoomIndex (i);
        FileResult last;
        auto eval = [&] (const double* v)
        {
            m.lateLsDb = (float) nh::clampv (v[0], -12.0, 12.0);
            m.lateHsDb = (float) nh::clampv (v[1], -12.0, 12.0);
            m.lateMidDb = (float) nh::clampv (v[2], -12.0, 12.0);
            m.lateAirDb = (float) nh::clampv (v[3], -12.0, 15.0);
            last = measureNachhall (ep, *row);
            double c = 0.0;
            for (double hz : toneHz)
            {
                const double e = toneAt (last.left, hz) - toneAt (tgt.left, hz);
                c += e * e;
            }
            // the decay of the 16 kHz octave follows the share of the late reverb there
            const double e16 = std::log (std::max (1.0e-3, last.left.t30[7]) / std::max (1.0e-3, tgt.left.t30[7])) / 0.1;
            // brightness of the late reverb itself
            const double etc = std::log (std::max (100.0, last.left.tailCentroid) / std::max (100.0, tgt.left.tailCentroid)) / 0.04;
            return c + e16 * e16 + etc * etc + 0.01 * (v[0] * v[0] + v[1] * v[1] + v[2] * v[2] + v[3] * v[3]);
        };
        double x[4] = { m.lateLsDb, m.lateHsDb, m.lateMidDb, m.lateAirDb };
        const double step[4] = { 3.0, -2.0, 3.0, 3.0 };
        nelderMead<4> (x, step, eval, 40);
        eval (x);
        m.levelDb += (float) (tgt.left.levelDb - last.left.levelDb);
        std::printf ("%-10s late tone LS %+5.2f HS %+5.2f mid %+5.2f air %+5.2f dB, T30 16k %.2f/%.2f, tail centroid %.0f/%.0f:", kRoomFiles[i], m.lateLsDb,
                     m.lateHsDb, m.lateMidDb, m.lateAirDb, last.left.t30[7], tgt.left.t30[7], last.left.tailCentroid, tgt.left.tailCentroid);
        for (double hz : toneHz)
            std::printf (" %5.0f %+5.1f/%+5.1f", hz, toneAt (last.left, hz), toneAt (tgt.left, hz));
        std::printf ("\n");
    }
    printRooms();
    return 0;
}

// renders one plan entry through Nachhall (plan codes in Pro-R units)
FileResult measureCode (const std::vector<Row>& plan, const std::string& file, const std::string& extra = {})
{
    const Row* row = rowFor (plan, file);
    nh::EngineParams ep;
    std::string why;
    nachhallParams (extra.empty() ? row->code : (row->code.empty() ? extra : row->code + "," + extra), ep, why);
    return measureNachhall (ep, *row);
}

double toneAt (const ImpulseFeatures& f, double hz)
{
    for (int b = 0; b < kNumToneBands; ++b)
        if (kToneBands[b] == hz)
            return f.tone[b];
    return 0.0;
}

void printTuning()
{
    const auto& t = nh::tuning();
    auto row = [] (const char* name, const float* v, int n)
    {
        std::printf ("    float %s[%d] = {", name, n);
        for (int i = 0; i < n; ++i)
            std::printf ("%s %.3ff", i ? "," : "", v[i]);
        std::printf (" };\n");
    };
    row ("widthCross", t.widthCross, nh::Tuning::kWidthPoints);
    row ("widthSide", t.widthSide, nh::Tuning::kWidthPoints);
    std::printf ("    float modDepth50 = %.5ff, modDepth100 = %.5ff;\n    float modRate50 = %.3ff, modRate100 = %.3ff;\n", t.modDepth50, t.modDepth100, t.modRate50, t.modRate100);
    std::printf ("    float thickPivotDb = %.2ff, thickSlopePos = %.3ff, thickSlopeNeg = %.3ff, thickStaticPos = %.2ff, thickStaticNeg = %.2ff;\n", t.thickPivotDb,
                 t.thickSlopePos, t.thickSlopeNeg, t.thickStaticPos, t.thickStaticNeg);
    row ("brightHf", t.brightHf, nh::Tuning::kBrightPoints);
    row ("brightFreq", t.brightFreq, nh::Tuning::kBrightPoints);
    row ("brightAir", t.brightAir, nh::Tuning::kBrightPoints);
    row ("brightLsDb", t.brightLsDb, nh::Tuning::kBrightPoints);
    row ("brightHsDb", t.brightHsDb, nh::Tuning::kBrightPoints);
    row ("brightErCut", t.brightErCut, nh::Tuning::kBrightPoints);
    row ("brightGainDb", t.brightGainDb, nh::Tuning::kBrightPoints);
    row ("brightErDb", t.brightErDb, nh::Tuning::kBrightPoints);
    row ("brightErHsDb", t.brightErHsDb, nh::Tuning::kBrightPoints);
    row ("thickErDb", t.thickErDb, 5);
    std::printf ("    float decayLowShelfFreq = %.3ff, decayHighShelfFreq = %.3ff, decayShelfQ = %.3ff;\n", t.decayLowShelfFreq, t.decayHighShelfFreq, t.decayShelfQ);
}

// Decay Rate: time-based HF loss and decay correction, from the decay ratios T(rate) / T(100 %)
int rate (const DirResult& pro, const std::vector<Row>& plan)
{
    auto& t = nh::tuningMutable();
    const char* files[4] = { "D_DecayRate_025", "D_DecayRate_050", "D_DecayRate_200", "D_DecayRate_400" };
    const auto& p1 = pro.files.at ("B_Basis_A").left;
    static const double weight[kNumOctaves] = { 0.5, 0.5, 1.0, 1.5, 1.0, 1.0, 1.0, 0.6 };
    auto eval = [&] (const double* v, bool print)
    {
        t.airAbsRate = (float) nh::clampv (std::exp (v[0]), 0.001, 2.0);
        t.airAbsFreq = (float) nh::clampv (std::exp (v[1]), 1000.0, 18000.0);
        t.airAbsQ = (float) nh::clampv (std::exp (v[2]), 0.1, 2.0);
        t.rateCorr[0] = (float) std::exp (v[3]);
        t.rateCorr[1] = (float) std::exp (v[4]);
        t.rateCorr[3] = (float) std::exp (v[5]);
        t.rateCorr[4] = (float) std::exp (v[6]);
        const auto n1 = measureCode (plan, "B_Basis_A").left;
        double c = 0.0;
        for (const char* f : files)
        {
            const auto nr = measureCode (plan, f).left;
            const auto& pr = pro.files.at (f).left;
            if (print)
                std::printf ("%-16s", f);
            for (int b = 0; b < kNumOctaves; ++b)
            {
                const double e = std::log ((nr.t30[b] / n1.t30[b]) / (pr.t30[b] / p1.t30[b]));
                c += weight[b] * (e / 0.03) * (e / 0.03);
                if (print)
                    std::printf (" %+5.1f%%", 100.0 * (std::exp (e) - 1.0));
            }
            if (print)
                std::printf ("\n");
        }
        return c;
    };
    double x[7] = { std::log (t.airAbsRate), std::log (t.airAbsFreq), std::log (t.airAbsQ), std::log (t.rateCorr[0]), std::log (t.rateCorr[1]),
                    std::log (t.rateCorr[3]), std::log (t.rateCorr[4]) };
    const double step[7] = { 0.4, 0.3, 0.3, 0.08, 0.05, 0.05, 0.05 };
    nelderMead<7> (x, step, [&] (const double* v) { return eval (v, false); }, 90);
    eval (x, true);
    std::printf ("    float airAbsRate = %.4ff, airAbsFreq = %.1ff, airAbsQ = %.3ff;\n    float rateCorr[5] = { %.3ff, %.3ff, 1.0f, %.3ff, %.3ff };\n", t.airAbsRate, t.airAbsFreq,
                 t.airAbsQ, t.rateCorr[0], t.rateCorr[1], t.rateCorr[3], t.rateCorr[4]);
    return 0;
}

// Distance: early reflection level and low-pass, late build-up at 0 / 25 / 75 / 100 % -> clarity C50,
// centre time Ts, early reflection brightness and level
int distance (const DirResult& pro, const std::vector<Row>& plan)
{
    auto& t = nh::tuningMutable();
    const struct { int idx; const char* file; } pts[] = { { 1, "E_Distance_025" }, { 0, "E_Distance_000" }, { 3, "E_Distance_075" }, { 4, "E_Distance_100" } };
    for (const auto& pt : pts)
    {
        const auto& tg = pro.files.at (pt.file).left;
        auto eval = [&] (const double* v, bool print)
        {
            t.distEr[pt.idx] = (float) nh::clampv (std::exp (v[0]), 0.02, 20.0);
            t.distErCut[pt.idx] = (float) nh::clampv (std::exp (v[1]), 0.1, 5.0);
            t.distBuild[pt.idx] = (float) nh::clampv (v[2], 0.0, 12.0); // far away: Pro-R 2 builds up slowly
            t.distLateDb[pt.idx] = (float) nh::clampv (v[3], -12.0, 12.0);
            t.distDiffIn[pt.idx] = (float) nh::clampv (v[4], 0.0, 1.2);
            const auto m = measureCode (plan, pt.file).left;
            const double dc = (m.c50 - tg.c50) / 0.7;
            const double dt = std::log (std::max (1.0, m.ts) / std::max (1.0, tg.ts)) / 0.06;
            const double de = std::log (m.erCentroid / tg.erCentroid) / 0.06;
            const double dl = (m.levelDb - tg.levelDb) / 0.7;
            const double dd = std::log (std::max (0.01, m.edtMid) / std::max (0.01, tg.edtMid)) / 0.05;
            const double n1 = (m.density[1] - tg.density[1]) / 0.1, n2 = (m.density[2] - tg.density[2]) / 0.1, n3 = (m.density[3] - tg.density[3]) / 0.1;
            if (print)
                std::printf ("%-15s C50 %+6.2f/%+6.2f  Ts %4.0f/%4.0f  EDT mid %5.2f/%5.2f  centroid %5.0f/%5.0f  level %+5.2f/%+5.2f  NED %.2f/%.2f %.2f/%.2f %.2f/%.2f -> er x%.3f cut x%.3f build %.3f late %+.2f dB diff %.2f\n",
                             pt.file, m.c50, tg.c50, m.ts, tg.ts, m.edtMid, tg.edtMid, m.erCentroid, tg.erCentroid, m.levelDb, tg.levelDb, m.density[1], tg.density[1],
                             m.density[2], tg.density[2], m.density[3], tg.density[3], t.distEr[pt.idx], t.distErCut[pt.idx], t.distBuild[pt.idx], t.distLateDb[pt.idx],
                             t.distDiffIn[pt.idx]);
            return dc * dc + dt * dt + de * de + dl * dl + dd * dd + n1 * n1 + n2 * n2 + n3 * n3;
        };
        double x[5] = { std::log (t.distEr[pt.idx]), std::log (t.distErCut[pt.idx]), t.distBuild[pt.idx], t.distLateDb[pt.idx], t.distDiffIn[pt.idx] };
        const double step[5] = { 0.4, 0.3, pt.idx == 0 ? 0.05 : (pt.idx == 4 ? 1.0 : 0.15), 1.5, 0.3 };
        nelderMead<5> (x, step, [&] (const double* v) { return eval (v, false); }, 55);
        eval (x, true);
    }
    auto row = [] (const char* name, const float* v)
    {
        std::printf ("    float %s[5] = {", name);
        for (int i = 0; i < 5; ++i)
            std::printf ("%s %.3ff", i ? "," : "", v[i]);
        std::printf (" };\n");
    };
    row ("distEr", t.distEr);
    row ("distErCut", t.distErCut);
    row ("distBuild", t.distBuild);
    row ("distLateDb", t.distLateDb);
    row ("distDiffIn", t.distDiffIn);
    return 0;
}

// Styles Vintage / Plate: decay time, high frequency decay, level and early part at the three
// recorded decay times
int styles (const DirResult& pro, const std::vector<Row>& plan, const std::string& which)
{
    auto& t = nh::tuningMutable();
    const int k = which == "plate" ? 1 : 0;
    const std::string st = k == 1 ? "Plate" : "Vintage";
    const struct { int idx; std::string file; } pts[] = { { 1, "S_" + st + "_Basis" }, { 0, "S_" + st + "_Klein" }, { 2, "S_" + st + "_Groesster" } };
    for (const auto& pt : pts)
    {
        const auto& tg = pro.files.at (pt.file).left;
        auto eval = [&] (const double* v, bool print)
        {
            // ranges keep the sound sensible (the early part must not replace the reverb)
            t.styleT60[k][pt.idx] = (float) nh::clampv (std::exp (v[0]), 0.3, 2.0);
            t.styleHf[k][pt.idx] = (float) nh::clampv (std::exp (v[1]), 0.05, 2.0);
            t.styleLevelDb[k][pt.idx] = (float) nh::clampv (v[2], -9.0, 9.0);
            t.styleEarlyDb[k][pt.idx] = (float) nh::clampv (v[3], -12.0, 16.0);
            const auto m = measureCode (plan, pt.file).left;
            const double em = (logMid (m.t30) - logMid (tg.t30)) / 0.03;
            const double eh = (std::log (m.t30[6] / m.t30[3]) - std::log (tg.t30[6] / tg.t30[3])) / 0.05;
            const double el = (m.levelDb - tg.levelDb) / 0.5;
            const double ec = (m.c50 - tg.c50) / 0.7;
            if (print)
                std::printf ("%-20s T30 mid %.2f/%.2f s  8k/1k %.2f/%.2f  level %+5.2f/%+5.2f  C50 %+6.2f/%+6.2f  Ts %4.0f/%4.0f -> t60 x%.3f hf x%.3f level %+.2f early %+.2f\n",
                             pt.file.c_str(), std::exp (logMid (m.t30)), std::exp (logMid (tg.t30)), m.t30[6] / m.t30[3], tg.t30[6] / tg.t30[3], m.levelDb, tg.levelDb, m.c50,
                             tg.c50, m.ts, tg.ts, t.styleT60[k][pt.idx], t.styleHf[k][pt.idx], t.styleLevelDb[k][pt.idx], t.styleEarlyDb[k][pt.idx]);
            return em * em + eh * eh + el * el + ec * ec;
        };
        double x[4] = { std::log (t.styleT60[k][pt.idx]), std::log (t.styleHf[k][pt.idx]), t.styleLevelDb[k][pt.idx], t.styleEarlyDb[k][pt.idx] };
        const double step[4] = { 0.15, 0.2, 1.5, 4.0 };
        nelderMead<4> (x, step, [&] (const double* v) { return eval (v, false); }, 45);
        eval (x, true);
        if (pt.idx == 1) // start the other points from the base result
            for (int j : { 0, 2 })
            {
                t.styleT60[k][j] = t.styleT60[k][1];
                t.styleHf[k][j] = t.styleHf[k][1];
                t.styleLevelDb[k][j] = t.styleLevelDb[k][1];
                t.styleEarlyDb[k][j] = t.styleEarlyDb[k][1];
            }
    }
    auto row = [k] (const char* name, const float (*v)[3])
    {
        std::printf ("    %s[%d] = { %.3ff, %.3ff, %.3ff }\n", name, k, v[k][0], v[k][1], v[k][2]);
    };
    row ("styleT60", t.styleT60);
    row ("styleHf", t.styleHf);
    row ("styleLevelDb", t.styleLevelDb);
    row ("styleEarlyDb", t.styleEarlyDb);
    return 0;
}

// start of the decay per room: early reflection level, early taps of the late reverb and build-up ->
// clarity C50, centre time Ts and early decay time of the mid bands; level kept at the target
// brightness of the first 30 ms per room through the early reflection low-pass (a few multiplicative
// steps), level kept at the target
int erCut (const DirResult& pro, const std::vector<Row>& plan, int firstRoom, int lastRoom)
{
    for (int i = std::max (0, firstRoom); i <= std::min (lastRoom, nh::kNumRoomModels - 1); ++i)
    {
        const auto& tgt = pro.files.at (kRoomFiles[i]).left;
        const Row* row = rowFor (plan, kRoomFiles[i]);
        nh::RoomModel& m = nh::roomModelsMutable()[i];
        nh::EngineParams ep;
        std::string why;
        nachhallParams ("", ep, why);
        ep.space = nh::spaceForRoomIndex (i);
        FileResult r;
        for (int it = 0; it < 4; ++it)
        {
            r = measureNachhall (ep, *row);
            std::printf ("%-10s it %d: centroid %5.0f/%5.0f  C50 %+6.2f/%+6.2f  cut %.0f\n", kRoomFiles[i], it, r.left.erCentroid, tgt.erCentroid, r.left.c50, tgt.c50,
                         m.erCutoff);
            m.erCutoff = (float) nh::clampv (m.erCutoff * std::pow (tgt.erCentroid / std::max (100.0, r.left.erCentroid), 1.2), 2000.0, 22000.0);
        }
        r = measureNachhall (ep, *row);
        m.levelDb += (float) (tgt.levelDb - r.left.levelDb);
        std::printf ("%-10s -> centroid %5.0f/%5.0f cut %.0f\n", kRoomFiles[i], r.left.erCentroid, tgt.erCentroid, m.erCutoff);
    }
    printRooms();
    return 0;
}

// Character: level and brightness of the early reflections away from 50 % (C50, centroid of 0-30 ms)
int characterEarly (const DirResult& pro, const std::vector<Row>& plan)
{
    auto& t = nh::tuningMutable();
    const char* files[] = { "C_Character_000_A", "C_Character_025_A", "C_Character_075_A", "C_Character_100_A" };
    auto eval = [&] (const double* v, bool print)
    {
        t.charErDb = (float) nh::clampv (v[0], -8.0, 4.0);
        t.charErCut = (float) nh::clampv (std::exp (v[1]), 0.2, 1.5);
        double c = 0.0;
        for (const char* f : files)
        {
            const auto& tg = pro.files.at (f).left;
            const auto m = measureCode (plan, f).left;
            const double ec = (m.c50 - tg.c50) / 0.5, ee = std::log (m.erCentroid / tg.erCentroid) / 0.05;
            c += ec * ec + ee * ee;
            if (print)
                std::printf ("%-18s C50 %+6.2f/%+6.2f  centroid %5.0f/%5.0f\n", f, m.c50, tg.c50, m.erCentroid, tg.erCentroid);
        }
        return c;
    };
    double x[2] = { t.charErDb, std::log (t.charErCut) };
    const double step[2] = { 1.0, 0.2 };
    nelderMead<2> (x, step, [&] (const double* v) { return eval (v, false); }, 30);
    eval (x, true);
    std::printf ("    float charErDb = %.2ff;\n    float charErCut = %.3ff;\n", t.charErDb, t.charErCut);
    return 0;
}

// Vintage / Plate: tone of the late reverb at the three style points (63 Hz ... 16 kHz), level kept
int styleTone (const DirResult& pro, const std::vector<Row>& plan, const std::string& which)
{
    auto& t = nh::tuningMutable();
    const int k = which == "plate" ? 1 : 0;
    const std::string st = k == 1 ? "Plate" : "Vintage";
    const double toneHz[8] = { 63, 125, 250, 500, 2000, 4000, 8000, 16000 };
    const struct { int idx; std::string file; } pts[] = { { 1, "S_" + st + "_Basis" }, { 0, "S_" + st + "_Klein" }, { 2, "S_" + st + "_Groesster" } };
    for (const auto& pt : pts)
    {
        const auto& tg = pro.files.at (pt.file).left;
        FileResult last;
        auto eval = [&] (const double* v)
        {
            t.styleLsDb[k][pt.idx] = (float) nh::clampv (v[0], -12.0, 12.0);
            t.styleHsDb[k][pt.idx] = (float) nh::clampv (v[1], -12.0, 12.0);
            t.styleAirDb[k][pt.idx] = (float) nh::clampv (v[2], -15.0, 12.0);
            last = measureCode (plan, pt.file);
            double c = 0.0;
            for (double hz : toneHz)
            {
                const double e = toneAt (last.left, hz) - toneAt (tg, hz);
                c += e * e;
            }
            const double etc = std::log (std::max (100.0, last.left.tailCentroid) / std::max (100.0, tg.tailCentroid)) / 0.04;
            return c + etc * etc + 0.01 * (v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        };
        double x[3] = { t.styleLsDb[k][pt.idx], t.styleHsDb[k][pt.idx], t.styleAirDb[k][pt.idx] };
        const double step[3] = { 3.0, 2.0, 3.0 };
        nelderMead<3> (x, step, eval, 40);
        eval (x);
        t.styleLevelDb[k][pt.idx] += (float) (tg.levelDb - last.left.levelDb);
        std::printf ("%-20s LS %+5.2f HS %+5.2f air %+5.2f dB:", pt.file.c_str(), t.styleLsDb[k][pt.idx], t.styleHsDb[k][pt.idx], t.styleAirDb[k][pt.idx]);
        for (double hz : toneHz)
            std::printf (" %5.0f %+5.1f/%+5.1f", hz, toneAt (last.left, hz), toneAt (tg, hz));
        std::printf ("\n");
    }
    auto row = [k] (const char* name, float (*v)[3])
    {
        std::printf ("    %s[%d] = { %.3ff, %.3ff, %.3ff }\n", name, k, v[k][0], v[k][1], v[k][2]);
    };
    row ("styleLsDb", t.styleLsDb);
    row ("styleHsDb", t.styleHsDb);
    row ("styleAirDb", t.styleAirDb);
    row ("styleLevelDb", t.styleLevelDb);
    return 0;
}

// echo density of the late reverb per room, 20 ... 200 ms, through the room size (line lengths of
// the late reverb): Pro-R 2 builds up its density more slowly than short lines would; level kept
int density (const DirResult& pro, const std::vector<Row>& plan, int firstRoom, int lastRoom)
{
    for (int i = std::max (0, firstRoom); i <= std::min (lastRoom, nh::kNumRoomModels - 1); ++i)
    {
        const auto& tgt = pro.files.at (kRoomFiles[i]).left;
        const Row* row = rowFor (plan, kRoomFiles[i]);
        nh::RoomModel& m = nh::roomModelsMutable()[i];
        nh::EngineParams ep;
        std::string why;
        nachhallParams ("", ep, why);
        ep.space = nh::spaceForRoomIndex (i);
        const float x0 = m.dimX, y0 = m.dimY, z0 = m.dimZ;
        double best = 1.0e30, bestK = 1.0;
        FileResult bestR;
        for (double k : { 1.0, 0.5, 0.7, 0.85, 1.2, 1.45, 1.75, 2.1, 2.5, 3.0, 3.6 }) // 1.0 first: kept on a tie
        {
            m.dimX = (float) (x0 * k);
            m.dimY = (float) (y0 * k);
            m.dimZ = (float) (z0 * k);
            const auto r = measureNachhall (ep, *row);
            const double e1 = (r.left.density[1] - tgt.density[1]) / 0.06, e2 = (r.left.density[2] - tgt.density[2]) / 0.06,
                         e3 = (r.left.density[3] - tgt.density[3]) / 0.06;
            const double c = e1 * e1 + e2 * e2 + e3 * e3;
            std::printf ("%-10s size x%.2f: NED 20-50 %.2f/%.2f  50-100 %.2f/%.2f  100-200 %.2f/%.2f  cost %.2f\n", kRoomFiles[i], k, r.left.density[1],
                         tgt.density[1], r.left.density[2], tgt.density[2], r.left.density[3], tgt.density[3], c);
            if (c < best - 1.0e-6)
            {
                best = c;
                bestK = k;
                bestR = r;
            }
        }
        m.dimX = (float) (x0 * bestK);
        m.dimY = (float) (y0 * bestK);
        m.dimZ = (float) (z0 * bestK);
        m.levelDb += (float) (tgt.levelDb - bestR.left.levelDb);
        std::printf ("%-10s -> size x%.2f (%.1f x %.1f x %.1f m)\n", kRoomFiles[i], bestK, m.dimX, m.dimY, m.dimZ);
    }
    printRooms();
    return 0;
}

int earlyTaps (const DirResult& pro, const std::vector<Row>& plan, int firstRoom, int lastRoom)
{
    for (int i = std::max (0, firstRoom); i <= std::min (lastRoom, nh::kNumRoomModels - 1); ++i)
    {
        const auto& tgt = pro.files.at (kRoomFiles[i]).left;
        const Row* row = rowFor (plan, kRoomFiles[i]);
        nh::RoomModel& m = nh::roomModelsMutable()[i];
        nh::EngineParams ep;
        std::string why;
        nachhallParams ("", ep, why);
        ep.space = nh::spaceForRoomIndex (i);
        FileResult last;
        auto eval = [&] (const double* v)
        {
            m.erGain = (float) nh::clampv (std::exp (v[0]), 0.02, 8.0);
            m.earlyTap = (float) nh::clampv (v[1], 0.0, 6.0);
            m.buildScale = (float) nh::clampv (std::exp (v[2]), 0.05, 6.0);
            last = measureNachhall (ep, *row);
            const double ec = (last.left.c50 - tgt.c50) / 0.5;
            const double et = std::log (std::max (1.0, last.left.ts) / std::max (1.0, tgt.ts)) / 0.05;
            const double ee = std::log (std::max (0.01, last.left.edtMid) / std::max (0.01, tgt.edtMid)) / 0.03;
            // fine structure: strong single taps would make the start spiky (Pro-R 2: even train of reflections)
            const double d1 = (last.left.density[1] - tgt.density[1]) / 0.06, d2 = (last.left.density[2] - tgt.density[2]) / 0.08;
            return ec * ec + et * et + ee * ee + d1 * d1 + d2 * d2 + (v[1] < 0.0 ? 100.0 * v[1] * v[1] : 0.0);
        };
        double x[3] = { std::log (m.erGain), std::max (0.3, (double) m.earlyTap), std::log (m.buildScale) };
        const double step[3] = { 0.3, 0.4, 0.4 };
        nelderMead<3> (x, step, eval, 30);
        eval (x);
        // brightness of the first 30 ms through the early reflection low-pass
        m.erCutoff = (float) nh::clampv (m.erCutoff * std::pow (tgt.erCentroid / std::max (100.0, last.left.erCentroid), 1.5), 2000.0, 22000.0);
        m.levelDb += (float) (tgt.levelDb - last.left.levelDb);
        std::printf ("%-10s C50 %+6.2f/%+6.2f  Ts %4.0f/%4.0f  EDT mid %5.2f/%5.2f  EDT %5.2f/%5.2f  T30 1k %5.2f/%5.2f  NED %.2f/%.2f %.2f/%.2f -> er %.3f early %.3f build %.3f cut %.0f\n",
                     kRoomFiles[i], last.left.c50, tgt.c50, last.left.ts, tgt.ts, last.left.edtMid, tgt.edtMid, last.left.edt, tgt.edt, last.left.t30[3], tgt.t30[3],
                     last.left.density[1], tgt.density[1], last.left.density[2], tgt.density[2], m.erGain, m.earlyTap, m.buildScale, m.erCutoff);
    }
    printRooms();
    return 0;
}

// clarity C50 per room through the early reflection level only (Newton steps), level kept at the target
int roomsC50 (const DirResult& pro, const std::vector<Row>& plan)
{
    for (int i = 0; i < nh::kNumRoomModels; ++i)
    {
        const auto& tgt = pro.files.at (kRoomFiles[i]).left;
        const Row* row = rowFor (plan, kRoomFiles[i]);
        nh::RoomModel& m = nh::roomModelsMutable()[i];
        nh::EngineParams ep;
        std::string why;
        nachhallParams ("", ep, why);
        ep.space = nh::spaceForRoomIndex (i);
        for (int it = 0; it < 4; ++it)
        {
            const auto r = measureNachhall (ep, *row).left;
            std::printf ("%-10s it %d: C50 %+6.2f/%+6.2f  Ts %4.0f/%4.0f  level %+6.2f/%+6.2f  er %.3f\n", kRoomFiles[i], it, r.c50, tgt.c50, r.ts, tgt.ts, r.levelDb,
                         tgt.levelDb, m.erGain);
            if (it == 3)
            {
                m.levelDb += (float) (tgt.levelDb - r.levelDb);
                break;
            }
            m.erGain = (float) nh::clampv (m.erGain * std::pow (10.0, 0.9 * (tgt.c50 - r.c50) / 20.0), 0.02, 8.0);
        }
    }
    printRooms();
    return 0;
}

// clarity C50 per room through the level of the first strong reflections only (the train and the
// late reverb stay as they are), level kept at the target
int primaryC50 (const DirResult& pro, const std::vector<Row>& plan)
{
    for (int i = 0; i < nh::kNumRoomModels; ++i)
    {
        const auto& tgt = pro.files.at (kRoomFiles[i]).left;
        const Row* row = rowFor (plan, kRoomFiles[i]);
        nh::RoomModel& m = nh::roomModelsMutable()[i];
        nh::EngineParams ep;
        std::string why;
        nachhallParams ("", ep, why);
        ep.space = nh::spaceForRoomIndex (i);
        for (int it = 0; it < 5; ++it)
        {
            const auto r = measureNachhall (ep, *row).left;
            std::printf ("%-10s it %d: C50 %+6.2f/%+6.2f  Ts %4.0f/%4.0f  EDT mid %.2f/%.2f  T30 1k %.2f/%.2f  primary %+.2f dB\n", kRoomFiles[i], it, r.c50, tgt.c50, r.ts,
                         tgt.ts, r.edtMid, tgt.edtMid, r.t30[3], tgt.t30[3], m.erPrimaryDb);
            if (it == 4)
            {
                m.levelDb += (float) (tgt.levelDb - r.levelDb);
                break;
            }
            m.erPrimaryDb = (float) nh::clampv (m.erPrimaryDb + 1.5 * (tgt.c50 - r.c50), -12.0, 18.0);
        }
    }
    printRooms();
    return 0;
}

// Thickness: early reflection level -> clarity C50 at -100 / -50 / +50 / +100 % (Newton steps)
// Thickness: echo density of the start (Pro-R 2: -100 % very sparse, +100 % dense) through the
// single reflections (thinning / extra ones), the loop and input diffusion and the early taps;
// clarity and early decay kept
int thicknessDensity (const DirResult& pro, const std::vector<Row>& plan)
{
    auto& t = nh::tuningMutable();
    const struct { int idx; const char* file; } pts[] = { { 1, "T_Thickness_m050_A" }, { 0, "T_Thickness_m100_A" }, { 3, "T_Thickness_p050_A" }, { 4, "T_Thickness_p100_A" } };
    for (const auto& pt : pts)
    {
        const auto& tg = pro.files.at (pt.file).left;
        const bool below = pt.idx < 2;
        auto eval = [&] (const double* v, bool print)
        {
            if (below)
                t.thickThin[pt.idx] = (float) nh::clampv (v[0], 0.0, 0.95);
            else
                t.thickExtra[pt.idx] = (float) nh::clampv (v[0], 0.0, 2.0);
            t.diffLoop[pt.idx] = (float) nh::clampv (v[1], 0.0, 1.6);
            t.thickTap[pt.idx] = (float) nh::clampv (v[2], 0.0, 2.0);
            if (! below)
                t.diffIn[pt.idx] = (float) nh::clampv (v[3], 0.0, 1.2);
            const auto m = measureCode (plan, pt.file).left;
            const double d1 = (m.density[1] - tg.density[1]) / 0.04, d2 = (m.density[2] - tg.density[2]) / 0.05, d3 = (m.density[3] - tg.density[3]) / 0.05;
            const double ec = (m.c50 - tg.c50) / 1.0, ee = std::log (std::max (0.01, m.edtMid) / std::max (0.01, tg.edtMid)) / 0.06;
            if (print)
                std::printf ("%-20s NED %.2f/%.2f %.2f/%.2f %.2f/%.2f  C50 %+6.2f/%+6.2f  EDT mid %.2f/%.2f -> %s %.3f loop %.3f taps %.3f in %.3f\n", pt.file,
                             m.density[1], tg.density[1], m.density[2], tg.density[2], m.density[3], tg.density[3], m.c50, tg.c50, m.edtMid, tg.edtMid,
                             below ? "thin" : "extra", below ? t.thickThin[pt.idx] : t.thickExtra[pt.idx], t.diffLoop[pt.idx], t.thickTap[pt.idx], t.diffIn[pt.idx]);
            return d1 * d1 + d2 * d2 + d3 * d3 + ec * ec + ee * ee;
        };
        double x[4] = { below ? t.thickThin[pt.idx] : t.thickExtra[pt.idx], t.diffLoop[pt.idx], t.thickTap[pt.idx], t.diffIn[pt.idx] };
        const double step[4] = { below ? 0.3 : 0.4, 0.3, 0.25, 0.2 };
        if (below)
            nelderMead<3> (x, step, [&] (const double* v) { return eval (v, false); }, 40);
        else
            nelderMead<4> (x, step, [&] (const double* v) { return eval (v, false); }, 40);
        eval (x, true);
    }
    auto row = [] (const char* name, const float* v)
    {
        std::printf ("    float %s[5] = {", name);
        for (int i = 0; i < 5; ++i)
            std::printf ("%s %.3ff", i ? "," : "", v[i]);
        std::printf (" };\n");
    };
    row ("thickThin", t.thickThin);
    row ("thickExtra", t.thickExtra);
    row ("diffLoop", t.diffLoop);
    row ("thickTap", t.thickTap);
    row ("diffIn", t.diffIn);
    return 0;
}

int thicknessC50 (const DirResult& pro, const std::vector<Row>& plan)
{
    auto& t = nh::tuningMutable();
    const struct { int idx; const char* file; } pts[] = { { 0, "T_Thickness_m100_A" }, { 1, "T_Thickness_m050_A" }, { 3, "T_Thickness_p050_A" }, { 4, "T_Thickness_p100_A" } };
    for (const auto& pt : pts)
    {
        const auto& tg = pro.files.at (pt.file).left;
        for (int it = 0; it < 4; ++it)
        {
            const auto m = measureCode (plan, pt.file).left;
            std::printf ("%-20s it %d: C50 %+6.2f/%+6.2f  level %+6.2f/%+6.2f  er %+.2f dB\n", pt.file, it, m.c50, tg.c50, m.levelDb, tg.levelDb, t.thickErDb[pt.idx]);
            t.thickErDb[pt.idx] = (float) nh::clampv (t.thickErDb[pt.idx] + 0.9 * (tg.c50 - m.c50), -12.0, 12.0);
        }
    }
    printTuning();
    return 0;
}

// Decay Rate EQ shelves: frequency and Q mapping from the two shelf recordings
int decayEq (const DirResult& pro, const std::vector<Row>& plan)
{
    auto& t = nh::tuningMutable();
    const auto& p0 = pro.files.at ("B_Basis_A").left;
    const auto n0 = measureCode (plan, "B_Basis_A").left;
    const struct { const char* file; int b0, b1; } sets[] = { { "X_DecayEQ_LowShelf", 0, 3 }, { "X_DecayEQ_HighShelf", 4, 7 } };
    auto eval = [&] (const double* v, bool print)
    {
        t.decayLowShelfFreq = (float) nh::clampv (std::exp (v[0]), 0.25, 4.0);
        t.decayHighShelfFreq = (float) nh::clampv (std::exp (v[1]), 0.25, 4.0);
        t.decayShelfQ = (float) nh::clampv (std::exp (v[2]), 0.2, 4.0);
        double c = 0.0;
        for (const auto& set : sets)
        {
            const auto nv = measureCode (plan, set.file).left;
            const auto& pv = pro.files.at (set.file).left;
            if (print)
                std::printf ("%-20s", set.file);
            for (int b = set.b0; b <= set.b1; ++b)
            {
                const double e = std::log ((nv.t30[b] / n0.t30[b]) / (pv.t30[b] / p0.t30[b]));
                c += (e / 0.03) * (e / 0.03);
                if (print)
                    std::printf ("  %5.0f Hz x%.2f/x%.2f", kOctaves[b], nv.t30[b] / n0.t30[b], pv.t30[b] / p0.t30[b]);
            }
            if (print)
                std::printf ("\n");
        }
        return c;
    };
    double x[3] = { std::log (t.decayLowShelfFreq), std::log (t.decayHighShelfFreq), std::log (t.decayShelfQ) };
    const double step[3] = { 0.4, 0.4, 0.4 };
    nelderMead<3> (x, step, [&] (const double* v) { return eval (v, false); }, 40);
    eval (x, true);
    std::printf ("    float decayLowShelfFreq = %.3ff, decayHighShelfFreq = %.3ff, decayShelfQ = %.3ff;\n", t.decayLowShelfFreq, t.decayHighShelfFreq, t.decayShelfQ);
    return 0;
}

// Brightness: decay change, tone change and early reflection brightness relative to the base.
// The 50 % points are fitted first, the 100 % points must not be less extreme (no dips when turning
// the knob), the HF corner stays at the room value.
int brightness (const DirResult& pro, const std::vector<Row>& plan)
{
    const char* files[5] = { "H_Brightness_m100", "H_Brightness_m050", "B_Basis_A", "H_Brightness_p050", "H_Brightness_p100" };
    auto& t = nh::tuningMutable();
    const auto& p0 = pro.files.at ("B_Basis_A").left;
    const FileResult n0 = measureCode (plan, "B_Basis_A");
    const double toneHz[8] = { 63, 125, 250, 500, 2000, 4000, 8000, 16000 };
    for (int k = 0; k < nh::Tuning::kBrightPoints; ++k)
        t.brightFreq[k] = 1.0f;
    for (int k : { 1, 3, 0, 4 })
    {
        const int inner = k == 0 ? 1 : (k == 4 ? 3 : 2); // the neighbour towards the base
        const double dir = k < 2 ? -1.0 : 1.0;           // darker / brighter
        const auto& pv = pro.files.at (files[k]).left;
        auto eval = [&] (const double* v, bool print)
        {
            t.brightHf[k] = (float) std::exp (v[0]);
            t.brightAir[k] = (float) std::exp (v[1]);
            t.brightLsDb[k] = (float) v[2];
            t.brightHsDb[k] = (float) v[3];
            t.brightErCut[k] = (float) nh::clampv (std::exp (v[4]), 0.15, 4.0);
            t.brightGainDb[k] = (float) nh::clampv (v[5], -12.0, 12.0);
            t.brightErDb[k] = (float) nh::clampv (v[6], -12.0, 12.0);
            t.brightErHsDb[k] = (float) nh::clampv (v[7], -15.0, 8.0);
            const auto nv = measureCode (plan, files[k]).left;
            double c = 0.0;
            for (int b = 4; b < kNumOctaves; ++b) // 2k ... 16k
            {
                const double e = std::log ((nv.t30[b] / n0.left.t30[b]) / (pv.t30[b] / p0.t30[b])) / 0.04;
                c += e * e;
            }
            for (double hz : toneHz)
            {
                const double e = ((toneAt (nv, hz) - toneAt (n0.left, hz)) - (toneAt (pv, hz) - toneAt (p0, hz))) / 1.0;
                c += e * e;
            }
            const double ec = std::log ((nv.erCentroid / n0.left.erCentroid) / (pv.erCentroid / p0.erCentroid)) / 0.05;
            const double el = ((nv.levelDb - n0.left.levelDb) - (pv.levelDb - p0.levelDb)) / 0.5;
            const double e50 = ((nv.c50 - n0.left.c50) - (pv.c50 - p0.c50)) / 0.7;
            const double eed = std::log ((nv.edtMid / n0.left.edtMid) / (pv.edtMid / p0.edtMid)) / 0.05;
            // brightness of the late reverb itself (centroid 0.1-0.4 s)
            const double etc = std::log ((std::max (50.0, nv.tailCentroid) / n0.left.tailCentroid) / (std::max (50.0, pv.tailCentroid) / p0.tailCentroid)) / 0.05;
            c += ec * ec + el * el + e50 * e50 + eed * eed + etc * etc;
            // monotonic towards the neighbour, mild preference for small shelf gains
            auto hinge = [] (double x) { return x > 0.0 ? x : 0.0; };
            const double m[5] = { hinge (dir * (std::log (t.brightHf[inner]) - v[0])) / 0.03, hinge (dir * (std::log (t.brightAir[inner]) - v[1])) / 0.03,
                                  hinge (dir * (v[2] - t.brightLsDb[inner])) / 0.3, hinge (dir * (t.brightHsDb[inner] - v[3])) / 0.3,
                                  hinge (dir * (std::log (t.brightErCut[inner]) - v[4])) / 0.03 };
            for (double x : m)
                c += x * x;
            c += (v[2] / 8.0) * (v[2] / 8.0) + (v[3] / 12.0) * (v[3] / 12.0) + (v[5] / 6.0) * (v[5] / 6.0) + (v[6] / 14.0) * (v[6] / 14.0) + (v[7] / 12.0) * (v[7] / 12.0);
            if (print)
                std::printf ("%-18s cost %.2f: level %+.1f/%+.1f dB, C50 %+.1f/%+.1f dB, EDT mid x%.2f/x%.2f, centroid x%.2f/x%.2f, tail x%.2f/x%.2f, T30 8k %.2f/%.2f s\n",
                             files[k], c, nv.levelDb - n0.left.levelDb, pv.levelDb - p0.levelDb, nv.c50 - n0.left.c50, pv.c50 - p0.c50, nv.edtMid / n0.left.edtMid,
                             pv.edtMid / p0.edtMid, nv.erCentroid / n0.left.erCentroid, pv.erCentroid / p0.erCentroid, nv.tailCentroid / n0.left.tailCentroid,
                             pv.tailCentroid / p0.tailCentroid, nv.t30[6], pv.t30[6]);
            return c;
        };
        // start from the current values of this point (a refit only polishes them)
        double x[8] = { std::log (t.brightHf[k]), std::log (t.brightAir[k]), t.brightLsDb[k], t.brightHsDb[k], std::log (t.brightErCut[k]), t.brightGainDb[k],
                        t.brightErDb[k], t.brightErHsDb[k] };
        const double step[8] = { 0.15 * dir, 0.2 * dir, -1.5 * dir, 1.5 * dir, 0.15 * dir, 1.0 * dir, 2.0 * dir, -2.0 * dir };
        nelderMead<8> (x, step, [&] (const double* v) { return eval (v, false); }, 100);
        eval (x, true);
    }
    printTuning();
    return 0;
}

// Stereo Width: cross-feed and correlation at 50 / 70 / 120 %
int width (const DirResult& pro, const std::vector<Row>& plan)
{
    auto& t = nh::tuningMutable();
    const struct { int idx; const char* file; } pts[] = { { 1, "W_Width_050" }, { 2, "W_Width_070" }, { 4, "W_Width_120" } };
    for (const auto& pt : pts)
    {
        const auto& tgt = pro.files.at (pt.file);
        auto eval = [&] (const double* v, bool print)
        {
            t.widthCross[pt.idx] = (float) nh::clampv (v[0], 0.0, 1.0);
            t.widthSide[pt.idx] = (float) nh::clampv (v[1], 0.0, 3.0);
            const auto m = measureCode (plan, pt.file);
            const double ec = (m.left.crossDb - tgt.left.crossDb) / 0.5;
            const double er = (m.left.corrLR - tgt.left.corrLR) / 0.05;
            const double em = (m.monoCorr - tgt.monoCorr) / 0.1;
            if (print)
                std::printf ("%-12s cross %+.1f/%+.1f dB  corr %+.2f/%+.2f  mono %+.2f/%+.2f\n", pt.file, m.left.crossDb, tgt.left.crossDb, m.left.corrLR, tgt.left.corrLR,
                             m.monoCorr, tgt.monoCorr);
            return ec * ec + er * er + em * em;
        };
        double x[2] = { t.widthCross[pt.idx], t.widthSide[pt.idx] };
        const double step[2] = { 0.1, 0.1 };
        nelderMead<2> (x, step, [&] (const double* v) { return eval (v, false); }, 30);
        eval (x, true);
    }
    printTuning();
    return 0;
}

// Character: tone width and time variance at 50 % and 100 %
int character (const DirResult& pro, const std::vector<Row>& plan)
{
    auto& t = nh::tuningMutable();
    for (int k = 0; k < 2; ++k)
    {
        const char* file = k == 0 ? "B_Basis_B" : "C_Character_100_B";
        const auto& tgt = pro.files.at (file);
        auto eval = [&] (const double* v, bool print)
        {
            float& depth = k == 0 ? t.modDepth50 : t.modDepth100;
            float& rate = k == 0 ? t.modRate50 : t.modRate100;
            depth = (float) nh::clampv (std::exp (v[0]), 1.0e-5, 0.01);
            rate = (float) nh::clampv (std::exp (v[1]), 0.01, 5.0);
            const auto m = measureCode (plan, file);
            const double ew = std::log (std::max (0.1, m.toneWidthHz) / tgt.toneWidthHz) / 0.1;
            const double et = (m.timeVar1kDb - tgt.timeVar1kDb) / 1.0;
            if (print)
                std::printf ("%-18s tone width %.1f/%.1f Hz  spread %.0f/%.0f %%  time variance 1 kHz %+.1f/%+.1f dB  depth %.2f ms rate %.2f Hz\n", file, m.toneWidthHz,
                             tgt.toneWidthHz, 100.0 * m.toneSpread, 100.0 * tgt.toneSpread, m.timeVar1kDb, tgt.timeVar1kDb, 1000.0 * depth, rate);
            return ew * ew + et * et;
        };
        double x[2] = { std::log (k == 0 ? t.modDepth50 : t.modDepth100), std::log (k == 0 ? t.modRate50 : t.modRate100) };
        const double step[2] = { 0.4, 0.4 };
        nelderMead<2> (x, step, [&] (const double* v) { return eval (v, false); }, 25);
        eval (x, true);
    }
    printTuning();
    return 0;
}

// Thickness: gains of loud / quiet noise, tone and impulse relative to the base
int thickness (const DirResult& pro, const std::vector<Row>& plan)
{
    auto& t = nh::tuningMutable();
    const auto& pb = pro.files.at ("B_Basis_B");
    const auto& pa = pro.files.at ("B_Basis_A").left;
    const FileResult nb = measureCode (plan, "B_Basis_B");
    const FileResult na = measureCode (plan, "B_Basis_A");
    auto eval = [&] (const double* v, bool print)
    {
        t.thickPivotDb = (float) v[0];
        t.thickSlopePos = (float) v[1];
        t.thickSlopeNeg = (float) v[2];
        t.thickStaticPos = (float) v[3];
        t.thickStaticNeg = (float) v[4];
        double c = 0.0;
        for (const char* f : { "T_Thickness_m100_B", "T_Thickness_p100_B" })
        {
            const auto m = measureCode (plan, f);
            const auto& tg = pro.files.at (f);
            const double e1 = ((m.loudGainDb - nb.loudGainDb) - (tg.loudGainDb - pb.loudGainDb)) / 0.5;
            const double e2 = ((m.quietGainDb - nb.quietGainDb) - (tg.quietGainDb - pb.quietGainDb)) / 0.5;
            const double e3 = ((m.toneGainDb - nb.toneGainDb) - (tg.toneGainDb - pb.toneGainDb)) / 1.5;
            c += e1 * e1 + e2 * e2 + e3 * e3;
            if (print)
                std::printf ("%-20s loud %+5.1f/%+5.1f  quiet %+5.1f/%+5.1f  tone %+5.1f/%+5.1f dB\n", f, m.loudGainDb - nb.loudGainDb, tg.loudGainDb - pb.loudGainDb,
                             m.quietGainDb - nb.quietGainDb, tg.quietGainDb - pb.quietGainDb, m.toneGainDb - nb.toneGainDb, tg.toneGainDb - pb.toneGainDb);
        }
        for (const char* f : { "T_Thickness_m100_A", "T_Thickness_m050_A", "T_Thickness_p050_A", "T_Thickness_p100_A" })
        {
            const auto m = measureCode (plan, f).left;
            const auto& tg = pro.files.at (f).left;
            const double e = ((m.levelDb - na.left.levelDb) - (tg.levelDb - pa.levelDb)) / 1.5;
            c += e * e;
            if (print)
                std::printf ("%-20s impulse %+5.1f/%+5.1f dB\n", f, m.levelDb - na.left.levelDb, tg.levelDb - pa.levelDb);
        }
        return c;
    };
    double x[5] = { t.thickPivotDb, t.thickSlopePos, t.thickSlopeNeg, t.thickStaticPos, t.thickStaticNeg };
    const double step[5] = { 4.0, 0.1, 0.1, 2.0, 2.0 };
    nelderMead<5> (x, step, [&] (const double* v) { return eval (v, false); }, 60);
    eval (x, true);
    printTuning();
    return 0;
}
} // namespace fit

//==============================================================================
// Match test: renders the whole plan through Nachhall and compares the measurements with the
// reference features (CSV written by "analyze --csv" for the recordings). Fails when a deviation
// exceeds its tolerance - keeps the fit from silently getting worse.
namespace match
{
using FeatureRow = std::map<std::string, std::string>;

std::map<std::string, FeatureRow> readFeatures (const std::string& path, const std::string& source)
{
    std::map<std::string, FeatureRow> out;
    std::ifstream in (path);
    std::string line;
    std::vector<std::string> header;
    while (std::getline (in, line))
    {
        if (! line.empty() && line.back() == '\r')
            line.pop_back();
        const auto cols = readCsvLine (line, ',');
        if (header.empty())
        {
            header = cols;
            continue;
        }
        FeatureRow r;
        for (size_t i = 0; i < cols.size() && i < header.size(); ++i)
            r[header[i]] = cols[i];
        if (r["source"] == source)
            out[r["file"]] = r;
    }
    return out;
}

double num (const FeatureRow& r, const std::string& key)
{
    const auto it = r.find (key);
    return it == r.end() ? 0.0 : std::atof (it->second.c_str());
}

// One measure over all recordings. Passes when 90 % of the deviations are within the limit and the
// worst one within 2.5 x the limit (single recordings can hit known differences, see
// docs/VERGLEICH-PRO-R.md; a real regression moves the bulk of the values).
// A known difference (guard > 0) is not matched yet: its limit is what Pro-R would need, the check
// only fails when it gets worse than the guard.
struct Check
{
    Check (const char* w, double l, double g = 0.0) : what (w), limit (l), guard (g) {}
    const char* what;
    double limit, guard, worst = 0.0, sum = 0.0;
    int count = 0;
    std::string worstFile;
    std::vector<double> all;
    void add (double dev, const std::string& file)
    {
        sum += dev;
        ++count;
        all.push_back (dev);
        if (dev > worst)
        {
            worst = dev;
            worstFile = file;
        }
    }
    double p90() const
    {
        if (all.empty())
            return 0.0;
        auto v = all;
        std::sort (v.begin(), v.end());
        return v[(size_t) (0.9 * (double) (v.size() - 1))];
    }
    bool ok() const
    {
        const double l = guard > 0.0 ? guard : limit;
        return p90() <= l && worst <= 2.5 * l;
    }
};

int run (const std::string& featurePath, const std::vector<Row>& plan)
{
    const auto ref = readFeatures (featurePath, "pro-r");
    if (ref.empty())
    {
        std::printf ("no reference features in %s\n", featurePath.c_str());
        return 1;
    }
    Check t30mid ("decay time 500 Hz - 2 kHz (%)", 8.0), t30band ("decay time per octave 125 Hz - 8 kHz (%)", 18.0);
    Check level ("level (dB)", 1.5), c50 ("clarity C50 (dB)", 3.0);
    // early decay time of the 500 Hz ... 2 kHz octaves (the measure for perceived reverberance): within
    // 10 % (single extreme settings - Distance 0 / 100 %, Brightness -100 % - up to ~15 %). Broadband EDT
    // is dominated by the highs, where Pro-R decays faster at first at short decay times: a known
    // difference, guarded against getting worse.
    Check edtMid ("early decay time EDT 500 Hz - 2 kHz (%)", 10.0);
    Check ts ("centre time Ts (%)", 25.0), edt ("early decay time EDT broadband (%)", 10.0, 16.0);
    Check tone ("tonal balance 63 Hz - 8 kHz (dB)", 6.0), cross ("stereo cross-feed (dB)", 3.0);
    Check width ("1 kHz tone width (Hz)", 3.0), dyn ("Thickness loud/quiet gains (dB)", 4.0);
    Check styleMid ("styles: decay time 500 Hz - 2 kHz (%)", 8.0), styleLevel ("styles: level (dB)", 1.5), styleC50 ("styles: clarity C50 (dB)", 3.0);
    // fine structure of the start: echo density 20-50 / 50-100 / 100-200 ms (1 = dense like noise) and
    // the brightness of the first 30 ms (spectral centroid)
    Check density ("echo density 20 - 200 ms (NED)", 0.2), erBright ("brightness 0 - 30 ms (%)", 15.0);
    const char* bands[] = { "t30_125", "t30_250", "t30_500", "t30_1000", "t30_2000", "t30_4000", "t30_8000" };
    const char* tones[] = { "tone_63", "tone_125", "tone_250", "tone_500", "tone_2000", "tone_4000", "tone_8000" };
    for (const auto& row : plan)
    {
        if (row.group == "Kalibrierung" || ref.count (row.file) == 0)
            continue;
        const auto& r = ref.at (row.file);
        nh::EngineParams ep;
        std::string why;
        if (! nachhallParams (row.code, ep, why))
            continue;
        const FileResult f = fit::measureNachhall (ep, row);
        const auto& m = f.left;
        if (row.signal == 'A')
        {
            const double mid = std::cbrt (m.t30[2] * m.t30[3] * m.t30[4]);
            const double rmid = std::cbrt (num (r, "t30_500") * num (r, "t30_1000") * num (r, "t30_2000"));
            if (row.group != "Styles")
            {
                t30mid.add (100.0 * std::abs (mid / rmid - 1.0), row.file);
                for (int b = 0; b < 7; ++b)
                    t30band.add (100.0 * std::abs (m.t30[b] / std::max (1.0e-3, num (r, bands[b])) - 1.0), row.file);
                level.add (std::abs (m.levelDb - num (r, "level")), row.file);
                if (row.group != "Extras")
                {
                    c50.add (std::abs (m.c50 - num (r, "c50")), row.file);
                    ts.add (100.0 * std::abs (m.ts / std::max (1.0, num (r, "ts")) - 1.0), row.file);
                    edt.add (100.0 * std::abs (m.edt / std::max (0.01, num (r, "edt")) - 1.0), row.file);
                    if (r.count ("edt_mid") != 0)
                        edtMid.add (100.0 * std::abs (m.edtMid / std::max (0.01, num (r, "edt_mid")) - 1.0), row.file);
                }
                for (int b = 0; b < 7; ++b)
                    tone.add (std::abs (fit::toneAt (m, std::atof (tones[b] + 5)) - num (r, tones[b])), row.file);
                if (row.group != "Extras" && r.count ("dens100_200") != 0)
                {
                    density.add (std::abs (m.density[1] - num (r, "dens20_50")), row.file);
                    density.add (std::abs (m.density[2] - num (r, "dens50_100")), row.file);
                    density.add (std::abs (m.density[3] - num (r, "dens100_200")), row.file);
                    erBright.add (100.0 * std::abs (m.erCentroid / std::max (100.0, num (r, "er_centroid")) - 1.0), row.file);
                }
            }
            else
            {
                styleMid.add (100.0 * std::abs (mid / rmid - 1.0), row.file);
                styleLevel.add (std::abs (m.levelDb - num (r, "level")), row.file);
                styleC50.add (std::abs (m.c50 - num (r, "c50")), row.file);
            }
            if (row.group == "Stereo Width")
                cross.add (std::abs (std::max (-60.0, m.crossDb) - std::max (-60.0, num (r, "cross"))), row.file);
        }
        else
        {
            width.add (std::abs (f.toneWidthHz - num (r, "tone_width")), row.file);
            if (row.group == "Thickness")
            {
                const auto& base = ref.at ("B_Basis_B");
                static const FileResult nb = fit::measureCode (plan, "B_Basis_B");
                dyn.add (std::abs ((f.loudGainDb - nb.loudGainDb) - (num (r, "loud_gain") - num (base, "loud_gain"))), row.file);
                dyn.add (std::abs ((f.quietGainDb - nb.quietGainDb) - (num (r, "quiet_gain") - num (base, "quiet_gain"))), row.file);
            }
        }
    }
    int failures = 0;
    std::printf ("%-42s %8s %8s %8s %8s  %s\n", "measure (Nachhall vs. reference)", "mean", "90 %", "worst", "limit", "worst file");
    for (const auto* c : { &t30mid, &t30band, &level, &c50, &edtMid, &ts, &edt, &tone, &cross, &width, &dyn, &styleMid, &styleLevel, &styleC50, &density, &erBright })
    {
        if (c->count == 0)
            continue;
        const bool ok = c->ok();
        failures += ok ? 0 : 1;
        char note[64] = "";
        if (c->guard > 0.0)
            std::snprintf (note, sizeof (note), " (known difference, guard %.0f)", c->guard);
        std::printf ("%-42s %8.2f %8.2f %8.2f %8.2f  %s%s %s\n", c->what, c->sum / c->count, c->p90(), c->worst, c->limit, c->worstFile.c_str(), note,
                     ok ? "" : "<- FAIL");
    }
    std::printf ("%s\n", failures == 0 ? "MATCH OK" : "MATCH FAILED");
    return failures == 0 ? 0 : 1;
}
} // namespace match

int usage()
{
    std::printf ("usage:\n  NachhallReference signals <dir>\n  NachhallReference plan <file.csv> <file.md>\n  NachhallReference checkplan <file.csv>\n"
                 "  NachhallReference nachhall <dir> [--extra \"key=value,...\"]\n  NachhallReference analyze <dir> [<dir2>] [--md <report.md>] [--csv <features.csv>]\n");
    return 2;
}

} // namespace

int main (int argc, char** argv)
{
    enableFlushToZero();
    std::setvbuf (stdout, nullptr, _IONBF, 0);
    if (argc < 3)
        return usage();
    const std::string cmd = argv[1];
    const auto p = plan();

    if (cmd == "signals")
    {
        bool ok = true;
        for (char s : { 'A', 'B' })
        {
            const auto sig = makeSignal (s);
            const std::string path = std::string (argv[2]) + "/" + signalFile (s);
            ok = wav::write (path, sig.l, sig.r, kFs, true) && ok;
            std::printf ("%s %s (%.1f s)\n", ok ? "wrote" : "FAILED", path.c_str(), (double) sig.l.size() / kFs);
        }
        return ok ? 0 : 1;
    }

    if (cmd == "plan" && argc >= 4)
    {
        std::ofstream csv (argv[2], std::ios::binary);
        csv << "\xEF\xBB\xBF" // UTF-8 BOM: Excel shows the umlauts correctly
            << "Nr;Datei;Testsignal;Pflicht;Einstellung (Rest wie Basis);Anzeige Hallzeit (s);Raumname;erledigt;Code\r\n";
        std::ofstream md (argv[3], std::ios::binary);
        md << "| Nr | Datei | Testsignal | Pflicht | Einstellung (Rest wie Basis) |\n|---|---|---|---|---|\n";
        int nr = 1;
        for (const auto& r : p)
        {
            csv << nr << ";" << r.file << ".wav;" << signalFile (r.signal) << ";" << r.need << ";" << r.setting << ";;;;" << r.code << "\r\n";
            md << "| " << nr << " | `" << r.file << ".wav` | " << r.signal << " | " << r.need << " | " << r.setting << " |\n";
            ++nr;
        }
        int required = 0, ifPresent = 0, optional = 0;
        for (const auto& r : p)
            (r.need == "ja" ? required : (r.need == "optional" ? optional : ifPresent))++;
        std::printf ("plan: %d entries (%d required, %d if present, %d optional)\n", (int) p.size(), required, ifPresent, optional);
        return csv && md ? 0 : 1;
    }

    if (cmd == "fit" && argc >= 4)
    {
        // NachhallReference fit rooms <pro-r recordings> [iterations]
        const std::string what = argv[2];
        const DirResult pro = analyzeDir (argv[3], "pro-r", p);
        const int iterations = argc > 4 ? std::atoi (argv[4]) : 4;
        fit::fastRender() = true;
        if (what == "rooms")
            return fit::rooms (pro, p, iterations);
        if (what == "early")
        {
            // optional room range "first-last" (0 ... 10) to run several fits in parallel
            int first = 0, last = nh::kNumRoomModels - 1;
            if (argc > 5)
                std::sscanf (argv[5], "%d-%d", &first, &last);
            return fit::early (pro, p, iterations, first, last);
        }
        if (what == "rate")
            return fit::rate (pro, p);
        if (what == "brightness")
            return fit::brightness (pro, p);
        if (what == "decayeq")
            return fit::decayEq (pro, p);
        if (what == "distance")
            return fit::distance (pro, p);
        if (what == "thickdens")
            return fit::thicknessDensity (pro, p);
        if (what == "thickc50")
            return fit::thicknessC50 (pro, p);
        if (what == "roomsc50")
            return fit::roomsC50 (pro, p);
        if (what == "primaryc50")
            return fit::primaryC50 (pro, p);
        if (what == "earlytap")
        {
            int first = 0, last = nh::kNumRoomModels - 1;
            if (argc > 4)
                std::sscanf (argv[4], "%d-%d", &first, &last);
            return fit::earlyTaps (pro, p, first, last);
        }
        if (what == "ercut")
        {
            int first = 0, last = nh::kNumRoomModels - 1;
            if (argc > 4)
                std::sscanf (argv[4], "%d-%d", &first, &last);
            return fit::erCut (pro, p, first, last);
        }
        if (what == "charer")
            return fit::characterEarly (pro, p);
        if (what == "styletone" && argc > 4) // NachhallReference fit styletone <recordings> vintage|plate
            return fit::styleTone (pro, p, argv[4]);
        if (what == "density")
        {
            int first = 0, last = nh::kNumRoomModels - 1;
            if (argc > 4)
                std::sscanf (argv[4], "%d-%d", &first, &last);
            return fit::density (pro, p, first, last);
        }
        if (what == "tone")
        {
            int first = 0, last = nh::kNumRoomModels - 1;
            if (argc > 4)
                std::sscanf (argv[4], "%d-%d", &first, &last);
            return fit::tone (pro, p, first, last);
        }
        if (what == "styles" && argc > 4) // NachhallReference fit styles <recordings> vintage|plate
            return fit::styles (pro, p, argv[4]);
        if (what == "width")
            return fit::width (pro, p);
        if (what == "character")
            return fit::character (pro, p);
        if (what == "thickness")
            return fit::thickness (pro, p);
        return usage();
    }

    if (cmd == "match")
        return match::run (argv[2], p);

    if (cmd == "checkplan")
    {
        // compares the plan columns (Nr, Datei, Testsignal, Pflicht, Einstellung, Code) and ignores the
        // columns the user fills in (Anzeige Hallzeit, Raumname, erledigt)
        std::ifstream in (argv[2], std::ios::binary);
        if (! in)
        {
            std::printf ("cannot read %s\n", argv[2]);
            return 1;
        }
        std::vector<std::vector<std::string>> rows;
        std::string line;
        bool header = true;
        while (std::getline (in, line))
        {
            if (header)
            {
                header = false;
                continue;
            }
            if (! line.empty() && line.back() == '\r')
                line.pop_back();
            if (! line.empty())
                rows.push_back (readCsvLine (line, ';'));
        }
        int errors = 0;
        if (rows.size() != p.size())
        {
            std::printf ("plan has %d rows, the tool %d\n", (int) rows.size(), (int) p.size());
            ++errors;
        }
        for (size_t i = 0; i < std::min (rows.size(), p.size()); ++i)
        {
            const auto& c = rows[i];
            const auto& r = p[i];
            const bool ok = c.size() >= 9 && c[0] == std::to_string (i + 1) && c[1] == r.file + ".wav" && c[2] == signalFile (r.signal) && c[3] == r.need
                            && c[4] == r.setting && c[8] == r.code;
            if (! ok)
            {
                std::printf ("row %d differs from the tool (%s)\n", (int) i + 1, r.file.c_str());
                ++errors;
            }
        }
        std::printf ("%s\n", errors == 0 ? "plan matches the tool" : "PLAN DOES NOT MATCH");
        return errors == 0 ? 0 : 1;
    }

    if (cmd == "nachhall")
    {
        // optional: --extra "brightness=50,thickness=50" is added to every plan entry
        std::string extra;
        for (int i = 3; i + 1 < argc; ++i)
            if (std::string (argv[i]) == "--extra")
                extra = argv[i + 1];
        const Stereo sig[2] = { makeSignal ('A'), makeSignal ('B') };
        int written = 0;
        for (const auto& r : p)
        {
            nh::EngineParams ep;
            std::string why;
            const std::string code = extra.empty() ? r.code : (r.code.empty() ? extra : r.code + "," + extra);
            if (! nachhallParams (code, ep, why))
            {
                std::printf ("  skip %s (%s)\n", r.file.c_str(), why.c_str());
                continue;
            }
            const auto out = renderNachhall (ep, sig[r.signal == 'A' ? 0 : 1]);
            const std::string path = std::string (argv[2]) + "/" + r.file + ".wav";
            if (! wav::write (path, out.l, out.r, kFs, false))
            {
                std::printf ("FAILED to write %s\n", path.c_str());
                return 1;
            }
            ++written;
        }
        std::printf ("rendered %d files into %s\n", written, argv[2]);
        return 0;
    }

    if (cmd == "analyze")
    {
        std::vector<std::string> dirs;
        std::string mdPath, csvPath;
        for (int i = 2; i < argc; ++i)
        {
            const std::string a = argv[i];
            if (a == "--md" && i + 1 < argc)
                mdPath = argv[++i];
            else if (a == "--csv" && i + 1 < argc)
                csvPath = argv[++i];
            else
                dirs.push_back (a);
        }
        if (dirs.empty())
            return usage();
        std::vector<DirResult> results;
        for (const auto& d : dirs)
        {
            results.push_back (analyzeDir (d, baseName (d), p));
            const auto& r = results.back();
            int present = 0;
            for (const auto& kv : r.files)
                present += kv.second.present ? 1 : 0;
            std::printf ("%s: %d recordings, calibration %s (offset %ld, gain %.2f dB, rest %.0f dB) %s\n", r.label.c_str(), present, r.cal.found ? "found" : "MISSING",
                         r.cal.offset, r.cal.gainDb, r.cal.residualDb, r.cal.note.c_str());
            if (r.cal.foundB)
                std::printf ("  signal B through the chain: loud/quiet step %.2f dB (should be 40.00), tone gain %.2f dB %s\n", r.cal.stepDb, r.cal.toneGainDb, r.cal.noteB.c_str());
            for (const auto& row : p)
            {
                const auto& f = r.files.at (row.file);
                if (f.present && ! f.problem.empty())
                    std::printf ("  %s: %s\n", row.file.c_str(), f.problem.c_str());
            }
        }
        if (! csvPath.empty())
        {
            std::ofstream csv (csvPath, std::ios::binary);
            writeCsv (csv, p, results);
            std::printf ("features: %s\n", csvPath.c_str());
        }
        if (! mdPath.empty())
        {
            std::ofstream md (mdPath, std::ios::binary);
            writeReport (md, p, results);
            std::printf ("report: %s\n", mdPath.c_str());
        }
        else
            writeReport (std::cout, p, results);
        return 0;
    }
    return usage();
}
