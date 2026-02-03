/*
 * converter.c - SF2 to WFB conversion logic
 */

#include "../include/converter.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* Forward declarations */
extern uint16_t swap16(uint16_t val);
extern void safe_string_copy(char *dest, const char *src, size_t dest_size);
extern int resample_to_44100_if_needed(int16_t **data, uint32_t *sample_count,
                                       uint32_t *sample_rate);
extern struct sfPresetHeader *sf2_get_preset(struct SF2Bank *bank, int bank_num, int preset_num);
extern struct sfPresetHeader *sf2_get_first_preset(struct SF2Bank *bank);

/* Convert SF2 timecents to WaveFront time value (0-127) */
static uint8_t timecents_to_wf(int16_t timecents) {
    /* SF2 timecents: -12000 to 8000 (log2 seconds * 1200)
     * WaveFront: 0-127 linear approximation */
    if (timecents < -12000) timecents = -12000;
    if (timecents > 8000) timecents = 8000;

    /* Simple mapping - could be refined */
    int value = (timecents + 12000) * 127 / 20000;
    if (value < 0) value = 0;
    if (value > 127) value = 127;
    return (uint8_t)value;
}

/* Convert SF2 centibels to WaveFront level (-128 to 127) */
static int8_t centibels_to_level(int16_t centibels) {
    /* SF2: 0 = full, 960 = -96dB
     * WaveFront: 127 = full, 0 = silent, -128 = inverse */
    int level = 127 - (centibels / 8);
    if (level < -128) level = -128;
    if (level > 127) level = 127;
    return (int8_t)level;
}

/* Convert SF2 cents to WaveFront frequency bias */
static int16_t cents_to_freq_bias(int16_t cents) {
    /* Approximate conversion */
    return cents;
}

/* Initialize default envelope */
static void init_default_envelope(struct ENVELOPE *env) {
    memset(env, 0, sizeof(*env));
    env->fAttackTime = 0;
    env->fDecay1Time = 40;
    env->fDecay2Time = 40;
    env->fSustainTime = 100;
    env->fReleaseTime = 30;
    env->fRelease2Time = 10;
    env->cAttackLevel = 127;
    env->cDecay1Level = 100;
    env->cDecay2Level = 80;
    env->cSustainLevel = 64;
    env->cReleaseLevel = 0;
}

/* Initialize default LFO */
static void init_default_lfo(struct LFO *lfo) {
    memset(lfo, 0, sizeof(*lfo));
    lfo->fFrequency = 20;
}

/* Initialize default patch */
static void init_default_patch(struct PATCH *patch) {
    memset(patch, 0, sizeof(*patch));
    patch->nFreqBias = 0;
    patch->fAmpBias = 64;
    patch->bySampleNumber = 0;
    patch->fPitchBend = 2; /* ±2 semitones */
    init_default_envelope(&patch->envelope1);
    init_default_envelope(&patch->envelope2);
    init_default_lfo(&patch->lfo1);
    init_default_lfo(&patch->lfo2);
}

/* Parse SF2 generators and apply to patch */
static void apply_generators(struct PATCH *patch, struct SF2Bank *sf2,
                            int gen_start, int gen_end) {
    int i;

    for (i = gen_start; i < gen_end; i++) {
        struct sfInstGenList *gen = &sf2->inst_gens[i];

        switch (gen->sfGenOper) {
            case GEN_ATTACK_VOL_ENV:
                patch->envelope1.fAttackTime = timecents_to_wf(gen->genAmount.shAmount);
                break;
            case GEN_DECAY_VOL_ENV:
                patch->envelope1.fDecay1Time = timecents_to_wf(gen->genAmount.shAmount);
                break;
            case GEN_SUSTAIN_VOL_ENV:
                patch->envelope1.cSustainLevel = centibels_to_level(gen->genAmount.shAmount);
                break;
            case GEN_RELEASE_VOL_ENV:
                patch->envelope1.fReleaseTime = timecents_to_wf(gen->genAmount.shAmount);
                break;
            case GEN_INITIAL_ATTENUATION:
                patch->fAmpBias = 127 - (gen->genAmount.shAmount / 8);
                if (patch->fAmpBias > 127) patch->fAmpBias = 127;
                if (patch->fAmpBias < 0) patch->fAmpBias = 0;
                break;
            case GEN_PAN:
                /* Pan is handled at layer level in WFB */
                break;
            case GEN_COARSE_TUNE:
                patch->nFreqBias = swap16(gen->genAmount.shAmount * 100);
                break;
            case GEN_FINE_TUNE:
                {
                    int16_t current = swap16(patch->nFreqBias);
                    patch->nFreqBias = swap16(current + gen->genAmount.shAmount);
                }
                break;
        }
    }
}

