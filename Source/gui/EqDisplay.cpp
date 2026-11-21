#include "EqDisplay.h"

namespace
{
constexpr float kDotRadius = 7.0f;
constexpr int kNumDecayBands = nh::kNumDecayBands;
constexpr int kNumPostBands = nh::kNumPostBands;

float defaultQForPostShape (int shape)
{
    switch (shape)
    {
        case nh::kPostLowShelf:
        case nh::kPostHighShelf: return 0.7f;
        case nh::kPostLowCut:
        case nh::kPostHighCut: return 0.7071f;
        default: return 1.0f;
    }
}

float defaultQForDecayShape (int shape)
{
    switch (shape)
    {
        case nh::kDecayLowShelf:
        case nh::kDecayHighShelf: return 0.7f;
        case nh::kDecayNotch: return 2.0f;
        default: return 1.0f;
    }
}

juce::String freqLabel (float f)
{
    if (f >= 1000.0f)
        return juce::String (f / 1000.0f, f >= 10000.0f ? 0 : (std::fmod (f, 1000.0f) < 0.5f ? 0 : 1)) + "k";
    return juce::String (juce::roundToInt (f));
}
} // namespace

//==============================================================================
EqDisplay::EqDisplay (NachhallAudioProcessor& p) : proc (p), apvts (p.apvts), fftData ((size_t) kFftSize * 2, 0.0f)
{
    for (int b = 0; b < kNumDecayBands; ++b)
        decay.push_back (std::make_unique<DecayHandles> (apvts, b));
    for (int b = 0; b < kNumPostBands; ++b)
        post.push_back (std::make_unique<PostHandles> (apvts, b));

    for (int i = 0; i < kPoints; ++i)
    {
        pointFreq[i] = fMin * std::pow (fMax / fMin, (float) i / (float) (kPoints - 1));
        specOut[i] = specPre[i] = specWet[i] = -120.0f;
        t60Curve[i] = 1.5f;
        decayCurve[i] = 1.0f;
    }

    setComponentID ("eqDisplay");
    setWantsKeyboardFocus (true);
    setRepaintsOnMouseActivity (false);
    updateCurves();
    startTimerHz (30);
}

EqDisplay::~EqDisplay() { stopTimer(); }

//==============================================================================
void EqDisplay::resized()
{
    auto r = getLocalBounds().toFloat();
    meterArea = proc.showMeter ? r.removeFromRight (14.0f).reduced (2.0f, 30.0f) : juce::Rectangle<float>();
    r.removeFromLeft (46.0f);
    r.removeFromRight (46.0f);
    r.removeFromTop (28.0f);
    pianoArea = proc.showPiano ? r.removeFromBottom (24.0f) : juce::Rectangle<float>();
    r.removeFromBottom (18.0f);
    plot = r;
}

float EqDisplay::rangeDb() const noexcept
{
    switch (proc.displayRange)
    {
        case 0: return 30.0f;
        case 2: return 9.0f;
        default: return 18.0f;
    }
}

float EqDisplay::xForFreq (float f) const noexcept
{
    return plot.getX() + plot.getWidth() * std::log (juce::jmax (1.0f, f) / fMin) / std::log (fMax / fMin);
}

float EqDisplay::freqForX (float x) const noexcept
{
    return fMin * std::pow (fMax / fMin, (x - plot.getX()) / plot.getWidth());
}

float EqDisplay::yForDb (float db) const noexcept { return plot.getCentreY() - db / rangeDb() * plot.getHeight() * 0.5f; }
float EqDisplay::dbForY (float y) const noexcept { return (plot.getCentreY() - y) / (plot.getHeight() * 0.5f) * rangeDb(); }
float EqDisplay::yForRate (float r) const noexcept
{
    return plot.getCentreY() - std::log2 (juce::jmax (1.0e-3f, r)) / 2.0f * plot.getHeight() * 0.5f;
}
float EqDisplay::rateForY (float y) const noexcept { return std::pow (2.0f, (plot.getCentreY() - y) / (plot.getHeight() * 0.5f) * 2.0f); }
float EqDisplay::yForAnalyzer (float db) const noexcept { return plot.getY() + (6.0f - db) / 96.0f * plot.getHeight(); }

//==============================================================================
bool EqDisplay::isUsed (BandRef b) const
{
    if (b.kind == Kind::Decay)
        return decay[(size_t) b.index]->used.get() > 0.5f;
    if (b.kind == Kind::Post)
        return post[(size_t) b.index]->used.get() > 0.5f;
    return false;
}

bool EqDisplay::isEnabled (BandRef b) const
{
    if (b.kind == Kind::Decay)
        return decay[(size_t) b.index]->on.get() > 0.5f;
    return post[(size_t) b.index]->on.get() > 0.5f;
}

int EqDisplay::shapeOf (BandRef b) const
{
    if (b.kind == Kind::Decay)
        return juce::roundToInt (decay[(size_t) b.index]->shape.get());
    return juce::roundToInt (post[(size_t) b.index]->shape.get());
}

float EqDisplay::freqOf (BandRef b) const
{
    if (b.kind == Kind::Decay)
        return decay[(size_t) b.index]->freq.get();
    return post[(size_t) b.index]->freq.get();
}

float EqDisplay::valueOf (BandRef b) const
{
    if (b.kind == Kind::Decay)
        return decay[(size_t) b.index]->rate.get() * 0.01f;
    return post[(size_t) b.index]->gain.get();
}

float EqDisplay::qOf (BandRef b) const
{
    if (b.kind == Kind::Decay)
        return decay[(size_t) b.index]->q.get();
    return post[(size_t) b.index]->q.get();
}

bool EqDisplay::usesQForVertical (BandRef b) const
{
    const int s = shapeOf (b);
    if (b.kind == Kind::Decay)
        return s == nh::kDecayNotch;
    return s == nh::kPostLowCut || s == nh::kPostHighCut;
}

juce::Point<float> EqDisplay::dotPosition (BandRef b) const
{
    const float x = xForFreq (freqOf (b));
    float y;
    if (b.kind == Kind::Decay)
        y = shapeOf (b) == nh::kDecayNotch ? yForRate (0.25f) : yForRate (valueOf (b));
    else
        y = usesQForVertical (b) ? yForDb (0.0f) : yForDb (valueOf (b));
    return { juce::jlimit (plot.getX(), plot.getRight(), x), juce::jlimit (plot.getY() + 2.0f, plot.getBottom() - 2.0f, y) };
}

std::vector<EqDisplay::BandRef> EqDisplay::allUsedBands() const
{
    std::vector<BandRef> v;
    for (int i = 0; i < kNumDecayBands; ++i)
        if (isUsed ({ Kind::Decay, i }))
            v.push_back ({ Kind::Decay, i });
    for (int i = 0; i < kNumPostBands; ++i)
        if (isUsed ({ Kind::Post, i }))
            v.push_back ({ Kind::Post, i });
    return v;
}

bool EqDisplay::isSelected (BandRef b) const { return std::find (selection.begin(), selection.end(), b) != selection.end(); }

EqDisplay::BandRef EqDisplay::bandAt (juce::Point<float> p) const
{
    BandRef best;
    float bestD = kDotRadius + 4.0f;
    for (auto b : allUsedBands())
    {
        const float d = dotPosition (b).getDistanceFrom (p);
        if (d < bestD)
        {
            bestD = d;
            best = b;
        }
    }
    return best;
}

