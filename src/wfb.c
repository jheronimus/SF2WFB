/*
 * wfb.c - WaveFront Bank file I/O
 */

#include "../include/converter.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Forward declarations */
extern uint16_t swap16(uint16_t val);
extern void safe_string_copy(char *dest, const char *src, size_t dest_size);

/* Write WFB file */
int wfb_write(const char *filename, struct WFBBank *bank) {
    FILE *f;
    int i;
    uint32_t offset;

    f = fopen(filename, "wb");
    if (!f) {
        fprintf(stderr, "Error: Cannot create '%s'\n", filename);
        return -1;
    }

    /* Calculate offsets */
    offset = sizeof(struct WaveFrontFileHeader);

    bank->header.dwProgramOffset = offset;
    offset += bank->program_count * sizeof(struct WaveFrontProgram);

    bank->header.dwDrumkitOffset = bank->has_drumkit ? offset : 0;
    if (bank->has_drumkit) {
        offset += sizeof(struct WaveFrontDrumkit);
    }

    bank->header.dwPatchOffset = offset;
    offset += bank->patch_count * sizeof(struct WaveFrontPatch);

    bank->header.dwSampleOffset = offset;

    /* Write header */
    if (fwrite(&bank->header, sizeof(bank->header), 1, f) != 1) {
        fprintf(stderr, "Error: Failed to write header\n");
        fclose(f);
        return -1;
    }

    /* Write programs */
    if (bank->program_count > 0) {
        if (fwrite(bank->programs, sizeof(struct WaveFrontProgram),
                   bank->program_count, f) != (size_t)bank->program_count) {
            fprintf(stderr, "Error: Failed to write programs\n");
            fclose(f);
            return -1;
        }
    }

    /* Write drumkit */
    if (bank->has_drumkit) {
        if (fwrite(&bank->drumkit, sizeof(struct WaveFrontDrumkit), 1, f) != 1) {
            fprintf(stderr, "Error: Failed to write drumkit\n");
            fclose(f);
            return -1;
        }
    }

    /* Write patches */
    if (bank->patch_count > 0) {
        if (fwrite(bank->patches, sizeof(struct WaveFrontPatch),
                   bank->patch_count, f) != (size_t)bank->patch_count) {
            fprintf(stderr, "Error: Failed to write patches\n");
            fclose(f);
            return -1;
        }
    }

    /* Write samples */
    for (i = 0; i < bank->sample_count; i++) {
        struct WaveFrontExtendedSampleInfo *info = &bank->samples[i].info;
        char embedded_marker[MAX_PATH_LENGTH] = "EMBEDDED";
        uint32_t sample_struct_size = 0;

        /* Calculate size of this sample entry */
        info->dwSize = sizeof(struct WaveFrontExtendedSampleInfo);

        /* Add appropriate struct size */
        if (info->nSampleType == WF_ST_SAMPLE) {
            sample_struct_size = sizeof(struct SAMPLE);
        } else if (info->nSampleType == WF_ST_MULTISAMPLE) {
            sample_struct_size = sizeof(struct MULTISAMPLE);
        } else if (info->nSampleType == WF_ST_ALIAS) {
            sample_struct_size = sizeof(struct ALIAS);
        }

        info->dwSize += sample_struct_size;
        info->dwSize += MAX_PATH_LENGTH;  /* Filespec/marker */

        if (info->nSampleType == WF_ST_SAMPLE && bank->samples[i].pcm_data) {
            info->dwSize += info->dwSizeInBytes;  /* Embedded PCM data */
        }

        /* Write sample info header */
        if (fwrite(info, sizeof(*info), 1, f) != 1) {
            fprintf(stderr, "Error: Failed to write sample %d info\n", i);
            fclose(f);
            return -1;
        }

        /* Write sample struct */
        if (sample_struct_size > 0) {
            if (fwrite(&bank->samples[i].data, sample_struct_size, 1, f) != 1) {
                fprintf(stderr, "Error: Failed to write sample %d data struct\n", i);
                fclose(f);
                return -1;
            }
        }

        /* Write filespec (always "EMBEDDED" for our purposes) */
        if (fwrite(embedded_marker, MAX_PATH_LENGTH, 1, f) != 1) {
            fprintf(stderr, "Error: Failed to write sample %d filespec\n", i);
            fclose(f);
            return -1;
        }

        /* Write PCM data if embedded */
        if (info->nSampleType == WF_ST_SAMPLE && bank->samples[i].pcm_data) {
            if (fwrite(bank->samples[i].pcm_data, info->dwSizeInBytes, 1, f) != 1) {
                fprintf(stderr, "Error: Failed to write sample %d PCM data\n", i);
                fclose(f);
                return -1;
            }
        }
    }

    fclose(f);
    return 0;
}

