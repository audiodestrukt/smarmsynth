// vsthost32 -- a small standalone VST2 host for 32-bit Windows plugins,
// meant to be run under Wine so a legacy .dll can be played and studied.
//
//   vsthost32.exe plugin.dll                      GUI, live audio + MIDI in
//   vsthost32.exe plugin.dll --info               dump everything we can ask it
//   vsthost32.exe plugin.dll --render out.wav ... offline render, no GUI
//
// Build: see Makefile (i686-w64-mingw32-g++).

#include <windows.h>
#include <mmsystem.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <deque>

#include "vst2.h"

// ---------------------------------------------------------------------------
// globals
// ---------------------------------------------------------------------------

static AEffect*  g_effect      = NULL;
static HMODULE   g_module      = NULL;
static double    g_sampleRate  = 44100.0;
static int       g_blockSize   = 512;
static int       g_numBuffers  = 4;
static VstTimeInfo g_timeInfo;
static double    g_samplePos   = 0.0;
static CRITICAL_SECTION g_plugCs;   // old plugins are not thread safe
static CRITICAL_SECTION g_midiCs;
static bool      g_verboseMaster = false;

struct MidiMsg { unsigned char b[4]; };
static std::deque<MidiMsg> g_midiIn;

// scratch audio buffers, sized for numInputs/numOutputs x blockSize
static std::vector<float>  g_inStore, g_outStore;
static std::vector<float*> g_inPtr,   g_outPtr;

static void plugLock()   { EnterCriticalSection(&g_plugCs); }
static void plugUnlock() { LeaveCriticalSection(&g_plugCs); }

static VstIntPtr dispatch(int32_t op, int32_t index = 0, VstIntPtr value = 0,
                          void* ptr = NULL, float opt = 0.0f) {
    if (!g_effect) return 0;
    plugLock();
    VstIntPtr r = g_effect->dispatcher(g_effect, op, index, value, ptr, opt);
    plugUnlock();
    return r;
}

// Same, but for use from code that already owns the lock.
static VstIntPtr dispatchLocked(int32_t op, int32_t index = 0, VstIntPtr value = 0,
                                void* ptr = NULL, float opt = 0.0f) {
    if (!g_effect) return 0;
    return g_effect->dispatcher(g_effect, op, index, value, ptr, opt);
}

// ---------------------------------------------------------------------------
// audioMaster -- what the plugin calls back into
// ---------------------------------------------------------------------------

static HWND g_mainWnd = NULL;
static bool g_wantResize = false;
static int  g_resizeW = 0, g_resizeH = 0;

static VstIntPtr VSTCALLBACK audioMaster(AEffect* eff, int32_t opcode, int32_t index,
                                         VstIntPtr value, void* ptr, float opt) {
    (void)eff; (void)index; (void)opt;
    switch (opcode) {
    case audioMasterVersion:              return 2400;
    case audioMasterCurrentId:            return 0;
    case audioMasterIdle:                 return 0;
    case audioMasterGetSampleRate:        return (VstIntPtr)g_sampleRate;
    case audioMasterGetBlockSize:         return g_blockSize;
    case audioMasterGetInputLatency:      return 0;
    case audioMasterGetOutputLatency:     return (VstIntPtr)(g_blockSize * g_numBuffers);
    case audioMasterGetCurrentProcessLevel: return kVstProcessLevelRealtime;
    case audioMasterGetAutomationState:   return 0;   // unsupported
    case audioMasterGetLanguage:          return 1;   // English
    case audioMasterWantMidi:             return 1;
    case audioMasterGetVendorVersion:     return 1000;

    case audioMasterGetTime: {
        g_timeInfo.samplePos  = g_samplePos;
        g_timeInfo.sampleRate = g_sampleRate;
        g_timeInfo.nanoSeconds = (double)timeGetTime() * 1.0e6;
        g_timeInfo.tempo      = 120.0;
        g_timeInfo.ppqPos     = (g_samplePos / g_sampleRate) * (120.0 / 60.0);
        g_timeInfo.barStartPos = floor(g_timeInfo.ppqPos / 4.0) * 4.0;
        g_timeInfo.timeSigNumerator   = 4;
        g_timeInfo.timeSigDenominator = 4;
        g_timeInfo.flags = kVstNanosValid | kVstPpqPosValid | kVstTempoValid |
                           kVstBarsValid | kVstTimeSigValid;
        return (VstIntPtr)&g_timeInfo;
    }

    case audioMasterGetVendorString:
        strcpy((char*)ptr, "audiodestrukt"); return 1;
    case audioMasterGetProductString:
        strcpy((char*)ptr, "vsthost32");     return 1;

    case audioMasterCanDo: {
        const char* s = (const char*)ptr;
        if (!s) return 0;
        if (!strcmp(s, "sendVstEvents")      || !strcmp(s, "sendVstMidiEvent") ||
            !strcmp(s, "sendVstTimeInfo")    || !strcmp(s, "receiveVstEvents") ||
            !strcmp(s, "receiveVstMidiEvent")|| !strcmp(s, "sizeWindow")       ||
            !strcmp(s, "supplyIdle")         || !strcmp(s, "startStopProcess") ||
            !strcmp(s, "shellCategory")      || !strcmp(s, "acceptIOChanges"))
            return 1;
        if (g_verboseMaster) printf("[master] canDo? '%s' -> 0\n", s);
        return 0;
    }

    case audioMasterSizeWindow:
        g_resizeW = (int)index; g_resizeH = (int)value; g_wantResize = true;
        return 1;

    case audioMasterAutomate:
        if (g_verboseMaster) printf("[master] automate param %d = %.4f\n", (int)index, opt);
        return 0;

    case audioMasterBeginEdit:
    case audioMasterEndEdit:
    case audioMasterUpdateDisplay:
    case audioMasterNeedIdle:
    case audioMasterIOChanged:
        return 1;

    case audioMasterGetDirectory:
        return 0;

    default:
        if (g_verboseMaster)
            printf("[master] unhandled opcode %d (index %d, value %ld, opt %.3f)\n",
                   (int)opcode, (int)index, (long)value, opt);
        return 0;
    }
}

// ---------------------------------------------------------------------------
// loading
// ---------------------------------------------------------------------------

static bool loadPlugin(const char* path) {
    g_module = LoadLibraryA(path);
    if (!g_module) {
        printf("!! LoadLibrary failed for %s (error %lu)\n", path, GetLastError());
        return false;
    }
    VstPluginMainProc entry = (VstPluginMainProc)GetProcAddress(g_module, "VSTPluginMain");
    const char* used = "VSTPluginMain";
    if (!entry) { entry = (VstPluginMainProc)GetProcAddress(g_module, "main"); used = "main"; }
    if (!entry) { entry = (VstPluginMainProc)GetProcAddress(g_module, "MAIN"); used = "MAIN"; }
    if (!entry) { printf("!! no VST entry point exported\n"); return false; }
    printf("   entry point: %s\n", used);

    g_effect = entry(audioMaster);
    if (!g_effect) { printf("!! entry point returned NULL\n"); return false; }
    if (g_effect->magic != kEffectMagic) {
        printf("!! bad magic 0x%08x (expected 'VstP')\n", (unsigned)g_effect->magic);
        return false;
    }
    return true;
}

