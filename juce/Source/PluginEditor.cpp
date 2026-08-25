#include "PluginEditor.h"

using namespace swarmui;

namespace {

// Rectangles measured off analysis/data/ref_native.png, in the original's
// 680 x 536 coordinate space.
struct R { int x, y, w, h; juce::Rectangle<int> r() const { return { x, y, w, h }; } };

constexpr R kMenu       { 8,   6, 208,  22 };
constexpr R kLogo       { 8,  34, 260,  62 };
constexpr R kColumns    { 8, 104, 260, 137 };
constexpr R kEnvBlock   { 8, 245, 260, 145 };
constexpr R kViewPanel  {278,  22, 378, 366 };
constexpr R kSpeedKnob  { 8, 393,  48,  68 };
constexpr R kSpeedEnv   { 60, 393, 260,  68 };
constexpr R kMotionA    {325, 393, 140,  68 };   // LFO, Std Dev, Reflection
constexpr R kMotionB    {470, 393, 152,  68 };   // Attract, Repel, Proximity
constexpr R kDrive      {626, 393,  46,  68 };   // Overdrive
constexpr R kKeyboard   { 8, 466, 562,  62 };
constexpr R kFilter     {576, 462,  96,  66 };   // LoPass, Q

const char* kDimNames[5] = { "VOL", "PITCH", "PAN", "RES", "NOISE" };
const char* kHomeCaps[5] = { "master", "coarse", "home", "home", "home" };

// parameter ids, in the generated order
const char* kHomeIds[5]  = { "volume", "pan", "pan", "resonance", "noise" };

juce::String idFor (int index) { return swarm::kParams[index].id; }

// value envelope parameter bases per dimension: vol, pitch, pan, res, noise
constexpr int kValueEnvBase[5] = { 22, 28, 36, 44, 52 };
constexpr int kRangeEnvBase[5] = { 59, 66, 73, 80, 87 };

} // namespace

SwarmAudioProcessorEditor::SwarmAudioProcessorEditor (SwarmAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    // the five columns: a home/master knob and a range knob each
    // Vol/Pan/Res/Noise have a real "home" parameter. Pitch does not: the
    // original's coarse and fine tune live in its state chunk (offsets 0x1c and
    // 0x20) but are not exposed as VST parameters, so that slot is inert here.
    const int homeIdx[5]  = { 0, -1, 1, 2, 3 };
    const int rangeIdx[5] = { 4, 5, 6, 7, 8 };
    for (int d = 0; d < 5; ++d)
    {
        auto accent = colours::dim (d);
        homeKnobs[d]  = std::make_unique<Knob> (proc.apvts,
                                                homeIdx[d] >= 0 ? idFor (homeIdx[d]) : juce::String(),
                                                kHomeCaps[d], accent);
        if (homeIdx[d] < 0) homeKnobs[d]->placeholder = "0 s/t";
        else if (d == 0)    homeKnobs[d]->suffix = "dB";
        else if (d == 4)    homeKnobs[d]->suffix = "%";

        rangeKnobs[d] = std::make_unique<Knob> (proc.apvts, idFor (rangeIdx[d]),
                                                "range", accent);
        rangeKnobs[d]->suffix = "%";
        addAndMakeVisible (*homeKnobs[d]);
        addAndMakeVisible (*rangeKnobs[d]);

        columnHeaders[d].setText (kDimNames[d], juce::dontSendNotification);
        columnHeaders[d].setJustificationType (juce::Justification::centred);
        columnHeaders[d].setColour (juce::Label::textColourId, accent);
        columnHeaders[d].setInterceptsMouseClicks (true, false);
        columnHeaders[d].addMouseListener (this, false);
        addAndMakeVisible (columnHeaders[d]);
    }

    addAndMakeVisible (wordmark);

    view = std::make_unique<SwarmView> (proc.getEngine());
    addAndMakeVisible (*view);

    speedEnv = std::make_unique<EnvelopeEditor> (proc.apvts,
        EnvelopeEditor::Ids { idFor (95), idFor (96), idFor (97), idFor (98),
                              idFor (99), idFor (100), idFor (94) },
        "Speed Env", colours::text, true);
    addAndMakeVisible (*speedEnv);

    auto mk = [this] (std::unique_ptr<Knob>& k, int idx, const char* cap, juce::Colour c)
    {
        k = std::make_unique<Knob> (proc.apvts, idFor (idx), cap, c);
        addAndMakeVisible (*k);
    };
    mk (kSpeed,     9, "Speed",     colours::text);
    mk (kLfo,      10, "LFO",       colours::text);
    mk (kStdDev,   11, "Std Dev",   colours::text);
    mk (kReflect,  12, "Reflect",   colours::text);
    mk (kAttract,  13, "Attract",   colours::text);
    mk (kRepel,    14, "Repel",     colours::text);
    mk (kProximity,15, "Proximity", colours::text);
    mk (kOverdrive,18, "Overdrive", colours::text);
    mk (kLowpass,  16, "LoPass",    colours::text);
    mk (kQ,        17, "Q",         colours::text);

    for (auto* b : { &presetsButton, &optionsButton, &helpButton })
        addAndMakeVisible (*b);

    presetsButton.onClick = [this]
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

    keyboard = std::make_unique<juce::MidiKeyboardComponent> (
                   proc.getKeyboardState(), juce::MidiKeyboardComponent::horizontalKeyboard);
    keyboard->setAvailableRange (36, 96);
    keyboard->setKeyWidth (15.2f);
    addAndMakeVisible (*keyboard);

    setResizable (true, true);
    setResizeLimits (kBaseW, kBaseH, kBaseW * 3, kBaseH * 3);
    if (auto* c = getConstrainer())      // only exists once limits are set
        c->setFixedAspectRatio ((double) kBaseW / (double) kBaseH);
    setSize (kBaseW * 2, kBaseH * 2);
    selectDimension (0);
    startTimerHz (20);
}

