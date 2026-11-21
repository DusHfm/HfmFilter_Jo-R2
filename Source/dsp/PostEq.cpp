#include "PostEq.h"

namespace nh
{
namespace
{
    inline bool differs (float a, float b) noexcept { return a < b || b < a; }
} // namespace

PostBandDesign designPostBand (const PostBandParams& p, double fs) noexcept
{
    PostBandDesign d;
    const double f = clampv ((double) p.freq, 10.0, 30000.0);
    const double q = clampv ((double) p.q, 0.025, 40.0);

    switch (p.shape)
    {
        case kPostBell:
            d.numSections = 1;
            d.sections[0] = makeSvf (FilterShape::Bell, f, q, clampv ((double) p.gain, -30.0, 30.0), fs);
            break;
        case kPostLowShelf:
            d.numSections = 1;
            d.sections[0] = makeSvf (FilterShape::LowShelf, f, clampv (q, 0.1, 4.0), clampv ((double) p.gain, -30.0, 30.0), fs);
            break;
        case kPostHighShelf:
            d.numSections = 1;
            d.sections[0] = makeSvf (FilterShape::HighShelf, f, clampv (q, 0.1, 4.0), clampv ((double) p.gain, -30.0, 30.0), fs);
            break;
        case kPostLowCut:
        case kPostHighCut:
        {
            const bool lowCut = p.shape == kPostLowCut;
            const int order = kSlopeDbPerOct[clampv (p.slope, 0, kNumSlopes - 1)] / 6;
            const int numBiquads = order / 2;
            int s = 0;
            if (order & 1)
                d.sections[s++] = makeSvf (lowCut ? FilterShape::HighPass1 : FilterShape::LowPass1, f, 0.7071, 0.0, fs);
            const double resonance = clampv (q / 0.70710678, 0.05, 20.0);
            for (int k = 1; k <= numBiquads; ++k)
            {
                const double theta = (2.0 * k - 1.0) * kPi / (2.0 * order);
                double qk = 1.0 / (2.0 * std::cos (theta));
                if (k == numBiquads)
                    qk *= resonance; // user Q acts as resonance on the highest-Q section
                d.sections[s++] = makeSvf (lowCut ? FilterShape::HighPass : FilterShape::LowPass, f, qk, 0.0, fs);
            }
            d.numSections = s;
            break;
        }
        default: d.numSections = 0; break;
    }
    return d;
}

float computePostEqLoudnessDb (const PostBandParams* bands, double fs) noexcept
{
    constexpr int kPts = 40;
    double sumW = 0.0, sum = 0.0;
    PostBandDesign designs[kNumPostBands];
    for (int b = 0; b < kNumPostBands; ++b)
        if (bands[b].active())
            designs[b] = designPostBand (bands[b], fs);

    for (int i = 0; i < kPts; ++i)
    {
        const double f = 30.0 * std::pow (16000.0 / 30.0, (double) i / (kPts - 1));
        // simple loudness weighting: most weight between 200 Hz and 6 kHz
        const double lw = std::log2 (f / 1000.0);
        const double w = std::exp (-0.5 * (lw * lw) / (2.0 * 2.0));
        const double tanW = std::tan (kPi * std::min (f, fs * 0.49) / fs);
        double pl = 1.0, pr = 1.0;
        for (int b = 0; b < kNumPostBands; ++b)
        {
            if (! bands[b].active())
                continue;
            const double m = designs[b].magnitudeSqFromTan (tanW);
            switch (bands[b].placement)
            {
                case kPlaceLeft: pl *= m; break;
                case kPlaceRight: pr *= m; break;
                case kPlaceMid:
                case kPlaceSide:
                {
                    // half of the (uncorrelated) power lives in mid or side
                    const double mixPow = 0.5 * m + 0.5;
                    pl *= mixPow;
                    pr *= mixPow;
                    break;
                }
                default:
                    pl *= m;
                    pr *= m;
                    break;
            }
        }
        sum += w * 0.5 * (pl + pr);
        sumW += w;
    }
    return (float) (10.0 * std::log10 (std::max (sum / sumW, 1.0e-12)));
}

void PostEq::prepare (double sampleRate)
{
    fs = sampleRate;
    reset();
}

void PostEq::reset()
{
    for (auto& b : bands)
    {
        for (auto& ch : b.state)
            for (auto& s : ch)
                s.reset();
        b.mix = b.mixTarget = b.mixStart = 0.0f;
        b.mixInc = 0.0f;
        b.initialised = false;
        b.structurePending = false;
        b.current = PostBandParams();
        b.design = PostBandDesign();
    }
    compDb = compTargetDb = 0.0f;
    compGain = 1.0f;
    compGainInc = 0.0f;
    needsCompUpdate = true;
}

void PostEq::setParams (const PostBandParams* p, bool autoGain) noexcept
{
    for (int i = 0; i < kNumPostBands; ++i)
    {
        const auto& a = bands[i].target;
        const auto& b = p[i];
        if (a.used != b.used || a.enabled != b.enabled || a.shape != b.shape || differs (a.freq, b.freq) || differs (a.gain, b.gain)
            || differs (a.q, b.q) || a.slope != b.slope || a.placement != b.placement)
            needsCompUpdate = true;
        bands[i].target = b;
    }
    if (autoGain != autoGainOn)
        needsCompUpdate = true;
    autoGainOn = autoGain;
}

void PostEq::update (int n) noexcept
{
    const float a = smoothingAlpha (0.03, fs, n);
    const float mixStep = (float) n / (float) (0.02 * fs); // 20 ms fades

    for (auto& b : bands)
    {
        const auto& t = b.target;
        const bool wantOn = t.active();

        if (! b.initialised)
        {
            b.current = t;
            b.design = designPostBand (b.current, fs);
            b.mix = b.mixStart = wantOn ? 1.0f : 0.0f;
            b.mixTarget = b.mix;
            b.mixInc = 0.0f;
            b.initialised = true;
            continue;
        }

        bool changed = false;

        // Structural changes (shape / slope / placement): fade out, switch, fade in
        if (! sameStructure (b.current, t))
        {
            if (b.mix <= 0.0f || ! b.current.active())
            {
                for (auto& ch : b.state)
                    for (auto& s : ch)
                        s.reset();
                b.current.shape = t.shape;
                b.current.slope = t.slope;
                b.current.placement = t.placement;
                b.current.freq = t.freq;
                b.current.gain = t.gain;
                b.current.q = t.q;
                changed = true;
            }
            else
            {
                b.mixTarget = 0.0f;
            }
        }
        else
        {
            b.mixTarget = wantOn ? 1.0f : 0.0f;
            if (b.mix <= 0.0f && b.mixTarget > 0.0f)
            {
                // band starts from silence: clear stale filter memory and jump to the target values
                for (auto& ch : b.state)
                    for (auto& s : ch)
                        s.reset();
                b.current.freq = t.freq;
                b.current.gain = t.gain;
                b.current.q = t.q;
                changed = true;
            }
        }
        b.current.used = t.used;
        b.current.enabled = t.enabled;

        // parameter smoothing
        const float lf = std::log (std::max (b.current.freq, 1.0f));
        const float lt = std::log (std::max (t.freq, 1.0f));
        if (std::abs (lf - lt) > 1.0e-4f)
        {
            b.current.freq = std::exp (lf + (lt - lf) * a);
            changed = true;
        }
        else if (differs (b.current.freq, t.freq))
        {
            b.current.freq = t.freq;
            changed = true;
        }
        if (std::abs (b.current.gain - t.gain) > 1.0e-3f)
        {
            b.current.gain += (t.gain - b.current.gain) * a;
            changed = true;
        }
        else if (differs (b.current.gain, t.gain))
        {
            b.current.gain = t.gain;
            changed = true;
        }
        const float lq = std::log (std::max (b.current.q, 0.001f));
        const float lqt = std::log (std::max (t.q, 0.001f));
        if (std::abs (lq - lqt) > 1.0e-4f)
        {
            b.current.q = std::exp (lq + (lqt - lq) * a);
            changed = true;
        }
        else if (differs (b.current.q, t.q))
        {
            b.current.q = t.q;
            changed = true;
        }
        if (changed || b.design.numSections == 0)
            b.design = designPostBand (b.current, fs);

        // mix ramp
        b.mixStart = b.mix;
        float next = b.mix;
        if (b.mixTarget > b.mix)
            next = std::min (b.mixTarget, b.mix + mixStep);
        else if (b.mixTarget < b.mix)
            next = std::max (b.mixTarget, b.mix - mixStep);
        b.mixInc = (next - b.mix) / (float) n;
    }

    if (needsCompUpdate && ++compCounter >= 4)
    {
        compCounter = 0;
        needsCompUpdate = false;
        PostBandParams targets[kNumPostBands];
        for (int i = 0; i < kNumPostBands; ++i)
            targets[i] = bands[i].target;
        compTargetDb = autoGainOn ? clampv (-computePostEqLoudnessDb (targets, fs), -18.0f, 18.0f) : 0.0f;
    }
    const float aComp = smoothingAlpha (0.08, fs, n);
    compDb += (compTargetDb - compDb) * aComp;
    const float newGain = dbToGain (compDb);
    compGainInc = (newGain - compGain) / (float) n;
}

void PostEq::process (float* L, float* R, int n) noexcept
{
    for (auto& b : bands)
    {
        if (b.mix <= 0.0f && b.mixInc <= 0.0f)
            continue;

        const int ns = b.design.numSections;
        const auto* sec = b.design.sections;
        float mix = b.mix;
        const int placement = b.current.placement;

        for (int s = 0; s < n; ++s)
        {
            mix += b.mixInc;
            const float m = clampv (mix, 0.0f, 1.0f);
            float l = L[s], r = R[s];
            switch (placement)
            {
                case kPlaceLeft:
                {
                    float y = l;
                    for (int k = 0; k < ns; ++k)
                        y = b.state[0][k].process (y, sec[k]);
                    l += m * (y - l);
                    break;
                }
                case kPlaceRight:
                {
                    float y = r;
                    for (int k = 0; k < ns; ++k)
                        y = b.state[1][k].process (y, sec[k]);
                    r += m * (y - r);
                    break;
                }
                case kPlaceMid:
                case kPlaceSide:
                {
                    const float mid = 0.5f * (l + r);
                    const float side = 0.5f * (l - r);
                    float in = placement == kPlaceMid ? mid : side;
                    float y = in;
                    for (int k = 0; k < ns; ++k)
                        y = b.state[0][k].process (y, sec[k]);
                    y = in + m * (y - in);
                    if (placement == kPlaceMid)
                    {
                        l = y + side;
                        r = y - side;
                    }
                    else
                    {
                        l = mid + y;
                        r = mid - y;
                    }
                    break;
                }
                default:
                {
                    float yl = l, yr = r;
                    for (int k = 0; k < ns; ++k)
                    {
                        yl = b.state[0][k].process (yl, sec[k]);
                        yr = b.state[1][k].process (yr, sec[k]);
                    }
                    l += m * (yl - l);
                    r += m * (yr - r);
                    break;
                }
            }
            L[s] = l;
            R[s] = r;
        }
        b.mix = clampv (mix, 0.0f, 1.0f);
    }

    float g = compGain;
    for (int s = 0; s < n; ++s)
    {
        g += compGainInc;
        L[s] *= g;
        R[s] *= g;
    }
    compGain = g;
}

} // namespace nh
