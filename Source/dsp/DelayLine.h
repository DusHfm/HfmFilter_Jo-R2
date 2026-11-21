// Nachhall - delay lines and allpass diffusers
#pragma once

#include "DspCommon.h"

namespace nh
{
// Power-of-two circular buffer.
// Semantics: tap(d) returns the sample that was pushed d pushes ago (tap(1) = most recent push).
// Read-before-push therefore yields a delay of exactly d samples.
class DelayLine
{
public:
    void prepare (int maxDelaySamples)
    {
        size = nextPowerOfTwo (std::max (16, maxDelaySamples + 8));
        mask = size - 1;
        buffer.assign ((size_t) size, 0.0f);
        writePos = 0;
    }

    void reset() noexcept
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        writePos = 0;
    }

    int capacity() const noexcept { return size - 8; }

    inline void push (float x) noexcept
    {
        buffer[(size_t) writePos] = x;
        writePos = (writePos + 1) & mask;
    }

    inline float tap (int d) const noexcept { return buffer[(size_t) ((writePos - d) & mask)]; }

    // acc[i] += g * tap (d) as it was right after the i-th of the last n pushes (block-wise tap)
    inline void accumulate (float* acc, int n, int d, float g) const noexcept
    {
        const int start = (writePos - n + 1 - d) & mask;
        const int first = std::min (n, size - start);
        const float* b = buffer.data();
        for (int i = 0; i < first; ++i)
            acc[i] += g * b[start + i];
        for (int i = first; i < n; ++i)
            acc[i] += g * b[i - first];
    }

    // Linear interpolation, d >= 1
    inline float tapLinear (float d) const noexcept
    {
        const int di = (int) d;
        const float fr = d - (float) di;
        const float a = buffer[(size_t) ((writePos - di) & mask)];
        const float b = buffer[(size_t) ((writePos - di - 1) & mask)];
        return a + fr * (b - a);
    }

    // 3rd-order Lagrange interpolation, d >= 2. The fractional point always lies in the central
    // interval, where Lagrange interpolation is passive (|H| <= 1): safe inside feedback loops.
    inline float tapLagrange (float d) const noexcept
    {
        const int n = (int) d;
        const float mu = d - (float) n;
        const float ym1 = buffer[(size_t) ((writePos - (n - 1)) & mask)];
        const float y0 = buffer[(size_t) ((writePos - n) & mask)];
        const float y1 = buffer[(size_t) ((writePos - (n + 1)) & mask)];
        const float y2 = buffer[(size_t) ((writePos - (n + 2)) & mask)];
        const float dm1 = mu - 1.0f;
        const float dm2 = mu - 2.0f;
        const float dp1 = mu + 1.0f;
        const float hm1 = -mu * dm1 * dm2 * (1.0f / 6.0f);
        const float h0 = dp1 * dm1 * dm2 * 0.5f;
        const float h1 = -dp1 * mu * dm2 * 0.5f;
        const float h2 = dp1 * mu * dm1 * (1.0f / 6.0f);
        return hm1 * ym1 + h0 * y0 + h1 * y1 + h2 * y2;
    }

private:
    std::vector<float> buffer;
    int size = 0, mask = 0, writePos = 0;
};

// Schroeder allpass with (fractional) delay M:  H(z) = (z^-M - g) / (1 - g z^-M)  ->  |H| = 1
class Allpass
{
public:
    void prepare (int maxDelaySamples) { line.prepare (maxDelaySamples + 4); }
    void reset() noexcept { line.reset(); }

    inline float process (float x, float delaySamples, float g) noexcept
    {
        const float d = line.tapLagrange (delaySamples);
        const float v = x + g * d;
        line.push (v);
        return d - g * v;
    }

    inline float processLinear (float x, float delaySamples, float g) noexcept
    {
        const float d = line.tapLinear (delaySamples);
        const float v = x + g * d;
        line.push (v);
        return d - g * v;
    }

    // Access to the internal delay (used for plate output taps). Valid after the sample was processed.
    inline float tapInternal (float d) const noexcept { return line.tapLinear (d); }

private:
    DelayLine line;
};

} // namespace nh