EqDisplay::Kind EqDisplay::curveAt (juce::Point<float> p) const
{
    if (! plot.contains (p))
        return Kind::None;
    const float f = freqForX (p.x);
    const float yPost = yForDb (interpolatePoints (postCurve, f));
    const float yDecay = yForRate (interpolatePoints (decayCurve, f));
    const float dPost = std::abs (p.y - yPost);
    const float dDecay = std::abs (p.y - yDecay);
    if (std::min (dPost, dDecay) > 6.0f)
        return Kind::None;
    return dPost <= dDecay ? Kind::Post : Kind::Decay;
}

juce::Colour EqDisplay::bandColour (BandRef b) const { return b.kind == Kind::Decay ? theme::blue : theme::yellow; }

juce::String EqDisplay::bandDescription (BandRef b) const
{
    const float f = freqOf (b);
    juce::String s = (b.kind == Kind::Decay ? "Decay " : "Post ") + juce::String (b.index + 1) + "  ";
    if (b.kind == Kind::Decay)
    {
        s << params::decayShapeChoices()[shapeOf (b)] << "  " << params::formatFrequency (f);
        if (proc.showPiano)
            s << " (" << params::noteNameForFrequency (f, true) << ")";
        if (shapeOf (b) != nh::kDecayNotch)
            s << "  " << juce::roundToInt (valueOf (b) * 100.0f) << " %";
        s << "  Q " << juce::String (qOf (b), 2);
        s << "  ->  " << juce::String (interpolatePoints (t60Curve, f), 2) << " s";
    }
    else
    {
        const int sh = shapeOf (b);
        s << params::postShapeChoices()[sh] << "  " << params::formatFrequency (f);
        if (proc.showPiano)
            s << " (" << params::noteNameForFrequency (f, true) << ")";
        if (sh == nh::kPostLowCut || sh == nh::kPostHighCut)
            s << "  " << params::slopeChoices()[juce::roundToInt (post[(size_t) b.index]->slope.get())];
        else
            s << "  " << (valueOf (b) >= 0.0f ? "+" : "") << juce::String (valueOf (b), 1) << " dB";
        s << "  Q " << juce::String (qOf (b), 2);
        const int pl = juce::roundToInt (post[(size_t) b.index]->place.get());
        if (pl != 0)
            s << "  [" << params::placementChoices()[pl] << "]";
    }
    if (! isEnabled (b))
        s << "  (bypassed)";
    return s;
}

//==============================================================================
EqDisplay::BandRef EqDisplay::createBand (Kind kind, float freq, float value, int shape)
{
    freq = juce::jlimit (10.0f, 30000.0f, freq);
    if (kind == Kind::Decay)
    {
        for (int i = 0; i < kNumDecayBands; ++i)
        {
            auto& h = *decay[(size_t) i];
            if (h.used.get() > 0.5f)
                continue;
            h.shape.setWithGesture ((float) shape);
            h.freq.setWithGesture (freq);
            h.rate.setWithGesture (juce::jlimit (25.0f, 400.0f, value * 100.0f));
            h.q.setWithGesture (defaultQForDecayShape (shape));
            h.on.setWithGesture (1.0f);
            h.used.setWithGesture (1.0f);
            return { Kind::Decay, i };
        }
    }
    else if (kind == Kind::Post)
    {
        for (int i = 0; i < kNumPostBands; ++i)
        {
            auto& h = *post[(size_t) i];
            if (h.used.get() > 0.5f)
                continue;
            h.shape.setWithGesture ((float) shape);
            h.freq.setWithGesture (freq);
            h.gain.setWithGesture (juce::jlimit (-30.0f, 30.0f, value));
            h.q.setWithGesture (defaultQForPostShape (shape));
            h.slope.setWithGesture (3.0f);
            h.place.setWithGesture (0.0f);
            h.on.setWithGesture (1.0f);
            h.used.setWithGesture (1.0f);
            return { Kind::Post, i };
        }
    }
    return {};
}

void EqDisplay::deleteBand (BandRef b)
{
    if (! b.valid())
        return;
    if (b.kind == Kind::Decay)
    {
        decay[(size_t) b.index]->used.setWithGesture (0.0f);
        decay[(size_t) b.index]->rate.setWithGesture (100.0f);
    }
    else
    {
        post[(size_t) b.index]->used.setWithGesture (0.0f);
        post[(size_t) b.index]->gain.setWithGesture (0.0f);
    }
    selection.erase (std::remove (selection.begin(), selection.end(), b), selection.end());
    if (hoverBand == b)
        hoverBand = {};
}

void EqDisplay::deleteSelection()
{
    proc.undoManager.beginNewTransaction ("Delete bands");
    auto sel = selection;
    for (auto b : sel)
        deleteBand (b);
    selection.clear();
    repaint();
}

void EqDisplay::toggleEnabled (BandRef b)
{
    proc.undoManager.beginNewTransaction ("Toggle band");
    if (b.kind == Kind::Decay)
        decay[(size_t) b.index]->on.setWithGesture (isEnabled (b) ? 0.0f : 1.0f);
    else
        post[(size_t) b.index]->on.setWithGesture (isEnabled (b) ? 0.0f : 1.0f);
}

void EqDisplay::setShape (BandRef b, int shape)
{
    proc.undoManager.beginNewTransaction ("Band shape");
    if (b.kind == Kind::Decay)
    {
        decay[(size_t) b.index]->shape.setWithGesture ((float) shape);
        decay[(size_t) b.index]->q.setWithGesture (defaultQForDecayShape (shape));
    }
    else
    {
        post[(size_t) b.index]->shape.setWithGesture ((float) shape);
        post[(size_t) b.index]->q.setWithGesture (defaultQForPostShape (shape));
    }
}

void EqDisplay::cycleShape (BandRef b)
{
    const int n = b.kind == Kind::Decay ? (int) nh::kNumDecayShapes : (int) nh::kNumPostShapes;
    setShape (b, (shapeOf (b) + 1) % n);
}

void EqDisplay::setFreq (BandRef b, float f)
{
    f = juce::jlimit (10.0f, 30000.0f, f);
    if (b.kind == Kind::Decay)
        decay[(size_t) b.index]->freq.set (f);
    else
        post[(size_t) b.index]->freq.set (f);
}

void EqDisplay::setValue (BandRef b, float v)
{
    if (b.kind == Kind::Decay)
        decay[(size_t) b.index]->rate.set (juce::jlimit (25.0f, 400.0f, v * 100.0f));
    else
        post[(size_t) b.index]->gain.set (juce::jlimit (-30.0f, 30.0f, v));
}

void EqDisplay::setQ (BandRef b, float q)
{
    q = juce::jlimit (0.025f, 40.0f, q);
    if (b.kind == Kind::Decay)
        decay[(size_t) b.index]->q.set (q);
    else
        post[(size_t) b.index]->q.set (q);
}

void EqDisplay::beginGestures (BandRef b)
{
    if (b.kind == Kind::Decay)
    {
        auto& h = *decay[(size_t) b.index];
        h.freq.begin();
        h.rate.begin();
        h.q.begin();
    }
    else
    {
        auto& h = *post[(size_t) b.index];
        h.freq.begin();
        h.gain.begin();
        h.q.begin();
    }
}

