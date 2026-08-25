#pragma once
// Reads an original SwarmSynth patch.
//
// The original stores its state as an opaque VST program chunk (it sets
// effFlagsProgramChunks). The layout was recovered by setting one parameter at
// a time and diffing the chunk -- see analysis/chunkdiff.sh. 100 of the 101
// parameters resolved to a single 4-byte slot each; the 101st (Seed) sits at
// 0x550 and is re-randomised per instance, which is why the diff could not
// isolate it on its own.
//
//   0x000..0x017   patch name, 24 bytes ASCII
//   int32 slots    the 19 integer-quantised controls (see kParams)
//   float slots    the 77 envelope times and levels
//   total          1908 bytes
//
// This reads a raw chunk, or the standard .fxp wrapper around one.

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include "SwarmParams.h"

namespace swarm {

inline constexpr int kChunkSize = 1908;
inline constexpr int kNameLen   = 24;

struct Preset {
    std::string name;
    float value[kNumParams] = { 0 };   // normalised 0..1, ready for the APVTS
    bool  ok = false;
};

namespace detail {
    inline int32_t rdI32 (const uint8_t* d, int off)
    { int32_t v; std::memcpy (&v, d + off, 4); return v; }
    inline float rdF32 (const uint8_t* d, int off)
    { float v; std::memcpy (&v, d + off, 4); return v; }
    inline uint32_t beU32 (const uint8_t* p)
    { return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 8) | p[3]; }
}

// Turn a stored value back into the normalised 0..1 the parameter uses.
inline float denormalise (const ParamSpec& s, const uint8_t* chunk)
{
    if (s.chunkOffset < 0) return 0.0f;
    if (s.chunkIsFloat) return detail::rdF32 (chunk, s.chunkOffset);

    const float n = (float) detail::rdI32 (chunk, s.chunkOffset);
    switch (s.quant)
    {
        case Quant::Pct:   return n / 100.0f;
        case Quant::Pan:   return (n + 100.0f) / 200.0f;
        case Quant::Count: return (n - 1.0f) / 63.0f;
        case Quant::Ms1k:  return (n - 1.0f) / 999.0f;
        case Quant::Semi:  return n / 24.0f;
        case Quant::Pct50: return n / 50.0f;
        case Quant::Real:  default: return n / 134217728.0f;   // Seed: v * 2^27
    }
}

inline Preset parseChunk (const uint8_t* data, size_t size)
{
    Preset p;
    if (size < (size_t) kChunkSize) return p;

    char nm[kNameLen + 1] = { 0 };
    std::memcpy (nm, data, kNameLen);
    p.name = nm;

    for (int i = 0; i < kNumParams; ++i)
        p.value[i] = denormalise (kParams[i], data);
    p.ok = true;
    return p;
}

// Accepts either a bare chunk or an .fxp with an FPCh/FBCh payload.
inline Preset parseFile (const std::vector<uint8_t>& bytes)
{
    const uint8_t* d = bytes.data();
    const size_t   n = bytes.size();

    if (n >= 8 && std::memcmp (d, "CcnK", 4) == 0)
    {
        if (n >= 60 && std::memcmp (d + 8, "FPCh", 4) == 0)
        {
            const uint32_t cs = detail::beU32 (d + 56);
            if (60 + cs <= n) return parseChunk (d + 60, cs);
        }
        if (n >= 156 && std::memcmp (d + 8, "FBCh", 4) == 0)
        {
            const uint32_t cs = detail::beU32 (d + 152);
            if (156 + cs <= n) return parseChunk (d + 156, cs);
        }
        return {};
    }
    return parseChunk (d, n);
}

} // namespace swarm
