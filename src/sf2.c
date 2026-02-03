/*
 * sf2.c - SoundFont 2 file parser
 */

#include "../include/converter.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Forward declarations */
extern uint16_t swap16(uint16_t val);
extern uint32_t swap32(uint32_t val);

/* Read a RIFF chunk header */
static int read_chunk(FILE *f, struct RIFFChunk *chunk) {
    if (fread(chunk->chunkID, 1, 4, f) != 4) return -1;
    if (fread(&chunk->chunkSize, 4, 1, f) != 1) return -1;
    return 0;
}

/* Find and seek to a specific LIST chunk */
static int find_list_chunk(FILE *f, const char *list_type, uint32_t *size) {
    struct RIFFChunk chunk;
    char list_id[4];
    long start_pos = ftell(f);

    while (read_chunk(f, &chunk) == 0) {
        if (memcmp(chunk.chunkID, "LIST", 4) == 0) {
            /* Read LIST type */
            if (fread(list_id, 1, 4, f) != 4) {
                long skip = chunk.chunkSize - 4;
                fseek(f, skip, SEEK_CUR);
                /* Skip padding byte if chunk size is odd */
                if (chunk.chunkSize & 1) {
                    fseek(f, 1, SEEK_CUR);
                }
                continue;
            }
            if (memcmp(list_id, list_type, 4) == 0) {
                *size = chunk.chunkSize - 4;  /* Subtract LIST type */
                return 0;
            }
            /* Skip rest of this LIST */
            long skip = chunk.chunkSize - 4;
            fseek(f, skip, SEEK_CUR);
            /* Skip padding byte if chunk size is odd */
            if (chunk.chunkSize & 1) {
                fseek(f, 1, SEEK_CUR);
            }
        } else {
            /* Skip non-LIST chunk */
            fseek(f, chunk.chunkSize, SEEK_CUR);
            /* Skip padding byte if chunk size is odd */
            if (chunk.chunkSize & 1) {
                fseek(f, 1, SEEK_CUR);
            }
        }
    }

    fseek(f, start_pos, SEEK_SET);
    return -1;
}

/* Parse the pdta (preset data) section */
static int parse_hydra(FILE *f, struct SF2Bank *bank) {
    struct RIFFChunk chunk;
    uint32_t pdta_size;
    long pdta_start;

    /* Find pdta LIST */
    if (find_list_chunk(f, "pdta", &pdta_size) != 0) {
        fprintf(stderr, "Error: 'pdta' LIST chunk not found\n");
        return -1;
    }

    pdta_start = ftell(f);

    /* Read each sub-chunk */
    while (ftell(f) < pdta_start + pdta_size) {
        if (read_chunk(f, &chunk) != 0) break;

        if (memcmp(chunk.chunkID, "phdr", 4) == 0) {
            bank->preset_count = chunk.chunkSize / sizeof(struct sfPresetHeader) - 1;
            bank->presets = malloc(chunk.chunkSize);
            fread(bank->presets, chunk.chunkSize, 1, f);
        }
        else if (memcmp(chunk.chunkID, "pbag", 4) == 0) {
            bank->preset_bag_count = chunk.chunkSize / sizeof(struct sfPresetBag) - 1;
            bank->preset_bags = malloc(chunk.chunkSize);
            fread(bank->preset_bags, chunk.chunkSize, 1, f);
        }
        else if (memcmp(chunk.chunkID, "pmod", 4) == 0) {
            bank->preset_mod_count = chunk.chunkSize / sizeof(struct sfModList) - 1;
            bank->preset_mods = malloc(chunk.chunkSize);
            fread(bank->preset_mods, chunk.chunkSize, 1, f);
        }
        else if (memcmp(chunk.chunkID, "pgen", 4) == 0) {
            bank->preset_gen_count = chunk.chunkSize / sizeof(struct sfGenList) - 1;
            bank->preset_gens = malloc(chunk.chunkSize);
            fread(bank->preset_gens, chunk.chunkSize, 1, f);
        }
        else if (memcmp(chunk.chunkID, "inst", 4) == 0) {
            bank->inst_count = chunk.chunkSize / sizeof(struct sfInst) - 1;
            bank->instruments = malloc(chunk.chunkSize);
            fread(bank->instruments, chunk.chunkSize, 1, f);
        }
        else if (memcmp(chunk.chunkID, "ibag", 4) == 0) {
            bank->inst_bag_count = chunk.chunkSize / sizeof(struct sfInstBag) - 1;
            bank->inst_bags = malloc(chunk.chunkSize);
            fread(bank->inst_bags, chunk.chunkSize, 1, f);
        }
        else if (memcmp(chunk.chunkID, "imod", 4) == 0) {
            bank->inst_mod_count = chunk.chunkSize / sizeof(struct sfInstModList) - 1;
            bank->inst_mods = malloc(chunk.chunkSize);
            fread(bank->inst_mods, chunk.chunkSize, 1, f);
        }
        else if (memcmp(chunk.chunkID, "igen", 4) == 0) {
            bank->inst_gen_count = chunk.chunkSize / sizeof(struct sfInstGenList) - 1;
            bank->inst_gens = malloc(chunk.chunkSize);
            fread(bank->inst_gens, chunk.chunkSize, 1, f);
        }
        else if (memcmp(chunk.chunkID, "shdr", 4) == 0) {
            bank->sample_count = chunk.chunkSize / sizeof(struct sfSample) - 1;
            bank->samples = malloc(chunk.chunkSize);
            fread(bank->samples, chunk.chunkSize, 1, f);
        }
        else {
            /* Skip unknown chunk */
            fseek(f, chunk.chunkSize, SEEK_CUR);
        }

        /* Skip padding byte if chunk size is odd (RIFF word alignment) */
        if (chunk.chunkSize & 1) {
            fseek(f, 1, SEEK_CUR);
        }
    }

    return 0;
}

