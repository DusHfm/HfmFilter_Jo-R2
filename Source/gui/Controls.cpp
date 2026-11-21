#include "Controls.h"

#include "../Params.h"

namespace
{
constexpr float kStartAngle = juce::MathConstants<float>::pi * 1.25f;
constexpr float kEndAngle = juce::MathConstants<float>::pi * 2.75f;

void setupSlider (juce::Slider& s, juce::Colour colour, bool bipolar)
{
    s.setRotaryParameters (kStartAngle, kEndAngle, true);
    s.setColour (juce::Slider::rotarySliderFillColourId, colour);
    s.getProperties().set ("bipolar", bipolar);
    s.setPopupDisplayEnabled (false, false, nullptr);
    s.setScrollWheelEnabled (true);
}
} // namespace

//==============================================================================
bool parseTypedValue (juce::RangedAudioParameter& p, const juce::String& textIn, float& norm)
{
    const auto text = textIn.trim();
    if (text.isEmpty())
        return false;
    const auto number = text.retainCharacters ("0123456789.,-+").replaceCharacter (',', '.');
    const bool percentParam = p.getLabel() == "%";
    if (text.endsWithChar ('%') && ! percentParam)
    {
        // FabFilter: any value accepts a percentage, "50%" = middle position
        norm = juce::jlimit (0.0f, 1.0f, number.getFloatValue() * 0.01f);
        return number.isNotEmpty();
    }
    if (p.getLabel() == "dB" && text.endsWithIgnoreCase ("x"))
    {
        // "2x" = +6 dB (amplitude factor)
        const float f = number.getFloatValue();
        if (f <= 0.0f)
            return false;
        norm = p.convertTo0to1 (juce::jlimit (p.getNormalisableRange().start, p.getNormalisableRange().end, 20.0f * std::log10 (f)));
        return true;
    }
    norm = juce::jlimit (0.0f, 1.0f, p.getValueForText (text));
    return true;
}

InlineValueEditor::InlineValueEditor()
{
    setJustification (juce::Justification::centred);
    setFont (theme::font (13.0f, true));
    setColour (juce::TextEditor::backgroundColourId, theme::display);
    setColour (juce::TextEditor::outlineColourId, theme::yellow);
    setColour (juce::TextEditor::focusedOutlineColourId, theme::yellow);
    setColour (juce::TextEditor::textColourId, theme::text);
    setSelectAllWhenFocused (true);
    onReturnKey = [this] { commit(); };
    onEscapeKey = [this] { dismiss(); };
    onFocusLost = [this] { dismiss(); };
    setVisible (false);
}

void InlineValueEditor::begin (ParamHandle* h, juce::Rectangle<int> area)
{
    handle = h;
    setBounds (area);
    setText (h->text(), false);
    setVisible (true);
    toFront (true);
    grabKeyboardFocus();
    selectAll();
}

void InlineValueEditor::commit()
{
    if (handle != nullptr && isVisible())
    {
        float norm = 0.0f;
        if (parseTypedValue (*handle->param, getText(), norm))
            handle->setNormalisedWithGesture (norm);
    }
    dismiss();
}

void InlineValueEditor::dismiss()
{
    if (isVisible())
        setVisible (false);
}

FineSlider::FineSlider() : juce::Slider (juce::Slider::RotaryVerticalDrag, juce::Slider::NoTextBox)
{
    // like FabFilter knobs: the change follows the speed of the mouse movement
    setVelocityBasedMode (true);
    setVelocityModeParameters (1.0, 1, 0.0, false);
}

void FineSlider::mouseDown (const juce::MouseEvent& e)
{
    resetClick = e.mods.isCommandDown() && ! e.mods.isPopupMenu();
    if (resetClick)
    {
        if (onResetRequest)
            onResetRequest();
        return;
    }
    setVelocityModeParameters (e.mods.isShiftDown() ? 0.2 : 1.0, 1, 0.0, false);
    juce::Slider::mouseDown (e);
}

