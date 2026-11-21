#include "PluginProcessor.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <map>

#include "IRAnalyzer.h"
#include "PluginEditor.h"
#include "Presets.h"

namespace
{
// Global settings (shared by all instances). A short-lived PropertiesFile per access: no static JUCE
// objects that would outlive the JUCE runtime when the host unloads the plug-in.
juce::PropertiesFile::Options settingsOptions()
{
    juce::PropertiesFile::Options o;
    o.applicationName = "Nachhall";
    o.filenameSuffix = ".settings";
    o.folderName = "Nachhall";
    o.osxLibrarySubFolder = "Application Support";
    o.millisecondsBeforeSaving = -1; // saved explicitly
    return o;
}

bool readGlobalBool (const juce::String& key, bool fallback)
{
    juce::PropertiesFile f (settingsOptions());
    return f.getBoolValue (key, fallback);
}

void writeGlobalBool (const juce::String& key, bool value)
{
    juce::PropertiesFile f (settingsOptions());
    f.setValue (key, value);
    f.saveIfNeeded();
}

juce::String readGlobalString (const juce::String& key)
{
    juce::PropertiesFile f (settingsOptions());
    return f.getValue (key);
}

void writeGlobalString (const juce::String& key, const juce::String& value)
{
    juce::PropertiesFile f (settingsOptions());
    f.setValue (key, value);
    f.saveIfNeeded();
}

const juce::String kPresetExtension { ".nhpreset" };
} // namespace

//==============================================================================
NachhallAudioProcessor::NachhallAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, &undoManager, "NachhallState", params::createLayout()),
      cache (apvts)
{
    lockMix = readGlobalBool ("lockMixDefault", false);
    {
        const int w = readGlobalString ("defaultEditorWidth").getIntValue();
        editorWidth = w > 0 ? juce::jlimit (700, 4000, w) : 1000;
    }
    for (auto& m : midiMap)
        m = -1;
    revertMidiMapping();

    // Pro-R: the "Default Setting" preset is loaded automatically when the plug-in starts
    const auto def = getDefaultPresetFile();
    if (def.existsAsFile())
    {
        const bool lock = lockMix.load();
        lockMix = false;
        loadPresetFile (def);
        lockMix = lock;
        setCurrentPresetName ("Default Setting");
    }
    undoManager.clearUndoHistory();
    snapshotPresetState();
    abStates[0] = apvts.copyState();
}

NachhallAudioProcessor::~NachhallAudioProcessor()
{
    // Pro-R: the current MIDI mapping is saved automatically when the plug-in is closed
    if (juce::MessageManager::getInstanceWithoutCreating() != nullptr && juce::MessageManager::getInstance()->isThisTheMessageThread())
        saveMidiMapping();
}

int NachhallAudioProcessor::getParameterIndex (const juce::String& paramID) const
{
    const auto& ps = getParameters();
    for (int i = 0; i < ps.size(); ++i)
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (ps[i]))
            if (rp->getParameterID() == paramID)
                return i;
    return -1;
}

int NachhallAudioProcessor::getControllerForParameter (int paramIndex) const noexcept
{
    for (int cc = 0; cc < kNumControllers; ++cc)
        if (midiMap[(size_t) cc].load() == paramIndex)
            return cc;
    return -1;
}

void NachhallAudioProcessor::clearMidiMapping (int cc)
{
    for (int i = 0; i < kNumControllers; ++i)
        if (cc < 0 || i == cc)
            midiMap[(size_t) i] = -1;
}

void NachhallAudioProcessor::saveMidiMapping()
{
    juce::StringArray entries;
    const auto& ps = getParameters();
    for (int cc = 0; cc < kNumControllers; ++cc)
    {
        const int idx = midiMap[(size_t) cc].load();
        if (juce::isPositiveAndBelow (idx, ps.size()))
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (ps[idx]))
                entries.add (juce::String (cc) + "=" + rp->getParameterID());
    }
    writeGlobalString ("midiMap", entries.joinIntoString (";"));
    writeGlobalString ("midiEnabled", midiEnabled.load() ? "1" : "0");
}

