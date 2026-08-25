#pragma once
// The swarm synthesis engine.
//
// Model recovered by measuring the original SwarmSynth.dll through the Wine
// host in ../analysis. Two parts:
//
//   1. A cloud of oscillators ("particles") moving in a 5-dimensional box
//      (Vol, Pitch, Pan, Res, Noise). The original's 3D display plots three of
//      those spatially and colours the dots by the other two. Each dimension
//      has a centre ("home") and a half-width ("range"); a particle's position
//      is an offset in -1..1 that is scaled by the range.
//
//   2. A formant grain oscillator per particle. MEASURED: at Resonance 0 the
//      output is a pure sine at f0; as Resonance rises the output becomes a
//      short burst repeating at f0, whose spectral peak sits at
//          f_res = f0 * (1 + 46 * res^3)          [rms error 0.375 harmonics]
//      and whose burst length is very close to one cycle of f_res. That is a
//      FOF / VOSIM style formant grain.
//
// What is measured and what is modelled is flagged per block. The motion law
// (attract / repel / proximity / std-dev) is the least pinned down; see
// analysis/FINDINGS.md.

#include <cmath>
#include <cstdint>
#include <vector>
#include <algorithm>
#include <atomic>

#include "SwarmEnvModel.h"

namespace swarm {

enum Dim { DVol = 0, DPitch, DPan, DRes, DNoise, NumDims };

constexpr int   kMaxParticles = 64;    // MEASURED: Oscillators is an int 1..64
constexpr int   kControlBlock = 32;    // particle integration rate
constexpr float kTwoPi        = 6.28318530718f;

// ---------------------------------------------------------------------------

struct Rng {
    uint32_t s = 1;
    void  seed (uint32_t v)  { s = v ? v : 1u; }
    uint32_t next()          { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float uni()              { return (float) (next() >> 8) * (1.0f / 16777216.0f); }
    float bi()               { return uni() * 2.0f - 1.0f; }
    float gauss()            { return (bi() + bi() + bi()) * 0.57735027f; }
};

// ---------------------------------------------------------------------------
// One of the eleven envelopes: a piecewise-linear run through its breakpoints,
// a hold at the last one, then a release. MEASURED: every time control maps as
//   t_ms = 0.1 + 9999.9 * v^2   (fit residual < 0.005 ms across the range)
// See SwarmEnvModel.h for why this is a breakpoint list and not a fixed ADSR.
// ---------------------------------------------------------------------------
class Env {
public:
    void  setSampleRate (double sr) { srate = sr > 0 ? sr : 44100.0; }
    void  configure (const EnvShape& s) { shape = s; }

    void  noteOn()  { stage = Run;     idx = 0; level = shape.ini; pos = 0.0; }
    void  noteOff() { stage = Release; from = level;               pos = 0.0; }
    bool  isActive() const { return stage != Idle; }
    float current()  const { return level; }

    float tick()
    {
        if (stage == Idle) return level;

        if (stage == Release)
        {
            const double n = std::max (1.0, (double) shape.relT * srate);
            if (++pos >= n) { stage = Idle; level = shape.relL; }
            else            level = from + (shape.relL - from) * (float) (pos / n);
            return level;
        }

        // running through the breakpoints, then holding at the last one
        if (idx >= shape.n) return level;

        const float a = (idx == 0) ? shape.ini : shape.p[idx - 1].l;
        const double n = std::max (1.0, (double) shape.p[idx].t * srate);
        if (++pos >= n) { level = shape.p[idx].l; ++idx; pos = 0.0; }
        else            level = a + (shape.p[idx].l - a) * (float) (pos / n);
        return level;
    }

private:
    enum Stage { Idle, Run, Release };
    EnvShape shape;
    Stage    stage = Idle;
    double   srate = 44100.0, pos = 0.0;
    int      idx = 0;
    float    level = 0.0f, from = 0.0f;
};

// ---------------------------------------------------------------------------
// Everything the engine needs, in engineering units rather than 0..1.
// PluginProcessor converts using the laws in SwarmParams.h.
// ---------------------------------------------------------------------------
struct SwarmSettings {
    int   numParticles = 8;

    // per-dimension centre ("home") and half-width ("range")
    float home[NumDims]  = { 1.0f, 0.0f, 0.0f, 0.10f, 0.10f };
    float range[NumDims] = { 0.0f, 0.0f, 0.0f, 0.0f,  0.0f  };

