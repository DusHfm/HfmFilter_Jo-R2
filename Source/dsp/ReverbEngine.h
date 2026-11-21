// Nachhall - complete reverb engine (no JUCE dependency)
#pragma once

#include "DecayDesign.h"
#include "EarlyReflections.h"
#include "PostEq.h"
#include "RoomModels.h"
#include "Tanks.h"

namespace nh
{
enum ReverbStyle : int
{
    kStyleModern = 0,
    kStyleVintage = 1,
    kStylePlate = 2,
    kNumStyles = 3
};

struct EngineParams
{
    float space = 0.5f;      // 0 .. 1 (room model + decay)
    float decayRate = 1.0f;  // 0.25 .. 4
    int style = kStyleModern;
    float predelayMs = 0.0f; // already resolved (tempo sync is handled by the host wrapper)
    float character = 0.5f;  // 0 .. 1
    float thickness = 0.5f;  // 0 .. 1
    float distance = 0.5f;   // 0 .. 1
    float brightness = 0.5f; // 0 .. 1
    float width = 0.7f;      // 0 .. 2
    float duckingDb = 0.0f;  // 0 .. 30 dB range
    bool gateOn = false;
    float gateHoldMs = 250.0f; // already resolved
    bool freeze = false;
    float mix = 0.3f;        // 0 .. 1
    float inGainDb = 0.0f, inPan = 0.0f, outGainDb = 0.0f, outPan = 0.0f;
    bool bypass = false;
    bool eqAutoGain = true;
    DecayBandParams decayBands[kNumDecayBands];
    PostBandParams postBands[kNumPostBands];
};

class ReverbEngine
{
public:
    static constexpr int kControlBlock = 32;
    static constexpr double kMaxPredelaySeconds = 4.0;

    void prepare (double sampleRate, int maxBlockSize);
    void reset();
    void setParams (const EngineParams& p) noexcept { params = p; }
    void process (float* left, float* right, int numSamples) noexcept;

    // analysis signals of the most recent process() call (mono), length = last numSamples
    const float* getAnalysisPre() const noexcept { return anaPre.data(); }
    const float* getAnalysisWet() const noexcept { return anaWet.data(); }
    const float* getAnalysisOut() const noexcept { return anaOut.data(); }
    int getAnalysisLength() const noexcept { return anaLength; }
    float getOutputPeak (int channel) const noexcept { return peak[channel & 1]; }
    float getCurrentT60() const noexcept { return currentT60; }
    double getSampleRate() const noexcept { return fs; }

    // Shared mapping helpers (also used by the GUI for its curves)
    static DecaySettings makeDecaySettings (const DecayBandParams* bands, float space, float decayRate, float brightness, int style) noexcept;
    static double referenceDelayFor (int style, float space) noexcept;

private:
    void processChunk (float* L, float* R, int n, int offset) noexcept;
    void controlUpdate (int n) noexcept;
    void updateTankActivity (int n) noexcept;

    double fs = 48000.0;
    int maxBlock = 512;
    EngineParams params;

    // smoothed controls
    float sSpace = 0.5f, sDecayRate = 1.0f, sCharacter = 0.5f, sThickness = 0.5f, sDistance = 0.5f, sBrightness = 0.5f;
    float sWidth = 0.7f, sDuck = 0.0f;
    float sVintage = 0.0f;
    float sFreeze = 0.0f;
    float freezeRamp = 0.0f;   // 0..1 within ~0.8 s after Freeze (loop delays settle on whole samples)
    float lateFrozen = 1.0f;   // late level held while frozen
    float trimHeldDb = 0.0f;   // Thickness / Distance level trim (held while frozen)
    float mixCur = 0.3f, mixInc = 0.0f;
    float inGL = 1.0f, inGR = 1.0f, inGLInc = 0.0f, inGRInc = 0.0f;
    float outGL = 1.0f, outGR = 1.0f, outGLInc = 0.0f, outGRInc = 0.0f;
    float bypassCur = 0.0f, bypassInc = 0.0f;
    float sideGain = 1.0f, sideGainInc = 0.0f;
    float erLevel = 0.5f, erLevelInc = 0.0f;
    float lateGain = 1.0f, lateGainInc = 0.0f;
    float erFeed = 0.3f;
    float inDrive2 = 0.0f;
    bool firstUpdate = true;

    // predelay
    DelayLine predelay[2];
    float pdCur = 0.0f, pdInc = 0.0f, pdOld = 0.0f, pdXfade = 0.0f, pdXfadeStep = 0.0f;

    // late reverb input delay (onset); the build-up itself is spread over the FDN lines
    DelayLine buildBuf[2];
    float lateOnsetCur = 0.0f, lateOnsetInc = 0.0f;

    EarlyReflections er;
    FdnTank fdn;
    PlateTank plate[2];

    // tank activity / style switching
    float fdnInGain = 1.0f, fdnOutGain = 1.0f, plateInGain = 0.0f, plateOutGain = 0.0f;
    bool fdnRunning = true, plateRunning = false;
    bool fdnNeedsReset = false, plateNeedsReset = false;

    // decay design
    DecayBandParams sBands[kNumDecayBands];
    DecaySettings lastDecay;
    DecaySpec spec;
    bool haveSpec = false;
    int specCountdown = 0;
    float currentT60 = 1.5f;
    float diffuserT60 = 1.5f; // shortest decay of the spectrum (limits the diffuser ringing)

    // tone
    SvfCoefs tiltLs, tiltHs;
    float tiltGain = 1.0f;
    float levelTrim = 1.0f, levelTrimInc = 0.0f;  // Decay Rate / Width level (Pro-R 2)
    SvfCoefs lateLs, lateHs, lateMid, lateAir;    // tone of the late reverb (room model)
    Svf lateToneState[2][4];
    float lastLateLsDb = -999.0f, lastLateHsDb = -999.0f, lastLateMidDb = -999.0f, lastLateAirDb = -999.0f;
    Svf tiltState[2][2];
    Svf erTiltState[2];
    SvfCoefs erTiltHs;
    float lastTiltBrightness = -1.0f;
    SvfCoefs vintLp[2], vintInLp;
    Svf vintState[2][2], vintInState[2];
    bool vintActive = false;
    int erCounter = 0;

    PostEq postEq;

    // Thickness dynamics (compressor / expander on the reverb input)
    SvfCoefs thickLpCoefs;
    Svf thickLp;
    float thickEnv = 0.0f, thickAtt = 0.01f, thickRel = 0.0001f;

    // dynamics
    float env = 0.0f, envAtt = 0.1f, envRel = 0.99f;
    float refDb = -90.0f;
    float duckDb = 0.0f, duckGain = 1.0f, duckGainInc = 0.0f;
    float gateGain = 1.0f, gateGainInc = 0.0f;
    bool gateOpen = false;
    int gateHoldCounter = 0;

    // analysis
    std::vector<float> anaPre, anaWet, anaOut;
    int anaLength = 0;
    float peak[2] = { 0.0f, 0.0f };
};

} // namespace nh