void EqDisplay::endGestures (BandRef b)
{
    if (b.kind == Kind::Decay)
    {
        auto& h = *decay[(size_t) b.index];
        h.freq.end();
        h.rate.end();
        h.q.end();
    }
    else
    {
        auto& h = *post[(size_t) b.index];
        h.freq.end();
        h.gain.end();
        h.q.end();
    }
}

void EqDisplay::showBandMenu (BandRef b)
{
    juce::PopupMenu m;
    m.addSectionHeader (b.kind == Kind::Decay ? "Decay Rate EQ band " + juce::String (b.index + 1) : "Post EQ band " + juce::String (b.index + 1));
    m.addItem (1, "Enabled", true, isEnabled (b));
    juce::PopupMenu shapes;
    const auto& names = b.kind == Kind::Decay ? params::decayShapeChoices() : params::postShapeChoices();
    for (int i = 0; i < names.size(); ++i)
        shapes.addItem (100 + i, names[i], true, shapeOf (b) == i);
    m.addSubMenu ("Shape", shapes);
    if (b.kind == Kind::Post)
    {
        juce::PopupMenu place, slope;
        const int pl = juce::roundToInt (post[(size_t) b.index]->place.get());
        for (int i = 0; i < params::placementChoices().size(); ++i)
            place.addItem (200 + i, params::placementChoices()[i], true, pl == i);
        m.addSubMenu ("Stereo Placement", place);
        const int sh = shapeOf (b);
        const int sl = juce::roundToInt (post[(size_t) b.index]->slope.get());
        for (int i = 0; i < params::slopeChoices().size(); ++i)
            slope.addItem (300 + i, params::slopeChoices()[i], true, sl == i);
        m.addSubMenu ("Slope", slope, sh == nh::kPostLowCut || sh == nh::kPostHighCut);
    }
    m.addSeparator();
    m.addItem (2, "Enter values...");
    m.addItem (3, "Delete band");

    juce::Component::SafePointer<EqDisplay> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options(), [safe, b] (int r) {
        if (safe == nullptr || r == 0)
            return;
        auto& d = *safe;
        if (r == 1)
            d.toggleEnabled (b);
        else if (r == 2)
            d.openValueEditor (b);
        else if (r == 3)
        {
            d.proc.undoManager.beginNewTransaction ("Delete band");
            d.deleteBand (b);
        }
        else if (r >= 100 && r < 200)
            d.setShape (b, r - 100);
        else if (r >= 200 && r < 300)
        {
            d.proc.undoManager.beginNewTransaction ("Band placement");
            d.post[(size_t) b.index]->place.setWithGesture ((float) (r - 200));
        }
        else if (r >= 300)
        {
            d.proc.undoManager.beginNewTransaction ("Band slope");
            d.post[(size_t) b.index]->slope.setWithGesture ((float) (r - 300));
        }
        d.repaint();
    });
}

void EqDisplay::openValueEditor (BandRef b)
{
    if (! b.valid())
        return;
    const bool isDecay = b.kind == Kind::Decay;
    auto* w = new juce::AlertWindow (isDecay ? "Decay Rate EQ band " + juce::String (b.index + 1) : "Post EQ band " + juce::String (b.index + 1),
                                     "Frequency accepts Hz, kHz (\"2k\") or notes (\"A4\", \"C#2+13\").", juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("freq", params::formatFrequency (freqOf (b)), "Frequency");
    w->addTextEditor ("value", isDecay ? juce::String (juce::roundToInt (valueOf (b) * 100.0f)) + " %" : juce::String (valueOf (b), 1) + " dB",
                      isDecay ? "Decay Rate" : "Gain");
    w->addTextEditor ("q", juce::String (qOf (b), 3), "Q");
    w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    juce::Component::SafePointer<EqDisplay> safe (this);
    w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w, b, isDecay] (int result) {
                            std::unique_ptr<juce::AlertWindow> owner (w);
                            if (safe == nullptr || result != 1)
                                return;
                            auto& d = *safe;
                            d.proc.undoManager.beginNewTransaction ("Edit band");
                            d.beginGestures (b);
                            const float f = params::parseFrequency (w->getTextEditorContents ("freq"));
                            if (f > 0.0f)
                                d.setFreq (b, f);
                            const auto vt = w->getTextEditorContents ("value").retainCharacters ("0123456789.,-+").replaceCharacter (',', '.');
                            if (vt.isNotEmpty())
                                d.setValue (b, isDecay ? vt.getFloatValue() * 0.01f : vt.getFloatValue());
                            const float q = w->getTextEditorContents ("q").replaceCharacter (',', '.').getFloatValue();
                            if (q > 0.0f)
                                d.setQ (b, q);
                            d.endGestures (b);
                            d.repaint();
                        }),
                        false);
}

//==============================================================================
void EqDisplay::mouseMove (const juce::MouseEvent& e)
{
    mousePos = e.position;
    mouseInside = true;
    const auto hb = bandAt (e.position);
    const auto hc = hb.valid() ? Kind::None : curveAt (e.position);
    hoverBand = hb;
    hoverCurve = hc;
    setMouseCursor (hb.valid() ? juce::MouseCursor::PointingHandCursor
                               : (hc != Kind::None ? juce::MouseCursor::CrosshairCursor : juce::MouseCursor::NormalCursor));
    repaint();
}

void EqDisplay::mouseExit (const juce::MouseEvent&)
{
    mouseInside = false;
    hoverBand = {};
    hoverCurve = Kind::None;
    repaint();
}

bool EqDisplay::handleStripClick (const juce::MouseEvent& e)
{
    if (selection.size() != 1)
        return false;
    const auto b = selection.front();
    for (const auto& seg : stripSegments)
    {
        if (! seg.area.contains (e.position))
            continue;
        switch (seg.action)
        {
            case 0: toggleEnabled (b); break;
            case 1:
            case 2:
            case 3: showBandMenu (b); break;
            case 4:
                proc.undoManager.beginNewTransaction ("Delete band");
                deleteBand (b);
                break;
            case 5: openValueEditor (b); break;
            default: break;
        }
        repaint();
        return true;
    }
    return false;
}

bool EqDisplay::handlePianoClick (const juce::MouseEvent& e)
{
    if (! proc.showPiano || ! pianoArea.contains (e.position))
        return false;
    // the band whose dot is closest on the keyboard snaps to the nearest note; dragging moves it
    // in semitone steps
    BandRef best;
    float bestD = 10.0f;
    for (auto b : allUsedBands())
    {
        const float d = std::abs (xForFreq (freqOf (b)) - e.position.x);
        if (d < bestD)
        {
            bestD = d;
            best = b;
        }
    }
    if (best.valid())
    {
        const double midi = std::round (69.0 + 12.0 * std::log2 (freqOf (best) / 440.0));
        proc.undoManager.beginNewTransaction ("Snap to note");
        beginGestures (best);
        setFreq (best, (float) (440.0 * std::pow (2.0, (midi - 69.0) / 12.0)));
        selection = { best };
        primaryDrag = best;
        dragStarts = { { best, freqOf (best), valueOf (best), qOf (best) } };
        dragMode = DragMode::Piano;
        repaint();
    }
    return true;
}

void EqDisplay::cycleSlope (BandRef b)
{
    if (b.kind != Kind::Post)
        return;
    const int sh = shapeOf (b);
    if (sh != nh::kPostLowCut && sh != nh::kPostHighCut)
        return;
    proc.undoManager.beginNewTransaction ("Band slope");
    auto& slope = post[(size_t) b.index]->slope;
    slope.setWithGesture ((float) ((juce::roundToInt (slope.get()) + 1) % nh::kNumSlopes));
    repaint();
}

