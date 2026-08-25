#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "SwarmLookAndFeel.h"
#include "SwarmEngine.h"
#include "SwarmParams.h"

namespace swarmui {

// ---------------------------------------------------------------------------
/** A knob with a caption above and its value printed below, exactly the
    arrangement the original uses in its five parameter columns. */
class Knob : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState& s, const juce::String& paramId,
          const juce::String& caption, juce::Colour accent, bool showCaption = true)
        : apvts (s), id (paramId), text (caption), colour (accent), captioned (showCaption)
    {
        param = id.isNotEmpty() ? apvts.getParameter (id) : nullptr;
        setRepaintsOnMouseActivity (true);
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        const float lineH = captioned ? b.getHeight() * 0.17f : 0.0f;

        if (captioned)
        {
            g.setColour (colour);
            g.setFont (juce::FontOptions (juce::jmax (6.0f, lineH * 0.82f)));
            g.drawText (text, b.removeFromTop (lineH).toNearestInt(),
                        juce::Justification::centred, false);
        }
        auto valueRow = b.removeFromBottom (b.getHeight() * 0.24f);
        drawSwarmKnob (g, b.reduced (1.0f), param ? param->getValue() : 0.0f, colour);

        g.setColour (colours::text.withAlpha (param != nullptr ? 1.0f : 0.55f));
        g.setFont (juce::FontOptions (juce::jmax (6.0f, valueRow.getHeight() * 0.80f)));
        g.drawText (param != nullptr ? param->getCurrentValueAsText() + suffix : placeholder,
                    valueRow.toNearestInt(), juce::Justification::centred, false);
    }

    void mouseDown (const juce::MouseEvent&) override
    {
        if (param) { param->beginChangeGesture(); dragStart = param->getValue(); }
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! param) return;
        const float delta = -(float) e.getDistanceFromDragStartY() / 150.0f;
        param->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, dragStart + delta));
        repaint();
    }
    void mouseUp (const juce::MouseEvent&) override { if (param) param->endChangeGesture(); }
    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        if (param) { param->setValueNotifyingHost (param->getDefaultValue()); repaint(); }
    }

    juce::String suffix;
    juce::String placeholder;

private:
    juce::AudioProcessorValueTreeState& apvts;
    juce::String id, text;
    juce::Colour colour;
    bool captioned;
    juce::RangedAudioParameter* param = nullptr;
    float dragStart = 0.0f;
};

// ---------------------------------------------------------------------------
/** The breakpoint envelope editor. The original draws attack / decay / release
    as three segments between draggable points, with the segment durations
    written underneath as arrows. Times use the measured law
    t_ms = 0.1 + 9999.9*v^2. */
class EnvelopeEditor : public juce::Component
{
public:
    struct Ids { juce::String atkT, atkL, decT, decL, relT, relL, ini; };

    EnvelopeEditor (juce::AudioProcessorValueTreeState& s, Ids ids,
                    juce::String caption, juce::Colour accent, bool hasIni)
        : apvts (s), p (ids), title (caption), colour (accent), withIni (hasIni) {}

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();

        const float u = b.getHeight() / 66.0f;      // the panel is ~66 base units tall
        g.setColour (colour);
        g.setFont (juce::FontOptions (juce::jmax (6.0f, 8.0f * u)));
        auto header = b.removeFromTop (11.0f * u);
        g.drawText (title, header.toNearestInt(), juce::Justification::topLeft, false);
        g.setColour (colours::text.withAlpha (0.75f));
        const float third = header.getWidth() / 3.0f;
        g.drawText ("attack",  header.withX (header.getX() + third * 0.9f).toNearestInt(),
                    juce::Justification::topLeft, false);
        g.drawText ("decay",   header.withX (header.getX() + third * 1.75f).toNearestInt(),
                    juce::Justification::topLeft, false);
        g.drawText ("release", header.withX (header.getRight() - 44.0f * u).toNearestInt(),
                    juce::Justification::topLeft, false);

        auto footer = b.removeFromBottom (11.0f * u);
        auto plot   = b.reduced (2.0f);

        g.setColour (colours::envPaper);
        g.fillRect (plot);
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.drawRect (plot, 1.0f);

