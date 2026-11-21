#include "ReverbEngine.h"
#include "Tuning.h"

namespace nh
{
namespace
{
    // Level calibration (measured with the offline test, pink noise in -> similar perceived level out)
    constexpr float kFdnLevel = 1.30f;
    constexpr float kPlateLevel = 0.95f;
    constexpr float kErLevel = 1.00f;
    constexpr float kErDimX = 7.0f, kErDimY = 5.5f, kErDimZ = 3.2f; // early reflection geometry (all rooms)
    constexpr float kLateOnsetSeconds = 0.0045f;                     // late reverb input delay (Modern, Vintage)

    inline float widthTable (const float* table, float w) noexcept
    {
        const Tuning& tu = tuning();
        w = clampv (w, 0.0f, 2.0f);
        const int last = Tuning::kWidthPoints - 1;
        if (w >= tu.widthAt[last])
        {
            if (table == tu.widthSide) // beyond 120 % (old projects up to 200 %): keep widening
                return table[last] + (w - tu.widthAt[last]) * 2.05f;
            return table[last];
        }
        int i = 0;
        while (i < last - 1 && w > tu.widthAt[i + 1])
            ++i;
        const float t = (w - tu.widthAt[i]) / (tu.widthAt[i + 1] - tu.widthAt[i]);
        return lerpf (table[i], table[i + 1], t);
    }

    // Thickness detector (weighting and timing)
    constexpr double kThickLpHz = 4000.0;     // detector weighting
    constexpr double kThickAttack = 0.005, kThickRelease = 0.25;

    inline void balanceGains (float pan, float& gl, float& gr) noexcept
    {
        pan = clampv (pan, -1.0f, 1.0f);
        gl = pan > 0.0f ? 1.0f - pan : 1.0f;
        gr = pan < 0.0f ? 1.0f + pan : 1.0f;
    }

    inline float meanDelayForRoom (const RoomState& room, int style) noexcept
    {
        if (style == kStyleVintage)
            return clampv (room.vintageMeanFreePathSec * 1.1f, 0.008f, 0.11f);
        return clampv (room.meanFreePathSec * 0.9f, 0.0035f, 0.09f);
    }

    inline float plateSizeForSpace (float space) noexcept { return 0.45f + clampv (space, 0.0f, 1.0f); }

    // Space value of Nachhall 1.0 (15 room models) for a displayed decay time
    inline float legacySpaceForT60 (float t60) noexcept
    {
        float lo = 0.0f, hi = 1.0f;
        for (int i = 0; i < 30; ++i)
        {
            const float mid = 0.5f * (lo + hi);
            if (legacySpaceToT60 (mid) < t60)
                lo = mid;
            else
                hi = mid;
        }
        return 0.5f * (lo + hi);
    }

    // Wet level correction. Modern: per room model (fitted to Pro-R 2). Vintage and Plate: the
    // loudness tables of Nachhall 1.0 (11 points along its Space control, pink noise).
    inline float levelCorrectionDb (int style, float space) noexcept
    {
        const RoomState room = interpolateRoom (space);
        if (style == kStyleModern)
            return room.levelDb;
        static const float vintage[11] = { -2.0f, -2.4f, -1.0f, 0.0f, 1.2f, 2.7f, 3.5f, 3.3f, 4.7f, 3.4f, 2.3f };
        static const float plateTab[11] = { -4.9f, -3.9f, -3.6f, -3.0f, -2.5f, -2.0f, -1.7f, -1.8f, -1.7f, -1.6f, -2.0f };
        const float* t = style == kStylePlate ? plateTab : vintage;
        const float pos = legacySpaceForT60 (room.t60) * 10.0f;
        const int i = std::min (9, (int) pos);
        return lerpf (t[i], t[i + 1], pos - (float) i) + Tuning::styleAt (tuning().styleLevelDb[style == kStylePlate ? 1 : 0], room.t60);
    }

    inline bool relDiff (float a, float b, float tol) noexcept
    {
        return std::abs (a - b) > tol * std::max (std::abs (a), std::abs (b));
    }

    bool decaySettingsDiffer (const DecaySettings& a, const DecaySettings& b) noexcept
    {
        if (relDiff (a.t60, b.t60, 0.002f) || std::abs (a.bassRatio - b.bassRatio) > 0.001f || relDiff (a.bassFreq, b.bassFreq, 0.002f)
            || relDiff (a.hfRatio, b.hfRatio, 0.002f) || relDiff (a.hfFreq, b.hfFreq, 0.002f)
            || std::abs (a.airRatio - b.airRatio) > 0.001f || relDiff (a.airFreq, b.airFreq, 0.002f)
            || relDiff (a.airAbsRate, b.airAbsRate, 0.002f) || relDiff (a.airAbsFreq, b.airAbsFreq, 0.002f) || relDiff (a.airAbsQ, b.airAbsQ, 0.002f))
            return true;
        for (int k = 0; k < kNumDecayBands; ++k)
            if (! (a.bands[k] == b.bands[k]))
                return true;
        return false;
    }