/* Parse the sdta (sample data) section */
static int parse_sample_data(FILE *f, struct SF2Bank *bank) {
    struct RIFFChunk chunk;
    uint32_t sdta_size;

    /* Find sdta LIST */
    if (find_list_chunk(f, "sdta", &sdta_size) != 0) {
        fprintf(stderr, "Error: 'sdta' LIST chunk not found\n");
        return -1;
    }

    /* Look for smpl chunk */
    if (read_chunk(f, &chunk) != 0) return -1;

    if (memcmp(chunk.chunkID, "smpl", 4) == 0) {
        bank->sample_data_size = chunk.chunkSize;
        bank->sample_data = malloc(chunk.chunkSize);
        if (!bank->sample_data) {
            fprintf(stderr, "Error: Failed to allocate sample data buffer\n");
            return -1;
        }
        if (fread(bank->sample_data, chunk.chunkSize, 1, f) != 1) {
            fprintf(stderr, "Error: Failed to read sample data\n");
            free(bank->sample_data);
            bank->sample_data = NULL;
            return -1;
        }
        /* Skip padding byte if chunk size is odd (RIFF word alignment) */
        if (chunk.chunkSize & 1) {
            fseek(f, 1, SEEK_CUR);
        }
    }

    return 0;
}

/* Open and parse an SF2 file */
int sf2_open(const char *filename, struct SF2Bank *bank) {
    struct RIFFChunk riff;
    char form_type[4];

    memset(bank, 0, sizeof(*bank));

    /* Open file */
    bank->file = fopen(filename, "rb");
    if (!bank->file) {
        fprintf(stderr, "Error: Cannot open '%s'\n", filename);
        return -1;
    }

    /* Read RIFF header */
    if (read_chunk(bank->file, &riff) != 0) {
        fprintf(stderr, "Error: Not a valid RIFF file\n");
        goto error;
    }

    if (memcmp(riff.chunkID, "RIFF", 4) != 0) {
        fprintf(stderr, "Error: Not a valid RIFF file\n");
        goto error;
    }

    /* Check form type */
    if (fread(form_type, 1, 4, bank->file) != 4) {
        goto error;
    }

    if (memcmp(form_type, "sfbk", 4) != 0) {
        fprintf(stderr, "Error: Not a valid SF2 file\n");
        goto error;
    }

    /* Rewind to start of data chunks (after "RIFF" header) */
    fseek(bank->file, 12, SEEK_SET);

    /* Parse sample data first */
    if (parse_sample_data(bank->file, bank) != 0) {
        goto error;
    }

    /* Rewind and parse hydra */
    fseek(bank->file, 12, SEEK_SET);
    if (parse_hydra(bank->file, bank) != 0) {
        goto error;
    }

    return 0;

error:
    sf2_close(bank);
    return -1;
}

/* Close and free SF2 bank data */
void sf2_close(struct SF2Bank *bank) {
    if (bank->file) {
        fclose(bank->file);
        bank->file = NULL;
    }

    free(bank->presets);
    free(bank->preset_bags);
    free(bank->preset_mods);
    free(bank->preset_gens);
    free(bank->instruments);
    free(bank->inst_bags);
    free(bank->inst_mods);
    free(bank->inst_gens);
    free(bank->samples);
    free(bank->sample_data);

    memset(bank, 0, sizeof(*bank));
}

/* Get preset by bank and program number */
struct sfPresetHeader *sf2_get_preset(struct SF2Bank *bank, int bank_num, int preset_num) {
    int i;
    for (i = 0; i < bank->preset_count; i++) {
        if (bank->presets[i].wBank == bank_num &&
            bank->presets[i].wPreset == preset_num) {
            return &bank->presets[i];
        }
    }
    return NULL;
}

/* Get first preset from bank */
struct sfPresetHeader *sf2_get_first_preset(struct SF2Bank *bank) {
    if (bank->preset_count > 0) {
        return &bank->presets[0];
    }
    return NULL;
}
