#include "PluginEditor.h"

#include "Presets.h"

namespace
{
const char* analyzerModeNames[3] = { "Analyzer: Reverb + Post", "Analyzer: Pre + Post", "Analyzer: Off" };

void styleSmallButton (juce::TextButton& b)
{
    b.setColour (juce::TextButton::buttonColourId, theme::panelLight);
    b.setColour (juce::TextButton::buttonOnColourId, theme::yellow.withAlpha (0.85f));
}
} // namespace

//==============================================================================
IOPanel::IOPanel (juce::AudioProcessorValueTreeState& s)
    : inGain (s, ids::inGain, "Input Level", theme::teal, true),
      inPan (s, ids::inPan, "Input Pan", theme::teal, true),
      outGain (s, ids::outGain, "Output Level", theme::teal, true),
      outPan (s, ids::outPan, "Output Pan", theme::teal, true)
{
    for (auto* k : { &inGain, &inPan, &outGain, &outPan })
        addAndMakeVisible (*k);
}

void IOPanel::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (theme::panel.withAlpha (0.97f));
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (theme::outline);
    g.drawRoundedRectangle (r, 6.0f, 1.0f);
}

void IOPanel::resized()
{
    auto r = getLocalBounds().reduced (10, 8);
    const int w = r.getWidth() / 4;
    for (auto* k : { &inGain, &inPan, &outGain, &outPan })
        k->setBounds (r.removeFromLeft (w));
}

