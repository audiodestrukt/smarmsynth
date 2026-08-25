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

    // --- the five per-particle dimensions ---------------------------------
    v[0]  = r.range (0.60f, 0.92f);        // Volume: audible without pinning the output
    v[1]  = r.range (0.35f, 0.65f);        // Pan: near centre, the swarm spreads it
    v[2]  = r.range (0.00f, 0.85f);        // Resonance: the timbre control
    v[3]  = r.low   (0.00f, 0.60f, 2);     // Noise: a little goes a long way

    // variance is the whole point of the instrument, so be generous
    for (int i = 4; i <= 8; ++i) v[i] = r.low (0.0f, 0.85f, 2);
    if (r.chance (0.7f)) v[5] = r.range (0.05f, 0.45f);   // keep pitch spread musical

    // --- the flocking model ------------------------------------------------
    v[9]  = r.range (0.10f, 0.90f);        // Speed
    v[10] = r.chance (0.5f) ? r.range (0.0f, 0.7f) : 0.0f;   // Speed LFO
    v[11] = r.range (0.0f, 1.0f);          // Std. Dev.
    v[12] = r.range (0.2f, 1.0f);          // Reflection
    v[13] = r.range (0.0f, 1.0f);          // Attract
    v[14] = r.range (0.0f, 1.0f);          // Repel
    v[15] = r.range (0.1f, 1.0f);          // Proximity

    // --- global ------------------------------------------------------------
    v[16] = r.range (0.45f, 1.00f);        // Lowpass: leave the top open-ish
    v[17] = r.low   (0.00f, 0.80f, 2);     // Q
    v[18] = r.chance (0.35f) ? r.range (0.0f, 0.6f) : 0.0f;  // Overdrive
    v[19] = r.range (0.05f, 0.55f);        // Oscillators: roughly 4..36
    v[20] = r.low   (0.0f, 0.35f, 2);      // Portamento
    v[21] = r.uni();                       // Seed

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