/* Add a sample to the WFB bank */
static int add_sample(struct WFBBank *wfb, struct SF2Bank *sf2, int sf2_sample_idx,
                      int *resampled_count) {
    struct sfSample *sf2_samp;
    struct WaveFrontExtendedSampleInfo *info;
    struct SAMPLE *wf_samp;
    int16_t *sample_data;
    uint32_t sample_count, sample_rate;
    int wfb_idx;

    if (wfb->sample_count >= WF_MAX_SAMPLES) {
        return -1;  /* Sample limit reached */
    }

    if (sf2_sample_idx < 0 || sf2_sample_idx >= sf2->sample_count) {
        return -1;
    }

    sf2_samp = &sf2->samples[sf2_sample_idx];
    wfb_idx = wfb->sample_count;

    info = &wfb->samples[wfb_idx].info;
    wf_samp = &wfb->samples[wfb_idx].data.sample;

    /* Fill in sample info */
    info->nSampleType = WF_ST_SAMPLE;
    info->nNumber = wfb_idx;
    safe_string_copy(info->szName, sf2_samp->achSampleName, NAME_LENGTH);

    sample_rate = sf2_samp->dwSampleRate;
    sample_count = sf2_samp->dwEnd - sf2_samp->dwStart;

    /* Extract sample data */
    sample_data = malloc(sample_count * sizeof(int16_t));
    if (!sample_data) {
        return -1;
    }

    memcpy(sample_data, &sf2->sample_data[sf2_samp->dwStart], sample_count * sizeof(int16_t));

    /* Resample if needed */
    if (resample_to_44100_if_needed(&sample_data, &sample_count, &sample_rate) > 0) {
        (*resampled_count)++;
    }

    info->dwSampleRate = sample_rate;
    info->dwSizeInSamples = sample_count;
    info->dwSizeInBytes = sample_count * sizeof(int16_t);

    /* Detect stereo pairing */
    if (sf2_samp->sfSampleType == LEFT_SAMPLE) {
        info->nChannel = WF_CH_LEFT;
    } else if (sf2_samp->sfSampleType == RIGHT_SAMPLE) {
        info->nChannel = WF_CH_RIGHT;
    } else {
        info->nChannel = WF_CH_MONO;
    }

    /* Fill SAMPLE struct */
    memset(wf_samp, 0, sizeof(*wf_samp));
    wf_samp->sampleStartOffset.fInteger = 0;
    wf_samp->sampleEndOffset.fInteger = sample_count;

    /* Set loop points if present */
    if (sf2_samp->dwStartloop < sf2_samp->dwEndloop &&
        sf2_samp->dwStartloop >= sf2_samp->dwStart &&
        sf2_samp->dwEndloop <= sf2_samp->dwEnd) {

        uint32_t loop_start = sf2_samp->dwStartloop - sf2_samp->dwStart;
        uint32_t loop_end = sf2_samp->dwEndloop - sf2_samp->dwStart;

        /* Adjust for resampling */
        if (sf2_samp->dwSampleRate != sample_rate) {
            float ratio = (float)sample_rate / (float)sf2_samp->dwSampleRate;
            loop_start = (uint32_t)(loop_start * ratio);
            loop_end = (uint32_t)(loop_end * ratio);
        }

        wf_samp->loopStartOffset.fInteger = loop_start;
        wf_samp->loopEndOffset.fInteger = loop_end;
        wf_samp->fLoop = 1;
    }

    wf_samp->nFrequencyBias = cents_to_freq_bias(sf2_samp->chPitchCorrection);
    wf_samp->fSampleResolution = LINEAR_16BIT;

    /* Store PCM data */
    wfb->samples[wfb_idx].pcm_data = sample_data;

    /* Update totals */
    wfb->total_sample_memory += info->dwSizeInBytes;
    wfb->sample_count++;

    return wfb_idx;
}