//==============================================================================
MainPanel::MainPanel (NachhallAudioProcessor& p)
    : proc (p), apvts (p.apvts), undoListener (p.undoManager),
      decayRate (apvts, ids::decayRate, "Decay Rate", theme::yellow),
      predelay (apvts, ids::predelay, "Predelay"),
      predelayOffset (apvts, ids::predelayOffset, "Predelay Offset"),
      character (apvts, ids::character, "Character"),
      thickness (apvts, ids::thickness, "Thickness", theme::teal, true),
      distance (apvts, ids::distance, "Distance"),
      brightness (apvts, ids::brightness, "Brightness", theme::teal, true),
      width (apvts, ids::width, "Stereo Width"),
      ducking (apvts, ids::ducking, "Ducking"),
      mix (apvts, ids::mix, "Mix", theme::yellow),
      space (apvts),
      predelaySync (apvts, ids::predelaySync, "Sync: "),
      style (apvts),
      freeze (apvts),
      gate (apvts),
      display (p),
      ioPanel (apvts),
      browser (p),
      midiOverlay (p)
{
    setSize (kWidth, kHeight);
    addMouseListener (&undoListener, true);

    for (auto* b : { &prevPreset, &nextPreset, &presetName, &savePresetButton, &irButton, &undoButton, &redoButton, &aButton, &bButton,
                     &copyButton, &helpButton, &analyzerButton, &pianoButton, &meterButton, &autoGainButton, &ioButton, &bypassButton,
                     &lockMixButton, &fullScreenButton, &midiLearnButton, &midiMenuButton, &sizeButton })
    {
        styleSmallButton (*b);
        addAndMakeVisible (*b);
    }

    prevPreset.onClick = [this] { stepPreset (-1); };
    nextPreset.onClick = [this] { stepPreset (1); };
    presetName.onClick = [this] { togglePresetBrowser(); };
    presetName.setTooltip ("Open the preset browser (search, categories, favourites)");
    savePresetButton.onClick = [this] { savePreset(); };
    irButton.onClick = [this] { importIR(); };
    irButton.setTooltip ("Import an impulse response (WAV/AIFF). It is analysed and converted into reverb settings. You can also drop a file onto the plug-in.");
    undoButton.onClick = [this] { proc.undoManager.undo(); };
    redoButton.onClick = [this] { proc.undoManager.redo(); };
    aButton.onClick = [this] { proc.selectABSlot (0); updateButtonStates(); };
    bButton.onClick = [this] { proc.selectABSlot (1); updateButtonStates(); };
    copyButton.onClick = [this] {
        proc.copyCurrentToOtherSlot();
        statusText = proc.getABSlot() == 0 ? "A copied to B" : "B copied to A";
        statusCountdown = 45;
        repaint();
    };
    helpButton.onClick = [this] { showHelpMenu(); };
    fullScreenButton.setTooltip ("Full Screen (Esc to leave)");
    fullScreenButton.onClick = [this] {
        if (onToggleFullScreen)
            onToggleFullScreen();
    };
    midiLearnButton.setClickingTogglesState (true);
    midiLearnButton.setTooltip ("MIDI Learn: click a control, then move a knob on your MIDI controller");
    midiLearnButton.onClick = [this] {
        midiOverlay.setVisible (midiLearnButton.getToggleState());
        if (midiOverlay.isVisible())
            midiOverlay.toFront (false);
    };
    midiMenuButton.setTooltip ("MIDI options: enable, clear, revert, save");
    midiMenuButton.onClick = [this] { showMidiMenu(); };
    sizeButton.setTooltip ("Interface size (also the default for new instances)");
    sizeButton.onClick = [this] { showSizeMenu(); };

    lockMixButton.setClickingTogglesState (true);
    lockMixButton.setToggleState (proc.isMixLocked(), juce::dontSendNotification);
    lockMixButton.onClick = [this] { proc.setMixLocked (lockMixButton.getToggleState()); };
    lockMixButton.setTooltip ("Lock Mix: keeps the Mix value when loading presets (remembered for new instances).");

    analyzerButton.onClick = [this] {
        proc.analyzerMode = (proc.analyzerMode + 1) % 3;
        updateButtonStates();
    };
    pianoButton.setClickingTogglesState (true);
    pianoButton.onClick = [this] {
        proc.showPiano = pianoButton.getToggleState();
        display.refreshLayout();
    };
    meterButton.setClickingTogglesState (true);
    meterButton.onClick = [this] {
        proc.showMeter = meterButton.getToggleState();
        display.refreshLayout();
    };
    autoGainButton.setClickingTogglesState (true);
    autoGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, ids::eqAutoGain, autoGainButton);
    autoGainButton.setTooltip ("Automatic gain compensation for the Post EQ");
    bypassButton.setClickingTogglesState (true);
    bypassButton.setColour (juce::TextButton::buttonOnColourId, theme::red);
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, ids::bypass, bypassButton);
    ioButton.setClickingTogglesState (true);
    ioButton.onClick = [this] { ioPanel.setVisible (ioButton.getToggleState()); };

    for (auto* k : { &decayRate, &predelay, &predelayOffset, &character, &thickness, &distance, &brightness, &width, &ducking, &mix })
        addAndMakeVisible (*k);
    addAndMakeVisible (space);
    addAndMakeVisible (predelaySync);
    addAndMakeVisible (style);
    addAndMakeVisible (freeze);
    addAndMakeVisible (gate);
    addAndMakeVisible (display);
    addChildComponent (ioPanel);
    addChildComponent (browser);
    browser.onClose = [this] { browser.setVisible (false); };
    browser.onPresetLoaded = [this] { updateButtonStates(); };

    addChildComponent (midiOverlay);
    midiOverlay.setTargets ({ { &space, ids::space }, { &decayRate, ids::decayRate }, { &predelay, ids::predelay },
                              { &predelayOffset, ids::predelayOffset }, { &character, ids::character }, { &thickness, ids::thickness },
                              { &distance, ids::distance }, { &brightness, ids::brightness }, { &width, ids::width },
                              { &ducking, ids::ducking }, { &mix, ids::mix }, { &style, ids::style }, { &freeze, ids::freeze },
                              { &gate, ids::gate } });
    midiOverlay.onClose = [this] {
        midiOverlay.setVisible (false);
        midiLearnButton.setToggleState (false, juce::dontSendNotification);
    };

    predelaySync.setTooltip ("Predelay tempo sync. When synced, the knob sets an offset (50 - 200 %, e.g. 150 % = dotted).");
    character.setTooltip ("Character: from clean and transparent to lively (modulation, reflections, echoes) and chorus-like");
    thickness.setTooltip ("Thickness: density and saturation - low = airy and sparse, high = dense with non-linear dynamics");
    distance.setTooltip ("Distance: close with bright early reflections, or far away with a longer build-up and a diffuse tail");
    brightness.setTooltip ("Brightness: high frequency absorption and tonal balance of the reverb");
    width.setTooltip ("Stereo Width: 0 % mono, 50 % maximum cross-feed, 100 % independent channels, above 100 % extra wide");
    ducking.setTooltip ("Ducking: lowers the reverb while the input plays (range in dB)");
    decayRate.setTooltip ("Decay Rate: scales the decay time of the selected space (25 - 400 %)");
    mix.setTooltip ("Mix: dry/wet balance. Use 100 % on a send.");

    pianoButton.setToggleState (proc.showPiano, juce::dontSendNotification);
    meterButton.setToggleState (proc.showMeter, juce::dontSendNotification);

    updateButtonStates();
    startTimerHz (10);
}

