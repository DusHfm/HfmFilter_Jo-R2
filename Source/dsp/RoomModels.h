// Nachhall - room models along the stepless "Space" control
//
// 11 models at the decay times FabFilter Pro-R 2 shows for its 6 room icons (0.20 / 0.75 / 1.85 /
// 3.20 / 5.20 / 10.0 s) and for the points between them (0.40 / 1.2 / 2.4 / 4.0 / 7.0 s). Their
// parameters were fitted to recordings of Pro-R 2 made with the reference package (reference/):
// decay time per octave, level, clarity, early reflections, tone of the late reverb. Only the audio
// output was measured (tests/ReferenceTool.cpp, "fit").
#pragma once

#include "DspCommon.h"

namespace nh
{
struct RoomModel
{
    const char* name;
    float t60;                // displayed decay time (s) at Decay Rate 100 %
    float t60Scale;           // measured mid-band decay / displayed time
    float dimX, dimY, dimZ;   // room size in metres: line lengths of the late reverb (fitted to its echo density)
    float bassRatio;          // T60(low) / T60(mid)
    float hfRatio;            // T60(high) / T60(mid) at Brightness 0 % (neutral)
    float hfFreq;             // HF absorption corner at Brightness 0 %
    float airRatio;           // T60 above ~11 kHz / T60(mid)
    float erGain;             // early reflection level
    float levelDb;            // wet level correction
    float diffusion;          // baseline diffusion
    float erCutoff = 9500.0f; // early reflection low-pass at Distance 50 %, Brightness 0 %
    float buildScale = 1.0f;  // spread of the late reverb build-up
    float airAbs = 1.0f;      // x time-based HF loss (Tuning::airAbsRate)
    float lateLsDb = 0.0f;    // tone of the late reverb: low shelf 120 Hz (Modern)
    float lateHsDb = 0.0f;    //                          high shelf 2.5 kHz
    float earlyTap = 0.0f;    // early output taps of the late reverb (Modern)
    float lateMidDb = 0.0f;   // tone of the late reverb: bell 300 Hz (Modern)
    float lateAirDb = 0.0f;   // tone of the late reverb: high shelf 10 kHz (Modern)
    float erPrimaryDb = 0.0f; // level of the first strong reflections on top of the train (Modern, clarity)
};

constexpr int kNumRoomModels = 11;
constexpr int kNumRoomIcons = 6; // Pro-R shows 6 icons: models 0, 2, 4, 6, 8, 10
constexpr int kRoomIconModel[kNumRoomIcons] = { 0, 2, 4, 6, 8, 10 };

// Mutable only for the calibration tools (tests/ReferenceTool.cpp); the plug-in never changes it.
inline RoomModel* roomModelsMutable() noexcept
{
    static RoomModel models[kNumRoomModels] = {
        // name           t60     scale   X       Y      Z      bass   hf     hfFreq   air    er     level  diff   erCut    build  airAbs lateLs lateHs early  lateMid lateAir primary
        { "Ambience",      0.20f, 1.092f, 7.5f, 6.2f, 6.0f, 1.39f, 0.31f, 16000.0f, 1.30f, 5.359f, -5.62f, 0.860f, 16595.0f, 0.543f, 0.741f, 9.07f, -2.94f, 0.708f, 3.53f, 5.24f, 0.00f },
        { "Small Room",    0.40f, 1.202f, 12.0f, 9.9f, 6.3f, 0.94f, 0.63f, 7393.7f, 1.30f, 3.878f, -5.00f, 0.823f, 16701.9f, 0.474f, 0.937f, 7.16f, -3.29f, 0.200f, 4.76f, 2.62f, 0.00f },
        { "Room",          0.75f, 1.125f, 33.0f, 23.1f, 15.9f, 1.10f, 1.01f, 1758.2f, 0.65f, 4.162f, -7.05f, 0.797f, 16492.5f, 1.523f, 1.034f, 5.12f, -3.61f, 0.928f, 3.84f, 1.59f, 0.00f },
        { "Chamber",       1.20f, 1.110f, 44.3f, 31.1f, 19.7f, 0.98f, 0.65f, 6585.8f, 1.30f, 2.431f, -6.58f, 0.778f, 16480.2f, 0.336f, 1.147f, 4.62f, -3.21f, 1.997f, 1.88f, 1.60f, -1.57f },
        { "Hall",          1.85f, 1.124f, 53.2f, 36.5f, 22.5f, 1.04f, 0.69f, 6720.5f, 1.30f, 2.611f, -9.26f, 0.761f, 16440.9f, 0.069f, 1.042f, 3.51f, -3.56f, 3.317f, 0.10f, 1.79f, -1.13f },
        { "Large Hall",    2.40f, 1.120f, 57.6f, 38.4f, 24.0f, 0.96f, 0.71f, 6525.0f, 1.30f, 1.624f, -6.40f, 0.750f, 16416.0f, 0.127f, 0.823f, 2.30f, -3.78f, 2.512f, -0.15f, 0.86f, -1.02f },
        { "Church",        3.20f, 1.099f, 88.3f, 46.1f, 33.1f, 1.07f, 0.52f, 9423.4f, 1.30f, 4.369f, -13.04f, 0.737f, 16412.9f, 0.050f, 0.660f, 2.05f, -3.33f, 6.000f, 0.39f, -0.77f, -0.65f },
        { "Arena",         4.00f, 1.124f, 83.4f, 55.6f, 26.6f, 0.91f, 0.63f, 9386.2f, 1.30f, 1.075f, -0.84f, 0.728f, 16416.5f, 0.056f, 0.761f, 1.70f, -3.40f, 1.290f, 0.15f, -0.80f, -0.60f },
        { "Cathedral",     5.20f, 1.108f, 88.4f, 37.4f, 30.6f, 1.00f, 0.54f, 9569.4f, 1.30f, 0.826f, 1.59f, 0.719f, 16419.3f, 0.090f, 0.651f, 1.19f, -3.33f, 0.829f, -0.04f, -0.46f, -0.46f },
        { "Huge Space",    7.00f, 1.111f, 70.0f, 45.0f, 22.5f, 0.97f, 0.82f, 4564.6f, 1.30f, 0.627f, 4.24f, 0.710f, 16421.5f, 0.497f, 0.613f, 1.21f, -3.23f, 0.013f, -0.01f, -0.25f, -0.25f },
        { "Abyss",        10.00f, 1.123f, 200.0f, 150.0f, 60.0f, 0.97f, 0.86f, 4005.6f, 1.30f, 0.565f, 4.67f, 0.700f, 16423.1f, 0.230f, 0.560f, 1.48f, -3.56f, 0.657f, -0.24f, 0.35f, 0.04f },
    };
    return models;
}

inline const RoomModel* roomModels() noexcept { return roomModelsMutable(); }

struct RoomState
{
    float t60 = 1.5f, t60Scale = 1.0f;
    float dimX = 28.0f, dimY = 20.0f, dimZ = 12.0f;
    float bassRatio = 1.1f, hfRatio = 0.5f, hfFreq = 3600.0f, airRatio = 0.7f;
    float erGain = 0.7f, levelDb = 0.0f, diffusion = 0.77f;
    float erCutoff = 9500.0f, buildScale = 1.0f, airAbs = 1.0f, lateLsDb = 0.0f, lateHsDb = 0.0f, earlyTap = 0.0f, lateMidDb = 0.0f, lateAirDb = 0.0f;
    float erPrimaryDb = 0.0f;
    float meanFreePathSec = 0.034f;
    float vintageMeanFreePathSec = 0.034f; // Vintage: from its own room sizes