        // parameter values are normalised; real durations come from the
        // measured law t_ms = 0.1 + 9999.9*v^2
        const float a = swarm::envTimeMs (val (p.atkT));
        const float d = swarm::envTimeMs (val (p.decT));
        const float r = swarm::envTimeMs (val (p.relT));
        const float total = juce::jmax (0.001f, a + d + r);
        const float x0 = plot.getX();
        const float xa = x0 + plot.getWidth() * (a / total);
        const float xd = xa + plot.getWidth() * (d / total);
        const float x1 = plot.getRight();

        // inset by a pixel so a full-scale level is not drawn on the border
        auto yFor = [&] (float lvl)
        {
            auto in = plot.reduced (0.0f, 1.5f);
            return in.getBottom() - in.getHeight() * juce::jlimit (0.0f, 1.0f, lvl);
        };
        const float y0 = yFor (withIni ? val (p.ini) : 0.0f);
        const float ya = yFor (val (p.atkL));
        const float yd = yFor (val (p.decL));
        const float y1 = yFor (withIni ? val (p.relL) : 0.0f);

        juce::Path path;
        path.startNewSubPath (x0, y0);
        path.lineTo (xa, ya); path.lineTo (xd, yd); path.lineTo (x1, y1);
        g.setColour (colours::envInk);
        g.strokePath (path, juce::PathStrokeType (1.2f));

        g.setColour (juce::Colours::black.withAlpha (0.45f));
        for (float x : { xa, xd })
        {
            const float dash[] = { 2.0f, 2.0f };
            juce::Path v; v.startNewSubPath (x, plot.getY()); v.lineTo (x, plot.getBottom());
            juce::PathStrokeType (0.7f).createDashedStroke (v, v, dash, 2);
            g.strokePath (v, juce::PathStrokeType (0.7f));
        }
        g.setColour (colours::envInk);
        for (auto pt : { juce::Point<float> (x0, y0), { xa, ya }, { xd, yd }, { x1, y1 } })
            g.fillRect (pt.x - 2.0f, pt.y - 2.0f, 4.0f, 4.0f);

        // segment durations, as the original prints them
        g.setColour (colours::text.withAlpha (0.8f));
        g.setFont (juce::FontOptions (juce::jmax (6.0f, 7.5f * u)));
        auto seg = [&] (float lo, float hi, float ms)
        {
            juce::Rectangle<float> box (lo, footer.getY(), hi - lo, footer.getHeight());
            const juce::String s = ms < 1000.0f ? juce::String (juce::roundToInt (ms)) + "ms"
                                                : juce::String (ms * 0.001f, 2) + "s";
            g.drawText ("<-" + s + "->", box.toNearestInt(), juce::Justification::centred, false);
        };
        seg (x0, xa, a); seg (xa, xd, d); seg (xd, x1, r);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        auto b = plotArea();
        const float fx = juce::jlimit (0.0f, 1.0f,
                                       (e.position.x - b.getX()) / juce::jmax (1.0f, b.getWidth()));
        seg = fx < 0.34f ? 0 : (fx < 0.67f ? 1 : 2);
        timeAtDown  = val (segTime());
        levelAtDown = val (segLevel());
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        // horizontal drag stretches the segment, vertical sets its end level
        setVal (segTime(),  timeAtDown  + (float) e.getDistanceFromDragStartX() / 220.0f);
        setVal (segLevel(), levelAtDown - (float) e.getDistanceFromDragStartY() / 120.0f);
        repaint();
    }

private:
    float val (const juce::String& id) const
    {
        auto* pr = apvts.getParameter (id);
        return pr != nullptr ? pr->getValue() : 0.0f;
    }
    void setVal (const juce::String& id, float v)
    {
        if (auto* pr = apvts.getParameter (id)) pr->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, v));
    }
    juce::Rectangle<float> plotArea() const
    {
        auto b = getLocalBounds().toFloat().reduced (2.0f);
        const float u = b.getHeight() / 66.0f;
        b.removeFromTop (11.0f * u); b.removeFromBottom (11.0f * u);
        return b;
    }
    const juce::String& segTime()  const { return seg == 0 ? p.atkT : (seg == 1 ? p.decT : p.relT); }
    const juce::String& segLevel() const { return seg == 0 ? p.atkL : (seg == 1 ? p.decL : p.relL); }

    int   seg = 0;
    float timeAtDown = 0.0f, levelAtDown = 0.0f;

    juce::AudioProcessorValueTreeState& apvts;
    Ids p;
    juce::String title;
    juce::Colour colour;
    bool withIni;
};