MainPanel::~MainPanel()
{
    stopTimer();
    removeMouseListener (&undoListener);
}

void MainPanel::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);

    // top bar
    auto top = getLocalBounds().removeFromTop (38).toFloat();
    g.setColour (theme::panel);
    g.fillRect (top);
    g.setFont (theme::font (21.0f, true));
    g.setColour (theme::yellow);
    g.drawText ("NACH", juce::Rectangle<float> (14.0f, 0.0f, 60.0f, 38.0f), juce::Justification::centredLeft);
    g.setColour (theme::text);
    g.drawText ("HALL", juce::Rectangle<float> (68.0f, 0.0f, 60.0f, 38.0f), juce::Justification::centredLeft);


    // knob section background
    auto knobs = juce::Rectangle<float> (8.0f, 42.0f, (float) kWidth - 16.0f, 252.0f);
    g.setColour (theme::panel);
    g.fillRoundedRectangle (knobs, 6.0f);
    g.setColour (theme::outline);
    g.drawRoundedRectangle (knobs, 6.0f, 1.0f);

    // separators around the space knob
    g.setColour (theme::outline);
    g.drawVerticalLine (362, 52.0f, 286.0f);
    g.drawVerticalLine (638, 52.0f, 286.0f);

    // bottom bar
    auto bottom = getLocalBounds().removeFromBottom (36).toFloat();
    g.setColour (theme::panel);
    g.fillRect (bottom);

    if (statusCountdown > 0)
    {
        g.setFont (theme::font (12.5f));
        g.setColour (theme::yellow);
        g.drawText (statusText, juce::Rectangle<float> (436.0f, bottom.getY(), 160.0f, bottom.getHeight()), juce::Justification::centredLeft);
    }
}