    float actualT60() const noexcept { return t60 * t60Scale; } // what is measured; t60 is what is shown
};

// Vintage keeps the room sizes it was fitted with (Modern: sizes fitted to the echo density of Pro-R 2)
constexpr float kVintageDims[kNumRoomModels][3] = { { 3.0f, 2.5f, 2.4f },     { 5.7f, 4.7f, 3.0f },    { 11.0f, 7.7f, 5.3f },  { 21.1f, 14.8f, 9.4f },
                                                   { 36.7f, 25.2f, 15.5f },  { 48.0f, 32.0f, 20.0f }, { 60.9f, 31.8f, 22.8f }, { 83.4f, 55.6f, 26.6f },
                                                   { 104.0f, 44.0f, 36.0f }, { 140.0f, 90.0f, 45.0f }, { 200.0f, 150.0f, 60.0f } };

inline float meanFreePath (float x, float y, float z) noexcept
{
    return 4.0f * x * y * z / (2.0f * (x * y + x * z + y * z)) / 343.0f;
}

inline RoomState interpolateRoom (float space) noexcept
{
    space = clampv (space, 0.0f, 1.0f);
    const float pos = space * (float) (kNumRoomModels - 1);
    int i = (int) pos;
    if (i >= kNumRoomModels - 1)
        i = kNumRoomModels - 2;
    const float t = pos - (float) i;
    const RoomModel& a = roomModels()[i];
    const RoomModel& b = roomModels()[i + 1];

    RoomState r;
    r.t60 = expLerp (a.t60, b.t60, t);
    r.t60Scale = lerpf (a.t60Scale, b.t60Scale, t);
    r.dimX = expLerp (a.dimX, b.dimX, t);
    r.dimY = expLerp (a.dimY, b.dimY, t);
    r.dimZ = expLerp (a.dimZ, b.dimZ, t);
    r.bassRatio = lerpf (a.bassRatio, b.bassRatio, t);
    r.hfRatio = lerpf (a.hfRatio, b.hfRatio, t);
    r.hfFreq = expLerp (a.hfFreq, b.hfFreq, t);
    r.airRatio = lerpf (a.airRatio, b.airRatio, t);
    r.erGain = lerpf (a.erGain, b.erGain, t);
    r.levelDb = lerpf (a.levelDb, b.levelDb, t);
    r.diffusion = lerpf (a.diffusion, b.diffusion, t);
    r.erCutoff = expLerp (a.erCutoff, b.erCutoff, t);
    r.buildScale = expLerp (a.buildScale, b.buildScale, t);
    r.airAbs = expLerp (a.airAbs, b.airAbs, t);
    r.lateLsDb = lerpf (a.lateLsDb, b.lateLsDb, t);
    r.lateHsDb = lerpf (a.lateHsDb, b.lateHsDb, t);
    r.earlyTap = lerpf (a.earlyTap, b.earlyTap, t);
    r.lateMidDb = lerpf (a.lateMidDb, b.lateMidDb, t);
    r.lateAirDb = lerpf (a.lateAirDb, b.lateAirDb, t);
    r.erPrimaryDb = lerpf (a.erPrimaryDb, b.erPrimaryDb, t);
    r.meanFreePathSec = meanFreePath (r.dimX, r.dimY, r.dimZ);
    r.vintageMeanFreePathSec = meanFreePath (expLerp (kVintageDims[i][0], kVintageDims[i + 1][0], t), expLerp (kVintageDims[i][1], kVintageDims[i + 1][1], t),
                                             expLerp (kVintageDims[i][2], kVintageDims[i + 1][2], t));
    return r;
}

inline int nearestRoomIndex (float space) noexcept
{
    return (int) std::lround (clampv (space, 0.0f, 1.0f) * (float) (kNumRoomModels - 1));
}

inline float spaceForRoomIndex (int index) noexcept
{
    return (float) clampv (index, 0, kNumRoomModels - 1) / (float) (kNumRoomModels - 1);
}

// Inverse of the Space -> displayed decay time mapping (monotonic).
inline float spaceForT60 (float t60) noexcept
{
    float lo = 0.0f, hi = 1.0f;
    for (int i = 0; i < 40; ++i)
    {
        const float mid = 0.5f * (lo + hi);
        if (interpolateRoom (mid).t60 < t60)
            lo = mid;
        else
            hi = mid;
    }
    return 0.5f * (lo + hi);
}

// Inverse of the Space -> measured decay time mapping (for the IR import).
inline float spaceForActualT60 (float t60) noexcept
{
    float lo = 0.0f, hi = 1.0f;
    for (int i = 0; i < 40; ++i)
    {
        const float mid = 0.5f * (lo + hi);
        if (interpolateRoom (mid).actualT60() < t60)
            lo = mid;
        else
            hi = mid;
    }
    return 0.5f * (lo + hi);
}

// Displayed decay time of a Space value saved by Nachhall 1.0 (15 room models): used to convert
// old presets and projects so that they keep their decay time.
inline float legacySpaceToT60 (float oldSpace) noexcept
{
    static const float t[15] = { 0.20f, 0.30f, 0.42f, 0.55f, 0.70f, 0.90f, 1.15f, 1.50f, 1.90f, 2.40f, 3.00f, 3.80f, 5.00f, 7.00f, 10.0f };
    const float pos = clampv (oldSpace, 0.0f, 1.0f) * 14.0f;
    const int i = std::min (13, (int) pos);
    return expLerp (t[i], t[i + 1], pos - (float) i);
}

} // namespace nh