void EqDisplay::selectRange (BandRef from, BandRef to)
{
    const float f0 = std::min (freqOf (from), freqOf (to)), f1 = std::max (freqOf (from), freqOf (to));
    for (auto b : allUsedBands())
        if (freqOf (b) >= f0 && freqOf (b) <= f1 && ! isSelected (b))
            selection.push_back (b);
    repaint();
}

void EqDisplay::createBandAt (juce::Point<float> pos)
{
    if (! plot.contains (pos))
        return;
    proc.undoManager.beginNewTransaction ("New band");
    const float f = freqForX (pos.x);
    const float rel = (pos.x - plot.getX()) / plot.getWidth();
    BandRef nb;
    if (pos.y > plot.getBottom() - plot.getHeight() * 0.15f)
        nb = createBand (Kind::Decay, f, 1.0f, nh::kDecayNotch);
    else if (rel < 0.06f)
        nb = createBand (Kind::Post, f, 0.0f, nh::kPostLowCut);
    else if (rel > 0.94f)
        nb = createBand (Kind::Post, f, 0.0f, nh::kPostHighCut);
    else
        nb = createBand (Kind::Post, f, dbForY (pos.y), nh::kPostBell);
    if (nb.valid())
        selection = { nb };
    repaint();
}

void EqDisplay::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    mousePos = e.position;
    dragMode = DragMode::None;
    constrainAxis = 0;

    // display range button (top of the right scale)
    {
        const auto rangeButton = juce::Rectangle<float> (plot.getRight() + 4.0f, 4.0f, 42.0f, 20.0f);
        if (rangeButton.contains (e.position))
        {
            proc.displayRange = (proc.displayRange + 1) % 3;
            repaint();
            return;
        }
    }

    if (handleStripClick (e) || handlePianoClick (e))
        return;

    const auto hit = bandAt (e.position);
    if (hit.valid())
    {
        if (e.mods.isPopupMenu())
        {
            selection = { hit };
            showBandMenu (hit);
            repaint();
            return;
        }
        if (e.mods.isAltDown() && e.mods.isShiftDown())
        {
            cycleSlope (hit);
            return;
        }
        if (e.mods.isAltDown() && e.mods.isCommandDown())
        {
            cycleShape (hit);
            return;
        }
        if (e.mods.isAltDown())
        {
            toggleEnabled (hit);
            return;
        }
        if (e.mods.isCommandDown() && ! isSelected (hit))
        {
            selection.push_back (hit);
            repaint();
            return;
        }
        if (e.mods.isShiftDown() && ! selection.empty() && ! isSelected (hit))
        {
            selectRange (selection.back(), hit);
            return;
        }
        if (! isSelected (hit))
            selection = { hit };

        proc.undoManager.beginNewTransaction ("Edit band");
        primaryDrag = hit;
        dragOrigin = e.position;
        dragStarts.clear();
        for (auto b : selection)
        {
            dragStarts.push_back ({ b, freqOf (b), valueOf (b), qOf (b) });
            beginGestures (b);
        }
        dragMode = e.mods.isCommandDown() ? DragMode::Q : DragMode::Band;
        repaint();
        return;
    }

    if (e.mods.isPopupMenu())
        return;

    const auto curve = curveAt (e.position);
    if (curve != Kind::None)
    {
        proc.undoManager.beginNewTransaction ("New band");
        const float f = freqForX (e.position.x);
        const float rel = (e.position.x - plot.getX()) / plot.getWidth();
        BandRef nb;
        if (curve == Kind::Decay)
        {
            const int shape = rel < 0.07f ? nh::kDecayLowShelf : (rel > 0.93f ? nh::kDecayHighShelf : nh::kDecayBell);
            nb = createBand (Kind::Decay, f, rateForY (e.position.y), shape);
        }
        else
        {
            const int shape = rel < 0.07f ? nh::kPostLowShelf : (rel > 0.93f ? nh::kPostHighShelf : nh::kPostBell);
            nb = createBand (Kind::Post, f, dbForY (e.position.y), shape);
        }
        if (nb.valid())
        {
            selection = { nb };
            primaryDrag = nb;
            dragOrigin = e.position;
            dragStarts = { { nb, freqOf (nb), valueOf (nb), qOf (nb) } };
            beginGestures (nb);
            dragMode = DragMode::Band;
        }
        repaint();
        return;
    }

    if (e.mods.isCommandDown())
    {
        createBandAt (e.position); // Ctrl/Cmd + click on the background = new band
        return;
    }
    selection.clear();
    dragMode = DragMode::Rect;
    dragOrigin = e.position;
    selectionRect = {};
    repaint();
}

void EqDisplay::mouseDrag (const juce::MouseEvent& e)
{
    mousePos = e.position;
    if (dragMode == DragMode::Rect)
    {
        selectionRect = juce::Rectangle<float> (dragOrigin, e.position);
        repaint();
        return;
    }
    if (dragMode == DragMode::Piano)
    {
        if (primaryDrag.valid())
        {
            const double midi = std::round (69.0 + 12.0 * std::log2 (freqForX (e.position.x) / 440.0));
            setFreq (primaryDrag, (float) (440.0 * std::pow (2.0, (midi - 69.0) / 12.0)));
            repaint();
        }
        return;
    }
    if (dragMode != DragMode::Band && dragMode != DragMode::Q)
        return;

    const float fine = e.mods.isShiftDown() ? 0.2f : 1.0f;
    float dx = (e.position.x - dragOrigin.x) * fine;
    float dy = (e.position.y - dragOrigin.y) * fine;

    if (e.mods.isAltDown())
    {
        if (constrainAxis == 0 && (std::abs (dx) > 4.0f || std::abs (dy) > 4.0f))
            constrainAxis = std::abs (dx) > std::abs (dy) ? 1 : 2;
        if (constrainAxis == 1)
            dy = 0.0f;
        else if (constrainAxis == 2)
            dx = 0.0f;
    }

    if (dragMode == DragMode::Q)
    {
        for (const auto& s : dragStarts)
            setQ (s.band, s.q * std::pow (2.0f, -dy / 60.0f));
        repaint();
        return;
    }

    const float octaves = dx / plot.getWidth() * std::log2 (fMax / fMin);

    // several Post EQ bands: their gains scale relative to each other (like Pro-R)
    float gainScale = 1.0f;
    bool scaleGains = false;
    for (const auto& s : dragStarts)
        if (s.band == primaryDrag && s.band.kind == Kind::Post && dragStarts.size() > 1 && std::abs (s.value) > 0.1f)
        {
            const float newPrimary = s.value - dy / (plot.getHeight() * 0.5f) * rangeDb();
            gainScale = newPrimary / s.value;
            scaleGains = true;
        }

    for (const auto& s : dragStarts)
    {
        setFreq (s.band, s.freq * std::pow (2.0f, octaves));
        if (usesQForVertical (s.band))
        {
            if (s.band == primaryDrag)
                setQ (s.band, s.q * std::pow (2.0f, -dy / 60.0f));
            continue;
        }
        if (s.band.kind == Kind::Decay)
        {
            const float dOct = -dy / (plot.getHeight() * 0.5f) * 2.0f;
            setValue (s.band, s.value * std::pow (2.0f, dOct));
        }
        else if (scaleGains)
            setValue (s.band, s.value * gainScale);
        else
        {
            const float dDb = -dy / (plot.getHeight() * 0.5f) * rangeDb();
            setValue (s.band, s.value + dDb);
        }
    }
    repaint();
}

