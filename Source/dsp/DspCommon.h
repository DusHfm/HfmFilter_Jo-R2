// Nachhall - algorithmic reverb
// Common DSP helpers (no JUCE dependency, so the engine can be unit-tested stand-alone).
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace nh
{
constexpr double kPi = 3.14159265358979323846;
constexpr float kPiF = 3.14159265358979f;
constexpr float kTwoPiF = 6.28318530717959f;

template <typename T>
inline T clampv (T x, T lo, T hi) noexcept { return std::min (hi, std::max (lo, x)); }

inline float lerpf (float a, float b, float t) noexcept { return a + (b - a) * t; }

// Interpolates in the logarithmic domain (a, b > 0)
inline float expLerp (float a, float b, float t) noexcept
{
    return std::exp (std::log (a) + (std::log (b) - std::log (a)) * t);
}

inline float dbToGain (float db) noexcept { return std::pow (10.0f, db * 0.05f); }
inline float gainToDb (float g) noexcept { return 20.0f * std::log10 (std::max (g, 1.0e-12f)); }

// Coefficient for a one-pole smoother that is updated every 'samples' samples
inline float smoothingAlpha (double timeConstantSeconds, double sampleRate, int samples) noexcept
{
    if (timeConstantSeconds <= 0.0)
        return 1.0f;
    return (float) (1.0 - std::exp (-(double) samples / (timeConstantSeconds * sampleRate)));
}

inline int nextPowerOfTwo (int x) noexcept
{
    int p = 1;
    while (p < x)
        p <<= 1;
    return p;
}

// Small, fast deterministic random generator (xorshift32)
struct Rng
{
    uint32_t state = 0x9E3779B9u;
    explicit Rng (uint32_t seed = 0x9E3779B9u) noexcept : state (seed != 0 ? seed : 0x9E3779B9u) {}
    uint32_t next() noexcept
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }
    float uniform() noexcept { return (float) (next() >> 8) * (1.0f / 16777216.0f); } // [0, 1)
    float bipolar() noexcept { return uniform() * 2.0f - 1.0f; }                       // [-1, 1)
};

// Passive soft limiter: |f(x)| <= |x| for all x, so it can never add energy to a feedback loop.
inline float passiveSaturate (float x, float k2) noexcept
{
    return x / std::sqrt (1.0f + k2 * x * x);
}

} // namespace nh
