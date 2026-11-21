// Behaviour checks: every statement of the FabFilter Pro-R 2 documentation about what a control
// does is turned into a measurement on Nachhall's engine. Where Pro-R 2 itself was measured
// (recordings in reference/pro-r, evaluated with NachhallReference), the expected numbers are the
// measured Pro-R 2 values; the file names of the recordings are given next to them.
// Usage: NachhallBehaviourTests
#include "TestHelpers.h"

#include <cmath>

using namespace nh;
using namespace testing;

namespace
{
constexpr double fs = 48000.0;

// the base setting of the Pro-R 2 recordings (reference/ANLEITUNG.md): Space "Hall" 1.85 s,
// Character 50 %, Distance 50 %, Thickness 0 %, Brightness 0 %, Width 100 %, Mix 100 %
EngineParams base()
{
    EngineParams p;
    p.mix = 1.0f;
    p.predelayMs = 0.0f;
    p.space = spaceForT60 (1.85f);
    p.distance = 0.5f;
    p.character = 0.5f;
    p.thickness = 0.5f;  // Pro-R scale 0 %
    p.brightness = 0.5f; // Pro-R scale 0 %
    p.width = 1.0f;
    return p;
}

std::string fmt (const char* f, double a, double b = 0, double c = 0, double d = 0)
{
    char buf[400];
    std::snprintf (buf, sizeof (buf), f, a, b, c, d);
    return buf;
}

void section (const char* quote)
{
    std::printf ("\n-- Pro-R 2: \"%s\"\n", quote);
}

// T30 like NachhallReference measures it (octave bands, from the start of the response)
double t60Mid (const Stereo& s)
{
    const auto m = s.mono();
    return 0.5 * (measureT60 (m, fs, 500.0, 0.0) + measureT60 (m, fs, 1000.0, 0.0));
}

float noiseSample (uint32_t i)
{
    uint32_t s = i * 747796405u + 2891336453u;
    s = ((s >> ((s >> 28u) + 4u)) ^ s) * 277803737u;
    s = (s >> 22u) ^ s;
    return (float) s / 4294967296.0f * 2.0f - 1.0f;
}
} // namespace