static void allocBuffers() {
    int ni = g_effect->numInputs  > 0 ? g_effect->numInputs  : 0;
    int no = g_effect->numOutputs > 0 ? g_effect->numOutputs : 0;
    g_inStore.assign((size_t)ni * g_blockSize, 0.0f);
    g_outStore.assign((size_t)no * g_blockSize, 0.0f);
    g_inPtr.resize(ni); g_outPtr.resize(no);
    for (int i = 0; i < ni; i++) g_inPtr[i]  = &g_inStore[(size_t)i * g_blockSize];
    for (int i = 0; i < no; i++) g_outPtr[i] = &g_outStore[(size_t)i * g_blockSize];
}

static void startPlugin() {
    dispatch(effOpen);
    dispatch(effSetSampleRate, 0, 0, NULL, (float)g_sampleRate);
    dispatch(effSetBlockSize, 0, g_blockSize);
    dispatch(effSetProgram, 0, 0);
    dispatch(effMainsChanged, 0, 1);
    dispatch(effStartProcess);
    allocBuffers();
}

static void stopPlugin() {
    if (!g_effect) return;
    dispatch(effStopProcess);
    dispatch(effMainsChanged, 0, 0);
    dispatch(effClose);
    g_effect = NULL;
    if (g_module) { FreeLibrary(g_module); g_module = NULL; }
}

// ---------------------------------------------------------------------------
// audio rendering core: drain queued MIDI, run one block
// ---------------------------------------------------------------------------

static void sendEvents(const std::vector<MidiMsg>& msgs) {
    if (msgs.empty()) return;
    size_t n = msgs.size();
    size_t bytes = sizeof(VstEvents) + (n > 2 ? (n - 2) * sizeof(VstEvent*) : 0);
    std::vector<char> blob(bytes, 0);
    VstEvents* ev = (VstEvents*)&blob[0];
    std::vector<VstMidiEvent> me(n);
    ev->numEvents = (int32_t)n;
    ev->reserved  = 0;
    for (size_t i = 0; i < n; i++) {
        memset(&me[i], 0, sizeof(VstMidiEvent));
        me[i].type     = kVstMidiType;
        me[i].byteSize = sizeof(VstMidiEvent);
        me[i].deltaFrames = 0;
        me[i].midiData[0] = (char)msgs[i].b[0];
        me[i].midiData[1] = (char)msgs[i].b[1];
        me[i].midiData[2] = (char)msgs[i].b[2];
        me[i].midiData[3] = 0;
        ev->events[i] = (VstEvent*)&me[i];
    }
    dispatchLocked(effProcessEvents, 0, 0, ev, 0.0f);
}

// Renders one block into g_outPtr. Caller holds the plugin lock.
static void renderBlockLocked(int frames) {
    for (size_t i = 0; i < g_inPtr.size(); i++)
        memset(g_inPtr[i], 0, sizeof(float) * frames);
    for (size_t i = 0; i < g_outPtr.size(); i++)
        memset(g_outPtr[i], 0, sizeof(float) * frames);

    float** in  = g_inPtr.empty()  ? NULL : &g_inPtr[0];
    float** out = g_outPtr.empty() ? NULL : &g_outPtr[0];

    if ((g_effect->flags & effFlagsCanReplacing) && g_effect->processReplacing)
        g_effect->processReplacing(g_effect, in, out, frames);
    else if (g_effect->process)
        g_effect->process(g_effect, in, out, frames);   // accumulating; we pre-zeroed

    g_samplePos += frames;
}

static inline short toS16(float v) {
    if (v >  1.0f) v =  1.0f;
    if (v < -1.0f) v = -1.0f;
    int s = (int)(v * 32767.0f);
    if (s >  32767) s =  32767;
    if (s < -32768) s = -32768;
    return (short)s;
}

// Interleave the plugin's first two outputs into a stereo s16 buffer.
static void interleaveS16(short* dst, int frames) {
    int no = (int)g_outPtr.size();
    const float* L = no > 0 ? g_outPtr[0] : NULL;
    const float* R = no > 1 ? g_outPtr[1] : L;
    for (int i = 0; i < frames; i++) {
        dst[i * 2 + 0] = L ? toS16(L[i]) : 0;
        dst[i * 2 + 1] = R ? toS16(R[i]) : 0;
    }
}

// ---------------------------------------------------------------------------
// live audio out (WinMM waveOut -> Wine -> PulseAudio/PipeWire)
// ---------------------------------------------------------------------------

static HWAVEOUT   g_wave = NULL;
static HANDLE     g_waveEvent = NULL;
static HANDLE     g_audioThread = NULL;
static volatile LONG g_audioRun = 0;
static std::vector<WAVEHDR> g_hdrs;
static std::vector<std::vector<short> > g_hdrData;

static DWORD WINAPI audioThreadProc(LPVOID) {
    // Round-robin over the queued buffers: whenever one comes back done,
    // refill it from the plugin and re-queue.
    while (InterlockedCompareExchange(&g_audioRun, 1, 1)) {
        bool didWork = false;
        for (size_t i = 0; i < g_hdrs.size(); i++) {
            if (!(g_hdrs[i].dwFlags & WHDR_INQUEUE)) {
                std::vector<MidiMsg> batch;
                EnterCriticalSection(&g_midiCs);
                while (!g_midiIn.empty() && batch.size() < 128) {
                    batch.push_back(g_midiIn.front());
                    g_midiIn.pop_front();
                }
                LeaveCriticalSection(&g_midiCs);

                plugLock();
                sendEvents(batch);
                renderBlockLocked(g_blockSize);
                interleaveS16(&g_hdrData[i][0], g_blockSize);
                plugUnlock();

                waveOutWrite(g_wave, &g_hdrs[i], sizeof(WAVEHDR));
                didWork = true;
            }
        }
        if (!didWork) WaitForSingleObject(g_waveEvent, 50);
    }
    return 0;
}