void MainPanel::resized()
{
    // ---- top bar
    prevPreset.setBounds (150, 7, 24, 24);
    presetName.setBounds (178, 7, 230, 24);
    nextPreset.setBounds (412, 7, 24, 24);
    savePresetButton.setBounds (444, 7, 52, 24);
    irButton.setBounds (502, 7, 76, 24);
    undoButton.setBounds (660, 7, 52, 24);
    redoButton.setBounds (716, 7, 52, 24);
    aButton.setBounds (786, 7, 28, 24);
    bButton.setBounds (816, 7, 28, 24);
    copyButton.setBounds (848, 7, 56, 24);
    fullScreenButton.setBounds (912, 7, 42, 24);
    helpButton.setBounds (958, 7, 28, 24);

    // ---- knob section
    const int colL[3] = { 16, 128, 240 };
    const int colR[3] = { 646, 758, 870 };
    const int kw = 108, row1 = 46, row1h = 112, row2 = 178, row2h = 110;

    decayRate.setBounds (colL[0], row1, kw, row1h);
    predelay.setBounds (colL[1], row1, kw, row1h);
    predelayOffset.setBounds (colL[1], row1, kw, row1h);
    predelaySync.setBounds (colL[1] + 14, row1 + row1h + 1, kw - 28, 18);
    character.setBounds (colL[2], row1, kw, row1h);
    thickness.setBounds (colL[0], row2, kw, row2h);
    distance.setBounds (colL[1], row2, kw, row2h);
    brightness.setBounds (colL[2], row2, kw, row2h);

    space.setBounds (370, 42, 260, 250);

    width.setBounds (colR[0], row1, kw, row1h);
    ducking.setBounds (colR[1], row1, kw, row1h);
    mix.setBounds (colR[2], row1, kw, row1h);
    lockMixButton.setBounds (colR[2] + 18, row1 + row1h + 1, kw - 36, 18);
    style.setBounds (colR[0] + 6, row2 + 8, colR[2] + kw - colR[0] - 12, 26);
    freeze.setBounds (colR[0] + 6, row2 + 46, 158, 50);
    gate.setBounds (colR[0] + 172, row2 + 46, 158, 50);

    // ---- display
    display.setBounds (8, 300, kWidth - 16, 300);

    // ---- bottom bar
    const int by = kHeight - 30;
    analyzerButton.setBounds (10, by, 176, 24);
    pianoButton.setBounds (192, by, 60, 24);
    meterButton.setBounds (256, by, 60, 24);
    autoGainButton.setBounds (322, by, 104, 24);
    midiLearnButton.setBounds (600, by, 86, 24);
    midiMenuButton.setBounds (688, by, 22, 24);
    sizeButton.setBounds (716, by, 56, 24);
    ioButton.setBounds (800, by, 80, 24);
    bypassButton.setBounds (886, by, 72, 24);

    ioPanel.setBounds (kWidth - 470, kHeight - 36 - 136, 460, 132);
    midiOverlay.setBounds (0, 0, kWidth, kHeight - 36);
    browser.setBounds (8, 42, kWidth - 16, kHeight - 42 - 40);
}

void MainPanel::updateButtonStates()
{
    analyzerButton.setButtonText (analyzerModeNames[juce::jlimit (0, 2, proc.analyzerMode)]);
    aButton.setToggleState (proc.getABSlot() == 0, juce::dontSendNotification);
    bButton.setToggleState (proc.getABSlot() == 1, juce::dontSendNotification);
    copyButton.setButtonText (proc.getABSlot() == 0 ? "A > B" : "B > A");
    undoButton.setEnabled (proc.undoManager.canUndo());
    redoButton.setEnabled (proc.undoManager.canRedo());
    // like Pro-R: the preset name is dimmed (and marked) once a parameter was changed
    const bool modified = proc.isPresetModified();
    presetName.setButtonText (proc.getCurrentPresetName() + (modified ? " *" : ""));
    presetName.setColour (juce::TextButton::textColourOffId, modified ? theme::textDim : theme::text);
    lockMixButton.setToggleState (proc.isMixLocked(), juce::dontSendNotification);
}

void MainPanel::timerCallback()
{
    updateButtonStates();
    const int sync = juce::roundToInt (apvts.getRawParameterValue (ids::predelaySync)->load());
    if (sync != lastPdSync)
    {
        lastPdSync = sync;
        predelay.setVisible (sync == 0);
        predelayOffset.setVisible (sync != 0);
    }
    if (statusCountdown > 0 && --statusCountdown == 0)
        repaint();
}

juce::Array<juce::File> MainPanel::userPresetFiles() const
{
    auto files = NachhallAudioProcessor::getUserPresetFolder().findChildFiles (juce::File::findFiles, false, "*.nhpreset");
    files.sort();
    return files;
}

void MainPanel::togglePresetBrowser()
{
    browser.setVisible (! browser.isVisible());
    if (browser.isVisible())
        browser.toFront (true);
}

