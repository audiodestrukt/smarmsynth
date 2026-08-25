#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "SwarmParams.h"
#include "SwarmEngine.h"

class SwarmAudioProcessor : public juce::AudioProcessor
{
public:
    SwarmAudioProcessor();
    ~SwarmAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "SwarmSynth"; }
    bool acceptsMidi()  const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 10.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    /** Loads an original SwarmSynth patch (raw chunk or .fxp). */
    bool loadOriginalPreset (const juce::File&);

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout makeLayout();
    void pullSettings();

    float raw (int i) const { return cached[i] != nullptr ? cached[i]->load() : 0.0f; }

    std::atomic<float>* cached[swarm::kNumParams] = { nullptr };
    swarm::SwarmEngine  engine;
    int   heldNote = -1;
    double sr = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SwarmAudioProcessor)
};
