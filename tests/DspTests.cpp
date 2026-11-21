// Offline tests for the Nachhall reverb engine (no JUCE required).
//   - decay time accuracy per style / space / decay rate / octave band
//   - Decay Rate EQ (bell + notch) effect
//   - stability under random parameter changes and extreme settings
//   - freeze sustain
//   - ducking and gate behaviour
//   - CPU usage
// Usage: DspTests [outputDirForWavs]
#include "../Source/dsp/ReverbEngine.h"
#include "../Source/dsp/Tuning.h"

#include <chrono>
#include <cstdio>
#include <string>

#if defined(__SSE__) || defined(_M_X64)
 #include <xmmintrin.h>
#endif

using namespace nh;

namespace
{
int failures = 0;

void check (bool ok, const char* what)
{
    std::printf ("  [%s] %s\n", ok ? " OK " : "FAIL", what);
    if (! ok)
        ++failures;
}

void writeWav (const std::string& path, const std::vector<float>& l, const std::vector<float>& r, int fs)
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

EngineParams wetParams()
{
    EngineParams p;
    p.mix = 1.0f;
    p.predelayMs = 0.0f;
    p.distance = 0.3f;
    p.character = 0.3f;
    p.thickness = 0.5f; // Pro-R scale 0 %: no compression / expansion -> linear
    p.brightness = 0.5f;
    p.width = 0.7f;
    return p;
}

struct Render
{
    std::vector<float> l, r;
};

Render renderImpulse (const EngineParams& p, double fs, double seconds, float left = 1.0f, float right = 1.0f)
{
    ReverbEngine e;
    e.setParams (p);
    e.prepare (fs, 512);
    const int total = (int) (seconds * fs);
    Render out;
    out.l.assign ((size_t) total, 0.0f);
    out.r.assign ((size_t) total, 0.0f);
    // let parameter smoothing settle with silence
    {
        std::vector<float> a (512, 0.0f), b (512, 0.0f);
        for (int i = 0; i < (int) (0.6 * fs / 512); ++i)
        {
            std::fill (a.begin(), a.end(), 0.0f);
            std::fill (b.begin(), b.end(), 0.0f);
            e.process (a.data(), b.data(), 512);
        }
    }
    out.l[0] = left;
    out.r[0] = right;
    for (int pos = 0; pos < total; pos += 512)
    {
        const int n = std::min (512, total - pos);
        e.process (out.l.data() + pos, out.r.data() + pos, n);
    }
    return out;
}

// Octave band filtered energy decay -> T30 based T60 estimate
double measureT60 (const std::vector<float>& x, double fs, double centre, double skipSeconds = 0.0)
{
    std::vector<double> e (x.size());
    SvfCoefs bp = makeSvf (FilterShape::BandPass, centre, 1.414, 0.0, fs);
    Svf f1, f2;
    for (size_t i = 0; i < x.size(); ++i)
    {
        const float y = f2.process (f1.process (x[i], bp), bp);
        e[i] = (double) y * y;
    }
    // Schroeder backward integration
    std::vector<double> edc (x.size());
    double acc = 0.0;
    for (size_t i = x.size(); i-- > 0;)
    {
        acc += e[i];
        edc[i] = acc;
    }
    const size_t start = (size_t) (skipSeconds * fs);
    const double ref = edc[start];
    if (ref <= 0.0)
        return 0.0;
    double t5 = -1.0, t35 = -1.0, t25 = -1.0;
    for (size_t i = start; i < edc.size(); ++i)
    {
        const double db = 10.0 * std::log10 (edc[i] / ref + 1.0e-30);
        if (t5 < 0.0 && db <= -5.0)
            t5 = (double) i / fs;
        if (t25 < 0.0 && db <= -25.0)
            t25 = (double) i / fs;
        if (t35 < 0.0 && db <= -35.0)
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

double rms (const std::vector<float>& x, size_t from, size_t to)
{
    double s = 0.0;
    to = std::min (to, x.size());
    for (size_t i = from; i < to; ++i)
        s += (double) x[i] * x[i];
    return std::sqrt (s / std::max<size_t> (1, to - from));
}

const char* styleName (int s) { return s == 0 ? "Modern" : (s == 1 ? "Vintage" : "Plate"); }

} // namespace

int main (int argc, char** argv)
{
#if defined(__SSE__) || defined(_M_X64)
    _mm_setcsr (_mm_getcsr() | 0x8040); // FTZ + DAZ, like JUCE's ScopedNoDenormals
#endif
    const std::string outDir = argc > 1 ? argv[1] : "";
    const double fs = 48000.0;

    //--------------------------------------------------------------------------
    std::printf ("\n== Decay time accuracy (mid band, 500 Hz / 1 kHz average) ==\n");
    {
        const float spaces[] = { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };
        for (int style = 0; style < 3; ++style)
        {
            for (float sp : spaces)
            {
                for (float rate : { 0.5f, 1.0f, 2.0f })
                {
                    if (sp >= 0.99f && rate > 1.5f)
                        continue; // 20 s tails take long to render
                    EngineParams p = wetParams();
                    p.style = style;
                    p.space = sp;
                    p.decayRate = rate;
                    p.brightness = 0.75f; // keep high bands long enough to measure the mid band cleanly
                    // the decay the engine aims for (room, Decay Rate step and style corrections fitted to Pro-R 2)
                    const double expected = ReverbEngine::makeDecaySettings (p.decayBands, sp, rate, p.brightness, style).t60;
                    const double len = std::min (30.0, expected * 1.3 + 1.0);
                    const Render r = renderImpulse (p, fs, len);
                    std::vector<float> mono (r.l.size());
                    for (size_t i = 0; i < mono.size(); ++i)
                        mono[i] = 0.5f * (r.l[i] + r.r[i]);
                    const double skip = 0.05 + 0.02 * expected;
                    const double m = 0.5 * (measureT60 (mono, fs, 500.0, skip) + measureT60 (mono, fs, 1000.0, skip));
                    const double err = m / expected - 1.0;
                    char buf[200];
                    std::snprintf (buf, sizeof (buf), "%-7s space %.2f rate %3.0f%%: expected %6.2f s, measured %6.2f s (%+5.1f %%)",
                                   styleName (style), sp, rate * 100.0f, expected, m, err * 100.0);
                    check (std::abs (err) < 0.2, buf);

                    if (! outDir.empty() && rate == 1.0f && (sp == 0.25f || sp == 0.5f || sp == 0.75f))
                        writeWav (outDir + "/ir_" + styleName (style) + "_" + std::to_string ((int) (sp * 100)) + ".wav", r.l, r.r, (int) fs);
                }
            }
        }
    }

    //--------------------------------------------------------------------------
    std::printf ("\n== Frequency dependent decay (Brightness, Decay Rate EQ) ==\n");
    {
        EngineParams p = wetParams();
        p.space = 0.6f;
        p.brightness = 0.5f;
        const Render r = renderImpulse (p, fs, 6.0);
        std::vector<float> mono (r.l.size());
        for (size_t i = 0; i < mono.size(); ++i)
            mono[i] = 0.5f * (r.l[i] + r.r[i]);
        const double tLow = measureT60 (mono, fs, 125.0, 0.05);
        const double tMid = measureT60 (mono, fs, 1000.0, 0.05);
        const double tHigh = measureT60 (mono, fs, 8000.0, 0.05);
        std::printf ("  Brightness 0 %% (Pro-R scale): T60 125 Hz %.2f s, 1 kHz %.2f s, 8 kHz %.2f s\n", tLow, tMid, tHigh);
        // Pro-R 2, Church 3.2 s (recording R04): 8 kHz / 1 kHz 0.78, 125 Hz / 1 kHz 1.07
        check (std::abs (tHigh / tMid - 0.78) < 0.08 && std::abs (tLow / tMid - 1.07) < 0.1,
               "natural HF damping and slightly longer bass like Pro-R 2 (8 kHz x0.78, 125 Hz x1.07)");

        EngineParams q = p;
        q.decayBands[0].used = true;
        q.decayBands[0].shape = kDecayBell;
        q.decayBands[0].freq = 2000.0f;
        q.decayBands[0].rate = 3.0f;
        q.decayBands[0].q = 1.0f;
        const Render r2 = renderImpulse (q, fs, 10.0);
        for (size_t i = 0; i < mono.size() && i < r2.l.size(); ++i)
            mono[i] = 0.5f * (r2.l[i] + r2.r[i]);
        mono.resize (r2.l.size());
        for (size_t i = 0; i < mono.size(); ++i)
            mono[i] = 0.5f * (r2.l[i] + r2.r[i]);
        const double t2k = measureT60 (mono, fs, 2000.0, 0.05);
        const double t250 = measureT60 (mono, fs, 250.0, 0.05);
        const double base2k = measureT60 ([&] {
            std::vector<float> m2 (r.l.size());
            for (size_t i = 0; i < m2.size(); ++i)
                m2[i] = 0.5f * (r.l[i] + r.r[i]);
            return m2;
        }(), fs, 2000.0, 0.05);
        std::printf ("  Bell 2 kHz x3.0: T60 2 kHz %.2f s (was %.2f s), 250 Hz %.2f s\n", t2k, base2k, t250);
        check (t2k > base2k * 2.2 && t2k < base2k * 3.6, "decay rate band multiplies the decay at its frequency");

        EngineParams nq = p;
        nq.decayBands[1].used = true;
        nq.decayBands[1].shape = kDecayNotch;
        nq.decayBands[1].freq = 1000.0f;
        nq.decayBands[1].q = 2.0f;
        const Render r3 = renderImpulse (nq, fs, 6.0);
        std::vector<float> m3 (r3.l.size());
        for (size_t i = 0; i < m3.size(); ++i)
            m3[i] = 0.5f * (r3.l[i] + r3.r[i]);
        const double eNotch = rms (m3, (size_t) (0.3 * fs), (size_t) (1.5 * fs));
        // compare band energy around 1 kHz
        auto bandRms = [&] (const std::vector<float>& x, double fc)
        {
            SvfCoefs bp = makeSvf (FilterShape::BandPass, fc, 8.0, 0.0, fs);
            Svf a, b;
            std::vector<float> y (x.size());
            for (size_t i = 0; i < x.size(); ++i)
                y[i] = b.process (a.process (x[i], bp), bp);
            return rms (y, (size_t) (0.3 * fs), (size_t) (1.5 * fs));
        };
        const double n1k = bandRms (m3, 1000.0);
        const double o1k = bandRms (mono.size() ? [&] {
            std::vector<float> m2 (r.l.size());
            for (size_t i = 0; i < m2.size(); ++i)
                m2[i] = 0.5f * (r.l[i] + r.r[i]);
            return m2;
        }() : mono, 1000.0);
        std::printf ("  Notch 1 kHz: tail energy at 1 kHz %.1f dB relative\n", 20.0 * std::log10 (n1k / o1k));
        check (n1k < o1k * 0.1 && eNotch > 0.0, "decay notch removes ringing at its frequency");
    }

    //--------------------------------------------------------------------------
    std::printf ("\n== Stability (random parameter automation, extreme settings) ==\n");
    for (int run = 0; run < 6; ++run)
    {
        const int style = run % 3;
        const bool withPostEq = run < 3;
        ReverbEngine e;
        EngineParams p = wetParams();
        p.style = style;
        e.setParams (p);
        e.prepare (fs, 256);
        Rng rng (1234u + (uint32_t) style);
        float maxAbs = 0.0f;
        bool finite = true;
        std::vector<float> l (256), r (256);
        const int blocks = (int) (40.0 * fs / 256);
        for (int b = 0; b < blocks; ++b)
        {
            if (b % 40 == 0)
            {
                p.space = rng.uniform();
                p.decayRate = expLerp (0.25f, 4.0f, rng.uniform());
                p.character = rng.uniform();
                p.thickness = rng.uniform();
                p.distance = rng.uniform();
                p.brightness = rng.uniform();
                p.width = 2.0f * rng.uniform();
                p.predelayMs = 500.0f * rng.uniform();
                p.duckingDb = 30.0f * rng.uniform();
                p.gateOn = rng.uniform() < 0.3f;
                p.freeze = rng.uniform() < 0.15f;
                if (rng.uniform() < 0.1f)
                    p.style = (int) (rng.uniform() * 2.99f);
                for (int k = 0; k < kNumDecayBands; ++k)
                {
                    auto& d = p.decayBands[k];
                    d.used = rng.uniform() < 0.5f;
                    d.shape = (int) (rng.uniform() * 3.99f);
                    d.freq = expLerp (20.0f, 20000.0f, rng.uniform());
                    d.rate = expLerp (0.25f, 4.0f, rng.uniform());
                    d.q = expLerp (0.1f, 20.0f, rng.uniform());
                }
                for (int k = 0; k < kNumPostBands; ++k)
                {
                    auto& d = p.postBands[k];
                    d.used = withPostEq && rng.uniform() < 0.5f;
                    d.shape = (int) (rng.uniform() * 4.99f);
                    d.freq = expLerp (20.0f, 20000.0f, rng.uniform());
                    d.gain = -30.0f + 60.0f * rng.uniform();
                    d.q = expLerp (0.1f, 20.0f, rng.uniform());
                    d.slope = (int) (rng.uniform() * 8.99f);
                    d.placement = (int) (rng.uniform() * 4.99f);
                }
                e.setParams (p);
            }
            const bool loud = (b / 200) % 2 == 0;
            for (int i = 0; i < 256; ++i)
            {
                l[(size_t) i] = loud ? 0.5f * rng.bipolar() : 0.0f;
                r[(size_t) i] = loud ? 0.5f * rng.bipolar() : 0.0f;
            }
            e.process (l.data(), r.data(), 256);
            for (int i = 0; i < 256; ++i)
            {
                if (! std::isfinite (l[(size_t) i]) || ! std::isfinite (r[(size_t) i]))
                    finite = false;
                maxAbs = std::max (maxAbs, std::max (std::abs (l[(size_t) i]), std::abs (r[(size_t) i])));
            }
        }
        char buf[160];
        std::snprintf (buf, sizeof (buf), "%s: 40 s random automation %s, finite output, peak %.2f", styleName (style),
                       withPostEq ? "incl. +-30 dB Post EQ" : "without Post EQ", maxAbs);
        check (finite && maxAbs < (withPostEq ? 60.0f : 8.0f), buf);
    }

    //--------------------------------------------------------------------------
    std::printf ("\n== Freeze ==\n");
    for (int style = 0; style < 3; ++style)
    {
        ReverbEngine e;
        EngineParams p = wetParams();
        p.style = style;
        p.space = 0.4f;
        e.setParams (p);
        e.prepare (fs, 256);
        Rng rng (77u);
        std::vector<float> outL;
        std::vector<float> l (256), r (256);
        const int blocks = (int) (12.0 * fs / 256);
        for (int b = 0; b < blocks; ++b)
        {
            const double t = (double) b * 256 / fs;
            if (t > 1.0 && ! p.freeze)
            {
                p.freeze = true;
                e.setParams (p);
            }
            for (int i = 0; i < 256; ++i)
            {
                const float x = t < 1.0 ? 0.3f * rng.bipolar() : 0.0f;
                l[(size_t) i] = x;
                r[(size_t) i] = x;
            }
            e.process (l.data(), r.data(), 256);
            outL.insert (outL.end(), l.begin(), l.end());
        }
        const double a = rms (outL, (size_t) (2.0 * fs), (size_t) (3.0 * fs));
        const double b = rms (outL, (size_t) (11.0 * fs), (size_t) (12.0 * fs));
        char buf[160];
        std::snprintf (buf, sizeof (buf), "%s: level after 9 s of freeze %.2f dB (should stay ~0)", styleName (style), 20.0 * std::log10 (b / a));
        check (a > 1.0e-4 && std::abs (20.0 * std::log10 (b / a)) < 1.0, buf);
    }

    //--------------------------------------------------------------------------
    std::printf ("\n== Ducking and Auto Gate ==\n");
    {
        auto renderBurst = [&] (EngineParams p, double& wetDuringInput, double& tailAfter)
        {
            ReverbEngine e;
            e.setParams (p);
            e.prepare (fs, 256);
            Rng rng (5u);
            std::vector<float> outL;
            std::vector<float> l (256), r (256);
            const int blocks = (int) (4.0 * fs / 256);
            for (int b = 0; b < blocks; ++b)
            {
                const double t = (double) b * 256 / fs;
                for (int i = 0; i < 256; ++i)
                {
                    const float x = (t > 0.5 && t < 2.0) ? 0.3f * rng.bipolar() : 0.0f;
                    l[(size_t) i] = x;
                    r[(size_t) i] = x;
                }
                e.process (l.data(), r.data(), 256);
                outL.insert (outL.end(), l.begin(), l.end());
            }
            wetDuringInput = rms (outL, (size_t) (1.5 * fs), (size_t) (2.0 * fs));
            tailAfter = rms (outL, (size_t) (2.6 * fs), (size_t) (3.0 * fs));
        };
        EngineParams p = wetParams();
        p.space = 0.55f;
        double w0, t0, w1, t1, w2, t2;
        renderBurst (p, w0, t0);
        EngineParams pd = p;
        pd.duckingDb = 18.0f;
        renderBurst (pd, w1, t1);
        std::printf ("  ducking 18 dB: wet level while playing %.1f dB, tail after %.1f dB\n", 20 * std::log10 (w1 / w0), 20 * std::log10 (t1 / t0));
        check (w1 < w0 * 0.35 && t1 > t0 * 0.5, "ducking lowers the reverb while the input plays, tail returns");
        EngineParams pg = p;
        pg.gateOn = true;
        pg.gateHoldMs = 100.0f;
        renderBurst (pg, w2, t2);
        std::printf ("  auto gate 100 ms: tail after input stops %.1f dB\n", 20 * std::log10 (t2 / t0 + 1e-12));
        check (t2 < t0 * 0.05 && w2 > w0 * 0.8, "gate cuts the tail after the hold time");
    }

    //--------------------------------------------------------------------------
    std::printf ("\n== CPU ==\n");
    for (int style = 0; style < 3; ++style)
    {
        ReverbEngine e;
        EngineParams p = wetParams();
        p.style = style;
        p.mix = 0.3f;
        for (int k = 0; k < 4; ++k)
        {
            p.decayBands[k].used = true;
            p.decayBands[k].freq = 200.0f * (float) (k + 1) * (float) (k + 1);
            p.postBands[k].used = true;
            p.postBands[k].freq = 300.0f * (float) (k + 1);
            p.postBands[k].gain = 3.0f;
        }
        p.postBands[0].shape = kPostLowCut;
        p.postBands[0].slope = 8;
        e.setParams (p);
        e.prepare (fs, 512);
        Rng rng (9u);
        std::vector<float> l (512), r (512);
        const double seconds = 20.0;
        const int blocks = (int) (seconds * fs / 512);
        const auto t0 = std::chrono::high_resolution_clock::now();
        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < 512; ++i)
            {
                l[(size_t) i] = 0.2f * rng.bipolar();
                r[(size_t) i] = 0.2f * rng.bipolar();
            }
            e.process (l.data(), r.data(), 512);
        }
        const auto t1 = std::chrono::high_resolution_clock::now();
        const double used = std::chrono::duration<double> (t1 - t0).count();
        std::printf ("  %-7s: %.2f %% of one core at 48 kHz (realtime factor %.0fx)\n", styleName (style), 100.0 * used / seconds, seconds / used);
    }

