// Nachhall - control curves fitted to recordings of FabFilter Pro-R 2 (reference/, only the audio
// output was measured). The calibration tool (tests/ReferenceTool.cpp, "fit") may change them in
// its own process; the plug-in only reads them.
#pragma once

#include <cmath>

namespace nh
{
struct Tuning
{
    // ---- Stereo Width: tank cross-feed (1 = fully mixed) and output side gain at 0 / 50 / 70 / 100 / 120 %
    static constexpr int kWidthPoints = 5;
    float widthAt[kWidthPoints] = { 0.0f, 0.5f, 0.7f, 1.0f, 1.2f };
    float widthCross[kWidthPoints] = { 1.000f, 0.981f, 0.529f, 0.000f, 0.344f };
    float widthSide[kWidthPoints] = { 0.000f, 0.600f, 0.999f, 1.000f, 1.434f };
    float widthLevelDb[kWidthPoints] = { 1.85f, -1.36f, -1.70f, 0.00f, -2.34f }; // wet level (left in -> left out)

    // ---- Decay Rate: time-based HF loss (air absorption) and a correction of the decay time at
    // 25 / 50 / 100 / 200 / 400 %
    float airAbsRate = 0.2631f, airAbsFreq = 9225.1f, airAbsQ = 0.340f;
    float rateCorr[5] = { 0.901f, 0.998f, 1.000f, 1.008f, 1.017f };
    float rateLevelDb[5] = { -1.20f, -0.43f, 0.00f, 0.86f, 1.82f }; // wet level vs Decay Rate (Pro-R 2)
    float rateHsDb[5] = { 10.47f, 2.66f, 0.00f, -0.91f, -0.75f };   // air shelf (10 kHz) of the late reverb vs Decay Rate (Pro-R 2: short = brighter)

    // ---- Distance at 0 / 25 / 50 / 75 / 100 %: early reflection level and low-pass (x the value at
    // 50 %) and the spread of the late build-up
    float distEr[5] = { 2.377f, 1.517f, 1.000f, 0.755f, 0.224f };
    float distErCut[5] = { 4.787f, 5.000f, 1.000f, 0.761f, 0.680f };
    float distBuild[5] = { 0.264f, 0.462f, 0.406f, 1.375f, 2.531f };
    float distLateDb[5] = { -3.294f, -1.910f, 0.000f, -0.796f, -0.776f }; // level of the late reverb
    float distDiffIn[5] = { 0.000f, 0.000f, 0.000f, 0.014f, 0.544f }; // input diffusion of the late reverb (denser far away)
    float distLevelDb[5] = { 0.25f, 0.02f, 0.00f, 0.36f, -0.47f }; // wet level per Distance point

    // ---- Styles Vintage [0] and Plate [1] at the displayed decay 0.75 / 1.85 / 10 s (in between:
    // interpolated over the logarithm of the decay time)
    float styleT60[2][3] = { { 0.443f, 0.802f, 0.876f }, { 1.048f, 0.944f, 0.892f } };       // x decay time
    float styleHf[2][3] = { { 2.000f, 2.000f, 0.203f }, { 2.000f, 2.000f, 0.872f } };        // x high frequency decay ratio
    float styleLevelDb[2][3] = { { -0.695f, -1.811f, 4.669f }, { -7.443f, -2.948f, 3.726f } };   // wet level
    float styleEarlyDb[2][3] = { { -9.342f, -5.622f, 1.528f }, { 14.883f, 10.583f, 8.010f } };   // reflections / plate early output
    // tone of the late reverb per style point: low shelf 120 Hz, high shelf 2.5 kHz, high shelf 10 kHz (dB)
    float styleLsDb[2][3] = { { -1.787f, 0.926f, 1.237f }, { -1.612f, -0.620f, 4.445f } };
    float styleHsDb[2][3] = { { 4.217f, 1.480f, 2.123f }, { 3.300f, 2.864f, 1.358f } };
    float styleAirDb[2][3] = { { -2.808f, -4.852f, 3.442f }, { -10.976f, -9.410f, -12.168f } };

    static float styleAt (const float* table, float displayedT60) noexcept
    {
        const float x = std::log (displayedT60 > 0.01f ? displayedT60 : 0.01f);
        const float x0 = -0.28768f, x1 = 0.61519f, x2 = 2.30259f; // ln 0.75, ln 1.85, ln 10
        if (x <= x0)
            return table[0];
        if (x <= x1)
            return table[0] + (table[1] - table[0]) * (x - x0) / (x1 - x0);
        if (x <= x2)
            return table[1] + (table[2] - table[1]) * (x - x1) / (x2 - x1);
        return table[2];
    }

    // ---- Decay Rate EQ shelves: frequency and Q of Pro-R 2 -> filter of the decay design
    float decayLowShelfFreq = 1.019f, decayHighShelfFreq = 1.441f, decayShelfQ = 0.626f;

