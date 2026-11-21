#include "Tanks.h"
#include "Tuning.h"

namespace nh
{
namespace
{
    constexpr float kFreezeLoopGain = 0.999999f;
    // soft limit of the loop (keeps it bounded); much weaker while frozen, so that even a loud
    // frozen tail keeps its level
    constexpr float kSoftLimit = 1.0f / 256.0f;
    constexpr float kSoftLimitFrozen = 1.0f / 8192.0f;

    // nearest whole sample; whole-sample delays make the frozen loop lossless (an interpolated
    // read damps the highs a little on every pass, a frozen tail would slowly get darker)
    inline float wholeSamples (float d, float glide) noexcept
    {
        const float r = std::round (d);
        return glide >= 1.0f ? r : d + (r - d) * glide;
    }

    inline void fwht8 (float* x) noexcept
    {
        for (int h = 1; h < 8; h <<= 1)
            for (int i = 0; i < 8; i += h << 1)
                for (int j = i; j < i + h; ++j)
                {
                    const float a = x[j];
                    const float b = x[j + h];
                    x[j] = a + b;
                    x[j + h] = a - b;
                }
        constexpr float s = 0.35355339059f; // 1/sqrt(8)
        for (int i = 0; i < 8; ++i)
            x[i] *= s;
    }

    // ms values of the input diffusers (scaled by inDiffScale)
    constexpr float kInDiffMs[FdnTank::kNumInputDiffusers] = { 0.95f, 1.55f, 2.45f, 3.85f };