static bool audioStart() {
    WAVEFORMATEX wf;
    memset(&wf, 0, sizeof(wf));
    wf.wFormatTag      = WAVE_FORMAT_PCM;
    wf.nChannels       = 2;
    wf.nSamplesPerSec  = (DWORD)g_sampleRate;
    wf.wBitsPerSample  = 16;
    wf.nBlockAlign     = wf.nChannels * wf.wBitsPerSample / 8;
    wf.nAvgBytesPerSec = wf.nSamplesPerSec * wf.nBlockAlign;

    g_waveEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    MMRESULT r = waveOutOpen(&g_wave, WAVE_MAPPER, &wf, (DWORD_PTR)g_waveEvent, 0,
                             CALLBACK_EVENT);
    if (r != MMSYSERR_NOERROR) {
        printf("!! waveOutOpen failed (%u)\n", r);
        return false;
    }

    g_hdrs.resize(g_numBuffers);
    g_hdrData.resize(g_numBuffers);
    for (int i = 0; i < g_numBuffers; i++) {
        g_hdrData[i].assign((size_t)g_blockSize * 2, 0);
        memset(&g_hdrs[i], 0, sizeof(WAVEHDR));
        g_hdrs[i].lpData         = (LPSTR)&g_hdrData[i][0];
        g_hdrs[i].dwBufferLength = (DWORD)(g_blockSize * 2 * sizeof(short));
        waveOutPrepareHeader(g_wave, &g_hdrs[i], sizeof(WAVEHDR));
    }

    InterlockedExchange(&g_audioRun, 1);
    g_audioThread = CreateThread(NULL, 0, audioThreadProc, NULL, 0, NULL);
    SetThreadPriority(g_audioThread, THREAD_PRIORITY_TIME_CRITICAL);
    printf("   audio: %d Hz, %d frames x %d buffers (~%.1f ms)\n",
           (int)g_sampleRate, g_blockSize, g_numBuffers,
           1000.0 * g_blockSize * g_numBuffers / g_sampleRate);
    return true;
}

static void audioStop() {
    if (!g_wave) return;
    InterlockedExchange(&g_audioRun, 0);
    SetEvent(g_waveEvent);
    if (g_audioThread) { WaitForSingleObject(g_audioThread, 2000); CloseHandle(g_audioThread); }
    waveOutReset(g_wave);
    for (size_t i = 0; i < g_hdrs.size(); i++)
        waveOutUnprepareHeader(g_wave, &g_hdrs[i], sizeof(WAVEHDR));
    waveOutClose(g_wave);
    g_wave = NULL;
    if (g_waveEvent) CloseHandle(g_waveEvent);
}

// ---------------------------------------------------------------------------
// MIDI input (WinMM midiIn -> Wine -> ALSA sequencer)
// ---------------------------------------------------------------------------

static std::vector<HMIDIIN> g_midiHandles;

static void CALLBACK midiInProc(HMIDIIN, UINT msg, DWORD_PTR, DWORD_PTR p1, DWORD_PTR) {
    if (msg != MIM_DATA) return;
    MidiMsg m;
    m.b[0] = (unsigned char)(p1 & 0xff);
    m.b[1] = (unsigned char)((p1 >> 8) & 0xff);
    m.b[2] = (unsigned char)((p1 >> 16) & 0xff);
    m.b[3] = 0;
    EnterCriticalSection(&g_midiCs);
    if (g_midiIn.size() < 1024) g_midiIn.push_back(m);
    LeaveCriticalSection(&g_midiCs);
}

static void listMidiIn() {
    UINT n = midiInGetNumDevs();
    printf("   MIDI inputs: %u\n", n);
    for (UINT i = 0; i < n; i++) {
        MIDIINCAPSA caps;
        if (midiInGetDevCapsA(i, &caps, sizeof(caps)) == MMSYSERR_NOERROR)
            printf("     [%u] %s\n", i, caps.szPname);
    }
}

static void openMidiIn(int which) {
    UINT n = midiInGetNumDevs();
    for (UINT i = 0; i < n; i++) {
        if (which >= 0 && (int)i != which) continue;
        HMIDIIN h = NULL;
        if (midiInOpen(&h, i, (DWORD_PTR)midiInProc, 0, CALLBACK_FUNCTION) == MMSYSERR_NOERROR) {
            midiInStart(h);
            g_midiHandles.push_back(h);
            MIDIINCAPSA caps;
            if (midiInGetDevCapsA(i, &caps, sizeof(caps)) == MMSYSERR_NOERROR)
                printf("   opened MIDI in [%u] %s\n", i, caps.szPname);
        }
    }
    if (g_midiHandles.empty())
        printf("   no MIDI input opened -- use the computer keyboard\n");
}

static void closeMidiIn() {
    for (size_t i = 0; i < g_midiHandles.size(); i++) {
        midiInStop(g_midiHandles[i]);
        midiInClose(g_midiHandles[i]);
    }
    g_midiHandles.clear();
}

static void pushMidi(unsigned char a, unsigned char b, unsigned char c) {
    MidiMsg m; m.b[0] = a; m.b[1] = b; m.b[2] = c; m.b[3] = 0;
    EnterCriticalSection(&g_midiCs);
    g_midiIn.push_back(m);
    LeaveCriticalSection(&g_midiCs);
}

// ---------------------------------------------------------------------------
// introspection
// ---------------------------------------------------------------------------

static std::string strDispatch(int32_t op, int32_t index = 0) {
    char buf[512];
    memset(buf, 0, sizeof(buf));
    dispatch(op, index, 0, buf, 0.0f);
    buf[sizeof(buf) - 1] = 0;
    return std::string(buf);
}

static const char* kCanDos[] = {
    "sendVstEvents", "sendVstMidiEvent", "receiveVstEvents", "receiveVstMidiEvent",
    "receiveVstTimeInfo", "offline", "midiProgramNames", "bypass",
    "midiSingleNoteTuningChange", "2in2out", "plugAsChannelInsert", "plugAsSend", NULL
};