void NachhallAudioProcessor::revertMidiMapping()
{
    clearMidiMapping (-1);
    for (const auto& entry : juce::StringArray::fromTokens (readGlobalString ("midiMap"), ";", ""))
    {
        const int cc = entry.upToFirstOccurrenceOf ("=", false, false).getIntValue();
        const int idx = getParameterIndex (entry.fromFirstOccurrenceOf ("=", false, false));
        if (juce::isPositiveAndBelow (cc, kNumControllers) && idx >= 0)
            midiMap[(size_t) cc] = idx;
    }
    midiEnabled = readGlobalString ("midiEnabled") != "0";
}

//==============================================================================
void NachhallAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    cache.read (engineParams, hostBpm.load());
    engine.setParams (engineParams);
    engine.prepare (sampleRate, std::max (samplesPerBlock, 32));
    monoScratch.assign ((size_t) std::max (samplesPerBlock, 8192), 0.0f);
}

void NachhallAudioProcessor::releaseResources() {}

bool NachhallAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    const auto in = layouts.getMainInputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    if (in != juce::AudioChannelSet::mono() && in != juce::AudioChannelSet::stereo())
        return false;
    return in.size() <= out.size();
}

double NachhallAudioProcessor::getTailLengthSeconds() const
{
    const float space = apvts.getRawParameterValue (ids::space)->load();
    const float rate = apvts.getRawParameterValue (ids::decayRate)->load() * 0.01f;
    return juce::jlimit (0.5, 60.0, (double) nh::interpolateRoom (space).actualT60() * rate * 1.2 + 0.5);
}

void NachhallAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    // MIDI Learn: an armed parameter takes the next controller; mapped controllers move parameters
    if (! midi.isEmpty())
    {
        const auto& ps = getParameters();
        for (const auto meta : midi)
        {
            const auto m = meta.getMessage();
            if (! m.isController())
                continue;
            const int cc = m.getControllerNumber() & 127;
            const int armed = midiLearnParam.load();
            if (armed >= 0)
            {
                for (auto& slot : midiMap)
                    if (slot.load() == armed)
                        slot = -1;
                midiMap[(size_t) cc] = armed;
                midiLearnParam = -1;
                continue;
            }
            const int idx = midiMap[(size_t) cc].load();
            if (midiEnabled.load() && juce::isPositiveAndBelow (idx, ps.size()))
                ps[idx]->setValueNotifyingHost ((float) m.getControllerValue() / 127.0f);
        }
        midi.clear();
    }

    const int numIn = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    const int n = buffer.getNumSamples();
    if (n <= 0 || numOut <= 0)
        return;

    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto bpm = pos->getBpm())
                if (*bpm > 1.0)
                    hostBpm = *bpm;

    cache.read (engineParams, hostBpm.load());
    engine.setParams (engineParams);

    if (numIn == 0)
        buffer.clear();
    else if (numIn == 1 && numOut > 1)
        buffer.copyFrom (1, 0, buffer, 0, 0, n);

    float* L = buffer.getWritePointer (0);
    float peakL = 0.0f, peakR = 0.0f;

    auto pushAnalysis = [this] (int len)
    {
        const int m = std::min (len, engine.getAnalysisLength());
        tapPre.push (engine.getAnalysisPre(), m);
        tapWet.push (engine.getAnalysisWet(), m);
        tapOut.push (engine.getAnalysisOut(), m);
    };

    if (numOut > 1)
    {
        float* R = buffer.getWritePointer (1);
        engine.process (L, R, n);
        pushAnalysis (n);
        peakL = engine.getOutputPeak (0);
        peakR = engine.getOutputPeak (1);
    }
    else
    {
        // mono output: run the stereo engine and fold down
        int pos = 0;
        while (pos < n)
        {
            const int len = std::min (n - pos, (int) monoScratch.size());
            std::copy (L + pos, L + pos + len, monoScratch.begin());
            engine.process (L + pos, monoScratch.data(), len);
            for (int i = 0; i < len; ++i)
                L[pos + i] = 0.5f * (L[pos + i] + monoScratch[(size_t) i]);
            pushAnalysis (len);
            peakL = std::max (peakL, engine.getOutputPeak (0));
            pos += len;
        }
        peakR = peakL;
    }

    for (int ch = 2; ch < numOut; ++ch)
        buffer.clear (ch, 0, n);

    // the editor reads and resets the peaks
    meterPeak[0] = std::max (meterPeak[0].load(), peakL);
    meterPeak[1] = std::max (meterPeak[1].load(), peakR);
}

