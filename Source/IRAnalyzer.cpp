#include "IRAnalyzer.h"
#include "dsp/EarlyReflections.h"

#include "dsp/ReverbEngine.h"

namespace
{
constexpr int kNumOct = 9;
constexpr double kOct[kNumOct] = { 63.0, 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0, 16000.0 };

struct BandResult
{
    double t60 = 0.0;    // seconds (0 = not measurable)
    double energy = 0.0; // total band energy
};

BandResult analyseBand (const std::vector<float>& x, double fs, double centre, size_t onset)
{
    BandResult res;
    if (centre > fs * 0.45)
        return res;

    std::vector<double> e (x.size() - onset);
    const auto bp = nh::makeSvf (nh::FilterShape::BandPass, centre, 1.414, 0.0, fs);
    nh::Svf a, b;
    for (size_t i = onset; i < x.size(); ++i)
    {
        const float y = b.process (a.process (x[i], bp), bp);
        e[i - onset] = (double) y * y;
    }

    // noise floor: energy of the last 10 %
    const size_t n = e.size();
    const size_t tailStart = n - n / 10;
    double noise = 0.0;
    for (size_t i = tailStart; i < n; ++i)
        noise += e[i];
    noise /= (double) std::max<size_t> (1, n - tailStart);

    // smoothed envelope (10 ms) to find where the decay hits the noise floor
    const size_t win = std::max<size_t> (1, (size_t) (0.01 * fs));
    size_t end = n;
    {
        double acc = 0.0;
        for (size_t i = 0; i < n; ++i)
        {
            acc += e[i];
            if (i >= win)
                acc -= e[i - win];
            const double env = acc / (double) win;
            if (i > (size_t) (0.05 * fs) && env < noise * 2.0)
            {
                end = i;
                break;
            }
        }
    }
    if (end < (size_t) (0.02 * fs))
        return res;

    std::vector<double> edc (end);
    double acc = 0.0;
    for (size_t i = end; i-- > 0;)
    {
        acc += std::max (0.0, e[i] - noise);
        edc[i] = acc;
    }
    res.energy = acc;
    if (acc <= 0.0)
        return res;

    // linear regression on the EDC between -5 and -25 dB (or to the lowest usable level)
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    int count = 0;
    double lowest = 0.0;
    for (size_t i = 0; i < end; ++i)
    {
        const double db = 10.0 * std::log10 (edc[i] / acc + 1.0e-30);
        lowest = std::min (lowest, db);
        if (db <= -5.0 && db >= -25.0)
        {
            const double t = (double) i / fs;
            sx += t;
            sy += db;
            sxx += t * t;
            sxy += t * db;
            ++count;
        }
    }
    if (count < 16)
        return res;
    const double slope = (count * sxy - sx * sy) / std::max (1.0e-30, count * sxx - sx * sx); // dB per second
    if (slope >= -1.0)
        return res;
    res.t60 = -60.0 / slope;
    return res;
}

} // namespace

