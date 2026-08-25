#pragma once
// The Anarchy button: randomise the patch.
//
// The original has this under Options -> "<anarchy button>", and names the
// result "random patch #%X". What it randomises internally was not recovered,
// so this is a designed randomiser rather than a reproduction of that one.
//
// The design goal is that a press should nearly always give something you can
// hear and that sounds like this synth: a moving swarm, not silence and not
// mush. So the distribution is shaped per parameter rather than uniform over
// all 101 -- uniform envelope times alone would spend most presses on a patch
// that takes eight seconds to fade in.

#include <cstdint>
#include "SwarmParams.h"

namespace swarm {

struct AnarchyRng {
    uint32_t s = 1;
    explicit AnarchyRng (uint32_t seed)
    {
        // xorshift32 cold-starts badly: seeded with a small integer its first
        // few outputs are tiny, which pinned the first parameter drawn to the
        // bottom of its range. Scramble first, then warm up.
        uint32_t x = seed * 2654435761u + 0x9E3779B9u;
        x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
        s = x ? x : 0x9E3779B9u;
        for (int i = 0; i < 8; ++i) next();
    }
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float uni()                 { return (float) (next() >> 8) * (1.0f / 16777216.0f); }
    float range (float a, float b) { return a + (b - a) * uni(); }
    /** Uniform pushed toward the low end; higher power = shorter times. */
    float low (float a, float b, int power)
    {
        float v = uni();
        for (int i = 1; i < power; ++i) v *= uni();
        return a + (b - a) * v;
    }
    bool chance (float p) { return uni() < p; }
};

/** Fills `v` with kNumParams normalised values. */
inline void anarchy (float* v, uint32_t seed)
{
    AnarchyRng r (seed);

    // The ranges below follow the distribution the original's own 45 factory
    // patches occupy (analysis/preset_stats.py), rather than being spread
    // evenly over the space. Real patches turn out to be quite particular:
    // Pan Var and Res Var are pinned near maximum, Resonance and Std. Dev. sit
    // near zero, Volume runs at full, and Portamento barely moves.

    // --- the five per-particle dimensions ---------------------------------
    v[0]  = 1.0f - r.low (0.0f, 0.32f, 2);   // Volume: p50 1.00, p10 0.68
    v[1]  = r.range (0.46f, 0.56f);          // Pan: real patches stay centred
    v[2]  = r.low   (0.0f, 0.85f, 3);        // Resonance: p50 0.07, long tail
    v[3]  = r.low   (0.0f, 0.95f, 2);        // Noise: p50 0.14, long tail

    // the spread controls are what make it a swarm, and the factory patches
    // lean on them hard
    v[4]  = r.range (0.0f,  1.0f);           // Vol Var:   p50 0.68, wide
    v[5]  = r.low   (0.0f,  1.0f, 2);        // Pitch Var: p50 0.15, long tail
    v[6]  = 1.0f - r.low (0.0f, 0.67f, 2);   // Pan Var:   p50 1.00
    v[7]  = 1.0f - r.low (0.0f, 0.50f, 2);   // Res Var:   p50 1.00
    v[8]  = r.range (0.0f,  1.0f);           // Noise Var: p50 0.66, wide

    // --- the flocking model ------------------------------------------------
    v[9]  = r.range (0.20f, 1.00f);          // Speed:      p50 0.46
    v[10] = r.chance (0.30f) ? r.range (0.1f, 1.0f) : 0.0f;   // Speed LFO: usually off
    v[11] = r.low   (0.0f, 0.63f, 2);        // Std. Dev.:  p50 0.04, max 0.63
    v[12] = r.range (0.0f, 1.0f);            // Reflection: p50 0.42
    v[13] = r.range (0.1f, 1.0f);            // Attract:    p50 0.50
    v[14] = r.range (0.1f, 1.0f);            // Repel:      p50 0.50
    v[15] = r.low   (0.05f, 1.0f, 2);        // Proximity:  p50 0.25

    // --- global ------------------------------------------------------------
    v[16] = r.range (0.50f, 1.00f);          // Lowpass: never fully shut
    v[17] = r.chance (0.30f) ? r.range (0.1f, 1.0f) : 0.0f;   // Q: usually off
    v[18] = r.chance (0.45f) ? r.range (0.1f, 1.0f) : 0.0f;   // Overdrive
    v[19] = r.low   (0.03f, 0.45f, 2);       // Oscillators: p50 0.11, about 8
    v[20] = r.low   (0.0f, 0.10f, 2);        // Portamento: p50 0.02, about 20 ms
    v[21] = r.uni();                         // Seed

    // --- envelopes ----------------------------------------------------------
    // Times lean short; levels are free. A block is
    //   value envelopes  : Ini, AtkTm, AtkLv, DecTm, DecLv, RelTm, RelLv
    //   the volume one   : AtkTm, AtkLv, DecTm, DecLv, RelTm   (no Ini/RelLv)
    auto times  = [&r] (float& t) { t = r.low (0.0f, 0.55f, 2); };

    v[22] = r.low (0.0f, 0.45f, 2);        // Vol Atk Tm: keep the onset quick
    v[23] = r.range (0.75f, 1.0f);         // Vol Atk Lv: reach a real level
    times (v[24]);                         // Vol Dec Tm
    v[25] = r.range (0.35f, 1.0f);         // Vol Dec Lv: do not decay to nothing
    v[26] = r.range (0.08f, 0.45f);        // Vol Rel Tm: an audible tail

    const int valueBases[4]    = { 28, 36, 44, 52 };
    const int varianceBases[6] = { 59, 66, 73, 80, 87, 94 };
    auto fillBlock = [&] (int b, float loLevel, float hiLevel)
    {
        v[b]     = r.range (loLevel, hiLevel);   // Ini
        times (v[b + 1]); v[b + 2] = r.range (loLevel, hiLevel);
        times (v[b + 3]); v[b + 4] = r.range (loLevel, hiLevel);
        times (v[b + 5]); v[b + 6] = r.range (loLevel, hiLevel);
    };
    for (int b : valueBases)    fillBlock (b, 0.15f, 0.85f);
    for (int b : varianceBases) fillBlock (b, 0.40f, 1.00f);

    // envelope depths: mostly modest, occasionally wild
    v[27] = r.chance (0.4f) ? r.low (0.0f, 0.5f, 2) : 0.0f;   // Pitch, semitones
    v[35] = r.low (0.0f, 0.7f, 2);                            // Pan
    v[43] = r.low (0.0f, 0.8f, 2);                            // Res
    v[51] = r.low (0.0f, 0.6f, 2);                            // Noise

    for (int i = 0; i < kNumParams; ++i)
    {
        if (v[i] < 0.0f) v[i] = 0.0f;
        if (v[i] > 1.0f) v[i] = 1.0f;
        v[i] = quantise (kParams[i].quant, v[i]);
    }
}

} // namespace swarm