void FineSlider::mouseDrag (const juce::MouseEvent& e)
{
    if (resetClick)
        return;
    setVelocityModeParameters (e.mods.isShiftDown() ? 0.2 : 1.0, 1, 0.0, false);
    juce::Slider::mouseDrag (e);
}

void FineSlider::mouseDoubleClick (const juce::MouseEvent&)
{
    if (onDoubleClick)
        onDoubleClick();
}

void FineSlider::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    auto d = w;
    if (e.mods.isShiftDown())
    {
        d.deltaX *= 0.2f;
        d.deltaY *= 0.2f;
    }
    juce::Slider::mouseWheelMove (e, d);
}

//==============================================================================
Knob::Knob (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& t, juce::Colour colour, bool bipolar)
    : handle (state, paramId), paramID (paramId), title (t)
{
    setupSlider (slider, colour, bipolar);
    addAndMakeVisible (slider);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, paramId, slider);
    slider.setDoubleClickReturnValue (false, 0.0);
    slider.onValueChange = [this] { repaint(); };
    slider.onResetRequest = [this] { handle.resetToDefault(); };
    slider.onDoubleClick = [this] { valueEditor.begin (&handle, getLocalBounds().removeFromBottom (15).reduced (4, 0)); };
    addChildComponent (valueEditor);
}

void Knob::resized()
{
    auto r = getLocalBounds();
    r.removeFromBottom (30);
    const int size = std::min (r.getWidth(), r.getHeight());
    slider.setBounds (r.withSizeKeepingCentre (size, size));
}

void Knob::paint (juce::Graphics& g)
{
    auto r = getLocalBounds();
    auto textArea = r.removeFromBottom (30);
    g.setColour (theme::textDim);
    g.setFont (theme::font (12.5f));
    g.drawText (title, textArea.removeFromTop (15), juce::Justification::centred);
    g.setColour (slider.isMouseOverOrDragging() ? theme::yellow : theme::text);
    g.setFont (theme::font (13.0f, true));
    g.drawText (handle.text(), textArea, juce::Justification::centred);
}

//==============================================================================
SpaceKnob::SpaceKnob (juce::AudioProcessorValueTreeState& state)
    : apvts (state), space (state, ids::space), decayRate (state, ids::decayRate), freeze (state, ids::freeze), style (state, ids::style)
{
    setupSlider (slider, theme::yellow, false);
    addAndMakeVisible (slider);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, ids::space, slider);
    slider.setDoubleClickReturnValue (false, 0.0);
    slider.onResetRequest = [this] { space.resetToDefault(); };
    slider.onDoubleClick = [this] { valueEditor.begin (&space, juce::Rectangle<int> (getWidth() / 2 - 70, 2, 140, 24)); };
    addChildComponent (valueEditor);
    slider.setTooltip ("Space: room model + decay time. Double click: type a time (\"2.5 s\") or a room name (\"Cathedral\"). Ctrl/Cmd + click: default.");
    startTimerHz (20);
}

void SpaceKnob::resized()
{
    auto r = getLocalBounds().toFloat();
    r.removeFromTop (44.0f);
    r.removeFromBottom (22.0f);
    const float size = std::min (r.getWidth(), r.getHeight()) - 26.0f;
    knobArea = r.withSizeKeepingCentre (size, size);
    slider.setBounds (knobArea.toNearestInt());
}

// Pro-R 2 shows 6 room icons around the Space knob (0.20 / 0.75 / 1.85 / 3.20 / 5.20 / 10.0 s)
juce::Point<float> SpaceKnob::dotPosition (int index) const
{
    const float t = nh::spaceForRoomIndex (nh::kRoomIconModel[index]);
    const float a = kStartAngle + t * (kEndAngle - kStartAngle);
    const float radius = knobArea.getWidth() * 0.5f + 10.0f;
    const auto c = knobArea.getCentre();
    return { c.x + radius * std::sin (a), c.y - radius * std::cos (a) };
}

float SpaceKnob::dotRadius (int index) const { return 1.8f + 2.6f * (float) index / (float) (nh::kNumRoomIcons - 1); }