SwarmAudioProcessorEditor::~SwarmAudioProcessorEditor() = default;

void SwarmAudioProcessorEditor::selectDimension (int d)
{
    selectedDim = d;
    const auto accent = colours::dim (d);

    valueEnv = std::make_unique<EnvelopeEditor> (proc.apvts,
        EnvelopeEditor::Ids { idFor (kValueEnvBase[d] + (d == 0 ? 0 : 1)),
                              idFor (kValueEnvBase[d] + (d == 0 ? 1 : 2)),
                              idFor (kValueEnvBase[d] + (d == 0 ? 2 : 3)),
                              idFor (kValueEnvBase[d] + (d == 0 ? 3 : 4)),
                              idFor (kValueEnvBase[d] + (d == 0 ? 4 : 5)),
                              idFor (kValueEnvBase[d] + (d == 0 ? 4 : 6)),
                              idFor (kValueEnvBase[d]) },
        juce::String (kDimNames[d]).toLowerCase().substring (0, 1).toUpperCase()
            + juce::String (kDimNames[d]).toLowerCase().substring (1) + " Env",
        accent, d != 0);

    rangeEnv = std::make_unique<EnvelopeEditor> (proc.apvts,
        EnvelopeEditor::Ids { idFor (kRangeEnvBase[d] + 1), idFor (kRangeEnvBase[d] + 2),
                              idFor (kRangeEnvBase[d] + 3), idFor (kRangeEnvBase[d] + 4),
                              idFor (kRangeEnvBase[d] + 5), idFor (kRangeEnvBase[d] + 6),
                              idFor (kRangeEnvBase[d]) },
        juce::String (kDimNames[d]).toLowerCase().substring (0, 1).toUpperCase()
            + juce::String (kDimNames[d]).toLowerCase().substring (1) + " Range Env",
        accent, true);

    addAndMakeVisible (*valueEnv);
    addAndMakeVisible (*rangeEnv);

    for (int i = 0; i < 5; ++i)
    {
        juce::Font f { juce::FontOptions (11.0f) };
        f.setBold (true);
        f.setUnderline (i == d);
        columnHeaders[i].setFont (f);
    }
    layoutInBaseUnits();
    repaint();
}

void SwarmAudioProcessorEditor::mouseDown (const juce::MouseEvent& e)
{
    for (int d = 0; d < 5; ++d)
        if (e.eventComponent == &columnHeaders[d]) { selectDimension (d); return; }
}

void SwarmAudioProcessorEditor::timerCallback()
{
    for (auto* c : { (juce::Component*) homeKnobs[0].get() }) juce::ignoreUnused (c);
    for (int d = 0; d < 5; ++d) { homeKnobs[d]->repaint(); rangeKnobs[d]->repaint(); }
}

