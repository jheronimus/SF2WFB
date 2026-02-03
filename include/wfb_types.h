/*
 * wfb_types.h - WaveFront Bank File Format Data Structures
 *
 * Implements the binary layout for Turtle Beach WaveFront .WFB files
 * Based on FILES.H and WFGATAPI.H from the WaveFront SDK
 */

#ifndef WFB_TYPES_H
#define WFB_TYPES_H

#include <stdint.h>

/* Constants */
#define NUM_LAYERS 4
#define NUM_MIDIKEYS 128
#define NUM_MIDICHANNELS 16
#define NAME_LENGTH 32
#define MAX_COMMENT 64
#define MAX_PATH_LENGTH 260

#define WF_VERSION 120  /* Version 1.20 */

#define WF_MAX_PROGRAMS 128
#define WF_MAX_PATCHES 256
#define WF_MAX_SAMPLES 512

/* Channel constants */
#define WF_CH_MONO  0
#define WF_CH_LEFT  1
#define WF_CH_RIGHT 2

/* Sample types */
#define WF_ST_SAMPLE      0
#define WF_ST_MULTISAMPLE 1
#define WF_ST_ALIAS       2
#define WF_ST_EMPTY       127

/* Sample formats */
#define LINEAR_16BIT 0
#define WHITE_NOISE  1
#define LINEAR_8BIT  2
#define MULAW_8BIT   3

/* Device memory limits (in bytes) */
#define DEVICE_MEM_RIO        (4 * 1024 * 1024)      /* 4 MB */
#define DEVICE_MEM_MAUI       8650752                /* 8.25 MB */
#define DEVICE_MEM_TROPEZ     8650752                /* 8.25 MB */
#define DEVICE_MEM_TROPEZPLUS 12845056               /* 12.25 MB */

/* Data types matching Windows SDK */
typedef uint8_t  BYTE;
typedef uint16_t WORD;
typedef uint32_t DWORD;
typedef uint32_t UINT;
typedef int16_t  BOOL;

/* Ensure packed structures */
#ifdef __GNUC__
#define PACKED __attribute__((packed))
#else
#define PACKED
#pragma pack(push, 1)
#endif

/* ===== Core WaveFront Structures ===== */

struct ENVELOPE {
    BYTE fAttackTime:7;
    BYTE fUnused1:1;
    BYTE fDecay1Time:7;
    BYTE fUnused2:1;
    BYTE fDecay2Time:7;
    BYTE fUnused3:1;
    BYTE fSustainTime:7;
    BYTE fUnused4:1;
    BYTE fReleaseTime:7;
    BYTE fUnused5:1;
    BYTE fRelease2Time:7;
    BYTE fUnused6:1;
    int8_t cAttackLevel;
    int8_t cDecay1Level;
    int8_t cDecay2Level;
    int8_t cSustainLevel;
    int8_t cReleaseLevel;
    BYTE fAttackVelocity:7;
    BYTE fUnused7:1;
    BYTE fVolumeVelocity:7;
    BYTE fUnused8:1;
    BYTE fKeyScale:7;
    BYTE fUnused9:1;
} PACKED;

struct LFO {
    BYTE bySampleNumber;
    BYTE fFrequency:7;
    BYTE fUnused1:1;
    BYTE fAMSource:4;
    BYTE fFMSource:4;
    int8_t cFMAmount;
    int8_t cAMAmount;
    int8_t cStartLevel;
    int8_t cEndLevel;
    BYTE fDelayTime:7;
    BYTE fWaveRestart:1;
    BYTE fRampTime:7;
    BYTE fUnused2:1;
} PACKED;

struct PATCH {
    int16_t nFreqBias;         /* ** THIS IS IN MOTOROLA FORMAT (BIG ENDIAN)!! ** */
    BYTE fAmpBias:7;
    BYTE fUnused1:1;
    BYTE fPortamento:7;
    BYTE fUnused2:1;
    BYTE bySampleNumber;
    BYTE fPitchBend:4;
    BYTE fSampleMSB:1;
    BYTE fUnused3:3;
    BYTE fMono:1;
    BYTE fRetrigger:1;
    BYTE fNoHold:1;
    BYTE fRestart:1;
    BYTE fFilterConfig:2;
    BYTE fReuse:1;
    BYTE fResetLfo:1;
    BYTE fFMSource2:4;
    BYTE fFMSource1:4;
    int8_t cFMAmount1;
    int8_t cFMAmount2;
    BYTE fAMSource:4;
    BYTE fUnused4:4;
    int8_t cAMAmount;
    BYTE fFC1MSource:4;
    BYTE fFC2MSource:4;
    int8_t cFC1MAmount;
    int8_t cFC1KeyScale;
    int8_t cFC1FreqBias;
    int8_t cFC2MAmount;
    int8_t cFC2KeyScale;
    int8_t cFC2FreqBias;
    BYTE fRandomizerRate:7;
    BYTE fUnused5:1;
    struct ENVELOPE envelope1;
    struct ENVELOPE envelope2;
    struct LFO lfo1;
    struct LFO lfo2;
} PACKED;

struct LAYER {
    BYTE byPatchNumber;
    BYTE fMixLevel:7;
    BYTE fUnmute:1;
    BYTE fSplitPoint:7;
    BYTE fSplitDir:1;
    BYTE fPanModSource:2;
    BYTE fPanModulated:1;
    BYTE fPan:4;
    BYTE fSplitType:1;
} PACKED;