juce::AudioProcessorParameter* NachhallAudioProcessor::getBypassParameter() const
{
    return apvts.getParameter (ids::bypass);
}

juce::AudioProcessorEditor* NachhallAudioProcessor::createEditor()
{
    return new NachhallAudioProcessorEditor (*this);
}

//==============================================================================
int NachhallAudioProcessor::getNumPrograms() { return (int) factoryPresets().size(); }

void NachhallAudioProcessor::setCurrentProgram (int index)
{
    if (juce::isPositiveAndBelow (index, getNumPrograms()))
        loadFactoryPreset (index);
}

const juce::String NachhallAudioProcessor::getProgramName (int index)
{
    if (juce::isPositiveAndBelow (index, getNumPrograms()))
        return factoryPresets()[(size_t) index].name;
    return {};
}

void NachhallAudioProcessor::applyValues (const std::vector<std::pair<juce::String, float>>& values, bool resetOthers, bool respectMixLock)
{
    std::map<juce::String, float> lookup;
    for (const auto& v : values)
        lookup[v.first] = v.second;

    for (auto* p : getParameters())
    {
        auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p);
        if (rp == nullptr)
            continue;
        const auto id = rp->getParameterID();
        if (id == ids::bypass)
            continue;
        if (respectMixLock && lockMix.load() && id == ids::mix)
            continue;

        float norm;
        if (auto it = lookup.find (id); it != lookup.end())
            norm = rp->convertTo0to1 (it->second);
        else if (resetOthers)
            norm = rp->getDefaultValue();
        else
            continue;

        if (std::abs (rp->getValue() - norm) > 1.0e-7f)
        {
            rp->beginChangeGesture();
            rp->setValueNotifyingHost (norm);
            rp->endChangeGesture();
        }
    }
}

void NachhallAudioProcessor::loadFactoryPreset (int index)
{
    const auto& list = factoryPresets();
    if (! juce::isPositiveAndBelow (index, (int) list.size()))
        return;
    undoManager.beginNewTransaction ("Load preset");
    applyValues (list[(size_t) index].values, true, true);
    currentProgram = index;
    setCurrentPresetName (list[(size_t) index].name);
    snapshotPresetState();
    updateHostDisplay (ChangeDetails().withProgramChanged (true));
}

juce::File NachhallAudioProcessor::getUserPresetFolder()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Nachhall").getChildFile ("Presets");
    if (! dir.exists())
        dir.createDirectory();
    return dir;
}

bool NachhallAudioProcessor::savePresetFile (const juce::File& file, const PresetInfo& info)
{
    auto state = apvts.copyState();
    state.setProperty ("nhVersion", params::kStateVersion, nullptr);
    juce::ValueTree preset ("NachhallPreset");
    preset.setProperty ("version", params::kStateVersion, nullptr);
    preset.setProperty ("author", info.author, nullptr);
    preset.setProperty ("description", info.description, nullptr);
    preset.setProperty ("tags", info.tags.joinIntoString (","), nullptr);
    preset.appendChild (state, nullptr);
    if (auto xml = preset.createXml())
    {
        const auto target = file.hasFileExtension (kPresetExtension) ? file : file.withFileExtension (kPresetExtension);
        if (xml->writeTo (target))
        {
            setCurrentPresetName (target.getFileNameWithoutExtension());
            snapshotPresetState();
            return true;
        }
    }
    return false;
}