void MainPanel::stepPreset (int delta)
{
    // combined list: factory presets followed by user presets
    const auto& list = factoryPresets();
    const auto user = userPresetFiles();
    const int total = (int) list.size() + user.size();
    int current = -1;
    for (size_t i = 0; i < list.size(); ++i)
        if (list[i].name == proc.getCurrentPresetName())
            current = (int) i;
    if (current < 0)
        for (int i = 0; i < user.size(); ++i)
            if (user[i].getFileNameWithoutExtension() == proc.getCurrentPresetName())
                current = (int) list.size() + i;
    const int next = ((current < 0 ? (delta > 0 ? -1 : 0) : current) + delta + total) % total;
    if (next < (int) list.size())
        proc.loadFactoryPreset (next);
    else
        loadPresetFile (user[next - (int) list.size()]);
    updateButtonStates();
}

void MainPanel::loadPresetFile (const juce::File& f)
{
    if (! proc.loadPresetFile (f))
    {
        statusText = "Preset could not be loaded";
        statusCountdown = 40;
        repaint();
    }
    updateButtonStates();
}

void MainPanel::savePreset()
{
    chooser = std::make_unique<juce::FileChooser> ("Save preset", NachhallAudioProcessor::getUserPresetFolder().getChildFile (proc.getCurrentPresetName() + ".nhpreset"),
                                                   "*.nhpreset");
    juce::Component::SafePointer<MainPanel> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [safe] (const juce::FileChooser& fc) {
                              if (safe == nullptr)
                                  return;
                              const auto f = fc.getResult();
                              if (f != juce::File())
                              {
                                  safe->statusText = safe->proc.savePresetFile (f) ? "Preset saved" : "Saving failed";
                                  safe->statusCountdown = 40;
                                  safe->updateButtonStates();
                                  safe->repaint();
                              }
                          });
}

void MainPanel::importIR()
{
    chooser = std::make_unique<juce::FileChooser> ("Import impulse response", juce::File::getSpecialLocation (juce::File::userHomeDirectory),
                                                   "*.wav;*.aif;*.aiff");
    juce::Component::SafePointer<MainPanel> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe] (const juce::FileChooser& fc) {
        if (safe != nullptr && fc.getResult().existsAsFile())
            safe->importIRFile (fc.getResult());
    });
}

void MainPanel::importIRFile (const juce::File& f)
{
    juce::String msg;
    const bool ok = proc.importImpulseResponse (f, msg);
    if (! ok)
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "IR Import", msg, "OK", this);
    statusText = ok ? msg : "IR import failed";
    statusCountdown = 60;
    updateButtonStates();
    repaint();
}

void MainPanel::showMidiMenu()
{
    juce::PopupMenu m;
    m.addItem (1, "Enable MIDI", true, proc.midiEnabled.load());
    juce::PopupMenu clear;
    const auto& ps = proc.getParameters();
    int count = 0;
    for (int cc = 0; cc < NachhallAudioProcessor::kNumControllers; ++cc)
    {
        const int idx = proc.getParameterForController (cc);
        if (juce::isPositiveAndBelow (idx, ps.size()))
        {
            clear.addItem (1000 + cc, "CC " + juce::String (cc) + ":  " + ps[idx]->getName (64));
            ++count;
        }
    }
    if (count > 0)
        clear.addSeparator();
    clear.addItem (2, "Clear all", count > 0);
    m.addSubMenu ("Clear", clear);
    m.addItem (3, "Revert (last saved mapping)");
    m.addItem (4, "Save mapping");
    juce::Component::SafePointer<MainPanel> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&midiMenuButton), [safe] (int r) {
        if (safe == nullptr || r == 0)
            return;
        auto& pr = safe->proc;
        if (r == 1)
            pr.midiEnabled = ! pr.midiEnabled.load();
        else if (r == 2)
            pr.clearMidiMapping (-1);
        else if (r == 3)
            pr.revertMidiMapping();
        else if (r == 4)
            pr.saveMidiMapping();
        else if (r >= 1000)
            pr.clearMidiMapping (r - 1000);
        safe->midiOverlay.repaint();
    });
}