    // ---- Character -> delay modulation (50 % and 100 %; below 50 % the depth fades to 0)
    float modDepth50 = 0.00014f, modDepth100 = 0.00196f; // seconds
    float modRate50 = 1.216f, modRate100 = 0.641f;       // Hz

    // ---- Thickness: compressor (+) / expander (-) on the reverb input
    float thickPivotDb = -12.62f; // weighted detector level where only the static gain acts
    float thickSlopePos = -0.503f; // dB per dB at +100 % (2:1 compression)
    float thickSlopeNeg = 0.417f;  // dB per dB at -100 % (expansion)
    float thickStaticPos = -7.26f;  // dB at +100 % (grows with the square of the setting)
    float thickStaticNeg = -2.75f;  // dB at -100 %
    float thickErDb[5] = { -2.853f, -3.612f, 0.000f, -1.984f, -8.540f }; // early reflection level at -100 ... +100 % (clarity C50)
    float thickTap[5] = { 1.505f, 0.706f, 1.000f, 1.110f, 1.181f };            // x early taps of the late reverb (airier below 0 %)
    float thickThin[5] = { 0.950f, 0.160f, 0.000f, 0.000f, 0.000f };   // share of the single reflections left out (sparser below 0 %)
    float thickExtra[5] = { 0.000f, 0.000f, 0.000f, 1.281f, 1.387f };  // extra reflections at 10 ... 32 ms (denser above 0 %)
    float thickLevelDb[5] = { 0.00f, 3.05f, 0.00f, -0.38f, 0.00f }; // wet level per Thickness point (measured on Pro-R 2)
    float earlyTapHpHz = 0.0f; // high-pass of the early taps of the late reverb (0 = off)
    // Character: Pro-R 2's early reflections are louder and brighter at exactly 50 % than at 0 / 25 / 75 / 100 %
    float charErDb = -0.93f;   // level away from 50 %
    float charErCut = 0.772f;    // x early reflection low-pass away from 50 %
    float charLevelDb = -0.14f;  // wet level away from Character 50 %
    // x input diffusion of the late reverb (Modern): Pro-R 2 starts sparse (echo density 0.18 at
    // 20 ... 50 ms) and gets denser with Thickness (0.39 at +100 %)
    float diffIn[5] = { 0.000f, 0.000f, 0.000f, 0.298f, 0.463f };
    float diffLoop[5] = { 0.000f, 0.257f, 1.000f, 0.191f, 0.334f }; // x allpass diffusion inside the loop (Modern)

    // ---- Brightness at -100 / -50 / 0 / +50 / +100 % (Pro-R scale; Nachhall 0 / 25 / 50 / 75 / 100 %)
    static constexpr int kBrightPoints = 5;
    float brightHf[kBrightPoints] = { 0.941f, 0.940f, 1.000f, 1.271f, 1.290f };   // x room HF decay ratio
    float brightFreq[kBrightPoints] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };    // x room HF corner
    float brightAir[kBrightPoints] = { 0.238f, 0.546f, 1.000f, 1.754f, 1.876f };    // x room air ratio
    float brightLsDb[kBrightPoints] = { 9.706f, 1.333f, 0.000f, -0.032f, -2.473f }; // output low shelf (brightLsFreq)
    float brightHsDb[kBrightPoints] = { -14.657f, -11.616f, 0.000f, 9.985f, 9.898f }; // output high shelf (brightHsFreq)
    float brightErCut[kBrightPoints] = { 0.803f, 0.794f, 1.000f, 2.087f, 4.000f };   // x early reflection low-pass
    float brightErDb[kBrightPoints] = { 8.343f, 2.124f, 0.000f, 3.162f, 4.464f };   // early reflection level (clarity C50)
    float brightErHsDb[kBrightPoints] = { 8.000f, 8.000f, 0.000f, -9.848f, -10.607f }; // early reflections: high shelf added to brightHsDb
    float brightGainDb[kBrightPoints] = { -9.348f, -0.888f, 0.000f, -3.038f, -3.165f };  // wet level
    float brightLsFreq = 500.0f, brightHsFreq = 3000.0f;                  // output shelf corners

    // interpolation helper for the 5-point tables (x = 0 ... 1)
    static float at (const float* table, int points, float x) noexcept
    {
        x = x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
        const float pos = x * (float) (points - 1);
        int i = (int) pos;
        if (i >= points - 1)
            i = points - 2;
        const float t = pos - (float) i;
        return table[i] + (table[i + 1] - table[i]) * t;
    }
};

inline Tuning& tuningMutable() noexcept
{
    static Tuning t;
    return t;
}

inline const Tuning& tuning() noexcept { return tuningMutable(); }

} // namespace nh
