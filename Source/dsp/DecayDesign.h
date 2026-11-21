// Nachhall - frequency dependent decay ("Decay Rate EQ") design
//
// Every loop segment of a reverb tank gets an attenuation filter whose dB response is
// proportional to the segment length:   A(f) = -60 dB * delay / T60(f)
//
// The target decay curve is defined in the "rate domain":
//     T60(f) = T60mid / S(f),   S(f) = 1 + sum_k u_k * shape_k(f),   u_k = 1/rate_k - 1
// so every band can be realised by one filter per segment with dB gain = c * u_k
// (c = -60 * delay / T60mid). A guard keeps S(f) >= Smin everywhere, so the loop gain is
// always < 1 (the tank can never become unstable, whatever the user dials in).
#pragma once

#include "Svf.h"

namespace nh
{
enum DecayBandShape : int
{
    kDecayBell = 0,
    kDecayLowShelf = 1,
    kDecayHighShelf = 2,
    kDecayNotch = 3,
    kNumDecayShapes = 4
};

constexpr int kNumDecayBands = 6;
constexpr int kNumRoomDecayStages = 5; // bass, HF absorption (2 staggered shelves), air, air absorption over time
constexpr int kMaxDecayStages = kNumRoomDecayStages + kNumDecayBands;

constexpr float kMinBandRate = 0.25f;
constexpr float kMaxBandRate = 4.0f;

struct DecayBandParams
{
    bool used = false;
    bool enabled = true;
    int shape = kDecayBell;
    float freq = 1000.0f;
    float rate = 1.0f; // decay time multiplier (0.25 ... 4)
    float q = 1.0f;

    bool active() const noexcept { return used && enabled; }
    bool operator== (const DecayBandParams& o) const noexcept
    {
        auto same = [] (float a, float b) { return ! (a < b) && ! (b < a); };
        return used == o.used && enabled == o.enabled && shape == o.shape && same (freq, o.freq) && same (rate, o.rate) && same (q, o.q);
    }
};

struct DecaySettings
{
    float t60 = 1.5f; // mid-band decay incl. Decay Rate
    float bassRatio = 1.1f, bassFreq = 220.0f;
    float hfRatio = 0.5f, hfFreq = 3600.0f;
    float airRatio = 0.8f, airFreq = 11000.0f;
    // high frequencies lose a fixed amount per second (like air absorption): longer decays get
    // relatively darker - Pro-R 2 behaves like this when the Decay Rate is raised
    float airAbsRate = 0.0f;   // 1/s on the plateau of the shelf
    float airAbsFreq = 7000.0f, airAbsQ = 0.5f;
    DecayBandParams bands[kNumDecayBands];
};

struct DecayStage
{
    bool active = false;
    FilterShape shape = FilterShape::Bell;
    float freq = 1000.0f;
    float q = 0.707f;
    float u = 0.0f;
    bool fixedNotch = false;
};

struct DecaySpec
{
    float t60 = 1.5f;
    DecayStage stages[kMaxDecayStages];
};

// Builds the stage list and applies the stability guard.
DecaySpec buildDecaySpec (const DecaySettings& settings, double sampleRate, double referenceDelaySeconds);

// Neutral room (only the user bands) - used for the blue Decay Rate EQ curve.
DecaySpec buildUserDecaySpec (const DecayBandParams* bands, double sampleRate, double referenceDelaySeconds);

// Coefficients for one loop segment of the given length.
void designSegmentFilters (const DecaySpec& spec, double sampleRate, double delaySeconds,
                           SvfCoefs* coefsOut /* [kMaxDecayStages] */, float& broadbandGainOut) noexcept;

// T60 (seconds) at the given frequencies for a reference segment (display / analysis).
void decayTimeCurve (const DecaySpec& spec, double sampleRate, double referenceDelaySeconds,
                     const float* freqs, float* t60Out, int numPoints);

} // namespace nh