void MainPanel::showSizeMenu()
{
    juce::PopupMenu m;
    const auto area = getDisplayArea ? getDisplayArea() : juce::Rectangle<int> (0, 0, 4000, 4000);
    const int sizes[3] = { 1000, 1250, 1500 };
    const char* names[3] = { "Medium", "Large", "Extra Large" };
    for (int i = 0; i < 3; ++i)
    {
        const int h = sizes[i] * kHeight / kWidth;
        const bool fits = sizes[i] <= area.getWidth() && h + 40 <= area.getHeight();
        m.addItem (i + 1, names[i] + juce::String ("  (") + juce::String (sizes[i]) + " x " + juce::String (h) + ")", fits,
                   std::abs (proc.editorWidth - sizes[i]) < 20);
    }
    juce::Component::SafePointer<MainPanel> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&sizeButton), [safe, sizes] (int r) {
        if (safe == nullptr || r == 0)
            return;
        const int w = sizes[r - 1];
        NachhallAudioProcessor::setDefaultEditorWidth (w); // remembered for new instances
        if (safe->onSetEditorWidth)
            safe->onSetEditorWidth (w);
    });
}

void MainPanel::showHelpMenu()
{
    juce::PopupMenu m;
    m.addItem (1, "Mouse & keyboard help");
    m.addItem (2, "Show output level meter", true, proc.showMeter);
    m.addSeparator();
    m.addItem (3, "About Nachhall");
    juce::Component::SafePointer<MainPanel> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&helpButton), [safe] (int r) {
        if (safe == nullptr)
            return;
        if (r == 1)
        {
            juce::AlertWindow::showMessageBoxAsync (
                juce::MessageBoxIconType::InfoIcon, "Mouse & keyboard",
                "Knobs: drag up/down (reacts to the speed), Shift = fine, Ctrl/Cmd + click = default,\n"
                "double click = type a value (\"1k\", \"A4\", \"2x\" = +6 dB, \"50%\" = middle), mouse wheel.\n"
                "Space: click the dots around the knob to jump to a room model.\n\n"
                "Display: click + drag the blue line = new Decay Rate EQ band, the yellow line = new Post EQ band.\n"
                "Double click background = Post EQ band (edges: low/high cut), bottom area = Decay notch.\n"
                "Drag a dot: frequency + gain/decay rate.  Shift = fine,  Alt = lock axis,  Ctrl/Cmd + drag = Q.\n"
                "Mouse wheel on a dot = Q.  Alt + click = bypass band.  Ctrl/Cmd + Alt + click = change shape.\n"
                "Right click a dot = band menu.  Double click a dot = type values.  Del = delete selected bands.\n"
                "Drag on the background = select several bands, Shift + click = range, Ctrl/Cmd + click = add.\n"
                "Alt + Shift + click on a cut filter = next slope.  Piano: click/drag a dot = snap to notes.\n\n"
                "Freeze: click = toggle, click and hold = momentary.\n"
                "Auto Gate: click = on/off, drag = hold time, right click = tempo sync.\n"
                "Ctrl/Cmd + Z = undo, Ctrl/Cmd + Shift + Z = redo.  Drop a WAV/AIFF file to import an impulse response.",
                "OK", safe.getComponent());
        }
        else if (r == 2)
        {
            safe->proc.showMeter = ! safe->proc.showMeter;
            safe->meterButton.setToggleState (safe->proc.showMeter, juce::dontSendNotification);
            safe->display.refreshLayout();
        }
        else if (r == 3)
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Nachhall " JucePlugin_VersionString,
                                                    "Algorithmic reverb with Decay Rate EQ, Post EQ, Freeze, Ducking and Auto Gate.\n"
                                                    "Styles: Modern, Vintage, Plate.\n\nBuilt with JUCE.",
                                                    "OK", safe.getComponent());
        }
    });
}