    //--------------------------------------------------------------------------
    std::printf ("\n== Level (impulse response energy, 100 %% wet, base settings of the Pro-R 2 recordings) ==\n");
    {
        // Nachhall follows the level of FabFilter Pro-R 2: energy of the left output for a unit impulse
        // on the left input, measured like NachhallReference does on the recordings in reference/pro-r
        // (dB). Modern: R01, RZ_0400ms, R02, ... R06 (11 sizes); Vintage and Plate: S_*_Klein / _Basis /
        // _Groesster (0.75 / 1.85 / 10 s).
        static const double proModern[kNumRoomModels] = { -3.11, -5.26, -6.87, -7.89, -7.65, -7.48, -7.20, -6.54, -5.77, -4.63, -3.56 };
        static const double proStyle[2][3] = { { -5.95, -6.99, -3.09 }, { -7.70, -5.71, -1.56 } };
        static const float styleT60s[3] = { 0.75f, 1.85f, 10.0f };
        auto irLevel = [&] (int style, float sp)
        {
            EngineParams p = wetParams();
            p.style = style;
            p.space = sp;
            p.distance = 0.5f;
            p.character = 0.5f;
            p.width = 1.0f;
            const Render r = renderImpulse (p, fs, 10.0, 1.0f, 0.0f);
            double e = 0.0;
            for (float v : r.l)
                e += (double) v * v;
            return 10.0 * std::log10 (std::max (e, 1.0e-30));
        };
        for (int i = 0; i < kNumRoomModels; ++i)
        {
            const float sp = spaceForRoomIndex (i);
            const double lvl = irLevel (kStyleModern, sp);
            char buf[200];
            std::snprintf (buf, sizeof (buf), "Modern  %5.2f s: level %+6.2f dB (Pro-R 2 %+6.2f dB, +-1.5 dB)", interpolateRoom (sp).t60, lvl, proModern[i]);
            check (std::abs (lvl - proModern[i]) < 1.5, buf);
        }
        for (int style : { kStyleVintage, kStylePlate })
        {
            for (int k = 0; k < 3; ++k)
            {
                const double lvl = irLevel (style, spaceForT60 (styleT60s[k]));
                const double want = proStyle[style == kStylePlate ? 1 : 0][k];
                char buf[200];
                std::snprintf (buf, sizeof (buf), "%-7s %5.2f s: level %+6.2f dB (Pro-R 2 %+6.2f dB, +-1.5 dB)", styleName (style), styleT60s[k], lvl, want);
                check (std::abs (lvl - want) < 1.5, buf);
            }
            for (float sp : { 0.0f, 0.5f, 1.0f })
            {
                const double lvl = irLevel (style, sp);
                char buf[160];
                std::snprintf (buf, sizeof (buf), "%-7s space %.1f: level %+6.2f dB (within -15 ... +5 dB)", styleName (style), sp, lvl);
                check (lvl > -15.0 && lvl < 5.0, buf);
            }
        }
    }

