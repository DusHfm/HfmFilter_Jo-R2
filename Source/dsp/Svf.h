// Nachhall - state variable filter (Andrew Simper / Cytomic "SvfLinearTrapOptimised2").
// Topology-preserving, robust against fast coefficient changes. The same coefficients can
// evaluate their exact digital frequency response for the GUI curves.
#pragma once

#include "DspCommon.h"
#include <complex>

namespace nh
{
enum class FilterShape : int
{
    Bell = 0,
    LowShelf,
    HighShelf,
    Notch,
    LowPass,   // 2nd order, Q = resonance
    HighPass,  // 2nd order
    LowPass1,  // 1st order (implemented with a cancelled pole/zero pair)
    HighPass1, // 1st order
    BandPass,
    AllPass,
    Identity
};

struct SvfCoefs
{
    float a1 = 1.0f, a2 = 0.0f, a3 = 0.0f;
    float m0 = 1.0f, m1 = 0.0f, m2 = 0.0f;

    // Analog prototype data (double) for response evaluation
    double g = 0.1, k = 1.41421356, dm0 = 1.0, dm1 = 0.0, dm2 = 0.0;
    bool identity = true;

    static SvfCoefs makeIdentity() noexcept { return SvfCoefs(); }

    // |H|^2 at a digital frequency given by tanW = tan(pi * f / fs)
    double magnitudeSqFromTan (double tanW) const noexcept
    {
        if (identity)
            return 1.0;
        const double w = tanW / g;
        const double dr = 1.0 - w * w;
        const double di = k * w;
        const double nr = dm0 * dr + dm2;
        const double ni = w * (dm0 * k + dm1);
        const double den = dr * dr + di * di;
        return den > 1.0e-300 ? (nr * nr + ni * ni) / den : 1.0;
    }

    double magnitudeSq (double f, double fs) const noexcept
    {
        return magnitudeSqFromTan (std::tan (kPi * std::min (f, fs * 0.49999) / fs));
    }

    double magnitudeDb (double f, double fs) const noexcept
    {
        return 10.0 * std::log10 (std::max (magnitudeSq (f, fs), 1.0e-30));
    }
};

inline SvfCoefs makeSvf (FilterShape shape, double fc, double q, double gainDb, double fs) noexcept
{
    if (shape == FilterShape::Identity)
        return SvfCoefs::makeIdentity();

    fc = clampv (fc, 1.0, fs * 0.4975);
    q = clampv (q, 0.01, 100.0);

    double g = std::tan (kPi * fc / fs);
    double k = 1.0 / q;
    double m0 = 1.0, m1 = 0.0, m2 = 0.0;
    const double A = std::pow (10.0, gainDb / 40.0);

    switch (shape)
    {
        case FilterShape::LowPass:   m0 = 0.0; m1 = 0.0; m2 = 1.0; break;
        case FilterShape::HighPass:  m0 = 1.0; m1 = -k; m2 = -1.0; break;
        case FilterShape::BandPass:  m0 = 0.0; m1 = 1.0; m2 = 0.0; break;
        case FilterShape::Notch:     m0 = 1.0; m1 = -k; m2 = 0.0; break;
        case FilterShape::AllPass:   m0 = 1.0; m1 = -2.0 * k; m2 = 0.0; break;
        case FilterShape::Bell:      k = 1.0 / (q * A); m0 = 1.0; m1 = k * (A * A - 1.0); m2 = 0.0; break;
        case FilterShape::LowShelf:  g = g / std::sqrt (A); m0 = 1.0; m1 = k * (A - 1.0); m2 = A * A - 1.0; break;
        case FilterShape::HighShelf: g = g * std::sqrt (A); m0 = A * A; m1 = k * (1.0 - A) * A; m2 = 1.0 - A * A; break;
        case FilterShape::LowPass1:  k = 2.0; m0 = 0.0; m1 = 1.0; m2 = 1.0; break;
        case FilterShape::HighPass1: k = 2.0; m0 = 1.0; m1 = -1.0; m2 = -1.0; break;
        case FilterShape::Identity:
        default: return SvfCoefs::makeIdentity();
    }

    SvfCoefs c;
    c.identity = false;
    c.g = g;
    c.k = k;
    c.dm0 = m0;
    c.dm1 = m1;
    c.dm2 = m2;
    const double a1 = 1.0 / (1.0 + g * (g + k));
    const double a2 = g * a1;
    const double a3 = g * a2;
    c.a1 = (float) a1;
    c.a2 = (float) a2;
    c.a3 = (float) a3;
    c.m0 = (float) m0;
    c.m1 = (float) m1;
    c.m2 = (float) m2;
    return c;
}

struct Svf
{
    float ic1 = 0.0f, ic2 = 0.0f;

    inline float process (float v0, const SvfCoefs& c) noexcept
    {
        const float v3 = v0 - ic2;
        const float v1 = c.a1 * ic1 + c.a2 * v3;
        const float v2 = ic2 + c.a2 * ic1 + c.a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        return c.m0 * v0 + c.m1 * v1 + c.m2 * v2;
    }

    void reset() noexcept { ic1 = ic2 = 0.0f; }
};

// Structure-of-arrays bank of N filters processed in parallel (auto-vectorises well).
template <int N>
struct SvfBank
{
    alignas (32) float a1[(size_t) N];
    alignas (32) float a2[(size_t) N];
    alignas (32) float a3[(size_t) N];
    alignas (32) float m0[(size_t) N];
    alignas (32) float m1[(size_t) N];
    alignas (32) float m2[(size_t) N];
    alignas (32) float ic1[(size_t) N];
    alignas (32) float ic2[(size_t) N];

    SvfBank() noexcept
    {
        for (int i = 0; i < N; ++i)
            setIdentity (i);
        reset();
    }

    void reset() noexcept
    {
        for (int i = 0; i < N; ++i)
            ic1[i] = ic2[i] = 0.0f;
    }

    void setIdentity (int i) noexcept
    {
        a1[i] = 1.0f; a2[i] = 0.0f; a3[i] = 0.0f;
        m0[i] = 1.0f; m1[i] = 0.0f; m2[i] = 0.0f;
    }

    void set (int i, const SvfCoefs& c) noexcept
    {
        a1[i] = c.a1; a2[i] = c.a2; a3[i] = c.a3;
        m0[i] = c.m0; m1[i] = c.m1; m2[i] = c.m2;
    }

    inline void process (float* x) noexcept
    {
        for (int i = 0; i < N; ++i)
        {
            const float v0 = x[i];
            const float v3 = v0 - ic2[i];
            const float v1 = a1[i] * ic1[i] + a2[i] * v3;
            const float v2 = ic2[i] + a2[i] * ic1[i] + a3[i] * v3;
            ic1[i] = 2.0f * v1 - ic1[i];
            ic2[i] = 2.0f * v2 - ic2[i];
            x[i] = m0[i] * v0 + m1[i] * v1 + m2[i] * v2;
        }
    }
};

} // namespace nh