juce::File NachhallAudioProcessor::getDefaultPresetFile()
{
    return getUserPresetFolder().getChildFile ("Default Setting" + kPresetExtension);
}

bool NachhallAudioProcessor::saveAsDefaultPreset()
{
    const auto name = getCurrentPresetName();
    const bool ok = savePresetFile (getDefaultPresetFile(), { "", "Loaded when Nachhall starts", {} });
    setCurrentPresetName (ok ? juce::String ("Default Setting") : name);
    return ok;
}

PresetInfo NachhallAudioProcessor::readPresetInfo (const juce::File& file)
{
    PresetInfo info;
    if (auto xml = juce::XmlDocument::parse (file))
    {
        info.author = xml->getStringAttribute ("author");
        info.description = xml->getStringAttribute ("description");
        info.tags = juce::StringArray::fromTokens (xml->getStringAttribute ("tags"), ",", "");
        info.tags.trim();
        info.tags.removeEmptyStrings();
    }
    return info;
}

bool NachhallAudioProcessor::writePresetInfo (const juce::File& file, const PresetInfo& info)
{
    auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr || ! xml->hasTagName ("NachhallPreset"))
        return false;
    xml->setAttribute ("author", info.author);
    xml->setAttribute ("description", info.description);
    xml->setAttribute ("tags", info.tags.joinIntoString (","));
    return xml->writeTo (file);
}

void NachhallAudioProcessor::snapshotPresetState()
{
    presetSnapshot.clear();
    for (auto* p : getParameters())
        presetSnapshot.push_back (p->getValue());
}

bool NachhallAudioProcessor::isPresetModified() const
{
    const auto& ps = getParameters();
    if ((size_t) ps.size() != presetSnapshot.size())
        return false;
    for (int i = 0; i < ps.size(); ++i)
        if (ps[i] != getBypassParameter() && std::abs (ps[i]->getValue() - presetSnapshot[(size_t) i]) > 1.0e-4f)
            return true;
    return false;
}

bool NachhallAudioProcessor::loadPresetFile (const juce::File& file)
{
    auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr)
        return false;
    auto tree = juce::ValueTree::fromXml (*xml);
    if (tree.hasType ("NachhallPreset"))
        tree = tree.getChildWithName (apvts.state.getType());
    if (! tree.isValid() || ! tree.hasType (apvts.state.getType()))
        return false;
    tree = tree.createCopy();
    params::upgradeLegacyState (tree); // presets saved by Nachhall 1.0
    undoManager.beginNewTransaction ("Load preset");
    applyStateTree (tree, true);
    setCurrentPresetName (file.getFileNameWithoutExtension());
    snapshotPresetState();
    return true;
}

void NachhallAudioProcessor::applyStateTree (const juce::ValueTree& tree, bool respectMixLock)
{
    std::vector<std::pair<juce::String, float>> values;
    for (const auto& child : tree)
    {
        if (! child.hasType ("PARAM"))
            continue;
        const auto id = child.getProperty ("id").toString();
        if (id.isNotEmpty() && child.hasProperty ("value"))
            values.push_back ({ id, (float) child.getProperty ("value") });
    }
    applyValues (values, true, respectMixLock);
}

bool NachhallAudioProcessor::importImpulseResponse (const juce::File& file, juce::String& message)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));
    if (reader == nullptr)
    {
        message = "The file could not be read (WAV or AIFF required).";
        return false;
    }
    const auto maxSamples = (juce::int64) (reader->sampleRate * 30.0);
    const int len = (int) std::min (reader->lengthInSamples, maxSamples);
    juce::AudioBuffer<float> buf ((int) std::min (2u, reader->numChannels), len);
    reader->read (&buf, 0, len, 0, true, buf.getNumChannels() > 1);

    const auto result = analyseImpulseResponse (buf, reader->sampleRate);
    message = result.message;
    if (! result.ok)
        return false;

    undoManager.beginNewTransaction ("Import IR");
    applyValues (result.values, false, true);
    setCurrentPresetName ("IR: " + file.getFileNameWithoutExtension());
    snapshotPresetState();
    return true;
}

