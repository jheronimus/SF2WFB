/*
 * resample.c - Linear interpolation resampling for audio data
 */

#include "../include/converter.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* Linear interpolation between two samples */
static inline int16_t lerp(int16_t a, int16_t b, float t) {
    return (int16_t)(a + t * (b - a));
}

void resample_set_sample_offset(struct SAMPLE_OFFSET *offset, double pos,
                                uint32_t max_samples) {
    if (!offset) {
        return;
    }
    if (pos < 0.0) {
        pos = 0.0;
    }
    if (pos > (double)max_samples) {
        pos = (double)max_samples;
    }
    double int_part = floor(pos);
    double frac = pos - int_part;
    int frac_steps = (int)lround(frac * 16.0);
    if (frac_steps >= 16) {
        int_part += 1.0;
        frac_steps = 0;
    }
    if (int_part > (double)max_samples) {
        int_part = (double)max_samples;
        frac_steps = 0;
    }
    offset->fInteger = (uint32_t)int_part;
    offset->fFraction = (uint32_t)frac_steps;
    offset->fUnused = 0;
}

void resample_scale_loop_points(uint32_t input_rate, uint32_t output_rate,
                                uint32_t input_loop_start, uint32_t input_loop_end,
                                uint32_t output_samples,
                                struct SAMPLE_OFFSET *out_start,
                                struct SAMPLE_OFFSET *out_end) {
    double ratio;
    double start_pos;
    double end_pos;

    if (input_rate == 0 || output_rate == 0) {
        resample_set_sample_offset(out_start, 0.0, output_samples);
        resample_set_sample_offset(out_end, (double)output_samples, output_samples);
        return;
    }

    ratio = (double)output_rate / (double)input_rate;
    start_pos = (double)input_loop_start * ratio;
    end_pos = (double)input_loop_end * ratio;

    resample_set_sample_offset(out_start, start_pos, output_samples);
    resample_set_sample_offset(out_end, end_pos, output_samples);
}

/*
 * Resample audio data using linear interpolation
 * Returns newly allocated buffer (caller must free)
 */
int16_t *resample_linear(int16_t *input, uint32_t input_samples,
                         uint32_t input_rate, uint32_t output_rate,
                         uint32_t *output_samples) {
    int16_t *output;
    uint32_t i;
    float ratio;
    float position;

    if (!input || !output_samples || input_rate == 0 || output_rate == 0) {
        return NULL;
    }

    /* No resampling needed */
    if (input_rate == output_rate) {
        *output_samples = input_samples;
        output = malloc(input_samples * sizeof(int16_t));
        if (!output) {
            return NULL;
        }
        memcpy(output, input, input_samples * sizeof(int16_t));
        return output;
    }

    /* Calculate output size */
    ratio = (float)input_rate / (float)output_rate;
    *output_samples = (uint32_t)(input_samples / ratio);

    /* Allocate output buffer */
    output = malloc(*output_samples * sizeof(int16_t));
    if (!output) {
        return NULL;
    }

    /* Perform linear interpolation */
    for (i = 0; i < *output_samples; i++) {
        position = i * ratio;
        uint32_t index = (uint32_t)position;
        float frac = position - index;

        if (index + 1 < input_samples) {
            /* Interpolate between two samples */
            output[i] = lerp(input[index], input[index + 1], frac);
        } else {
            /* Last sample - no interpolation */
            output[i] = input[index < input_samples ? index : input_samples - 1];
        }
    }

    return output;
}

/*
 * Downsample to 44.1kHz if needed
 * Returns 1 if resampling occurred, 0 if not needed, -1 on error
 */
int resample_to_44100_if_needed(int16_t **data, uint32_t *sample_count,
                                 uint32_t *sample_rate) {
    int16_t *resampled;
    uint32_t new_count;

    if (*sample_rate <= 44100) {
        return 0; /* No resampling needed */
    }

    printf("Warning: Resampling audio from %u Hz to 44100 Hz. "
           "This may result in reduced sound quality compared to the original SoundFont.\n",
           *sample_rate);

    resampled = resample_linear(*data, *sample_count, *sample_rate, 44100, &new_count);
    if (!resampled) {
        fprintf(stderr, "Error: Failed to resample audio data\n");
        return -1;
    }

    /* Replace original data */
    free(*data);
    *data = resampled;
    *sample_count = new_count;
    *sample_rate = 44100;

    return 1;
}
