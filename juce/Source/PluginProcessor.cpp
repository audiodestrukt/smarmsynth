#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "SwarmPreset.h"

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
        case Disp::Db:    return v <= 0.0f ? juce::String ("-inf")
                                           : juce::String (20.0f * std::log10 (v), 2);
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

    // the original's power-on defaults, read straight off its own chunk
    auto set = [this] (int i, float v) { if (auto* p = apvts.getParameter (kParams[i].id)) p->setValueNotifyingHost (v); };
    set (0, 1.00f);  set (1, 0.50f);  set (2, 0.10f);  set (3, 0.10f);
    set (9, 0.50f);  set (11, 0.50f); set (12, 0.50f); set (13, 0.50f);
    set (14, 0.50f); set (15, 0.50f); set (16, 1.00f);
    set (19, 7.0f / 63.0f);            // 8 oscillators
    set (20, 19.0f / 999.0f);          // 20 ms portamento
    for (int i = 22; i < kNumParams; ++i)
    {
        const auto& s = kParams[i];
        if (s.disp == Disp::Time) set (i, envTimeNorm (i % 3 == 0 ? 500.0f : 100.0f));
        else                      set (i, 0.5f);
    }
    set (23, 1.0f);   // Vl Atk Lv 100%
}

void SwarmAudioProcessor::prepareToPlay (double sampleRate, int block)
{
    sr = sampleRate;
    engine.prepare (sampleRate, block);
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

    s.env[EVol] = { 0.0f, ms (raw (22)), raw (23), ms (raw (24)), raw (25), ms (raw (26)), 0.0f };

    const int base[10] = { 28, 36, 44, 52, 59, 66, 73, 80, 87, 94 };
    const int slot[10] = { EPitch, EPan, ERes, ENoise,
                           EVolVar, EPitchVar, EPanVar, EResVar, ENoiseVar, ESpeed };
    for (int b = 0; b < 10; ++b)
    {
        const int i = base[b];
        s.env[slot[b]] = { raw (i),          // Ini Lv
                           ms (raw (i + 1)), // Atk Tm
                           raw (i + 2),      // Atk Lv
                           ms (raw (i + 3)), // Dec Tm
                           raw (i + 4),      // Dec Lv
                           ms (raw (i + 5)), // Rel Tm
                           raw (i + 6) };    // Rel Lv
    }

    engine.setSettings (s);
}

void SwarmAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    pullSettings();

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

juce::AudioProcessorEditor* SwarmAudioProcessor::createEditor()
{
    return new SwarmAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SwarmAudioProcessor();
}