    // Limits an allpass coefficient so that its own ringing (T60 = 3 M / log10(1/g)) stays
    // below 60 % of the reverb decay - short decays stay short.
    inline float capAllpass (float g, double delaySeconds, double t60) noexcept
    {
        const double gMax = std::pow (10.0, -5.0 * delaySeconds / std::max (0.02, t60));
        const float sign = g < 0.0f ? -1.0f : 1.0f;
        return sign * (float) std::min ((double) std::abs (g), gMax);
    }
} // namespace

//==============================================================================
void FdnTank::prepare (double sampleRate, double maxMeanDelaySeconds)
{
    {
        const double hp = tuning().earlyTapHpHz;
        earlyHp = hp > 1.0 ? makeSvf (FilterShape::HighPass, hp, 0.7071, 0.0, sampleRate) : makeSvf (FilterShape::Bell, 1000.0, 0.7071, 0.0, sampleRate);
    }
    fs = sampleRate;

    static const float jitter[N] = { 0.013f, -0.021f, 0.007f, 0.019f, -0.011f, 0.004f, -0.017f, 0.023f,
                                     -0.006f, 0.015f, -0.024f, 0.009f, 0.002f, -0.013f, 0.021f, -0.008f };
    // Sorted, irregular spread 0.55 ... 1.85 of the mean delay; even indices -> left tank, odd -> right tank
    float sorted[N];
    for (int i = 0; i < N; ++i)
        sorted[i] = 0.55f * std::pow (1.85f / 0.55f, (float) i / (N - 1)) * (1.0f + jitter[i]);
    for (int i = 0; i < 8; ++i)
    {
        ratio[i] = sorted[2 * i];
        ratio[i + 8] = sorted[2 * i + 1];
    }
    // shuffle within each half so that neighbouring lines are not monotonic
    static const int perm[8] = { 3, 6, 0, 5, 2, 7, 1, 4 };
    float tmp[N];
    for (int i = 0; i < 8; ++i)
    {
        tmp[i] = ratio[perm[i]];
        tmp[i + 8] = ratio[8 + perm[(i + 3) & 7]];
    }
    for (int i = 0; i < N; ++i)
    {
        ratio[i] = tmp[i];
        apRatio[i] = 0.12f + 0.08f * (float) ((i * 7) % 16) / 15.0f;
        rateMul[i] = 0.53f + 0.94f * (float) ((i * 5 + 3) % 16) / 15.0f;
        inSign[i] = ((i * 3 + 1) % 4) < 2 ? 1.0f : -1.0f;
        outSign[i] = ((i * 5 + 2) % 3) == 0 ? -1.0f : 1.0f;
        earlySign[i] = outSign[i]; // the balanced pattern of the main output (others add up at low frequencies)
        lfoPhase[i] = kTwoPiF * (float) ((i * 11) % 16) / 16.0f;
    }

    const double maxModSec = 0.006;
    maxLineSamples = (float) ((maxMeanDelaySeconds * 1.95 + maxModSec) * fs) + 8.0f;
    maxApSamples = (float) (maxMeanDelaySeconds * 1.95 * 0.21 * fs) + 8.0f;
    for (int i = 0; i < N; ++i)
    {
        lines[i].prepare ((int) maxLineSamples + 8);
        loopAp[i].prepare ((int) maxApSamples + 8);
    }
    for (auto& ch : inAp)
        for (auto& ap : ch)
            ap.prepare ((int) (0.004 * 4.0 * 1.1 * fs) + 16);
    maxSpreadSamples = (float) (0.6 * fs);
    for (auto& b : inBuf)
        b.prepare ((int) (maxSpreadSamples * 1.1f) + 16);

    reset();
}

void FdnTank::reset()
{
    for (auto& f : earlyHpState)
        f.reset();
    for (auto& l : lines)
        l.reset();
    for (auto& a : loopAp)
        a.reset();
    for (auto& ch : inAp)
        for (auto& a : ch)
            a.reset();
    for (auto& b : inBuf)
        b.reset();
    for (auto& s : stages)
        s.reset();
    for (int i = 0; i < N; ++i)
    {
        rnd[i] = rndTarget[i] = rndSmooth[i] = 0.0f;
        lineGain[i] = 0.0f;
    }
    first = true;
    designedMean = -1.0f;
}

double FdnTank::getReferenceDelaySeconds() const noexcept
{
    // average effective loop delay (delay + allpass)
    return (double) sMean * 1.0 * (1.0 + 0.16);
}

void FdnTank::redesign (const DecaySpec& spec) noexcept
{
    SvfCoefs co[kMaxDecayStages];
    for (int s = 0; s < kMaxDecayStages; ++s)
    {
        const bool a = spec.stages[s].active;
        if (a && ! stageActive[s])
            stages[s].reset();
        stageActive[s] = a;
    }
    for (int i = 0; i < N; ++i)
    {
        const double delaySec = (double) sMean * ratio[i] * (1.0 + apRatio[i]);
        float g = 1.0f;
        designSegmentFilters (spec, fs, delaySec, co, g);
        for (int s = 0; s < kMaxDecayStages; ++s)
        {
            if (stageActive[s])
                stages[s].set (i, co[s]);
            else
                stages[s].setIdentity (i);
        }
        lineGain[i] = g;
    }
    designedMean = sMean;
}

void FdnTank::update (int n, const DecaySpec& spec, bool specChanged) noexcept
{
    const float a = smoothingAlpha (0.05, fs, n);
    const auto& st = settings;

    if (first)
    {
        sMean = st.meanDelay;
        sApGain = st.apGain;
        sInDiffGain = st.inDiffGain;
        sInDiffScale = st.inDiffScale;
        sModDepth = st.modDepth;
        sModRate = st.modRate;
        sModRandom = st.modRandom;
        sCross = st.crossfeed;
        sSat = st.saturation;
        sFreeze = st.freeze;
        sEarlyTap = st.earlyTap;
    }

    // Freeze: everything inside the loop stays put (a moving coefficient or length would not be
    // lossless), only the input side and the modulators keep following
    const bool frozen = st.freeze > 0.5f;
    const float glide = clampv (st.freezeGlide, 0.0f, 1.0f);
    if (! frozen)
        sApGain += (st.apGain - sApGain) * a;
    sInDiffGain += (st.inDiffGain - sInDiffGain) * a;
    sInDiffScale += (st.inDiffScale - sInDiffScale) * a;
    sModDepth += (st.modDepth - sModDepth) * a;
    sModRate += (st.modRate - sModRate) * a;
    sModRandom += (st.modRandom - sModRandom) * a;
    sCross += (st.crossfeed - sCross) * a;
    sSat += (st.saturation - sSat) * a;
    sEarlyTap += (st.earlyTap - sEarlyTap) * a;
    sFreeze += (st.freeze - sFreeze) * smoothingAlpha (0.03, fs, n);
    if (std::abs (sFreeze - st.freeze) < 1.0e-4f)
        sFreeze = st.freeze;

    // Size: smooth + slew limited so that the doppler shift while morphing stays below ~3 %
    if (! frozen)
    {
        const float aMean = smoothingAlpha (0.22, fs, n);
        float next = sMean + (st.meanDelay - sMean) * aMean;
        const float maxStep = 0.03f * (float) n / (float) fs / 1.9f;
        next = sMean + clampv (next - sMean, -maxStep, maxStep);
        sMean = clampv (next, 0.002f, (float) (maxLineSamples / fs / 1.95));
    }

    // early output taps: whole samples at fixed fractions of every line (no interpolation, no
    // modulation), so the late reverb is there from its first milliseconds on
    {
        static constexpr float kEarlyFrac[N] = { 0.13f, 0.41f, 0.27f, 0.55f, 0.19f, 0.47f, 0.33f, 0.61f,
                                                 0.16f, 0.44f, 0.24f, 0.58f, 0.36f, 0.21f, 0.52f, 0.30f };
        for (int i = 0; i < N; ++i)
            earlyPos[i] = std::max (1, (int) (kEarlyFrac[i] * sMean * ratio[i] * (float) fs));
    }

    // build-up: line i gets the input kSpreadPos * spread later (whole samples); the later lines
    // get louder with spreadMix. Every line hears only one copy, so nothing is comb filtered.
    {
        static constexpr float kSpreadPos[8] = { 0.0f, 0.55f, 0.16f, 1.0f, 0.27f, 0.75f, 0.07f, 0.40f };
        const float spread = clampv (st.spread, 0.0f, maxSpreadSamples);
        const float mix = clampv (st.spreadMix, 0.0f, 1.0f);
        const float aS = smoothingAlpha (0.08, fs, n);
        for (int side = 0; side < 2; ++side)
        {
            float w[8], e = 0.0f;
            for (int k = 0; k < 8; ++k)
            {
                const float pos = kSpreadPos[side == 0 ? k : (k + 3) & 7];
                w[k] = (1.0f - mix) + mix * (0.35f + 0.65f * pos);
                e += w[k] * w[k];
            }
            const float norm = std::sqrt (8.0f / std::max (e, 1.0e-6f));
            for (int k = 0; k < 8; ++k)
            {
                const int i = side * 8 + k;
                const float pos = kSpreadPos[side == 0 ? k : (k + 3) & 7];
                const float targetD = std::round (pos * spread * (side == 0 ? 1.0f : 1.09f));
                const float targetW = w[k] * norm;
                if (first)
                    spreadW[i] = targetW;
                if (first || std::abs (targetD - spreadD[i]) < 0.01f)
                {
                    spreadD[i] = targetD; // at rest: exactly on a whole sample
                    spreadDInc[i] = 0.0f;
                }
                else
                {
                    float nd = spreadD[i] + (targetD - spreadD[i]) * aS;
                    if (std::abs (targetD - nd) < 0.01f)
                        nd = targetD;
                    spreadDInc[i] = (nd - spreadD[i]) / (float) n;
                }
                const float nw = spreadW[i] + (targetW - spreadW[i]) * aS;
                spreadWInc[i] = (nw - spreadW[i]) / (float) n;
            }
        }
    }

    // Modulation (control rate, linearly interpolated per sample)
    // Every fractional (interpolated) read damps the highs a little - in a loop on every pass, which
    // made the high frequencies decay much too fast. So all lengths sit on whole samples and only 4 of
    // the 16 lines are modulated (with twice the depth): the loop stays almost free of this loss.
    static constexpr bool kModulated[N] = { false, true, false, false, false, false, true, false,
                                            false, false, true, false, false, true, false, false };
    const float depthSamples = 2.0f * sModDepth * (float) fs;
    const float dt = (float) n / (float) fs;
    for (int i = 0; i < N; ++i)
    {
        lfoPhase[i] += kTwoPiF * sModRate * rateMul[i] * dt;
        if (lfoPhase[i] > kTwoPiF)
            lfoPhase[i] -= kTwoPiF;

        // smooth random walk
        const float rRate = sModRate * rateMul[i] * 1.3f;
        const float aR = 1.0f - std::exp (-kTwoPiF * rRate * dt);
        rnd[i] += (rndTarget[i] - rnd[i]) * aR;
        rndSmooth[i] += (rnd[i] - rndSmooth[i]) * aR;
        if (std::abs (rndTarget[i] - rnd[i]) < 0.08f)
            rndTarget[i] = rng.bipolar();

        modSignal[i] = (1.0f - sModRandom) * std::sin (lfoPhase[i]) + sModRandom * rndSmooth[i] * 1.4f;
        // while frozen the delay modulation fades out (slowly, so the pitch never bends) and the
        // delays settle on whole samples; the movement then comes from the pair rotations below
        const float mod = kModulated[i] ? depthSamples * modSignal[i] * (1.0f - glide) : 0.0f;
        const float base = sMean * ratio[i] * (float) fs;
        float target = clampv (std::round (base) + mod, 4.0f, maxLineSamples - 4.0f);
        float apTarget = clampv (std::round (base * apRatio[i]), 3.0f, maxApSamples - 4.0f);
        if (glide > 0.0f)
        {
            const float whole = std::round (clampv (base, 4.0f, maxLineSamples - 4.0f));
            target = glide >= 1.0f ? whole : target + (whole - target) * glide;
            apTarget = wholeSamples (apTarget, glide);
        }

        if (first)
        {
            curDelay[i] = target;
            curAp[i] = apTarget;
        }
        if (glide >= 1.0f) // land exactly on the whole sample (no rounding residue of the ramps)
        {
            if (std::abs (target - curDelay[i]) < 0.02f)
                curDelay[i] = target;
            if (std::abs (apTarget - curAp[i]) < 0.02f)
                curAp[i] = apTarget;
        }
        delayInc[i] = (target - curDelay[i]) / (float) n;
        apInc[i] = (apTarget - curAp[i]) / (float) n;
    }

    // Freeze motion (Character still works while frozen): pairs of lines are rotated against
    // each other by slowly moving angles. A rotation is orthogonal at every instant, so the
    // frozen loop stays lossless while its modes keep moving.
    {
        const float depth = glide * st.freezeMotion;
        bool active = depth > 0.0f;
        for (int pr = 0; pr < kNumPairs; ++pr)
            active = active || std::abs (rotS[pr]) > 1.0e-7f;
        motionActive = active;
        for (int pr = 0; pr < kNumPairs; ++pr)
        {
            if (! active)
            {
                rotC[pr] = 1.0f;
                rotS[pr] = 0.0f;
                rotCInc[pr] = rotSInc[pr] = 0.0f;
                continue;
            }
            const int i = pr < 4 ? pr : pr + 4; // pairs (i, i + 4) inside each block of 8
            const float ang = depth * clampv (modSignal[i], -1.5f, 1.5f);
            const float cT = std::cos (ang), sT = std::sin (ang);
            if (first)
            {
                rotC[pr] = cT;
                rotS[pr] = sT;
            }
            rotCInc[pr] = (cT - rotC[pr]) / (float) n;
            rotSInc[pr] = (sT - rotS[pr]) / (float) n;
        }
    }

    for (int k = 0; k < kNumInputDiffusers; ++k)
    {
        const float target = std::max (2.0f, kInDiffMs[k] * 0.001f * sInDiffScale * (float) fs);
        if (first)
            curInAp[k] = target;
        inApInc[k] = (target - curInAp[k]) / (float) n;
        gInK[k] = capAllpass (sInDiffGain, target * 1.071 / fs, st.t60);
    }

    if (specChanged || designedMean <= 0.0f || std::abs (sMean - designedMean) > 0.002f * designedMean)
        redesign (spec);

    first = false;
}

void FdnTank::process (const float* inL, const float* inR, float* outL, float* outR, int n) noexcept
{
    const float fz = sFreeze;
    const float att = 1.0f - fz;
    const float raw = fz * kFreezeLoopGain;
    const float inGain = 1.0f - fz;
    const float gAp = sApGain;
    const float k2 = sSat * sSat + kSoftLimit * (1.0f - fz) + kSoftLimitFrozen * fz;
    const float theta = sCross * (kPiF * 0.25f);
    const float rc = std::cos (theta), rs = std::sin (theta);
    constexpr float outNorm = 0.35355339059f;
    const float eg = sEarlyTap;
    constexpr float inNorm = 0.35355339059f;
    constexpr float rightDiffScale = 1.071f;

    alignas (32) float x[N];
    alignas (32) float dry[N];

    for (int smp = 0; smp < n; ++smp)
    {
        for (int i = 0; i < N; ++i)
        {
            curDelay[i] += delayInc[i];
            x[i] = lines[i].tapLagrange (curDelay[i]);
            dry[i] = x[i];
        }

        for (int s = 0; s < kMaxDecayStages; ++s)
            if (stageActive[s])
                stages[s].process (x);

        for (int i = 0; i < N; ++i)
            x[i] = x[i] * lineGain[i] * att + dry[i] * raw;

        for (int i = 0; i < N; ++i)
        {
            curAp[i] += apInc[i];
            x[i] = loopAp[i].process (x[i], curAp[i], gAp);
        }

        for (int i = 0; i < N; ++i)
            x[i] = passiveSaturate (x[i], k2);

        float oL = 0.0f, oR = 0.0f;
        for (int i = 0; i < 8; ++i)
        {
            oL += outSign[i] * x[i];
            oR += outSign[i + 8] * x[i + 8];
        }
        if (eg > 0.0f)
        {
            float eL = 0.0f, eR = 0.0f;
            for (int i = 0; i < 8; ++i)
            {
                eL += earlySign[i] * lines[i].tap (earlyPos[i]);
                eR += earlySign[i + 8] * lines[i + 8].tap (earlyPos[i + 8]);
            }
            // high-pass 160 Hz: the lines carry low-frequency energy that cancels at the main
            // output but not at these taps
            oL += eg * earlyHpState[0].process (eL, earlyHp);
            oR += eg * earlyHpState[1].process (eR, earlyHp);
        }
        outL[smp] = oL * outNorm;
        outR[smp] = oR * outNorm;

        fwht8 (x);
        fwht8 (x + 8);
        for (int i = 0; i < 8; ++i)
        {
            const float p = x[i];
            const float q = x[i + 8];
            x[i] = rc * p + rs * q;
            x[i + 8] = -rs * p + rc * q;
        }
        if (motionActive)
            for (int pr = 0; pr < kNumPairs; ++pr)
            {
                rotC[pr] += rotCInc[pr];
                rotS[pr] += rotSInc[pr];
                const int i = pr < 4 ? pr : pr + 4;
                const float p = x[i];
                const float q = x[i + 4];
                x[i] = rotC[pr] * p - rotS[pr] * q;
                x[i + 4] = rotS[pr] * p + rotC[pr] * q;
            }

        float il = inL[smp] * inGain;
        float ir = inR[smp] * inGain;
        for (int k = 0; k < kNumInputDiffusers; ++k)
        {
            curInAp[k] += inApInc[k];
            il = inAp[0][k].processLinear (il, curInAp[k], gInK[k]);
            ir = inAp[1][k].processLinear (ir, curInAp[k] * rightDiffScale, gInK[k]);
        }
        inBuf[0].push (il * inNorm);
        inBuf[1].push (ir * inNorm);
        for (int i = 0; i < N; ++i)
        {
            spreadD[i] += spreadDInc[i];
            spreadW[i] += spreadWInc[i];
            x[i] += inSign[i] * spreadW[i] * inBuf[i < 8 ? 0 : 1].tapLinear (spreadD[i] + 1.0f);
        }

        for (int i = 0; i < N; ++i)
            lines[i].push (x[i]);
    }
}

//==============================================================================
namespace
{
    constexpr double kDattorroRate = 29761.0;
}

void PlateTank::prepare (double sampleRate, float maxSize, float lengthVariant, float modPhaseOffset)
{
    fs = sampleRate;
    variant = lengthVariant;
    maxSizeAllowed = maxSize;
    lfoPhaseOffset = modPhaseOffset;
    const float k = (float) (fs / kDattorroRate) * variant;

    lIn[0] = 142.0f * k;
    lIn[1] = 107.0f * k;
    lIn[2] = 379.0f * k;
    lIn[3] = 277.0f * k;
    lApA1 = 672.0f * k;
    lDA1 = 4453.0f * k;
    lApA2 = 1800.0f * k;
    lDA2 = 3720.0f * k;
    lApB1 = 908.0f * k;
    lDB1 = 4217.0f * k;
    lApB2 = 2656.0f * k;
    lDB2 = 3163.0f * k;
    {
        const float l[kLoopDelays] = { lApA1, lDA1, lApA2, lDA2, lApB1, lDB1, lApB2, lDB2 };
        for (int i = 0; i < kLoopDelays; ++i)
            lLoop[i] = l[i];
    }

    const float tl[7] = { 266.0f, 2974.0f, 1913.0f, 1996.0f, 1990.0f, 187.0f, 1066.0f };
    const float tr[7] = { 353.0f, 3627.0f, 1228.0f, 2673.0f, 2111.0f, 335.0f, 121.0f };
    for (int i = 0; i < 7; ++i)
    {
        tapsL[i] = tl[i] * k;
        tapsR[i] = tr[i] * k;
    }

    const float m = maxSize * 1.05f;
    const int modExtra = (int) (0.004 * fs) + 8;
    for (int i = 0; i < 4; ++i)
        inAp[i].prepare ((int) (lIn[i] * m * 2.6f) + 8);
    apA1.prepare ((int) (lApA1 * m) + modExtra);
    apB1.prepare ((int) (lApB1 * m) + modExtra);
    apA2.prepare ((int) (lApA2 * m) + 8);
    apB2.prepare ((int) (lApB2 * m) + 8);
    dA1.prepare ((int) (lDA1 * m) + 8);
    dA2.prepare ((int) (lDA2 * m) + 8);
    dB1.prepare ((int) (lDB1 * m) + 8);
    dB2.prepare ((int) (lDB2 * m) + 8);
    reset();
}

void PlateTank::reset()
{
    for (auto& a : inAp)
        a.reset();
    apA1.reset();
    apA2.reset();
    apB1.reset();
    apB2.reset();
    dA1.reset();
    dA2.reset();
    dB1.reset();
    dB2.reset();
    for (auto& a : att)
        a.reset();
    lastA = lastB = 0.0f;
    first = true;
    designedSize = -1.0f;
}

double PlateTank::getReferenceDelaySeconds() const noexcept
{
    return (double) ((lApA1 + lDA1 + lApA2 + lDA2 + lApB1 + lDB1 + lApB2 + lDB2) * 0.25f * curSize) / fs;
}

void PlateTank::update (int n, const DecaySpec& spec, bool specChanged) noexcept
{
    const float a = smoothingAlpha (0.05, fs, n);
    const auto& st = settings;
    const float targetSize = clampv (st.size, 0.3f, maxSizeAllowed);

    if (first)
    {
        curSize = targetSize;
        sMod = st.modDepth;
        sModRate = st.modRate;
        sSat = st.saturation;
        sFreeze = st.freeze;
        sPreDiff = st.preDiffScale;
        sInDiff1 = st.inDiff1;
        sInDiff2 = st.inDiff2;
        sDecDiff1 = st.decayDiff1;
        sDecDiff2 = st.decayDiff2;
        sEarly = st.earlyLevel;
    }

    const bool frozen = st.freeze > 0.5f; // see FdnTank: the loop stays put while frozen
    const float glide = clampv (st.freezeGlide, 0.0f, 1.0f);

    sEarly += (st.earlyLevel - sEarly) * a;
    sMod += (st.modDepth - sMod) * a;
    sModRate += (st.modRate - sModRate) * a;
    sSat += (st.saturation - sSat) * a;
    sPreDiff += (st.preDiffScale - sPreDiff) * a;
    sInDiff1 += (st.inDiff1 - sInDiff1) * a;
    sInDiff2 += (st.inDiff2 - sInDiff2) * a;
    sDecDiff1 += (st.decayDiff1 - sDecDiff1) * a;
    sDecDiff2 += (st.decayDiff2 - sDecDiff2) * a;
    sFreeze += (st.freeze - sFreeze) * smoothingAlpha (0.03, fs, n);
    if (std::abs (sFreeze - st.freeze) < 1.0e-4f)
        sFreeze = st.freeze;

    {
        const double t60 = st.t60;
        const double sz = curSize;
        gIn[0] = capAllpass (sInDiff1, lIn[0] * sz * sPreDiff / fs, t60);
        gIn[1] = capAllpass (sInDiff1, lIn[1] * sz * sPreDiff / fs, t60);
        gIn[2] = capAllpass (sInDiff2, lIn[2] * sz * sPreDiff / fs, t60);
        gIn[3] = capAllpass (sInDiff2, lIn[3] * sz * sPreDiff / fs, t60);
        if (! frozen || first)
        {
            gDecA1 = capAllpass (sDecDiff1, lApA1 * sz / fs, t60);
            gDecB1 = capAllpass (sDecDiff1, lApB1 * sz / fs, t60);
            gDecA2 = capAllpass (sDecDiff2, lApA2 * sz / fs, t60);
            gDecB2 = capAllpass (sDecDiff2, lApB2 * sz / fs, t60);
        }
    }

    float next = curSize;
    if (! frozen)
    {
        const float aSize = smoothingAlpha (0.22, fs, n);
        next = curSize + (targetSize - curSize) * aSize;
        const float maxStep = 0.03f * (float) n / (float) fs * 2.0f; // relative to ~0.5 s total loop
        next = curSize + clampv (next - curSize, -maxStep, maxStep);
    }
    sizeInc = (next - curSize) / (float) n;

    // loop delays: per-sample ramps, whole samples while frozen (lossless loop)
    for (int i = 0; i < kLoopDelays; ++i)
    {
        float t = std::max (4.0f, lLoop[i] * next);
        if (glide > 0.0f)
            t = wholeSamples (t, glide);
        if (first)
            curD[i] = t;
        if (glide >= 1.0f && std::abs (t - curD[i]) < 0.02f)
            curD[i] = t;
        dInc[i] = (t - curD[i]) / (float) n;
    }
    // delay modulation fades out while frozen, a rotation between the two halves takes over
    {
        const float keep = 1.0f - glide;
        if (first)
            modKeep = keep;
        modKeepInc = (keep - modKeep) / (float) n;
    }

    lfoPhase += kTwoPiF * sModRate * (float) n / (float) fs;
    if (lfoPhase > kTwoPiF)
        lfoPhase -= kTwoPiF;

    {
        const float ang = glide * st.freezeMotion * std::sin (lfoPhase * 0.71f + 1.3f + lfoPhaseOffset);
        const float cT = std::cos (ang), sT = std::sin (ang);
        if (first)
        {
            rotC = cT;
            rotS = sT;
        }
        rotCInc = (cT - rotC) / (float) n;
        rotSInc = (sT - rotS) / (float) n;
    }

    if (specChanged || designedSize <= 0.0f || std::abs (next - designedSize) > 0.002f * designedSize)
    {
        const double s = (double) next / fs;
        att[0].design (spec, fs, (lApA1 + lDA1) * s);
        att[1].design (spec, fs, (lApA2 + lDA2) * s);
        att[2].design (spec, fs, (lApB1 + lDB1) * s);
        att[3].design (spec, fs, (lApB2 + lDB2) * s);
        designedSize = next;
    }
    first = false;
}

void PlateTank::process (const float* in, float* outL, float* outR, int n) noexcept
{
    const float fz = sFreeze;
    const float attMix = 1.0f - fz;
    const float rawMix = fz * kFreezeLoopGain;
    const float inGain = 1.0f - fz;
    const float k2 = sSat * sSat + kSoftLimit * (1.0f - fz) + kSoftLimitFrozen * fz;
    const float modSamples = sMod * (float) fs;
    const float lfoStep = kTwoPiF * sModRate / (float) fs;
    float phase = lfoPhase - lfoStep * (float) n; // phase at block start (lfoPhase was advanced in update)

    for (int smp = 0; smp < n; ++smp)
    {
        curSize += sizeInc;
        phase += lfoStep;
        const float sz = curSize;
        const float pre = sz * sPreDiff;

        float x = in[smp] * inGain;
        x = inAp[0].processLinear (x, std::max (2.0f, lIn[0] * pre), gIn[0]);
        x = inAp[1].processLinear (x, std::max (2.0f, lIn[1] * pre), gIn[1]);
        x = inAp[2].processLinear (x, std::max (2.0f, lIn[2] * pre), gIn[2]);
        x = inAp[3].processLinear (x, std::max (2.0f, lIn[3] * pre), gIn[3]);
        const float early = x * sEarly;

        for (int i = 0; i < kLoopDelays; ++i)
            curD[i] += dInc[i];
        modKeep += modKeepInc;
        rotC += rotCInc;
        rotS += rotSInc;
        const float modA = modSamples * modKeep * std::sin (phase + lfoPhaseOffset);
        const float modB = modSamples * modKeep * std::cos (phase * 0.93f + lfoPhaseOffset);

        // cross coupling of the two halves (identity unless frozen with Character > 0)
        const float fromB = rotC * lastB - rotS * lastA;
        const float fromA = rotS * lastB + rotC * lastA;

        // ---- half A
        float a = x + fromB;
        a = apA1.process (a, std::max (4.0f, curD[0] + modA), -gDecA1);
        {
            const float t = dA1.tapLagrange (curD[1]);
            dA1.push (a);
            a = t;
        }
        {
            const float y = att[0].process (a);
            a = y * attMix + a * rawMix;
        }
        a = apA2.process (a, curD[2], gDecA2);
        {
            const float t = dA2.tapLagrange (curD[3]);
            dA2.push (a);
            a = t;
        }
        {
            const float y = att[1].process (a);
            a = y * attMix + a * rawMix;
        }
        a = passiveSaturate (a, k2);

        // ---- half B
        float b = x + fromA;
        b = apB1.process (b, std::max (4.0f, curD[4] + modB), -gDecB1);
        {
            const float t = dB1.tapLagrange (curD[5]);
            dB1.push (b);
            b = t;
        }
        {
            const float y = att[2].process (b);
            b = y * attMix + b * rawMix;
        }
        b = apB2.process (b, curD[6], gDecB2);
        {
            const float t = dB2.tapLagrange (curD[7]);
            dB2.push (b);
            b = t;
        }
        {
            const float y = att[3].process (b);
            b = y * attMix + b * rawMix;
        }
        b = passiveSaturate (b, k2);

        lastA = a;
        lastB = b;

        // ---- output taps (Dattorro)
        const float yl = dB1.tapLinear (tapsL[0] * sz + 1.0f) + dB1.tapLinear (tapsL[1] * sz + 1.0f)
                         - apB2.tapInternal (tapsL[2] * sz + 1.0f) + dB2.tapLinear (tapsL[3] * sz + 1.0f)
                         - dA1.tapLinear (tapsL[4] * sz + 1.0f) - apA2.tapInternal (tapsL[5] * sz + 1.0f)
                         - dA2.tapLinear (tapsL[6] * sz + 1.0f);
        const float yr = dA1.tapLinear (tapsR[0] * sz + 1.0f) + dA1.tapLinear (tapsR[1] * sz + 1.0f)
                         - apA2.tapInternal (tapsR[2] * sz + 1.0f) + dA2.tapLinear (tapsR[3] * sz + 1.0f)
                         - dB1.tapLinear (tapsR[4] * sz + 1.0f) - apB2.tapInternal (tapsR[5] * sz + 1.0f)
                         - dB2.tapLinear (tapsR[6] * sz + 1.0f);
        outL[smp] = yl * 0.6f + early;
        outR[smp] = yr * 0.6f - early;
    }
}

} // namespace nh
