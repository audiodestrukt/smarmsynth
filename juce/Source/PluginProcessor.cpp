#include "PluginProcessor.h"
#include "SwarmPreset.h"
#include "SwarmDefaults.h"
#include "SwarmAnarchy.h"
#include <cstdio>
#include <cstdlib>

using namespace swarm;

// ---------------------------------------------------------------------------
// parameters -- one per original parameter, same order, same names, and the
// same quantiser the original applies to incoming values.
// ---------------------------------------------------------------------------

namespace {

juce::String formatValue (const ParamSpec& s, float v)
{
    switch (s.disp)
    {
        case Disp::Db:
        {
            if (v <= 0.0f) return "-oo";
            auto t = juce::String (20.0f * std::log10 (v), 2);
            if (t.contains (".")) t = t.trimCharactersAtEnd ("0").trimCharactersAtEnd (".");
            return t;
        }
        case Disp::Pan:   return juce::String ((int) std::trunc (v * 200.0f - 100.0f));
        case Disp::Count: return juce::String (1 + (int) (v * 63.0f));
        case Disp::Ms1k:  return juce::String (1 + (int) (v * 999.0f));
        case Disp::Semi:  return juce::String ((int) (v * 24.0f));
        case Disp::Pct50: return juce::String ((int) (v * 50.0f));
        case Disp::Time:  return juce::String (envTimeMs (v), 2);
        case Disp::Seed:  return juce::String ((juce::int64) (v * 134217728.0));
        case Disp::Pct:
        default:          return juce::String ((int) (v * 100.0f));
    }
}

} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout SwarmAudioProcessor::makeLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    for (const auto& s : kParams)
    {
        auto attrs = juce::AudioParameterFloatAttributes()
                        .withLabel (s.unit)
                        .withStringFromValueFunction ([&s] (float v, int) { return formatValue (s, v); });
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { s.id, 1 }, s.name,
            juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f, attrs));
    }
    return layout;
}

SwarmAudioProcessor::SwarmAudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "state", makeLayout())
{
    for (int i = 0; i < kNumParams; ++i)
        cached[i] = apvts.getRawParameterValue (kParams[i].id);

    // the original's power-on defaults (see SwarmDefaults.h)
    for (int i = 0; i < kNumParams; ++i)
        if (auto* pr = apvts.getParameter (kParams[i].id))
        {
            pr->setValueNotifyingHost (defaultValue (i));
            if (auto* fp = dynamic_cast<juce::AudioParameterFloat*> (pr))
                juce::ignoreUnused (fp);
        }
}

void SwarmAudioProcessor::prepareToPlay (double sampleRate, int block)
{
    sr = sampleRate;
    engine.prepare (sampleRate, block);
    if (std::getenv ("SWARM_DEBUG") != nullptr)
    {
        std::printf ("[audio] %.0f Hz, %d frames\n", sampleRate, block);
        std::fflush (stdout);
    }
}

void SwarmAudioProcessor::pullSettings()
{
    SwarmSettings s;

    s.numParticles = 1 + (int) (raw (19) * 63.0f);
    s.home[DVol]   = raw (0);
    s.home[DPan]   = raw (1) * 2.0f - 1.0f;
    s.home[DRes]   = raw (2);
    s.home[DNoise] = raw (3);
    s.home[DPitch] = 0.0f;

    s.range[DVol]   = raw (4);
    s.range[DPitch] = raw (5);
    s.range[DPan]   = raw (6);
    s.range[DRes]   = raw (7);
    s.range[DNoise] = raw (8);

    s.speed      = raw (9);
    s.speedLfo   = raw (10);
    s.stdDev     = raw (11);
    s.reflection = raw (12);
    s.attract    = raw (13);
    s.repel      = raw (14);
    s.proximity  = raw (15);

    s.lowpass    = raw (16);
    s.q          = raw (17);
    s.overdrive  = raw (18);
    s.portamento = (1.0f + raw (20) * 999.0f) * 0.001f;
    s.seed       = (uint32_t) (raw (21) * 134217728.0f);

    // Pitch/Pan/Res/Noise envelope ranges (params 27, 35, 43, 51)
    s.envRange[DPitch] = raw (27) * 24.0f;
    s.envRange[DPan]   = raw (35);
    s.envRange[DRes]   = raw (43) * 0.5f;
    s.envRange[DNoise] = raw (51) * 0.5f;

    // envelope blocks. The volume envelope has no initial or release level;
    // the other ten do. Base indices follow the original's parameter order.
    auto ms = [] (float v) { return envTimeMs (v) * 0.001f; };

    s.env[EVol] = makeShape (0.0f, ms (raw (22)), raw (23),
                             ms (raw (24)), raw (25), ms (raw (26)), 0.0f);

    const int base[10] = { 28, 36, 44, 52, 59, 66, 73, 80, 87, 94 };
    const int slot[10] = { EPitch, EPan, ERes, ENoise,
                           EVolVar, EPitchVar, EPanVar, EResVar, ENoiseVar, ESpeed };
    for (int b = 0; b < 10; ++b)
    {
        const int i = base[b];
        s.env[slot[b]] = makeShape (raw (i),           // Ini Lv
                                    ms (raw (i + 1)),  // Atk Tm
                                    raw (i + 2),       // Atk Lv
                                    ms (raw (i + 3)),  // Dec Tm
                                    raw (i + 4),       // Dec Lv
                                    ms (raw (i + 5)),  // Rel Tm
                                    raw (i + 6));      // Rel Lv
    }

    // extra breakpoints, if the editor has added any, override the two-point
    // shape the parameters describe (release always comes from the parameters)
    for (int e = 0; e < NumEnvs; ++e)
        deserialisePoints (envPointsFor (e).toStdString(), s.env[e]);

    engine.setSettings (s);
}

void SwarmAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    pullSettings();

    // notes played on the editor's keyboard arrive the same way as host MIDI
    keyboardState.processNextMidiBuffer (midi, 0, buffer.getNumSamples(), true);

    int pos = 0;
    const int n = buffer.getNumSamples();
    auto* L = buffer.getWritePointer (0);
    auto* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : L;

    for (const auto meta : midi)
    {
        const int at = juce::jlimit (0, n, meta.samplePosition);
        if (at > pos) { engine.process (L + pos, R + pos, at - pos); pos = at; }

        const auto m = meta.getMessage();
        if (m.isNoteOn())
        {
            heldNote = m.getNoteNumber();
            engine.noteOn (heldNote, m.getFloatVelocity());
        }
        else if (m.isNoteOff() && m.getNoteNumber() == heldNote)
        {
            heldNote = -1;
            engine.noteOff();
        }
        else if (m.isAllNotesOff() || m.isAllSoundOff())
        {
            heldNote = -1;
            engine.noteOff();
        }
    }
    if (pos < n) engine.process (L + pos, R + pos, n - pos);
}

juce::String SwarmAudioProcessor::envPointsFor (int slot) const
{
    return apvts.state.getProperty (envPointsId (slot), juce::String()).toString();
}

void SwarmAudioProcessor::setEnvPoints (int slot, const juce::String& text)
{
    apvts.state.setProperty (envPointsId (slot), text, nullptr);
    if (std::getenv ("SWARM_DEBUG") != nullptr)
        std::printf ("[env %d] %s\n", slot, text.toRawUTF8());
}

swarm::EnvShape SwarmAudioProcessor::envShapeFor (int slot) const
{
    // the parameter-described shape, then any stored breakpoints on top
    static const int base[NumEnvs] = { 22, 28, 36, 44, 52, 59, 66, 73, 80, 87, 94 };
    auto ms = [] (float v) { return envTimeMs (v) * 0.001f; };
    const int i = base[slot];
    swarm::EnvShape sh = slot == EVol
        ? makeShape (0.0f, ms (raw (22)), raw (23), ms (raw (24)), raw (25), ms (raw (26)), 0.0f)
        : makeShape (raw (i), ms (raw (i + 1)), raw (i + 2), ms (raw (i + 3)),
                     raw (i + 4), ms (raw (i + 5)), raw (i + 6));
    deserialisePoints (envPointsFor (slot).toStdString(), sh);
    return sh;
}

void SwarmAudioProcessor::anarchy()
{
    float values[kNumParams];
    const auto seed = (uint32_t) juce::Random::getSystemRandom().nextInt();
    swarm::anarchy (values, seed);

    for (int i = 0; i < kNumParams; ++i)
        if (auto* p = apvts.getParameter (kParams[i].id))
            p->setValueNotifyingHost (values[i]);

    // a randomised patch starts from the plain envelope shapes
    for (int e = 0; e < NumEnvs; ++e) setEnvPoints (e, {});

    // the original names these "random patch #%X"
    apvts.state.setProperty ("patchName",
                             "random patch #" + juce::String::toHexString ((int) (seed & 0xffff)).toUpperCase(),
                             nullptr);
}

bool SwarmAudioProcessor::loadOriginalPreset (const juce::File& f)
{
    juce::MemoryBlock mb;
    if (! f.loadFileAsData (mb)) return false;

    std::vector<uint8_t> bytes ((const uint8_t*) mb.getData(),
                                (const uint8_t*) mb.getData() + mb.getSize());
    const auto preset = parseFile (bytes);
    if (! preset.ok) return false;

    for (int i = 0; i < kNumParams; ++i)
        if (auto* p = apvts.getParameter (kParams[i].id))
            p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, preset.value[i]));

    // an imported patch replaces any breakpoints the editor had added
    for (int e = 0; e < NumEnvs; ++e) setEnvPoints (e, {});

    apvts.state.setProperty ("patchName",
                             preset.name.empty() ? f.getFileNameWithoutExtension().replaceCharacter ('_', ' ')
                                                 : juce::String (preset.name),
                             nullptr);
    return true;
}

void SwarmAudioProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, dest);
}

void SwarmAudioProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SwarmAudioProcessor();
}