static void dumpInfo() {
    printf("\n== plugin ==\n");
    printf("  name          : %s\n", strDispatch(effGetEffectName).c_str());
    printf("  vendor        : %s\n", strDispatch(effGetVendorString).c_str());
    printf("  product       : %s\n", strDispatch(effGetProductString).c_str());
    printf("  vendor version: %ld\n", (long)dispatch(effGetVendorVersion));
    printf("  VST version   : %ld\n", (long)dispatch(effGetVstVersion));
    printf("  uniqueID      : 0x%08x ('%c%c%c%c')\n", (unsigned)g_effect->uniqueID,
           (char)((g_effect->uniqueID >> 24) & 0xff), (char)((g_effect->uniqueID >> 16) & 0xff),
           (char)((g_effect->uniqueID >> 8) & 0xff),  (char)(g_effect->uniqueID & 0xff));
    printf("  version       : %d\n", (int)g_effect->version);
    printf("  audio io      : %d in / %d out\n", (int)g_effect->numInputs, (int)g_effect->numOutputs);
    printf("  programs      : %d\n", (int)g_effect->numPrograms);
    printf("  parameters    : %d\n", (int)g_effect->numParams);
    printf("  initial delay : %d\n", (int)g_effect->initialDelay);
    printf("  flags         : 0x%04x%s%s%s%s%s\n", (unsigned)g_effect->flags,
           (g_effect->flags & effFlagsHasEditor)     ? " hasEditor"     : "",
           (g_effect->flags & effFlagsCanReplacing)  ? " canReplacing"  : "",
           (g_effect->flags & effFlagsProgramChunks) ? " programChunks" : "",
           (g_effect->flags & effFlagsIsSynth)       ? " isSynth"       : "",
           (g_effect->flags & effFlagsNoSoundInStop) ? " noSoundInStop" : "");

    printf("\n== canDo ==\n");
    for (int i = 0; kCanDos[i]; i++)
        printf("  %-28s %ld\n", kCanDos[i], (long)dispatch(effCanDo, 0, 0, (void*)kCanDos[i]));

    if (g_effect->flags & effFlagsHasEditor) {
        ERect* r = NULL;
        dispatch(effEditGetRect, 0, 0, &r);
        if (r) printf("\n== editor ==\n  %d x %d\n", r->right - r->left, r->bottom - r->top);
    }

    printf("\n== programs (%d) ==\n", (int)g_effect->numPrograms);
    for (int i = 0; i < g_effect->numPrograms; i++) {
        char nm[256]; memset(nm, 0, sizeof(nm));
        if (!dispatch(effGetProgramNameIndexed, i, 0, nm)) {
            // fall back: switch to it and ask
            dispatch(effSetProgram, 0, i);
            dispatch(effGetProgramName, 0, 0, nm);
        }
        nm[255] = 0;
        printf("  [%3d] %s\n", i, nm);
    }
    dispatch(effSetProgram, 0, 0);

    printf("\n== parameters (%d) ==\n", (int)g_effect->numParams);
    printf("  %-4s %-28s %-12s %-10s %s\n", "idx", "name", "display", "label", "value");
    for (int i = 0; i < g_effect->numParams; i++) {
        char nm[256], dv[256], lb[256];
        memset(nm, 0, sizeof(nm)); memset(dv, 0, sizeof(dv)); memset(lb, 0, sizeof(lb));
        dispatch(effGetParamName, i, 0, nm);
        dispatch(effGetParamDisplay, i, 0, dv);
        dispatch(effGetParamLabel, i, 0, lb);
        nm[63] = dv[63] = lb[63] = 0;
        plugLock();
        float v = g_effect->getParameter(g_effect, i);
        plugUnlock();
        printf("  %-4d %-28s %-12s %-10s %.6f\n", i, nm, dv, lb, v);
    }
    printf("\n");
}

// Sweep every parameter across its 0..1 range and print what the plugin says
// each value means. This recovers the exact value->engineering-unit curve for
// all params at once, which is what a re-implementation has to match.
static void mapParams(int steps) {
    printf("param_index,param_name,unit,norm_value,readback,display\n");
    for (int i = 0; i < g_effect->numParams; i++) {
        char nm[256], lb[256];
        memset(nm, 0, sizeof(nm)); memset(lb, 0, sizeof(lb));
        dispatch(effGetParamName, i, 0, nm);
        dispatch(effGetParamLabel, i, 0, lb);
        nm[63] = lb[63] = 0;
        plugLock();
        float saved = g_effect->getParameter(g_effect, i);
        plugUnlock();
        for (int s = 0; s < steps; s++) {
            float v = (steps > 1) ? (float)s / (float)(steps - 1) : 0.0f;
            char dv[256]; memset(dv, 0, sizeof(dv));
            plugLock();
            g_effect->setParameter(g_effect, i, v);
            float rb = g_effect->getParameter(g_effect, i);
            plugUnlock();
            dispatch(effGetParamDisplay, i, 0, dv);
            dv[63] = 0;
            printf("%d,\"%s\",\"%s\",%.6f,%.6f,\"%s\"\n", i, nm, lb, v, rb, dv);
        }
        plugLock();
        g_effect->setParameter(g_effect, i, saved);
        plugUnlock();
    }
}

static void dumpChunk(const char* path, bool isPreset) {
    void* data = NULL;
    VstIntPtr n = dispatch(effGetChunk, isPreset ? 1 : 0, 0, &data);
    if (n <= 0 || !data) { printf("!! effGetChunk returned %ld\n", (long)n); return; }
    FILE* f = fopen(path, "wb");
    if (!f) { printf("!! cannot write %s\n", path); return; }
    fwrite(data, 1, (size_t)n, f);
    fclose(f);
    printf("   wrote %s (%ld bytes, %s)\n", path, (long)n, isPreset ? "program" : "bank");
}

// ---------------------------------------------------------------------------
// preset chunks / .fxp
// ---------------------------------------------------------------------------

static uint32_t be32(const unsigned char* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

// Loads a raw state chunk (what --dump-chunk writes) straight into the plugin.
static bool loadRawChunk(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) { printf("!! cannot open %s\n", path); return false; }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    std::vector<unsigned char> d((size_t)sz);
    if (sz <= 0 || fread(&d[0], 1, (size_t)sz, f) != (size_t)sz) { fclose(f); return false; }
    fclose(f);
    dispatch(effSetChunk, 1 /*isPreset*/, (VstIntPtr)sz, &d[0]);
    printf("   loaded chunk %s (%ld bytes)\n", path, sz);
    return true;
}

static bool loadFxp(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) { printf("!! cannot open %s\n", path); return false; }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    std::vector<unsigned char> d((size_t)sz);
    if (fread(&d[0], 1, (size_t)sz, f) != (size_t)sz) { fclose(f); return false; }
    fclose(f);
    if (sz < 60 || memcmp(&d[0], "CcnK", 4) != 0) { printf("!! not an fxp/fxb\n"); return false; }

    const char* kind = (const char*)&d[8];   // FxCk / FPCh / FxBk / FBCh
    int numParams = (int)be32(&d[54 - 0]);   // offset 54? -- see layout below
    // fxProgram layout: chunkMagic(0) byteSize(4) fxMagic(8) version(12) fxID(16)
    //                   fxVersion(20) numParams(24) prgName[28..55] params(56..)
    numParams = (int)be32(&d[24]);

    if (!memcmp(kind, "FxCk", 4)) {
        printf("   fxp: single program, %d params\n", numParams);
        for (int i = 0; i < numParams && (size_t)(56 + i * 4 + 4) <= d.size(); i++) {
            uint32_t bits = be32(&d[56 + i * 4]);
            float v; memcpy(&v, &bits, 4);
            plugLock(); g_effect->setParameter(g_effect, i, v); plugUnlock();
        }
        return true;
    }
    if (!memcmp(kind, "FPCh", 4)) {
        // fxProgram (opaque): ... numParams(24) prgName[28..55] chunkSize(56) chunk(60..)
        uint32_t chunkSize = be32(&d[56]);
        if (60 + chunkSize > d.size()) { printf("!! truncated FPCh\n"); return false; }
        printf("   fxp: opaque chunk, %u bytes\n", (unsigned)chunkSize);
        dispatch(effSetChunk, 1 /*isPreset*/, chunkSize, &d[60]);
        return true;
    }
    if (!memcmp(kind, "FBCh", 4)) {
        uint32_t chunkSize = be32(&d[152]);
        if (156 + chunkSize > d.size()) { printf("!! truncated FBCh\n"); return false; }
        printf("   fxb: opaque bank chunk, %u bytes\n", (unsigned)chunkSize);
        dispatch(effSetChunk, 0 /*bank*/, chunkSize, &d[156]);
        return true;
    }
    printf("!! unsupported fx magic '%.4s'\n", kind);
    return false;
}

