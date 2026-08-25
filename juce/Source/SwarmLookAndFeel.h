#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// Colours and metrics traced from a native-resolution capture of the original
// editor (analysis/data/ref_native.png, 680 x 536).
namespace swarmui {

// The original is a fixed 680 x 536. Everything is laid out in those units and
// the whole editor scales as one, so the proportions survive resizing.
constexpr int kBaseW = 680;
constexpr int kBaseH = 536;

namespace colours {
    const juce::Colour bgTop      { 0xff3d5a72 };
    const juce::Colour bgBottom   { 0xff15222e };
    const juce::Colour panel      { 0xff3a3a3a };
    const juce::Colour panelEdge  { 0xff7d8d9b };
    const juce::Colour inset      { 0xff2b2b2b };
    const juce::Colour glow       { 0xffc6dced };
    const juce::Colour cubeFace   { 0xff8b8b8b };
    const juce::Colour cubeFloor  { 0xff9a9a9a };
    const juce::Colour envPaper   { 0xfff4f4f4 };
    const juce::Colour envInk     { 0xff101010 };
    const juce::Colour text       { 0xffd8e2ea };

    // one accent per swarm dimension, matching the original's column headers
    const juce::Colour vol        { 0xff7f8fe0 };
    const juce::Colour pitch      { 0xff5fc98a };
    const juce::Colour pan        { 0xffd97b7b };
    const juce::Colour res        { 0xffd2d27a };
    const juce::Colour noise      { 0xffb07fd9 };
    inline juce::Colour dim (int d)
    {
        switch (d) { case 0: return vol; case 1: return pitch; case 2: return pan;
                     case 3: return res; default: return noise; }
    }
}

/** The original's knobs are little glossy spheres with a coloured bead marking
    the position, sunk into a bevelled recess. */
inline void drawSwarmKnob (juce::Graphics& g, juce::Rectangle<float> b,
                           float proportion, juce::Colour accent)
{
    const auto c = b.getCentre();
    const float r = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f - 1.0f;

    g.setColour (juce::Colour (0xff202020));
    g.fillEllipse (c.x - r - 1.0f, c.y - r + 1.0f, (r + 1) * 2, (r + 1) * 2);

    juce::ColourGradient body (accent.withMultipliedSaturation (0.35f).brighter (0.85f),
                               c.x - r * 0.45f, c.y - r * 0.55f,
                               accent.withMultipliedSaturation (0.55f).darker (0.25f),
                               c.x + r * 0.6f,  c.y + r * 0.7f, true);
    g.setGradientFill (body);
    g.fillEllipse (c.x - r, c.y - r, r * 2, r * 2);

    g.setColour (juce::Colours::white.withAlpha (0.55f));
    g.fillEllipse (c.x - r * 0.55f, c.y - r * 0.72f, r * 0.62f, r * 0.44f);

    // the bead: swings around most of a turn, like the original
    const float a = juce::degreesToRadians (225.0f) + proportion * juce::degreesToRadians (270.0f);
    const float br = r * 0.62f, bs = juce::jmax (2.0f, r * 0.26f);
    const juce::Point<float> bp { c.x + std::cos (a) * br, c.y + std::sin (a) * br };
    g.setColour (accent.darker (0.55f));
    g.fillEllipse (bp.x - bs, bp.y - bs, bs * 2, bs * 2);

    g.setColour (juce::Colours::black.withAlpha (0.4f));
    g.drawEllipse (c.x - r, c.y - r, r * 2, r * 2, 1.0f);
}

/** Sunken panel with a light edge, as used for every group in the original. */
inline void drawPanel (juce::Graphics& g, juce::Rectangle<float> b, float corner = 2.0f)
{
    g.setColour (colours::panel);
    g.fillRoundedRectangle (b, corner);
    g.setColour (colours::panelEdge.withAlpha (0.75f));
    g.drawRoundedRectangle (b.reduced (0.5f), corner, 1.0f);
}

inline void drawInset (juce::Graphics& g, juce::Rectangle<float> b)
{
    g.setColour (colours::inset);
    g.fillRect (b);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawRect (b, 1.0f);
}

} // namespace swarmui