/* Convert a single SF2 preset to WFB program */
static int convert_preset(struct WFBBank *wfb, struct SF2Bank *sf2,
                         struct sfPresetHeader *preset, int prog_num,
                         int *resampled_count) {
    struct WaveFrontProgram *wf_prog;
    struct WaveFrontPatch *wf_patch;
    int bag_start, bag_end;
    int layer_idx = 0;
    int i;

    if (wfb->program_count >= WF_MAX_PROGRAMS) {
        return -1;
    }

    wf_prog = &wfb->programs[wfb->program_count];
    memset(wf_prog, 0, sizeof(*wf_prog));

    wf_prog->nNumber = prog_num;
    safe_string_copy(wf_prog->szName, preset->achPresetName, NAME_LENGTH);

    /* Parse preset bags (zones) */
    bag_start = preset->wPresetBagNdx;
    bag_end = (preset < &sf2->presets[sf2->preset_count - 1]) ?
              (preset + 1)->wPresetBagNdx : sf2->preset_bag_count;

    for (i = bag_start; i < bag_end && layer_idx < NUM_LAYERS; i++) {
        struct sfPresetBag *bag = &sf2->preset_bags[i];
        int gen_start = bag->wGenNdx;
        int gen_end = (i + 1 < sf2->preset_bag_count) ?
                      sf2->preset_bags[i + 1].wGenNdx : sf2->preset_gen_count;
        int instrument_idx = -1;
        int j;

        /* Find instrument reference */
        for (j = gen_start; j < gen_end; j++) {
            if (sf2->preset_gens[j].sfGenOper == GEN_INSTRUMENT) {
                instrument_idx = sf2->preset_gens[j].genAmount.wAmount;
                break;
            }
        }

        if (instrument_idx < 0 || instrument_idx >= sf2->inst_count) {
            continue;  /* Skip global zone or invalid */
        }

        /* Create patch for this layer */
        if (wfb->patch_count >= WF_MAX_PATCHES) {
            break;
        }

        wf_patch = &wfb->patches[wfb->patch_count];
        memset(wf_patch, 0, sizeof(*wf_patch));
        wf_patch->nNumber = wfb->patch_count;

        snprintf(wf_patch->szName, NAME_LENGTH, "%s_L%d",
                 preset->achPresetName, layer_idx);

        init_default_patch(&wf_patch->base);

        /* Parse instrument */
        struct sfInst *inst = &sf2->instruments[instrument_idx];
        int inst_bag_start = inst->wInstBagNdx;
        int inst_bag_end = (instrument_idx + 1 < sf2->inst_count) ?
                          sf2->instruments[instrument_idx + 1].wInstBagNdx :
                          sf2->inst_bag_count;

        /* Use first instrument zone */
        if (inst_bag_start < inst_bag_end) {
            struct sfInstBag *inst_bag = &sf2->inst_bags[inst_bag_start];
            int inst_gen_start = inst_bag->wInstGenNdx;
            int inst_gen_end = (inst_bag_start + 1 < sf2->inst_bag_count) ?
                              sf2->inst_bags[inst_bag_start + 1].wInstGenNdx :
                              sf2->inst_gen_count;
            int sample_idx = -1;

            /* Apply generators */
            apply_generators(&wf_patch->base, sf2, inst_gen_start, inst_gen_end);

            /* Find sample */
            for (j = inst_gen_start; j < inst_gen_end; j++) {
                if (sf2->inst_gens[j].sfGenOper == GEN_SAMPLE_ID) {
                    sample_idx = sf2->inst_gens[j].genAmount.wAmount;
                    break;
                }
            }

            /* Add sample */
            if (sample_idx >= 0) {
                int wfb_sample_idx = add_sample(wfb, sf2, sample_idx, resampled_count);
                if (wfb_sample_idx >= 0) {
                    wf_patch->base.bySampleNumber = wfb_sample_idx;
                }
            }
        }

        /* Setup layer */
        struct LAYER *layer = &wf_prog->base.layer[layer_idx];
        layer->byPatchNumber = wfb->patch_count;
        layer->fMixLevel = 127;
        layer->fUnmute = 1;
        layer->fSplitPoint = 0;
        layer->fPan = 7;  /* Center */

        wfb->patch_count++;
        layer_idx++;
    }

    wfb->program_count++;
    return 0;
}

