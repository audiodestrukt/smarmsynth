// WebAssembly entry point.
//
// The synthesis engine is header-only C++ with no framework in it, so this is a
// thin shim rather than a port: allocate an engine, expose the parameter table,
// and let an AudioWorklet drive process(). Everything below is the same code
// the plugin and the offline renderer use.

#include <emscripten.h>
#include <cstring>
#include <cstdint>
#include <vector>

#include "../../juce/Source/SwarmParams.h"
#include "../../juce/Source/SwarmEnvModel.h"
#include "../../juce/Source/SwarmEngine.h"
#include "../../juce/Source/SwarmDefaults.h"
#include "../../juce/Source/SwarmAnarchy.h"
#include "../../juce/Source/SwarmPreset.h"

using namespace swarm;

namespace {

SwarmEngine  gEngine;
float        gParam[kNumParams];
bool         gDirty = true;
std::vector<float> gL, gR;
constexpr int kParticleStride = 7;           // see swarm_particles()
ParticleView gView[kMaxParticles];
float        gViewFlat[kMaxParticles * kParticleStride];

void applySettings()
{
    auto r  = [] (int i) { return gParam[i]; };
    auto ms = [] (float v) { return envTimeMs (v) * 0.001f; };

    SwarmSettings s;
    s.numParticles = 1 + (int) (r (19) * 63.0f);
    s.home[DVol]   = r (0);
    s.home[DPan]   = r (1) * 2.0f - 1.0f;
    s.home[DRes]   = r (2);
    s.home[DNoise] = r (3);
    s.home[DPitch] = 0.0f;
    s.range[DVol]  = r (4); s.range[DPitch] = r (5); s.range[DPan] = r (6);
    s.range[DRes]  = r (7); s.range[DNoise] = r (8);
    s.speed = r (9); s.speedLfo = r (10); s.stdDev = r (11); s.reflection = r (12);
    s.attract = r (13); s.repel = r (14); s.proximity = r (15);
    s.lowpass = r (16); s.q = r (17); s.overdrive = r (18);
    s.portamento = (1.0f + r (20) * 999.0f) * 0.001f;
    s.seed = (uint32_t) (r (21) * 134217728.0f);
    s.envRange[DPitch] = r (27) * 24.0f; s.envRange[DPan] = r (35);
    s.envRange[DRes]   = r (43) * 0.5f;  s.envRange[DNoise] = r (51) * 0.5f;

    s.env[EVol] = makeShape (0.0f, ms (r (22)), r (23),
                             ms (r (24)), r (25), ms (r (26)), 0.0f);
    const int base[10] = { 28, 36, 44, 52, 59, 66, 73, 80, 87, 94 };
    const int slot[10] = { EPitch, EPan, ERes, ENoise, EVolVar, EPitchVar,
                           EPanVar, EResVar, ENoiseVar, ESpeed };
    for (int b = 0; b < 10; ++b)
    {
        const int i = base[b];
        s.env[slot[b]] = makeShape (r (i), ms (r (i+1)), r (i+2), ms (r (i+3)),
                                    r (i+4), ms (r (i+5)), r (i+6));
    }
    gEngine.setSettings (s);
    gDirty = false;
}

} // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE
void swarm_init (float sampleRate, int maxBlock)
{
    for (int i = 0; i < kNumParams; ++i) gParam[i] = defaultValue (i);
    gEngine.prepare (sampleRate, maxBlock);
    gL.assign ((size_t) maxBlock, 0.0f);
    gR.assign ((size_t) maxBlock, 0.0f);
    applySettings();
}

EMSCRIPTEN_KEEPALIVE int   swarm_num_params()            { return kNumParams; }
EMSCRIPTEN_KEEPALIVE float swarm_get_param (int i)
{ return (i >= 0 && i < kNumParams) ? gParam[i] : 0.0f; }

EMSCRIPTEN_KEEPALIVE
void swarm_set_param (int i, float v)
{
    if (i < 0 || i >= kNumParams) return;
    gParam[i] = quantise (kParams[i].quant, v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v));
    gDirty = true;
}

