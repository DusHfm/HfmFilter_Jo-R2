// Nachhall - late reverb tanks
//   FdnTank   : 16-line feedback delay network (Modern + Vintage style)
//   PlateTank : figure-of-eight allpass/delay plate tank (Plate style)
#pragma once

#include "DecayDesign.h"
#include "DelayLine.h"

namespace nh
{
// Scalar attenuation filter for one loop segment
struct AttenuationCascade
{
    SvfCoefs coefs[kMaxDecayStages];
    Svf state[kMaxDecayStages];
    bool active[kMaxDecayStages] = {};
    float gain = 1.0f;

    void design (const DecaySpec& spec, double fs, double delaySeconds) noexcept
    {
        SvfCoefs c[kMaxDecayStages];
        float g = 1.0f;
        designSegmentFilters (spec, fs, delaySeconds, c, g);
        for (int s = 0; s < kMaxDecayStages; ++s)
        {
            const bool a = spec.stages[s].active;
            if (a && ! active[s])
                state[s].reset();
            active[s] = a;
            coefs[s] = c[s];
        }
        gain = g;
    }

    inline float process (float x) noexcept
    {
        for (int s = 0; s < kMaxDecayStages; ++s)
            if (active[s])
                x = state[s].process (x, coefs[s]);
        return x * gain;
    }

    void reset() noexcept
    {
        for (auto& s : state)
            s.reset();
    }
};

//==============================================================================
class FdnTank
{
public:
    static constexpr int N = 16;
    static constexpr int kNumInputDiffusers = 4;

    struct Settings
    {
        float meanDelay = 0.03f;  // seconds
        float apGain = 0.5f;      // in-loop allpass coefficient
        float inDiffGain = 0.6f;  // input diffusion coefficient
        float inDiffScale = 1.0f; // input diffuser length scale
        float modDepth = 0.0f;    // seconds
        float modRate = 0.5f;     // Hz
        float modRandom = 0.5f;   // 0 = sine, 1 = random
        float crossfeed = 1.0f;   // 0 = independent L/R tanks, 1 = fully mixed
        float saturation = 0.0f;  // passive saturation drive
        float freeze = 0.0f;      // 0 / 1
        float t60 = 1.5f;         // mid-band decay, limits the ringing of the diffusers
        float freezeGlide = 0.0f; // 0..1, slow ramp after Freeze: loop delays glide to whole samples
        float freezeMotion = 0.0f; // radians, lossless movement of the frozen tail (Character)
        float earlyTap = 0.0f;     // level of the early output taps inside the lines (fills the
                                   // build-up gap: the decay starts at once, like Pro-R 2)
        float spread = 0.0f;       // samples: build-up of the late reverb - every line gets the input
                                   // at its own time between 0 and this (no comb filter, unlike a sum
                                   // of delayed copies)
        float spreadMix = 0.0f;    // 0 = all lines equally loud, 1 = the later lines louder (rising build-up)
    };

    void prepare (double sampleRate, double maxMeanDelaySeconds);
    void reset();
    void setSettings (const Settings& s) noexcept { settings = s; }
    void update (int blockSize, const DecaySpec& spec, bool specChanged) noexcept;
    void process (const float* inL, const float* inR, float* outL, float* outR, int numSamples) noexcept;
    double getReferenceDelaySeconds() const noexcept;
    void forceDecayRedesign() noexcept { designedMean = -1.0f; }

private:
    void redesign (const DecaySpec& spec) noexcept;

    double fs = 48000.0;
    Settings settings;

    DelayLine lines[N];
    Allpass loopAp[N];
    Allpass inAp[2][kNumInputDiffusers];
    SvfBank<N> stages[kMaxDecayStages];
    bool stageActive[kMaxDecayStages] = {};
    alignas (32) float lineGain[N];

    float ratio[N] {}, apRatio[N] {}, rateMul[N] {};
    float inSign[N] {}, outSign[N] {};

    // per-sample delay ramps
    float curDelay[N] {}, delayInc[N] {};
    float curAp[N] {}, apInc[N] {};
    float curInAp[kNumInputDiffusers] {}, inApInc[kNumInputDiffusers] {};
    float gInK[kNumInputDiffusers] {};

    // modulation
    float lfoPhase[N] {};
    float rnd[N] {}, rndTarget[N] {}, rndSmooth[N] {};
    float modSignal[N] {};
    Rng rng { 0x1234567u };

