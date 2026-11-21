// Nachhall - 6 band Post EQ for the reverb signal
#pragma once

#include "Svf.h"

namespace nh
{
enum PostShape : int
{
    kPostBell = 0,
    kPostLowShelf,
    kPostHighShelf,
    kPostLowCut,
    kPostHighCut,
    kNumPostShapes
};

enum Placement : int
{
    kPlaceStereo = 0,
    kPlaceLeft,
    kPlaceRight,
    kPlaceMid,
    kPlaceSide,
    kNumPlacements
};

constexpr int kNumPostBands = 6;
constexpr int kNumSlopes = 9;
constexpr int kSlopeDbPerOct[kNumSlopes] = { 6, 12, 18, 24, 30, 36, 48, 72, 96 };
constexpr int kMaxPostSections = 8;

struct PostBandParams
{
    bool used = false;
    bool enabled = true;
    int shape = kPostBell;
    float freq = 1000.0f;
    float gain = 0.0f;
    float q = 1.0f;
    int slope = 3;
    int placement = kPlaceStereo;

    bool active() const noexcept { return used && enabled; }
};

struct PostBandDesign
{
    int numSections = 0;
    SvfCoefs sections[kMaxPostSections];

    double magnitudeSqFromTan (double tanW) const noexcept
    {
        double m = 1.0;
        for (int i = 0; i < numSections; ++i)
            m *= sections[i].magnitudeSqFromTan (tanW);
        return m;
    }
};

PostBandDesign designPostBand (const PostBandParams& p, double sampleRate) noexcept;

// Loudness-weighted average power gain of the whole EQ in dB (for the automatic gain compensation)
float computePostEqLoudnessDb (const PostBandParams* bands, double sampleRate) noexcept;

class PostEq
{
public:
    void prepare (double sampleRate);
    void reset();
    void setParams (const PostBandParams* bands, bool autoGain) noexcept;
    void update (int blockSize) noexcept; // control rate smoothing + coefficient updates
    void process (float* left, float* right, int numSamples) noexcept;
    float getCompensationDb() const noexcept { return compDb; }

private:
    struct Band
    {
        PostBandParams target, current;
        PostBandDesign design;
        Svf state[2][kMaxPostSections];
        float mix = 0.0f, mixTarget = 0.0f;
        float mixStart = 0.0f, mixInc = 0.0f;
        bool structurePending = false;
        bool initialised = false;
    };

    static bool sameStructure (const PostBandParams& a, const PostBandParams& b) noexcept
    {
        return a.shape == b.shape && a.slope == b.slope && a.placement == b.placement;
    }

    double fs = 48000.0;
    Band bands[kNumPostBands];
    bool autoGainOn = true;
    float compDb = 0.0f, compTargetDb = 0.0f;
    float compGain = 1.0f, compGainInc = 0.0f;
    bool needsCompUpdate = true;
    int compCounter = 0;
};

} // namespace nh