void EqDisplay::mouseUp (const juce::MouseEvent&)
{
    if (dragMode == DragMode::Band || dragMode == DragMode::Q || dragMode == DragMode::Piano)
        for (const auto& s : dragStarts)
            endGestures (s.band);

    if (dragMode == DragMode::Rect && selectionRect.getWidth() > 2.0f)
    {
        for (auto b : allUsedBands())
            if (selectionRect.contains (dotPosition (b)) && ! isSelected (b))
                selection.push_back (b);
    }
    dragMode = DragMode::None;
    dragStarts.clear();
    selectionRect = {};
    repaint();
}

void EqDisplay::mouseDoubleClick (const juce::MouseEvent& e)
{
    const auto hit = bandAt (e.position);
    if (hit.valid())
    {
        openValueEditor (hit);
        return;
    }
    createBandAt (e.position);
}

void EqDisplay::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    auto target = bandAt (e.position);
    if (! target.valid() && selection.size() == 1 && plot.contains (e.position))
        target = selection.front();
    if (! target.valid())
        return;
    const float factor = std::pow (2.0f, w.deltaY * (w.isReversed ? -1.0f : 1.0f) * 1.5f);
    std::vector<BandRef> targets = isSelected (target) ? selection : std::vector<BandRef> { target };
    for (auto b : targets)
    {
        if (b.kind == Kind::Decay)
            decay[(size_t) b.index]->q.begin();
        else
            post[(size_t) b.index]->q.begin();
        setQ (b, qOf (b) * factor);
        if (b.kind == Kind::Decay)
            decay[(size_t) b.index]->q.end();
        else
            post[(size_t) b.index]->q.end();
    }
    repaint();
}

bool EqDisplay::keyPressed (const juce::KeyPress& k)
{
    if ((k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey) && ! selection.empty())
    {
        deleteSelection();
        return true;
    }
    return false;
}

//==============================================================================
float EqDisplay::interpolatePoints (const float* values, float freq) const
{
    const float pos = std::log (juce::jlimit (fMin, fMax, freq) / fMin) / std::log (fMax / fMin) * (float) (kPoints - 1);
    const int i = juce::jlimit (0, kPoints - 2, (int) pos);
    const float t = pos - (float) i;
    return values[i] + (values[i + 1] - values[i]) * t;
}

void EqDisplay::updateCurves()
{
    const double fs = std::max (8000.0, proc.currentSampleRate.load());
    const float space = apvts.getRawParameterValue (ids::space)->load();
    const float rate = apvts.getRawParameterValue (ids::decayRate)->load() * 0.01f;
    const float bright = 0.5f + apvts.getRawParameterValue (ids::brightness)->load() * 0.005f;
    const int style = juce::roundToInt (apvts.getRawParameterValue (ids::style)->load());

    nh::DecayBandParams db[kNumDecayBands];
    nh::PostBandParams pb[kNumPostBands];
    juce::String sig;
    sig << fs << ";" << space << ";" << rate << ";" << bright << ";" << style;
    for (int i = 0; i < kNumDecayBands; ++i)
    {
        auto& h = *decay[(size_t) i];
        db[i].used = h.used.get() > 0.5f;
        db[i].enabled = h.on.get() > 0.5f;
        db[i].shape = juce::roundToInt (h.shape.get());
        db[i].freq = h.freq.get();
        db[i].rate = h.rate.get() * 0.01f;
        db[i].q = h.q.get();
        sig << ";" << (int) db[i].used << (int) db[i].enabled << db[i].shape << "," << db[i].freq << "," << db[i].rate << "," << db[i].q;
    }
    for (int i = 0; i < kNumPostBands; ++i)
    {
        auto& h = *post[(size_t) i];
        pb[i].used = h.used.get() > 0.5f;
        pb[i].enabled = h.on.get() > 0.5f;
        pb[i].shape = juce::roundToInt (h.shape.get());
        pb[i].freq = h.freq.get();
        pb[i].gain = h.gain.get();
        pb[i].q = h.q.get();
        pb[i].slope = juce::roundToInt (h.slope.get());
        pb[i].placement = juce::roundToInt (h.place.get());
        sig << ";" << (int) pb[i].used << (int) pb[i].enabled << pb[i].shape << "," << pb[i].freq << "," << pb[i].gain << "," << pb[i].q << ","
            << pb[i].slope;
    }
    if (sig == curveSignature)
        return;
    curveSignature = sig;

    // Post EQ
    for (int k = 0; k < kPoints; ++k)
        postCurve[k] = 0.0f;
    for (int i = 0; i < kNumPostBands; ++i)
    {
        if (! pb[i].active())
            continue;
        const auto d = nh::designPostBand (pb[i], fs);
        for (int k = 0; k < kPoints; ++k)
        {
            const double tanW = std::tan (nh::kPi * std::min ((double) pointFreq[k], fs * 0.4999) / fs);
            postCurve[k] += (float) (10.0 * std::log10 (std::max (d.magnitudeSqFromTan (tanW), 1.0e-30)));
        }
    }

    // Decay Rate EQ (relative) + total decay time
    const double refDelay = nh::ReverbEngine::referenceDelayFor (style, space);
    {
        const auto spec = nh::buildUserDecaySpec (db, fs, refDelay);
        nh::decayTimeCurve (spec, fs, refDelay, pointFreq, decayCurve, kPoints);
        for (int k = 0; k < kPoints; ++k)
            decayCurve[k] = decayCurve[k] / spec.t60;
    }
    {
        const auto ds = nh::ReverbEngine::makeDecaySettings (db, space, rate, bright, style);
        const auto spec = nh::buildDecaySpec (ds, fs, refDelay);
        nh::decayTimeCurve (spec, fs, refDelay, pointFreq, t60Curve, kPoints);
    }
}

void EqDisplay::computeSpectrum (const AnalyzerTap& tap, float* dbOut)
{
    tap.readLatest (fftData.data(), kFftSize);
    std::fill (fftData.begin() + kFftSize, fftData.end(), 0.0f);
    window.multiplyWithWindowingTable (fftData.data(), (size_t) kFftSize);
    fft.performFrequencyOnlyForwardTransform (fftData.data(), true);

    const double fs = std::max (8000.0, proc.currentSampleRate.load());
    const double binHz = fs / kFftSize;
    for (int k = 0; k < kPoints; ++k)
    {
        const double fLo = k > 0 ? std::sqrt ((double) pointFreq[k] * pointFreq[k - 1]) : pointFreq[k];
        const double fHi = k < kPoints - 1 ? std::sqrt ((double) pointFreq[k] * pointFreq[k + 1]) : pointFreq[k];
        int b0 = (int) std::floor (fLo / binHz), b1 = (int) std::ceil (fHi / binHz);
        b0 = juce::jlimit (1, kFftSize / 2 - 1, b0);
        b1 = juce::jlimit (b0, kFftSize / 2 - 1, b1);
        float mag = 0.0f;
        if (b1 - b0 <= 1)
        {
            // interpolate between bins for the low end
            const double pos = pointFreq[k] / binHz;
            const int i = juce::jlimit (1, kFftSize / 2 - 2, (int) pos);
            const float t = (float) (pos - i);
            mag = fftData[(size_t) i] + (fftData[(size_t) i + 1] - fftData[(size_t) i]) * t;
        }
        else
            for (int b = b0; b <= b1; ++b)
                mag = std::max (mag, fftData[(size_t) b]);
        const float db = 20.0f * std::log10 (2.0f * mag / (float) kFftSize + 1.0e-9f);
        dbOut[k] = db + 4.5f * std::log2 (pointFreq[k] / 1000.0f); // +4.5 dB/oct tilt: pink noise looks flat
    }
}