    // motion
    float speed      = 0.5f;   // 0..1
    float speedLfo   = 0.0f;   // 0..1
    float stdDev     = 0.5f;   // 0..1, spread of the initial state
    float reflection = 0.5f;   // 0..1, energy kept when a particle hits a wall
    float attract    = 0.5f;
    float repel      = 0.5f;
    float proximity  = 0.5f;   // interaction radius

    // global
    float lowpass    = 1.0f;
    float q          = 0.0f;
    float overdrive  = 0.0f;
    float portamento = 0.02f;  // seconds
    uint32_t seed    = 12346;

    EnvShape env[11];          // vol, pitch, pan, res, noise, then their five
                               // variance envelopes, then speed
    float envRange[NumDims] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };  // Pt/Pn/Rs/Ns Env Rg
};

enum EnvSlot { EVol = 0, EPitch, EPan, ERes, ENoise,
               EVolVar, EPitchVar, EPanVar, EResVar, ENoiseVar, ESpeed, NumEnvs };

// ---------------------------------------------------------------------------

struct Particle {
    float pos[NumDims] = { 0, 0, 0, 0, 0 };   // -1..1 inside the range box
    float vel[NumDims] = { 0, 0, 0, 0, 0 };
    float phase = 0.0f;                       // 0..1 over one f0 period
};

/** One particle as the editor sees it: absolute dimension values, plus the
    velocity it is carrying so the view can draw motion streaks the way the
    original's display does. */
struct ParticleView {
    float value[NumDims] = { 0, 0, 0, 0, 0 };   // vol/pitch/pan/res/noise, 0..1
    float vel[NumDims]   = { 0, 0, 0, 0, 0 };
};

class SwarmEngine {
public:
    void prepare (double sampleRate, int /*maxBlock*/)
    {
        srate = sampleRate;
        for (auto& e : envs) e.setSampleRate (sampleRate);
        reset();
    }

    void reset()
    {
        for (auto& p : particles) p = Particle();
        gate = false; level = 0.0f;
        lpZ1 = lpZ2 = 0.0f;
        ctrlCount = 0; lfoPhase = 0.0f;
    }

    void setSettings (const SwarmSettings& s) { cfg = s; }

    void noteOn (int midiNote, float velocity)
    {
        targetHz = 440.0f * std::pow (2.0f, (midiNote - 69) / 12.0f);
        if (! gate) currentHz = targetHz;      // no glide into the first note
        vel  = velocity;
        gate = true;

        rng.seed (cfg.seed);
        // MEASURED: Std. Dev. changes how tightly the cloud starts packed.
        // MODELLED: a gaussian scatter whose sigma is stdDev, clipped to the box.
        for (int i = 0; i < cfg.numParticles; ++i)
        {
            for (int d = 0; d < NumDims; ++d)
            {
                particles[i].pos[d] = std::clamp (rng.gauss() * cfg.stdDev, -1.0f, 1.0f);
                particles[i].vel[d] = rng.bi() * cfg.speed;
            }
            particles[i].phase = rng.uni();    // decorrelate the grain trains
        }
        for (int e = 0; e < NumEnvs; ++e) { envs[e].configure (cfg.env[e]); envs[e].noteOn(); }
    }

    void noteOff()
    {
        gate = false;
        for (auto& e : envs) e.noteOff();
    }

    bool isActive() const { return envs[EVol].isActive(); }

    /** Copies the latest particle state for the editor. Returns how many. */
    int getParticles (ParticleView* dst, int maxCount) const
    {
        const int r = viewIndex.load (std::memory_order_acquire);
        const int n = std::min (viewCount.load (std::memory_order_relaxed), maxCount);
        for (int i = 0; i < n; ++i) dst[i] = viewBuf[r][i];
        return n;
    }

    bool isSounding() const { return envs[EVol].isActive(); }