/* Read WFB file */
int wfb_read(const char *filename, struct WFBBank *bank) {
    FILE *f;
    int i;

    memset(bank, 0, sizeof(*bank));

    f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "Error: Cannot open '%s'\n", filename);
        return -1;
    }

    /* Read header */
    if (fread(&bank->header, sizeof(bank->header), 1, f) != 1) {
        fprintf(stderr, "Error: Failed to read header\n");
        fclose(f);
        return -1;
    }

    /* Validate */
    if (bank->header.wVersion != WF_VERSION) {
        fprintf(stderr, "Warning: File version is %d, expected %d\n",
                bank->header.wVersion, WF_VERSION);
    }

    bank->program_count = bank->header.wProgramCount;
    bank->patch_count = bank->header.wPatchCount;
    bank->sample_count = bank->header.wSampleCount;
    bank->has_drumkit = bank->header.wDrumkitCount > 0;

    /* Read programs */
    if (bank->program_count > 0) {
        fseek(f, bank->header.dwProgramOffset, SEEK_SET);
        if (fread(bank->programs, sizeof(struct WaveFrontProgram),
                  bank->program_count, f) != (size_t)bank->program_count) {
            fprintf(stderr, "Error: Failed to read programs\n");
            fclose(f);
            return -1;
        }
    }

    /* Read drumkit */
    if (bank->has_drumkit) {
        fseek(f, bank->header.dwDrumkitOffset, SEEK_SET);
        if (fread(&bank->drumkit, sizeof(struct WaveFrontDrumkit), 1, f) != 1) {
            fprintf(stderr, "Error: Failed to read drumkit\n");
            fclose(f);
            return -1;
        }
    }

    /* Read patches */
    if (bank->patch_count > 0) {
        fseek(f, bank->header.dwPatchOffset, SEEK_SET);
        if (fread(bank->patches, sizeof(struct WaveFrontPatch),
                  bank->patch_count, f) != (size_t)bank->patch_count) {
            fprintf(stderr, "Error: Failed to read patches\n");
            fclose(f);
            return -1;
        }
    }

    /* Read samples (basic info only - skip PCM data for now) */
    if (bank->sample_count > 0) {
        fseek(f, bank->header.dwSampleOffset, SEEK_SET);
        for (i = 0; i < bank->sample_count; i++) {
            if (fread(&bank->samples[i].info, sizeof(struct WaveFrontExtendedSampleInfo),
                      1, f) != 1) {
                fprintf(stderr, "Error: Failed to read sample %d info\n", i);
                break;
            }
            uint32_t struct_bytes = 0;
            if (bank->samples[i].info.nSampleType == WF_ST_SAMPLE) {
                if (fread(&bank->samples[i].data.sample, sizeof(struct SAMPLE), 1, f) != 1) {
                    fprintf(stderr, "Error: Failed to read sample %d data struct\n", i);
                    break;
                }
                struct_bytes = sizeof(struct SAMPLE);
            } else if (bank->samples[i].info.nSampleType == WF_ST_MULTISAMPLE) {
                struct_bytes = sizeof(struct MULTISAMPLE);
            } else if (bank->samples[i].info.nSampleType == WF_ST_ALIAS) {
                struct_bytes = sizeof(struct ALIAS);
            }
            if (struct_bytes > 0) {
                fseek(f, struct_bytes + MAX_PATH_LENGTH, SEEK_CUR);
            } else if (bank->samples[i].info.nSampleType == WF_ST_MULTISAMPLE) {
                /* No-op */
            }
            /* Skip the rest of the sample data */
            uint32_t skip_size = bank->samples[i].info.dwSize -
                                 sizeof(struct WaveFrontExtendedSampleInfo);
            if (skip_size > struct_bytes + MAX_PATH_LENGTH) {
                fseek(f, skip_size - (struct_bytes + MAX_PATH_LENGTH), SEEK_CUR);
            } else if (skip_size > 0) {
                fseek(f, skip_size, SEEK_CUR);
            }
        }
    }

    fclose(f);
    return 0;
}

/* Print WFB file information */
void wfb_print_info(struct WFBBank *bank) {
    printf("\n=== WaveFront Bank Information ===\n");
    printf("Synth Name:      %s\n", bank->header.szSynthName);
    printf("File Type:       %s\n", bank->header.szFileType);
    printf("Version:         %.2f\n", bank->header.wVersion / 100.0);
    printf("\n--- Counts ---\n");
    printf("Programs:        %d\n", bank->header.wProgramCount);
    printf("Patches:         %d\n", bank->header.wPatchCount);
    printf("Samples:         %d\n", bank->header.wSampleCount);
    printf("Drumkits:        %d\n", bank->header.wDrumkitCount);
    printf("\n--- Memory ---\n");
    printf("RAM Required:    %u bytes (%.2f MB)\n",
           bank->header.dwMemoryRequired,
           bank->header.dwMemoryRequired / (1024.0 * 1024.0));
    printf("Embedded:        %s\n", bank->header.bEmbeddedSamples ? "Yes" : "No");
    printf("\n--- Offsets ---\n");
    printf("Programs:        0x%08X\n", bank->header.dwProgramOffset);
    printf("Drumkit:         0x%08X\n", bank->header.dwDrumkitOffset);
    printf("Patches:         0x%08X\n", bank->header.dwPatchOffset);
    printf("Samples:         0x%08X\n", bank->header.dwSampleOffset);

    if (bank->header.szComment[0]) {
        printf("\n--- Comment ---\n%s\n", bank->header.szComment);
    }
    printf("==================================\n\n");
}

/* Update device name in existing WFB file */
int wfb_retarget(const char *filename, const char *new_device) {
    struct WFBBank bank;

    if (wfb_read(filename, &bank) != 0) {
        return -1;
    }

    safe_string_copy(bank.header.szSynthName, new_device, NAME_LENGTH);

    if (wfb_write(filename, &bank) != 0) {
        return -1;
    }

    printf("Updated '%s' target device to '%s'.\n", filename, new_device);
    return 0;
}