    //--------------------------------------------------------------------------
    std::printf ("\n== Extreme Decay Rate EQ boosts must not explode ==\n");
    for (int style = 0; style < 3; ++style)
    {
        EngineParams p = wetParams();
        p.style = style;
        p.space = 1.0f;
        p.decayRate = 4.0f;
        p.brightness = 1.0f;
        for (int k = 0; k < kNumDecayBands; ++k)
        {
            p.decayBands[k].used = true;
            p.decayBands[k].shape = k < 3 ? kDecayBell : (k == 3 ? kDecayLowShelf : kDecayHighShelf);
            p.decayBands[k].freq = 300.0f + 50.0f * (float) k;
            p.decayBands[k].rate = 4.0f;
            p.decayBands[k].q = 0.5f;
        }
        ReverbEngine e;
        e.setParams (p);
        e.prepare (fs, 256);
        Rng rng (11u);
        std::vector<float> l (256), r (256), outL;
        for (int b = 0; b < (int) (20.0 * fs / 256); ++b)
        {
            const double t = (double) b * 256 / fs;
            for (int i = 0; i < 256; ++i)
                l[(size_t) i] = r[(size_t) i] = t < 0.5 ? 0.3f * rng.bipolar() : 0.0f;
            e.process (l.data(), r.data(), 256);
            outL.insert (outL.end(), l.begin(), l.end());
        }
        const double a = rms (outL, (size_t) (1.0 * fs), (size_t) (2.0 * fs));
        const double b = rms (outL, (size_t) (19.0 * fs), (size_t) (20.0 * fs));
        char buf[160];
        std::snprintf (buf, sizeof (buf), "%s: 6 overlapping x4 bands at 400 %% / 10 s: level change after 18 s %.1f dB", styleName (style), 20.0 * std::log10 (b / a + 1e-20));
        check (std::isfinite (b) && b < a, buf);
    }

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