    void process (float* outL, float* outR, int numSamples)
    {
        const float glide = cfg.portamento > 0.0001f
                          ? (float) std::exp (-1.0 / (cfg.portamento * srate)) : 0.0f;

        for (int n = 0; n < numSamples; ++n)
        {
            if (ctrlCount-- <= 0) { updateParticles(); ctrlCount = kControlBlock - 1; }

            currentHz = targetHz + (currentHz - targetHz) * glide;

            const float envVol   = envs[EVol].tick();
            const float envPitch = envs[EPitch].tick();
            const float envPan   = envs[EPan].tick();
            const float envRes   = envs[ERes].tick();
            const float envNoise = envs[ENoise].tick();
            for (int e = EVolVar; e < NumEnvs; ++e) envs[e].tick();

            float sumL = 0.0f, sumR = 0.0f;

            for (int i = 0; i < cfg.numParticles; ++i)
            {
                auto& p = particles[i];

                // dimension value = home + range * position, then the matching
                // envelope offsets it by its own range control.
                const float vVol   = clamp01 (cfg.home[DVol]   + cfg.range[DVol]   * p.pos[DVol]);
                const float vPan   = std::clamp (cfg.home[DPan] + cfg.range[DPan] * p.pos[DPan]
                                                 + cfg.envRange[DPan] * (envPan - 0.5f), -1.0f, 1.0f);
                const float vRes   = clamp01 (cfg.home[DRes]   + cfg.range[DRes]   * p.pos[DRes]
                                              + cfg.envRange[DRes] * (envRes - 0.5f));
                const float vNoise = clamp01 (cfg.home[DNoise] + cfg.range[DNoise] * p.pos[DNoise]
                                              + cfg.envRange[DNoise] * (envNoise - 0.5f));

                // Pitch offset is in semitones: Pt Env Rg is 0..24 s/tone.
                const float semis = cfg.range[DPitch] * p.pos[DPitch] * 12.0f
                                  + cfg.envRange[DPitch] * (envPitch - 0.5f) * 2.0f;
                const float hz    = currentHz * std::pow (2.0f, semis / 12.0f);

                // ---- the formant grain -------------------------------------
                // MEASURED: peak at f0*(1+46*res^3); burst is ~one cycle of it.
                const float M   = 1.0f + 46.0f * vRes * vRes * vRes;
                const float inc = hz / (float) srate;
                p.phase += inc;
                if (p.phase >= 1.0f) p.phase -= 1.0f;

                const float g = p.phase * M;            // position in formant cycles
                float s = 0.0f;
                // The burst *looks* unwindowed in a time plot, but A/B against
                // the original says otherwise: a raised-cosine window scores
                // 14.8 dB spectral distance where no window scores 24.6 dB.
                // Trust the metric, not the eyeball. (analysis/ab.py)
                if (g < 1.0f)
                {
                    const float w = 0.5f * (1.0f - std::cos (kTwoPi * g));
                    s = std::sin (kTwoPi * g) * (vRes > 0.0f ? w : 1.0f);
                }
                if (vNoise > 0.0f) s += rng.bi() * vNoise * (g < 1.0f ? 1.0f : 0.25f);

                const float a  = s * vVol;
                const float pl = std::sqrt (0.5f * (1.0f - vPan));
                const float pr = std::sqrt (0.5f * (1.0f + vPan));
                sumL += a * pl;
                sumR += a * pr;
            }

            const float norm = 1.0f / std::sqrt ((float) std::max (1, cfg.numParticles));
            sumL *= norm * envVol * vel;
            sumR *= norm * envVol * vel;

            // MODELLED: the original has a separate global LoPass/Q pair and an
            // Overdrive knob. A 2-pole state-variable lowpass plus tanh drive.
            filterStereo (sumL, sumR);
            if (cfg.overdrive > 0.0f)
            {
                const float d = 1.0f + cfg.overdrive * 24.0f;
                sumL = std::tanh (sumL * d) / std::tanh (d);
                sumR = std::tanh (sumR * d) / std::tanh (d);
            }

            // NB: master volume is already in vVol via home[DVol]; do not
            // apply it a second time here.
            outL[n] += sumL;
            outR[n] += sumR;
        }
    }

private:
    static float clamp01 (float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

    // MODELLED. What is measured: Speed 0 freezes the cloud exactly, Speed
    // raises the drift rate and saturates; Pitch Var sets the vertical extent
    // of the box; Reflection changes how much the cloud spreads. The pairwise
    // force law below is a boids-style stand-in, not a recovered formula.
    void updateParticles()
    {
        const float dt      = (float) (kControlBlock / srate);
        const float spdEnv  = envs[ESpeed].current();
        lfoPhase += dt * 4.0f * cfg.speedLfo;
        if (lfoPhase >= 1.0f) lfoPhase -= 1.0f;
        const float lfo   = cfg.speedLfo > 0.0f ? 0.5f + 0.5f * std::sin (kTwoPi * lfoPhase) : 1.0f;
        const float vmax  = cfg.speed * spdEnv * lfo * 4.0f;
        if (vmax <= 0.0f) return;              // MEASURED: Speed 0 => zero drift

        const float prox = 0.05f + cfg.proximity * 1.5f;

        for (int i = 0; i < cfg.numParticles; ++i)
        {
            float f[NumDims] = { 0, 0, 0, 0, 0 };
            for (int j = 0; j < cfg.numParticles; ++j)
            {
                if (j == i) continue;
                float d[NumDims], r2 = 1e-9f;
                for (int k = 0; k < NumDims; ++k)
                { d[k] = particles[j].pos[k] - particles[i].pos[k]; r2 += d[k] * d[k]; }
                const float r = std::sqrt (r2);
                if (r > prox) continue;
                const float att = cfg.attract;
                const float rep = cfg.repel / (r2 + 0.01f);
                for (int k = 0; k < NumDims; ++k) f[k] += (att - rep) * d[k] / r;
            }

            auto& p = particles[i];
            for (int k = 0; k < NumDims; ++k)
            {
                p.vel[k] += f[k] * dt;
                p.vel[k] = std::clamp (p.vel[k], -vmax, vmax);
                p.pos[k] += p.vel[k] * dt;

                // walls of the box: reflect, keeping `reflection` of the energy
                if (p.pos[k] > 1.0f)  { p.pos[k] =  2.0f - p.pos[k]; p.vel[k] = -p.vel[k] * cfg.reflection; }
                if (p.pos[k] < -1.0f) { p.pos[k] = -2.0f - p.pos[k]; p.vel[k] = -p.vel[k] * cfg.reflection; }
                p.pos[k] = std::clamp (p.pos[k], -1.0f, 1.0f);
            }
        }
        publishSnapshot();
    }

    // Double-buffered so the editor can read while audio writes. A torn read
    // would only ever cost one frame of one dot, so no lock is warranted.
    void publishSnapshot()
    {
        const int w = 1 - viewIndex.load (std::memory_order_relaxed);
        auto& dst = viewBuf[w];
        for (int i = 0; i < cfg.numParticles; ++i)
        {
            for (int k = 0; k < NumDims; ++k)
            {
                const float centre = (k == DPan) ? (cfg.home[DPan] * 0.5f + 0.5f)
                                   : (k == DPitch) ? 0.5f : cfg.home[k];
                dst[i].value[k] = std::clamp (centre + cfg.range[k] * particles[i].pos[k] * 0.5f,
                                              0.0f, 1.0f);
                dst[i].vel[k] = particles[i].vel[k];
            }
        }
        viewCount.store (cfg.numParticles, std::memory_order_relaxed);
        viewIndex.store (w, std::memory_order_release);
    }

    void filterStereo (float& l, float& r)
    {
        if (cfg.lowpass >= 0.999f && cfg.q <= 0.0f) return;
        // cutoff maps exponentially over the audible range
        const float fc  = 30.0f * std::pow (700.0f, cfg.lowpass);
        const float g   = std::tan (3.14159265f * std::min (fc, (float) srate * 0.45f) / (float) srate);
        const float k   = 2.0f - 1.98f * cfg.q;
        const float a1  = 1.0f / (1.0f + g * (g + k));
        auto svf = [&] (float in, float& z1, float& z2)
        {
            const float hp = (in - (g + k) * z1 - z2) * a1;
            const float bp = g * hp + z1;  z1 = g * hp + bp;
            const float lp = g * bp + z2;  z2 = g * bp + lp;
            return lp;
        };
        l = svf (l, lpZ1, lpZ2);
        r = svf (r, rpZ1, rpZ2);
    }

    SwarmSettings cfg;
    Particle particles[kMaxParticles];
    ParticleView viewBuf[2][kMaxParticles];
    mutable std::atomic<int> viewIndex { 0 };
    mutable std::atomic<int> viewCount { 0 };
    Env      envs[NumEnvs];
    Rng      rng;
    double   srate = 44100.0;
    float    currentHz = 440.0f, targetHz = 440.0f, vel = 1.0f, level = 0.0f;
    float    lpZ1 = 0, lpZ2 = 0, rpZ1 = 0, rpZ2 = 0, lfoPhase = 0;
    int      ctrlCount = 0;
    bool     gate = false;
};

} // namespace swarm
