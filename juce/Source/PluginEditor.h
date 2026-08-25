#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

/** Placeholder editor: the generic parameter panel plus a button to import an
    original SwarmSynth patch. The real 3D swarm view comes later. */
class SwarmAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit SwarmAudioProcessorEditor (SwarmAudioProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    SwarmAudioProcessor& proc;
    juce::GenericAudioProcessorEditor generic;
    juce::TextButton importButton { "Import original patch..." };
    std::unique_ptr<juce::FileChooser> chooser;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SwarmAudioProcessorEditor)
};
