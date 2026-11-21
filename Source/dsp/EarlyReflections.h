// Nachhall - early reflections from a shoebox image-source model.
// Two virtual sources (left / right input) and two ears give a true-stereo pattern that
// morphs continuously with room size and listener distance.
#pragma once

#include "DelayLine.h"
#include "Svf.h"

namespace nh
{
class EarlyReflections
{
public:
    static constexpr int kNumImages = 36; // 24 images up to 2nd order + 12 axial "late echoes"
    static constexpr int kStatPerPath = 124; // statistical pattern: 4 strong + 112 single + 8 extra reflections
    static constexpr int kMaxTaps = kStatPerPath * 4 > kNumImages * 4 ? kStatPerPath * 4 : kNumImages * 4;

    struct Geometry
    {
        float dimX = 28.0f, dimY = 20.0f, dimZ = 12.0f;
        float distance = 0.5f;  // 0 .. 1
        float character = 0.5f; // 0 .. 1 (late echoes)
        float beta = 0.82f;     // wall reflection coefficient
        bool sparse = false;    // vintage style: fewer reflections
        float crossfeed = 1.0f; // 0 = left input only reaches the left ear (independent channels)
        float onsetSec = 0.0f;  // > 0: the first reflection arrives at this time (pattern shifted)
        bool statistical = false; // fixed pattern (Modern): a few strong reflections, then a train of
                                  // single ones up to ~100 ms, the same times in every room (like Pro-R 2)
        float extra = 0.0f;       // 0 .. 1: additional reflections at 10 ... 32 ms (denser start)
        float thin = 0.0f;        // 0 .. 1: share of the single reflections left out (sparser start)
        float primaryDb = 0.0f;   // extra level of the first strong reflections (statistical pattern)
        float t60 = 2.0f;         // decay time of the room: the later reflections fade with it
    };

    // Pro-R 2 starts its early reflections 6.7 ms after the input in every room (measured)
    static constexpr float kModernOnsetSeconds = 0.00667f;
    static constexpr float kVintageOnsetSeconds = 0.0137f;

    void prepare (double sampleRate);
    void reset();
    void setGeometry (const Geometry& g);
    void setToneCutoff (float hz);
    void process (const float* inL, const float* inR, float* outL, float* outR, int numSamples) noexcept;
    static constexpr int kMaxBlock = 64; // block-wise path up to this block size
    void feed (const float* inL, const float* inR, int numSamples) noexcept; // input only (output silent)

private:
    struct Tap
    {
        int src = 0, ear = 0;
        float targetDelay = 2.0f, targetGain = 0.0f;
        float delay = 2.0f, gain = 0.0f;
        float dInc = 0.0f, gInc = 0.0f;
        bool primary = false; // one of the first strong reflections (statistical pattern)
    };

    double fs = 48000.0;
    float maxDelaySamples = 1000.0f;
    DelayLine buf[2];
    Tap taps[kMaxTaps];
    int numTaps = kMaxTaps; // taps in use (the rest are silent)
    Svf lp[2][2];
    SvfCoefs lpCoefs;
    float smoothA = 0.1f;
    bool firstGeometry = true;
};

} // namespace nh
