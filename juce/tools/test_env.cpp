// Headless checks for the breakpoint envelope model, so its behaviour is
// verified without driving a GUI.
#include <cstdio>
#include <cmath>
#include "../Source/SwarmEnvModel.h"
#include "../Source/SwarmAnarchy.h"

using namespace swarm;

static int failures = 0;
static void check (bool ok, const char* what)
{
    std::printf("  %-58s %s\n", what, ok ? "ok" : "FAIL");
    if (! ok) ++failures;
}
static bool near (float a, float b, float eps = 1e-4f) { return std::fabs (a - b) < eps; }

int main()
{
    std::printf ("envelope model\n");

    // the default two-point shape the parameters describe
    auto s = makeShape (0.0f, 0.1f, 1.0f, 0.5f, 0.5f, 0.2f, 0.0f);
    check (s.n == 2, "default shape has two points");
    check (near (s.totalPreReleaseSeconds(), 0.6f), "default pre-release span is 600 ms");

    // double-click adds a point, and the total length is unchanged
    const float before = s.totalPreReleaseSeconds();
    check (insertPoint (s, 0.5f, 0.75f), "insert at the half-way point succeeds");
    check (s.n == 3, "insert adds one point");
    check (near (s.totalPreReleaseSeconds(), before), "insert preserves total length");
    check (near (s.p[1].l, 0.75f), "inserted point takes the requested level");
    check (near (s.p[0].t + s.p[1].t, 0.3f, 1e-3f), "insert splits the segment it landed in");

    // inserting inside the first segment
    auto a = makeShape (0.0f, 0.1f, 1.0f, 0.5f, 0.5f, 0.2f, 0.0f);
    check (insertPoint (a, 0.1f, 0.4f), "insert inside the first segment succeeds");
    check (near (a.p[0].t, 0.06f, 1e-3f), "first segment is shortened to the click position");
    check (near (a.p[1].t, 0.04f, 1e-3f), "remainder carries over to the next segment");

    // right-click deletes, and the length is still preserved
    const float len = s.totalPreReleaseSeconds();
    check (removePoint (s, 1), "remove the middle point succeeds");
    check (s.n == 2, "remove drops one point");
    check (near (s.totalPreReleaseSeconds(), len), "remove preserves total length");

    // the last point cannot be removed
    auto one = makeShape (0.0f, 0.1f, 1.0f, 0.5f, 0.5f, 0.2f, 0.0f);
    removePoint (one, 1); removePoint (one, 0); removePoint (one, 0);
    check (one.n >= 1, "at least one point always remains");

    // capacity
    auto full = makeShape (0.0f, 0.1f, 1.0f, 0.5f, 0.5f, 0.2f, 0.0f);
    int added = 0;
    while (insertPoint (full, 0.5f, 0.5f)) ++added;
    check (full.n == kMaxEnvPoints, "insert stops at the capacity limit");

    // round trip through the state-tree representation
    const auto text = serialisePoints (s);
    EnvShape back = makeShape (0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.2f, 0.0f);
    check (deserialisePoints (text, back), "serialised points parse back");
    check (back.n == s.n, "round trip keeps the point count");
    bool same = true;
    for (int i = 0; i < s.n; ++i)
        same = same && near (back.p[i].t, s.p[i].t, 1e-3f) && near (back.p[i].l, s.p[i].l, 1e-3f);
    check (same, "round trip keeps every time and level");
    check (! deserialisePoints ("", back), "an empty string means 'use the parameters'");

    // ---- the Anarchy button -------------------------------------------
    std::printf ("\nanarchy\n");
    bool allInRange = true, allQuantised = true, everyParamVaries = true;
    float first[kNumParams], seen[kNumParams];
    bool  varies[kNumParams] = { false };
    anarchy (first, 1);
    for (uint32_t seed = 1; seed <= 500; ++seed)
    {
        float v[kNumParams];
        anarchy (v, seed);
        for (int i = 0; i < kNumParams; ++i)
        {
            if (! (v[i] >= 0.0f && v[i] <= 1.0f)) allInRange = false;
            if (std::fabs (v[i] - quantise (kParams[i].quant, v[i])) > 1e-6f) allQuantised = false;
            if (std::fabs (v[i] - first[i]) > 1e-6f) varies[i] = true;
        }
        (void) seen;
    }
    check (allInRange,   "every parameter stays within 0..1 over 500 seeds");
    check (allQuantised, "every value already obeys its quantiser");

    int stuck = 0;
    for (int i = 0; i < kNumParams; ++i)
        if (! varies[i]) ++stuck;
    // Speed LFO and Overdrive are deliberately zero much of the time, but
    // nothing should be frozen across all 500 seeds.
    check (stuck == 0, "no parameter is frozen across 500 seeds");
    everyParamVaries = (stuck == 0);
    (void) everyParamVaries;

    float run1[kNumParams], run2[kNumParams];
    anarchy (run1, 42); anarchy (run2, 42);
    bool deterministic = true;
    for (int i = 0; i < kNumParams; ++i) deterministic = deterministic && near (run1[i], run2[i]);
    check (deterministic, "the same seed gives the same patch");

    std::printf ("\n%s (%d failure%s)\n", failures ? "FAILED" : "all passed",
                 failures, failures == 1 ? "" : "s");
    return failures != 0;
}