int SpaceKnob::dotAt (juce::Point<float> p) const
{
    for (int i = 0; i < nh::kNumRoomIcons; ++i)
        if (dotPosition (i).getDistanceFrom (p) < 7.0f)
            return i;
    return -1;
}

void SpaceKnob::paint (juce::Graphics& g)
{
    const float sp = space.get();
    const float rate = decayRate.get() * 0.01f;
    const bool frozen = freeze.get() > 0.5f;
    const auto room = nh::interpolateRoom (sp);
    const int nearest = nh::nearestRoomIndex (sp);
    int nearestIcon = 0;
    for (int i = 1; i < nh::kNumRoomIcons; ++i)
        if (std::abs (nh::spaceForRoomIndex (nh::kRoomIconModel[i]) - sp) < std::abs (nh::spaceForRoomIndex (nh::kRoomIconModel[nearestIcon]) - sp))
            nearestIcon = i;

    // decay time (yellow) above the knob
    auto top = getLocalBounds().removeFromTop (44);
    g.setColour (theme::yellow);
    g.setFont (theme::font (22.0f, true));
    const float t60 = room.t60 * rate;
    const juce::String timeText = frozen ? juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x9e")) // infinity
                                         : (t60 < 10.0f ? juce::String (t60, 2) : juce::String (t60, 1)) + " s";
    g.drawText (timeText, top.removeFromTop (26), juce::Justification::centred);
    g.setFont (theme::font (12.0f));
    g.setColour (theme::textDim);
    if (frozen)
        g.drawText ("Freeze", top, juce::Justification::centred);
    else if (std::abs (rate - 1.0f) > 0.005f)
        g.drawText (juce::String (room.t60, 2) + " s x " + juce::String (juce::roundToInt (rate * 100.0f)) + " %", top, juce::Justification::centred);

    // room icons
    for (int i = 0; i < nh::kNumRoomIcons; ++i)
    {
        const auto p = dotPosition (i);
        const float r = dotRadius (i) + (i == hoverDot ? 1.5f : 0.0f);
        const bool on = i == nearestIcon && std::abs (nh::spaceForRoomIndex (nh::kRoomIconModel[i]) - sp) < 0.01f;
        g.setColour (on ? theme::yellow : (i == hoverDot ? theme::text : theme::textDim.withAlpha (0.6f)));
        g.fillEllipse (p.x - r, p.y - r, 2.0f * r, 2.0f * r);
    }

    // room name below
    auto bottom = getLocalBounds().removeFromBottom (22);
    g.setColour (theme::text);
    g.setFont (theme::font (14.0f, true));
    const int shown = hoverDot >= 0 ? nh::kRoomIconModel[hoverDot] : nearest;
    juce::String name = nh::roomModels()[shown].name;
    if (juce::roundToInt (style.get()) == nh::kStylePlate)
    {
        const float s = hoverDot >= 0 ? nh::spaceForRoomIndex (nh::kRoomIconModel[hoverDot]) : sp;
        name = s < 0.34f ? "Small Plate" : (s < 0.67f ? "Medium Plate" : "Large Plate");
    }
    g.drawText (name, bottom, juce::Justification::centred);
}

void SpaceKnob::mouseMove (const juce::MouseEvent& e)
{
    const int d = dotAt (e.position);
    if (d != hoverDot)
    {
        hoverDot = d;
        repaint();
    }
}

void SpaceKnob::mouseExit (const juce::MouseEvent&)
{
    hoverDot = -1;
    repaint();
}

void SpaceKnob::mouseDown (const juce::MouseEvent& e)
{
    const int d = dotAt (e.position);
    if (d >= 0)
        space.setWithGesture (nh::spaceForRoomIndex (nh::kRoomIconModel[d]));
}

