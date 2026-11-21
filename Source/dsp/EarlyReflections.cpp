#include "EarlyReflections.h"

namespace nh
{
namespace
{
    constexpr float kSpeedOfSound = 343.0f;
    constexpr float kMaxErSeconds = 0.32f;

    // Image source coordinate along one axis (walls at 0 and L), m = signed reflection index
    inline float imageCoord (int m, float L, float s) noexcept
    {
        if ((m & 1) == 0)
            return (float) m * L + s;
        return (float) (m + 1) * L - s;
    }

    struct ImageIndex
    {
        int x, y, z;
    };

    // thread-safe one-time initialisation (C++11 "magic static"): several plug-in instances may
    // reach this from different audio threads at the same time
    const ImageIndex* imageTable() noexcept
    {
        static const std::array<ImageIndex, EarlyReflections::kNumImages> table = []
        {
            std::array<ImageIndex, EarlyReflections::kNumImages> t {};
            size_t n = 0;
            for (int mx = -2; mx <= 2; ++mx)
                for (int my = -2; my <= 2; ++my)
                    for (int mz = -2; mz <= 2; ++mz)
                    {
                        const int order = std::abs (mx) + std::abs (my) + std::abs (mz);
                        if (order >= 1 && order <= 2)
                            t[n++] = { mx, my, mz };
                    }
            // axial 3rd and 4th order images: discrete "late echoes" (flutter between walls)
            const int axial[] = { -4, -3, 3, 4 };
            for (int a : axial)
            {
                t[n++] = { a, 0, 0 };
                t[n++] = { 0, a, 0 };
                t[n++] = { 0, 0, a };
            }
            return t;
        }();
        return table.data();
    }
} // namespace

void EarlyReflections::prepare (double sampleRate)
{
    fs = sampleRate;
    maxDelaySamples = (float) (kMaxErSeconds * fs) + 4.0f;
    for (auto& b : buf)
        b.prepare ((int) maxDelaySamples + 16);
    smoothA = smoothingAlpha (0.08, fs, 32);
    setToneCutoff (12000.0f);
    reset();
}

void EarlyReflections::reset()
{
    for (auto& b : buf)
        b.reset();
    for (auto& ch : lp)
        for (auto& f : ch)
            f.reset();
    firstGeometry = true;
}

void EarlyReflections::setToneCutoff (float hz)
{
    lpCoefs = makeSvf (FilterShape::LowPass, std::min ((double) hz, fs * 0.45), 0.5412, 0.0, fs);
}

void EarlyReflections::setGeometry (const Geometry& g)
{
    (void) imageTable();

    const float Lx = clampv (g.dimX, 2.0f, 60.0f);
    const float Ly = clampv (g.dimY, 2.0f, 45.0f);
    const float Lz = clampv (g.dimZ, 2.0f, 25.0f);

    const float lx = 0.5f * Lx;
    const float ly = 0.28f * Ly;
    const float lz = std::min (1.5f, 0.45f * Lz);

    const float maxSrcDist = std::max (1.0f, 0.62f * Ly - ly);
    const float srcDist = lerpf (0.7f, std::min (maxSrcDist, std::max (1.2f, 0.42f * Ly)), clampv (g.distance, 0.0f, 1.0f));
    const float spread = std::min (0.16f * Lx, 1.0f + 0.6f * srcDist);
    const float sz = std::min (lz + 0.25f, Lz * 0.6f);

    const float echoGain = g.character <= 0.5f ? (g.character / 0.5f) : 1.0f + 0.8f * (g.character - 0.5f) / 0.5f;
    const float maxDelaySec = kMaxErSeconds - 0.005f;

    int t = 0;
    double energy = 0.0;
    const ImageIndex* table = imageTable();

    if (g.statistical)
    {
        // Statistical pattern. The recordings of Pro-R 2 show a few strong reflections in the first
        // ~20 ms and then a train of single reflections of similar level (about one per millisecond
        // between 20 and 100 ms) at the same times in every room; only their levels change with
        // Distance and the decay of the room. The times here are random (fixed seed); the rate of
        // the train grows with time like in a real room.
        constexpr int kPrimary = 4, kTrain = 112, kExtra = 8;
        static_assert (kPrimary + kTrain + kExtra == kStatPerPath, "pattern size");
        const float onset = std::max (0.001f, g.onsetSec);
        const float trainStart = 0.003f, trainSpan = 0.089f;
        const float d = clampv (g.distance, 0.0f, 1.0f);
        const float extra = clampv (g.extra, 0.0f, 2.0f);
        const float thin = clampv (g.thin, 0.0f, 0.95f);
        uint32_t seed = 0x2C1B3C6Du;
        auto rnd = [&seed]
        {
            seed = seed * 1664525u + 1013904223u;
            return (float) (seed >> 8) / 16777216.0f;
        };
        for (int src = 0; src < 2; ++src)
            for (int ear = 0; ear < 2; ++ear)
                for (int k = 0; k < kStatPerPath; ++k)
                {
                    float tsec, levelDb;
                    if (k < kPrimary) // strong first reflections
                    {
                        tsec = k == 0 ? onset : onset + 0.0018f + 0.0115f * ((float) (k - 1) + rnd()) / (float) (kPrimary - 1);
                        levelDb = -2.5f * (float) k - 2.0f * rnd();
                    }
                    else if (k < kPrimary + kTrain) // train of single reflections up to ~100 ms
                    {
                        const float u = ((float) (k - kPrimary) + rnd()) / (float) kTrain;
                        tsec = onset + trainStart + trainSpan * std::pow (u, 1.0f / 1.6f);
                        // fades with the room: Pro-R 2's single reflections stay nearly level in a hall
                        // and fade within ~50 ms in the smallest rooms
                        levelDb = -13.0f - 60.0f * (tsec - onset) / std::max (0.1f, g.t60) - 5.0f * (rnd() + rnd()) + 5.0f;
                    }
                    else // denser start (Thickness above 0 %)
                    {
                        tsec = onset + 0.003f + 0.022f * ((float) (k - kPrimary - kTrain) + rnd()) / (float) kExtra;
                        levelDb = -8.0f - 4.0f * rnd();
                    }
                    if (src != ear) // the other side: a little later and weaker
                        tsec += 0.0004f + 0.0012f * rnd();
                    float gain = dbToGain (levelDb);
                    // Distance: close = the first reflections dominate, far = the later ones get stronger
                    gain *= k < kPrimary ? 1.25f - 0.5f * d : 0.35f + 1.3f * d;
                    if (k >= kPrimary + kTrain)
                        gain *= extra;
                    else if (k >= kPrimary && thin > 0.0f)
                    {
                        // own hash (the random sequence above stays the same): which ones are left out
                        uint32_t h = (uint32_t) k * 2654435761u ^ (uint32_t) (src * 2 + ear + 1) * 0x9E3779B9u;
                        h ^= h >> 13;
                        h *= 0xC2B2AE35u;
                        h ^= h >> 16;
                        if ((float) h * (1.0f / 4294967296.0f) < thin)
                            gain = 0.0f;
                    }
                    if (rnd() < 0.5f)
                        gain = -gain;
                    Tap& tp = taps[t++];
                    tp.src = src;
                    tp.ear = ear;
                    tp.primary = k < kPrimary;
                    // whole samples: an interpolated read would damp the highs (-3 dB at 16 kHz)
                    tp.targetDelay = std::min (std::round (tsec * (float) fs) + 1.0f, maxDelaySamples - 2.0f);
                    tp.targetGain = gain;
                    energy += (double) gain * gain;
                }
    }
    else
    for (int src = 0; src < 2; ++src)
    {
        const float sx = clampv (lx + (src == 0 ? -spread : spread), 0.2f, Lx - 0.2f);
        const float sy = clampv (ly + srcDist, 0.2f, Ly - 0.2f);

        for (int im = 0; im < kNumImages; ++im)
        {
            const auto idx = table[im];
            const int order = std::abs (idx.x) + std::abs (idx.y) + std::abs (idx.z);
            const bool lateEcho = im >= 24;
            const float ix = imageCoord (idx.x, Lx, sx);
            const float iy = imageCoord (idx.y, Ly, sy);
            const float iz = imageCoord (idx.z, Lz, sz);

            const float cx = ix - lx, cy = iy - ly, cz = iz - lz;
            const float rc = std::sqrt (cx * cx + cy * cy + cz * cz) + 1.0e-3f;
            const float sinAz = cx / rc;

            for (int ear = 0; ear < 2; ++ear)
            {
                Tap& tp = taps[t++];
                tp.src = src;
                tp.ear = ear;
                tp.primary = false;

                const float ex = lx + (ear == 0 ? -0.09f : 0.09f);
                const float dxs = sx - ex, dys = sy - ly, dzs = sz - lz;
                const float direct = std::sqrt (dxs * dxs + dys * dys + dzs * dzs);
                const float dx = ix - ex, dy = iy - ly, dz = iz - lz;
                const float r = std::sqrt (dx * dx + dy * dy + dz * dz);

                const float delaySec = (r - direct) / kSpeedOfSound;
                float gain = std::pow (g.beta, (float) order) * std::min (1.0f, direct / std::max (r, 0.1f)) * 1.6f;
                gain *= std::sqrt (0.5f * (1.0f + (ear == 0 ? -0.72f : 0.72f) * sinAz));

                if (lateEcho)
                    gain *= 0.85f * echoGain;
                if (g.sparse && ! lateEcho && order == 2 && (idx.x == 0 || idx.y == 0 || idx.z == 0) && (im & 1))
                    gain = 0.0f;

                if (delaySec <= 0.0005f || delaySec >= maxDelaySec)
                {
                    gain = 0.0f;
                    tp.targetDelay = std::max (2.0f, clampv (delaySec, 0.001f, maxDelaySec) * (float) fs);
                }
                else
                {
                    tp.targetDelay = delaySec * (float) fs + 1.0f;
                }
                tp.targetGain = gain;
                if (! lateEcho)
                    energy += (double) gain * gain;
            }
        }
    }

    // taps beyond t are silent (they glide out if they were in use before)
    for (int i = t; i < kMaxTaps; ++i)
    {
        taps[i].targetGain = 0.0f;
        if (firstGeometry)
            taps[i].targetDelay = 2.0f;
    }
    numTaps = firstGeometry ? t : std::max (t, numTaps);

    // fixed start of the pattern: shift all reflections so that the earliest one arrives at onsetSec
    if (g.onsetSec > 0.0f && ! g.statistical)
    {
        float first = 1.0e9f;
        for (int i = 0; i < t; ++i)
            if (taps[i].targetGain > 0.0f)
                first = std::min (first, taps[i].targetDelay);
        const float shift = g.onsetSec * (float) fs + 1.0f - first;
        if (first < 1.0e8f && shift > 0.0f)
            for (int i = 0; i < t; ++i)
                taps[i].targetDelay = std::min (taps[i].targetDelay + shift, maxDelaySamples - 2.0f);
    }

    // Normalise the reflection energy (without the optional late echoes) to a constant level.
    // Cross-ear reflections (left source -> right ear and vice versa) follow the cross-feed of the
    // Stereo Width control; same-side taps are scaled so that the level stays constant.
    const float cf = clampv (g.crossfeed, 0.0f, 1.0f);
    const float sameSideComp = std::sqrt (2.0f / (1.0f + cf * cf));
    const float norm = energy > 1.0e-9 ? (float) (1.0 / std::sqrt (energy / 2.0)) : 0.0f;
    const float primaryGain = g.statistical ? dbToGain (clampv (g.primaryDb, -12.0f, 18.0f)) : 1.0f;
    for (int i = 0; i < kMaxTaps; ++i)
    {
        const bool cross = taps[i].src != taps[i].ear;
        taps[i].targetGain *= norm * 0.5f * (cross ? cf * sameSideComp : sameSideComp) * (i < t && taps[i].primary ? primaryGain : 1.0f);
        if (firstGeometry)
        {
            taps[i].delay = taps[i].targetDelay;
            taps[i].gain = taps[i].targetGain;
        }
    }
    firstGeometry = false;
}

void EarlyReflections::feed (const float* inL, const float* inR, int n) noexcept
{
    for (int s = 0; s < n; ++s)
    {
        buf[0].push (inL[s]);
        buf[1].push (inR[s]);
    }
}

void EarlyReflections::process (const float* inL, const float* inR, float* outL, float* outR, int n) noexcept
{
    if (n <= 0)
        return;

    const float invN = 1.0f / (float) n;
    const float a = smoothingAlpha (0.08, fs, n);
    for (int i = 0; i < numTaps; ++i)
    {
        Tap& tp = taps[i];
        float nd = tp.delay + (tp.targetDelay - tp.delay) * a;
        // limit the glide speed (pitch shift) to about 10 %
        const float maxStep = 0.1f * (float) n;
        nd = tp.delay + clampv (nd - tp.delay, -maxStep, maxStep);
        if (std::abs (tp.targetDelay - nd) < 0.01f) // land exactly on the target (whole samples: no damping)
        {
            if (std::abs (tp.targetDelay - tp.delay) < 0.01f)
                tp.delay = tp.targetDelay;
            nd = tp.targetDelay;
        }
        float ng = tp.gain + (tp.targetGain - tp.gain) * a;
        if (std::abs (tp.targetGain - ng) < 1.0e-7f) // land exactly on the target (block-wise path)
        {
            if (std::abs (tp.targetGain - tp.gain) < 1.0e-7f)
                tp.gain = tp.targetGain;
            ng = tp.targetGain;
        }
        tp.dInc = (nd - tp.delay) * invN;
        tp.gInc = (ng - tp.gain) * invN;
    }

    // at rest (no glide, whole-sample delays: the usual case) the taps are read block-wise
    bool atRest = n <= kMaxBlock;
    for (int i = 0; i < numTaps && atRest; ++i)
    {
        const Tap& tp = taps[i];
        // exact tests on purpose (written as <= 0 to keep -Wfloat-equal quiet)
        atRest = std::abs (tp.dInc) <= 0.0f && std::abs (tp.gInc) <= 0.0f && std::abs (tp.delay - std::floor (tp.delay)) <= 0.0f && tp.delay >= 1.0f
                 && tp.delay <= maxDelaySamples;
    }
    if (atRest)
    {
        for (int s = 0; s < n; ++s)
        {
            buf[0].push (inL[s]);
            buf[1].push (inR[s]);
        }
        float acc[2][kMaxBlock];
        for (int s = 0; s < n; ++s)
            acc[0][s] = acc[1][s] = 0.0f;
        for (int i = 0; i < numTaps; ++i)
        {
            const Tap& tp = taps[i];
            if (std::abs (tp.gain) > 0.0f)
                buf[tp.src].accumulate (acc[tp.ear], n, (int) tp.delay, tp.gain);
        }
        for (int s = 0; s < n; ++s)
        {
            outL[s] = lp[0][1].process (lp[0][0].process (acc[0][s], lpCoefs), lpCoefs);
            outR[s] = lp[1][1].process (lp[1][0].process (acc[1][s], lpCoefs), lpCoefs);
        }
        return;
    }

    for (int s = 0; s < n; ++s)
    {
        buf[0].push (inL[s]);
        buf[1].push (inR[s]);
        float acc[2] = { 0.0f, 0.0f };
        for (int i = 0; i < numTaps; ++i)
        {
            Tap& tp = taps[i];
            tp.delay += tp.dInc;
            tp.gain += tp.gInc;
            const float d = clampv (tp.delay, 1.0f, maxDelaySamples);
            acc[tp.ear] += tp.gain * buf[tp.src].tapLinear (d);
        }
        outL[s] = lp[0][1].process (lp[0][0].process (acc[0], lpCoefs), lpCoefs);
        outR[s] = lp[1][1].process (lp[1][0].process (acc[1], lpCoefs), lpCoefs);
    }
}

} // namespace nh
