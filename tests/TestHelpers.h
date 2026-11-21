// Shared helpers for the offline tests (no JUCE)
#pragma once

#include "../Source/dsp/ReverbEngine.h"

#include <complex>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#if defined(__SSE__) || defined(_M_X64)
 #include <xmmintrin.h>
#endif

namespace testing
{
inline int& failureCount()
{
    static int f = 0;
    return f;
}

inline void check (bool ok, const std::string& what)
{
    std::printf ("  [%s] %s\n", ok ? " OK " : "FAIL", what.c_str());
    if (! ok)
        ++failureCount();
}

inline void enableFlushToZero()
{
#if defined(__SSE__) || defined(_M_X64)
    _mm_setcsr (_mm_getcsr() | 0x8040); // FTZ + DAZ, like JUCE's ScopedNoDenormals
#endif
}

struct Stereo
{
    std::vector<float> l, r;
    std::vector<float> mono() const
    {
        std::vector<float> m (l.size());
        for (size_t i = 0; i < l.size(); ++i)
            m[i] = 0.5f * (l[i] + r[i]);
        return m;
    }
};

// Renders the engine for 'seconds'. The engine first runs 0.6 s of silence so that all parameter
// smoothing has settled. input (sampleIndex, left, right) fills the input samples.
inline Stereo render (const nh::EngineParams& p, double fs, double seconds, const std::function<void (int, float&, float&)>& input,
                      const std::function<void (int, nh::EngineParams&)>& automation = {})
{
    nh::ReverbEngine e;
    nh::EngineParams params = p;
    e.setParams (params);
    e.prepare (fs, 256);
    std::vector<float> a (256), b (256);
    for (int i = 0; i < (int) (0.6 * fs / 256); ++i)
    {
        std::fill (a.begin(), a.end(), 0.0f);
        std::fill (b.begin(), b.end(), 0.0f);
        e.process (a.data(), b.data(), 256);
    }
    const int total = (int) (seconds * fs);
    Stereo out;
    out.l.assign ((size_t) total, 0.0f);
    out.r.assign ((size_t) total, 0.0f);
    for (int pos = 0; pos < total; pos += 256)
    {
        const int n = std::min (256, total - pos);
        if (automation)
        {
            automation (pos, params);
            e.setParams (params);
        }
        for (int i = 0; i < n; ++i)
            input (pos + i, out.l[(size_t) (pos + i)], out.r[(size_t) (pos + i)]);
        e.process (out.l.data() + pos, out.r.data() + pos, n);
    }
    return out;
}

inline Stereo renderImpulse (const nh::EngineParams& p, double fs, double seconds, float left = 1.0f, float right = 1.0f)
{
    return render (p, fs, seconds, [left, right] (int i, float& l, float& r) {
        l = i == 0 ? left : 0.0f;
        r = i == 0 ? right : 0.0f;
    });
}

inline std::vector<float> bandpass (const std::vector<float>& x, double fs, double centre, double q)
{
    const auto bp = nh::makeSvf (nh::FilterShape::BandPass, centre, q, 0.0, fs);
    nh::Svf f1, f2;
    std::vector<float> y (x.size());
    for (size_t i = 0; i < x.size(); ++i)
        y[i] = f2.process (f1.process (x[i], bp), bp);
    return y;
}

inline double energy (const std::vector<float>& x, double fs, double t0, double t1)
{
    const size_t a = (size_t) std::max (0.0, t0 * fs), b = std::min (x.size(), (size_t) (t1 * fs));
    double s = 0.0;
    for (size_t i = a; i < b; ++i)
        s += (double) x[i] * x[i];
    return s;
}

inline double db (double ratio) { return 10.0 * std::log10 (std::max (ratio, 1.0e-30)); }

inline double rms (const std::vector<float>& x, size_t from, size_t to)
{
    double s = 0.0;
    to = std::min (to, x.size());
    for (size_t i = from; i < to; ++i)
        s += (double) x[i] * x[i];
    return std::sqrt (s / (double) std::max<size_t> (1, to - from));
}

// T60 of an octave band from the Schroeder integral (T30, or T20 if the range is short)
inline double measureT60 (const std::vector<float>& x, double fs, double centre, double skipSeconds = 0.0, double q = 1.414)
{
    const auto y = bandpass (x, fs, centre, q);
    std::vector<double> edc (y.size());
    double acc = 0.0;
    for (size_t i = y.size(); i-- > 0;)
    {
        acc += (double) y[i] * y[i];
        edc[i] = acc;
    }
    const size_t start = (size_t) (skipSeconds * fs);
    const double ref = edc[start];
    if (ref <= 0.0)
        return 0.0;
    double t5 = -1.0, t25 = -1.0, t35 = -1.0;
    for (size_t i = start; i < edc.size(); ++i)
    {
        const double d = 10.0 * std::log10 (edc[i] / ref + 1.0e-30);
        if (t5 < 0.0 && d <= -5.0)
            t5 = (double) i / fs;
        if (t25 < 0.0 && d <= -25.0)
            t25 = (double) i / fs;
        if (t35 < 0.0 && d <= -35.0)
        {
            t35 = (double) i / fs;
            break;
        }
    }
    if (t5 >= 0.0 && t35 > t5)
        return 2.0 * (t35 - t5);
    if (t5 >= 0.0 && t25 > t5)
        return 3.0 * (t25 - t5);
    return 0.0;
}

// Normalised echo density (Abel & Huang): 1.0 = Gaussian-like dense reverb
inline double echoDensity (const std::vector<float>& x, double fs, double t0, double t1)
{
    const size_t a = (size_t) (t0 * fs), b = std::min (x.size(), (size_t) (t1 * fs));
    if (b <= a)
        return 0.0;
    double s = 0.0;
    for (size_t i = a; i < b; ++i)
        s += (double) x[i] * x[i];
    const double sd = std::sqrt (s / (double) (b - a)) + 1.0e-30;
    size_t over = 0;
    for (size_t i = a; i < b; ++i)
        if (std::abs ((double) x[i]) > sd)
            ++over;
    return ((double) over / (double) (b - a)) / 0.3173;
}

// ISO 3382 centre time Ts (s): later energy = more distant / diffuse
inline double centreTime (const std::vector<float>& x, double fs, double t1)
{
    double num = 0.0, den = 0.0;
    const size_t n = std::min (x.size(), (size_t) (t1 * fs));
    for (size_t i = 0; i < n; ++i)
    {
        const double e = (double) x[i] * x[i];
        num += e * (double) i / fs;
        den += e;
    }
    return den > 0.0 ? num / den : 0.0;
}

inline double correlation (const std::vector<float>& a, const std::vector<float>& b, double fs, double t0, double t1)
{
    const size_t i0 = (size_t) (t0 * fs), i1 = std::min (a.size(), (size_t) (t1 * fs));
    double sab = 0, saa = 0, sbb = 0;
    for (size_t i = i0; i < i1; ++i)
    {
        sab += (double) a[i] * b[i];
        saa += (double) a[i] * a[i];
        sbb += (double) b[i] * b[i];
    }
    return sab / std::sqrt (saa * sbb + 1.0e-30);
}

// In-place radix-2 FFT
inline void fft (std::vector<std::complex<double>>& x)
{
    const size_t n = x.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap (x[i], x[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = -2.0 * nh::kPi / (double) len;
        const std::complex<double> wl (std::cos (ang), std::sin (ang));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> w (1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k)
            {
                const auto u = x[i + k];
                const auto v = x[i + k + len / 2] * w;
                x[i + k] = u + v;
                x[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

// Power spectrum of a Hann-windowed segment (zero padded to a power of two)
inline std::vector<double> powerSpectrum (const std::vector<float>& x, double fs, double t0, double t1, size_t& fftSize)
{
    const size_t a = (size_t) (t0 * fs), b = std::min (x.size(), (size_t) (t1 * fs));
    const size_t len = b - a;
    fftSize = 1;
    while (fftSize < len)
        fftSize <<= 1;
    std::vector<std::complex<double>> buf (fftSize);
    for (size_t i = 0; i < len; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (2.0 * nh::kPi * (double) i / (double) (len - 1));
        buf[i] = (double) x[a + i] * w;
    }
    fft (buf);
    std::vector<double> p (fftSize / 2);
    for (size_t k = 0; k < p.size(); ++k)
        p[k] = std::norm (buf[k]);
    return p;
}

inline double spectralCentroid (const std::vector<float>& x, double fs, double t0, double t1)
{
    size_t n = 0;
    const auto p = powerSpectrum (x, fs, t0, t1, n);
    double num = 0.0, den = 0.0;
    for (size_t k = 1; k < p.size(); ++k)
    {
        const double f = (double) k * fs / (double) n;
        num += f * p[k];
        den += p[k];
    }
    return den > 0.0 ? num / den : 0.0;
}

inline double bandPower (const std::vector<double>& p, double fs, size_t n, double f0, double f1)
{
    double s = 0.0;
    for (size_t k = 0; k < p.size(); ++k)
    {
        const double f = (double) k * fs / (double) n;
        if (f >= f0 && f < f1)
            s += p[k];
    }
    return s;
}

inline void writeWav (const std::string& path, const std::vector<float>& l, const std::vector<float>& r, int fs)
{
    FILE* f = std::fopen (path.c_str(), "wb");
    if (f == nullptr)
        return;
    const uint32_t n = (uint32_t) l.size();
    const uint32_t dataBytes = n * 2 * 4;
    auto w32 = [&] (uint32_t v) { std::fwrite (&v, 4, 1, f); };
    auto w16 = [&] (uint16_t v) { std::fwrite (&v, 2, 1, f); };
    std::fwrite ("RIFF", 1, 4, f);
    w32 (36 + dataBytes);
    std::fwrite ("WAVEfmt ", 1, 8, f);
    w32 (16);
    w16 (3);
    w16 (2);
    w32 ((uint32_t) fs);
    w32 ((uint32_t) fs * 8);
    w16 (8);
    w16 (32);
    std::fwrite ("data", 1, 4, f);
    w32 (dataBytes);
    for (uint32_t i = 0; i < n; ++i)
    {
        std::fwrite (&l[i], 4, 1, f);
        std::fwrite (&r[i], 4, 1, f);
    }
    std::fclose (f);
}

} // namespace testing