/* Main conversion function */
int convert_sf2_to_wfb(const char *input_file, const char *output_file,
                       struct ConversionOptions *opts) {
    struct SF2Bank sf2;
    struct WFBBank wfb;
    int i, resampled_count = 0;
    int discarded_samples = 0;
    uint32_t memory_limit;

    /* Open SF2 file */
    if (sf2_open(input_file, &sf2) != 0) {
        return -1;
    }

    /* Initialize WFB bank */
    init_wfb_bank(&wfb, opts->device_name ? opts->device_name : "Maui");

    /* Convert Bank 0 (melodic programs 0-127) */
    for (i = 0; i < 128; i++) {
        struct sfPresetHeader *preset = sf2_get_preset(&sf2, 0, i);
        if (preset) {
            if (convert_preset(&wfb, &sf2, preset, i, &resampled_count) != 0) {
                fprintf(stderr, "Warning: Failed to convert preset %d\n", i);
            }
        }
    }

    /* Convert drums (Bank 128) if not using separate drum file */
    if (!opts->drums_file) {
        struct sfPresetHeader *drums = sf2_get_preset(&sf2, 128, 0);
        if (!drums) {
            /* Try bank 0 preset 128 */
            drums = sf2_get_preset(&sf2, 0, 128);
            if (drums) {
                printf("Warning: Using Bank 0 as Drum Kit. "
                       "Verify that key mappings align with GM percussion (Key 35-81).\n");
            }
        }

        if (drums) {
            /* For drums, we'd need to create a drumkit structure
             * This is simplified for now */
            wfb.has_drumkit = 1;
            memset(&wfb.drumkit, 0, sizeof(wfb.drumkit));
        }
    }

    /* Check sample limit */
    if (wfb.sample_count > WF_MAX_SAMPLES) {
        discarded_samples = wfb.sample_count - WF_MAX_SAMPLES;
        wfb.sample_count = WF_MAX_SAMPLES;
        printf("Warning: Source exceeded 512 sample limit. "
               "%d samples were discarded.\n", discarded_samples);
    }

    /* Update header counts */
    wfb.header.wProgramCount = wfb.program_count;
    wfb.header.wPatchCount = wfb.patch_count;
    wfb.header.wSampleCount = wfb.sample_count;
    wfb.header.wDrumkitCount = wfb.has_drumkit ? 1 : 0;
    wfb.header.dwMemoryRequired = wfb.total_sample_memory;

    /* Check memory limit */
    memory_limit = get_device_memory_limit(wfb.header.szSynthName);
    if (wfb.total_sample_memory > memory_limit) {
        printf("Warning: Total sample memory (%u bytes) exceeds %s limit (%u bytes)\n",
               wfb.total_sample_memory, wfb.header.szSynthName, memory_limit);
    }

    /* Write WFB file */
    const char *final_output = output_file ? output_file : get_auto_increment_filename(output_file);
    if (wfb_write(final_output, &wfb) != 0) {
        sf2_close(&sf2);
        return -1;
    }

    printf("Conversion complete: '%s' -> '%s'\n", input_file, final_output);
    printf("  Programs: %d, Patches: %d, Samples: %d\n",
           wfb.program_count, wfb.patch_count, wfb.sample_count);
    if (resampled_count > 0) {
        printf("  Resampled: %d samples\n", resampled_count);
    }

    /* Print info */
    wfb_print_info(&wfb);

    /* Cleanup */
    sf2_close(&sf2);
    for (i = 0; i < wfb.sample_count; i++) {
        free(wfb.samples[i].pcm_data);
    }

    return 0;
}