    // Freeze motion: rotations between line pairs (orthogonal -> the frozen loop stays lossless)
    static constexpr int kNumPairs = N / 2;
    float rotC[kNumPairs] { 1, 1, 1, 1, 1, 1, 1, 1 }, rotS[kNumPairs] {};
    float rotCInc[kNumPairs] {}, rotSInc[kNumPairs] {};
    bool motionActive = false;

    // smoothed controls
    float sMean = 0.03f, sApGain = 0.5f, sInDiffGain = 0.6f, sInDiffScale = 1.0f;
    float sModDepth = 0.0f, sModRate = 0.5f, sModRandom = 0.5f, sCross = 1.0f, sSat = 0.0f, sFreeze = 0.0f;
    float sEarlyTap = 0.0f;
    // build-up: per-line input delay and weight (smoothed, whole samples at rest)
    DelayLine inBuf[2];
    float spreadD[N] {}, spreadDInc[N] {};
    float spreadW[N] {}, spreadWInc[N] {};
    float maxSpreadSamples = 100.0f;
    int earlyPos[N] {};
    float earlySign[N] {};
    SvfCoefs earlyHp;  // high-pass of the early taps
    Svf earlyHpState[2];
    float designedMean = -1.0f;
    float maxLineSamples = 1000.0f, maxApSamples = 100.0f;
    bool first = true;
};

//==============================================================================
class PlateTank
{
public:
    struct Settings
    {
        float size = 1.0f;
        float inDiff1 = 0.75f, inDiff2 = 0.625f;
        float decayDiff1 = 0.70f, decayDiff2 = 0.50f;
        float modDepth = 0.0005f; // seconds
        float modRate = 1.0f;
        float saturation = 0.0f;
        float freeze = 0.0f;
        float preDiffScale = 1.0f;
        float t60 = 1.5f;
        float earlyLevel = 0.4f; // diffused direct path -> fast plate build-up
        float freezeGlide = 0.0f;  // see FdnTank
        float freezeMotion = 0.0f; // see FdnTank
    };

    void prepare (double sampleRate, float maxSize, float lengthVariant, float modPhaseOffset);
    void reset();
    void setSettings (const Settings& s) noexcept { settings = s; }
    void update (int blockSize, const DecaySpec& spec, bool specChanged) noexcept;
    void process (const float* in, float* outL, float* outR, int numSamples) noexcept;
    double getReferenceDelaySeconds() const noexcept;
    void forceDecayRedesign() noexcept { designedSize = -1.0f; }

private:
    double fs = 48000.0;
    Settings settings;
    float variant = 1.0f;

    Allpass inAp[4];
    Allpass apA1, apA2, apB1, apB2;
    DelayLine dA1, dA2, dB1, dB2;
    AttenuationCascade att[4];

    // base lengths in samples at size 1
    float lIn[4] {}, lApA1 = 0, lApA2 = 0, lApB1 = 0, lApB2 = 0, lDA1 = 0, lDA2 = 0, lDB1 = 0, lDB2 = 0;
    float tapsL[7] {}, tapsR[7] {};

    // loop delays in processing order: apA1, dA1, apA2, dA2, apB1, dB1, apB2, dB2 (per-sample ramps)
    static constexpr int kLoopDelays = 8;
    float lLoop[kLoopDelays] {};
    float curD[kLoopDelays] {}, dInc[kLoopDelays] {};
    float modKeep = 1.0f, modKeepInc = 0.0f;
    float rotC = 1.0f, rotS = 0.0f, rotCInc = 0.0f, rotSInc = 0.0f; // freeze motion between the halves

    float curSize = 1.0f, sizeInc = 0.0f;
    float sMod = 0.0f, sModRate = 1.0f, sSat = 0.0f, sFreeze = 0.0f, sPreDiff = 1.0f;
    float sInDiff1 = 0.75f, sInDiff2 = 0.625f, sDecDiff1 = 0.7f, sDecDiff2 = 0.5f;
    float gIn[4] { 0.75f, 0.75f, 0.625f, 0.625f };
    float gDecA1 = 0.7f, gDecB1 = 0.7f, gDecA2 = 0.5f, gDecB2 = 0.5f;
    float lfoPhase = 0.0f, lfoPhaseOffset = 0.0f;
    float sEarly = 0.4f;
    float lastA = 0.0f, lastB = 0.0f;
    float designedSize = -1.0f;
    float maxSizeAllowed = 1.6f;
    bool first = true;
};

} // namespace nh