IRAnalysisResult analyseImpulseResponse (const juce::AudioBuffer<float>& ir, double fs)
{
    IRAnalysisResult result;
    const int numSamples = ir.getNumSamples();
    const int numCh = ir.getNumChannels();
    if (numSamples < (int) (0.05 * fs) || numCh < 1)
    {
        result.message = "The file is too short to be an impulse response.";
        return result;
    }

    std::vector<float> mono ((size_t) numSamples, 0.0f);
    for (int c = 0; c < numCh; ++c)
        for (int i = 0; i < numSamples; ++i)
            mono[(size_t) i] += ir.getSample (c, i) / (float) numCh;

    float peak = 0.0f;
    size_t peakPos = 0;
    for (size_t i = 0; i < mono.size(); ++i)
        if (std::abs (mono[i]) > peak)
        {
            peak = std::abs (mono[i]);
            peakPos = i;
        }
    if (peak < 1.0e-5f)
    {
        result.message = "The file is silent.";
        return result;
    }

    // Sine sweeps / non-IR material: the energy does not decay from the start
    {
        const size_t q = mono.size() / 4;
        double e1 = 0.0, e4 = 0.0;
        for (size_t i = 0; i < q; ++i)
            e1 += (double) mono[i] * mono[i];
        for (size_t i = 3 * q; i < mono.size(); ++i)
            e4 += (double) mono[i] * mono[i];
        if ((double) peakPos > 0.2 * (double) mono.size() || e4 > e1 * 0.5)
        {
            result.message = "This file does not look like an impulse response (sine sweeps cannot be imported).";
            return result;
        }
    }

    // onset = first sample above -20 dB relative to the peak
    size_t onset = 0;
    for (size_t i = 0; i < mono.size(); ++i)
        if (std::abs (mono[i]) > 0.1f * peak)
        {
            onset = i;
            break;
        }

    // predelay: gap between the direct sound and the start of the reflections
    double predelayMs = 0.0;
    {
        const size_t skip = onset + (size_t) (0.0025 * fs);
        const size_t win = std::max<size_t> (1, (size_t) (0.002 * fs));
        double maxEnv = 0.0;
        std::vector<double> env;
        double acc = 0.0;
        for (size_t i = skip; i < std::min (mono.size(), skip + (size_t) (0.3 * fs)); ++i)
        {
            acc += (double) mono[i] * mono[i];
            if (i >= skip + win)
                acc -= (double) mono[i - win] * mono[i - win];
            env.push_back (acc);
            maxEnv = std::max (maxEnv, acc);
        }
        for (size_t i = 0; i < env.size(); ++i)
            if (env[i] > maxEnv * 0.01)
            {
                predelayMs = juce::jlimit (0.0, 150.0, 1000.0 * (double) i / fs);
                break;
            }
    }

    // octave band decay times + energies
    BandResult bands[kNumOct];
    for (int b = 0; b < kNumOct; ++b)
        bands[b] = analyseBand (mono, fs, kOct[b], onset);

    double tMid = 0.0;
    {
        int c = 0;
        for (int b : { 3, 4 })
            if (bands[b].t60 > 0.0)
            {
                tMid += bands[b].t60;
                ++c;
            }
        if (c == 0)
        {
            result.message = "Could not measure a decay in this file.";
            return result;
        }
        tMid /= c;
    }
    // fill gaps with neighbours
    for (int b = 0; b < kNumOct; ++b)
        if (bands[b].t60 <= 0.0)
            bands[b].t60 = tMid;

    // ---- main settings
    float space = 0.5f, decayRatePct = 100.0f;
    const double tMin = nh::interpolateRoom (0.0f).actualT60(), tMax = nh::interpolateRoom (1.0f).actualT60();
    if (tMid < tMin)
    {
        space = 0.0f;
        decayRatePct = (float) juce::jlimit (25.0, 100.0, tMid / tMin * 100.0);
    }
    else if (tMid > tMax)
    {
        space = 1.0f;
        decayRatePct = (float) juce::jlimit (100.0, 400.0, tMid / tMax * 100.0);
    }
    else
        space = nh::spaceForActualT60 ((float) tMid);

    // the early reflections of the room model already start 6.7 ms after the direct sound
    predelayMs = std::max (0.0, predelayMs - 1000.0 * (double) nh::EarlyReflections::kModernOnsetSeconds);

    // clarity C50 -> distance
    double early = 0.0, late = 0.0;
    for (size_t i = onset; i < mono.size(); ++i)
    {
        const double e = (double) mono[i] * mono[i];
        if (i < onset + (size_t) (0.05 * fs))
            early += e;
        else
            late += e;
    }
    const double c50 = 10.0 * std::log10 ((early + 1.0e-20) / (late + 1.0e-20));
    const float distance = (float) juce::jlimit (5.0, 90.0, 45.0 - c50 * 4.0);

    // stereo width from the tail correlation
    float width = 70.0f;
    if (numCh >= 2)
    {
        double sl = 0, sr = 0, slr = 0;
        const int from = (int) onset + (int) (0.05 * fs);
        for (int i = from; i < numSamples; ++i)
        {
            const double l = ir.getSample (0, i), r = ir.getSample (1, i);
            sl += l * l;
            sr += r * r;
            slr += l * r;
        }
        const double corr = slr / std::sqrt (sl * sr + 1.0e-30);
        width = corr > 0.95 ? 0.0f : (float) juce::jlimit (30.0, 100.0, 50.0 + 50.0 * (1.0 - corr));
    }
    else
        width = 50.0f;

    auto& v = result.values;
    v.push_back ({ "style", 0.0f });
    v.push_back ({ "space", space });
    v.push_back ({ "decayRate", decayRatePct });
    v.push_back ({ "predelaySync", 0.0f });
    v.push_back ({ "predelay", (float) predelayMs });
    v.push_back ({ "brightness", 0.0f });
    v.push_back ({ "character", 30.0f });
    v.push_back ({ "thickness", 0.0f });
    v.push_back ({ "distance", distance });
    v.push_back ({ "width", width });
    v.push_back ({ "freeze", 0.0f });

    // ---- Decay Rate EQ: match the measured octave band decay times
    nh::DecayBandParams db[nh::kNumDecayBands];
    struct Slot
    {
        int shape;
        float freq;
        float q;
        std::vector<int> octs;
    };
    const Slot slots[5] = { { nh::kDecayLowShelf, 150.0f, 0.7f, { 0, 1 } },
                            { nh::kDecayBell, 400.0f, 1.0f, { 2, 3 } },
                            { nh::kDecayBell, 2000.0f, 1.0f, { 5 } },
                            { nh::kDecayBell, 4500.0f, 1.2f, { 6 } },
                            { nh::kDecayHighShelf, 9000.0f, 0.7f, { 7, 8 } } };
    for (int s = 0; s < 5; ++s)
    {
        db[s].used = true;
        db[s].enabled = true;
        db[s].shape = slots[s].shape;
        db[s].freq = slots[s].freq;
        db[s].q = slots[s].q;
        db[s].rate = 1.0f;
    }

    const float decayRate = decayRatePct * 0.01f;
    const double refDelay = nh::ReverbEngine::referenceDelayFor (nh::kStyleModern, space);
    float freqs[kNumOct];
    for (int b = 0; b < kNumOct; ++b)
        freqs[b] = (float) std::min (kOct[b], fs * 0.45);

    for (int iter = 0; iter < 6; ++iter)
    {
        const auto ds = nh::ReverbEngine::makeDecaySettings (db, space, decayRate, 0.5f, nh::kStyleModern);
        const auto spec = nh::buildDecaySpec (ds, fs, refDelay);
        float t60[kNumOct];
        nh::decayTimeCurve (spec, fs, refDelay, freqs, t60, kNumOct);
        for (int s = 0; s < 5; ++s)
        {
            double target = 0.0, actual = 0.0;
            for (int o : slots[s].octs)
            {
                target += std::log (bands[o].t60);
                actual += std::log (std::max (0.01f, t60[o]));
            }
            target /= (double) slots[s].octs.size();
            actual /= (double) slots[s].octs.size();
            const double corr = std::exp (0.8 * (target - actual));
            db[s].rate = (float) juce::jlimit (0.25, 4.0, db[s].rate * corr);
        }
    }

    for (int s = 0; s < nh::kNumDecayBands; ++s)
    {
        const juce::String p = "d" + juce::String (s + 1);
        const bool use = s < 5 && std::abs (std::log (db[s].rate)) > 0.08;
        v.push_back ({ p + "Used", use ? 1.0f : 0.0f });
        v.push_back ({ p + "On", 1.0f });
        if (use)
        {
            v.push_back ({ p + "Shape", (float) db[s].shape });
            v.push_back ({ p + "Freq", db[s].freq });
            v.push_back ({ p + "Rate", db[s].rate * 100.0f });
            v.push_back ({ p + "Q", db[s].q });
        }
    }

    // ---- Post EQ: compare the spectral balance of the IR with Nachhall's own response for these settings
    // (analysis by synthesis: render an impulse through the engine and analyse it the same way)
    double level[kNumOct];
    {
        nh::EngineParams ep;
        ep.mix = 1.0f;
        ep.style = nh::kStyleModern;
        ep.space = space;
        ep.decayRate = decayRate;
        ep.predelayMs = 0.0f;
        ep.distance = distance * 0.01f;
        ep.character = 0.3f;
        ep.thickness = 0.3f; // below the saturation threshold: linear reference
        ep.brightness = 0.5f;
        ep.width = width * 0.01f;
        for (int b = 0; b < nh::kNumDecayBands; ++b)
            ep.decayBands[b] = db[b];

        nh::ReverbEngine eng;
        eng.setParams (ep);
        eng.prepare (fs, 512);
        std::vector<float> l (512, 0.0f), r (512, 0.0f);
        for (int i = 0; i < (int) (0.4 * fs / 512); ++i)
        {
            std::fill (l.begin(), l.end(), 0.0f);
            std::fill (r.begin(), r.end(), 0.0f);
            eng.process (l.data(), r.data(), 512);
        }
        const int len = (int) (std::min (8.0, tMid * 1.3 + 0.5) * fs);
        std::vector<float> ref ((size_t) len, 0.0f);
        for (int pos = 0; pos < len; pos += 512)
        {
            const int n = std::min (512, len - pos);
            std::fill (l.begin(), l.end(), 0.0f);
            std::fill (r.begin(), r.end(), 0.0f);
            if (pos == 0)
                l[0] = r[0] = 1.0f;
            eng.process (l.data(), r.data(), n);
            for (int i = 0; i < n; ++i)
                ref[(size_t) (pos + i)] = 0.5f * (l[(size_t) i] + r[(size_t) i]);
        }

        // short tail of silence so that the noise floor estimate of analyseBand stays at zero
        ref.resize (ref.size() + (size_t) (0.2 * fs), 0.0f);
        for (int b = 0; b < kNumOct; ++b)
        {
            const auto rb = analyseBand (ref, fs, kOct[b], 0);
            if (bands[b].energy > 0.0 && rb.energy > 0.0)
                level[b] = 10.0 * std::log10 (bands[b].energy / rb.energy);
            else
                level[b] = -200.0;
        }
    }
    double ref = 0.0;
    int refCount = 0;
    for (int b = 3; b <= 5; ++b)
        if (level[b] > -150.0)
        {
            ref += level[b];
            ++refCount;
        }
    ref = refCount > 0 ? ref / refCount : 0.0;

    struct PostSlot
    {
        int shape;
        float freq;
        float q;
        std::vector<int> octs;
    };
    const PostSlot pslots[4] = { { nh::kPostLowShelf, 110.0f, 0.7f, { 0, 1 } },
                                 { nh::kPostBell, 350.0f, 0.8f, { 2, 3 } },
                                 { nh::kPostBell, 2800.0f, 0.8f, { 5, 6 } },
                                 { nh::kPostHighShelf, 8000.0f, 0.7f, { 7, 8 } } };
    int used = 0;
    for (int s = 0; s < 4; ++s)
    {
        double g = 0.0;
        int c = 0;
        for (int o : pslots[s].octs)
            if (level[o] > -150.0)
            {
                g += level[o] - ref;
                ++c;
            }
        if (c == 0)
            continue;
        g = juce::jlimit (-24.0, 12.0, g / c);
        if (std::abs (g) < 1.0)
            continue;
        const juce::String p = "p" + juce::String (++used);
        int shape = pslots[s].shape;
        float freq = pslots[s].freq;
        // very little energy at the extremes -> a cut filter matches better than a shelf
        if (g < -15.0 && shape == nh::kPostLowShelf)
        {
            shape = nh::kPostLowCut;
            freq = 90.0f;
        }
        else if (g < -15.0 && shape == nh::kPostHighShelf)
        {
            shape = nh::kPostHighCut;
            freq = 7000.0f;
        }
        v.push_back ({ p + "Used", 1.0f });
        v.push_back ({ p + "On", 1.0f });
        v.push_back ({ p + "Shape", (float) shape });
        v.push_back ({ p + "Freq", freq });
        v.push_back ({ p + "Gain", (float) juce::jlimit (-30.0, 30.0, g * 0.8) });
        v.push_back ({ p + "Q", pslots[s].q });
        v.push_back ({ p + "Slope", 1.0f });
        v.push_back ({ p + "Place", 0.0f });
    }
    for (int s = used; s < nh::kNumPostBands; ++s)
        v.push_back ({ "p" + juce::String (s + 1) + "Used", 0.0f });

    result.ok = true;
    result.message = "Imported: decay " + juce::String (tMid, 2) + " s, predelay " + juce::String (predelayMs, 1) + " ms";
    return result;
}