void SpaceKnob::timerCallback()
{
    const float sp = space.get(), rate = decayRate.get();
    const bool fr = freeze.get() > 0.5f;
    const int st = juce::roundToInt (style.get());
    if (std::abs (sp - lastSpace) > 1.0e-6f || std::abs (rate - lastRate) > 1.0e-6f || fr != lastFreeze || st != lastStyle)
    {
        lastStyle = st;
        lastSpace = sp;
        lastRate = rate;
        lastFreeze = fr;
        repaint();
    }
}

//==============================================================================
StyleSelector::StyleSelector (juce::AudioProcessorValueTreeState& state) : style (state, ids::style)
{
    setTooltip ("Reverb style: Modern (natural spaces), Vintage (early digital character), Plate (fast, dense, metallic)");
    startTimerHz (15);
}

void StyleSelector::paint (juce::Graphics& g)
{
    const int current = juce::roundToInt (style.get());
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (theme::panelLight);
    g.fillRoundedRectangle (r, 4.0f);
    const float w = r.getWidth() / 3.0f;
    for (int i = 0; i < 3; ++i)
    {
        auto seg = juce::Rectangle<float> (r.getX() + w * (float) i, r.getY(), w, r.getHeight());
        if (i == current)
        {
            g.setColour (theme::yellow.withAlpha (0.9f));
            g.fillRoundedRectangle (seg.reduced (2.0f), 3.0f);
        }
        g.setColour (i == current ? juce::Colours::black : theme::text);
        g.setFont (theme::font (12.5f, i == current));
        g.drawText (params::styleChoices()[i], seg, juce::Justification::centred);
    }
    g.setColour (theme::outline);
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
}

void StyleSelector::mouseDown (const juce::MouseEvent& e)
{
    const int seg = juce::jlimit (0, 2, (int) (e.position.x / (float) getWidth() * 3.0f));
    style.setWithGesture ((float) seg);
}

void StyleSelector::timerCallback()
{
    const int c = juce::roundToInt (style.get());
    if (c != last)
    {
        last = c;
        repaint();
    }
}

//==============================================================================
FreezeButton::FreezeButton (juce::AudioProcessorValueTreeState& state) : freeze (state, ids::freeze)
{
    setTooltip ("Freeze: endless sustain of the current reverb. Click to toggle, click and hold for momentary freeze.");
    startTimerHz (15);
}

void FreezeButton::paint (juce::Graphics& g)
{
    const bool on = freeze.get() > 0.5f;
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    const juce::Colour ice (0xff8fd8ff);
    g.setColour (on ? ice.withAlpha (0.9f) : theme::panelLight);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (on ? ice.brighter (0.4f) : theme::outline);
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
    g.setColour (on ? juce::Colours::black : theme::text);
    g.setFont (theme::font (13.0f, true));
    g.drawText ("FREEZE", r, juce::Justification::centred);
}

void FreezeButton::mouseDown (const juce::MouseEvent&)
{
    wasOnAtDown = freeze.get() > 0.5f;
    downTime = juce::Time::getMillisecondCounter();
    freeze.setWithGesture (wasOnAtDown ? 0.0f : 1.0f);
    repaint();
}

void FreezeButton::mouseUp (const juce::MouseEvent&)
{
    // click and hold = momentary freeze
    if (! wasOnAtDown && juce::Time::getMillisecondCounter() - downTime > 400)
        freeze.setWithGesture (0.0f);
    repaint();
}

void FreezeButton::timerCallback()
{
    const bool on = freeze.get() > 0.5f;
    if (on != last)
    {
        last = on;
        repaint();
    }
}

//==============================================================================
GateButton::GateButton (juce::AudioProcessorValueTreeState& state)
    : gate (state, ids::gate), hold (state, ids::gateHold), sync (state, ids::gateSync)
{
    setTooltip ("Auto Gate: cuts the reverb after the input stops. Click = on/off, drag up/down = hold time, right click = tempo sync.");
    startTimerHz (15);
}

