#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "SwarmLookAndFeel.h"
#include "SwarmComponents.h"

/** A recreation of the original SwarmSynth editor. Laid out in the original's
    own 680 x 536 units and scaled as a whole, so the proportions hold at any
    size. Reference capture: analysis/data/ref_native.png */
class SwarmAudioProcessorEditor : public juce::AudioProcessorEditor,
                                  private juce::Timer
{
public:
    explicit SwarmAudioProcessorEditor (SwarmAudioProcessor&);
    ~SwarmAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    void selectDimension (int d);
    void layoutInBaseUnits();

    SwarmAudioProcessor& proc;

    // the five parameter columns
    std::array<std::unique_ptr<swarmui::Knob>, 5> homeKnobs;
    std::array<std::unique_ptr<swarmui::Knob>, 5> rangeKnobs;
    std::array<juce::Label, 5> columnHeaders;
    int selectedDim = 0;

    swarmui::Wordmark wordmark;
    std::unique_ptr<swarmui::SwarmView>      view;
    std::unique_ptr<swarmui::EnvelopeEditor> valueEnv, rangeEnv, speedEnv;

    // motion + global controls along the bottom
    std::unique_ptr<swarmui::Knob> kSpeed, kLfo, kStdDev, kReflect,
                                   kAttract, kRepel, kProximity,
                                   kOverdrive, kLowpass, kQ,
                                   kOscillators, kPortamento;

    // shares the processor's keyboard state, so on-screen notes reach the engine
    std::unique_ptr<juce::MidiKeyboardComponent> keyboard;
    juce::TextButton presetsButton { "Presets" }, optionsButton { "Options" }, helpButton { "Help" };
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SwarmAudioProcessorEditor)
};