void EqDisplay::timerCallback()
{
    const float dt = 1.0f / 30.0f;
    updateCurves();

    const auto wp = proc.tapOut.getWritePosition();
    const bool newData = wp != lastWritePos;
    lastWritePos = wp;

    auto smoothInto = [dt] (float* dst, const float* src)
    {
        for (int k = 0; k < kPoints; ++k)
            dst[k] = src[k] > dst[k] ? src[k] : std::max (src[k], dst[k] - 45.0f * dt);
    };

    if (proc.analyzerMode != 2)
    {
        float tmp[kPoints];
        if (newData)
            computeSpectrum (proc.tapOut, tmp);
        else
            std::fill (tmp, tmp + kPoints, -150.0f);
        smoothInto (specOut, tmp);

        if (proc.analyzerMode == 1)
        {
            if (newData)
                computeSpectrum (proc.tapPre, tmp);
            smoothInto (specPre, tmp);
        }
        else
        {
            if (newData)
                computeSpectrum (proc.tapWet, tmp);
            smoothInto (specWet, tmp);
            if (++frameCounter >= 4)
            {
                frameCounter = 0;
                if (newData && *std::max_element (tmp, tmp + kPoints) > -80.0f)
                {
                    Snapshot s;
                    std::copy (tmp, tmp + kPoints, s.db.begin());
                    history.push_back (s);
                    while (history.size() > 14)
                        history.pop_front();
                }
            }
        }
    }
    for (auto& s : history)
        s.age += dt;
    while (! history.empty() && history.front().age > 8.0f)
        history.pop_front();

    // output meter
    for (int ch = 0; ch < 2; ++ch)
    {
        const float pk = proc.meterPeak[ch].exchange (0.0f);
        const float db = 20.0f * std::log10 (pk + 1.0e-9f);
        meterLevel[ch] = db > meterLevel[ch] ? db : std::max (db, meterLevel[ch] - 30.0f * dt);
        if (db >= meterHold[ch])
        {
            meterHold[ch] = db;
            meterHoldCounter[ch] = 45;
        }
        else if (--meterHoldCounter[ch] <= 0)
            meterHold[ch] = std::max (-100.0f, meterHold[ch] - 20.0f * dt);
    }

    repaint();
}

//==============================================================================
void EqDisplay::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setColour (theme::display);
    g.fillRoundedRectangle (bounds, 6.0f);

    drawGrid (g);
    {
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (plot.toNearestInt());
        drawAnalyzer (g);
        drawCurves (g);
    }
    drawBands (g);
    drawScales (g);
    drawMeter (g);
    if (proc.showPiano)
        drawPiano (g);
    drawStrip (g);
    drawHoverInfo (g);

    if (! selectionRect.isEmpty())
    {
        g.setColour (theme::text.withAlpha (0.12f));
        g.fillRect (selectionRect);
        g.setColour (theme::text.withAlpha (0.5f));
        g.drawRect (selectionRect, 1.0f);
    }

    g.setColour (theme::outline);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 6.0f, 1.0f);
}

void EqDisplay::drawGrid (juce::Graphics& g)
{
    g.setColour (theme::grid);
    for (float f : { 30.0f, 40.0f, 50.0f, 60.0f, 70.0f, 80.0f, 90.0f, 100.0f, 200.0f, 300.0f, 400.0f, 500.0f, 600.0f, 700.0f, 800.0f, 900.0f,
                     1000.0f, 2000.0f, 3000.0f, 4000.0f, 5000.0f, 6000.0f, 7000.0f, 8000.0f, 9000.0f, 10000.0f })
    {
        const int fi = juce::roundToInt (f);
        const bool major = fi == 100 || fi == 1000 || fi == 10000;
        g.setColour (major ? theme::grid.brighter (0.25f) : theme::grid);
        const float x = xForFreq (f);
        g.drawVerticalLine (juce::roundToInt (x), plot.getY(), plot.getBottom());
    }
    const float range = rangeDb();
    const float step = range >= 30.0f ? 10.0f : (range >= 18.0f ? 6.0f : 3.0f);
    for (float db = -range; db <= range + 0.01f; db += step)
    {
        g.setColour (std::abs (db) < 0.01f ? theme::grid.brighter (0.45f) : theme::grid);
        g.drawHorizontalLine (juce::roundToInt (yForDb (db)), plot.getX(), plot.getRight());
    }

    g.setFont (theme::font (11.0f));
    g.setColour (theme::textDim);
    for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        const float x = xForFreq (f);
        g.drawText (freqLabel (f), juce::Rectangle<float> (x - 20.0f, plot.getBottom() + 2.0f, 40.0f, 14.0f), juce::Justification::centred);
    }
}

void EqDisplay::drawAnalyzer (juce::Graphics& g)
{
    if (proc.analyzerMode == 2)
        return;

    auto makePath = [this] (const float* db, bool closed)
    {
        juce::Path p;
        for (int k = 0; k < kPoints; ++k)
        {
            const float x = xForFreq (pointFreq[k]);
            const float y = juce::jlimit (plot.getY() - 2.0f, plot.getBottom() + 2.0f, yForAnalyzer (db[k]));
            if (k == 0)
                p.startNewSubPath (x, y);
            else
                p.lineTo (x, y);
        }
        if (closed)
        {
            p.lineTo (plot.getRight(), plot.getBottom() + 2.0f);
            p.lineTo (plot.getX(), plot.getBottom() + 2.0f);
            p.closeSubPath();
        }
        return p;
    };

    if (proc.analyzerMode == 0)
    {
        // decay visualisation: earlier reverb spectra falling with the decay time of each frequency
        for (const auto& s : history)
        {
            float vals[kPoints];
            float maxV = -200.0f;
            for (int k = 0; k < kPoints; ++k)
            {
                const float t60 = std::max (0.03f, t60Curve[k]);
                vals[k] = s.db[(size_t) k] - 60.0f * s.age / t60;
                maxV = std::max (maxV, vals[k]);
            }
            if (maxV < -95.0f)
                continue;
            const float alpha = juce::jlimit (0.0f, 1.0f, 1.0f - s.age / 8.0f);
            g.setColour (theme::yellow.withAlpha (0.07f * alpha));
            g.fillPath (makePath (vals, true));
            g.setColour (theme::yellow.withAlpha (0.22f * alpha));
            g.strokePath (makePath (vals, false), juce::PathStrokeType (1.0f));
        }
        g.setColour (theme::yellow.withAlpha (0.10f));
        g.fillPath (makePath (specWet, true));
    }
    else
    {
        g.setColour (theme::textDim.withAlpha (0.25f));
        g.fillPath (makePath (specPre, true));
    }

    g.setColour (juce::Colours::white.withAlpha (0.8f));
    g.strokePath (makePath (specOut, false), juce::PathStrokeType (1.2f));
}

