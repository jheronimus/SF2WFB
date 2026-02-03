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

static uint64_t hash_pcm_data(const int16_t *data, uint32_t samples) {
    const uint8_t *bytes = (const uint8_t *)data;
    uint64_t hash = 1469598103934665603ULL;
    uint32_t total_bytes = samples * sizeof(int16_t);
    for (uint32_t i = 0; i < total_bytes; i++) {
        hash ^= bytes[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

static int sample_offsets_equal(const struct SAMPLE_OFFSET *a, const struct SAMPLE_OFFSET *b) {
    return a->fInteger == b->fInteger && a->fFraction == b->fFraction;
}

/* Conversion context to avoid global state */
struct ConversionContext {
    int dedupe_alias_count;
    int patch_reserve;
    int *sf2_sample_map;
    int sf2_sample_map_count;
    int verbose;
};

/* Initialize conversion context */
static void init_conversion_context(struct ConversionContext *ctx, int sample_count, int verbose) {
    ctx->dedupe_alias_count = 0;
    ctx->patch_reserve = 0;
    ctx->verbose = verbose;
    ctx->sf2_sample_map_count = sample_count;
    if (sample_count > 0) {
        ctx->sf2_sample_map = malloc((size_t)sample_count * sizeof(int));
        if (ctx->sf2_sample_map) {
            for (int i = 0; i < sample_count; i++) {
                ctx->sf2_sample_map[i] = -1;
            }
        }
    } else {
        ctx->sf2_sample_map = NULL;
    }
}

/* Free conversion context resources */
static void free_conversion_context(struct ConversionContext *ctx) {
    free(ctx->sf2_sample_map);
    ctx->sf2_sample_map = NULL;
    ctx->sf2_sample_map_count = 0;
}

static int patch_base_equal(const struct PATCH *a, const struct PATCH *b) {
    return memcmp(a, b, sizeof(*a)) == 0;
}

static int8_t cents_to_amount(int16_t cents, int16_t scale);
static int8_t centibels_to_amount(int16_t centibels, int16_t scale);

static int wf_mod_source_from_sf2(uint16_t srcOper) {
    if (srcOper == 0) {
        return -1;
    }
    int is_cc = (srcOper & 0x80) != 0;
    int index = srcOper & 0x7F;
    if (is_cc) {
        switch (index) {
            case 1:  return 10; /* Mod Wheel */
            case 2:  return 11; /* Breath */
            case 4:  return 12; /* Foot */
            case 7:  return 13; /* Volume */
            case 10: return 14; /* Pan */
            case 11: return 15; /* Expression */
            default: return -1;
        }
    }
    switch (index) {
        case 2:  return 6;  /* Velocity */
        case 3:  return 4;  /* Key Number */
        case 13: return 9;  /* Channel Pressure */
        default: return -1;
    }
}

static void apply_modulator_to_patch(struct PATCH *patch, uint16_t destOper,
                                     int16_t amount, int wf_source) {
    if (!patch || wf_source < 0) {
        return;
    }
    switch (destOper) {
        case GEN_INITIAL_ATTENUATION:
            patch->fAMSource = wf_source;
            patch->cAMAmount = centibels_to_amount(amount, 5);
            break;
        case GEN_INITIAL_FILTER_FC:
        case GEN_MOD_LFO_TO_FILTER_FC:
        case GEN_MOD_ENV_TO_FILTER_FC:
            patch->fFC1MSource = wf_source;
            patch->cFC1MAmount = cents_to_amount(amount, 100);
            break;
        case GEN_COARSE_TUNE:
        case GEN_FINE_TUNE:
        case GEN_MOD_LFO_TO_PITCH:
        case GEN_VIB_LFO_TO_PITCH:
        case GEN_MOD_ENV_TO_PITCH:
            if (patch->fFMSource1 == 0 && patch->cFMAmount1 == 0) {
                patch->fFMSource1 = wf_source;
                patch->cFMAmount1 = cents_to_amount(amount, 10);
            } else {
                patch->fFMSource2 = wf_source;
                patch->cFMAmount2 = cents_to_amount(amount, 10);
            }
            break;
        default:
            break;
    }
}

static void apply_modulator_list_to_patch(struct PATCH *patch,
                                          const struct sfModList *mods,
                                          int mod_start, int mod_end) {
    if (!mods || mod_start < 0 || mod_end <= mod_start) {
        return;
    }
    for (int i = mod_start; i < mod_end; i++) {
        int wf_source = wf_mod_source_from_sf2(mods[i].sfModSrcOper);
        apply_modulator_to_patch(patch, mods[i].sfModDestOper,
                                 mods[i].modAmount, wf_source);
    }
}
static const float wf_time_table[128] = {
    0.0f, 0.008f, 0.009f, 0.010f, 0.012f, 0.013f, 0.015f, 0.016f,
    0.018f, 0.020f, 0.022f, 0.024f, 0.026f, 0.029f, 0.031f, 0.034f,
    0.037f, 0.041f, 0.044f, 0.048f, 0.052f, 0.056f, 0.061f, 0.066f,
    0.071f, 0.076f, 0.083f, 0.089f, 0.096f, 0.103f, 0.111f, 0.120f,
    0.129f, 0.139f, 0.150f, 0.161f, 0.173f, 0.186f, 0.200f, 0.215f,
    0.231f, 0.248f, 0.266f, 0.286f, 0.307f, 0.330f, 0.354f, 0.380f,
    0.408f, 0.438f, 0.470f, 0.504f, 0.541f, 0.580f, 0.622f, 0.668f,
    0.716f, 0.768f, 0.824f, 0.883f, 0.947f, 1.016f, 1.089f, 1.168f,
    1.253f, 1.343f, 1.440f, 1.544f, 1.655f, 1.775f, 1.903f, 2.040f,
    2.187f, 2.345f, 2.513f, 2.694f, 2.888f, 3.096f, 3.319f, 3.558f,
    3.814f, 4.088f, 4.382f, 4.697f, 5.035f, 5.397f, 5.785f, 6.200f,
    6.646f, 7.124f, 7.635f, 8.184f, 8.772f, 9.402f, 10.078f, 10.801f,
    11.577f, 12.409f, 13.300f, 14.255f, 15.279f, 16.376f, 17.552f, 18.812f,
    20.163f, 21.611f, 23.162f, 24.826f, 26.608f, 28.518f, 30.566f, 32.760f,
    35.112f, 37.633f, 40.334f, 43.230f, 46.333f, 49.659f, 53.224f, 57.044f,
    61.139f, 65.528f, 70.232f, 75.273f, 80.676f, 86.467f, 92.674f, 99.326f
};

static const float wf_lfo_freq_table[128] = {
    0.0f, 0.0172f, 0.0182f, 0.0194f, 0.0206f, 0.0218f, 0.0232f, 0.0246f,
    0.0261f, 0.0277f, 0.0294f, 0.0312f, 0.0331f, 0.0351f, 0.0373f, 0.0396f,
    0.0420f, 0.0446f, 0.0473f, 0.0502f, 0.0533f, 0.0566f, 0.0601f, 0.0637f,
    0.0676f, 0.0718f, 0.0762f, 0.0809f, 0.0859f, 0.0911f, 0.0967f, 0.1026f,
    0.1089f, 0.1156f, 0.1227f, 0.1303f, 0.1383f, 0.1467f, 0.1558f, 0.1653f,
    0.1755f, 0.1862f, 0.1977f, 0.2098f, 0.2227f, 0.2363f, 0.2508f, 0.2662f,
    0.2826f, 0.2999f, 0.3183f, 0.3379f, 0.3586f, 0.3806f, 0.4040f, 0.4288f,
    0.4551f, 0.4830f, 0.5127f, 0.5441f, 0.5775f, 0.6130f, 0.6506f, 0.6905f,
    0.7329f, 0.7779f, 0.8256f, 0.8763f, 0.9301f, 0.9872f, 1.0478f, 1.1121f,
    1.1804f, 1.2528f, 1.3297f, 1.4113f, 1.4979f, 1.5899f, 1.6875f, 1.7910f,
    1.9010f, 2.0176f, 2.1415f, 2.2729f, 2.4124f, 2.5605f, 2.7176f, 2.8844f,
    3.0615f, 3.2494f, 3.4488f, 3.6605f, 3.8852f, 4.1236f, 4.3767f, 4.6453f,
    4.9305f, 5.2331f, 5.5543f, 5.8952f, 6.2570f, 6.6410f, 7.0486f, 7.4813f,
    7.9405f, 8.4278f, 8.9451f, 9.4941f, 10.076f, 10.695f, 11.352f, 12.048f,
    12.788f, 13.573f, 14.406f, 15.290f, 16.229f, 17.225f, 18.282f, 19.404f,
    20.595f, 21.859f, 23.201f, 24.625f, 26.136f, 27.740f, 29.443f, 31.250f
};

static float sf2_timecents_to_seconds(int16_t timecents) {
    if (timecents <= -32768) {
        return 0.0f;
    }
    return powf(2.0f, (float)timecents / 1200.0f);
}

static float sf2_cents_to_hz(int16_t cents) {
    return 8.176f * powf(2.0f, (float)cents / 1200.0f);
}

static uint8_t wf_time_from_seconds(float seconds) {
    if (seconds <= 0.0f) {
        return 0;
    }
    float best_diff = fabsf(wf_time_table[0] - seconds);
    uint8_t best_idx = 0;
    for (uint8_t i = 1; i < 128; i++) {
        float diff = fabsf(wf_time_table[i] - seconds);
        if (diff < best_diff) {
            best_diff = diff;
            best_idx = i;
        }
    }
    return best_idx;
}

static uint8_t wf_lfo_freq_from_hz(float hz) {
    if (hz <= 0.0f) {
        return 0;
    }
    float best_diff = fabsf(wf_lfo_freq_table[0] - hz);
    uint8_t best_idx = 0;
    for (uint8_t i = 1; i < 128; i++) {
        float diff = fabsf(wf_lfo_freq_table[i] - hz);
        if (diff < best_diff) {
            best_diff = diff;
            best_idx = i;
        }
    }
    return best_idx;
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

static int8_t cents_to_amount(int16_t cents, int16_t scale) {
    int value = cents / scale;
    if (value < -127) value = -127;
    if (value > 127) value = 127;
    return (int8_t)value;
}

static int8_t centibels_to_amount(int16_t centibels, int16_t scale) {
    int value = centibels / scale;
    if (value < -127) value = -127;
    if (value > 127) value = 127;
    return (int8_t)value;
}

/* Convert SF2 mod envelope sustain (centibels attenuation) to WaveFront level
 * SF2: 0 = no attenuation (full), 1000 = -100dB (silent)
 * WaveFront: 127 = full, 0 = silent
 * Note: SF2 spec section 8.1.3 defines sustainModEnv as centibels (0.1 dB) */
static int8_t mod_env_sustain_to_level(int16_t sustain_centibels) {
    if (sustain_centibels < 0) sustain_centibels = 0;
    if (sustain_centibels > 1000) sustain_centibels = 1000;
    /* Divide by 8 to convert centibels to ~WaveFront scale (same as vol env) */
    int level = 127 - (sustain_centibels / 8);
    if (level < 0) level = 0;
    if (level > 127) level = 127;
    return (int8_t)level;
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

struct Sf2GenState {
    int16_t mod_lfo_to_pitch;
    int16_t vib_lfo_to_pitch;
    int16_t mod_env_to_pitch;
    int16_t initial_filter_fc;
    int16_t initial_filter_q;   /* Filter resonance (centibels) */
    int16_t mod_lfo_to_filter_fc;
    int16_t mod_env_to_filter_fc;
    int16_t mod_lfo_to_volume;
    int16_t delay_mod_lfo;
    int16_t freq_mod_lfo;
    int16_t delay_vib_lfo;
    int16_t freq_vib_lfo;
    int16_t delay_mod_env;
    int16_t attack_mod_env;
    int16_t hold_mod_env;
    int16_t decay_mod_env;
    int16_t sustain_mod_env;
    int16_t release_mod_env;
    int16_t delay_vol_env;
    int16_t attack_vol_env;
    int16_t hold_vol_env;
    int16_t decay_vol_env;
    int16_t sustain_vol_env;
    int16_t release_vol_env;
    int16_t initial_attenuation;
    int16_t coarse_tune;
    int16_t fine_tune;
    int16_t pan;
    int16_t exclusive_class;
    int16_t chorus_send;
    int16_t reverb_send;
};

static void sf2_gen_defaults(struct Sf2GenState *state) {
    memset(state, 0, sizeof(*state));
    state->initial_filter_fc = 13500;  /* SF2 default: 13500 cents = ~8kHz */
    state->initial_filter_q = 0;       /* SF2 default: 0 centibels (no resonance) */
    state->delay_mod_lfo = -12000;
    state->freq_mod_lfo = 0;
    state->delay_vib_lfo = -12000;
    state->freq_vib_lfo = 0;
    state->delay_mod_env = -12000;
    state->attack_mod_env = -12000;
    state->hold_mod_env = -12000;
    state->decay_mod_env = -12000;
    state->sustain_mod_env = 0;
    state->release_mod_env = -12000;
    state->delay_vol_env = -12000;
    state->attack_vol_env = -12000;
    state->hold_vol_env = -12000;
    state->decay_vol_env = -12000;
    state->sustain_vol_env = 0;
    state->release_vol_env = -12000;
    state->initial_attenuation = 0;
    state->pan = 0;
    state->exclusive_class = 0;
    state->chorus_send = 0;
    state->reverb_send = 0;
}

/* Apply a single generator value to state (internal helper) */
static void sf2_apply_single_generator(struct Sf2GenState *state, uint16_t oper,
                                       int16_t val, int preset_relative) {
    /* Helper macro to apply value with optional relative mode */
    #define APPLY_GEN(field) \
        state->field = preset_relative ? (state->field + val) : val

    switch (oper) {
        case GEN_MOD_LFO_TO_PITCH:     APPLY_GEN(mod_lfo_to_pitch); break;
        case GEN_VIB_LFO_TO_PITCH:     APPLY_GEN(vib_lfo_to_pitch); break;
        case GEN_MOD_ENV_TO_PITCH:     APPLY_GEN(mod_env_to_pitch); break;
        case GEN_INITIAL_FILTER_FC:    APPLY_GEN(initial_filter_fc); break;
        case GEN_INITIAL_FILTER_Q:     APPLY_GEN(initial_filter_q); break;
        case GEN_MOD_LFO_TO_FILTER_FC: APPLY_GEN(mod_lfo_to_filter_fc); break;
        case GEN_MOD_ENV_TO_FILTER_FC: APPLY_GEN(mod_env_to_filter_fc); break;
        case GEN_MOD_LFO_TO_VOLUME:    APPLY_GEN(mod_lfo_to_volume); break;
        case GEN_DELAY_MOD_LFO:        APPLY_GEN(delay_mod_lfo); break;
        case GEN_FREQ_MOD_LFO:         APPLY_GEN(freq_mod_lfo); break;
        case GEN_DELAY_VIB_LFO:        APPLY_GEN(delay_vib_lfo); break;
        case GEN_FREQ_VIB_LFO:         APPLY_GEN(freq_vib_lfo); break;
        case GEN_DELAY_MOD_ENV:        APPLY_GEN(delay_mod_env); break;
        case GEN_ATTACK_MOD_ENV:       APPLY_GEN(attack_mod_env); break;
        case GEN_HOLD_MOD_ENV:         APPLY_GEN(hold_mod_env); break;
        case GEN_DECAY_MOD_ENV:        APPLY_GEN(decay_mod_env); break;
        case GEN_SUSTAIN_MOD_ENV:      APPLY_GEN(sustain_mod_env); break;
        case GEN_RELEASE_MOD_ENV:      APPLY_GEN(release_mod_env); break;
        case GEN_DELAY_VOL_ENV:        APPLY_GEN(delay_vol_env); break;
        case GEN_ATTACK_VOL_ENV:       APPLY_GEN(attack_vol_env); break;
        case GEN_HOLD_VOL_ENV:         APPLY_GEN(hold_vol_env); break;
        case GEN_DECAY_VOL_ENV:        APPLY_GEN(decay_vol_env); break;
        case GEN_SUSTAIN_VOL_ENV:      APPLY_GEN(sustain_vol_env); break;
        case GEN_RELEASE_VOL_ENV:      APPLY_GEN(release_vol_env); break;
        case GEN_INITIAL_ATTENUATION:  APPLY_GEN(initial_attenuation); break;
        case GEN_COARSE_TUNE:          APPLY_GEN(coarse_tune); break;
        case GEN_FINE_TUNE:            APPLY_GEN(fine_tune); break;
        case GEN_PAN:                  APPLY_GEN(pan); break;
        case GEN_CHORUS_EFFECTS_SEND:  APPLY_GEN(chorus_send); break;
        case GEN_REVERB_EFFECTS_SEND:  APPLY_GEN(reverb_send); break;
        case GEN_EXCLUSIVE_CLASS:      APPLY_GEN(exclusive_class); break;
        default: break;
    }
    #undef APPLY_GEN
}

/* Apply instrument generators to state */
static void sf2_apply_generators(struct Sf2GenState *state, struct sfInstGenList *gens,
                                 int gen_start, int gen_end, int preset_relative) {
    for (int i = gen_start; i < gen_end; i++) {
        sf2_apply_single_generator(state, gens[i].sfGenOper,
                                   gens[i].genAmount.shAmount, preset_relative);
    }
}

/* Apply preset generators to state */
static void sf2_apply_preset_generators(struct Sf2GenState *state, struct sfGenList *gens,
                                        int gen_start, int gen_end, int preset_relative) {
    for (int i = gen_start; i < gen_end; i++) {
        sf2_apply_single_generator(state, gens[i].sfGenOper,
                                   gens[i].genAmount.shAmount, preset_relative);
    }
}

static void apply_sf2_state_to_patch(struct PATCH *patch, const struct Sf2GenState *state) {
    float delay_vol_s = sf2_timecents_to_seconds(state->delay_vol_env);
    float attack_vol_s = sf2_timecents_to_seconds(state->attack_vol_env);
    float hold_vol_s = sf2_timecents_to_seconds(state->hold_vol_env);
    float decay_vol_s = sf2_timecents_to_seconds(state->decay_vol_env);
    float release_vol_s = sf2_timecents_to_seconds(state->release_vol_env);

    float delay_mod_s = sf2_timecents_to_seconds(state->delay_mod_env);
    float attack_mod_s = sf2_timecents_to_seconds(state->attack_mod_env);
    float hold_mod_s = sf2_timecents_to_seconds(state->hold_mod_env);
    float decay_mod_s = sf2_timecents_to_seconds(state->decay_mod_env);
    float release_mod_s = sf2_timecents_to_seconds(state->release_mod_env);

    patch->envelope2.fAttackTime = wf_time_from_seconds(delay_vol_s + attack_vol_s);
    patch->envelope2.fDecay1Time = wf_time_from_seconds(hold_vol_s);
    patch->envelope2.fDecay2Time = wf_time_from_seconds(decay_vol_s);
    patch->envelope2.fSustainTime = 0;
    patch->envelope2.fReleaseTime = wf_time_from_seconds(release_vol_s);
    patch->envelope2.fRelease2Time = 0;
    patch->envelope2.cAttackLevel = 127;
    patch->envelope2.cDecay1Level = 127;
    patch->envelope2.cDecay2Level = centibels_to_level(state->sustain_vol_env);
    patch->envelope2.cSustainLevel = centibels_to_level(state->sustain_vol_env);
    patch->envelope2.cReleaseLevel = 0;

    patch->envelope1.fAttackTime = wf_time_from_seconds(delay_mod_s + attack_mod_s);
    patch->envelope1.fDecay1Time = wf_time_from_seconds(hold_mod_s);
    patch->envelope1.fDecay2Time = wf_time_from_seconds(decay_mod_s);
    patch->envelope1.fSustainTime = 0;
    patch->envelope1.fReleaseTime = wf_time_from_seconds(release_mod_s);
    patch->envelope1.fRelease2Time = 0;
    patch->envelope1.cAttackLevel = 127;
    patch->envelope1.cDecay1Level = 127;
    patch->envelope1.cDecay2Level = mod_env_sustain_to_level(state->sustain_mod_env);
    patch->envelope1.cSustainLevel = mod_env_sustain_to_level(state->sustain_mod_env);
    patch->envelope1.cReleaseLevel = 0;

    int amp_bias = 127 - (state->initial_attenuation / 5);
    if (amp_bias > 127) amp_bias = 127;
    if (amp_bias < 0) amp_bias = 0;
    patch->fAmpBias = (uint8_t)amp_bias;

    int16_t total_cents = (state->coarse_tune * 100) + state->fine_tune;
    patch->nFreqBias = swap16((int16_t)total_cents);

    patch->lfo1.fFrequency = wf_lfo_freq_from_hz(sf2_cents_to_hz(state->freq_vib_lfo));
    patch->lfo1.fDelayTime = wf_time_from_seconds(sf2_timecents_to_seconds(state->delay_vib_lfo));
    patch->lfo1.cStartLevel = 0;
    patch->lfo1.cEndLevel = 127;
    patch->lfo1.fRampTime = 0;

    patch->lfo2.fFrequency = wf_lfo_freq_from_hz(sf2_cents_to_hz(state->freq_mod_lfo));
    patch->lfo2.fDelayTime = wf_time_from_seconds(sf2_timecents_to_seconds(state->delay_mod_lfo));
    patch->lfo2.cStartLevel = 0;
    patch->lfo2.cEndLevel = 127;
    patch->lfo2.fRampTime = 0;

    patch->fFMSource1 = 0;
    patch->cFMAmount1 = 0;
    patch->fFMSource2 = 0;
    patch->cFMAmount2 = 0;
    patch->fAMSource = 0;
    patch->cAMAmount = 0;
    patch->fFC1MSource = 0;
    patch->cFC1MAmount = 0;

    if (state->vib_lfo_to_pitch != 0) {
        patch->fFMSource1 = 0;
        patch->cFMAmount1 = cents_to_amount(state->vib_lfo_to_pitch, 10);
    }

    if (state->mod_lfo_to_pitch != 0) {
        patch->fFMSource2 = 1;
        patch->cFMAmount2 = cents_to_amount(state->mod_lfo_to_pitch, 10);
    }

    if (state->mod_env_to_pitch != 0 && state->vib_lfo_to_pitch == 0) {
        patch->fFMSource1 = 2;
        patch->cFMAmount1 = cents_to_amount(state->mod_env_to_pitch, 10);
    } else if (state->mod_env_to_pitch != 0 && state->mod_lfo_to_pitch == 0) {
        patch->fFMSource2 = 2;
        patch->cFMAmount2 = cents_to_amount(state->mod_env_to_pitch, 10);
    }

    if (state->mod_lfo_to_volume != 0) {
        patch->fAMSource = 1;
        patch->cAMAmount = centibels_to_amount(state->mod_lfo_to_volume, 5);
    }

    if (state->mod_env_to_filter_fc != 0) {
        patch->fFC1MSource = 2;
        patch->cFC1MAmount = cents_to_amount(state->mod_env_to_filter_fc, 100);
    } else if (state->mod_lfo_to_filter_fc != 0) {
        patch->fFC1MSource = 1;
        patch->cFC1MAmount = cents_to_amount(state->mod_lfo_to_filter_fc, 100);
    }

    /* Apply filter cutoff bias relative to SF2 default of 13500 cents (~8kHz) */
    {
        int bias = (state->initial_filter_fc - 13500) / 100;
        if (bias < -127) bias = -127;
        if (bias > 127) bias = 127;
        patch->cFC1FreqBias = (int8_t)bias;
    }

    /* Note: SF2 initial_filter_q (resonance) is tracked but cannot be mapped
     * because WaveFront's ICS2115 filter doesn't have a resonance parameter.
     * The filter only supports cutoff bias, modulation source/amount, and key scaling. */
    (void)state->initial_filter_q;  /* Suppress unused warning */
}

static uint8_t sf2_pan_to_wf(int16_t pan) {
    if (pan < -500) pan = -500;
    if (pan > 500) pan = 500;
    int value = ((pan + 500) * 7 + 500) / 1000;
    if (value < 0) value = 0;
    if (value > 7) value = 7;
    return (uint8_t)value;
}

static uint8_t sf2_atten_to_mixlevel(int16_t centibels) {
    int level = 127 - (centibels / 5);
    if (level < 0) level = 0;
    if (level > 127) level = 127;
    return (uint8_t)level;
}

static int zone_matches_key_inst(struct sfInstGenList *gens, int gen_start, int gen_end, int key) {
    for (int i = gen_start; i < gen_end; i++) {
        if (gens[i].sfGenOper == GEN_KEY_RANGE) {
            uint8_t lo = gens[i].genAmount.range.byLo;
            uint8_t hi = gens[i].genAmount.range.byHi;
            return key >= lo && key <= hi;
        }
    }
    return 1;
}

static int zone_matches_key_preset(struct sfGenList *gens, int gen_start, int gen_end, int key) {
    for (int i = gen_start; i < gen_end; i++) {
        if (gens[i].sfGenOper == GEN_KEY_RANGE) {
            uint8_t lo = gens[i].genAmount.range.byLo;
            uint8_t hi = gens[i].genAmount.range.byHi;
            return key >= lo && key <= hi;
        }
    }
    return 1;
}

static void get_key_range_inst(struct sfInstGenList *gens, int gen_start, int gen_end,
                               uint8_t *lo, uint8_t *hi) {
    *lo = 0;
    *hi = 127;
    for (int i = gen_start; i < gen_end; i++) {
        if (gens[i].sfGenOper == GEN_KEY_RANGE) {
            *lo = gens[i].genAmount.range.byLo;
            *hi = gens[i].genAmount.range.byHi;
            return;
        }
    }
}

static void get_key_range_preset(struct sfGenList *gens, int gen_start, int gen_end,
                                 uint8_t *lo, uint8_t *hi) {
    *lo = 0;
    *hi = 127;
    for (int i = gen_start; i < gen_end; i++) {
        if (gens[i].sfGenOper == GEN_KEY_RANGE) {
            *lo = gens[i].genAmount.range.byLo;
            *hi = gens[i].genAmount.range.byHi;
            return;
        }
    }
}

static void get_vel_range_inst(struct sfInstGenList *gens, int gen_start, int gen_end,
                               uint8_t *lo, uint8_t *hi) {
    *lo = 0;
    *hi = 127;
    for (int i = gen_start; i < gen_end; i++) {
        if (gens[i].sfGenOper == GEN_VEL_RANGE) {
            *lo = gens[i].genAmount.range.byLo;
            *hi = gens[i].genAmount.range.byHi;
            return;
        }
    }
}

static void get_vel_range_preset(struct sfGenList *gens, int gen_start, int gen_end,
                                 uint8_t *lo, uint8_t *hi) {
    *lo = 0;
    *hi = 127;
    for (int i = gen_start; i < gen_end; i++) {
        if (gens[i].sfGenOper == GEN_VEL_RANGE) {
            *lo = gens[i].genAmount.range.byLo;
            *hi = gens[i].genAmount.range.byHi;
            return;
        }
    }
}

static void apply_layer_split(struct LAYER *layer, uint8_t key_lo, uint8_t key_hi,
                              uint8_t vel_lo, uint8_t vel_hi) {
    layer->fSplitType = 0;
    layer->fSplitDir = 0;
    layer->fSplitPoint = 0;

    if (key_lo > 0 || key_hi < 127) {
        layer->fSplitType = 0; /* key split */
        if (key_lo > 0 && key_hi == 127) {
            layer->fSplitDir = 0;
            layer->fSplitPoint = key_lo;
        } else if (key_lo == 0 && key_hi < 127) {
            layer->fSplitDir = 1;
            layer->fSplitPoint = key_hi;
        } else {
            layer->fSplitDir = 0;
            layer->fSplitPoint = key_lo;
        }
        return;
    }

    if (vel_lo > 0 || vel_hi < 127) {
        layer->fSplitType = 1; /* velocity split */
        if (vel_lo > 0 && vel_hi == 127) {
            layer->fSplitDir = 0;
            layer->fSplitPoint = vel_lo;
        } else if (vel_lo == 0 && vel_hi < 127) {
            layer->fSplitDir = 1;
            layer->fSplitPoint = vel_hi;
        } else {
            layer->fSplitDir = 0;
            layer->fSplitPoint = vel_lo;
        }
    }
}

/* Count unique sample indices in O(n) using a bitset */
static int count_unique_samples(const int16_t *samples) {
    /* Bitset for tracking seen samples (512 bits = 64 bytes for WF_MAX_SAMPLES) */
    uint64_t seen[WF_MAX_SAMPLES / 64];
    int count = 0;

    memset(seen, 0, sizeof(seen));

    for (int i = 0; i < NUM_MIDIKEYS; i++) {
        int16_t val = samples[i];
        if (val < 0 || val >= WF_MAX_SAMPLES) {
            continue;
        }
        int word = val / 64;
        uint64_t bit = 1ULL << (val % 64);
        if (!(seen[word] & bit)) {
            seen[word] |= bit;
            count++;
        }
    }
    return count;
}

static int find_sample_in_inst_zone(struct SF2Bank *sf2, int gen_start, int gen_end) {
    for (int i = gen_start; i < gen_end; i++) {
        if (sf2->inst_gens[i].sfGenOper == GEN_SAMPLE_ID) {
            return sf2->inst_gens[i].genAmount.wAmount;
        }
    }
    return -1;
}

static int find_instrument_in_preset_zone(struct SF2Bank *sf2, int gen_start, int gen_end) {
    for (int i = gen_start; i < gen_end; i++) {
        if (sf2->preset_gens[i].sfGenOper == GEN_INSTRUMENT) {
            return sf2->preset_gens[i].genAmount.wAmount;
        }
    }
    return -1;
}

static int sf2_find_stereo_pair(struct SF2Bank *sf2, int sample_idx, int *left_idx, int *right_idx) {
    if (sample_idx < 0 || sample_idx >= sf2->sample_count) {
        return 0;
    }
    struct sfSample *samp = &sf2->samples[sample_idx];
    if (samp->sfSampleType != LEFT_SAMPLE && samp->sfSampleType != RIGHT_SAMPLE) {
        return 0;
    }
    int link_idx = samp->wSampleLink;
    if (link_idx < 0 || link_idx >= sf2->sample_count) {
        return 0;
    }
    struct sfSample *link = &sf2->samples[link_idx];
    if (samp->sfSampleType == LEFT_SAMPLE && link->sfSampleType != RIGHT_SAMPLE) {
        return 0;
    }
    if (samp->sfSampleType == RIGHT_SAMPLE && link->sfSampleType != LEFT_SAMPLE) {
        return 0;
    }
    uint32_t len_a = samp->dwEnd - samp->dwStart;
    uint32_t len_b = link->dwEnd - link->dwStart;
    if (len_a != len_b || samp->dwSampleRate != link->dwSampleRate) {
        return 0;
    }
    if (samp->sfSampleType == LEFT_SAMPLE) {
        *left_idx = sample_idx;
        *right_idx = link_idx;
    } else {
        *left_idx = link_idx;
        *right_idx = sample_idx;
    }
    return 1;
}

/* Add a sample to the WFB bank */
static int add_sample(struct WFBBank *wfb, struct SF2Bank *sf2, int sf2_sample_idx,
                      int *resampled_count, struct ConversionContext *ctx) {
    struct sfSample *sf2_samp;
    struct WaveFrontExtendedSampleInfo *info;
    struct SAMPLE temp_sample;
    struct ALIAS temp_alias;
    int16_t *sample_data;
    uint32_t sample_count, sample_rate;
    int wfb_idx;

    if (wfb->sample_count >= WF_MAX_SAMPLES) {
        return -1;  /* Sample limit reached */
    }

    if (sf2_sample_idx < 0 || sf2_sample_idx >= sf2->sample_count) {
        return -1;
    }

    if (ctx->sf2_sample_map && sf2_sample_idx < ctx->sf2_sample_map_count) {
        if (ctx->sf2_sample_map[sf2_sample_idx] >= 0) {
            return ctx->sf2_sample_map[sf2_sample_idx];
        }
    }

    sf2_samp = &sf2->samples[sf2_sample_idx];

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

    memset(&temp_sample, 0, sizeof(temp_sample));
    resample_set_sample_offset(&temp_sample.sampleStartOffset, 0.0, sample_count);
    resample_set_sample_offset(&temp_sample.sampleEndOffset, (double)sample_count, sample_count);

    /* Set loop points if present */
    if (sf2_samp->dwStartloop < sf2_samp->dwEndloop &&
        sf2_samp->dwStartloop >= sf2_samp->dwStart &&
        sf2_samp->dwEndloop <= sf2_samp->dwEnd) {

        uint32_t loop_start = sf2_samp->dwStartloop - sf2_samp->dwStart;
        uint32_t loop_end = sf2_samp->dwEndloop - sf2_samp->dwStart;

        resample_scale_loop_points(sf2_samp->dwSampleRate, sample_rate,
                                   loop_start, loop_end, sample_count,
                                   &temp_sample.loopStartOffset,
                                   &temp_sample.loopEndOffset);
        temp_sample.fLoop = 1;
    }

    /* nFrequencyBias must be big-endian for WaveFront hardware (Motorola 68000 based) */
    temp_sample.nFrequencyBias = swap16(cents_to_freq_bias(sf2_samp->chPitchCorrection));
    temp_sample.fSampleResolution = LINEAR_16BIT;

    uint32_t channel = WF_CH_MONO;
    if (sf2_samp->sfSampleType == LEFT_SAMPLE) {
        channel = WF_CH_LEFT;
    } else if (sf2_samp->sfSampleType == RIGHT_SAMPLE) {
        channel = WF_CH_RIGHT;
    }

    uint64_t data_hash = hash_pcm_data(sample_data, sample_count);

    for (int i = 0; i < wfb->sample_count; i++) {
        struct WaveFrontExtendedSampleInfo *existing_info = &wfb->samples[i].info;
        struct SAMPLE *existing_sample = &wfb->samples[i].data.sample;

        if (existing_info->nSampleType != WF_ST_SAMPLE) {
            continue;
        }
        if (existing_info->dwSampleRate != sample_rate ||
            existing_info->dwSizeInSamples != sample_count ||
            existing_info->nChannel != channel) {
            continue;
        }
        if (wfb->samples[i].data_hash != data_hash) {
            continue;
        }
        if (!sample_offsets_equal(&existing_sample->loopStartOffset, &temp_sample.loopStartOffset) ||
            !sample_offsets_equal(&existing_sample->loopEndOffset, &temp_sample.loopEndOffset) ||
            !sample_offsets_equal(&existing_sample->sampleStartOffset, &temp_sample.sampleStartOffset) ||
            !sample_offsets_equal(&existing_sample->sampleEndOffset, &temp_sample.sampleEndOffset) ||
            existing_sample->fLoop != temp_sample.fLoop ||
            existing_sample->nFrequencyBias != temp_sample.nFrequencyBias ||
            existing_sample->fSampleResolution != temp_sample.fSampleResolution) {
            continue;
        }
        if (memcmp(wfb->samples[i].pcm_data, sample_data,
                   sample_count * sizeof(int16_t)) != 0) {
            continue;
        }

        if (wfb->sample_count >= WF_MAX_SAMPLES) {
            free(sample_data);
            return -1;
        }

        wfb_idx = wfb->sample_count;
        info = &wfb->samples[wfb_idx].info;
        info->nSampleType = WF_ST_ALIAS;
        info->nNumber = wfb_idx;
        safe_string_copy(info->szName, sf2_samp->achSampleName, NAME_LENGTH);
        info->dwSampleRate = 0;
        info->dwSizeInSamples = 0;
        info->dwSizeInBytes = 0;
        info->nChannel = 0;

        memset(&temp_alias, 0, sizeof(temp_alias));
        temp_alias.nOriginalSample = i;
        temp_alias.sampleStartOffset = existing_sample->sampleStartOffset;
        temp_alias.loopStartOffset = existing_sample->loopStartOffset;
        temp_alias.loopEndOffset = existing_sample->loopEndOffset;
        temp_alias.sampleEndOffset = existing_sample->sampleEndOffset;
        temp_alias.nFrequencyBias = existing_sample->nFrequencyBias;
        temp_alias.fSampleResolution = existing_sample->fSampleResolution;
        temp_alias.fLoop = existing_sample->fLoop;
        temp_alias.fBidirectional = existing_sample->fBidirectional;
        temp_alias.fReverse = existing_sample->fReverse;

        wfb->samples[wfb_idx].data.alias = temp_alias;
        wfb->samples[wfb_idx].pcm_data = NULL;
        wfb->samples[wfb_idx].data_hash = 0;

        wfb->sample_count++;
        ctx->dedupe_alias_count++;
        if (ctx->sf2_sample_map && sf2_sample_idx < ctx->sf2_sample_map_count) {
            ctx->sf2_sample_map[sf2_sample_idx] = wfb_idx;
        }
        free(sample_data);
        return wfb_idx;
    }

    wfb_idx = wfb->sample_count;
    info = &wfb->samples[wfb_idx].info;
    info->nSampleType = WF_ST_SAMPLE;
    info->nNumber = wfb_idx;
    safe_string_copy(info->szName, sf2_samp->achSampleName, NAME_LENGTH);
    info->dwSampleRate = sample_rate;
    info->dwSizeInSamples = sample_count;
    info->dwSizeInBytes = sample_count * sizeof(int16_t);
    info->nChannel = channel;

    wfb->samples[wfb_idx].data.sample = temp_sample;

    /* Store PCM data */
    wfb->samples[wfb_idx].pcm_data = sample_data;
    wfb->samples[wfb_idx].data_hash = data_hash;

    /* Update totals */
    wfb->total_sample_memory += info->dwSizeInBytes;
    wfb->sample_count++;

    if (ctx->sf2_sample_map && sf2_sample_idx < ctx->sf2_sample_map_count) {
        ctx->sf2_sample_map[sf2_sample_idx] = wfb_idx;
    }

    return wfb_idx;
}

int add_multisample_entry(struct WFBBank *wfb, const int16_t *sample_numbers,
                          int16_t sample_count, const char *name) {
    if (!wfb || !sample_numbers) {
        return -1;
    }

    if (wfb->sample_count >= WF_MAX_SAMPLES) {
        return -1;
    }

    int wfb_idx = wfb->sample_count;
    struct WaveFrontExtendedSampleInfo *info = &wfb->samples[wfb_idx].info;
    struct MULTISAMPLE *ms = &wfb->samples[wfb_idx].data.multisample;

    memset(ms, 0, sizeof(*ms));
    ms->nNumberOfSamples = sample_count;
    for (int i = 0; i < NUM_MIDIKEYS; i++) {
        ms->nSampleNumber[i] = sample_numbers[i];
    }

    info->nSampleType = WF_ST_MULTISAMPLE;
    info->nNumber = wfb_idx;
    if (name) {
        safe_string_copy(info->szName, name, NAME_LENGTH);
    } else {
        snprintf(info->szName, NAME_LENGTH, "MS_%d", wfb_idx);
    }
    info->dwSampleRate = 0;
    info->dwSizeInSamples = 0;
    info->dwSizeInBytes = 0;
    info->nChannel = 0;

    wfb->samples[wfb_idx].pcm_data = NULL;
    wfb->samples[wfb_idx].data_hash = 0;

    wfb->sample_count++;
    return wfb_idx;
}

/* Convert a single SF2 preset to WFB program */
static int convert_preset(struct WFBBank *wfb, struct SF2Bank *sf2,
                         struct sfPresetHeader *preset, int prog_num,
                         int *resampled_count, struct ConversionContext *ctx) {
    struct WaveFrontProgram *wf_prog;
    struct WaveFrontPatch *wf_patch;
    int bag_start, bag_end;
    int layer_idx = 0;
    int i;
    int preset_global_gen_start = -1;
    int preset_global_gen_end = -1;
    struct ZoneDef {
        int inst_global_start;
        int inst_global_end;
        int inst_gen_start;
        int inst_gen_end;
        int preset_gen_start;
        int preset_gen_end;
        int inst_mod_start;
        int inst_mod_end;
        int preset_mod_start;
        int preset_mod_end;
        int sample_idx;
        uint8_t key_lo;
        uint8_t key_hi;
        uint8_t vel_lo;
        uint8_t vel_hi;
        uint8_t pan;
        struct PATCH patch_base;
    } zones[32];
    int zone_count = 0;
    struct GroupDef {
        struct PATCH patch_base;
        uint8_t pan;
        uint8_t vel_lo;
        uint8_t vel_hi;
        int inst_mod_start;
        int inst_mod_end;
        int preset_mod_start;
        int preset_mod_end;
        int16_t sample_idx[NUM_MIDIKEYS];
        int16_t sample_idx_r[NUM_MIDIKEYS];
        int has_stereo;
    } groups[16];
    int group_count = 0;
    int16_t preset_chorus_max = 0;
    int16_t preset_reverb_max = 0;

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

    for (i = bag_start; i < bag_end; i++) {
        struct sfPresetBag *bag = &sf2->preset_bags[i];
        int gen_start = bag->wGenNdx;
        int gen_end = (i + 1 < sf2->preset_bag_count) ?
                      sf2->preset_bags[i + 1].wGenNdx : sf2->preset_gen_count;
        int instrument_idx = -1;
        int preset_mod_start = bag->wModNdx;
        int preset_mod_end = (i + 1 < sf2->preset_bag_count) ?
                             sf2->preset_bags[i + 1].wModNdx : sf2->preset_mod_count;
        int j;

        /* Find instrument reference */
        for (j = gen_start; j < gen_end; j++) {
            if (sf2->preset_gens[j].sfGenOper == GEN_INSTRUMENT) {
                instrument_idx = sf2->preset_gens[j].genAmount.wAmount;
                break;
            }
        }

        if (instrument_idx < 0 || instrument_idx >= sf2->inst_count) {
            if (instrument_idx < 0 && preset_global_gen_start < 0) {
                preset_global_gen_start = gen_start;
                preset_global_gen_end = gen_end;
            }
            continue;  /* Skip global zone or invalid */
        }
        struct sfInst *inst = &sf2->instruments[instrument_idx];
        int inst_bag_start = inst->wInstBagNdx;
        int inst_bag_end = (instrument_idx + 1 < sf2->inst_count) ?
                          sf2->instruments[instrument_idx + 1].wInstBagNdx :
                          sf2->inst_bag_count;
        int inst_global_gen_start = -1;
        int inst_global_gen_end = -1;
        uint8_t preset_key_lo, preset_key_hi, preset_vel_lo, preset_vel_hi;

        get_key_range_preset(sf2->preset_gens, gen_start, gen_end,
                             &preset_key_lo, &preset_key_hi);
        get_vel_range_preset(sf2->preset_gens, gen_start, gen_end,
                             &preset_vel_lo, &preset_vel_hi);

        for (int ib = inst_bag_start; ib < inst_bag_end; ib++) {
            struct sfInstBag *inst_bag = &sf2->inst_bags[ib];
            int inst_gen_start = inst_bag->wInstGenNdx;
            int inst_gen_end = (ib + 1 < sf2->inst_bag_count) ?
                              sf2->inst_bags[ib + 1].wInstGenNdx :
                              sf2->inst_gen_count;
            int sample_idx = find_sample_in_inst_zone(sf2, inst_gen_start, inst_gen_end);
            uint8_t inst_key_lo, inst_key_hi, inst_vel_lo, inst_vel_hi;

            if (sample_idx < 0 && inst_global_gen_start < 0) {
                inst_global_gen_start = inst_gen_start;
                inst_global_gen_end = inst_gen_end;
                continue;
            }

            if (sample_idx < 0) {
                continue;
            }

            get_key_range_inst(sf2->inst_gens, inst_gen_start, inst_gen_end,
                               &inst_key_lo, &inst_key_hi);
            get_vel_range_inst(sf2->inst_gens, inst_gen_start, inst_gen_end,
                               &inst_vel_lo, &inst_vel_hi);

            uint8_t key_lo = (preset_key_lo > inst_key_lo) ? preset_key_lo : inst_key_lo;
            uint8_t key_hi = (preset_key_hi < inst_key_hi) ? preset_key_hi : inst_key_hi;
            uint8_t vel_lo = (preset_vel_lo > inst_vel_lo) ? preset_vel_lo : inst_vel_lo;
            uint8_t vel_hi = (preset_vel_hi < inst_vel_hi) ? preset_vel_hi : inst_vel_hi;

            if (key_lo > key_hi || vel_lo > vel_hi) {
                continue;
            }

            if (zone_count >= (int)(sizeof(zones) / sizeof(zones[0]))) {
                break;
            }

            {
                struct Sf2GenState gen_state;
                struct PATCH temp_patch;

                sf2_gen_defaults(&gen_state);
                if (inst_global_gen_start >= 0) {
                    sf2_apply_generators(&gen_state, sf2->inst_gens,
                                         inst_global_gen_start, inst_global_gen_end, 0);
                }
                sf2_apply_generators(&gen_state, sf2->inst_gens,
                                     inst_gen_start, inst_gen_end, 0);
                if (preset_global_gen_start >= 0) {
                    sf2_apply_preset_generators(&gen_state, sf2->preset_gens,
                                                preset_global_gen_start, preset_global_gen_end, 1);
                }
                sf2_apply_preset_generators(&gen_state, sf2->preset_gens,
                                            gen_start, gen_end, 1);

                init_default_patch(&temp_patch);
                apply_sf2_state_to_patch(&temp_patch, &gen_state);
                temp_patch.bySampleNumber = 0;
                temp_patch.fSampleMSB = 0;

                zones[zone_count].pan = sf2_pan_to_wf(gen_state.pan);
                zones[zone_count].patch_base = temp_patch;

                if (gen_state.chorus_send > preset_chorus_max) {
                    preset_chorus_max = gen_state.chorus_send;
                }
                if (gen_state.reverb_send > preset_reverb_max) {
                    preset_reverb_max = gen_state.reverb_send;
                }
            }

            zones[zone_count].inst_global_start = inst_global_gen_start;
            zones[zone_count].inst_global_end = inst_global_gen_end;
            zones[zone_count].inst_gen_start = inst_gen_start;
            zones[zone_count].inst_gen_end = inst_gen_end;
            zones[zone_count].preset_gen_start = gen_start;
            zones[zone_count].preset_gen_end = gen_end;
            zones[zone_count].inst_mod_start = inst_bag->wInstModNdx;
            zones[zone_count].inst_mod_end = (ib + 1 < sf2->inst_bag_count) ?
                                             sf2->inst_bags[ib + 1].wInstModNdx :
                                             sf2->inst_mod_count;
            zones[zone_count].preset_mod_start = preset_mod_start;
            zones[zone_count].preset_mod_end = preset_mod_end;
            zones[zone_count].sample_idx = sample_idx;
            zones[zone_count].key_lo = key_lo;
            zones[zone_count].key_hi = key_hi;
            zones[zone_count].vel_lo = vel_lo;
            zones[zone_count].vel_hi = vel_hi;
            zone_count++;
        }
    }

    for (int z = 0; z < zone_count; z++) {
        int left_idx = -1;
        int right_idx = -1;
        int sample_left = zones[z].sample_idx;
        int sample_right = -1;

        if (sf2_find_stereo_pair(sf2, zones[z].sample_idx, &left_idx, &right_idx)) {
            sample_left = left_idx;
            sample_right = right_idx;
        }

        int group_idx = -1;
        for (int g = 0; g < group_count; g++) {
            if (groups[g].pan == zones[z].pan &&
                groups[g].vel_lo == zones[z].vel_lo &&
                groups[g].vel_hi == zones[z].vel_hi &&
                patch_base_equal(&groups[g].patch_base, &zones[z].patch_base)) {
                group_idx = g;
                break;
            }
        }

        if (group_idx < 0) {
            if (group_count >= (int)(sizeof(groups) / sizeof(groups[0]))) {
                continue;
            }
            group_idx = group_count++;
            groups[group_idx].patch_base = zones[z].patch_base;
            groups[group_idx].pan = zones[z].pan;
            groups[group_idx].vel_lo = zones[z].vel_lo;
            groups[group_idx].vel_hi = zones[z].vel_hi;
            groups[group_idx].inst_mod_start = zones[z].inst_mod_start;
            groups[group_idx].inst_mod_end = zones[z].inst_mod_end;
            groups[group_idx].preset_mod_start = zones[z].preset_mod_start;
            groups[group_idx].preset_mod_end = zones[z].preset_mod_end;
            groups[group_idx].has_stereo = 0;
            for (int k = 0; k < NUM_MIDIKEYS; k++) {
                groups[group_idx].sample_idx[k] = -1;
                groups[group_idx].sample_idx_r[k] = -1;
            }
        }

        for (int key = zones[z].key_lo; key <= zones[z].key_hi; key++) {
            groups[group_idx].sample_idx[key] = sample_left;
            if (sample_right >= 0) {
                groups[group_idx].sample_idx_r[key] = sample_right;
                groups[group_idx].has_stereo = 1;
            }
        }
    }

    int dropped_groups = 0;
    int drop_reason = 0;
    for (int g = 0; g < group_count; g++) {
        if (layer_idx >= NUM_LAYERS) {
            dropped_groups = group_count - g;
            drop_reason = 1;
            break;
        }
        int patch_limit = WF_MAX_PATCHES - ctx->patch_reserve;
        if (patch_limit < 0) {
            patch_limit = 0;
        }
        if (wfb->patch_count >= patch_limit) {
            dropped_groups = group_count - g;
            drop_reason = 2;
            break;
        }

        int has_right = groups[g].has_stereo;
        if (has_right) {
            for (int k = 0; k < NUM_MIDIKEYS; k++) {
                if (groups[g].sample_idx[k] >= 0 &&
                    groups[g].sample_idx_r[k] < 0) {
                    has_right = 0;
                    break;
                }
            }
        }

        int16_t ms_numbers[NUM_MIDIKEYS];
        int16_t ms_numbers_r[NUM_MIDIKEYS];
        int mapped_keys = 0;
        int mapped_keys_r = 0;
        for (int k = 0; k < NUM_MIDIKEYS; k++) {
            ms_numbers[k] = -1;
            ms_numbers_r[k] = -1;
            if (groups[g].sample_idx[k] >= 0) {
                int wfb_idx = add_sample(wfb, sf2, groups[g].sample_idx[k], resampled_count, ctx);
                if (wfb_idx >= 0) {
                    ms_numbers[k] = (int16_t)wfb_idx;
                    mapped_keys++;
                }
            }
            if (has_right && groups[g].sample_idx_r[k] >= 0) {
                int wfb_idx = add_sample(wfb, sf2, groups[g].sample_idx_r[k], resampled_count, ctx);
                if (wfb_idx >= 0) {
                    ms_numbers_r[k] = (int16_t)wfb_idx;
                    mapped_keys_r++;
                }
            }
        }

        if (mapped_keys == 0) {
            continue;
        }

        int unique = count_unique_samples(ms_numbers);
        int need_multisample = (unique > 1) || (mapped_keys < NUM_MIDIKEYS);
        int16_t sample_ref = -1;
        if (need_multisample) {
            char ms_name[NAME_LENGTH];
            snprintf(ms_name, NAME_LENGTH, "%s_MS%d", preset->achPresetName, g);
            sample_ref = add_multisample_entry(wfb, ms_numbers, (int16_t)unique, ms_name);
        } else {
            for (int k = 0; k < NUM_MIDIKEYS; k++) {
                if (ms_numbers[k] >= 0) {
                    sample_ref = ms_numbers[k];
                    break;
                }
            }
        }

        if (sample_ref < 0) {
            continue;
        }

        wf_patch = &wfb->patches[wfb->patch_count];
        memset(wf_patch, 0, sizeof(*wf_patch));
        wf_patch->nNumber = wfb->patch_count;
        snprintf(wf_patch->szName, NAME_LENGTH, "%s_G%d",
                 preset->achPresetName, g);
        wf_patch->base = groups[g].patch_base;
        apply_modulator_list_to_patch(&wf_patch->base,
                                      (const struct sfModList *)sf2->inst_mods,
                                      groups[g].inst_mod_start, groups[g].inst_mod_end);
        apply_modulator_list_to_patch(&wf_patch->base,
                                      sf2->preset_mods,
                                      groups[g].preset_mod_start, groups[g].preset_mod_end);
        wf_patch->base.bySampleNumber = (uint8_t)sample_ref;

        struct LAYER *layer = &wf_prog->base.layer[layer_idx];
        layer->byPatchNumber = wfb->patch_count;
        layer->fMixLevel = 127;
        layer->fUnmute = 1;
        /* For stereo pairs, left channel gets hard-left (0); mono uses zone pan */
        layer->fPan = has_right ? 0 : groups[g].pan;
        apply_layer_split(layer, 0, 127, groups[g].vel_lo, groups[g].vel_hi);
        wfb->patch_count++;
        layer_idx++;

        if (has_right && layer_idx < NUM_LAYERS && wfb->patch_count < patch_limit) {
            int unique_r = count_unique_samples(ms_numbers_r);
            int need_multisample_r = (unique_r > 1) || (mapped_keys_r < NUM_MIDIKEYS);
            int16_t sample_ref_r = -1;
            if (need_multisample_r) {
                char ms_name_r[NAME_LENGTH];
                snprintf(ms_name_r, NAME_LENGTH, "%s_MS%dR", preset->achPresetName, g);
                sample_ref_r = add_multisample_entry(wfb, ms_numbers_r, (int16_t)unique_r, ms_name_r);
            } else {
                for (int k = 0; k < NUM_MIDIKEYS; k++) {
                    if (ms_numbers_r[k] >= 0) {
                        sample_ref_r = ms_numbers_r[k];
                        break;
                    }
                }
            }

            if (sample_ref_r >= 0) {
                struct WaveFrontPatch *wf_patch_r = &wfb->patches[wfb->patch_count];
                memset(wf_patch_r, 0, sizeof(*wf_patch_r));
                wf_patch_r->nNumber = wfb->patch_count;
                snprintf(wf_patch_r->szName, NAME_LENGTH, "%s_G%dR",
                         preset->achPresetName, g);
                wf_patch_r->base = groups[g].patch_base;
                apply_modulator_list_to_patch(&wf_patch_r->base,
                                              (const struct sfModList *)sf2->inst_mods,
                                              groups[g].inst_mod_start, groups[g].inst_mod_end);
                apply_modulator_list_to_patch(&wf_patch_r->base,
                                              sf2->preset_mods,
                                              groups[g].preset_mod_start, groups[g].preset_mod_end);
                wf_patch_r->base.bySampleNumber = (uint8_t)sample_ref_r;

                struct LAYER *layer_r = &wf_prog->base.layer[layer_idx];
                layer_r->byPatchNumber = wfb->patch_count;
                layer_r->fMixLevel = 127;
                layer_r->fUnmute = 1;
                layer_r->fPan = 7;
                apply_layer_split(layer_r, 0, 127, groups[g].vel_lo, groups[g].vel_hi);
                wfb->patch_count++;
                layer_idx++;
            }
        }
    }

    if (ctx->verbose && dropped_groups > 0) {
        if (drop_reason == 1) {
            fprintf(stderr,
                    "Warning: Program %d (\"%s\") has %d zone groups; only %d layers available. %d group(s) dropped.\n",
                    prog_num, preset->achPresetName, group_count, NUM_LAYERS, dropped_groups);
        } else if (drop_reason == 2) {
            fprintf(stderr,
                    "Warning: Program %d (\"%s\") dropped %d zone group(s) due to patch limit (%d).\n",
                    prog_num, preset->achPresetName, dropped_groups, WF_MAX_PATCHES - ctx->patch_reserve);
        }
    }

    if (ctx->verbose) {
        if (preset_chorus_max > 0) {
            fprintf(stderr,
                    "Notice: Program %d (\"%s\") chorus send up to %.1f%% (SF2).\n",
                    prog_num, preset->achPresetName, preset_chorus_max / 10.0f);
        }
        if (preset_reverb_max > 0) {
            fprintf(stderr,
                    "Notice: Program %d (\"%s\") reverb send up to %.1f%% (SF2).\n",
                    prog_num, preset->achPresetName, preset_reverb_max / 10.0f);
        }
    }

    wfb->program_count++;
    return 0;
}

static int convert_drumkit(struct WFBBank *wfb, struct SF2Bank *sf2,
                           struct sfPresetHeader *preset, int *resampled_count,
                           struct ConversionContext *ctx) {
    int bag_start, bag_end;
    int preset_global_gen_start = -1;
    int preset_global_gen_end = -1;

    if (wfb->has_drumkit) {
        return 0;
    }

    memset(&wfb->drumkit, 0, sizeof(wfb->drumkit));
    for (int i = 0; i < NUM_MIDIKEYS; i++) {
        wfb->drumkit.base.drum[i].byPatchNumber = 0;
        wfb->drumkit.base.drum[i].fMixLevel = 0;
        wfb->drumkit.base.drum[i].fUnmute = 0;
        wfb->drumkit.base.drum[i].fGroup = 0;
        wfb->drumkit.base.drum[i].fPanModSource = 0;
        wfb->drumkit.base.drum[i].fPanModulated = 0;
        wfb->drumkit.base.drum[i].fPanAmount = 4;
    }

    bag_start = preset->wPresetBagNdx;
    bag_end = (preset < &sf2->presets[sf2->preset_count - 1]) ?
              (preset + 1)->wPresetBagNdx : sf2->preset_bag_count;

    for (int i = bag_start; i < bag_end; i++) {
        struct sfPresetBag *bag = &sf2->preset_bags[i];
        int gen_start = bag->wGenNdx;
        int gen_end = (i + 1 < sf2->preset_bag_count) ?
                      sf2->preset_bags[i + 1].wGenNdx : sf2->preset_gen_count;
        int instrument_idx = find_instrument_in_preset_zone(sf2, gen_start, gen_end);

        if (instrument_idx < 0 || instrument_idx >= sf2->inst_count) {
            if (instrument_idx < 0 && preset_global_gen_start < 0) {
                preset_global_gen_start = gen_start;
                preset_global_gen_end = gen_end;
            }
            continue;
        }
    }

    for (int key = 35; key <= 81; key++) {
        int matched = 0;
        for (int i = bag_start; i < bag_end; i++) {
            struct sfPresetBag *bag = &sf2->preset_bags[i];
            int gen_start = bag->wGenNdx;
            int gen_end = (i + 1 < sf2->preset_bag_count) ?
                          sf2->preset_bags[i + 1].wGenNdx : sf2->preset_gen_count;
            int instrument_idx = find_instrument_in_preset_zone(sf2, gen_start, gen_end);

            if (instrument_idx < 0 || instrument_idx >= sf2->inst_count) {
                continue;
            }

            if (!zone_matches_key_preset(sf2->preset_gens, gen_start, gen_end, key)) {
                continue;
            }

            struct sfInst *inst = &sf2->instruments[instrument_idx];
            int inst_bag_start = inst->wInstBagNdx;
            int inst_bag_end = (instrument_idx + 1 < sf2->inst_count) ?
                              sf2->instruments[instrument_idx + 1].wInstBagNdx :
                              sf2->inst_bag_count;
            int inst_global_gen_start = -1;
            int inst_global_gen_end = -1;
            int inst_zone_gen_start = -1;
            int inst_zone_gen_end = -1;
            int sample_idx = -1;

            for (int ib = inst_bag_start; ib < inst_bag_end; ib++) {
                struct sfInstBag *inst_bag = &sf2->inst_bags[ib];
                int inst_gen_start = inst_bag->wInstGenNdx;
                int inst_gen_end = (ib + 1 < sf2->inst_bag_count) ?
                                  sf2->inst_bags[ib + 1].wInstGenNdx :
                                  sf2->inst_gen_count;
                int zone_sample = find_sample_in_inst_zone(sf2, inst_gen_start, inst_gen_end);

                if (zone_sample < 0 && inst_global_gen_start < 0) {
                    inst_global_gen_start = inst_gen_start;
                    inst_global_gen_end = inst_gen_end;
                    continue;
                }

                if (!zone_matches_key_inst(sf2->inst_gens, inst_gen_start, inst_gen_end, key)) {
                    continue;
                }

                if (zone_sample >= 0) {
                    inst_zone_gen_start = inst_gen_start;
                    inst_zone_gen_end = inst_gen_end;
                    sample_idx = zone_sample;
                    break;
                }
            }

            if (sample_idx < 0) {
                continue;
            }

            {
                int left_idx = -1;
                int right_idx = -1;
                if (sf2_find_stereo_pair(sf2, sample_idx, &left_idx, &right_idx)) {
                    sample_idx = left_idx;
                }
            }

            if (wfb->patch_count >= WF_MAX_PATCHES) {
                return -1;
            }

            struct WaveFrontPatch *wf_patch = &wfb->patches[wfb->patch_count];
            memset(wf_patch, 0, sizeof(*wf_patch));
            wf_patch->nNumber = wfb->patch_count;
            snprintf(wf_patch->szName, NAME_LENGTH, "Drum_%d", key);
            init_default_patch(&wf_patch->base);

            struct Sf2GenState gen_state;
            sf2_gen_defaults(&gen_state);
            if (inst_global_gen_start >= 0) {
                sf2_apply_generators(&gen_state, sf2->inst_gens,
                                     inst_global_gen_start, inst_global_gen_end, 0);
            }
            if (inst_zone_gen_start >= 0) {
                sf2_apply_generators(&gen_state, sf2->inst_gens,
                                     inst_zone_gen_start, inst_zone_gen_end, 0);
            }
            if (preset_global_gen_start >= 0) {
                sf2_apply_preset_generators(&gen_state, sf2->preset_gens,
                                            preset_global_gen_start, preset_global_gen_end, 1);
            }
            sf2_apply_preset_generators(&gen_state, sf2->preset_gens,
                                        gen_start, gen_end, 1);
            apply_sf2_state_to_patch(&wf_patch->base, &gen_state);

            if (gen_state.exclusive_class > 0) {
                wf_patch->base.fReuse = 1;
            }

            int wfb_sample_idx = add_sample(wfb, sf2, sample_idx, resampled_count, ctx);
            if (wfb_sample_idx >= 0) {
                wf_patch->base.bySampleNumber = wfb_sample_idx;
            }

            struct DRUM *drum = &wfb->drumkit.base.drum[key];
            drum->byPatchNumber = wfb->patch_count;
            drum->fMixLevel = sf2_atten_to_mixlevel(gen_state.initial_attenuation);
            drum->fUnmute = 1;
            if (gen_state.exclusive_class > 0) {
                int group = gen_state.exclusive_class;
                if (group > 15) group = 15;
                drum->fGroup = (uint8_t)group;
            } else {
                drum->fGroup = 0;
            }
            drum->fPanModSource = 0;
            drum->fPanModulated = 0;
            drum->fPanAmount = sf2_pan_to_wf(gen_state.pan);

            wfb->patch_count++;
            matched = 1;
            break;
        }

        if (!matched) {
            wfb->drumkit.base.drum[key].fUnmute = 0;
        }
    }

    wfb->has_drumkit = 1;
    return 0;
}

/* Main conversion function */
int convert_sf2_to_wfb(const char *input_file, const char *output_file,
                       struct ConversionOptions *opts) {
    struct SF2Bank sf2;
    struct WFBBank wfb;
    struct ConversionContext ctx;
    int i, resampled_count = 0;
    int discarded_samples = 0;
    uint32_t memory_limit;

    /* Open SF2 file */
    if (sf2_open(input_file, &sf2) != 0) {
        return -1;
    }

    /* Initialize conversion context */
    init_conversion_context(&ctx, sf2.sample_count, opts && opts->verbose);

    /* Initialize WFB bank */
    init_wfb_bank(&wfb, opts->device_name ? opts->device_name : "Maui");

    /* Convert Bank 0 (melodic programs 0-127) */
    if (!opts->drums_file) {
        struct sfPresetHeader *drums_probe = sf2_get_preset(&sf2, 128, 0);
        if (!drums_probe) {
            drums_probe = sf2_get_preset(&sf2, 0, 128);
        }
        if (drums_probe) {
            ctx.patch_reserve = 47; /* Reserve for Drumkit keys 35-81 */
        } else {
            ctx.patch_reserve = 0;
        }
    } else {
        ctx.patch_reserve = 0;
    }
    for (i = 0; i < 128; i++) {
        struct sfPresetHeader *preset = sf2_get_preset(&sf2, 0, i);
        if (preset) {
            if (convert_preset(&wfb, &sf2, preset, i, &resampled_count, &ctx) != 0) {
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
            if (convert_drumkit(&wfb, &sf2, drums, &resampled_count, &ctx) != 0) {
                fprintf(stderr, "Warning: Failed to convert drumkit\n");
            }
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
        free_conversion_context(&ctx);
        return -1;
    }

    printf("Conversion complete: '%s' -> '%s'\n", input_file, final_output);
    printf("  Programs: %d, Patches: %d, Samples: %d\n",
           wfb.program_count, wfb.patch_count, wfb.sample_count);
    if (ctx.dedupe_alias_count > 0) {
        printf("  Deduped samples (aliases): %d\n", ctx.dedupe_alias_count);
    }
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
    free_conversion_context(&ctx);

    return 0;
}