EMSCRIPTEN_KEEPALIVE const char* swarm_param_name (int i)
{ return (i >= 0 && i < kNumParams) ? kParams[i].name : ""; }
EMSCRIPTEN_KEEPALIVE const char* swarm_param_id (int i)
{ return (i >= 0 && i < kNumParams) ? kParams[i].id : ""; }
EMSCRIPTEN_KEEPALIVE const char* swarm_param_unit (int i)
{ return (i >= 0 && i < kNumParams) ? kParams[i].unit : ""; }

/** Engineering-unit reading of a parameter, matching the plugin's own display. */
EMSCRIPTEN_KEEPALIVE
float swarm_param_display (int i)
{
    if (i < 0 || i >= kNumParams) return 0.0f;
    const float v = gParam[i];
    switch (kParams[i].disp)
    {
        case Disp::Db:    return v <= 0.0f ? -1000.0f : 20.0f * std::log10 (v);
        case Disp::Pan:   return (float) (int) (v * 200.0f - 100.0f + kGrip);
        case Disp::Count: return (float) (1 + (int) (v * 63.0f + kGrip));
        case Disp::Ms1k:  return (float) (1 + (int) (v * 999.0f + kGrip));
        case Disp::Semi:  return (float) (int) (v * 24.0f + kGrip);
        case Disp::Pct50: return (float) (int) (v * 50.0f + kGrip);
        case Disp::Time:  return envTimeMs (v);
        case Disp::Seed:  return v * 134217728.0f;
        case Disp::Pct:
        default:          return (float) (int) (v * 100.0f + kGrip);
    }
}

EMSCRIPTEN_KEEPALIVE void swarm_note_on (int note, float velocity)
{
    if (gDirty) applySettings();
    gEngine.noteOn (note, velocity);
}
EMSCRIPTEN_KEEPALIVE void swarm_note_off() { gEngine.noteOff(); }

/** Renders one block into the two float arrays the caller owns. */
EMSCRIPTEN_KEEPALIVE
void swarm_process (float* outL, float* outR, int frames)
{
    if (gDirty) applySettings();
    if ((int) gL.size() < frames) { gL.assign ((size_t) frames, 0.0f); gR.assign ((size_t) frames, 0.0f); }
    std::memset (gL.data(), 0, sizeof (float) * (size_t) frames);
    std::memset (gR.data(), 0, sizeof (float) * (size_t) frames);
    gEngine.process (gL.data(), gR.data(), frames);
    std::memcpy (outL, gL.data(), sizeof (float) * (size_t) frames);
    std::memcpy (outR, gR.data(), sizeof (float) * (size_t) frames);
}

/** Loads a factory patch: pass the raw 1908-byte chunk. */
EMSCRIPTEN_KEEPALIVE
int swarm_load_patch (const uint8_t* data, int size)
{
    std::vector<uint8_t> bytes (data, data + size);
    const auto p = parseFile (bytes);
    if (! p.ok) return 0;
    for (int i = 0; i < kNumParams; ++i) gParam[i] = p.value[i];
    gDirty = true;
    return 1;
}

EMSCRIPTEN_KEEPALIVE
void swarm_anarchy (unsigned seed)
{
    anarchy (gParam, seed);
    gDirty = true;
}

/** Fills the shared particle buffer and returns how many are in it.
    Seven floats each, read them from swarm_particle_buffer():
        pan, pitch, vol, res, noise, panVelocity, pitchVelocity              */
EMSCRIPTEN_KEEPALIVE
int swarm_particles()
{
    const int n = gEngine.getParticles (gView, kMaxParticles);
    for (int i = 0; i < n; ++i)
    {
        float* p = gViewFlat + i * kParticleStride;
        p[0] = gView[i].value[DPan];
        p[1] = gView[i].value[DPitch];
        p[2] = gView[i].value[DVol];
        p[3] = gView[i].value[DRes];
        p[4] = gView[i].value[DNoise];
        p[5] = gView[i].vel[DPan];
        p[6] = gView[i].vel[DPitch];
    }
    return n;
}

EMSCRIPTEN_KEEPALIVE float* swarm_particle_buffer() { return gViewFlat; }

} // extern "C"