void EqDisplay::drawCurves (juce::Graphics& g)
{
    const float cy = plot.getCentreY();

    auto curvePath = [this] (const float* values, bool decayKind)
    {
        juce::Path p;
        for (int k = 0; k < kPoints; ++k)
        {
            const float x = xForFreq (pointFreq[k]);
            const float y = juce::jlimit (plot.getY() - 4.0f, plot.getBottom() + 4.0f, decayKind ? yForRate (values[k]) : yForDb (values[k]));
            if (k == 0)
                p.startNewSubPath (x, y);
            else
                p.lineTo (x, y);
        }
        return p;
    };
    auto fillFor = [&] (const juce::Path& curve)
    {
        juce::Path f (curve);
        f.lineTo (plot.getRight(), cy);
        f.lineTo (plot.getX(), cy);
        f.closeSubPath();
        return f;
    };

    // individual curve of the selected post band
    if (selection.size() == 1 && selection.front().kind == Kind::Post)
    {
        const auto b = selection.front();
        nh::PostBandParams pb;
        auto& h = *post[(size_t) b.index];
        pb.used = true;
        pb.enabled = true;
        pb.shape = juce::roundToInt (h.shape.get());
        pb.freq = h.freq.get();
        pb.gain = h.gain.get();
        pb.q = h.q.get();
        pb.slope = juce::roundToInt (h.slope.get());
        const auto d = nh::designPostBand (pb, std::max (8000.0, proc.currentSampleRate.load()));
        float vals[kPoints];
        const double fs = std::max (8000.0, proc.currentSampleRate.load());
        for (int k = 0; k < kPoints; ++k)
            vals[k] = (float) (10.0 * std::log10 (std::max (d.magnitudeSqFromTan (std::tan (nh::kPi * std::min ((double) pointFreq[k], fs * 0.4999) / fs)), 1.0e-30)));
        const auto p = curvePath (vals, false);
        g.setColour (theme::yellow.withAlpha (0.15f));
        g.fillPath (fillFor (p));
    }

    const auto decayPath = curvePath (decayCurve, true);
    const auto postPath = curvePath (postCurve, false);

    g.setColour (theme::blue.withAlpha (0.10f));
    g.fillPath (fillFor (decayPath));
    g.setColour (theme::yellow.withAlpha (0.10f));
    g.fillPath (fillFor (postPath));

    // yellow first and wider, blue on top and thinner: both stay visible when they overlap
    g.setColour (theme::yellow.withAlpha (hoverCurve == Kind::Post ? 1.0f : 0.85f));
    g.strokePath (postPath, juce::PathStrokeType (hoverCurve == Kind::Post ? 4.0f : 3.2f));
    g.setColour (theme::blue.withAlpha (hoverCurve == Kind::Decay ? 1.0f : 0.9f));
    g.strokePath (decayPath, juce::PathStrokeType (hoverCurve == Kind::Decay ? 2.6f : 1.6f));
}

void EqDisplay::drawBands (juce::Graphics& g)
{
    for (auto b : allUsedBands())
    {
        const auto p = dotPosition (b);
        const auto c = bandColour (b);
        const bool sel = isSelected (b);
        const bool en = isEnabled (b);
        const float r = kDotRadius + (b == hoverBand ? 1.5f : 0.0f);
        if (sel)
        {
            g.setColour (c.withAlpha (0.35f));
            g.fillEllipse (p.x - r - 4.0f, p.y - r - 4.0f, (r + 4.0f) * 2.0f, (r + 4.0f) * 2.0f);
        }
        g.setColour (en ? c : theme::panelLight);
        g.fillEllipse (p.x - r, p.y - r, r * 2.0f, r * 2.0f);
        g.setColour (en ? juce::Colours::black.withAlpha (0.7f) : c);
        g.drawEllipse (p.x - r, p.y - r, r * 2.0f, r * 2.0f, 1.2f);
        g.setFont (theme::font (10.5f, true));
        g.setColour (en ? juce::Colours::black : c);
        g.drawText (juce::String (b.index + 1), juce::Rectangle<float> (p.x - r, p.y - r, r * 2.0f, r * 2.0f), juce::Justification::centred);
    }
}

void EqDisplay::drawScales (juce::Graphics& g)
{
    // left: decay rate (blue)
    const bool decayActive = (hoverBand.kind == Kind::Decay) || hoverCurve == Kind::Decay
                             || (! selection.empty() && selection.front().kind == Kind::Decay);
    const bool postActive = (hoverBand.kind == Kind::Post) || hoverCurve == Kind::Post
                            || (! selection.empty() && selection.front().kind == Kind::Post);
    g.setFont (theme::font (11.0f));
    for (float r : { 4.0f, 2.0f, 1.0f, 0.5f, 0.25f })
    {
        const float y = yForRate (r);
        g.setColour (theme::blue.withAlpha (decayActive ? 1.0f : 0.55f));
        g.drawText (juce::String (juce::roundToInt (r * 100.0f)) + "%", juce::Rectangle<float> (2.0f, y - 7.0f, 42.0f, 14.0f),
                    juce::Justification::centredRight);
    }
    // right: post EQ gain (yellow)
    const float range = rangeDb();
    const float step = range >= 30.0f ? 10.0f : (range >= 18.0f ? 6.0f : 3.0f);
    for (float db = -range; db <= range + 0.01f; db += step)
    {
        const float y = yForDb (db);
        g.setColour (theme::yellow.withAlpha (postActive ? 1.0f : 0.55f));
        g.drawText ((db > 0 ? "+" : "") + juce::String (juce::roundToInt (db)), juce::Rectangle<float> (plot.getRight() + 4.0f, y - 7.0f, 34.0f, 14.0f),
                    juce::Justification::centredLeft);
    }
    // display range button
    const auto rb = juce::Rectangle<float> (plot.getRight() + 4.0f, 4.0f, 42.0f, 20.0f);
    g.setColour (theme::panelLight);
    g.fillRoundedRectangle (rb, 3.0f);
    g.setColour (theme::yellow.withAlpha (0.8f));
    g.drawText (juce::String (juce::CharPointer_UTF8 ("\xc2\xb1")) + juce::String ((int) range), rb, juce::Justification::centred);
}

void EqDisplay::drawMeter (juce::Graphics& g)
{
    if (meterArea.isEmpty())
        return;
    const float w = (meterArea.getWidth() - 2.0f) * 0.5f;
    for (int ch = 0; ch < 2; ++ch)
    {
        auto bar = juce::Rectangle<float> (meterArea.getX() + (float) ch * (w + 2.0f), meterArea.getY(), w, meterArea.getHeight());
        g.setColour (theme::panelLight);
        g.fillRect (bar);
        auto yFor = [&] (float db) { return bar.getBottom() - juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 66.0f) * bar.getHeight(); };
        const float top = yFor (meterLevel[ch]);
        g.setColour (meterLevel[ch] > 0.0f ? theme::red : (meterLevel[ch] > -6.0f ? theme::yellow : theme::green.withAlpha (0.85f)));
        g.fillRect (juce::Rectangle<float> (bar.getX(), top, w, bar.getBottom() - top));
        g.setColour (meterHold[ch] > 0.0f ? theme::red : theme::text);
        g.fillRect (juce::Rectangle<float> (bar.getX(), yFor (meterHold[ch]) - 1.0f, w, 2.0f));
    }
}

