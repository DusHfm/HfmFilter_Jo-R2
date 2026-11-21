// Developer tool: renders the editor with live analyser data into a PNG (run under Xvfb on Linux).
// Usage: NachhallUiShot <out.png> [scenario 0..2] [width]
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "../Source/PluginEditor.h"
#include "../Source/PluginProcessor.h"

class UiShotApp : public juce::JUCEApplication, private juce::Timer
{
public:
    const juce::String getApplicationName() override { return "NachhallUiShot"; }
    const juce::String getApplicationVersion() override { return "1.0"; }

    void initialise (const juce::String& commandLine) override
    {
        auto args = juce::StringArray::fromTokens (commandLine, true);
        args.removeEmptyStrings();

        // short parameter names (VST3 shortTitle): NachhallUiShot --names
        if (args.size() > 0 && args[0] == "--names")
        {
            NachhallAudioProcessor p;
            juce::StringArray names;
            bool ok = true;
            for (auto* prm : p.getParameters())
            {
                const auto n = prm->getName (8);
                names.add (n);
                ok = ok && n.isNotEmpty() && n.length() <= 8;
            }
            std::printf ("%s\n%s\n", names.joinIntoString (" | ").toRawUTF8(), ok ? "short names OK" : "short names FAILED");
            setApplicationReturnValue (ok ? 0 : 1);
            quit();
            return;
        }

        // MIDI Learn check: NachhallUiShot --midi
        if (args.size() > 0 && args[0] == "--midi")
        {
            int failures = 0;
            auto expect = [&failures] (bool ok, const char* what)
            {
                std::printf ("  [%s] %s\n", ok ? " OK " : "FAIL", what);
                failures += ok ? 0 : 1;
            };
            juce::AudioBuffer<float> buf (2, 256);
            auto sendCC = [&buf] (NachhallAudioProcessor& p, int cc, int value)
            {
                juce::MidiBuffer mb;
                mb.addEvent (juce::MidiMessage::controllerEvent (1, cc, value), 0);
                buf.clear();
                p.processBlock (buf, mb);
            };
            {
                NachhallAudioProcessor p;
                p.setPlayConfigDetails (2, 2, 48000.0, 256);
                p.prepareToPlay (48000.0, 256);
                auto* mix = p.apvts.getParameter ("mix");
                const int mixIndex = p.getParameterIndex ("mix");
                const bool wasEnabled = p.midiEnabled.load();
                p.midiEnabled = true;

                p.midiLearnParam = mixIndex; // MIDI Learn mode: click the Mix knob ...
                sendCC (p, 21, 10);          // ... and move a controller
                expect (p.getControllerForParameter (mixIndex) == 21 && p.midiLearnParam.load() == -1, "learn: CC 21 is connected to Mix");
                sendCC (p, 21, 127);
                expect (std::abs (mix->getValue() - 1.0f) < 1.0e-4f, "CC 21 = 127 -> Mix 100 %");
                sendCC (p, 21, 64);
                expect (std::abs (mix->getValue() - 64.0f / 127.0f) < 1.0e-3f, "CC 21 = 64 -> Mix 50 %");
                sendCC (p, 21, 0);
                expect (mix->getValue() < 1.0e-4f, "CC 21 = 0 -> Mix 0 %");
                sendCC (p, 22, 127);
                expect (mix->getValue() < 1.0e-4f, "a controller without mapping changes nothing");
                p.midiEnabled = false;
                sendCC (p, 21, 127);
                expect (mix->getValue() < 1.0e-4f, "Enable MIDI off: controllers are ignored");
                p.midiEnabled = true;
                p.midiLearnParam = mixIndex;
                sendCC (p, 30, 5);
                expect (p.getControllerForParameter (mixIndex) == 30 && p.getParameterForController (21) < 0, "learning again moves Mix from CC 21 to CC 30");
                p.clearMidiMapping (30);
                sendCC (p, 30, 127);
                expect (mix->getValue() < 1.0e-4f && p.getControllerForParameter (mixIndex) < 0, "Clear removes the mapping");
                p.midiEnabled = wasEnabled;
                p.revertMidiMapping(); // back to the saved mapping (saved again unchanged on close)
            }
            {
                // Pro-R: the mapping is saved automatically when the plug-in closes
                const juce::String id ("predelayOffset");
                {
                    NachhallAudioProcessor p;
                    p.setPlayConfigDetails (2, 2, 48000.0, 256);
                    p.prepareToPlay (48000.0, 256);
                    p.midiLearnParam = p.getParameterIndex (id);
                    sendCC (p, 77, 1);
                }
                {
                    NachhallAudioProcessor p;
                    expect (p.getParameterForController (77) == p.getParameterIndex (id), "mapping saved on close and back in a new instance");
                    p.clearMidiMapping (77); // leave the saved mapping as it was
                }
            }
            std::printf ("%s\n", failures == 0 ? "MIDI LEARN OK" : "MIDI LEARN FAILED");
            setApplicationReturnValue (failures == 0 ? 0 : 1);
            quit();
            return;
        }

        // IR import check: NachhallUiShot --ir <file.wav>
        if (args.size() > 1 && args[0] == "--ir")
        {
            NachhallAudioProcessor p;
            juce::String msg;
            const bool ok = p.importImpulseResponse (juce::File::getCurrentWorkingDirectory().getChildFile (args[1].unquoted()), msg);
            std::printf ("import %s: %s\n", ok ? "ok" : "failed", msg.toRawUTF8());
            for (const char* id : { "style", "space", "decayRate", "predelay", "distance", "width" })
                std::printf ("  %-10s = %s\n", id, p.apvts.getParameter (id)->getCurrentValueAsText().toRawUTF8());
            for (int b = 1; b <= 6; ++b)
            {
                const juce::String d = "d" + juce::String (b);
                if (p.apvts.getRawParameterValue (d + "Used")->load() > 0.5f)
                    std::printf ("  decay band %d: %s %s %s\n", b, p.apvts.getParameter (d + "Shape")->getCurrentValueAsText().toRawUTF8(),
                                 p.apvts.getParameter (d + "Freq")->getCurrentValueAsText().toRawUTF8(),
                                 p.apvts.getParameter (d + "Rate")->getCurrentValueAsText().toRawUTF8());
                const juce::String q = "p" + juce::String (b);
                if (p.apvts.getRawParameterValue (q + "Used")->load() > 0.5f)
                    std::printf ("  post band %d: %s %s %s\n", b, p.apvts.getParameter (q + "Shape")->getCurrentValueAsText().toRawUTF8(),
                                 p.apvts.getParameter (q + "Freq")->getCurrentValueAsText().toRawUTF8(),
                                 p.apvts.getParameter (q + "Gain")->getCurrentValueAsText().toRawUTF8());
            }
            setApplicationReturnValue (ok ? 0 : 1);
            quit();
            return;
        }
        outFile = juce::File::getCurrentWorkingDirectory().getChildFile (args.size() > 0 ? args[0] : "nachhall_ui.png");
        scenario = args.size() > 1 ? args[1].getIntValue() : 0;
        const int width = args.size() > 2 ? args[2].getIntValue() : 1000;

        proc = std::make_unique<NachhallAudioProcessor>();
        proc->setPlayConfigDetails (2, 2, 48000.0, 480);
        proc->prepareToPlay (48000.0, 480);
        proc->editorWidth = width;

        auto set = [this] (const juce::String& id, float v)
        {
            if (auto* p = proc->apvts.getParameter (id))
                p->setValueNotifyingHost (p->convertTo0to1 (v));
        };

        if (scenario == 1)
        {
            proc->loadFactoryPreset (11); // Vocal Hall
            set ("d1Used", 1);
            set ("d1Shape", 0);
            set ("d1Freq", 3000);
            set ("d1Rate", 50);
            set ("d1Q", 1.2f);
            set ("d2Used", 1);
            set ("d2Shape", 1);
            set ("d2Freq", 180);
            set ("d2Rate", 160);
            set ("d2Q", 0.7f);
            set ("d3Used", 1);
            set ("d3Shape", 3);
            set ("d3Freq", 800);
            set ("d3Q", 3);
            set ("p3Used", 1);
            set ("p3Shape", 0);
            set ("p3Freq", 1200);
            set ("p3Gain", 4.5f);
            set ("p3Q", 1.4f);
            proc->showPiano = true;
        }
        else if (scenario == 2)
        {
            proc->loadFactoryPreset (20); // Classic Plate
            proc->analyzerMode = 1;
            set ("predelaySync", 2);
            set ("gate", 1);
            set ("freeze", 0);
        }

        editor.reset (proc->createEditor());
        if (scenario == 1)
        {
            std::function<juce::Component* (juce::Component&)> findEq = [&findEq] (juce::Component& c) -> juce::Component*
            {
                if (c.getComponentID() == "eqDisplay")
                    return &c;
                for (auto* ch : c.getChildren())
                    if (auto* r = findEq (*ch))
                        return r;
                return nullptr;
            };
            if (auto* eq = dynamic_cast<EqDisplay*> (findEq (*editor)))
                eq->selectBand (1, 2); // Post EQ band 3
        }
        if (scenario == 4)
        {
            proc->midiLearnParam = proc->getParameterIndex ("mix");
            // pretend CC 21 was learned for Space and CC 74 for Brightness
            juce::MidiBuffer mb;
            juce::AudioBuffer<float> silent (2, 32);
            silent.clear();
            proc->midiLearnParam = proc->getParameterIndex ("space");
            mb.addEvent (juce::MidiMessage::controllerEvent (1, 21, 64), 0);
            proc->processBlock (silent, mb);
            mb.clear();
            proc->midiLearnParam = proc->getParameterIndex ("brightness");
            mb.addEvent (juce::MidiMessage::controllerEvent (1, 74, 80), 0);
            proc->processBlock (silent, mb);
            proc->midiLearnParam = proc->getParameterIndex ("mix");
        }
        if (scenario == 3 || scenario == 4)
        {
            std::function<juce::Component* (juce::Component&)> find = [&find, this] (juce::Component& c) -> juce::Component*
            {
                if (c.getComponentID() == (scenario == 4 ? "midiLearn" : "presetBrowser"))
                    return &c;
                for (auto* ch : c.getChildren())
                    if (auto* r = find (*ch))
                        return r;
                return nullptr;
            };
            if (auto* b = find (*editor))
                b->setVisible (true);
        }
        window = std::make_unique<juce::DocumentWindow> ("Nachhall", juce::Colours::black, 0);
        window->setUsingNativeTitleBar (false);
        window->setTitleBarHeight (0);
        window->setContentNonOwned (editor.get(), true);
        window->setTopLeftPosition (0, 0);
        window->setVisible (true);

        startTimer (20);
    }

