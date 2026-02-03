/*
 * converter.h - SF2 to WFB conversion interface
 */

#ifndef CONVERTER_H
#define CONVERTER_H

#include "wfb_types.h"
#include "sf2_types.h"
#include <stdio.h>

/* Conversion options */
struct ConversionOptions {
    const char *device_name;        /* Target device: Maui, Rio, Tropez, TropezPlus */
    const char *drums_file;         /* Optional drum kit SF2 file */
    const char *output_file;        /* Explicit output filename */
    int patch_count;                /* Number of patches to apply */
    struct {
        const char *file;           /* SF2 file for patch */
        int program_id;             /* Program ID to replace (0-127) */
    } patches[128];                 /* Max patches */
};

/* WFB Bank structure (in-memory representation) */
struct WFBBank {
    struct WaveFrontFileHeader header;
    struct WaveFrontProgram programs[WF_MAX_PROGRAMS];
    struct WaveFrontDrumkit drumkit;
    struct WaveFrontPatch patches[WF_MAX_PATCHES];
    struct {
        struct WaveFrontExtendedSampleInfo info;
        union {
            struct SAMPLE sample;
            struct MULTISAMPLE multisample;
            struct ALIAS alias;
        } data;
        int16_t *pcm_data;           /* Raw sample data */
        uint64_t data_hash;          /* Hash of PCM data for dedup */
        char filespec[MAX_PATH_LENGTH];
    } samples[WF_MAX_SAMPLES];

    int program_count;
    int patch_count;
    int sample_count;
    int has_drumkit;
    uint32_t total_sample_memory;
};

/* SF2 Bank structure (in-memory representation) */
struct SF2Bank {
    /* Hydra data */
    struct sfPresetHeader *presets;
    struct sfPresetBag *preset_bags;
    struct sfModList *preset_mods;
    struct sfGenList *preset_gens;
    struct sfInst *instruments;
    struct sfInstBag *inst_bags;
    struct sfInstModList *inst_mods;
    struct sfInstGenList *inst_gens;
    struct sfSample *samples;

    /* Counts */
    int preset_count;
    int preset_bag_count;
    int preset_mod_count;
    int preset_gen_count;
    int inst_count;
    int inst_bag_count;
    int inst_mod_count;
    int inst_gen_count;
    int sample_count;

    /* Sample data */
    int16_t *sample_data;
    uint32_t sample_data_size;

    /* File handle */
    FILE *file;
};

/* Function prototypes */

/* SF2 parsing */
int sf2_open(const char *filename, struct SF2Bank *bank);
void sf2_close(struct SF2Bank *bank);

/* Conversion */
int convert_sf2_to_wfb(const char *input_file, const char *output_file,
                       struct ConversionOptions *opts);

/* WFB file I/O */
int wfb_write(const char *filename, struct WFBBank *bank);
int wfb_read(const char *filename, struct WFBBank *bank);
void wfb_print_info(struct WFBBank *bank);

/* Resampling */
int16_t *resample_linear(int16_t *input, uint32_t input_samples,
                         uint32_t input_rate, uint32_t output_rate,
                         uint32_t *output_samples);
void resample_set_sample_offset(struct SAMPLE_OFFSET *offset, double pos,
                                uint32_t max_samples);
void resample_scale_loop_points(uint32_t input_rate, uint32_t output_rate,
                                uint32_t input_loop_start, uint32_t input_loop_end,
                                uint32_t output_samples,
                                struct SAMPLE_OFFSET *out_start,
                                struct SAMPLE_OFFSET *out_end);

/* Utility */
const char *get_auto_increment_filename(const char *base_path);
uint32_t get_device_memory_limit(const char *device_name);
int is_valid_device_name(const char *name);
void init_wfb_bank(struct WFBBank *bank, const char *device_name);

#endif /* CONVERTER_H */