void EqDisplay::drawPiano (juce::Graphics& g)
{
    if (pianoArea.isEmpty())
        return;
    g.setColour (theme::panel);
    g.fillRect (pianoArea);
    auto noteFreq = [] (double midi) { return (float) (440.0 * std::pow (2.0, (midi - 69.0) / 12.0)); };
    // 88 keys: A0 (21) .. C8 (108)
    for (int midi = 21; midi <= 108; ++midi)
    {
        const int pc = midi % 12;
        const bool black = pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
        const float x0 = xForFreq (noteFreq (midi - 0.5)), x1 = xForFreq (noteFreq (midi + 0.5));
        if (x1 < plot.getX() || x0 > plot.getRight())
            continue;
        auto key = juce::Rectangle<float> (x0, pianoArea.getY() + 2.0f, x1 - x0, pianoArea.getHeight() - 4.0f);
        if (black)
        {
            g.setColour (juce::Colour (0xff101216));
            g.fillRect (key.withHeight (key.getHeight() * 0.62f));
        }
        else
        {
            g.setColour (juce::Colour (0xffc9ced8));
            g.fillRect (key.reduced (0.4f, 0.0f));
        }
        if (pc == 0)
        {
            g.setColour (theme::textDim);
            g.setFont (theme::font (8.5f));
            g.drawText ("C" + juce::String (midi / 12 - 1), key.withTrimmedTop (key.getHeight() * 0.55f).expanded (4.0f, 0.0f),
                        juce::Justification::centred);
        }
    }
    for (auto b : allUsedBands())
    {
        const float x = xForFreq (freqOf (b));
        g.setColour (bandColour (b));
        g.fillEllipse (x - 3.5f, pianoArea.getBottom() - 9.0f, 7.0f, 7.0f);
    }
}

void EqDisplay::drawStrip (juce::Graphics& g)
{
    stripSegments.clear();
    const auto font = theme::font (12.0f);
    g.setFont (font);

    if (selection.size() != 1)
    {
        auto hintArea = juce::Rectangle<float> (plot.getX(), 4.0f, plot.getWidth(), 20.0f);
        g.setColour (theme::textDim);
        const juce::String hint = selection.size() > 1
                                      ? juce::String ((int) selection.size()) + " bands selected - drag to move, Del to delete"
                                      : juce::String ("Blue line: Decay Rate EQ  -  Yellow line: Post EQ  -  click a line or double-click to add a band");
        g.drawText (hint, hintArea, juce::Justification::centredLeft);
        return;
    }

    // parameter panel next to the selected band (Enable, Shape, Placement, Slope, values, delete)
    const auto b = selection.front();
    const auto c = bandColour (b);
    struct Seg
    {
        juce::String text;
        int action;
        bool highlight;
        float width;
    };
    std::vector<Seg> segs;
    auto add = [&] (const juce::String& text, int action, bool highlight, float minW = 0.0f)
    { segs.push_back ({ text, action, highlight, std::max (minW, juce::GlyphArrangement::getStringWidth (font, text) + 14.0f) }); };

    add (isEnabled (b) ? "ON" : "OFF", 0, isEnabled (b), 34.0f);
    add ((b.kind == Kind::Decay ? "DECAY " : "POST ") + juce::String (b.index + 1), -1, false, 64.0f);
    add ((b.kind == Kind::Decay ? params::decayShapeChoices() : params::postShapeChoices())[shapeOf (b)] + " v", 1, false);
    if (b.kind == Kind::Post)
    {
        add (params::placementChoices()[juce::roundToInt (post[(size_t) b.index]->place.get())] + " v", 2, false);
        const int sh = shapeOf (b);
        if (sh == nh::kPostLowCut || sh == nh::kPostHighCut)
            add (params::slopeChoices()[juce::roundToInt (post[(size_t) b.index]->slope.get())] + " v", 3, false);
    }
    juce::String values = params::formatFrequency (freqOf (b));
    if (b.kind == Kind::Decay)
    {
        if (shapeOf (b) != nh::kDecayNotch)
            values << "   " << juce::roundToInt (valueOf (b) * 100.0f) << " %";
    }
    else if (! usesQForVertical (b))
        values << "   " << (valueOf (b) >= 0.0f ? "+" : "") << juce::String (valueOf (b), 1) << " dB";
    values << "   Q " << juce::String (qOf (b), 2);
    add (values, 5, false);
    add ("X", 4, false, 24.0f);

    float total = 8.0f;
    for (const auto& sg : segs)
        total += sg.width + 4.0f;

    const auto dot = dotPosition (b);
    float y = dot.y - kDotRadius - 34.0f;
    if (y < plot.getY() - 22.0f)
        y = dot.y + kDotRadius + 10.0f;
    const float x0 = juce::jlimit (4.0f, (float) getWidth() - total - 4.0f, dot.x - total * 0.5f);
    auto panel = juce::Rectangle<float> (x0, y, total, 26.0f);
    g.setColour (theme::panel.withAlpha (0.95f));
    g.fillRoundedRectangle (panel, 5.0f);
    g.setColour (c.withAlpha (0.6f));
    g.drawRoundedRectangle (panel, 5.0f, 1.0f);

    float x = panel.getX() + 4.0f;
    for (const auto& sg : segs)
    {
        auto r = juce::Rectangle<float> (x, panel.getY() + 3.0f, sg.width, 20.0f);
        if (sg.action >= 0)
        {
            g.setColour (sg.highlight ? c.withAlpha (0.85f) : theme::panelLight);
            g.fillRoundedRectangle (r, 3.0f);
            g.setColour (sg.highlight ? juce::Colours::black : theme::text);
            stripSegments.push_back ({ r, sg.action });
        }
        else
            g.setColour (c);
        g.drawText (sg.text, r, juce::Justification::centred);
        x += sg.width + 4.0f;
    }
}

void EqDisplay::drawHoverInfo (juce::Graphics& g)
{
    if (! mouseInside || dragMode == DragMode::Rect)
        return;
    juce::String text;
    juce::Point<float> anchor = mousePos;
    if (hoverBand.valid())
    {
        text = bandDescription (hoverBand);
        anchor = dotPosition (hoverBand);
    }
    else if (dragMode != DragMode::None && primaryDrag.valid())
    {
        text = bandDescription (primaryDrag);
        anchor = dotPosition (primaryDrag);
    }
    else if (plot.contains (mousePos))
    {
        const float f = freqForX (mousePos.x);
        text = params::formatFrequency (f);
        if (proc.showPiano)
            text << "  " << params::noteNameForFrequency (f, true);
        text << "   decay " << juce::String (interpolatePoints (t60Curve, f), 2) << " s";
        if (hoverCurve == Kind::Decay)
            text << "   (click + drag: new Decay Rate band)";
        else if (hoverCurve == Kind::Post)
            text << "   (click + drag: new Post EQ band)";
    }
    if (text.isEmpty())
        return;

    const auto font = theme::font (12.0f);
    const float w = juce::GlyphArrangement::getStringWidth (font, text) + 16.0f;
    auto r = juce::Rectangle<float> (anchor.x + 12.0f, anchor.y - 30.0f, w, 20.0f);
    if (r.getRight() > plot.getRight())
        r.setX (anchor.x - w - 12.0f);
    if (r.getY() < plot.getY())
        r.setY (anchor.y + 12.0f);
    g.setColour (theme::panel.withAlpha (0.92f));
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (theme::outline);
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
    g.setColour (theme::text);
    g.setFont (font);
    g.drawText (text, r, juce::Justification::centred);
}
