/*
 * resample.c - Linear interpolation resampling for audio data
 */

#include "../include/converter.h"
#include <stdlib.h>
#include <string.h>

/* Linear interpolation between two samples */
static inline int16_t lerp(int16_t a, int16_t b, float t) {
    return (int16_t)(a + t * (b - a));
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
