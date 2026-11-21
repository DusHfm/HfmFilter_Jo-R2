#include "DecayDesign.h"
#include "Tuning.h"

namespace nh
{
namespace
{
    constexpr double kMinS = 0.12; // decay can be at most ~8x longer than T60mid in a band

    FilterShape shapeForBand (int shape) noexcept
    {
        switch (shape)
        {
            case kDecayLowShelf: return FilterShape::LowShelf;
            case kDecayHighShelf: return FilterShape::HighShelf;
            case kDecayNotch: return FilterShape::Notch;
            case kDecayBell:
            default: return FilterShape::Bell;
        }
    }

    void fillUserStages (DecaySpec& spec, const DecayBandParams* bands)
    {
        for (int b = 0; b < kNumDecayBands; ++b)
        {
            auto& st = spec.stages[kNumRoomDecayStages + b];
            const auto& p = bands[b];
            st = DecayStage();
            if (! p.active())
                continue;
            st.active = true;
            st.shape = shapeForBand (p.shape);
            st.freq = clampv (p.freq, 10.0f, 30000.0f);
            st.q = clampv (p.q, 0.025f, 40.0f);
            st.fixedNotch = (p.shape == kDecayNotch);
            st.u = st.fixedNotch ? 0.0f : 1.0f / clampv (p.rate, kMinBandRate, kMaxBandRate) - 1.0f;
            if (p.shape != kDecayBell && ! st.fixedNotch)
            {
                // shelves: placed and shaped like the ones of Pro-R 2 (fitted to its recordings)
                const auto& t = tuning();
                st.freq = clampv (st.freq * (p.shape == kDecayLowShelf ? t.decayLowShelfFreq : t.decayHighShelfFreq), 10.0f, 30000.0f);
                st.q = clampv (st.q * t.decayShelfQ, 0.1f, 4.0f); // keep the slope sensible
            }
        }
    }

    // normalised dB shape (0..1) of a stage at tan(pi f / fs)
    double stageShape (const DecayStage& st, double c, double fs, double tanW) noexcept
    {
        double gRef = c * st.u;
        if (std::abs (gRef) < 0.01)
            gRef = gRef < 0.0 ? -0.01 : 0.01;
        const auto co = makeSvf (st.shape, st.freq, st.q, gRef, fs);
        const double db = 10.0 * std::log10 (std::max (co.magnitudeSqFromTan (tanW), 1.0e-30));
        return clampv (db / gRef, 0.0, 1.5);
    }

    // The user bands act multiplicatively on the room's natural decay curve:
    // scale each band by the room curve S_room at the band's characteristic frequency.
    void makeUserBandsRelative (DecaySpec& spec, double fs, double refDelay)
    {
        const double c = -60.0 * refDelay / std::max (0.01f, spec.t60);
        for (int b = 0; b < kNumDecayBands; ++b)
        {
            auto& st = spec.stages[kNumRoomDecayStages + b];
            if (! st.active || st.fixedNotch)
                continue;
            double fChar = st.freq;
            if (st.shape == FilterShape::LowShelf)
                fChar *= 0.5;
            else if (st.shape == FilterShape::HighShelf)
                fChar *= 2.0;
            fChar = clampv (fChar, 10.0, fs * 0.45);
            const double tanW = std::tan (kPi * fChar / fs);
            double sRoom = 1.0;
            for (int r = 0; r < kNumRoomDecayStages; ++r)
            {
                const auto& rs = spec.stages[r];
                if (rs.active && std::abs (rs.u) > 1.0e-6f)
                    sRoom += rs.u * stageShape (rs, c, fs, tanW);
            }
            st.u *= (float) std::max (sRoom, 0.05);
        }
    }