struct PROGRAM {
    struct LAYER layer[NUM_LAYERS];
} PACKED;

struct SAMPLE_OFFSET {
    DWORD fFraction:4;
    DWORD fInteger:20;
    DWORD fUnused:8;
} PACKED;

struct SAMPLE {
    struct SAMPLE_OFFSET sampleStartOffset;
    struct SAMPLE_OFFSET loopStartOffset;
    struct SAMPLE_OFFSET loopEndOffset;
    struct SAMPLE_OFFSET sampleEndOffset;
    int16_t nFrequencyBias;
    BYTE fSampleResolution:2;
    BYTE fUnused1:1;
    BYTE fLoop:1;
    BYTE fBidirectional:1;
    BYTE fUnused2:1;
    BYTE fReverse:1;
    BYTE fUnused3:1;
} PACKED;

struct MULTISAMPLE {
    int16_t nNumberOfSamples;
    int16_t nSampleNumber[NUM_MIDIKEYS];
} PACKED;

struct ALIAS {
    int16_t nOriginalSample;
    struct SAMPLE_OFFSET sampleStartOffset;
    struct SAMPLE_OFFSET loopStartOffset;
    struct SAMPLE_OFFSET sampleEndOffset;
    struct SAMPLE_OFFSET loopEndOffset;
    int16_t nFrequencyBias;
    BYTE fSampleResolution:2;
    BYTE fUnused1:1;
    BYTE fLoop:1;
    BYTE fBidirectional:1;
    BYTE fUnused2:1;
    BYTE fReverse:1;
    BYTE fUnused3:1;
} PACKED;

struct DRUM {
    BYTE byPatchNumber;
    BYTE fMixLevel:7;
    BYTE fUnmute:1;
    BYTE fGroup:4;
    BYTE fUnused1:4;
    BYTE fPanModSource:2;
    BYTE fPanModulated:1;
    BYTE fPanAmount:4;
    BYTE fUnused2:1;
} PACKED;

struct DRUMKIT {
    struct DRUM drum[NUM_MIDIKEYS];
} PACKED;

/* ===== File Format Structures ===== */
/* These simulate C++ inheritance using composition */

/* 256-byte header at the start of every WaveFront file */
struct WaveFrontFileHeader {
    char szSynthName[NAME_LENGTH];      /* e.g., "Maui", "Rio", "Tropez" */
    char szFileType[NAME_LENGTH];       /* "Bank", "Program", or "DrumKit" */
    WORD wVersion;                      /* Version * 100 (e.g., 120 = v1.20) */
    WORD wProgramCount;                 /* 0-128 */
    WORD wDrumkitCount;                 /* 0-1 */
    WORD wPatchCount;                   /* 0-256 */
    WORD wSampleCount;                  /* 0-512 */
    WORD wEffectsCount;                 /* Effects count */
    DWORD dwProgramOffset;              /* File offset to program data */
    DWORD dwDrumkitOffset;              /* File offset to drumkit data */
    DWORD dwPatchOffset;                /* File offset to patch data */
    DWORD dwSampleOffset;               /* File offset to sample data */
    DWORD dwEffectsOffset;              /* File offset to effects data */
    DWORD dwMemoryRequired;             /* Total RAM needed for samples */
    BOOL bEmbeddedSamples;              /* TRUE if samples embedded */
    BOOL bUnused;                       /* Padding */
    char szComment[MAX_COMMENT];        /* Copyright/Comments */
    BYTE byPad[62];                     /* Reserved */
} PACKED;

/* Simulates: struct WaveFrontProgram : public PROGRAM */
struct WaveFrontProgram {
    struct PROGRAM base;                /* Inherited fields - MUST BE FIRST */
    int16_t nNumber;                    /* Program number (0-127) */
    char szName[NAME_LENGTH];           /* Program name */
} PACKED;

/* Simulates: struct WaveFrontPatch : public PATCH */
struct WaveFrontPatch {
    struct PATCH base;                  /* Inherited fields - MUST BE FIRST */
    int16_t nNumber;                    /* Patch number */
    char szName[NAME_LENGTH];           /* Patch name */
} PACKED;

/* Simulates: struct WaveFrontDrumkit : public DRUMKIT */
struct WaveFrontDrumkit {
    struct DRUMKIT base;                /* Inherited fields - MUST BE FIRST */
} PACKED;

/* Extended sample info (version 1.20+) */
struct WaveFrontExtendedSampleInfo {
    DWORD dwSize;                       /* Total size of this entry */
    int16_t nSampleType;                /* WF_ST_SAMPLE, etc. */
    int16_t nNumber;                    /* Sample number (0-511) */
    char szName[NAME_LENGTH];           /* Sample name */
    DWORD dwSampleRate;                 /* Hz (e.g., 44100) */
    DWORD dwSizeInBytes;                /* Raw PCM data size */
    DWORD dwSizeInSamples;              /* Sample count */
    UINT nChannel;                      /* WF_CH_MONO/LEFT/RIGHT */
    BYTE byUnused[62];                  /* Padding */
} PACKED;

#ifndef __GNUC__
#pragma pack(pop)
#endif

#endif /* WFB_TYPES_H */
