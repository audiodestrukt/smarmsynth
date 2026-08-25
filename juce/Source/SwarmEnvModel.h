#pragma once
// Envelope shape.
//
// The original's envelopes are not fixed attack/decay/release: its editor lets
// you add breakpoints, and its state chunk stores a count plus parallel time
// and level arrays (analysis/FINDINGS.md). Measured interaction model:
//
//   double-click on empty plot .. adds a breakpoint
//   drag a point ............... moves it in time and level
//   right-click a point ........ deletes it
//   single click ............... does nothing
//
// A level is exactly the vertical fraction of the plot.
//
// The VST parameter list only exposes the first and last segment times and
// levels, so extra breakpoints live in the plugin's state tree rather than as
// automatable parameters -- same split the original has.

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
#include <algorithm>

namespace swarm {

constexpr int kMaxEnvPoints = 10;

struct EnvPoint {
    float t = 0.1f;   // duration of the segment ENDING at this point, seconds
    float l = 1.0f;   // level reached at this point, 0..1
};

struct EnvShape {
    float    ini  = 0.0f;              // level at note-on
    int      n    = 2;                 // points before sustain
    EnvPoint p[kMaxEnvPoints];
    float    relT = 0.2f;              // release time, seconds
    float    relL = 0.0f;              // level released to

    float totalPreReleaseSeconds() const
    {
        float s = 0.0f;
        for (int i = 0; i < n; ++i) s += p[i].t;
        return s;
    }
};

/** The default two-point shape the exposed parameters describe. */
inline EnvShape makeShape (float ini, float atkT, float atkL,
                           float decT, float decL, float relT, float relL)
{
    EnvShape s;
    s.ini = ini; s.n = 2;
    s.p[0] = { atkT, atkL };
    s.p[1] = { decT, decL };
    s.relT = relT; s.relL = relL;
    return s;
}

// --- serialisation, for the plugin state tree ------------------------------
// "t,l;t,l;..." with times in seconds. Empty means "use the parameters".

inline std::string serialisePoints (const EnvShape& s)
{
    std::string out;
    char buf[64];
    for (int i = 0; i < s.n; ++i)
    {
        std::snprintf (buf, sizeof (buf), "%.6g,%.6g", s.p[i].t, s.p[i].l);
        if (i) out += ';';
        out += buf;
    }
    return out;
}

inline bool deserialisePoints (const std::string& text, EnvShape& s)
{
    if (text.empty()) return false;
    std::vector<EnvPoint> pts;
    size_t i = 0;
    while (i < text.size() && (int) pts.size() < kMaxEnvPoints)
    {
        const size_t comma = text.find (',', i);
        if (comma == std::string::npos) break;
        size_t semi = text.find (';', comma);
        if (semi == std::string::npos) semi = text.size();
        EnvPoint p;
        p.t = std::max (0.0001f, (float) atof (text.substr (i, comma - i).c_str()));
        p.l = std::min (1.0f, std::max (0.0f, (float) atof (text.substr (comma + 1, semi - comma - 1).c_str())));
        pts.push_back (p);
        i = semi + 1;
    }
    if (pts.empty()) return false;
    s.n = (int) pts.size();
    for (int k = 0; k < s.n; ++k) s.p[k] = pts[(size_t) k];
    return true;
}

/** Inserts a point at the given fraction of the envelope's pre-release span,
    keeping the total duration unchanged -- what the original does when you
    double-click empty plot area. */
inline bool insertPoint (EnvShape& s, float fraction, float level)
{
    if (s.n >= kMaxEnvPoints) return false;
    const float total = s.totalPreReleaseSeconds();
    const float at = std::min (std::max (fraction, 0.0f), 1.0f) * total;

    float acc = 0.0f;
    for (int i = 0; i < s.n; ++i)
    {
        if (at < acc + s.p[i].t)
        {
            const float before = std::max (0.0001f, at - acc);
            const float after  = std::max (0.0001f, s.p[i].t - before);
            for (int k = s.n; k > i; --k) s.p[k] = s.p[k - 1];
            s.p[i]     = { before, std::min (1.0f, std::max (0.0f, level)) };
            s.p[i + 1].t = after;
            ++s.n;
            return true;
        }
        acc += s.p[i].t;
    }
    return false;
}

inline bool removePoint (EnvShape& s, int index)
{
    if (s.n <= 1 || index < 0 || index >= s.n) return false;
    // fold the removed segment's time into its neighbour so the envelope keeps
    // its overall length, as the original does
    const float t = s.p[index].t;
    for (int k = index; k < s.n - 1; ++k) s.p[k] = s.p[k + 1];
    --s.n;
    const int into = std::min (index, s.n - 1);
    s.p[into].t += t;
    return true;
}

} // namespace swarm