// ---------------------------------------------------------------------------
// WAV writer
// ---------------------------------------------------------------------------

static bool writeWav(const char* path, const std::vector<float>& interleaved,
                     int channels, int sr) {
    FILE* f = fopen(path, "wb");
    if (!f) { printf("!! cannot write %s\n", path); return false; }
    uint32_t frames = (uint32_t)(interleaved.size() / channels);
    uint32_t dataBytes = frames * channels * 4;
    uint16_t fmt = 3;      // IEEE float
    uint16_t ch = (uint16_t)channels, bits = 32;
    uint32_t byteRate = sr * channels * 4;
    uint16_t blockAlign = (uint16_t)(channels * 4);
    uint32_t riffSize = 36 + dataBytes;
    uint32_t sub1 = 16;

    fwrite("RIFF", 1, 4, f); fwrite(&riffSize, 4, 1, f); fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f); fwrite(&sub1, 4, 1, f);
    fwrite(&fmt, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&sr, 4, 1, f);
    fwrite(&byteRate, 4, 1, f); fwrite(&blockAlign, 2, 1, f); fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&dataBytes, 4, 1, f);
    if (!interleaved.empty()) fwrite(&interleaved[0], 4, interleaved.size(), f);
    fclose(f);
    printf("   wrote %s (%u frames, %.2f s)\n", path, frames, (double)frames / sr);
    return true;
}

// ---------------------------------------------------------------------------
// offline render
// ---------------------------------------------------------------------------

struct RenderOpts {
    std::string out;
    std::vector<int> notes;
    int   velocity;
    double noteLen;
    double tail;
    RenderOpts() : velocity(100), noteLen(2.0), tail(2.0) {}
};

static int renderToFile(const RenderOpts& o) {
    double total = o.noteLen + o.tail;
    int totalFrames = (int)(total * g_sampleRate);
    int onFrames    = (int)(o.noteLen * g_sampleRate);
    int channels    = g_effect->numOutputs >= 2 ? 2 : 1;
    std::vector<float> acc;
    acc.reserve((size_t)totalFrames * channels);

    bool sentOn = false, sentOff = false;
    int pos = 0;
    while (pos < totalFrames) {
        int frames = g_blockSize;
        if (pos + frames > totalFrames) frames = totalFrames - pos;

        std::vector<MidiMsg> batch;
        if (!sentOn) {
            for (size_t i = 0; i < o.notes.size(); i++) {
                MidiMsg m; m.b[0] = 0x90; m.b[1] = (unsigned char)o.notes[i];
                m.b[2] = (unsigned char)o.velocity; m.b[3] = 0;
                batch.push_back(m);
            }
            sentOn = true;
        }
        if (!sentOff && pos + frames > onFrames) {
            for (size_t i = 0; i < o.notes.size(); i++) {
                MidiMsg m; m.b[0] = 0x80; m.b[1] = (unsigned char)o.notes[i];
                m.b[2] = 0; m.b[3] = 0;
                batch.push_back(m);
            }
            sentOff = true;
        }

        plugLock();
        sendEvents(batch);
        renderBlockLocked(frames);
        const float* L = g_outPtr.size() > 0 ? g_outPtr[0] : NULL;
        const float* R = g_outPtr.size() > 1 ? g_outPtr[1] : L;
        for (int i = 0; i < frames; i++) {
            acc.push_back(L ? L[i] : 0.0f);
            if (channels == 2) acc.push_back(R ? R[i] : 0.0f);
        }
        plugUnlock();
        pos += frames;
    }

    double peak = 0.0;
    for (size_t i = 0; i < acc.size(); i++) { double a = fabs(acc[i]); if (a > peak) peak = a; }
    printf("   peak %.4f (%.1f dBFS)\n", peak, peak > 0 ? 20.0 * log10(peak) : -999.0);
    if (peak < 1e-6) printf("   ** silence -- try a different program or check the note range\n");

    return writeWav(o.out.c_str(), acc, channels, (int)g_sampleRate) ? 0 : 1;
}

// ---------------------------------------------------------------------------
// GUI scripting
//
// Driving the plugin's editor through X (xdotool -> Wine -> Win32) turned out
// to be unreliable: some clicks land, some do not. Posting the mouse messages
// straight to the child window under the point is deterministic, and lets a
// script be written in the editor's own pixel coordinates.
//
//   --gui-script "click:200,290; drag:120,300,120,260; wait:400; dump"
// ---------------------------------------------------------------------------

static char   g_dumpPrefix[256] = "";
static int    g_dumpSeq = 0;
static bool guiScriptStep();
static bool g_guiQuitAtEnd = false;
static std::vector<std::string> g_guiSteps;
static size_t g_guiPos = 0;

// PostMessage, never SendMessage: a click that opens a menu would otherwise
// block inside Win32's modal menu loop and never return. The script runs on its
// own thread and sleeps between steps, so events are still properly spaced.
static void postMouse(int x, int y, UINT msg, WPARAM extraButtons) {
    POINT pt = { x, y };
    ClientToScreen(g_mainWnd, &pt);
    HWND target = WindowFromPoint(pt);
    if (!target) target = g_mainWnd;
    POINT local = pt;
    ScreenToClient(target, &local);
    PostMessage(target, msg, extraButtons, MAKELPARAM(local.x, local.y));
}

static void guiClick(int x, int y, bool right) {
    postMouse(x, y, WM_MOUSEMOVE, 0);
    postMouse(x, y, right ? WM_RBUTTONDOWN : WM_LBUTTONDOWN, right ? MK_RBUTTON : MK_LBUTTON);
    postMouse(x, y, right ? WM_RBUTTONUP   : WM_LBUTTONUP,   0);
}

// Asynchronous click, for anything that opens a menu: a menu bar runs its own
// modal message loop, so a synchronous SendMessage into one never returns.
static void guiPostClick(int x, int y) {
    POINT pt = { x, y };
    ClientToScreen(g_mainWnd, &pt);
    HWND target = WindowFromPoint(pt);
    if (!target) target = g_mainWnd;
    POINT local = pt;
    ScreenToClient(target, &local);
    const LPARAM lp = MAKELPARAM(local.x, local.y);
    PostMessage(target, WM_MOUSEMOVE,   0,          lp);
    PostMessage(target, WM_LBUTTONDOWN, MK_LBUTTON, lp);
    PostMessage(target, WM_LBUTTONUP,   0,          lp);
}

static void guiDrag(int x0, int y0, int x1, int y1) {
    postMouse(x0, y0, WM_MOUSEMOVE, 0);
    postMouse(x0, y0, WM_LBUTTONDOWN, MK_LBUTTON);
    const int steps = 12;
    for (int i = 1; i <= steps; i++)
        postMouse(x0 + (x1 - x0) * i / steps, y0 + (y1 - y0) * i / steps,
                  WM_MOUSEMOVE, MK_LBUTTON);
    postMouse(x1, y1, WM_LBUTTONUP, 0);
}

