// Loads the built Nachhall.vst3 through JUCE's VST3 hosting code (like a DAW / NI Maschine would) and checks:
//   description, bus layouts, parameter list (Maschine page order), processing at several sample rates and
//   block sizes with random automation, state round trip, all factory programs, editor open/close.
// Usage (Linux: run under xvfb-run): NachhallHostTest /path/to/Nachhall.vst3
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdio>

namespace
{
int failures = 0;

void check (bool ok, const juce::String& what)
{
    std::printf ("  [%s] %s\n", ok ? " OK " : "FAIL", what.toRawUTF8());
    if (! ok)
        ++failures;
}

bool processSeconds (juce::AudioPluginInstance& p, double fs, int block, double seconds, juce::Random& rng, bool automate, float& peakOut)
{
    juce::AudioBuffer<float> buf (2, block);
    juce::MidiBuffer midi;
    const int blocks = (int) (seconds * fs / block) + 1;
    auto params = p.getParameters();
    bool finite = true;
    for (int b = 0; b < blocks; ++b)
    {
        if (automate && b % 8 == 0)
            for (int k = 0; k < 6; ++k)
            {
                auto* prm = params[rng.nextInt (params.size())];
                const auto name = prm->getName (64);
                if (prm != p.getBypassParameter() && ! name.contains ("Level") && ! name.contains ("Gain") && ! name.startsWith ("MIDI CC"))
                    prm->setValue (rng.nextFloat());
            }
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < block; ++i)
                buf.setSample (ch, i, (b / 50) % 2 == 0 ? (rng.nextFloat() * 2.0f - 1.0f) * 0.5f : 0.0f);
        p.processBlock (buf, midi);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < block; ++i)
            {
                const float v = buf.getSample (ch, i);
                if (! std::isfinite (v))
                    finite = false;
                peakOut = std::max (peakOut, std::abs (v));
            }
    }
    return finite;
}
} // namespace

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::printf ("usage: NachhallHostTest <Nachhall.vst3>\n");
        return 2;
    }
    juce::ScopedJuceInitialiser_GUI init;
    const juce::String path (argv[1]);

    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> descs;
    format.findAllTypesForFile (descs, path);

    std::printf ("\n== Description ==\n");
    check (descs.size() == 1, "exactly one plug-in in the bundle (found " + juce::String (descs.size()) + ")");
    if (descs.isEmpty())
        return 1;
    const auto& d = *descs[0];
    std::printf ("  name '%s', manufacturer '%s', category '%s', version %s\n", d.name.toRawUTF8(), d.manufacturerName.toRawUTF8(),
                 d.category.toRawUTF8(), d.version.toRawUTF8());
    check (d.name == "Nachhall", "plug-in name");
    check (! d.isInstrument, "registered as effect, not instrument");
    check (d.category.containsIgnoreCase ("Reverb"), "VST3 category contains Reverb");

    juce::String error;
    auto instance = format.createInstanceFromDescription (d, 48000.0, 512, error);
    check (instance != nullptr, "instance created " + error);
    if (instance == nullptr)
        return 1;

    std::printf ("\n== Buses ==\n");
    {
        juce::AudioProcessor::BusesLayout stereo;
        stereo.inputBuses.add (juce::AudioChannelSet::stereo());
        stereo.outputBuses.add (juce::AudioChannelSet::stereo());
        check (instance->checkBusesLayoutSupported (stereo), "stereo in -> stereo out");
        juce::AudioProcessor::BusesLayout monoStereo;
        monoStereo.inputBuses.add (juce::AudioChannelSet::mono());
        monoStereo.outputBuses.add (juce::AudioChannelSet::stereo());
        check (instance->checkBusesLayoutSupported (monoStereo), "mono in -> stereo out");
        check (instance->setBusesLayout (stereo), "set stereo layout");
    }

    std::printf ("\n== Parameters ==\n");
    {
        auto params = instance->getParameters();
        int own = 0, midiHelpers = 0, midiHelpersAutomatable = 0;
        for (auto* p : params)
        {
            if (p->getName (64).startsWith ("MIDI CC"))
            {
                ++midiHelpers;
                if (p->isAutomatable())
                    ++midiHelpersAutomatable;
            }
            else
                ++own;
        }
        std::printf ("  %d plug-in parameters (+ %d hidden MIDI CC helpers for VST3 MIDI Learn)\n", own, midiHelpers);
        check (own >= 100, "all parameters exported");
        check (midiHelpersAutomatable == 0, "MIDI CC helpers are not automatable (hosts do not list them)");
        juce::StringArray firstPage;
        for (int i = 0; i < std::min (8, params.size()); ++i)
            firstPage.add (params[i]->getName (64));
        std::printf ("  Maschine page 1: %s\n", firstPage.joinIntoString (" | ").toRawUTF8());

        check (firstPage[0] == "Space" && firstPage[1] == "Decay Rate" && firstPage[2] == "Mix", "main controls come first");
        check (instance->getBypassParameter() != nullptr, "host bypass parameter mapped");
        bool textOk = true;
        for (auto* p : params)
            if (p->getText (p->getValue(), 64).isEmpty())
                textOk = false;
        check (textOk, "every parameter has a value text");
        check (instance->getTailLengthSeconds() > 0.1, "reports a reverb tail (" + juce::String (instance->getTailLengthSeconds(), 2) + " s)");
    }

    std::printf ("\n== MIDI ==\n");
    {
        check (instance->acceptsMidi(), "accepts MIDI (for MIDI Learn)");
        instance->prepareToPlay (48000.0, 512);
        juce::AudioBuffer<float> buf (2, 512);
        bool finite = true;
        for (int b = 0; b < 50; ++b)
        {
            juce::MidiBuffer midi;
            for (int k = 0; k < 8; ++k)
                midi.addEvent (juce::MidiMessage::controllerEvent (1, 20 + k, (b * 7 + k * 13) % 128), k * 50);
            buf.clear();
            buf.setSample (0, 0, 0.5f);
            instance->processBlock (buf, midi);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 512; ++i)
                    finite = finite && std::isfinite (buf.getSample (ch, i));
        }
        check (finite, "processing with a stream of MIDI controllers");
    }

    std::printf ("\n== Processing ==\n");
    {
        juce::Random rng (7);
        for (double fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
            for (int block : { 1, 17, 64, 512, 2048 })
            {
                instance->releaseResources();
                instance->setRateAndBufferSizeDetails (fs, block);
                instance->prepareToPlay (fs, block);
                float peak = 0.0f;
                const double seconds = block == 1 ? 0.2 : 1.5;
                const bool ok = processSeconds (*instance, fs, block, seconds, rng, true, peak);
                check (ok && peak < 100.0f, juce::String (fs / 1000.0, 1) + " kHz, block " + juce::String (block) + ": finite, peak " + juce::String (peak, 2));
            }
    }

    std::printf ("\n== State round trip ==\n");
    {
        juce::Random rng (99);
        auto params = instance->getParameters();
        instance->prepareToPlay (48000.0, 512);
        for (auto* p : params)
        {
            const auto name = p->getName (64);
            if (p != instance->getBypassParameter() && ! name.contains ("Level") && ! name.contains ("Gain") && ! name.startsWith ("MIDI CC"))
            {
                p->beginChangeGesture();
                p->setValue (rng.nextFloat());
                p->endChangeGesture();
            }
        }
        float peak = 0.0f;
        processSeconds (*instance, 48000.0, 512, 0.05, rng, false, peak); // deliver the changes to the processor
        juce::MessageManager::getInstance()->runDispatchLoopUntil (100);

        juce::MemoryBlock state;
        instance->getStateInformation (state);
        check (state.getSize() > 100, "state saved (" + juce::String ((int) state.getSize()) + " bytes)");

        auto second = format.createInstanceFromDescription (d, 48000.0, 512, error);
        second->setStateInformation (state.getData(), (int) state.getSize());
        juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
        auto params2 = second->getParameters();
        int mismatches = 0;
        for (int i = 0; i < params.size(); ++i)
        {
            // JUCE's hidden "MIDI CC" helper parameters (VST3 MIDI controller emulation) are not part of the state
            if (params[i]->getName (64).startsWith ("MIDI CC"))
                continue;
            // compare the displayed values: choice parameters snap their normalised value
            const auto a = params[i]->getCurrentValueAsText();
            const auto b = params2[i]->getCurrentValueAsText();
            if (a != b)
            {
                if (++mismatches <= 6)
                    std::printf ("    %s: '%s' vs '%s'\n", params[i]->getName (64).toRawUTF8(), a.toRawUTF8(), b.toRawUTF8());
            }
        }
        check (mismatches == 0, "all parameters restored (" + juce::String (mismatches) + " mismatches)");
    }

    std::printf ("\n== State restoration (same procedure as pluginval) ==\n");
    {
        juce::Random rng (1234);
        auto params = instance->getParameters();
        auto automatable = [&] (juce::AudioProcessorParameter* p) { return p != instance->getBypassParameter() && p->isAutomatable(); };
        for (auto* p : params)
            if (automatable (p))
                p->setValue (rng.nextFloat());
        juce::MemoryBlock original;
        instance->getStateInformation (original);
        std::vector<float> originalValues;
        for (auto* p : params)
            originalValues.push_back (p->getValue());
        for (auto* p : params)
            if (automatable (p))
                p->setValue (rng.nextFloat());
        instance->setStateInformation (original.getData(), (int) original.getSize());
        juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
        int failed = 0;
        for (int i = 0; i < params.size(); ++i)
            if (automatable (params[i]) && std::abs (params[i]->getValue() - originalValues[(size_t) i]) > 0.1f)
            {
                if (++failed <= 6)
                    std::printf ("    %s: expected %.3f, got %.3f\n", params[i]->getName (64).toRawUTF8(), originalValues[(size_t) i],
                                 params[i]->getValue());
            }
        check (failed == 0, "all automatable parameters restored within 0.1 (" + juce::String (failed) + " failed)");
    }

    std::printf ("\n== Factory programs ==\n");
    {
        instance->prepareToPlay (48000.0, 512);
        const int n = instance->getNumPrograms();
        std::printf ("  %d programs\n", n);
        check (n >= 30, "factory presets exposed as programs");
        juce::Random rng (3);
        bool allOk = true;
        for (int i = 0; i < n; ++i)
        {
            instance->setCurrentProgram (i);
            float peak = 0.0f;
            if (! processSeconds (*instance, 48000.0, 512, 0.25, rng, false, peak) || peak > 20.0f)
            {
                allOk = false;
                std::printf ("    program %d '%s' peak %.2f\n", i, instance->getProgramName (i).toRawUTF8(), peak);
            }
        }
        check (allOk, "all programs process cleanly");
    }

    const bool testEditor = argc > 2 && juce::String (argv[2]) == "--editor";
    if (testEditor)
        std::printf ("\n== Editor ==\n");
    for (int round = 0; testEditor && round < 3; ++round)
    {
        std::unique_ptr<juce::AudioProcessorEditor> ed (instance->createEditorIfNeeded());
        check (ed != nullptr, "editor created (round " + juce::String (round + 1) + ")");
        if (ed == nullptr)
            break;
        std::printf ("  editor size %d x %d\n", ed->getWidth(), ed->getHeight());
        juce::DocumentWindow w ("test", juce::Colours::black, 0);
        w.setContentNonOwned (ed.get(), true);
        w.setVisible (true);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (400);
        float peak = 0.0f;
        juce::Random rng (round);
        processSeconds (*instance, 48000.0, 512, 0.5, rng, true, peak);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (200);
        w.clearContentComponent();
        ed.reset();
    }

    instance->releaseResources();
    instance.reset();
    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL HOST TESTS PASSED" : "HOST TESTS FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
