#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "SwarmLookAndFeel.h"
#include "SwarmEngine.h"
#include "SwarmEnvModel.h"
#include <functional>
#include <vector>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
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
        if (onSelect) onSelect();      // bring this control's envelopes up
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
    /** Called when the knob is touched, so the editor can switch the envelope
        panels to the dimension this knob belongs to. */
    std::function<void()> onSelect;

private:
    juce::AudioProcessorValueTreeState& apvts;
    juce::String id, text;
    juce::Colour colour;
    bool captioned;
    juce::RangedAudioParameter* param = nullptr;
    float dragStart = 0.0f;
};

// ---------------------------------------------------------------------------
/** The breakpoint envelope editor.

    Verbs match the original, which was established by scripting its editor and
    diffing its state chunk after each interaction (analysis/env_probe):

      double-click on empty plot .. add a breakpoint
      drag a point ............... move it in time and level
      right-click a point ........ delete it
      single click ............... nothing

    Times use the measured law t_ms = 0.1 + 9999.9*v^2. The first and last
    points stay mirrored into the exposed parameters so host automation still
    reaches them; any points in between are stored in the plugin state.
*/
class EnvelopeEditor : public juce::Component
{
public:
    struct Ids { juce::String atkT, atkL, decT, decL, relT, relL, ini; };

    using GetShape = std::function<swarm::EnvShape()>;
    using PutShape = std::function<void (const swarm::EnvShape&)>;

    EnvelopeEditor (juce::AudioProcessorValueTreeState& s, Ids ids, juce::String caption,
                    juce::Colour accent, bool hasIni, GetShape get, PutShape put)
        : apvts (s), p (ids), title (caption), colour (accent), withIni (hasIni),
          getShape (std::move (get)), putShape (std::move (put)) {}

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        const float u = juce::jmax (0.35f, b.getHeight() / 66.0f);

        g.setColour (colour);
        g.setFont (juce::FontOptions (juce::jmax (6.0f, 8.0f * u)));
        auto header = b.removeFromTop (11.0f * u);
        g.drawText (title, header.toNearestInt(), juce::Justification::topLeft, false);

        auto footer = b.removeFromBottom (11.0f * u);
        auto plot   = b.reduced (2.0f * u);

        g.setColour (colours::envPaper);
        g.fillRect (plot);
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.drawRect (plot, 1.0f * u);

        const auto sh = getShape();
        const float total = juce::jmax (0.0001f, sh.totalPreReleaseSeconds() + sh.relT);
        auto in = plot.reduced (0.0f, 1.5f * u);

        auto xAt = [&] (float secs) { return plot.getX() + plot.getWidth() * (secs / total); };
        auto yAt = [&] (float lvl)  { return in.getBottom() - in.getHeight() * juce::jlimit (0.0f, 1.0f, lvl); };

        // the polyline: initial level, every breakpoint, then the release
        juce::Path path;
        float acc = 0.0f;
        path.startNewSubPath (xAt (0.0f), yAt (withIni ? sh.ini : 0.0f));
        std::vector<juce::Point<float>> handles;
        handles.push_back ({ xAt (0.0f), yAt (withIni ? sh.ini : 0.0f) });
        for (int i = 0; i < sh.n; ++i)
        {
            acc += sh.p[i].t;
            const juce::Point<float> pt { xAt (acc), yAt (sh.p[i].l) };
            path.lineTo (pt);
            handles.push_back (pt);
        }
        const juce::Point<float> relEnd { xAt (acc + sh.relT), yAt (sh.relL) };
        path.lineTo (relEnd);

        // segment dividers
        g.setColour (juce::Colours::black.withAlpha (0.28f));
        for (size_t i = 1; i + 1 <= (size_t) sh.n; ++i)
            g.drawLine (handles[i].x, plot.getY(), handles[i].x, plot.getBottom(), 0.6f * u);

        g.setColour (colours::envInk);
        g.strokePath (path, juce::PathStrokeType (1.2f * u));

        for (size_t i = 0; i < handles.size(); ++i)
        {
            const bool hot = ((int) i == hoverPoint + 1);
            g.setColour (hot ? colour.brighter (0.3f) : colours::envInk);
            const float r = (hot ? 3.4f : 2.4f) * u;
            g.fillRect (handles[i].x - r, handles[i].y - r, r * 2, r * 2);
        }
        g.setColour (colours::envInk.withAlpha (0.6f));
        const float rr = 2.4f * u;
        g.fillRect (relEnd.x - rr, relEnd.y - rr, rr * 2, rr * 2);