    inline float smoothLog (float cur, float target, float a) noexcept
    {
        const float lc = std::log (std::max (cur, 1.0e-6f));
        const float lt = std::log (std::max (target, 1.0e-6f));
        if (std::abs (lc - lt) < 1.0e-4f)
            return target;
        return std::exp (lc + (lt - lc) * a);
    }
} // namespace

//==============================================================================
DecaySettings ReverbEngine::makeDecaySettings (const DecayBandParams* bands, float space, float decayRate, float brightness, int style) noexcept
{
    const RoomState room = interpolateRoom (space);
    DecaySettings ds;
    const float rate = clampv (decayRate, 0.25f, 4.0f);
    {
        // measured on Pro-R 2: decay time per Decay Rate step (25 / 50 / 100 / 200 / 400 %)
        const Tuning& tr = tuning();
        const float x = clampv ((std::log2 (rate) + 2.0f) / 4.0f, 0.0f, 1.0f);
        ds.t60 = room.actualT60() * rate * Tuning::at (tr.rateCorr, 5, x);
        ds.airAbsRate = tr.airAbsRate * room.airAbs;
        ds.airAbsFreq = tr.airAbsFreq;
        ds.airAbsQ = tr.airAbsQ;
    }
    ds.bassRatio = room.bassRatio;
    ds.bassFreq = 220.0f;

    // Brightness changes the room's high-frequency decay (fitted to Pro-R 2 at -100 ... +100 %)
    const Tuning& tu = tuning();
    const float b = clampv (brightness, 0.0f, 1.0f);
    float hfR = clampv (room.hfRatio * Tuning::at (tu.brightHf, Tuning::kBrightPoints, b), 0.03f, 1.6f);
    float hfF = clampv (room.hfFreq * Tuning::at (tu.brightFreq, Tuning::kBrightPoints, b), 600.0f, 16000.0f);
    ds.airRatio = clampv (room.airRatio * Tuning::at (tu.brightAir, Tuning::kBrightPoints, b), 0.03f, 1.8f);
    ds.airFreq = 11000.0f;

    if (style == kStylePlate)
    {
        ds.bassRatio = 0.8f;
        ds.bassFreq = 180.0f;
        hfR = b < 0.5f ? expLerp (0.18f, 0.7f, b / 0.5f) : expLerp (0.7f, 1.0f, (b - 0.5f) / 0.5f);
        hfF = expLerp (3400.0f, 7000.0f, b);
        ds.airRatio = lerpf (0.6f, 0.95f, b);
    }
    else if (style == kStyleVintage)
    {
        hfR *= 0.85f;
        ds.airRatio *= 0.7f;
        ds.airFreq = 8000.0f;
        ds.bassRatio *= 1.05f;
    }
    if (style != kStyleModern) // fitted to the Pro-R 2 style recordings
    {
        const int k = style == kStylePlate ? 1 : 0;
        // the correction never pushes the decay below 120 ms, the shortest the tanks do cleanly
        ds.t60 = std::max (ds.t60 * Tuning::styleAt (tu.styleT60[k], room.t60), std::min (ds.t60, 0.12f));
        hfR = clampv (hfR * Tuning::styleAt (tu.styleHf[k], room.t60), 0.03f, 1.6f);
    }
    ds.hfRatio = hfR;
    ds.hfFreq = hfF;
    for (int k = 0; k < kNumDecayBands; ++k)
        ds.bands[k] = bands[k];
    return ds;
}

double ReverbEngine::referenceDelayFor (int style, float space) noexcept
{
    if (style == kStylePlate)
    {
        const double seg = (672.0 + 4453.0 + 1800.0 + 3720.0 + 908.0 + 4217.0 + 2656.0 + 3163.0) / 4.0 / 29761.0;
        return seg * plateSizeForSpace (space);
    }
    return (double) meanDelayForRoom (interpolateRoom (space), style) * 1.16;
}

//==============================================================================
void ReverbEngine::prepare (double sampleRate, int maxBlockSize)
{
    fs = sampleRate;
    maxBlock = std::max (32, maxBlockSize);

    for (auto& d : predelay)
        d.prepare ((int) (kMaxPredelaySeconds * fs) + 64);
    for (auto& d : buildBuf)
        d.prepare ((int) (0.45 * fs) + 64);

    er.prepare (fs);
    fdn.prepare (fs, 0.115);
    plate[0].prepare (fs, 1.6f, 1.0f, 0.0f);
    plate[1].prepare (fs, 1.6f, 0.937f, 1.7f);
    postEq.prepare (fs);

    // Vintage band limit: the Vintage tail of Pro-R 2 is even a little brighter above 12 kHz than
    // Modern (recordings S_Vintage_Basis / B_Basis_A), so the limit sits high
    vintLp[0] = makeSvf (FilterShape::LowPass, std::min (16000.0, 0.42 * fs), 0.5412, 0.0, fs);
    vintLp[1] = makeSvf (FilterShape::LowPass, std::min (16000.0, 0.42 * fs), 1.3066, 0.0, fs);
    vintInLp = makeSvf (FilterShape::LowPass, std::min (18000.0, 0.45 * fs), 0.7071, 0.0, fs);

    envAtt = 1.0f - std::exp (-1.0f / (float) (0.0005 * fs));
    thickLpCoefs = makeSvf (FilterShape::LowPass, kThickLpHz, 0.7071, 0.0, fs);
    thickAtt = 1.0f - std::exp (-1.0f / (float) (kThickAttack * fs));
    thickRel = 1.0f - std::exp (-1.0f / (float) (kThickRelease * fs));
    envRel = std::exp (-1.0f / (float) (0.08 * fs));

    const size_t anaSize = (size_t) std::max (maxBlock, 8192);
    anaPre.assign (anaSize, 0.0f);
    anaWet.assign (anaSize, 0.0f);
    anaOut.assign (anaSize, 0.0f);

    reset();
}

void ReverbEngine::reset()
{
    for (auto& d : predelay)
        d.reset();
    for (auto& d : buildBuf)
        d.reset();
    er.reset();
    fdn.reset();
    for (auto& p : plate)
        p.reset();
    postEq.reset();
    for (auto& f : erTiltState)
        f.reset();
    for (auto& ch : tiltState)
        for (auto& s : ch)
            s.reset();
    for (auto& ch : lateToneState)
        for (auto& s : ch)
            s.reset();
    lastLateLsDb = lastLateHsDb = lastLateMidDb = lastLateAirDb = -999.0f;
    levelTrim = 1.0f;
    levelTrimInc = 0.0f;
    for (auto& ch : vintState)
        for (auto& s : ch)
            s.reset();
    for (auto& s : vintInState)
        s.reset();

    firstUpdate = true;
    haveSpec = false;
    specCountdown = 0;
    lastTiltBrightness = -1.0f;
    env = 0.0f;
    thickEnv = 0.0f;
    thickLp.reset();
    refDb = -90.0f;
    duckDb = 0.0f;
    duckGain = gateGain = 1.0f;
    duckGainInc = gateGainInc = 0.0f;
    gateOpen = false;
    gateHoldCounter = 0;
    pdXfade = 0.0f;
    peak[0] = peak[1] = 0.0f;

    const bool plateStyle = params.style == kStylePlate;
    fdnRunning = ! plateStyle;
    plateRunning = plateStyle;
    fdnInGain = fdnOutGain = plateStyle ? 0.0f : 1.0f;
    plateInGain = plateOutGain = plateStyle ? 1.0f : 0.0f;
    fdnNeedsReset = plateNeedsReset = false;
}

//==============================================================================
void ReverbEngine::updateTankActivity (int n) noexcept
{
    const bool wantPlate = params.style == kStylePlate;
    const float inStep = (float) n / (float) (0.02 * fs);
    const float outStep = (float) n / (float) (1.2 * fs);

    auto activate = [&] (bool& running, bool& needsReset, float& inG, float& outG)
    {
        if (! running)
        {
            running = true;
            needsReset = true;
            inG = 0.0f;
            outG = 1.0f;
        }
        inG = std::min (1.0f, inG + inStep);
        outG = std::min (1.0f, outG + inStep);
    };
    auto deactivate = [&] (bool& running, bool& needsReset, float& inG, float& outG)
    {
        if (! running)
            return;
        inG = std::max (0.0f, inG - inStep);
        if (inG <= 0.0f)
            outG = std::max (0.0f, outG - outStep);
        if (outG <= 0.0f)
        {
            running = false;
            needsReset = true;
        }
    };

    if (wantPlate)
    {
        activate (plateRunning, plateNeedsReset, plateInGain, plateOutGain);
        deactivate (fdnRunning, fdnNeedsReset, fdnInGain, fdnOutGain);
    }
    else
    {
        activate (fdnRunning, fdnNeedsReset, fdnInGain, fdnOutGain);
        deactivate (plateRunning, plateNeedsReset, plateInGain, plateOutGain);
    }
}

void ReverbEngine::controlUpdate (int n) noexcept
{
    const EngineParams& p = params;
    const float aFast = smoothingAlpha (0.02, fs, n);
    const float aMed = smoothingAlpha (0.05, fs, n);
    const float aSlow = smoothingAlpha (0.12, fs, n);

    if (firstUpdate)
    {
        sSpace = p.space;
        sDecayRate = p.decayRate;
        sCharacter = p.character;
        sThickness = p.thickness;
        sDistance = p.distance;
        sBrightness = p.brightness;
        sWidth = p.width;
        sDuck = p.duckingDb;
        sVintage = p.style == kStyleVintage ? 1.0f : 0.0f;
        sFreeze = p.freeze ? 1.0f : 0.0f;
        mixCur = p.mix;
        for (int k = 0; k < kNumDecayBands; ++k)
            sBands[k] = p.decayBands[k];
        balanceGains (p.inPan, inGL, inGR);
        inGL *= dbToGain (p.inGainDb);
        inGR *= dbToGain (p.inGainDb);
        balanceGains (p.outPan, outGL, outGR);
        outGL *= dbToGain (p.outGainDb);
        outGR *= dbToGain (p.outGainDb);
        bypassCur = p.bypass ? 1.0f : 0.0f;
    }

    sSpace += (p.space - sSpace) * aSlow;
    sDecayRate = smoothLog (sDecayRate, clampv (p.decayRate, 0.25f, 4.0f), aMed);
    sCharacter += (p.character - sCharacter) * aMed;
    if (! p.freeze) // Pro-R: in Freeze, Distance and Thickness no longer affect the sound
    {
        sThickness += (p.thickness - sThickness) * aMed;
        sDistance += (p.distance - sDistance) * aMed;
    }
    sBrightness += (p.brightness - sBrightness) * aMed;
    sWidth += (p.width - sWidth) * aMed;
    sDuck += (p.duckingDb - sDuck) * aMed;
    sVintage += ((p.style == kStyleVintage ? 1.0f : 0.0f) - sVintage) * aSlow;
    if (std::abs (sVintage - (p.style == kStyleVintage ? 1.0f : 0.0f)) < 1.0e-4f)
        sVintage = p.style == kStyleVintage ? 1.0f : 0.0f;
    sFreeze += ((p.freeze ? 1.0f : 0.0f) - sFreeze) * smoothingAlpha (0.03, fs, n);
    {
        // slow enough that fading out the delay modulation never bends the pitch
        const float step = (float) n / (float) (0.8 * fs);
        freezeRamp = firstUpdate ? (p.freeze ? 1.0f : 0.0f)
                                 : (p.freeze ? std::min (1.0f, freezeRamp + step) : std::max (0.0f, freezeRamp - step));
    }
    const float freezeGlide = freezeRamp * freezeRamp * (3.0f - 2.0f * freezeRamp);

    for (int k = 0; k < kNumDecayBands; ++k)
    {
        const auto& t = p.decayBands[k];
        auto& c = sBands[k];
        if (t.used != c.used || t.shape != c.shape || ! c.active() || ! t.active())
        {
            c = t;
            continue;
        }
        c.freq = smoothLog (c.freq, t.freq, aMed);
        c.rate = smoothLog (c.rate, t.rate, aMed);
        c.q = smoothLog (c.q, t.q, aMed);
    }

    const int style = p.style;
    const bool plateStyle = style == kStylePlate;
    const int fdnStyle = style == kStyleVintage ? kStyleVintage : kStyleModern;

    updateTankActivity (n);

    // ------------------------------------------------------------- decay design
    const RoomState room = interpolateRoom (sSpace);
    {
        const DecaySettings ds = makeDecaySettings (sBands, sSpace, sDecayRate, sBrightness, style);
        currentT60 = ds.t60;
        bool specChanged = false;
        if (! haveSpec || decaySettingsDiffer (ds, lastDecay))
        {
            if (! haveSpec || --specCountdown <= 0)
            {
                spec = buildDecaySpec (ds, fs, referenceDelayFor (style, sSpace));
                lastDecay = ds;
                haveSpec = true;
                specChanged = true;
                specCountdown = 3;

                // the diffusers must not ring longer than the shortest decay of the spectrum,
                // otherwise e.g. a dark room (low Brightness) keeps ringing in the highs
                static const float probe[5] = { 250.0f, 1000.0f, 4000.0f, 8000.0f, 12000.0f };
                float t60s[5];
                decayTimeCurve (spec, fs, referenceDelayFor (style, sSpace), probe, t60s, 5);
                diffuserT60 = spec.t60;
                for (float t : t60s)
                    diffuserT60 = std::min (diffuserT60, std::max (0.05f, t));
            }
        }

        const float c = sCharacter;
        const float th = sThickness;
        // saturation only above Pro-R 0 % (0.5): Pro-R 2 measured linear at 0 %
        const float satAmt = std::max (0.0f, th - 0.5f) / 0.5f;
        const float freezeKeep = 1.0f - sFreeze;
        // Pro-R: Character can still be adjusted while frozen -> lossless movement of the frozen tail
        const float freezeMotion = c <= 0.5f ? 0.12f * std::pow (c / 0.5f, 1.3f) : 0.12f + 0.33f * std::pow ((c - 0.5f) / 0.5f, 2.0f);
        inDrive2 = (satAmt * 1.2f * freezeKeep) * (satAmt * 1.2f * freezeKeep);

        // width -> tank cross-feed + output side gain
        const float w = clampv (sWidth, 0.0f, 2.0f);
        const float crossfeed = widthTable (tuning().widthCross, w);

        if (fdnRunning)
        {
            if (fdnNeedsReset)
            {
                fdn.reset();
                fdnNeedsReset = false;
            }
            const bool vint = fdnStyle == kStyleVintage;
            FdnTank::Settings s;
            s.meanDelay = meanDelayForRoom (room, fdnStyle);
            if (vint)
            {
                s.apGain = (0.12f + 0.33f * th) * (1.0f - 0.2f * c);
                s.inDiffGain = 0.35f + 0.25f * th;
            }
            else
            {
                // density of the start (Pro-R 2: sparse first 50 ... 100 ms, Thickness makes it denser)
                s.apGain = clampv ((0.2f + 0.5f * th) * (1.0f - 0.3f * c) * Tuning::at (tuning().diffLoop, 5, th), 0.0f, 0.75f);
                s.inDiffGain = clampv ((0.45f + 0.27f * th) * (room.diffusion / 0.77f), 0.3f, 0.78f)
                               * clampv (Tuning::at (tuning().diffIn, 5, th) + Tuning::at (tuning().distDiffIn, 5, sDistance), 0.0f, 1.2f);
            }
            s.inDiffScale = clampv ((0.6f + 1.6f * std::sqrt (s.meanDelay / 0.035f)) * (1.0f + 1.2f * sDistance), 0.3f, 4.0f);

            // 0 % = no modulation (transparent), 50 % = clearly lively (a few cents), 100 % = chorus
            const Tuning& tu = tuning();
            float depth = c <= 0.5f ? std::pow (c / 0.5f, 1.3f) * tu.modDepth50
                                    : tu.modDepth50 + std::pow ((c - 0.5f) / 0.5f, 2.0f) * (tu.modDepth100 - tu.modDepth50);
            float rate = c <= 0.5f ? tu.modRate50 * (0.4f + 0.6f * c / 0.5f) : tu.modRate50 + (tu.modRate100 - tu.modRate50) * (c - 0.5f) / 0.5f;
            float rnd = 0.65f - 0.35f * std::max (0.0f, c - 0.5f) / 0.5f;
            if (vint)
            {
                depth = 0.0004f + depth * 1.3f;
                rate *= 0.8f;
                rnd = 0.8f;
            }
            s.modDepth = depth;
            s.modRate = rate;
            s.modRandom = rnd;
            s.crossfeed = crossfeed;
            s.saturation = satAmt * 2.2f * freezeKeep;
            s.freeze = p.freeze ? 1.0f : 0.0f;
            s.freezeGlide = freezeGlide;
            s.freezeMotion = freezeMotion;
            s.t60 = diffuserT60;
            // Modern only (Vintage was fitted without them); fewer below Thickness 0 %: airier, as Pro-R 2
            s.earlyTap = room.earlyTap * (1.0f - sVintage) * Tuning::at (tuning().thickTap, 5, sThickness);
            // build-up of the late reverb: longer for big rooms and far away (Distance)
            {
                const float sizeNorm = clampv (s.meanDelay / 0.035f, 0.15f, 2.0f);
                const float tb = Tuning::at (tuning().distBuild, 5, sDistance) * (0.012f + 0.11f * sizeNorm) * room.buildScale * (float) fs;
                const float d = sDistance;
                const float spread = d * d * (3.0f - 2.0f * d);
                s.spread = spread * tb;
                s.spreadMix = spread;
            }
            fdn.setSettings (s);
            fdn.update (n, spec, specChanged);
        }

        if (plateRunning)
        {
            if (plateNeedsReset)
            {
                for (auto& pt : plate)
                    pt.reset();
                plateNeedsReset = false;
            }
            PlateTank::Settings s;
            s.size = plateSizeForSpace (sSpace);
            s.inDiff1 = 0.6f + 0.2f * th;
            s.inDiff2 = 0.5f + 0.16f * th;
            s.decayDiff1 = 0.5f + 0.25f * th;
            s.decayDiff2 = 0.35f + 0.2f * th;
            const Tuning& tu = tuning();
            s.modDepth = (c <= 0.5f ? (c / 0.5f) * tu.modDepth50 : tu.modDepth50 + std::pow ((c - 0.5f) / 0.5f, 2.0f) * (tu.modDepth100 - tu.modDepth50)) * (0.0005f / 0.0007f);
            s.modRate = 0.6f + 0.9f * c;
            s.saturation = satAmt * 2.2f * freezeKeep;
            s.freeze = p.freeze ? 1.0f : 0.0f;
            s.preDiffScale = 1.0f + 1.5f * sDistance;
            s.earlyLevel = 0.45f * (1.0f - 0.6f * sDistance) * dbToGain (Tuning::styleAt (tu.styleEarlyDb[1], room.t60));
            s.t60 = diffuserT60;
            s.freezeGlide = freezeGlide;
            s.freezeMotion = freezeMotion;
            for (auto& pt : plate)
            {
                pt.setSettings (s);
                pt.update (n, spec, specChanged);
            }
        }

        // ------------------------------------------------------------- early reflections
        {
            // Pro-R 2: the same reflection timing in every room (only the level changes), first
            // reflection after 6.7 ms (Vintage 13.7 ms) -> one fixed geometry for all rooms
            EarlyReflections::Geometry g;
            g.dimX = kErDimX;
            g.dimY = kErDimY;
            g.dimZ = kErDimZ;
            g.onsetSec = style == kStyleVintage ? EarlyReflections::kVintageOnsetSeconds : EarlyReflections::kModernOnsetSeconds;
            g.distance = sDistance;
            g.character = c;
            g.beta = 0.82f;
            g.sparse = style == kStyleVintage;
            g.statistical = style == kStyleModern;
            g.extra = Tuning::at (tuning().thickExtra, 5, sThickness); // Thickness above 0 %: denser start
            g.thin = Tuning::at (tuning().thickThin, 5, sThickness);   // below 0 %: sparser start
            g.primaryDb = room.erPrimaryDb;
            g.t60 = currentT60;
            g.crossfeed = crossfeed;
            if (firstUpdate || ++erCounter >= 4) // rate limit (geometry is cheap but not free)
            {
                erCounter = 0;
                er.setGeometry (g);
                const float away = 1.0f - std::max (0.0f, 1.0f - std::abs (c - 0.5f) / 0.25f); // 0 at Character 50 %, 1 from 25 % away
                const float cutoff = clampv (room.erCutoff * Tuning::at (tuning().distErCut, 5, sDistance) * Tuning::at (tuning().brightErCut, Tuning::kBrightPoints, sBrightness)
                                                 * std::pow (tuning().charErCut, away),
                                             2000.0f, 22000.0f);
                er.setToneCutoff (cutoff);
            }

            const float levelCorr = dbToGain (levelCorrectionDb (style, sSpace));
            // Pro-R 2: the early reflections are 2 dB stronger at 50 % than at 0 / 25 / 75 / 100 %
            const float charEr = dbToGain (tuning().charErDb * (1.0f - std::max (0.0f, 1.0f - std::abs (c - 0.5f) / 0.25f)));
            const float erTone = dbToGain (Tuning::at (tuning().brightErDb, Tuning::kBrightPoints, sBrightness) + Tuning::at (tuning().thickErDb, 5, sThickness));
            float erTarget = plateStyle ? 0.0f : room.erGain * 0.65f * Tuning::at (tuning().distEr, 5, sDistance) * charEr * erTone * kErLevel * freezeKeep * levelCorr;
            if (style == kStyleVintage)
                erTarget *= 0.7f * dbToGain (Tuning::styleAt (tuning().styleEarlyDb[0], room.t60));
            if (firstUpdate)
                erLevel = erTarget;
            const float erNext = erLevel + (erTarget - erLevel) * aMed;
            erLevelInc = (erNext - erLevel) / (float) n;
            erFeed = plateStyle ? 0.0f : 0.25f + 0.25f * sDistance;

            const float t60n = clampv (currentT60 / 1.5f, 0.05f, 40.0f);
            float lateTarget = std::pow (t60n, -0.22f) * levelCorr * dbToGain (Tuning::at (tuning().distLateDb, 5, sDistance));
            if (p.freeze && ! firstUpdate) // 'eternal' decay: the frozen level stays where it was
                lateTarget = lateFrozen;
            lateFrozen = lateTarget;
            const float lateNext = firstUpdate ? lateTarget : lateGain + (lateTarget - lateGain) * aMed;
            if (firstUpdate)
                lateGain = lateTarget;
            lateGainInc = (lateNext - lateGain) / (float) n;
        }

        // ------------------------------------------------------------- late onset
        {
            // Pro-R 2: nothing arrives before the first reflection (6.7 ms), the late reverb neither
            const float target = plateStyle ? 0.0f : std::round (kLateOnsetSeconds * (float) fs);
            if (firstUpdate || std::abs (target - lateOnsetCur) < 0.01f)
            {
                lateOnsetCur = target;
                lateOnsetInc = 0.0f;
            }
            else
            {
                float nd = lateOnsetCur + (target - lateOnsetCur) * aMed;
                if (std::abs (target - nd) < 0.01f)
                    nd = target;
                lateOnsetInc = (nd - lateOnsetCur) / (float) n;
            }
        }

        // ------------------------------------------------------------- tone, width
        if (std::abs (sBrightness - lastTiltBrightness) > 0.002f)
        {
            const Tuning& tu = tuning();
            const float hsDb = Tuning::at (tu.brightHsDb, Tuning::kBrightPoints, sBrightness);
            const float lsDb = Tuning::at (tu.brightLsDb, Tuning::kBrightPoints, sBrightness);
            const float gDb = Tuning::at (tu.brightGainDb, Tuning::kBrightPoints, sBrightness);
            tiltLs = makeSvf (FilterShape::LowShelf, tu.brightLsFreq, 0.6, lsDb, fs);
            tiltHs = makeSvf (FilterShape::HighShelf, tu.brightHsFreq, 0.6, hsDb, fs);
            erTiltHs = makeSvf (FilterShape::HighShelf, tu.brightHsFreq, 0.6, hsDb + Tuning::at (tu.brightErHsDb, Tuning::kBrightPoints, sBrightness), fs);
            tiltGain = dbToGain (gDb);
            lastTiltBrightness = sBrightness;
        }

        const float sideTarget = widthTable (tuning().widthSide, w);
        const float sideNext = firstUpdate ? sideTarget : sideGain + (sideTarget - sideGain) * aMed;
        if (firstUpdate)
            sideGain = sideTarget;
        sideGainInc = (sideNext - sideGain) / (float) n;

        // wet level vs Decay Rate, Width, Thickness, Distance and Character, measured on Pro-R 2
        // (the Thickness / Distance part is held while frozen: they must not change the frozen sound)
        const float rx = clampv ((std::log2 (sDecayRate) + 2.0f) / 4.0f, 0.0f, 1.0f);
        const float away = 1.0f - std::max (0.0f, 1.0f - std::abs (sCharacter - 0.5f) / 0.25f); // 0 at Character 50 %
        if (! (p.freeze && ! firstUpdate))
            trimHeldDb = Tuning::at (tuning().thickLevelDb, 5, sThickness) + Tuning::at (tuning().distLevelDb, 5, sDistance);
        const float trimTarget = dbToGain (Tuning::at (tuning().rateLevelDb, 5, rx) + widthTable (tuning().widthLevelDb, w) + trimHeldDb + tuning().charLevelDb * away);
        const float trimNext = firstUpdate ? trimTarget : levelTrim + (trimTarget - levelTrim) * aMed;
        if (firstUpdate)
            levelTrim = trimTarget;
        levelTrimInc = (trimNext - levelTrim) / (float) n;

        // tone of the late reverb (room model, Modern only)
        // (Vintage, Plate: per style point, measured at three decay times)
        const int sk = style == kStylePlate ? 1 : 0;
        const float lsDb = style == kStyleModern ? room.lateLsDb : Tuning::styleAt (tuning().styleLsDb[sk], room.t60);
        const float hsDb = style == kStyleModern ? room.lateHsDb : Tuning::styleAt (tuning().styleHsDb[sk], room.t60);
        const float midDb = style == kStyleModern ? room.lateMidDb : 0.0f;
        // Decay Rate: Pro-R 2's late reverb is brighter at the top for short decays
        const float airDb = (style == kStyleModern ? room.lateAirDb : Tuning::styleAt (tuning().styleAirDb[sk], room.t60)) + Tuning::at (tuning().rateHsDb, 5, rx);
        if (std::abs (lsDb - lastLateLsDb) > 0.01f || std::abs (hsDb - lastLateHsDb) > 0.01f || std::abs (midDb - lastLateMidDb) > 0.01f
            || std::abs (airDb - lastLateAirDb) > 0.01f)
        {
            lateAir = makeSvf (FilterShape::HighShelf, std::min (10000.0, 0.4 * fs), 0.6, airDb, fs);
            lastLateAirDb = airDb;
            lateLs = makeSvf (FilterShape::LowShelf, 120.0, 0.6, lsDb, fs);
            lateHs = makeSvf (FilterShape::HighShelf, 2500.0, 0.6, hsDb, fs);
            lateMid = makeSvf (FilterShape::Bell, 300.0, 0.8, midDb, fs);
            lastLateLsDb = lsDb;
            lastLateHsDb = hsDb;
            lastLateMidDb = midDb;
        }
    }

    // ------------------------------------------------------------- predelay
    {
        const float target = clampv (p.predelayMs * 0.001f * (float) fs, 0.0f, (float) (kMaxPredelaySeconds * fs) - 4.0f);
        if (firstUpdate)
            pdCur = target;
        if (std::abs (target - pdCur) > 0.02f * (float) fs && pdXfade <= 0.0f)
        {
            // big jump (e.g. tempo sync change): crossfade instead of a pitch glide
            pdOld = pdCur;
            pdCur = target;
            pdXfade = 1.0f;
            pdXfadeStep = 1.0f / (float) (0.03 * fs);
            pdInc = 0.0f;
        }
        else
        {
            float next = pdCur + (target - pdCur) * smoothingAlpha (0.08, fs, n);
            next = pdCur + clampv (next - pdCur, -0.25f * (float) n, 0.25f * (float) n);
            pdInc = (next - pdCur) / (float) n;
        }
    }

    // ------------------------------------------------------------- gains
    {
        float gl, gr;
        balanceGains (p.inPan, gl, gr);
        const float ig = dbToGain (clampv (p.inGainDb, -60.0f, 36.0f));
        inGLInc = ((inGL + (gl * ig - inGL) * aFast) - inGL) / (float) n;
        inGRInc = ((inGR + (gr * ig - inGR) * aFast) - inGR) / (float) n;
        balanceGains (p.outPan, gl, gr);
        const float og = dbToGain (clampv (p.outGainDb, -60.0f, 36.0f));
        outGLInc = ((outGL + (gl * og - outGL) * aFast) - outGL) / (float) n;
        outGRInc = ((outGR + (gr * og - outGR) * aFast) - outGR) / (float) n;

        const float mixNext = mixCur + (clampv (p.mix, 0.0f, 1.0f) - mixCur) * aFast;
        mixInc = (mixNext - mixCur) / (float) n;

        const float bpTarget = p.bypass ? 1.0f : 0.0f;
        const float bpStep = (float) n / (float) (0.02 * fs);
        const float bpNext = bpTarget > bypassCur ? std::min (bpTarget, bypassCur + bpStep) : std::max (bpTarget, bypassCur - bpStep);
        bypassInc = (bpNext - bypassCur) / (float) n;
    }

    postEq.setParams (p.postBands, p.eqAutoGain);
    postEq.update (n);

    firstUpdate = false;
}

//==============================================================================
void ReverbEngine::process (float* L, float* R, int numSamples) noexcept
{
    peak[0] = peak[1] = 0.0f;
    anaLength = std::min (numSamples, (int) anaPre.size());
    int offset = 0;
    while (offset < numSamples)
    {
        const int n = std::min (kControlBlock, numSamples - offset);
        processChunk (L + offset, R + offset, n, offset);
        offset += n;
    }
}

void ReverbEngine::processChunk (float* L, float* R, int n, int offset) noexcept
{
    controlUpdate (n);

    float origL[kControlBlock], origR[kControlBlock];
    float xL[kControlBlock], xR[kControlBlock];
    float pdL[kControlBlock], pdR[kControlBlock];
    float erL[kControlBlock], erR[kControlBlock];
    float inL[kControlBlock], inR[kControlBlock];
    float tA[kControlBlock], tB[kControlBlock], tC[kControlBlock], tD[kControlBlock];
    float wetL[kControlBlock], wetR[kControlBlock];

    // ---- input gain / pan + detector
    for (int s = 0; s < n; ++s)
    {
        origL[s] = L[s];
        origR[s] = R[s];
        inGL += inGLInc;
        inGR += inGRInc;
        xL[s] = L[s] * inGL;
        xR[s] = R[s] * inGR;
        const float a = std::max (std::abs (xL[s]), std::abs (xR[s]));
        env = a > env ? env + (a - env) * envAtt : env * envRel;
    }
    if (! (env < 1.0e6f)) // NaN / inf protection of the detector
        env = 0.0f;

    // ---- dynamics (ducking + auto gate), chunk rate
    {
        const float envDb = gainToDb (env);
        const float dt = (float) n / (float) fs;
        refDb = envDb > refDb ? envDb : std::max (-100.0f, refDb - 9.0f * dt);

        float activity = clampv ((envDb - (refDb - 30.0f)) / 24.0f, 0.0f, 1.0f);
        if (envDb < -66.0f)
            activity *= clampv ((envDb + 80.0f) / 14.0f, 0.0f, 1.0f);
        const float duckTarget = -sDuck * activity;
        const float aD = duckTarget < duckDb ? smoothingAlpha (0.012, fs, n) : smoothingAlpha (0.22, fs, n);
        duckDb += (duckTarget - duckDb) * aD;
        duckGainInc = (dbToGain (duckDb) - duckGain) / (float) n;

        const float holdSamples = std::max (1.0f, params.gateHoldMs) * 0.001f * (float) fs;
        const float thrOpen = std::max (refDb - 34.0f, -62.0f);
        const float thrClose = thrOpen - 6.0f;
        if (envDb > thrOpen)
        {
            gateOpen = true;
            gateHoldCounter = (int) holdSamples;
        }
        else if (envDb > thrClose && gateOpen)
        {
            gateHoldCounter = (int) holdSamples;
        }
        else
        {
            if (gateHoldCounter > 0)
                gateHoldCounter -= n;
            else
                gateOpen = false;
        }
        const float gateTarget = params.gateOn ? (gateOpen ? 1.0f : 0.0f) : 1.0f;
        const double release = clampv (0.25 * holdSamples / fs, 0.03, 0.35);
        const float aG = gateTarget > gateGain ? smoothingAlpha (0.002, fs, n) : smoothingAlpha (release, fs, n);
        const float gNext = gateGain + (gateTarget - gateGain) * aG;
        gateGainInc = (gNext - gateGain) / (float) n;
    }

    // ---- predelay
    for (int s = 0; s < n; ++s)
    {
        predelay[0].push (xL[s]);
        predelay[1].push (xR[s]);
        pdCur += pdInc;
        float a = predelay[0].tapLinear (pdCur + 1.0f);
        float b = predelay[1].tapLinear (pdCur + 1.0f);
        if (pdXfade > 0.0f)
        {
            const float oa = predelay[0].tapLinear (pdOld + 1.0f);
            const float ob = predelay[1].tapLinear (pdOld + 1.0f);
            a = a + (oa - a) * pdXfade;
            b = b + (ob - b) * pdXfade;
            pdXfade = std::max (0.0f, pdXfade - pdXfadeStep);
        }
        pdL[s] = a;
        pdR[s] = b;
    }

    // ---- Thickness dynamics: level-dependent gain of the reverb input (detector on the predelayed
    // input; ER and late reverb start >= 4.5 ms later, which works like a short look-ahead)
    float thick[kControlBlock];
    {
        const float tv = clampv ((sThickness - 0.5f) * 2.0f, -1.0f, 1.0f);
        if (std::abs (tv) < 1.0e-3f)
        {
            for (int s = 0; s < n; ++s)
            {
                const float d = thickLp.process (0.5f * (pdL[s] + pdR[s]), thickLpCoefs);
                const float pw = d * d;
                thickEnv += (pw - thickEnv) * (pw > thickEnv ? thickAtt : thickRel);
                thick[s] = 1.0f;
            }
        }
        else
        {
            const Tuning& tu = tuning();
            const float slope = tv > 0.0f ? tu.thickSlopePos * tv : tu.thickSlopeNeg * -tv;
            const float stat = (tv > 0.0f ? tu.thickStaticPos : tu.thickStaticNeg) * tv * tv;
            const float pivot = tu.thickPivotDb;
            for (int s = 0; s < n; ++s)
            {
                const float d = thickLp.process (0.5f * (pdL[s] + pdR[s]), thickLpCoefs);
                const float pw = d * d;
                thickEnv += (pw - thickEnv) * (pw > thickEnv ? thickAtt : thickRel);
                const float levelDb = 10.0f * std::log10 (thickEnv + 1.0e-12f);
                thick[s] = dbToGain (clampv (stat + slope * (levelDb - pivot), -36.0f, 24.0f));
            }
        }
        if (! (thickEnv < 1.0e6f))
            thickEnv = 0.0f;
    }

    // ---- early reflections
    if (std::abs (erLevel) <= 0.0f && std::abs (erLevelInc) <= 0.0f) // silent (Plate): keep the input history only
    {
        er.feed (pdL, pdR, n);
        for (int s = 0; s < n; ++s)
            erL[s] = erR[s] = 0.0f;
    }
    else
        er.process (pdL, pdR, erL, erR, n);
    for (int s = 0; s < n; ++s)
    {
        erL[s] *= thick[s];
        erR[s] *= thick[s];
    }

    // ---- late reverb input: build-up taps + ER feed + drive + vintage band limit
    const float vint = sVintage;
    if (vint > 0.0f && ! vintActive)
    {
        for (auto& ch : vintState)
            for (auto& f : ch)
                f.reset();
        for (auto& f : vintInState)
            f.reset();
    }
    vintActive = vint > 0.0f;
    for (int s = 0; s < n; ++s)
    {
        buildBuf[0].push (pdL[s]);
        buildBuf[1].push (pdR[s]);
        lateOnsetCur += lateOnsetInc;
        float il = buildBuf[0].tapLinear (lateOnsetCur + 1.0f);
        float ir = buildBuf[1].tapLinear (lateOnsetCur + 1.0f);
        il = il * thick[s] + erFeed * erL[s]; // the ER output already carries the Thickness gain
        ir = ir * thick[s] + erFeed * erR[s];
        if (inDrive2 > 1.0e-6f)
        {
            il = passiveSaturate (il, inDrive2);
            ir = passiveSaturate (ir, inDrive2);
        }
        if (vint > 0.0f)
        {
            const float fl = vintInState[0].process (il, vintInLp);
            const float fr = vintInState[1].process (ir, vintInLp);
            il += vint * (fl - il);
            ir += vint * (fr - ir);
        }
        inL[s] = il;
        inR[s] = ir;
    }

    // ---- tanks
    for (int s = 0; s < n; ++s)
        wetL[s] = wetR[s] = 0.0f;

    if (fdnRunning)
    {
        const float g = fdnInGain;
        for (int s = 0; s < n; ++s)
        {
            tA[s] = inL[s] * g;
            tB[s] = inR[s] * g;
        }
        fdn.process (tA, tB, tC, tD, n);
        const float og = fdnOutGain * kFdnLevel;
        for (int s = 0; s < n; ++s)
        {
            wetL[s] += tC[s] * og;
            wetR[s] += tD[s] * og;
        }
    }

    if (plateRunning)
    {
        const float crossfeed = widthTable (tuning().widthCross, sWidth);
        const float cf = 0.5f * crossfeed;
        const float g = plateInGain;
        for (int s = 0; s < n; ++s)
        {
            tA[s] = ((1.0f - cf) * inL[s] + cf * inR[s]) * g;
            tB[s] = ((1.0f - cf) * inR[s] + cf * inL[s]) * g;
        }
        float pl[kControlBlock], pr[kControlBlock];
        plate[0].process (tA, tC, pr, n); // left tank -> left output
        plate[1].process (tB, pl, tD, n); // right tank -> right output
        const float og = plateOutGain * kPlateLevel;
        for (int s = 0; s < n; ++s)
        {
            wetL[s] += tC[s] * og;
            wetR[s] += tD[s] * og;
        }
    }

    // ---- wet sum, tone, width
    for (int s = 0; s < n; ++s)
    {
        lateGain += lateGainInc;
        erLevel += erLevelInc;
        sideGain += sideGainInc;
        levelTrim += levelTrimInc;
        const float lateL = lateToneState[0][3].process (
            lateToneState[0][2].process (lateToneState[0][1].process (lateToneState[0][0].process (wetL[s], lateLs), lateHs), lateMid), lateAir);
        const float lateR = lateToneState[1][3].process (
            lateToneState[1][2].process (lateToneState[1][1].process (lateToneState[1][0].process (wetR[s], lateLs), lateHs), lateMid), lateAir);
        // Brightness: the high shelf acts on the late reverb and (with its own gain) on the reflections
        float l = tiltState[0][1].process (lateL * lateGain, tiltHs) + erTiltState[0].process (erL[s] * erLevel, erTiltHs);
        float r = tiltState[1][1].process (lateR * lateGain, tiltHs) + erTiltState[1].process (erR[s] * erLevel, erTiltHs);

        l = levelTrim * tiltGain * tiltState[0][0].process (l, tiltLs);
        r = levelTrim * tiltGain * tiltState[1][0].process (r, tiltLs);

        if (vint > 0.0f)
        {
            const float fl = vintState[0][1].process (vintState[0][0].process (l, vintLp[0]), vintLp[1]);
            const float fr = vintState[1][1].process (vintState[1][0].process (r, vintLp[0]), vintLp[1]);
            l += vint * (fl - l);
            r += vint * (fr - r);
        }

        const float mid = 0.5f * (l + r);
        const float side = 0.5f * (l - r) * sideGain;
        wetL[s] = mid + side;
        wetR[s] = mid - side;
    }

    // ---- post EQ (incl. automatic gain compensation)
    postEq.process (wetL, wetR, n);

    // ---- ducking / gate, mix, output, bypass
    const float m0 = mixCur;
    const float m1 = mixCur + mixInc * (float) n;
    const float d0 = std::cos (m0 * kPiF * 0.5f), w0 = std::sin (m0 * kPiF * 0.5f);
    const float d1 = std::cos (m1 * kPiF * 0.5f), w1 = std::sin (m1 * kPiF * 0.5f);
    const float dInc = (d1 - d0) / (float) n, wInc = (w1 - w0) / (float) n;
    float dg = d0, wg = w0;
    mixCur = m1;

    const bool record = offset + n <= (int) anaPre.size();

    for (int s = 0; s < n; ++s)
    {
        duckGain += duckGainInc;
        gateGain += gateGainInc;
        dg += dInc;
        wg += wInc;
        const float dyn = duckGain * gateGain;
        const float wl = wetL[s] * dyn;
        const float wr = wetR[s] * dyn;

        outGL += outGLInc;
        outGR += outGRInc;
        float ol = (xL[s] * dg + wl * wg) * outGL;
        float orr = (xR[s] * dg + wr * wg) * outGR;

        bypassCur += bypassInc;
        if (bypassCur > 0.0f)
        {
            ol += bypassCur * (origL[s] - ol);
            orr += bypassCur * (origR[s] - orr);
        }

        // last line of defence against numerical problems
        if (! (std::abs (ol) < 1.0e4f))
            ol = 0.0f;
        if (! (std::abs (orr) < 1.0e4f))
            orr = 0.0f;

        L[s] = ol;
        R[s] = orr;
        peak[0] = std::max (peak[0], std::abs (ol));
        peak[1] = std::max (peak[1], std::abs (orr));

        if (record)
        {
            anaPre[(size_t) (offset + s)] = 0.5f * (xL[s] + xR[s]);
            anaWet[(size_t) (offset + s)] = 0.5f * (wl + wr);
            anaOut[(size_t) (offset + s)] = 0.5f * (ol + orr);
        }
    }
    bypassCur = clampv (bypassCur, 0.0f, 1.0f);
    gateGain = clampv (gateGain, 0.0f, 1.0f);
}

} // namespace nh