    void shutdown() override
    {
        stopTimer();
        if (window != nullptr)
            window->clearContentComponent();
        editor.reset();
        window.reset();
        proc.reset();
    }

private:
    void timerCallback() override
    {
        // feed ~ real time audio: drum-like noise bursts every 500 ms
        juce::AudioBuffer<float> buf (2, 960);
        juce::MidiBuffer midi;
        for (int i = 0; i < 960; ++i)
        {
            const int t = sampleCounter + i;
            const float env = std::exp (-(float) (t % 24000) / 2500.0f);
            const float x = (rng.nextFloat() * 2.0f - 1.0f) * 0.5f * env;
            buf.setSample (0, i, x);
            buf.setSample (1, i, x);
        }
        sampleCounter += 960;
        proc->processBlock (buf, midi);

        if (++ticks == 150)
        {
            auto img = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
            outFile.deleteFile();
            juce::FileOutputStream os (outFile);
            juce::PNGImageFormat png;
            png.writeImageToStream (img, os);
            os.flush();
            std::printf ("wrote %s (%d x %d)\n", outFile.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight());
            quit();
        }
    }

    std::unique_ptr<NachhallAudioProcessor> proc;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    std::unique_ptr<juce::DocumentWindow> window;
    juce::File outFile;
    juce::Random rng { 42 };
    int scenario = 0, ticks = 0, sampleCounter = 0;
};

START_JUCE_APPLICATION (UiShotApp)