    void applyGuard (DecaySpec& spec, double fs, double refDelay)
    {
        const double c = -60.0 * refDelay / std::max (0.01f, spec.t60);

        constexpr int kGrid = 56;
        constexpr int kMaxPts = kGrid + kMaxDecayStages;
        double tanW[kMaxPts];
        int numPts = 0;
        const double fmax = std::min (20000.0, fs * 0.45);
        for (int i = 0; i < kGrid; ++i)
        {
            const double f = 20.0 * std::pow (fmax / 20.0, (double) i / (kGrid - 1));
            tanW[numPts++] = std::tan (kPi * f / fs);
        }
        for (int s = 0; s < kMaxDecayStages; ++s)
            if (spec.stages[s].active && ! spec.stages[s].fixedNotch)
                tanW[numPts++] = std::tan (kPi * std::min ((double) spec.stages[s].freq, fs * 0.49) / fs);

        double cut[kMaxPts], boost[kMaxPts];
        for (int j = 0; j < numPts; ++j)
        {
            cut[j] = 1.0;
            boost[j] = 0.0;
        }

        bool anyBoost = false;
        for (int s = 0; s < kMaxDecayStages; ++s)
        {
            const auto& st = spec.stages[s];
            if (! st.active || st.fixedNotch || std::abs (st.u) < 1.0e-6f)
                continue;
            double gRef = c * st.u;
            if (std::abs (gRef) < 0.01)
                gRef = gRef < 0.0 ? -0.01 : 0.01;
            const auto co = makeSvf (st.shape, st.freq, st.q, gRef, fs);
            for (int j = 0; j < numPts; ++j)
            {
                const double db = 10.0 * std::log10 (std::max (co.magnitudeSqFromTan (tanW[j]), 1.0e-30));
                const double shape = clampv (db / gRef, 0.0, 1.5);
                const double contrib = st.u * shape;
                if (st.u > 0.0f)
                    cut[j] += contrib;
                else
                {
                    boost[j] += contrib;
                    anyBoost = true;
                }
            }
        }

        if (! anyBoost)
            return;

        double lambda = 1.0;
        for (int j = 0; j < numPts; ++j)
        {
            if (boost[j] < 0.0 && cut[j] + boost[j] < kMinS)
                lambda = std::min (lambda, std::max (0.0, (cut[j] - kMinS) / -boost[j]));
        }

        if (lambda < 1.0)
            for (int s = 0; s < kMaxDecayStages; ++s)
                if (spec.stages[s].active && ! spec.stages[s].fixedNotch && spec.stages[s].u < 0.0f)
                    spec.stages[s].u *= (float) lambda;
    }
} // namespace

DecaySpec buildDecaySpec (const DecaySettings& s, double fs, double refDelay)
{
    DecaySpec spec;
    spec.t60 = std::max (0.01f, s.t60);

    auto& bass = spec.stages[0];
    bass.active = true;
    bass.shape = FilterShape::LowShelf;
    bass.freq = s.bassFreq;
    bass.q = 0.6f;
    bass.u = 1.0f / clampv (s.bassRatio, 0.2f, 4.0f) - 1.0f;

    // HF absorption as two staggered shelves: a steeper transition keeps the mid-band decay
    // (the displayed decay time) unchanged, even for very dark settings
    const float hfTotal = 1.0f / clampv (s.hfRatio, 0.03f, 4.0f) - 1.0f;
    auto& hf = spec.stages[1];
    hf.active = true;
    hf.shape = FilterShape::HighShelf;
    hf.freq = s.hfFreq;
    hf.q = 0.7f;
    hf.u = 0.3f * hfTotal;

    auto& hf2 = spec.stages[2];
    hf2.active = true;
    hf2.shape = FilterShape::HighShelf;
    hf2.freq = (float) std::min ((double) s.hfFreq * 1.9, fs * 0.42);
    hf2.q = 0.7f;
    hf2.u = 0.7f * hfTotal;

    auto& air = spec.stages[3];
    air.active = true;
    air.shape = FilterShape::HighShelf;
    air.freq = std::min ((double) s.airFreq, fs * 0.42);
    air.q = 0.6f;
    air.u = 1.0f / clampv (s.airRatio, 0.03f, 4.0f) - 1.0f;

    // rate domain: 1/T(f) = 1/Troom(f) + A(f)  ->  S(f) += T60mid * A(f)
    auto& absorb = spec.stages[4];
    absorb.active = s.airAbsRate > 0.0f;
    absorb.shape = FilterShape::HighShelf;
    absorb.freq = std::min ((double) s.airAbsFreq, fs * 0.42);
    absorb.q = clampv (s.airAbsQ, 0.1f, 2.0f);
    absorb.u = spec.t60 * s.airAbsRate;

    fillUserStages (spec, s.bands);
    makeUserBandsRelative (spec, fs, refDelay);
    applyGuard (spec, fs, refDelay);
    return spec;
}

DecaySpec buildUserDecaySpec (const DecayBandParams* bands, double fs, double refDelay)
{
    DecaySpec spec;
    spec.t60 = 1.5f;
    for (int i = 0; i < kNumRoomDecayStages; ++i)
        spec.stages[i] = DecayStage();
    fillUserStages (spec, bands);
    applyGuard (spec, fs, refDelay);
    return spec;
}

void designSegmentFilters (const DecaySpec& spec, double fs, double delaySeconds, SvfCoefs* coefs, float& gain) noexcept
{
    double c = -60.0 * delaySeconds / std::max (0.01f, spec.t60);
    c = clampv (c, -96.0, -1.0e-7);
    gain = (float) std::pow (10.0, c / 20.0);

    for (int s = 0; s < kMaxDecayStages; ++s)
    {
        const auto& st = spec.stages[s];
        if (! st.active)
        {
            coefs[s] = SvfCoefs::makeIdentity();
            continue;
        }
        if (st.fixedNotch)
        {
            coefs[s] = makeSvf (FilterShape::Notch, st.freq, st.q, 0.0, fs);
            continue;
        }
        const double db = clampv (c * (double) st.u, -80.0, 60.0);
        coefs[s] = std::abs (db) < 1.0e-5 ? SvfCoefs::makeIdentity() : makeSvf (st.shape, st.freq, st.q, db, fs);
    }
}

void decayTimeCurve (const DecaySpec& spec, double fs, double refDelay, const float* freqs, float* t60Out, int numPoints)
{
    SvfCoefs co[kMaxDecayStages];
    float gain = 1.0f;
    designSegmentFilters (spec, fs, refDelay, co, gain);
    const double gainDb = 20.0 * std::log10 (std::max (gain, 1.0e-12f));

    for (int i = 0; i < numPoints; ++i)
    {
        const double tanW = std::tan (kPi * std::min ((double) freqs[i], fs * 0.4999) / fs);
        double db = gainDb;
        for (int s = 0; s < kMaxDecayStages; ++s)
            if (spec.stages[s].active)
                db += 10.0 * std::log10 (std::max (co[s].magnitudeSqFromTan (tanW), 1.0e-30));
        db = std::min (db, -1.0e-7);
        t60Out[i] = (float) clampv (-60.0 * refDelay / db, 0.0, 1000.0);
    }
}

} // namespace nh
