// Minimal clean-room VST 2.4 ABI declarations.
// Enough of the interface to load and drive a 32-bit Windows VST2 plugin.
// No Steinberg SDK code is used here; only the binary layout and opcode
// numbers, which are what the ABI actually is.
#ifndef VST2_H
#define VST2_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN64) || defined(__x86_64__)
typedef int64_t VstIntPtr;
#else
typedef int32_t VstIntPtr;
#endif

#define VSTCALLBACK __cdecl

struct AEffect;

typedef VstIntPtr (VSTCALLBACK *AudioMasterCallback)(struct AEffect*, int32_t opcode,
                                                     int32_t index, VstIntPtr value,
                                                     void* ptr, float opt);
typedef VstIntPtr (VSTCALLBACK *AEffectDispatcherProc)(struct AEffect*, int32_t opcode,
                                                       int32_t index, VstIntPtr value,
                                                       void* ptr, float opt);
typedef void  (VSTCALLBACK *AEffectProcessProc)(struct AEffect*, float**, float**, int32_t);
typedef void  (VSTCALLBACK *AEffectProcessDoubleProc)(struct AEffect*, double**, double**, int32_t);
typedef void  (VSTCALLBACK *AEffectSetParameterProc)(struct AEffect*, int32_t, float);
typedef float (VSTCALLBACK *AEffectGetParameterProc)(struct AEffect*, int32_t);

typedef struct AEffect* (VSTCALLBACK *VstPluginMainProc)(AudioMasterCallback);

#define kEffectMagic 0x56737450 /* 'VstP' */

struct AEffect {
    int32_t                  magic;
    AEffectDispatcherProc    dispatcher;
    AEffectProcessProc       process;              // deprecated (accumulating)
    AEffectSetParameterProc  setParameter;
    AEffectGetParameterProc  getParameter;
    int32_t                  numPrograms;
    int32_t                  numParams;
    int32_t                  numInputs;
    int32_t                  numOutputs;
    int32_t                  flags;
    VstIntPtr                resvd1;
    VstIntPtr                resvd2;
    int32_t                  initialDelay;
    int32_t                  realQualities;        // deprecated
    int32_t                  offQualities;         // deprecated
    float                    ioRatio;              // deprecated
    void*                    object;
    void*                    user;
    int32_t                  uniqueID;
    int32_t                  version;
    AEffectProcessProc       processReplacing;
    AEffectProcessDoubleProc processDoubleReplacing;
    char                     future[56];
};

enum {
    effFlagsHasEditor       = 1 << 0,
    effFlagsCanReplacing    = 1 << 4,
    effFlagsProgramChunks   = 1 << 5,
    effFlagsIsSynth         = 1 << 8,
    effFlagsNoSoundInStop   = 1 << 9,
    effFlagsCanDoubleReplacing = 1 << 12,
};

// ---- effect opcodes -------------------------------------------------------
enum {
    effOpen = 0, effClose, effSetProgram, effGetProgram, effSetProgramName,
    effGetProgramName, effGetParamLabel, effGetParamDisplay, effGetParamName,
    effSetSampleRate = 10, effSetBlockSize, effMainsChanged,
    effEditGetRect, effEditOpen, effEditClose,
    effEditIdle = 19,
    effGetChunk = 23, effSetChunk,
    effProcessEvents = 25, effCanBeAutomated, effString2Parameter,
    effGetProgramNameIndexed = 29,
    effGetInputProperties = 33, effGetOutputProperties, effGetPlugCategory,
    effSetSpeakerArrangement = 42,
    effSetBypass = 44, effGetEffectName,
    effGetVendorString = 47, effGetProductString, effGetVendorVersion,
    effVendorSpecific = 50, effCanDo, effGetTailSize,
    effGetParameterProperties = 56,
    effGetVstVersion = 58,
    effEditKeyDown = 59, effEditKeyUp, effSetEditKnobMode,
    effBeginSetProgram = 67, effEndSetProgram,
    effGetSpeakerArrangement = 69, effShellGetNextPlugin,
    effStartProcess = 71, effStopProcess,
    effSetProcessPrecision = 77,
    effGetNumMidiInputChannels = 78, effGetNumMidiOutputChannels,
};

// ---- host (audioMaster) opcodes ------------------------------------------
enum {
    audioMasterAutomate = 0, audioMasterVersion, audioMasterCurrentId,
    audioMasterIdle, audioMasterPinConnected,
    audioMasterWantMidi = 6, audioMasterGetTime, audioMasterProcessEvents,
    audioMasterIOChanged = 13, audioMasterNeedIdle, audioMasterSizeWindow,
    audioMasterGetSampleRate = 16, audioMasterGetBlockSize,
    audioMasterGetInputLatency = 18, audioMasterGetOutputLatency,
    audioMasterGetCurrentProcessLevel = 23, audioMasterGetAutomationState,
    audioMasterGetVendorString = 32, audioMasterGetProductString,
    audioMasterGetVendorVersion, audioMasterVendorSpecific,
    audioMasterCanDo = 37, audioMasterGetLanguage,
    audioMasterGetDirectory = 41, audioMasterUpdateDisplay,
    audioMasterBeginEdit, audioMasterEndEdit,
    audioMasterOpenFileSelector, audioMasterCloseFileSelector,
};

enum { kVstProcessLevelUnknown = 0, kVstProcessLevelUser, kVstProcessLevelRealtime,
       kVstProcessLevelPrefetch, kVstProcessLevelOffline };

// ---- events ---------------------------------------------------------------
enum { kVstMidiType = 1, kVstSysExType = 6 };

struct VstEvent {
    int32_t type;
    int32_t byteSize;
    int32_t deltaFrames;
    int32_t flags;
    char    data[16];
};

struct VstMidiEvent {
    int32_t type;            // kVstMidiType
    int32_t byteSize;        // sizeof(VstMidiEvent)
    int32_t deltaFrames;
    int32_t flags;
    int32_t noteLength;
    int32_t noteOffset;
    char    midiData[4];
    char    detune;
    char    noteOffVelocity;
    char    reserved1, reserved2;
};

struct VstEvents {
    int32_t     numEvents;
    VstIntPtr   reserved;
    VstEvent*   events[2];   // variable length in practice
};

// ---- time info ------------------------------------------------------------
enum {
    kVstTransportPlaying = 1 << 1,
    kVstNanosValid  = 1 << 8,
    kVstPpqPosValid = 1 << 9,
    kVstTempoValid  = 1 << 10,
    kVstBarsValid   = 1 << 11,
    kVstTimeSigValid = 1 << 13,
    kVstClockValid  = 1 << 15,
};

struct VstTimeInfo {
    double  samplePos;
    double  sampleRate;
    double  nanoSeconds;
    double  ppqPos;
    double  tempo;
    double  barStartPos;
    double  cycleStartPos;
    double  cycleEndPos;
    int32_t timeSigNumerator;
    int32_t timeSigDenominator;
    int32_t smpteOffset;
    int32_t smpteFrameRate;
    int32_t samplesToNextClock;
    int32_t flags;
};

struct ERect { int16_t top, left, bottom, right; };

struct VstParameterProperties {
    float   stepFloat;
    float   smallStepFloat;
    float   largeStepFloat;
    char    label[64];
    int32_t flags;
    int32_t minInteger;
    int32_t maxInteger;
    int32_t stepInteger;
    int32_t largeStepInteger;
    char    shortLabel[8];
    int16_t displayIndex;
    int16_t category;
    int16_t numParametersInCategory;
    int16_t reserved;
    char    categoryLabel[24];
    char    future[16];
};

#ifdef __cplusplus
}
#endif
#endif // VST2_H