// ---------------------------------------------------------------------------
/** The 3D swarm view: a box in one-point perspective with the particles inside.
    Axes follow the original -- Vol runs left-to-back, Pan left-to-right, Pitch
    vertically -- and each dot is tinted by its Resonance and lightened by its
    Noise, which is what the two colour keys above the box mean. */
class SwarmView : public juce::Component, private juce::Timer
{
public:
    explicit SwarmView (const swarm::SwarmEngine& e) : engine (e) { startTimerHz (30); }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();

        g.setGradientFill (juce::ColourGradient (colours::glow, b.getCentreX(), b.getCentreY(),
                                                 colours::glow.darker (0.35f), b.getX(), b.getBottom(), true));
        g.fillRect (b);

        auto box = b.reduced (b.getWidth() * 0.055f, b.getHeight() * 0.055f);
        const float depth = 0.62f;      // how much the back face shrinks
        auto back = box.withSizeKeepingCentre (box.getWidth() * depth, box.getHeight() * depth)
                       .translated (0.0f, -box.getHeight() * 0.02f);

        g.setColour (colours::cubeFloor);
        g.fillRect (box);
        // side walls, drawn as shaded quads from the front rim to the back rim
        auto quad = [&] (juce::Point<float> a1, juce::Point<float> a2,
                         juce::Point<float> a3, juce::Point<float> a4, juce::Colour c)
        {
            juce::Path p; p.startNewSubPath (a1); p.lineTo (a2); p.lineTo (a3); p.lineTo (a4);
            p.closeSubPath(); g.setColour (c); g.fillPath (p);
        };
        quad (box.getTopLeft(), back.getTopLeft(), back.getBottomLeft(), box.getBottomLeft(),
              colours::cubeFace.darker (0.10f));
        quad (box.getTopRight(), back.getTopRight(), back.getBottomRight(), box.getBottomRight(),
              colours::cubeFace.darker (0.16f));
        quad (box.getTopLeft(), back.getTopLeft(), back.getTopRight(), box.getTopRight(),
              colours::cubeFace.darker (0.22f));
        quad (box.getBottomLeft(), back.getBottomLeft(), back.getBottomRight(), box.getBottomRight(),
              colours::cubeFace.brighter (0.06f));
        g.setColour (colours::cubeFace.brighter (0.12f));
        g.fillRect (back);

        g.setColour (juce::Colours::white.withAlpha (0.35f));
        g.drawRect (box, 1.0f);
        g.drawRect (back, 1.0f);
        for (int i = 0; i < 4; ++i)
        {
            const juce::Point<float> f[4] = { box.getTopLeft(), box.getTopRight(),
                                              box.getBottomRight(), box.getBottomLeft() };
            const juce::Point<float> k[4] = { back.getTopLeft(), back.getTopRight(),
                                              back.getBottomRight(), back.getBottomLeft() };
            g.drawLine ({ f[i], k[i] }, 0.6f);
        }