// A popup menu is a top-level window of class "#32768". Asking it for its
// HMENU (MN_GETHMENU) lets the host read the item text directly, which beats
// trying to screenshot a window the compositor will not hand over.
#ifndef MN_GETHMENU
#define MN_GETHMENU 0x01E1
#endif

static void printMenu(HMENU m, int depth) {
    const int n = GetMenuItemCount(m);
    for (int i = 0; i < n; i++) {
        char buf[256] = {0};
        GetMenuStringA(m, i, buf, sizeof(buf)-1, MF_BYPOSITION);
        const UINT st = GetMenuState(m, i, MF_BYPOSITION);
        printf("   %*s[%2d] %-34s%s%s%s\n", depth * 3, "", i,
               buf[0] ? buf : "(separator)",
               (st & MF_CHECKED) ? " [checked]" : "",
               (st & MF_GRAYED)  ? " [greyed]"  : "",
               (st & MF_POPUP)   ? " [submenu]" : "");
        if (st & MF_POPUP) {
            if (HMENU sub = GetSubMenu(m, i)) printMenu(sub, depth + 1);
        }
    }
}

static void dumpOpenMenu() {
    HWND h = FindWindowA("#32768", NULL);
    if (!h) { printf("   no popup menu open\n"); return; }
    RECT r; GetWindowRect(h, &r);
    HMENU m = (HMENU)SendMessage(h, MN_GETHMENU, 0, 0);
    if (!m) { printf("   menu window found but no HMENU\n"); return; }
    printf("   menu at screen %ld,%ld %ldx%ld:\n",
           (long)r.left, (long)r.top, (long)(r.right-r.left), (long)(r.bottom-r.top));
    printMenu(m, 0);
}

// Clicks a menu item at the position Win32 says it occupies. More reliable
// than keyboard navigation, which the plugin's menu does not seem to follow.
static void clickMenuItem(int index) {
    HWND h = FindWindowA("#32768", NULL);
    if (!h) { printf("   no popup menu open\n"); return; }
    HMENU m = (HMENU)SendMessage(h, MN_GETHMENU, 0, 0);
    if (!m) { printf("   no HMENU\n"); return; }
    RECT ir;
    if (!GetMenuItemRect(h, m, (UINT)index, &ir)) {
        printf("   GetMenuItemRect failed for item %d\n", index);
        return;
    }
    const int cx = (ir.left + ir.right) / 2, cy = (ir.top + ir.bottom) / 2;
    POINT pt = { cx, cy };
    HWND target = WindowFromPoint(pt);
    if (!target) target = h;
    POINT local = pt; ScreenToClient(target, &local);
    const LPARAM lp = MAKELPARAM(local.x, local.y);
    printf("   clicking menu item %d at screen %d,%d\n", index, cx, cy);
    PostMessage(target, WM_MOUSEMOVE,   0,          lp);
    PostMessage(target, WM_LBUTTONDOWN, MK_LBUTTON, lp);
    Sleep(60);
    PostMessage(target, WM_LBUTTONUP,   0,          lp);
}

// Selects an item by keyboard, which is how a menu wants to be driven.
static void selectMenuItem(int index) {
    HWND h = FindWindowA("#32768", NULL);
    if (!h) { printf("   no popup menu open\n"); return; }
    for (int i = 0; i <= index; i++) {
        PostMessage(h, WM_KEYDOWN, VK_DOWN, 0);
        PostMessage(h, WM_KEYUP,   VK_DOWN, 0);
        Sleep(40);
    }
    Sleep(120);
    PostMessage(h, WM_KEYDOWN, VK_RETURN, 0);
    PostMessage(h, WM_KEYUP,   VK_RETURN, 0);
}

static void dumpLiveChunk() {
    char path[512];
    sprintf(path, "%s%d.chunk", g_dumpPrefix[0] ? g_dumpPrefix : "live", g_dumpSeq++);
    void* data = NULL;
    VstIntPtr n = dispatch(effGetChunk, 1, 0, &data);
    if (n > 0 && data) {
        FILE* f = fopen(path, "wb");
        if (f) { fwrite(data, 1, (size_t)n, f); fclose(f);
                 printf("   dumped %s (%ld bytes)\n", path, (long)n); }
    } else printf("   effGetChunk returned %ld\n", (long)n);
}

static DWORD WINAPI guiScriptThread(LPVOID) {
    Sleep(900);                       // let the editor settle and repaint
    while (guiScriptStep()) Sleep(350);
    if (g_guiQuitAtEnd) PostMessage(g_mainWnd, WM_CLOSE, 0, 0);
    return 0;
}

// Runs one step; called from the script thread.
static bool guiScriptStep() {
    if (g_guiPos >= g_guiSteps.size()) return false;
    std::string s = g_guiSteps[g_guiPos++];
    // log before acting: a step that opens a menu will not return until the
    // menu closes, and we still want to know which step we are on
    printf("   gui step %d/%d: %s\n", (int)g_guiPos, (int)g_guiSteps.size(), s.c_str());
    int a, b, c, d;
    if      (sscanf(s.c_str(), "press:%d,%d", &a, &b) == 2) {
        postMouse(a, b, WM_MOUSEMOVE, 0);
        postMouse(a, b, WM_LBUTTONDOWN, MK_LBUTTON);
    }
    else if (sscanf(s.c_str(), "release:%d,%d", &a, &b) == 2) {
        postMouse(a, b, WM_MOUSEMOVE, MK_LBUTTON);
        postMouse(a, b, WM_LBUTTONUP, 0);
    }
    else if (sscanf(s.c_str(), "move:%d,%d", &a, &b) == 2)           postMouse(a, b, WM_MOUSEMOVE, 0);
    else if (sscanf(s.c_str(), "pclick:%d,%d", &a, &b) == 2)         guiPostClick(a, b);
    else if (sscanf(s.c_str(), "click:%d,%d", &a, &b) == 2)          guiClick(a, b, false);
    else if (sscanf(s.c_str(), "rclick:%d,%d", &a, &b) == 2)         guiClick(a, b, true);
    else if (sscanf(s.c_str(), "dclick:%d,%d", &a, &b) == 2) {
        // two clicks inside the system double-click time, with a real gap
        guiClick(a, b, false); Sleep(60); guiClick(a, b, false);
    }
    else if (sscanf(s.c_str(), "drag:%d,%d,%d,%d", &a, &b, &c, &d) == 4) guiDrag(a, b, c, d);
    else if (s == "dump")                                            dumpLiveChunk();
    else if (s == "menudump")                                        dumpOpenMenu();
    else if (sscanf(s.c_str(), "menuitem:%d", &a) == 1)              selectMenuItem(a);
    else if (sscanf(s.c_str(), "menuclick:%d", &a) == 1)             clickMenuItem(a);
    else if (s.rfind("wait", 0) == 0)                                { /* a tick is the wait */ }
    else printf("   ?? gui step '%s'\n", s.c_str());
    return true;
}