//==============================================================================
NachhallAudioProcessorEditor::NachhallAudioProcessorEditor (NachhallAudioProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    // read the stored size first: setResizeLimits() triggers resized(), which stores the current width
    const int w = juce::jlimit (700, 4000, proc.editorWidth);

    setLookAndFeel (&lnf);
    main = std::make_unique<MainPanel> (p);
    addAndMakeVisible (*main);
    main->onToggleFullScreen = [this] { toggleFullScreen(); };
    main->onSetEditorWidth = [this] (int newWidth) {
        fullScreen = false;
        setEditorWidth (newWidth);
    };
    main->getDisplayArea = [this] {
        if (auto* d = juce::Desktop::getInstance().getDisplays().getDisplayForRect (getScreenBounds()))
            return d->userArea;
        return juce::Rectangle<int> (0, 0, 1920, 1080);
    };

    setResizable (true, true);
    setResizeLimits (700, 448, std::max (2000, w), std::max (2000, w) * MainPanel::kHeight / MainPanel::kWidth);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) MainPanel::kWidth / (double) MainPanel::kHeight);
    setSize (w, juce::roundToInt ((float) w * (float) MainPanel::kHeight / (float) MainPanel::kWidth));
    setWantsKeyboardFocus (true);
    sizeInitialised = true;
}

NachhallAudioProcessorEditor::~NachhallAudioProcessorEditor()
{
    main.reset();
    setLookAndFeel (nullptr);
}

void NachhallAudioProcessorEditor::paint (juce::Graphics& g) { g.fillAll (theme::background); }

void NachhallAudioProcessorEditor::resized()
{
    const float scale = (float) getWidth() / (float) MainPanel::kWidth;
    main->setTransform (juce::AffineTransform::scale (scale));
    main->setBounds (0, 0, MainPanel::kWidth, MainPanel::kHeight);
    if (sizeInitialised)
        proc.editorWidth = getWidth();
}

void NachhallAudioProcessorEditor::setEditorWidth (int w)
{
    const int maxW = std::max (2000, w);
    setResizeLimits (700, 448, maxW, maxW * MainPanel::kHeight / MainPanel::kWidth);
    setSize (w, juce::roundToInt ((float) w * (float) MainPanel::kHeight / (float) MainPanel::kWidth));
}

void NachhallAudioProcessorEditor::toggleFullScreen()
{
    if (! fullScreen)
    {
        widthBeforeFullScreen = getWidth();
        juce::Rectangle<int> area (0, 0, 1920, 1080);
        if (auto* d = juce::Desktop::getInstance().getDisplays().getDisplayForRect (getScreenBounds()))
            area = d->userArea;
        const int w = std::min (area.getWidth(), (area.getHeight() - 40) * MainPanel::kWidth / MainPanel::kHeight);
        fullScreen = true;
        setEditorWidth (std::max (700, w));
        if (auto* top = getTopLevelComponent(); top != nullptr && top != this)
            top->setTopLeftPosition (area.getX(), area.getY());
    }
    else
    {
        fullScreen = false;
        setEditorWidth (widthBeforeFullScreen);
    }
}

bool NachhallAudioProcessorEditor::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey && fullScreen)
    {
        toggleFullScreen();
        return true;
    }
    if (k.getModifiers().isCommandDown() && k.getKeyCode() == 'Z')
    {
        if (k.getModifiers().isShiftDown())
            proc.undoManager.redo();
        else
            proc.undoManager.undo();
        return true;
    }
    if (k.getModifiers().isCommandDown() && k.getKeyCode() == 'Y')
    {
        proc.undoManager.redo();
        return true;
    }
    return false;
}

bool NachhallAudioProcessorEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (f.endsWithIgnoreCase (".wav") || f.endsWithIgnoreCase (".aif") || f.endsWithIgnoreCase (".aiff") || f.endsWithIgnoreCase (".nhpreset"))
            return true;
    return false;
}

void NachhallAudioProcessorEditor::filesDropped (const juce::StringArray& files, int, int)
{
    for (const auto& path : files)
    {
        const juce::File f (path);
        if (f.hasFileExtension (".nhpreset"))
        {
            main->loadPresetFile (f);
            return;
        }
        if (f.hasFileExtension (".wav;.aif;.aiff"))
        {
            main->importIRFile (f);
            return;
        }
    }
}