        // ---- particles ----
        swarm::ParticleView pv[swarm::kMaxParticles];
        const int n = engine.getParticles (pv, swarm::kMaxParticles);
        for (int i = 0; i < n; ++i)
        {
            const float vol   = pv[i].value[swarm::DVol];
            const float pitch = pv[i].value[swarm::DPitch];
            const float pan   = pv[i].value[swarm::DPan];
            const float res   = pv[i].value[swarm::DRes];
            const float noise = pv[i].value[swarm::DNoise];

            // Vol is depth: louder particles come forward.
            const float t = 1.0f - juce::jlimit (0.0f, 1.0f, vol);
            auto lerp = [t] (juce::Rectangle<float> f, juce::Rectangle<float> k)
            {
                return juce::Rectangle<float> (f.getX() + (k.getX() - f.getX()) * t,
                                               f.getY() + (k.getY() - f.getY()) * t,
                                               f.getWidth() + (k.getWidth() - f.getWidth()) * t,
                                               f.getHeight() + (k.getHeight() - f.getHeight()) * t);
            };
            const auto plane = lerp (box, back);
            const float x = plane.getX() + plane.getWidth() * juce::jlimit (0.0f, 1.0f, pan);
            const float y = plane.getBottom() - plane.getHeight() * juce::jlimit (0.0f, 1.0f, pitch);

            // Res picks the hue across the rainbow key; Noise washes it out.
            juce::Colour c = juce::Colour::fromHSV (juce::jlimit (0.0f, 0.83f, 0.83f * res),
                                                    1.0f - noise * 0.75f, 1.0f, 1.0f);
            const float sz = juce::jmap (1.0f - t, 1.6f, 3.4f);

            // motion streak, as in the original
            const float k  = 0.04f;      // streak length per unit of velocity
            const float sx = juce::jlimit (-14.0f, 14.0f, pv[i].vel[swarm::DPan]   * plane.getWidth()  * k);
            const float sy = juce::jlimit (-14.0f, 14.0f, -pv[i].vel[swarm::DPitch] * plane.getHeight() * k);
            if (std::abs (sx) + std::abs (sy) > 0.6f)
            {
                g.setColour (c.withAlpha (0.55f));
                g.drawLine (x - sx, y - sy, x, y, sz * 0.9f);
            }
            g.setColour (c);
            g.fillEllipse (x - sz * 0.5f, y - sz * 0.5f, sz, sz);
        }

        // axis labels
        const float u = b.getWidth() / 378.0f;
        const int lw = (int) (42 * u), lh = (int) (13 * u), pad = (int) (2 * u);
        g.setColour (juce::Colours::black.withAlpha (0.7f));
        g.setFont (juce::FontOptions (juce::jmax (7.0f, 9.0f * u)));
        g.drawText ("Pitch", juce::Rectangle<int> ((int) b.getX() + pad, (int) b.getY() + pad, lw, lh),
                    juce::Justification::centredLeft, false);
        g.drawText ("Vol", juce::Rectangle<int> ((int) b.getX() + pad, (int) b.getBottom() - lh, lw, lh),
                    juce::Justification::centredLeft, false);
        g.drawText ("Pan", juce::Rectangle<int> ((int) b.getRight() - lw - pad, (int) b.getBottom() - lh, lw, lh),
                    juce::Justification::centredRight, false);
    }

private:
    void timerCallback() override { repaint(); }
    const swarm::SwarmEngine& engine;
};

// ---------------------------------------------------------------------------
/** The wordmark. The original is a chunky yellow pixel face sitting on a strip
    of waveform; this draws the same idea rather than tracing the bitmap. */
class Wordmark : public juce::Component
{
public:
    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff1c1c10));
        g.fillRect (b);

        juce::Random rnd (7);
        g.setColour (juce::Colour (0xff7a6a12));
        for (float x = b.getX(); x < b.getRight(); x += 1.0f)
        {
            const float a = (float) rnd.nextDouble() * 0.5f + (float) rnd.nextDouble() * 0.5f;
            const float h = b.getHeight() * 0.46f * a * a;
            g.drawLine (x, b.getCentreY() - h, x, b.getCentreY() + h, 1.0f);
        }

        auto drawWord = [&] (const juce::String& s, juce::Rectangle<float> r, float sq)
        {
            g.setFont (juce::FontOptions ("Monospaced", r.getHeight() * sq,
                                          juce::Font::bold));
            g.setColour (juce::Colour (0xff5a4400));
            g.drawText (s, r.translated (1.5f, 1.5f).toNearestInt(),
                        juce::Justification::centred, false);
            g.setColour (juce::Colour (0xffffd21e));
            g.drawText (s, r.toNearestInt(), juce::Justification::centred, false);
        };
        auto top = b.removeFromTop (b.getHeight() * 0.55f);
        drawWord ("SWARM", top.reduced (4.0f, 1.0f), 0.95f);
        drawWord ("SYNTH", b.reduced (4.0f, 1.0f).translated (b.getWidth() * 0.10f, -2.0f), 0.92f);

        g.setColour (juce::Colours::black.withAlpha (0.8f));
        g.drawRect (getLocalBounds().toFloat(), 1.5f);
    }
};

} // namespace swarmui