static void parseGuiScript(const char* text) {
    std::string cur;
    for (const char* p = text; ; p++) {
        if (*p == ';' || *p == 0) {
            std::string t;
            for (char ch : cur) if (!isspace((unsigned char)ch)) t += ch;
            if (!t.empty()) g_guiSteps.push_back(t);
            cur.clear();
            if (*p == 0) break;
        } else cur += *p;
    }
}

// ---------------------------------------------------------------------------
// GUI
// ---------------------------------------------------------------------------

static bool g_pokePending = false;
static bool g_guiThreadStarted = false;
static int  g_pokeTicks   = 0;
static int  g_octave = 4;
static bool g_keyHeld[256] = { false };

// tracker-style layout: lower row = C..B, upper row = one octave up
static int keyToNote(WPARAM vk) {
    switch (vk) {
    case 'Z': return 0;  case 'S': return 1;  case 'X': return 2;  case 'D': return 3;
    case 'C': return 4;  case 'V': return 5;  case 'G': return 6;  case 'B': return 7;
    case 'H': return 8;  case 'N': return 9;  case 'J': return 10; case 'M': return 11;
    case 'Q': return 12; case '2': return 13; case 'W': return 14; case '3': return 15;
    case 'E': return 16; case 'R': return 17; case '5': return 18; case 'T': return 19;
    case '6': return 20; case 'Y': return 21; case '7': return 22; case 'U': return 23;
    default:  return -1;
    }
}

static void allNotesOff() {
    for (int ch = 0; ch < 16; ch++) pushMidi((unsigned char)(0xB0 | ch), 123, 0);
    for (int n = 0; n < 128; n++) pushMidi(0x80, (unsigned char)n, 0);
}

static LRESULT CALLBACK wndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_KEYDOWN: {
        if (lp & (1 << 30)) return 0;            // ignore auto-repeat
        if (wp == VK_ESCAPE) { PostMessage(h, WM_CLOSE, 0, 0); return 0; }
        if (wp == VK_UP)   { if (g_octave < 8) g_octave++; printf("   octave %d\n", g_octave); return 0; }
        if (wp == VK_DOWN) { if (g_octave > 0) g_octave--; printf("   octave %d\n", g_octave); return 0; }
        if (wp == VK_SPACE) { allNotesOff(); printf("   all notes off\n"); return 0; }
        if (wp == VK_F2) { dumpLiveChunk(); return 0; }
        int rel = keyToNote(wp);
        if (rel >= 0) {
            int note = g_octave * 12 + rel;
            if (note >= 0 && note < 128 && !g_keyHeld[wp & 0xff]) {
                g_keyHeld[wp & 0xff] = true;
                pushMidi(0x90, (unsigned char)note, 100);
            }
        }
        return 0;
    }
    case WM_KEYUP: {
        int rel = keyToNote(wp);
        if (rel >= 0 && g_keyHeld[wp & 0xff]) {
            g_keyHeld[wp & 0xff] = false;
            int note = g_octave * 12 + rel;
            if (note >= 0 && note < 128) pushMidi(0x80, (unsigned char)note, 0);
        }
        return 0;
    }
    case WM_CLOSE:
        DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(h, msg, wp, lp);
}

static void sizeToClient(HWND h, int w, int h_) {
    RECT r = { 0, 0, w, h_ };
    AdjustWindowRect(&r, (DWORD)GetWindowLongPtr(h, GWL_STYLE), FALSE);
    SetWindowPos(h, NULL, 0, 0, r.right - r.left, r.bottom - r.top,
                 SWP_NOMOVE | SWP_NOZORDER);
}

