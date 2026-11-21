// Nachhall - interactive reverb display:
//   spectrum analyser with decay visualisation, Decay Rate EQ (blue) and Post EQ (yellow)
#pragma once

#include <deque>
#include <juce_dsp/juce_dsp.h>

#include "../PluginProcessor.h"
#include "Controls.h"

class EqDisplay : public juce::Component, private juce::Timer
{
public:
    explicit EqDisplay (NachhallAudioProcessor& processor);
    ~EqDisplay() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;

    void refreshLayout() { resized(); repaint(); }
    // selects a band (e.g. for keyboard navigation or screenshots); kind: 0 = Decay Rate EQ, 1 = Post EQ
    void selectBand (int kind, int index)
    {
        selection = { BandRef { kind == 0 ? Kind::Decay : Kind::Post, index } };
        repaint();
    }

private:
    enum class Kind
    {
        None,
        Decay,
        Post
    };

    struct BandRef
    {
        Kind kind = Kind::None;
        int index = -1;
        bool valid() const noexcept { return kind != Kind::None && index >= 0; }
        bool operator== (const BandRef& o) const noexcept { return kind == o.kind && index == o.index; }
        bool operator!= (const BandRef& o) const noexcept { return ! (*this == o); }
    };

    struct DecayHandles
    {
        DecayHandles (juce::AudioProcessorValueTreeState& s, int b)
            : used (s, ids::decay (b, "Used")), on (s, ids::decay (b, "On")), shape (s, ids::decay (b, "Shape")),
              freq (s, ids::decay (b, "Freq")), rate (s, ids::decay (b, "Rate")), q (s, ids::decay (b, "Q"))
        {
        }
        ParamHandle used, on, shape, freq, rate, q;
    };

    struct PostHandles
    {
        PostHandles (juce::AudioProcessorValueTreeState& s, int b)
            : used (s, ids::post (b, "Used")), on (s, ids::post (b, "On")), shape (s, ids::post (b, "Shape")),
              freq (s, ids::post (b, "Freq")), gain (s, ids::post (b, "Gain")), q (s, ids::post (b, "Q")),
              slope (s, ids::post (b, "Slope")), place (s, ids::post (b, "Place"))
        {
        }
        ParamHandle used, on, shape, freq, gain, q, slope, place;
    };

    struct DragStart
    {
        BandRef band;
        float freq = 1000.0f, value = 0.0f, q = 1.0f;
    };

    enum class DragMode
    {
        None,
        Band,
        Q,
        Rect,
        Piano
    };

    // ---- geometry
    float xForFreq (float f) const noexcept;
    float freqForX (float x) const noexcept;
    float yForDb (float db) const noexcept;
    float dbForY (float y) const noexcept;
    float yForRate (float r) const noexcept;
    float rateForY (float y) const noexcept;
    float yForAnalyzer (float db) const noexcept;
    float rangeDb() const noexcept;

    // ---- band helpers
    bool isUsed (BandRef b) const;
    bool isEnabled (BandRef b) const;
    int shapeOf (BandRef b) const;
    float freqOf (BandRef b) const;
    float valueOf (BandRef b) const; // gain (dB) or decay rate (multiplier)
    float qOf (BandRef b) const;
    bool usesQForVertical (BandRef b) const; // cut filters and notches
    juce::Point<float> dotPosition (BandRef b) const;
    BandRef bandAt (juce::Point<float> p) const;
    Kind curveAt (juce::Point<float> p) const;
    BandRef createBand (Kind kind, float freq, float value, int shape);
    void deleteBand (BandRef b);
    void deleteSelection();
    void toggleEnabled (BandRef b);
    void cycleShape (BandRef b);
    void cycleSlope (BandRef b);
    void selectRange (BandRef from, BandRef to);
    void createBandAt (juce::Point<float> p);
    void setShape (BandRef b, int shape);
    void setFreq (BandRef b, float f);
    void setValue (BandRef b, float v);
    void setQ (BandRef b, float q);
    void beginGestures (BandRef b);
    void endGestures (BandRef b);
    void showBandMenu (BandRef b);
    void openValueEditor (BandRef b);
    juce::String bandDescription (BandRef b) const;
    juce::Colour bandColour (BandRef b) const;
    std::vector<BandRef> allUsedBands() const;
    bool isSelected (BandRef b) const;

    // ---- analysis / curves
    void timerCallback() override;
    void updateCurves();
    void computeSpectrum (const AnalyzerTap& tap, float* dbOut);
    float interpolatePoints (const float* values, float freq) const;

    // ---- painting helpers
    void drawGrid (juce::Graphics&);
    void drawAnalyzer (juce::Graphics&);
    void drawCurves (juce::Graphics&);
    void drawBands (juce::Graphics&);
    void drawScales (juce::Graphics&);
    void drawMeter (juce::Graphics&);
    void drawPiano (juce::Graphics&);
    void drawStrip (juce::Graphics&);
    void drawHoverInfo (juce::Graphics&);
    bool handleStripClick (const juce::MouseEvent&);
    bool handlePianoClick (const juce::MouseEvent&);

    NachhallAudioProcessor& proc;
    juce::AudioProcessorValueTreeState& apvts;
    std::vector<std::unique_ptr<DecayHandles>> decay;
    std::vector<std::unique_ptr<PostHandles>> post;

    juce::Rectangle<float> plot, pianoArea, meterArea;
    static constexpr float fMin = 20.0f, fMax = 20000.0f;

    // analyser
    static constexpr int kFftOrder = 12;
    static constexpr int kFftSize = 1 << kFftOrder;
    static constexpr int kPoints = 240;
    juce::dsp::FFT fft { kFftOrder };
    juce::dsp::WindowingFunction<float> window { (size_t) kFftSize, juce::dsp::WindowingFunction<float>::hann, true };
    std::vector<float> fftData;
    float pointFreq[kPoints] {};
    float specOut[kPoints] {}, specPre[kPoints] {}, specWet[kPoints] {};
    struct Snapshot
    {
        std::array<float, kPoints> db {};
        float age = 0.0f;
    };
    std::deque<Snapshot> history;
    int frameCounter = 0;
    uint32_t lastWritePos = 0;
    float meterLevel[2] { -100.0f, -100.0f };
    float meterHold[2] { -100.0f, -100.0f };
    int meterHoldCounter[2] {};

    // curves
    float postCurve[kPoints] {};
    float decayCurve[kPoints] {}; // user decay multiplier (relative)
    float t60Curve[kPoints] {};   // total decay time (s)
    juce::String curveSignature;

    // interaction
    std::vector<BandRef> selection;
    BandRef hoverBand;
    Kind hoverCurve = Kind::None;
    juce::Point<float> mousePos { -1.0f, -1.0f };
    bool mouseInside = false;
    DragMode dragMode = DragMode::None;
    std::vector<DragStart> dragStarts;
    BandRef primaryDrag;
    juce::Point<float> dragOrigin;
    int constrainAxis = 0; // 0 = free, 1 = horizontal, 2 = vertical
    juce::Rectangle<float> selectionRect;

    // selected band strip (clickable segments)
    struct StripSegment
    {
        juce::Rectangle<float> area;
        int action; // 0 enable, 1 shape, 2 placement, 3 slope, 4 delete, 5 values
    };
    std::vector<StripSegment> stripSegments;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EqDisplay)
};