void SwarmAudioProcessorEditor::paint (juce::Graphics& g)
{
    const float s = (float) getWidth() / (float) kBaseW;
    g.setGradientFill (juce::ColourGradient (colours::bgTop, 0.0f, 0.0f,
                                             colours::bgBottom, 0.0f, (float) getHeight(), false));
    g.fillAll();

    g.saveState();
    g.addTransform (juce::AffineTransform::scale (s));

    auto panelAt = [&g] (R box) { drawPanel (g, box.r().toFloat()); };
    panelAt (kColumns);
    panelAt (kEnvBlock);
    panelAt (kSpeedKnob); panelAt (kSpeedEnv);
    panelAt (kMotionA);   panelAt (kMotionB); panelAt (kDrive);
    panelAt (kFilter);

    // the 3D view sits in a lit recess
    auto vp = kViewPanel.r().toFloat();
    g.setColour (colours::glow.withAlpha (0.35f));
    g.fillRoundedRectangle (vp.expanded (3.0f), 3.0f);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.drawRoundedRectangle (vp.expanded (3.0f), 3.0f, 1.0f);

    // the two colour keys above the box
    auto key = [&g] (juce::Rectangle<float> r, bool rainbow)
    {
        for (float x = 0; x < r.getWidth(); ++x)
        {
            const float t = x / juce::jmax (1.0f, r.getWidth() - 1.0f);
            g.setColour (rainbow ? juce::Colour::fromHSV (0.83f * t, 1.0f, 1.0f, 1.0f)
                                 : juce::Colour::greyLevel (t));
            g.drawLine (r.getX() + x, r.getY(), r.getX() + x, r.getBottom(), 1.0f);
        }
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.drawRect (r, 1.0f);
    };
    g.setColour (colours::text);
    g.setFont (juce::FontOptions (9.0f));
    g.drawText ("Res",   juce::Rectangle<int> (300, 6, 26, 12), juce::Justification::centredRight, false);
    key ({ 330.0f, 7.0f, 36.0f, 10.0f }, true);
    g.drawText ("Noise", juce::Rectangle<int> (386, 6, 34, 12), juce::Justification::centredRight, false);
    key ({ 424.0f, 7.0f, 30.0f, 10.0f }, false);

    g.restoreState();
}

void SwarmAudioProcessorEditor::layoutInBaseUnits()
{
    const float s = (float) getWidth() / (float) kBaseW;
    auto place = [s] (juce::Component& c, juce::Rectangle<int> base)
    {
        c.setBounds ((base.toFloat() * s).toNearestInt());
    };

    place (wordmark, kLogo.r());

    // menu row
    auto menu = kMenu.r();
    const int bw = menu.getWidth() / 3;
    place (presetsButton, menu.removeFromLeft (bw).reduced (1));
    place (optionsButton, menu.removeFromLeft (bw).reduced (1));
    place (helpButton,    menu.reduced (1));

    // five columns: header, home knob, range knob
    auto cols = kColumns.r().reduced (3, 3);
    auto headerRow = cols.removeFromTop (13);
    const int colW = cols.getWidth() / 5;
    for (int d = 0; d < 5; ++d)
    {
        place (columnHeaders[d], headerRow.removeFromLeft (colW));
        auto col = cols.withX (cols.getX() + d * colW).withWidth (colW).reduced (2, 0);
        place (*homeKnobs[d],  col.removeFromTop (col.getHeight() / 2).reduced (1));
        place (*rangeKnobs[d], col.reduced (1));
    }

    auto envs = kEnvBlock.r().reduced (4, 4);
    if (valueEnv) place (*valueEnv, envs.removeFromTop (envs.getHeight() / 2).reduced (1));
    if (rangeEnv) place (*rangeEnv, envs.reduced (1));

    if (view) place (*view, kViewPanel.r());
    if (speedEnv) place (*speedEnv, kSpeedEnv.r().reduced (3));

    place (*kSpeed, kSpeedKnob.r().reduced (3));

    auto motionA = kMotionA.r().reduced (3);
    const int mw = motionA.getWidth() / 3;
    place (*kLfo,     motionA.removeFromLeft (mw).reduced (1));
    place (*kStdDev,  motionA.removeFromLeft (mw).reduced (1));
    place (*kReflect, motionA.reduced (1));

    auto motionB = kMotionB.r().reduced (3);
    const int nw = motionB.getWidth() / 3;
    place (*kAttract,   motionB.removeFromLeft (nw).reduced (1));
    place (*kRepel,     motionB.removeFromLeft (nw).reduced (1));
    place (*kProximity, motionB.reduced (1));

    place (*kOverdrive, kDrive.r().reduced (3));

    auto filter = kFilter.r().reduced (3);
    place (*kLowpass, filter.removeFromLeft (filter.getWidth() / 2).reduced (1));
    place (*kQ,       filter.reduced (1));

    if (keyboard) place (*keyboard, kKeyboard.r());
}

void SwarmAudioProcessorEditor::resized()
{
    if (keyboard) keyboard->setKeyWidth (15.2f * (float) getWidth() / (float) kBaseW);
    layoutInBaseUnits();
}

juce::AudioProcessorEditor* SwarmAudioProcessor::createEditor()
{
    return new SwarmAudioProcessorEditor (*this);
}