        // durations under each segment, as the original prints them
        g.setColour (colours::text.withAlpha (0.8f));
        g.setFont (juce::FontOptions (juce::jmax (6.0f, 7.5f * u)));
        auto label = [&] (float x0, float x1, float secs)
        {
            if (x1 - x0 < 16.0f) return;
            const juce::String t = secs < 1.0f ? juce::String (juce::roundToInt (secs * 1000.0f)) + "ms"
                                               : juce::String (secs, 2) + "s";
            g.drawText ("<-" + t + "->",
                        juce::Rectangle<float> (x0, footer.getY(), x1 - x0, footer.getHeight()).toNearestInt(),
                        juce::Justification::centred, false);
        };
        for (size_t i = 0; i + 1 < handles.size(); ++i)
            label (handles[i].x, handles[i + 1].x, sh.p[i].t);
        label (handles.back().x, relEnd.x, sh.relT);
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const int h = hitTest (e.position);
        if (h != hoverPoint) { hoverPoint = h; repaint(); }
    }
    void mouseExit (const juce::MouseEvent&) override { hoverPoint = -1; repaint(); }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragPoint = hitTest (e.position);
        trace ("mouseDown hit=%d popup=%d at %.0f,%.0f", dragPoint,
               (int) e.mods.isPopupMenu(), e.position.x, e.position.y);
        if (e.mods.isPopupMenu())
        {
            // right-click on a point deletes it
            auto sh = getShape();
            if (dragPoint >= 0 && swarm::removePoint (sh, dragPoint)) { commit (sh); dragPoint = -1; }
            return;
        }
        if (dragPoint >= 0) shapeAtDown = getShape();
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        trace ("doubleClick hit=%d", hitTest (e.position));
        if (hitTest (e.position) >= 0) return;      // only empty area adds
        auto sh = getShape();
        const float total = juce::jmax (0.0001f, sh.totalPreReleaseSeconds() + sh.relT);
        const float pre   = sh.totalPreReleaseSeconds();
        auto plot = plotArea();
        const float secs = (e.position.x - plot.getX()) / juce::jmax (1.0f, plot.getWidth()) * total;
        if (secs >= pre) return;                    // the release segment is not divisible
        const float frac = pre > 0.0f ? secs / pre : 0.0f;
        const float lvl  = 1.0f - (e.position.y - plot.getY()) / juce::jmax (1.0f, plot.getHeight());
        if (swarm::insertPoint (sh, frac, lvl)) commit (sh);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        trace ("mouseDrag point=%d dx=%d dy=%d", dragPoint,
               e.getDistanceFromDragStartX(), e.getDistanceFromDragStartY());
        if (dragPoint < 0) return;
        auto sh = shapeAtDown;
        auto plot = plotArea();
        const float total = juce::jmax (0.0001f, sh.totalPreReleaseSeconds() + sh.relT);
        const float dSecs = (float) e.getDistanceFromDragStartX() / juce::jmax (1.0f, plot.getWidth()) * total;

        // horizontal: lengthen this segment, shorten the next, so the points
        // after it stay where they are
        const float newT = juce::jlimit (0.0005f, 10.0f, sh.p[dragPoint].t + dSecs);
        const float used = newT - sh.p[dragPoint].t;
        sh.p[dragPoint].t = newT;
        if (dragPoint + 1 < sh.n)
            sh.p[dragPoint + 1].t = juce::jlimit (0.0005f, 10.0f, sh.p[dragPoint + 1].t - used);

        sh.p[dragPoint].l = juce::jlimit (0.0f, 1.0f,
            shapeAtDown.p[dragPoint].l - (float) e.getDistanceFromDragStartY() / juce::jmax (1.0f, plot.getHeight()));

        commit (sh);
    }

    void mouseUp (const juce::MouseEvent&) override { dragPoint = -1; }

private:
    static void trace (const char* fmt, ...)
    {
        if (std::getenv ("SWARM_DEBUG") == nullptr) return;
        va_list a; va_start (a, fmt);
        std::vfprintf (stdout, fmt, a); std::fputc ('\n', stdout); std::fflush (stdout);
        va_end (a);
    }

    juce::Rectangle<float> plotArea() const
    {
        auto b = getLocalBounds().toFloat();
        const float u = juce::jmax (0.35f, b.getHeight() / 66.0f);
        b.removeFromTop (11.0f * u); b.removeFromBottom (11.0f * u);
        return b.reduced (2.0f * u);
    }

    /** The component's local coordinates are physical pixels, so every drawn
        size and every hit target has to scale with the editor. Without this the
        handles are a couple of pixels wide and dragging one is impossible. */
    float unitScale() const { return juce::jmax (0.35f, getHeight() / 66.0f); }

    /** Index of the breakpoint under the mouse, or -1. */
    int hitTest (juce::Point<float> pos) const
    {
        const auto sh = getShape();
        auto plot = plotArea();
        const float total = juce::jmax (0.0001f, sh.totalPreReleaseSeconds() + sh.relT);
        auto in = plot.reduced (0.0f, 1.5f * unitScale());
        const float grab = 7.0f * unitScale();
        float acc = 0.0f;
        int best = -1; float bestD = grab;
        for (int i = 0; i < sh.n; ++i)
        {
            acc += sh.p[i].t;
            const juce::Point<float> pt { plot.getX() + plot.getWidth() * (acc / total),
                                          in.getBottom() - in.getHeight() * sh.p[i].l };
            const float d = pos.getDistanceFrom (pt);
            if (d < bestD) { bestD = d; best = i; }
        }
        return best;
    }

    /** Writes the shape back: first and last points mirror into the exposed
        parameters, the whole list into the state tree. */
    void commit (const swarm::EnvShape& sh)
    {
        auto set = [this] (const juce::String& id, float v)
        {
            if (auto* pr = apvts.getParameter (id)) pr->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, v));
        };
        set (p.atkT, swarm::envTimeNorm (sh.p[0].t * 1000.0f));
        set (p.atkL, sh.p[0].l);
        const int last = sh.n - 1;
        if (last > 0) { set (p.decT, swarm::envTimeNorm (sh.p[last].t * 1000.0f)); set (p.decL, sh.p[last].l); }
        putShape (sh);
        repaint();
    }

    juce::AudioProcessorValueTreeState& apvts;
    Ids p;
    juce::String title;
    juce::Colour colour;
    bool withIni;
    GetShape getShape;
    PutShape putShape;
    swarm::EnvShape shapeAtDown;
    int dragPoint = -1, hoverPoint = -1;
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
