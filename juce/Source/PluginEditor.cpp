#include "PluginEditor.h"

SwarmAudioProcessorEditor::SwarmAudioProcessorEditor (SwarmAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p), generic (p)
{
    addAndMakeVisible (generic);
    addAndMakeVisible (importButton);
    importButton.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Original SwarmSynth patch",
                                                       juce::File(), "*.fxp;*.fxb;*.chunk;*");
        chooser->launchAsync (juce::FileBrowserComponent::openMode
                            | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc)
        {
            const auto f = fc.getResult();
            if (f.existsAsFile() && ! proc.loadOriginalPreset (f))
                juce::NativeMessageBox::showMessageBoxAsync (
                    juce::MessageBoxIconType::WarningIcon, "SwarmSynth",
                    "Not a SwarmSynth patch chunk.");
        });
    };
    setSize (620, 720);
}

void SwarmAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
}

void SwarmAudioProcessorEditor::resized()
{
    auto r = getLocalBounds();
    importButton.setBounds (r.removeFromTop (30).reduced (4));
    generic.setBounds (r);
}