void NachhallAudioProcessor::setDefaultEditorWidth (int width)
{
    writeGlobalString ("defaultEditorWidth", juce::String (width));
}

juce::StringArray NachhallAudioProcessor::getFavouritePresets()
{
    return juce::StringArray::fromLines (readGlobalString ("favourites"));
}

void NachhallAudioProcessor::setFavouritePresets (const juce::StringArray& keys)
{
    writeGlobalString ("favourites", keys.joinIntoString ("\n"));
}

void NachhallAudioProcessor::setMixLocked (bool shouldLock)
{
    lockMix = shouldLock;
    writeGlobalBool ("lockMixDefault", shouldLock);
}

void NachhallAudioProcessor::selectABSlot (int slot)
{
    slot = juce::jlimit (0, 1, slot);
    if (slot == abSlot.load())
        return;
    juce::ValueTree target;
    {
        const juce::ScopedLock sl (stateLock);
        abStates[abSlot.load()] = apvts.copyState();
        abSlot = slot;
        if (abStates[slot].isValid())
            target = abStates[slot];
        else
            abStates[slot] = apvts.copyState();
    }
    if (target.isValid())
    {
        undoManager.beginNewTransaction ("Switch A/B");
        applyStateTree (target, false);
    }
}

void NachhallAudioProcessor::copyCurrentToOtherSlot()
{
    const juce::ScopedLock sl (stateLock);
    const int current = abSlot.load();
    abStates[current] = apvts.copyState();
    abStates[1 - current] = apvts.copyState();
}

//==============================================================================
void NachhallAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("nhVersion", params::kStateVersion, nullptr);
    state.setProperty ("presetName", getCurrentPresetName(), nullptr);
    state.setProperty ("program", currentProgram, nullptr);
    state.setProperty ("lockMix", lockMix.load(), nullptr);
    state.setProperty ("analyzerMode", analyzerMode, nullptr);
    state.setProperty ("displayRange", displayRange, nullptr);
    state.setProperty ("showPiano", showPiano, nullptr);
    state.setProperty ("showMeter", showMeter, nullptr);
    state.setProperty ("editorWidth", editorWidth, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void NachhallAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;
    auto tree = juce::ValueTree::fromXml (*xml);
    params::upgradeLegacyState (tree); // projects saved with Nachhall 1.0
    apvts.replaceState (tree);

    // AudioParameterBool keeps the raw normalised value a host has sent (e.g. 0.85). If the restored
    // state has the same logical value, APVTS leaves that raw value untouched - snap it to 0 / 1 so the
    // host sees exactly the restored value.
    for (auto* p : getParameters())
        if (auto* b = dynamic_cast<juce::AudioParameterBool*> (p))
        {
            const float snapped = b->get() ? 1.0f : 0.0f;
            if (std::abs (p->getValue() - snapped) > 1.0e-6f)
                p->setValueNotifyingHost (snapped);
        }
    setCurrentPresetName (tree.getProperty ("presetName", "Default").toString());
    currentProgram = (int) tree.getProperty ("program", 0);
    lockMix = (bool) tree.getProperty ("lockMix", lockMix.load());
    analyzerMode = juce::jlimit (0, 2, (int) tree.getProperty ("analyzerMode", 0));
    displayRange = juce::jlimit (0, 2, (int) tree.getProperty ("displayRange", 1));
    showPiano = (bool) tree.getProperty ("showPiano", false);
    showMeter = (bool) tree.getProperty ("showMeter", true);
    editorWidth = juce::jlimit (700, 4000, (int) tree.getProperty ("editorWidth", editorWidth));
    {
        const juce::ScopedLock sl (stateLock);
        abStates[abSlot.load()] = apvts.copyState();
    }
    if (juce::MessageManager::getInstanceWithoutCreating() != nullptr && juce::MessageManager::getInstance()->isThisTheMessageThread())
        snapshotPresetState();
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new NachhallAudioProcessor();
}