static int runGui(const char* title) {
    WNDCLASSA wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc   = wndProc;
    wc.hInstance     = GetModuleHandle(NULL);
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = "vsthost32";
    RegisterClassA(&wc);

    int w = 640, h = 400;
    g_mainWnd = CreateWindowA("vsthost32", title,
                              WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                              CW_USEDEFAULT, CW_USEDEFAULT, w, h,
                              NULL, NULL, wc.hInstance, NULL);
    if (!g_mainWnd) { printf("!! CreateWindow failed\n"); return 1; }

    bool haveEditor = (g_effect->flags & effFlagsHasEditor) != 0;
    if (haveEditor) {
        dispatch(effEditOpen, 0, 0, (void*)g_mainWnd);
        ERect* r = NULL;
        dispatch(effEditGetRect, 0, 0, &r);
        if (r && r->right > r->left && r->bottom > r->top) {
            w = r->right - r->left;
            h = r->bottom - r->top;
            sizeToClient(g_mainWnd, w, h);
            printf("   editor %d x %d\n", w, h);
        }
    } else {
        printf("   plugin has no editor -- window is here just for keyboard input\n");
    }

    ShowWindow(g_mainWnd, SW_SHOW);
    SetForegroundWindow(g_mainWnd);
    SetFocus(g_mainWnd);
    SetTimer(g_mainWnd, 1, 25, NULL);

    // Some plugins only repaint panels that have been marked dirty, so on a
    // fresh open those areas stay blank until you touch a control. Rewriting
    // every parameter with the value it already has is exactly the nudge a
    // knob move gives them, and it costs nothing.
    g_pokePending = haveEditor;

    printf("\n   keys: z s x d c v g b h n j m = C..B, q 2 w 3 e r 5 t 6 y 7 u = +1 oct\n");
    printf("         up/down = octave, space = all notes off, esc = quit\n");
    printf("         (click the host window's title bar if the plugin GUI has focus)\n\n");

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0) > 0) {
        if (msg.message == WM_TIMER && msg.hwnd == g_mainWnd) {
            if (haveEditor) dispatch(effEditIdle);
            if (g_pokePending && ++g_pokeTicks > 8) {
                g_pokePending = false;
                plugLock();
                for (int i = 0; i < g_effect->numParams; i++)
                    g_effect->setParameter(g_effect, i, g_effect->getParameter(g_effect, i));
                plugUnlock();
                RedrawWindow(g_mainWnd, NULL, NULL,
                             RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
                dispatch(effEditIdle);
            }
            if (!g_pokePending && !g_guiSteps.empty() && !g_guiThreadStarted) {
                g_guiThreadStarted = true;
                CloseHandle(CreateThread(NULL, 0, guiScriptThread, NULL, 0, NULL));
            }
            dispatch(53 /*effIdle, deprecated but some old plugins want it*/);
            if (g_wantResize) {
                g_wantResize = false;
                sizeToClient(g_mainWnd, g_resizeW, g_resizeH);
            }
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    if (haveEditor) dispatch(effEditClose);
    return 0;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

static void usage() {
    printf(
    "usage: vsthost32 <plugin.dll> [options]\n"
    "\n"
    "  --info                 print id, flags, programs and all parameters, then exit\n"
    "  --list-midi            list MIDI input devices and exit\n"
    "  --map-params <steps>   sweep every param 0..1 and print a CSV of what\n"
    "                         each normalised value displays as, then exit\n"
    "  --dump-chunk <file>    write the plugin's opaque program chunk to a file\n"
    "  --dump-prefix <p>      in the GUI, F2 dumps the live chunk to <p><n>.chunk\n"
    "  --gui-script <steps>   drive the editor: semicolon-separated\n"
    "                         click:x,y pclick:x,y rclick:x,y dclick:x,y\n"
    "                         press:x,y release:x,y move:x,y\n"
    "                         menudump menuitem:n -- read/drive a popup menu\n"
    "                         drag:x0,y0,x1,y1 -- pclick is async, for menus\n"
    "                         wait dump -- coordinates are editor pixels\n"
    "  --gui-quit             close once the script finishes\n"
    "  --dump-bank <file>     write the plugin's opaque bank chunk to a file\n"
    "  --midi <n>             open only MIDI input device n (default: all)\n"
    "  --no-midi              do not open any MIDI input\n"
    "  --program <n>          select program n\n"
    "  --fxp <file>           load an .fxp/.fxb preset before playing/rendering\n"
    "  --chunk <file>         load a raw state chunk (as --dump-chunk writes)\n"
    "  --param <i>=<v>        set parameter i to v (0..1), repeatable\n"
    "  --sr <hz>              sample rate (default 44100)\n"
    "  --block <n>            block size in frames (default 512)\n"
    "  --buffers <n>          number of output buffers (default 4)\n"
    "  --render <out.wav>     offline render to 32-bit float wav, no GUI\n"
    "    --note <n>           note to play, repeatable for chords (default 60)\n"
    "    --vel <v>            velocity (default 100)\n"
    "    --len <sec>          how long the note is held (default 2)\n"
    "    --tail <sec>         extra time rendered after note off (default 2)\n"
    "  --verbose              log unhandled audioMaster calls\n");
}

int main(int argc, char** argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc < 2) { usage(); return 1; }

    const char* dllPath = argv[1];
    bool doInfo = false, doListMidi = false, noMidi = false;
    int  midiDev = -1, program = -1;
    const char* fxp = NULL;
    const char* rawChunk = NULL;
    const char* chunkOut = NULL; bool chunkIsPreset = true;
    int mapSteps = 0;
    RenderOpts ro;
    bool doRender = false;
    std::vector<std::pair<int, float> > paramSets;

    for (int i = 2; i < argc; i++) {
        std::string a = argv[i];
        const char* nx = (i + 1 < argc) ? argv[i + 1] : NULL;
        if      (a == "--info")      doInfo = true;
        else if (a == "--list-midi") doListMidi = true;
        else if (a == "--no-midi")   noMidi = true;
        else if (a == "--verbose")   g_verboseMaster = true;
        else if (a == "--midi"    && nx) midiDev = atoi(argv[++i]);
        else if (a == "--map-params" && nx) mapSteps = atoi(argv[++i]);
        else if (a == "--dump-chunk" && nx) { chunkOut = argv[++i]; chunkIsPreset = true; }
        else if (a == "--dump-prefix" && nx) { strncpy(g_dumpPrefix, argv[++i], sizeof(g_dumpPrefix)-1); }
        else if (a == "--gui-script"  && nx) { parseGuiScript(argv[++i]); }
        else if (a == "--gui-quit")          { g_guiQuitAtEnd = true; }
        else if (a == "--dump-bank"  && nx) { chunkOut = argv[++i]; chunkIsPreset = false; }
        else if (a == "--program" && nx) program = atoi(argv[++i]);
        else if (a == "--fxp"     && nx) fxp = argv[++i];
        else if (a == "--chunk"   && nx) rawChunk = argv[++i];
        else if (a == "--sr"      && nx) g_sampleRate = atof(argv[++i]);
        else if (a == "--block"   && nx) g_blockSize = atoi(argv[++i]);
        else if (a == "--buffers" && nx) g_numBuffers = atoi(argv[++i]);
        else if (a == "--render"  && nx) { doRender = true; ro.out = argv[++i]; }
        else if (a == "--note"    && nx) ro.notes.push_back(atoi(argv[++i]));
        else if (a == "--vel"     && nx) ro.velocity = atoi(argv[++i]);
        else if (a == "--len"     && nx) ro.noteLen = atof(argv[++i]);
        else if (a == "--tail"    && nx) ro.tail = atof(argv[++i]);
        else if (a == "--param"   && nx) {
            std::string s = argv[++i];
            size_t eq = s.find('=');
            if (eq != std::string::npos)
                paramSets.push_back(std::make_pair(atoi(s.substr(0, eq).c_str()),
                                                   (float)atof(s.substr(eq + 1).c_str())));
        }
        else if (a == "--help" || a == "-h") { usage(); return 0; }
        else { printf("!! unknown option %s\n", a.c_str()); usage(); return 1; }
    }
    if (ro.notes.empty()) ro.notes.push_back(60);

    InitializeCriticalSection(&g_plugCs);
    InitializeCriticalSection(&g_midiCs);
    memset(&g_timeInfo, 0, sizeof(g_timeInfo));

    if (doListMidi) { listMidiIn(); return 0; }

    printf("\n== loading %s ==\n", dllPath);
    if (!loadPlugin(dllPath)) return 1;
    printf("   AEffect ok: %d params, %d programs, %d in / %d out\n",
           (int)g_effect->numParams, (int)g_effect->numPrograms,
           (int)g_effect->numInputs, (int)g_effect->numOutputs);

    startPlugin();

    if (fxp) loadFxp(fxp);
    if (rawChunk) loadRawChunk(rawChunk);
    if (program >= 0) { dispatch(effSetProgram, 0, program); printf("   program %d\n", program); }
    for (size_t i = 0; i < paramSets.size(); i++) {
        plugLock();
        g_effect->setParameter(g_effect, paramSets[i].first, paramSets[i].second);
        plugUnlock();
        printf("   param %d = %.4f\n", paramSets[i].first, paramSets[i].second);
    }

    int rc = 0;
    if (mapSteps > 0) {
        mapParams(mapSteps);
    } else if (chunkOut) {
        dumpChunk(chunkOut, chunkIsPreset);
    } else if (doInfo) {
        dumpInfo();
    } else if (doRender) {
        printf("   rendering notes:");
        for (size_t i = 0; i < ro.notes.size(); i++) printf(" %d", ro.notes[i]);
        printf(" vel %d, len %.2fs + tail %.2fs\n", ro.velocity, ro.noteLen, ro.tail);
        rc = renderToFile(ro);
    } else {
        if (!noMidi) openMidiIn(midiDev);
        if (!audioStart()) rc = 1;
        else {
            // The window title carries a marker so scripts can tell this
            // window apart from the recreation's, which is also called
            // "SwarmSynth" and would otherwise be screenshotted by mistake.
            std::string title = strDispatch(effGetEffectName);
            if (title.empty()) title = dllPath;
            title += " - vsthost32";
            rc = runGui(title.c_str());
            audioStop();
        }
        closeMidiIn();
    }

    stopPlugin();
    DeleteCriticalSection(&g_plugCs);
    DeleteCriticalSection(&g_midiCs);
    return rc;
}
