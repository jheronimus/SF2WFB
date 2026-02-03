/*
 * sf2_types.h - SoundFont 2 File Format Data Structures
 *
 * Defines structures for parsing SoundFont 2.x RIFF files
 */

#ifndef SF2_TYPES_H
#define SF2_TYPES_H

#include <stdint.h>

/* Ensure structures are packed (no alignment padding) */
#define PACKED __attribute__((packed))

/* RIFF chunk header */
struct RIFFChunk {
    char chunkID[4];
    uint32_t chunkSize;
} PACKED;

/* SF2 Generator operators (subset - only the ones we need) */
enum SFGenerator {
    GEN_START_ADDRS_OFFSET = 0,
    GEN_END_ADDRS_OFFSET = 1,
    GEN_STARTLOOP_ADDRS_OFFSET = 2,
    GEN_ENDLOOP_ADDRS_OFFSET = 3,
    GEN_START_ADDRS_COARSE_OFFSET = 4,
    GEN_MOD_LFO_TO_PITCH = 5,
    GEN_VIB_LFO_TO_PITCH = 6,
    GEN_MOD_ENV_TO_PITCH = 7,
    GEN_INITIAL_FILTER_FC = 8,
    GEN_INITIAL_FILTER_Q = 9,
    GEN_MOD_LFO_TO_FILTER_FC = 10,
    GEN_MOD_ENV_TO_FILTER_FC = 11,
    GEN_END_ADDRS_COARSE_OFFSET = 12,
    GEN_MOD_LFO_TO_VOLUME = 13,
    GEN_CHORUS_EFFECTS_SEND = 15,
    GEN_REVERB_EFFECTS_SEND = 16,
    GEN_PAN = 17,
    GEN_DELAY_MOD_LFO = 21,
    GEN_FREQ_MOD_LFO = 22,
    GEN_DELAY_VIB_LFO = 23,
    GEN_FREQ_VIB_LFO = 24,
    GEN_DELAY_MOD_ENV = 25,
    GEN_ATTACK_MOD_ENV = 26,
    GEN_HOLD_MOD_ENV = 27,
    GEN_DECAY_MOD_ENV = 28,
    GEN_SUSTAIN_MOD_ENV = 29,
    GEN_RELEASE_MOD_ENV = 30,
    GEN_KEYNUM_TO_MOD_ENV_HOLD = 31,
    GEN_KEYNUM_TO_MOD_ENV_DECAY = 32,
    GEN_DELAY_VOL_ENV = 33,
    GEN_ATTACK_VOL_ENV = 34,
    GEN_HOLD_VOL_ENV = 35,
    GEN_DECAY_VOL_ENV = 36,
    GEN_SUSTAIN_VOL_ENV = 37,
    GEN_RELEASE_VOL_ENV = 38,
    GEN_KEYNUM_TO_VOL_ENV_HOLD = 39,
    GEN_KEYNUM_TO_VOL_ENV_DECAY = 40,
    GEN_INSTRUMENT = 41,
    GEN_KEY_RANGE = 43,
    GEN_VEL_RANGE = 44,
    GEN_STARTLOOP_ADDRS_COARSE_OFFSET = 45,
    GEN_KEYNUM = 46,
    GEN_VELOCITY = 47,
    GEN_INITIAL_ATTENUATION = 48,
    GEN_ENDLOOP_ADDRS_COARSE_OFFSET = 50,
    GEN_COARSE_TUNE = 51,
    GEN_FINE_TUNE = 52,
    GEN_SAMPLE_ID = 53,
    GEN_SAMPLE_MODES = 54,
    GEN_SCALE_TUNING = 56,
    GEN_EXCLUSIVE_CLASS = 57,
    GEN_OVERRIDING_ROOT_KEY = 58,
    GEN_END_OPER = 60
};

/* SF2 sample link types */
enum SFSampleLink {
    MONO_SAMPLE = 1,
    RIGHT_SAMPLE = 2,
    LEFT_SAMPLE = 4,
    LINKED_SAMPLE = 8,
    ROM_MONO_SAMPLE = 0x8001,
    ROM_RIGHT_SAMPLE = 0x8002,
    ROM_LEFT_SAMPLE = 0x8004,
    ROM_LINKED_SAMPLE = 0x8008
};

/* SF2 structures */
struct sfPresetHeader {
    char achPresetName[20];
    uint16_t wPreset;
    uint16_t wBank;
    uint16_t wPresetBagNdx;
    uint32_t dwLibrary;
    uint32_t dwGenre;
    uint32_t dwMorphology;
} PACKED;

struct sfPresetBag {
    uint16_t wGenNdx;
    uint16_t wModNdx;
} PACKED;

struct sfModList {
    uint16_t sfModSrcOper;
    uint16_t sfModDestOper;
    int16_t modAmount;
    uint16_t sfModAmtSrcOper;
    uint16_t sfModTransOper;
} PACKED;

struct sfGenList {
    uint16_t sfGenOper;
    union {
        int16_t shAmount;
        uint16_t wAmount;
        struct {
            uint8_t byLo;
            uint8_t byHi;
        } range;
    } genAmount;
} PACKED;

struct sfInst {
    char achInstName[20];
    uint16_t wInstBagNdx;
} PACKED;

struct sfInstBag {
    uint16_t wInstGenNdx;
    uint16_t wInstModNdx;
} PACKED;

struct sfInstGenList {
    uint16_t sfGenOper;
    union {
        int16_t shAmount;
        uint16_t wAmount;
        struct {
            uint8_t byLo;
            uint8_t byHi;
        } range;
    } genAmount;
} PACKED;

struct sfInstModList {
    uint16_t sfModSrcOper;
    uint16_t sfModDestOper;
    int16_t modAmount;
    uint16_t sfModAmtSrcOper;
    uint16_t sfModTransOper;
} PACKED;

struct sfSample {
    char achSampleName[20];
    uint32_t dwStart;
    uint32_t dwEnd;
    uint32_t dwStartloop;
    uint32_t dwEndloop;
    uint32_t dwSampleRate;
    uint8_t byOriginalPitch;
    int8_t chPitchCorrection;
    uint16_t wSampleLink;
    uint16_t sfSampleType;
} PACKED;

#endif /* SF2_TYPES_H */