void GateButton::paint (juce::Graphics& g)
{
    const bool on = gate.get() > 0.5f;
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (on ? theme::yellow.withAlpha (0.9f) : theme::panelLight);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (on ? theme::yellow.brighter (0.3f) : theme::outline);
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
    g.setColour (on ? juce::Colours::black : theme::text);
    auto top = r.removeFromTop (r.getHeight() * 0.55f);
    g.setFont (theme::font (12.5f, true));
    g.drawText ("AUTO GATE", top, juce::Justification::centredBottom);
    g.setFont (theme::font (11.5f));
    const int s = juce::roundToInt (sync.get());
    g.drawText (s > 0 ? params::gateSyncChoices()[s] : hold.text(), r, juce::Justification::centredTop);
}

void GateButton::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        showSyncMenu();
        return;
    }
    dragging = false;
    startHold = hold.get();
    startSync = juce::roundToInt (sync.get());
}

void GateButton::mouseDrag (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        return;
    const float dy = (float) e.getDistanceFromDragStartY();
    if (! dragging && std::abs (dy) > 3.0f)
    {
        dragging = true;
        hold.begin();
        sync.begin();
    }
    if (! dragging)
        return;
    if (startSync > 0)
        sync.set ((float) juce::jlimit (1, params::gateSyncChoices().size() - 1, startSync - juce::roundToInt (dy / 18.0f)));
    else
        hold.set (juce::jlimit (10.0f, 3000.0f, startHold * std::pow (2.0f, -dy / (e.mods.isShiftDown() ? 240.0f : 60.0f))));
    repaint();
}

void GateButton::mouseUp (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        return;
    if (dragging)
    {
        hold.end();
        sync.end();
    }
    else
        gate.setWithGesture (gate.get() > 0.5f ? 0.0f : 1.0f);
    dragging = false;
    repaint();
}

void GateButton::showSyncMenu()
{
    juce::PopupMenu m;
    m.addSectionHeader ("Gate hold time");
    const int current = juce::roundToInt (sync.get());
    for (int i = 0; i < params::gateSyncChoices().size(); ++i)
        m.addItem (i + 1, i == 0 ? juce::String ("Free (ms)") : "Sync " + params::gateSyncChoices()[i], true, i == current);
    juce::Component::SafePointer<GateButton> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safe] (int r) {
        if (safe != nullptr && r > 0)
        {
            safe->sync.setWithGesture ((float) (r - 1));
            safe->repaint();
        }
    });
}

void GateButton::timerCallback()
{
    const bool on = gate.get() > 0.5f;
    const int s = juce::roundToInt (sync.get());
    const auto t = s > 0 ? params::gateSyncChoices()[s] : hold.text();
    if (on != lastOn || t != lastText)
    {
        lastOn = on;
        lastText = t;
        repaint();
    }
}

//==============================================================================
ChoiceButton::ChoiceButton (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& p)
    : choice (state, paramId), prefix (p)
{
    startTimerHz (15);
}

void ChoiceButton::paint (juce::Graphics& g)
{
    const int c = juce::roundToInt (choice.get());
    const bool active = c > 0;
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (active ? theme::yellow.withAlpha (0.85f) : theme::panelLight);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (theme::outline);
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
    g.setColour (active ? juce::Colours::black : theme::text);
    g.setFont (theme::font (12.0f, active));
    g.drawText (prefix + choice.text(), r, juce::Justification::centred);
}

void ChoiceButton::mouseDown (const juce::MouseEvent&)
{
    juce::PopupMenu m;
    const auto* cp = dynamic_cast<juce::AudioParameterChoice*> (choice.param);
    if (cp == nullptr)
        return;
    const int current = juce::roundToInt (choice.get());
    for (int i = 0; i < cp->choices.size(); ++i)
        m.addItem (i + 1, cp->choices[i], true, i == current);
    juce::Component::SafePointer<ChoiceButton> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safe] (int r) {
        if (safe != nullptr && r > 0)
        {
            safe->choice.setWithGesture ((float) (r - 1));
            safe->repaint();
        }
    });
}

void ChoiceButton::timerCallback()
{
    const int c = juce::roundToInt (choice.get());
    if (c != last)
    {
        last = c;
        repaint();
    }
}
