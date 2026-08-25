// Headless renderer for the new engine, with the same command line as
// `vsthost32.exe --render`, so the recreation and the original can be rendered
// under identical conditions and compared.
//
//   swarmrender out.wav --note 60 --len 2 --tail 2 --param 2=0.5 ...

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include "../Source/SwarmParams.h"
#include "../Source/SwarmEngine.h"
#include "../Source/SwarmDefaults.h"
#include "../Source/SwarmAnarchy.h"
#include "../Source/SwarmPreset.h"
#include <fstream>

using namespace swarm;

static float gParam[kNumParams];

static void setDefaults()
{
    for (int i = 0; i < kNumParams; ++i) gParam[i] = defaultValue (i);
}

static SwarmSettings buildSettings()
{
    auto r = [] (int i) { return gParam[i]; };
    auto ms = [] (float v) { return envTimeMs (v) * 0.001f; };
    SwarmSettings s;
    s.numParticles = 1 + (int) (r (19) * 63.0f);
    s.home[DVol] = r (0); s.home[DPan] = r (1) * 2.0f - 1.0f;
    s.home[DRes] = r (2); s.home[DNoise] = r (3); s.home[DPitch] = 0.0f;
    s.range[DVol] = r (4); s.range[DPitch] = r (5); s.range[DPan] = r (6);
    s.range[DRes] = r (7); s.range[DNoise] = r (8);
    s.speed = r (9); s.speedLfo = r (10); s.stdDev = r (11); s.reflection = r (12);
    s.attract = r (13); s.repel = r (14); s.proximity = r (15);
    s.lowpass = r (16); s.q = r (17); s.overdrive = r (18);
    s.portamento = (1.0f + r (20) * 999.0f) * 0.001f;
    s.seed = (uint32_t) (r (21) * 134217728.0f);
    s.envRange[DPitch] = r (27) * 24.0f; s.envRange[DPan] = r (35);
    s.envRange[DRes] = r (43) * 0.5f;    s.envRange[DNoise] = r (51) * 0.5f;
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
    return s;
}

static bool writeWav (const char* path, const std::vector<float>& x, int ch, int sr)
{
    FILE* f = fopen (path, "wb");
    if (! f) return false;
    const uint32_t frames = (uint32_t) (x.size() / ch), dataBytes = frames * ch * 4;
    const uint32_t riff = 36 + dataBytes, sub1 = 16, byteRate = sr * ch * 4;
    const uint16_t fmt = 3, chn = (uint16_t) ch, bits = 32, align = (uint16_t) (ch * 4);
    fwrite ("RIFF", 1, 4, f); fwrite (&riff, 4, 1, f); fwrite ("WAVE", 1, 4, f);
    fwrite ("fmt ", 1, 4, f); fwrite (&sub1, 4, 1, f);
    fwrite (&fmt, 2, 1, f); fwrite (&chn, 2, 1, f); fwrite (&sr, 4, 1, f);
    fwrite (&byteRate, 4, 1, f); fwrite (&align, 2, 1, f); fwrite (&bits, 2, 1, f);
    fwrite ("data", 1, 4, f); fwrite (&dataBytes, 4, 1, f);
    if (! x.empty()) fwrite (x.data(), 4, x.size(), f);
    fclose (f);
    return true;
}

int main (int argc, char** argv)
{
    if (argc < 2) { printf ("usage: swarmrender out.wav [--note n] [--vel v] [--len s]"
                            " [--tail s] [--sr hz] [--param i=v]...\n"
                            "                   [--anarchy seed] [--preset file]\n"); return 1; }
    setDefaults();
    const char* out = argv[1];
    int note = 60, vel = 100, sr = 44100, block = 512;
    double len = 2.0, tail = 2.0;
    std::vector<int> notes;

    for (int i = 2; i < argc; ++i)
    {
        std::string a = argv[i];
        const bool has = (i + 1 < argc);
        if      (a == "--note"  && has) notes.push_back (atoi (argv[++i]));
        else if (a == "--vel"   && has) vel  = atoi (argv[++i]);
        else if (a == "--len"   && has) len  = atof (argv[++i]);
        else if (a == "--tail"  && has) tail = atof (argv[++i]);
        else if (a == "--sr"    && has) sr   = atoi (argv[++i]);
        else if (a == "--block" && has) block = atoi (argv[++i]);
        else if (a == "--preset" && has)
        {
            std::ifstream in (argv[++i], std::ios::binary);
            std::vector<uint8_t> bytes ((std::istreambuf_iterator<char> (in)),
                                         std::istreambuf_iterator<char>());
            const auto pr = parseFile (bytes);
            if (! pr.ok) { printf ("!! not a SwarmSynth patch: %s\n", argv[i]); return 1; }
            for (int k = 0; k < kNumParams; ++k) gParam[k] = pr.value[k];
            printf ("   patch \"%s\"\n", pr.name.c_str());
        }
        else if (a == "--anarchy" && has) anarchy (gParam, (uint32_t) strtoul (argv[++i], nullptr, 10));
        else if (a == "--param" && has)
        {
            std::string s = argv[++i];
            const auto eq = s.find ('=');
            if (eq != std::string::npos)
            {
                const int idx = atoi (s.substr (0, eq).c_str());
                if (idx >= 0 && idx < kNumParams)
                    gParam[idx] = quantise (kParams[idx].quant,
                                            (float) atof (s.substr (eq + 1).c_str()));
            }
        }
        else if (a == "--program" || a == "--no-midi") { if (a == "--program") ++i; }
        else { printf ("unknown option %s\n", a.c_str()); return 1; }
    }
    if (notes.empty()) notes.push_back (note);

    SwarmEngine engine;
    engine.prepare (sr, block);
    engine.setSettings (buildSettings());

    const int total = (int) ((len + tail) * sr);
    const int onFor = (int) (len * sr);
    std::vector<float> acc;
    acc.reserve ((size_t) total * 2);
    std::vector<float> L (block, 0.0f), R (block, 0.0f);

    engine.noteOn (notes[0], vel / 127.0f);
    bool released = false;
    for (int pos = 0; pos < total; )
    {
        const int n = std::min (block, total - pos);
        if (! released && pos + n > onFor) { engine.noteOff(); released = true; }
        std::fill (L.begin(), L.begin() + n, 0.0f);
        std::fill (R.begin(), R.begin() + n, 0.0f);
        engine.process (L.data(), R.data(), n);
        for (int i = 0; i < n; ++i) { acc.push_back (L[i]); acc.push_back (R[i]); }
        pos += n;
    }

    double peak = 0.0;
    for (float v : acc) peak = std::max (peak, (double) std::fabs (v));
    printf ("   peak %.4f (%.1f dBFS)\n", peak, peak > 0 ? 20.0 * std::log10 (peak) : -999.0);
    if (! writeWav (out, acc, 2, sr)) { printf ("!! write failed\n"); return 1; }
    printf ("   wrote %s (%d frames, %.2f s)\n", out, total, (double) total / sr);
    return 0;
}
