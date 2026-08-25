#pragma once
// The original's power-on state, read straight out of its own chunk
// (analysis/data/param_map.csv and the chunk dump). One source of truth so the
// plugin and the offline renderer cannot disagree.
#include "SwarmParams.h"

namespace swarm {

inline float defaultValue (int i)
{
    // envelope blocks: base index -> {Ini, AtkTm, AtkLv, DecTm, DecLv, RelTm, RelLv}
    // value envelopes rest at 50%, variance envelopes at 100%
    const float T100 = envTimeNorm (100.0f);
    const float T500 = envTimeNorm (500.0f);
    const float T200 = envTimeNorm (200.0f);

    switch (i)
    {
        case 0:  return 1.00f;                 // Volume    100
        case 1:  return 0.50f;                 // Pan         0
        case 2:  return 0.10f;                 // Resonance  10
        case 3:  return 0.10f;                 // Noise      10
        case 9:  case 11: case 12:
        case 13: case 14: case 15: return 0.50f;   // Speed, StdDev, Reflection,
                                                   // Attract, Repel, Proximity
        case 16: return 1.00f;                 // Lowpass   100
        case 19: return 7.0f / 63.0f;          // Oscillators 8
        case 20: return 19.0f / 999.0f;        // Portamento 20 ms
        case 21: return 12346.0f / 134217728.0f;
        case 22: return T100;  case 23: return 1.00f;   // Vol env
        case 24: return T500;  case 25: return 0.50f;
        case 26: return T200;
        default: break;
    }
    if (i == 27 || i == 35 || i == 43 || i == 51) return 0.0f;   // env ranges

    const int valueBases[4]    = { 28, 36, 44, 52 };
    const int varianceBases[6] = { 59, 66, 73, 80, 87, 94 };
    auto inBlock = [i] (int b) { return i >= b && i <= b + 6; };

    for (int b : valueBases)
        if (inBlock (b))
        {
            const int k = i - b;
            if (k == 1) return T100;
            if (k == 3) return T500;
            if (k == 5) return T200;
            return 0.50f;
        }
    for (int b : varianceBases)
        if (inBlock (b))
        {
            const int k = i - b;
            if (k == 1) return T100;
            if (k == 3) return T500;
            if (k == 5) return T200;
            return 1.00f;
        }
    return 0.0f;
}

} // namespace swarm