int main()
{
    enableFlushToZero();

    //--------------------------------------------------------------------------
    section ("Space: from 200 ms to ten seconds of decay");
    {
        // Pro-R 2 shows 0.20 s and 10.0 s at the ends of the range, its measured decay is longer:
        // 0.23 s and 10.8 s (T30, mean of the 500 Hz and 1 kHz octaves; recordings R01 and R06)
        EngineParams p = base();
        p.space = 0.0f;
        const double t0 = t60Mid (renderImpulse (p, fs, 1.5));
        p.space = 1.0f;
        const double t1 = t60Mid (renderImpulse (p, fs, 14.0));
        check (std::abs (t0 / 0.23 - 1.0) < 0.15 && std::abs (t1 / 10.8 - 1.0) < 0.1,
               fmt ("smallest space %.2f s (Pro-R 2: 0.23 s), largest space %.2f s (Pro-R 2: 10.8 s)", t0, t1));
        check (std::abs (interpolateRoom (0.0f).t60 - 0.2f) < 1.0e-4f && std::abs (interpolateRoom (1.0f).t60 - 10.0f) < 1.0e-4f,
               fmt ("displayed decay time %.2f s ... %.1f s", interpolateRoom (0.0f).t60, interpolateRoom (1.0f).t60));
    }

    section ("Decay Rate: 25 % to 400 % of the Space decay");
    {
        // Pro-R 2 measured (D_DecayRate_025 / _400 vs B_Basis_A, 500 Hz and 1 kHz octaves):
        // 25 % -> x0.228, 400 % -> x3.94 of the decay at 100 %
        EngineParams p = base();
        const double t100 = t60Mid (renderImpulse (p, fs, 5.0));
        p.decayRate = 0.25f;
        const double a = t60Mid (renderImpulse (p, fs, 1.5)) / t100;
        p.decayRate = 4.0f;
        const double b = t60Mid (renderImpulse (p, fs, 14.0)) / t100;
        check (std::abs (a / 0.228 - 1.0) < 0.12 && std::abs (b / 3.94 - 1.0) < 0.08,
               fmt ("Hall %.2f s: 25 %% -> x%.3f (Pro-R 2: x0.228), 400 %% -> x%.2f (Pro-R 2: x3.94)", t100, a, b));
    }

    //--------------------------------------------------------------------------
    section ("Brightness: lower values = more high frequency absorption");
    {
        // Pro-R 2 measured T30(8 kHz) / T30(1 kHz), octave bands: -100 % -> 0.45, 0 % -> 0.77,
        // +100 % -> 0.85 (H_Brightness_m100, B_Basis_A, H_Brightness_p100)
        double ratio[3];
        const float vals[3] = { 0.0f, 0.5f, 1.0f }; // Pro-R scale -100 / 0 / +100 %
        const double pro[3] = { 0.45, 0.77, 0.85 };
        bool near = true;
        for (int i = 0; i < 3; ++i)
        {
            EngineParams p = base();
            p.brightness = vals[i];
            const auto m = renderImpulse (p, fs, 4.0).mono();
            ratio[i] = measureT60 (m, fs, 8000.0, 0.0) / measureT60 (m, fs, 1000.0, 0.0);
            near = near && std::abs (ratio[i] - pro[i]) < 0.1;
        }
        check (ratio[0] < ratio[1] && ratio[1] < ratio[2] && near,
               fmt ("T30(8 kHz)/T30(1 kHz): -100 %% -> %.2f, 0 %% -> %.2f, +100 %% -> %.2f (Pro-R 2: 0.45 / 0.77 / 0.85)", ratio[0], ratio[1], ratio[2]));
    }

    //--------------------------------------------------------------------------
    section ("Character: 0 % transparent, 50 % modulation + reflections, 100 % chorus-like");
    {
        // Time variance: an impulse fired at two different moments must give the same response in a
        // static (unmodulated) reverb. Modulation makes the two responses differ.
        double variance[3], erRatio[3], spread[3] = { 0.0, 0.0, 0.0 }, width[3] = { 0.0, 0.0, 0.0 };
        const float vals[3] = { 0.0f, 0.5f, 1.0f };
        for (int i = 0; i < 3; ++i)
        {
            EngineParams p = base();
            p.space = 0.6f;
            p.character = vals[i];
            const int offset = (int) (0.37 * fs);
            const auto a = renderImpulse (p, fs, 1.2).mono();
            const auto b = render (p, fs, 1.2 + 0.37, [offset] (int n, float& l, float& r) { l = r = n == offset ? 1.0f : 0.0f; }).mono();
            double diff = 0.0, ref = 0.0;
            for (size_t k = (size_t) (0.1 * fs); k < (size_t) (1.1 * fs); ++k)
            {
                const double d = (double) a[k] - (double) b[k + (size_t) offset];
                diff += d * d;
                ref += (double) a[k] * a[k];
            }
            variance[i] = db (diff / ref);

            const auto ir = renderImpulse (p, fs, 0.6).mono();
            erRatio[i] = db (energy (ir, fs, 0.0, 0.04) / energy (ir, fs, 0.04, 0.3));

            if (i >= 1)
            {
                // chorus-like: a 1 kHz tone (1 ... 3 s, like signal B of the reference package) is
                // spread over many Hz in the tail (3.1 ... 7.9 s), measured like NachhallReference does.
                // The modulation wanders randomly, so the result is averaged over 4 starting points.
                for (int k = 0; k < 4; ++k)
                {
                    EngineParams q = base();
                    q.character = vals[i];
                    const double pre = 0.37 * k;
                    const auto s = render (q, fs, 7.9 + pre, [pre] (int n, float& l, float& r) {
                        const double t = (double) n / fs - 1.0 - pre;
                        const double fade = t < 0.0 || t > 2.0 ? 0.0 : std::min (1.0, std::min (t, 2.0 - t) / 0.05);
                        l = r = (float) (0.25 * (0.5 - 0.5 * std::cos (kPi * fade)) * std::sin (2.0 * kPi * 1000.0 * t));
                    });
                    size_t nfft = 0;
                    const auto ps = powerSpectrum (s.mono(), fs, 3.1 + pre, 7.9 + pre, nfft);
                    spread[i] += 0.25 * (1.0 - bandPower (ps, fs, nfft, 990.0, 1010.0) / std::max (bandPower (ps, fs, nfft, 500.0, 2000.0), 1.0e-30));
                    double num = 0.0, den = 0.0;
                    for (size_t b = 0; b < ps.size(); ++b)
                    {
                        const double f = (double) b * fs / (double) nfft;
                        if (f >= 900.0 && f <= 1100.0)
                        {
                            num += ps[b] * (f - 1000.0) * (f - 1000.0);
                            den += ps[b];
                        }
                    }
                    width[i] += 0.25 * (den > 0.0 ? std::sqrt (num / den) : 0.0);
                }
            }
        }
        check (variance[0] < -60.0 && variance[1] > -20.0,
               fmt ("time variance of the response: 0 %% -> %.0f dB (static, transparent), 50 %% -> %.1f dB (modulated)", variance[0], variance[1]));
        // Pro-R 2 measured (B_Basis_B, C_Character_100_B): tone width 2.3 Hz / 14.4 Hz, energy
        // outside +-10 Hz 1 % / 49 % at 50 % / 100 %
        check (width[1] < 5.0 && std::abs (width[2] - 14.4) < 3.0 && std::abs (spread[2] - 0.49) < 0.15,
               fmt ("1 kHz tone in the tail: width 50 %% -> %.1f Hz, 100 %% -> %.1f Hz (Pro-R 2: 2.3 / 14.4 Hz); outside +-10 Hz 50 %% -> %.0f %%, ",
                    width[1], width[2], spread[1] * 100.0) + fmt ("100 %% -> %.0f %% (Pro-R 2: 1 / 49 %%)", spread[2] * 100.0));
        check (erRatio[1] > erRatio[0] + 1.0,
               fmt ("early reflections vs tail: 0 %% -> %.1f dB, 50 %% -> %.1f dB, 100 %% -> %.1f dB", erRatio[0], erRatio[1], erRatio[2]));
    }

    //--------------------------------------------------------------------------
    section ("Distance: 0 % close with bright early reflections, higher = longer build-up, more diffuse");
    {
        double ts[3], c50[3], centroid[3];
        const float vals[3] = { 0.0f, 0.5f, 1.0f };
        for (int i = 0; i < 3; ++i)
        {
            EngineParams p = base();
            p.space = 0.55f;
            p.distance = vals[i];
            const auto ir = renderImpulse (p, fs, 2.0).mono();
            ts[i] = centreTime (ir, fs, 2.0);
            c50[i] = db (energy (ir, fs, 0.0, 0.05) / energy (ir, fs, 0.05, 2.0));
            centroid[i] = spectralCentroid (ir, fs, 0.0, 0.03);
        }
        check (ts[0] < ts[1] && ts[1] < ts[2] && ts[2] > ts[0] * 1.3,
               fmt ("centre time (later build-up): 0 %% -> %.0f ms, 50 %% -> %.0f ms, 100 %% -> %.0f ms", ts[0] * 1000, ts[1] * 1000, ts[2] * 1000));
        check (c50[0] > c50[1] && c50[1] > c50[2], fmt ("clarity C50: 0 %% -> %.1f dB, 50 %% -> %.1f dB, 100 %% -> %.1f dB", c50[0], c50[1], c50[2]));
        check (centroid[0] > centroid[2] * 1.2,
               fmt ("brightness of the first 30 ms (centroid): 0 %% -> %.0f Hz, 100 %% -> %.0f Hz", centroid[0], centroid[2]));
    }

    //--------------------------------------------------------------------------
    section ("Thickness: density + saturation; high = denser with non-linear dynamics, low = airy/sparse");
    {
        // Pro-R 2 measured on two noise bursts 40 dB apart (T_Thickness_m100_B, B_Basis_B,
        // T_Thickness_p100_B): 0 % linear, +100 % compresses 2:1 (-0.50 dB per dB), -100 % expands
        // (+0.45 dB per dB). The bursts here are 30 dB apart: expected -14.9 dB and +13.4 dB.
        double dens[3], nonlin[3];
        const float vals[3] = { 0.0f, 0.5f, 1.0f }; // Pro-R scale -100 / 0 / +100 %
        for (int i = 0; i < 3; ++i)
        {
            EngineParams p = base();
            p.space = 0.5f;
            p.distance = 0.5f;
            p.thickness = vals[i];
            const auto ir = renderImpulse (p, fs, 0.3).mono();
            dens[i] = echoDensity (ir, fs, 0.03, 0.07);

            double tail[2];
            const float amps[2] = { 0.0316f, 1.0f };
            for (int k = 0; k < 2; ++k)
            {
                const float a = amps[k];
                const auto s = render (p, fs, 1.4, [a] (int n, float& l, float& r) {
                    l = r = n < (int) (0.3 * fs) ? a * noiseSample ((uint32_t) n) : 0.0f;
                });
                tail[k] = db (energy (s.mono(), fs, 0.7, 1.3) / ((double) a * a));
            }
            nonlin[i] = tail[1] - tail[0];
        }
        check (dens[2] > dens[0] * 1.1, fmt ("echo density 30-70 ms: -100 %% -> %.2f, +100 %% -> %.2f", dens[0], dens[2]));
        check (std::abs (nonlin[1]) < 0.5 && std::abs (nonlin[2] + 14.9) < 2.5 && std::abs (nonlin[0] - 13.4) < 2.5,
               fmt ("loud vs quiet input (30 dB apart), tail level difference: -100 %% -> %+.1f dB (Pro-R 2: +13.4, expanding), 0 %% -> %+.2f dB (linear), "
                    "+100 %% -> %+.1f dB (Pro-R 2: -14.9, compressing)",
                    nonlin[0], nonlin[1], nonlin[2]));
    }

    //--------------------------------------------------------------------------
    section ("Stereo Width: 0 % mono, 50 % strong cross-feed, 100 % independent channels, up to 120 % amplified side");
    {
        // Pro-R 2 measured with a left-only impulse (W_Width_000 / _050 / _070 / _120): left -> right
        // level 50 % -2.7 dB, 70 % -6.9 dB, 120 % -8.9 dB; L/R correlation 0.1 ... 0.6 s after the
        // impulse 0 % 1.00, 50 % +0.46, 70 % -0.02, 120 % -0.52. There is no recording at 100 %.
        struct Point { float w; double cross, corr; };
        const Point pro[] = { { 0.5f, -2.7, 0.46 }, { 0.7f, -6.9, -0.02 }, { 1.2f, -8.9, -0.52 } };
        auto leftOnly = [] (float w, double& cross, double& corr)
        {
            EngineParams p = base();
            p.width = w;
            const auto s = renderImpulse (p, fs, 2.0, 1.0f, 0.0f);
            cross = db (energy (s.r, fs, 0.0, 2.0) / std::max (1.0e-30, energy (s.l, fs, 0.0, 2.0)));
            corr = correlation (s.l, s.r, fs, 0.1, 0.6);
        };
        bool near = true;
        std::string txt;
        for (const auto& pt : pro)
        {
            double cross, corr;
            leftOnly (pt.w, cross, corr);
            near = near && std::abs (cross - pt.cross) < 1.5 && std::abs (corr - pt.corr) < 0.12;
            txt += fmt ("%.0f %%: %+.1f dB / %+.2f ", 100.0 * pt.w, cross, corr) + fmt ("(Pro-R 2: %+.1f dB / %+.2f)  ", pt.cross, pt.corr);
        }
        double c0, x0, c100, x100;
        leftOnly (0.0f, x0, c0);
        leftOnly (1.0f, x100, c100);
        check (near, "left input -> right output level / L/R correlation: " + txt);
        check (c0 > 0.99 && x100 < -60.0, fmt ("0 %% -> correlation %.2f (mono), 100 %% -> left to right %.0f dB (independent)", c0, std::max (-120.0, x100)));

        auto sideToMid = [] (float w)
        {
            EngineParams p = base();
            p.width = w;
            const auto s = renderImpulse (p, fs, 1.5);
            std::vector<float> mid (s.l.size()), side (s.l.size());
            for (size_t i = 0; i < s.l.size(); ++i)
            {
                mid[i] = 0.5f * (s.l[i] + s.r[i]);
                side[i] = 0.5f * (s.l[i] - s.r[i]);
            }
            return energy (side, fs, 0.05, 1.0) / energy (mid, fs, 0.05, 1.0);
        };
        const double sm100 = sideToMid (1.0f), sm120 = sideToMid (1.2f);
        check (sm120 > sm100 * 1.3, fmt ("side/mid energy: 100 %% -> %.2f, 120 %% -> %.2f", sm100, sm120));
    }

    //--------------------------------------------------------------------------
    section ("Predelay: delay between the original sound and the start of the reverb");
    {
        EngineParams p = base();
        p.predelayMs = 100.0f;
        p.distance = 0.0f;
        const auto ir = renderImpulse (p, fs, 0.6).mono();
        float mx = 0.0f;
        for (float v : ir)
            mx = std::max (mx, std::abs (v));
        size_t onset = 0;
        while (onset < ir.size() && std::abs (ir[onset]) < 1.0e-3f * mx)
            ++onset;
        const double ms = 1000.0 * (double) onset / fs;
        check (ms >= 100.0 && ms < 125.0, fmt ("predelay 100 ms -> reverb starts after %.1f ms", ms));
    }

    //--------------------------------------------------------------------------
    section ("Auto Gate: cuts the reverb after the input stops; hold time adjustable");
    {
        auto cutTime = [] (float holdMs, bool gate = true)
        {
            EngineParams p = base();
            p.space = spaceForT60 (2.1f);
            p.gateOn = gate;
            p.gateHoldMs = holdMs;
            const auto s = render (p, fs, 5.0, [] (int n, float& l, float& r) {
                l = r = (n > (int) (0.5 * fs) && n < (int) (1.5 * fs)) ? 0.3f * noiseSample ((uint32_t) n) : 0.0f;
            });
            const auto m = s.mono();
            const double ref = rms (m, (size_t) (1.2 * fs), (size_t) (1.5 * fs));
            for (double t = 1.5; t < 4.9; t += 0.005)
                if (rms (m, (size_t) (t * fs), (size_t) ((t + 0.01) * fs)) < ref * 0.003)
                    return t - 1.5;
            return 3.4;
        };
        // time until the output is 50 dB down after the input stops
        const double a = cutTime (100.0f), b = cutTime (600.0f), natural = cutTime (100.0f, false);
        check (a < 0.35 * natural && b > a + 0.3,
               fmt ("tail 50 dB down after the input stops: hold 100 ms -> %.2f s, hold 600 ms -> %.2f s, without gate %.2f s", a, b, natural));
    }

    //--------------------------------------------------------------------------
    section ("Freeze: 'eternal' decay; Distance and Thickness no longer affect the sound; Character, Brightness still work");
    {
        // change: 0 = none (reference), 1 = Distance + Thickness to 100 %, 2 = Character to 100 %, 3 = Brightness to 0 %
        auto frozen = [] (int change, double seconds)
        {
            EngineParams p = base();
            p.space = 0.5f;
            return render (p, fs, seconds,
                           [] (int n, float& l, float& r) { l = r = n < (int) fs ? 0.3f * noiseSample ((uint32_t) n) : 0.0f; },
                           [change] (int pos, EngineParams& q) {
                               q.freeze = pos >= (int) (1.0 * fs);
                               if (pos >= (int) (3.0 * fs))
                               {
                                   if (change == 1)
                                   {
                                       q.distance = 1.0f;
                                       q.thickness = 1.0f;
                                   }
                                   else if (change == 2)
                                       q.character = 1.0f;
                                   else if (change == 3)
                                       q.brightness = 0.0f;
                               }
                           });
        };
        // difference to the unchanged frozen tail, relative to its level
        auto differenceDb = [] (const std::vector<float>& a, const std::vector<float>& ref)
        {
            double d = 0.0, r = 0.0;
            for (size_t i = (size_t) (4.0 * fs); i < (size_t) (5.9 * fs); ++i)
            {
                const double e = (double) a[i] - ref[i];
                d += e * e;
                r += (double) ref[i] * ref[i];
            }
            return db (d / std::max (r, 1.0e-30));
        };

        const auto ref = frozen (0, 32.0).mono();
        const double lvl = 20.0 * std::log10 (rms (ref, (size_t) (31.0 * fs), (size_t) (31.9 * fs)) / rms (ref, (size_t) (2.0 * fs), (size_t) (2.9 * fs)));
        const auto ref8k = bandpass (ref, fs, 8000.0, 1.414);
        const double hf = 20.0 * std::log10 (rms (ref8k, (size_t) (31.0 * fs), (size_t) (31.9 * fs)) / rms (ref8k, (size_t) (2.0 * fs), (size_t) (2.9 * fs)));
        check (std::abs (lvl) < 1.0 && std::abs (hf) < 1.0, fmt ("frozen for 30 s: level %+.2f dB, 8 kHz %+.2f dB (holds, no darkening)", lvl, hf));

        const double dt = differenceDb (frozen (1, 6.0).mono(), ref);
        const double ch = differenceDb (frozen (2, 6.0).mono(), ref);
        check (dt < -100.0 && ch > -20.0,
               fmt ("frozen, change vs unchanged tail: Distance + Thickness to 100 %% -> %.0f dB (no effect), Character to 100 %% -> %+.1f dB (still works)", dt, ch));

        const auto b = bandpass (frozen (3, 6.0).mono(), fs, 8000.0, 1.414);
        const double hb = rms (b, (size_t) (2.0 * fs), (size_t) (2.9 * fs));
        const double ha = rms (b, (size_t) (4.0 * fs), (size_t) (4.9 * fs));
        check (20.0 * std::log10 (ha / hb) < -3.0, fmt ("frozen, Brightness to 0 %%: 8 kHz level %+.1f dB (still works)", 20.0 * std::log10 (ha / hb)));
    }

    //--------------------------------------------------------------------------
    section ("Decay Rate EQ: changes how long frequencies decay (shelves, bells, notch), not their level");
    {
        // Pro-R 2 measured with Q 1 (X_DecayEQ_HighShelf / _LowShelf vs B_Basis_A, octave bands):
        // high shelf 6 kHz 200 % -> 8 kHz x1.40, 16 kHz x1.62; low shelf 200 Hz 50 % -> 125 Hz x0.58,
        // 250 Hz x0.82. The time-based high frequency loss keeps the high decay below x2.
        EngineParams p = base();
        const auto baseIr = renderImpulse (p, fs, 6.0).mono();
        EngineParams hs = p;
        hs.decayBands[0].used = true;
        hs.decayBands[0].shape = kDecayHighShelf;
        hs.decayBands[0].freq = 6000.0f;
        hs.decayBands[0].rate = 2.0f;
        hs.decayBands[0].q = 1.0f;
        const auto hsIr = renderImpulse (hs, fs, 6.0).mono();
        const double r8k = measureT60 (hsIr, fs, 8000.0, 0.0) / measureT60 (baseIr, fs, 8000.0, 0.0);
        const double r16k = measureT60 (hsIr, fs, 16000.0, 0.0) / measureT60 (baseIr, fs, 16000.0, 0.0);
        EngineParams ls = p;
        ls.decayBands[0].used = true;
        ls.decayBands[0].shape = kDecayLowShelf;
        ls.decayBands[0].freq = 200.0f;
        ls.decayBands[0].rate = 0.5f;
        ls.decayBands[0].q = 1.0f;
        const auto lsIr = renderImpulse (ls, fs, 6.0).mono();
        const double r125 = measureT60 (lsIr, fs, 125.0, 0.0) / measureT60 (baseIr, fs, 125.0, 0.0);
        const double r250 = measureT60 (lsIr, fs, 250.0, 0.0) / measureT60 (baseIr, fs, 250.0, 0.0);
        check (std::abs (r8k / 1.40 - 1.0) < 0.2 && std::abs (r16k / 1.62 - 1.0) < 0.2,
               fmt ("high shelf 6 kHz x2 -> decay at 8 kHz x%.2f (Pro-R 2: x1.40), 16 kHz x%.2f (Pro-R 2: x1.62)", r8k, r16k));
        check (std::abs (r125 / 0.58 - 1.0) < 0.2 && std::abs (r250 / 0.82 - 1.0) < 0.15,
               fmt ("low shelf 200 Hz x0.5 -> decay at 125 Hz x%.2f (Pro-R 2: x0.58), 250 Hz x%.2f (Pro-R 2: x0.82)", r125, r250));
        const auto b0 = bandpass (baseIr, fs, 10000.0, 1.414);
        const auto b1 = bandpass (hsIr, fs, 10000.0, 1.414);
        const double early = db (energy (b1, fs, 0.0, 0.05) / energy (b0, fs, 0.0, 0.05));
        check (std::abs (early) < 1.5, fmt ("level of the first 50 ms at 10 kHz changes by only %+.2f dB", early));
    }

    //--------------------------------------------------------------------------
    section ("Post EQ: 6 bands, cuts up to 96 dB/oct, L/R/M/S placement, automatic gain adjustment");
    {
        EngineParams p = base();
        p.eqAutoGain = false;
        const auto baseIr = renderImpulse (p, fs, 2.0);
        auto bandLevel = [] (const std::vector<float>& x, double f, double q)
        {
            return energy (bandpass (x, fs, f, q), fs, 0.0, 2.0);
        };
        EngineParams bell = p;
        bell.postBands[0].used = true;
        bell.postBands[0].shape = kPostBell;
        bell.postBands[0].freq = 1000.0f;
        bell.postBands[0].gain = 6.0f;
        bell.postBands[0].q = 1.0f;
        const double g = db (bandLevel (renderImpulse (bell, fs, 2.0).mono(), 1000.0, 8.0) / bandLevel (baseIr.mono(), 1000.0, 8.0));
        check (std::abs (g - 6.0) < 1.0, fmt ("bell +6 dB at 1 kHz -> measured %+.2f dB", g));

        EngineParams cut = p;
        cut.postBands[0].used = true;
        cut.postBands[0].shape = kPostLowCut;
        cut.postBands[0].freq = 400.0f;
        cut.postBands[0].slope = 8; // 96 dB/oct
        size_t n0 = 0, n1 = 0;
        const auto p0 = powerSpectrum (baseIr.mono(), fs, 0.0, 2.0, n0);
        const auto p1 = powerSpectrum (renderImpulse (cut, fs, 2.0).mono(), fs, 0.0, 2.0, n1);
        const double c = db (bandPower (p1, fs, n1, 90.0, 110.0) / bandPower (p0, fs, n0, 90.0, 110.0));
        const double c2 = db (bandPower (p1, fs, n1, 190.0, 210.0) / bandPower (p0, fs, n0, 190.0, 210.0));
        check (c < -100.0 && c2 < -60.0 && c2 > -110.0,
               fmt ("low cut 400 Hz, 96 dB/oct -> 200 Hz: %+.0f dB (1 oct.), 100 Hz: %+.0f dB (2 oct.)", c2, c));

        auto broadband = [] (const Stereo& s) { return energy (s.mono(), fs, 0.0, 2.0); };
        EngineParams boost = p;
        boost.postBands[0].used = true;
        boost.postBands[0].shape = kPostBell;
        boost.postBands[0].freq = 2000.0f;
        boost.postBands[0].gain = 12.0f;
        boost.postBands[0].q = 0.7f;
        const double off = db (broadband (renderImpulse (boost, fs, 2.0)) / broadband (baseIr));
        boost.eqAutoGain = true;
        EngineParams baseAuto = p;
        baseAuto.eqAutoGain = true;
        const double on = db (broadband (renderImpulse (boost, fs, 2.0)) / broadband (renderImpulse (baseAuto, fs, 2.0)));
        check (off > 3.0 && std::abs (on) < std::abs (off) * 0.5,
               fmt ("+12 dB bell at 2 kHz: overall level %+.1f dB without, %+.1f dB with auto gain", off, on));

        EngineParams side = p;
        side.postBands[0].used = true;
        side.postBands[0].shape = kPostLowShelf;
        side.postBands[0].freq = 25000.0f;
        side.postBands[0].gain = -20.0f;
        side.postBands[0].q = 0.7f;
        side.postBands[0].placement = kPlaceSide;
        const auto sideIr = renderImpulse (side, fs, 2.0);
        const double c0 = correlation (baseIr.l, baseIr.r, fs, 0.05, 1.5);
        const double c1 = correlation (sideIr.l, sideIr.r, fs, 0.05, 1.5);
        check (c1 > 0.9 && c1 > c0 + 0.5, fmt ("side band -20 dB -> L/R correlation %.2f -> %.2f (mid untouched)", c0, c1));
    }

    //--------------------------------------------------------------------------
    section ("Mix: final dry/wet balance; 100 % for send use");
    {
        EngineParams p = base();
        p.mix = 0.0f;
        double maxDiff = 0.0;
        {
            const auto s = render (p, fs, 0.5, [] (int n, float& l, float& r) {
                l = 0.3f * noiseSample ((uint32_t) n);
                r = 0.3f * noiseSample ((uint32_t) n + 7u);
            });
            for (size_t i = 0; i < s.l.size(); ++i)
            {
                maxDiff = std::max (maxDiff, (double) std::abs (s.l[i] - 0.3f * noiseSample ((uint32_t) i)));
                maxDiff = std::max (maxDiff, (double) std::abs (s.r[i] - 0.3f * noiseSample ((uint32_t) i + 7u)));
            }
        }
        p.mix = 1.0f;
        const auto w = renderImpulse (p, fs, 0.1);
        check (maxDiff < 1.0e-5 && std::abs (w.l[0]) < 1.0e-6,
               fmt ("mix 0 %%: output = input (max. difference %.1e), mix 100 %%: no dry signal (%.1e)", maxDiff, std::abs (w.l[0])));
        EngineParams h = base();
        h.mix = 0.5f;
        const auto half = renderImpulse (h, fs, 0.05);
        check (std::abs (half.l[0] - std::cos (kPiF * 0.25f)) < 1.0e-3, fmt ("zero latency: dry impulse appears at sample 0 (%.3f)", half.l[0]));
    }

    //--------------------------------------------------------------------------
    section ("Styles: Vintage = classic digital, Plate = fast build-up, dense");
    {
        auto hfRatio = [] (int style)
        {
            EngineParams p = base();
            p.style = style;
            size_t n = 0;
            const auto ps = powerSpectrum (renderImpulse (p, fs, 1.2).mono(), fs, 0.05, 1.0, n);
            return db (bandPower (ps, fs, n, 12000.0, 20000.0) / bandPower (ps, fs, n, 1000.0, 4000.0));
        };
        // Pro-R 2 measured the same way (B_Basis_A, S_Vintage_Basis, left impulse): Modern -16.8 dB,
        // Vintage -12.4 dB - the Vintage tail is brighter at the top, not band-limited
        const double m = hfRatio (kStyleModern), v = hfRatio (kStyleVintage);
        check (v > m && v < m + 9.0, fmt ("energy above 12 kHz vs 1-4 kHz in the tail: Modern %.1f dB, Vintage %.1f dB (Pro-R 2: -16.8 / -12.4 dB)", m, v));
        auto earlyDensity = [] (int style)
        {
            EngineParams p = base();
            p.style = style;
            p.distance = 0.0f;
            return echoDensity (renderImpulse (p, fs, 0.2).mono(), fs, 0.004, 0.02);
        };
        const double dm = earlyDensity (kStyleModern), dp = earlyDensity (kStylePlate);
        check (dp > dm, fmt ("echo density 4-20 ms: Modern %.2f, Plate %.2f", dm, dp));
    }

    //--------------------------------------------------------------------------
    section ("Stereo patches hold up well in mono (Sound On Sound review)");
    {
        const auto s = renderImpulse (base(), fs, 2.0);
        std::vector<float> m (s.l.size());
        for (size_t i = 0; i < m.size(); ++i)
            m[i] = 0.5f * (s.l[i] + s.r[i]);
        const double ratio = db (energy (m, fs, 0.0, 2.0) / (0.5 * (energy (s.l, fs, 0.0, 2.0) + energy (s.r, fs, 0.0, 2.0))));
        check (ratio > -4.5 && ratio < -1.5, fmt ("mono sum vs stereo energy: %.2f dB (decorrelated = -3 dB, no cancellation)", ratio));
    }

    const int f = failureCount();
    std::printf ("\n%s (%d failure%s)\n", f == 0 ? "ALL BEHAVIOUR CHECKS PASSED" : "BEHAVIOUR CHECKS FAILED", f, f == 1 ? "" : "s");
    return f == 0 ? 0 : 1;
}
