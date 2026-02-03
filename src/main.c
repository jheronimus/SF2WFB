/*
 * main.c - SF2WFB command-line interface
 *
 * SF2WFB - SoundFont 2 to WaveFront Bank Converter
 * Copyright (C) 2026
 */

#include "../include/converter.h"
#include "../include/viability.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <glob.h>

/* Forward declarations */
extern int is_valid_device_name(const char *name);
extern const char *normalize_device_name(const char *name);
extern int wfb_retarget(const char *filename, const char *new_device);

/* Usage information */
static void print_usage(const char *prog_name) {
    printf("SF2WFB - SoundFont 2 to WaveFront Bank Converter\n\n");
    printf("Usage: %s [options] <input_files...>\n\n", prog_name);
    printf("Options:\n");
    printf("  -d, --device <name>      Target device (Maui, Rio, Tropez, TBS-2001)\n");
    printf("                           Default: Maui\n");
    printf("  -D, --drums <path>       Use specified SF2 file for drum kit\n");
    printf("  -p, --patch <file>:<id>  Replace program ID with preset from file\n");
    printf("                           Can be used multiple times\n");
    printf("  -o, --output <path>      Output filename (single file only)\n");
    printf("  -v, --verbose            Enable verbose warnings and detailed assessment\n");
    printf("  -y, --yes                Skip assessment prompt, always proceed\n");
    printf("      --no-assess          Skip viability assessment entirely\n");
    printf("  -h, --help               Show this help message\n");
    printf("\nExamples:\n");
    printf("  %s piano.sf2\n", prog_name);
    printf("  %s -d Tropez *.sf2\n", prog_name);
    printf("  %s -D drums.sf2 melodic.sf2\n", prog_name);
    printf("  %s -p violin.sf2:40 orchestra.sf2\n", prog_name);
    printf("  %s -o custom.wfb bank.sf2\n", prog_name);
    printf("\nFile Operations:\n");
    printf("  .sf2 input:  Conversion mode\n");
    printf("  .wfb input:  Verification/modification mode\n");
    printf("\n");
}

/* Check if file has extension */
static int has_extension(const char *filename, const char *ext) {
    size_t len = strlen(filename);
    size_t ext_len = strlen(ext);

    if (len < ext_len) {
        return 0;
    }

    return strcasecmp(filename + len - ext_len, ext) == 0;
}

/* Get output filename from input filename, preserving extension case */
static const char *get_output_filename(const char *input, const char *explicit_output) {
    static char result[512];
    const char *dot;
    const char *ext;
    char wfb_ext[8];
    int i;

    if (explicit_output) {
        return explicit_output;
    }

    /* Find extension */
    dot = strrchr(input, '.');
    if (dot) {
        size_t base_len = dot - input;
        strncpy(result, input, base_len);
        result[base_len] = '\0';

        ext = dot + 1;  /* Point to extension (without dot) */

        /* Match case of input extension for output */
        /* Check if input is .sf2 or .SF2 (case variants) */
        if (strcasecmp(ext, "sf2") == 0) {
            /* Preserve the case pattern: sf2→wfb, SF2→WFB, Sf2→Wfb, sF2→wFb, etc. */
            const char *wfb_lower = "wfb";
            const char *wfb_upper = "WFB";
            int all_upper = 1, all_lower = 1;

            /* Check if entire extension is one case */
            for (i = 0; i < 3 && ext[i]; i++) {
                if (ext[i] >= 'a' && ext[i] <= 'z') all_upper = 0;
                if (ext[i] >= 'A' && ext[i] <= 'Z') all_lower = 0;
            }

            wfb_ext[0] = '.';
            if (all_upper) {
                /* SF2 → WFB */
                strcpy(wfb_ext + 1, wfb_upper);
            } else if (all_lower) {
                /* sf2 → wfb */
                strcpy(wfb_ext + 1, wfb_lower);
            } else {
                /* Mixed case: match each character position */
                for (i = 0; i < 3 && ext[i]; i++) {
                    if (ext[i] >= 'A' && ext[i] <= 'Z') {
                        wfb_ext[i + 1] = wfb_upper[i];
                    } else {
                        wfb_ext[i + 1] = wfb_lower[i];
                    }
                }
                wfb_ext[4] = '\0';
            }
        } else {
            /* For non-sf2 extensions, use lowercase .wfb */
            strcpy(wfb_ext, ".wfb");
        }

        strcat(result, wfb_ext);
    } else {
        strcpy(result, input);
        strcat(result, ".wfb");
    }

    return result;
}

/* Process a single file */
static int process_file(const char *filename, struct ConversionOptions *opts,
                       int assess, int interactive,
                       int *converted, int *failed) {
    int result = 0;

    if (has_extension(filename, ".sf2")) {
        /* Conversion mode */
        const char *output = get_output_filename(filename, opts->output_file);
        const char *device = opts->device_name ? opts->device_name : "Maui";

        /* Viability assessment */
        if (assess) {
            struct ViabilityReport report;
            struct ViabilityConfig config = {
                .verbose = opts->verbose,
                .interactive = interactive,
                .auto_yes = !interactive
            };

            printf("Assessing conversion viability for: %s\n\n", filename);

            if (assess_sf2_viability(filename, &report, &config) != 0) {
                fprintf(stderr, "Error: Assessment failed\n");
                (*failed)++;
                return -1;
            }

            /* Print report */
            if (opts->verbose) {
                print_viability_verbose(&report);
            } else {
                print_viability_summary(&report);
            }

            /* Prompt user if interactive and warnings exist */
            if (interactive && report.warning_count > 0) {
                if (!prompt_user_proceed(&report)) {
                    printf("Conversion cancelled.\n");
                    free_viability_report(&report);
                    return 0;  /* Not a failure, user chose to cancel */
                }
            }

            free_viability_report(&report);
            printf("\n");
        }

        /* Proceed with conversion */
        printf("Converting: %s -> %s\n", filename, output);

        /* Set device name for conversion if not specified */
        struct ConversionOptions conv_opts = *opts;
        conv_opts.device_name = device;

        if (convert_sf2_to_wfb(filename, output, &conv_opts) == 0) {
            (*converted)++;
        } else {
            fprintf(stderr, "Error: Failed to convert '%s'\n", filename);
            (*failed)++;
            result = -1;
        }
    }
    else if (has_extension(filename, ".wfb")) {
        /* Verification or modification mode */
        if (opts->device_name) {
            /* Retarget mode */
            if (wfb_retarget(filename, opts->device_name) == 0) {
                (*converted)++;
            } else {
                (*failed)++;
                result = -1;
            }
        } else {
            /* Verification mode */
            struct WFBBank bank;
            if (wfb_read(filename, &bank) == 0) {
                wfb_print_info(&bank);
                (*converted)++;
            } else {
                (*failed)++;
                result = -1;
            }
        }
    }
    else {
        fprintf(stderr, "Error: Unknown file type '%s' (expected .sf2 or .wfb)\n", filename);
        (*failed)++;
        result = -1;
    }

    return result;
}

/* Main function */
int main(int argc, char *argv[]) {
    struct ConversionOptions opts;
    int opt;
    int option_index = 0;
    int file_count = 0;
    int converted = 0, failed = 0;
    int i;

    static struct option long_options[] = {
        {"device",     required_argument, 0, 'd'},
        {"drums",      required_argument, 0, 'D'},
        {"patch",      required_argument, 0, 'p'},
        {"output",     required_argument, 0, 'o'},
        {"verbose",    no_argument,       0, 'v'},
        {"yes",        no_argument,       0, 'y'},
        {"no-assess",  no_argument,       0, 1000},
        {"help",       no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    /* Initialize options */
    memset(&opts, 0, sizeof(opts));
    opts.device_name = NULL;  /* Default - will be set to Maui for conversions */

    /* Assessment flags (local to main) */
    int assess_viability = 1;      /* Default: always assess */
    int interactive_prompt = 1;    /* Default: prompt if warnings */

    /* Parse command-line arguments */
    while ((opt = getopt_long(argc, argv, "d:D:p:o:vyh", long_options, &option_index)) != -1) {
        switch (opt) {
            case 'd':
                if (!is_valid_device_name(optarg)) {
                    fprintf(stderr, "Error: Invalid device name '%s'\n", optarg);
                    fprintf(stderr, "Valid devices: Maui, Rio, Tropez, TBS-2001\n");
                    return 1;
                }
                opts.device_name = normalize_device_name(optarg);
                break;

            case 'D':
                opts.drums_file = optarg;
                break;

            case 'p':
                {
                    char *colon = strchr(optarg, ':');
                    if (!colon) {
                        fprintf(stderr, "Error: Invalid patch format '%s' (expected file:id)\n", optarg);
                        return 1;
                    }

                    if (opts.patch_count >= 128) {
                        fprintf(stderr, "Error: Too many patches specified (max 128)\n");
                        return 1;
                    }

                    *colon = '\0';
                    opts.patches[opts.patch_count].file = optarg;
                    opts.patches[opts.patch_count].program_id = atoi(colon + 1);

                    if (opts.patches[opts.patch_count].program_id < 0 ||
                        opts.patches[opts.patch_count].program_id > 127) {
                        fprintf(stderr, "Error: Program ID must be 0-127\n");
                        return 1;
                    }

                    opts.patch_count++;
                }
                break;
            case 'v':
                opts.verbose = 1;
                break;

            case 'y':
                interactive_prompt = 0;  /* Skip prompt, always proceed */
                break;

            case 1000:  /* --no-assess */
                assess_viability = 0;
                break;

            case 'o':
                opts.output_file = optarg;
                break;

            case 'h':
                print_usage(argv[0]);
                return 0;

            default:
                print_usage(argv[0]);
                return 1;
        }
    }

    /* Check for input files */
    if (optind >= argc) {
        fprintf(stderr, "Error: No input files specified\n\n");
        print_usage(argv[0]);
        return 1;
    }

    /* Count input files first */
    for (i = optind; i < argc; i++) {
        file_count++;
    }

    /* Validate options */
    if (opts.output_file && file_count > 1) {
        fprintf(stderr, "Error: -o/--output can only be used with a single input file\n");
        return 1;
    }

    /* Process each input file */
    for (i = optind; i < argc; i++) {
        const char *pattern = argv[i];
        glob_t globbuf;

        /* Expand glob patterns */
        if (glob(pattern, GLOB_TILDE, NULL, &globbuf) == 0) {
            size_t j;
            for (j = 0; j < globbuf.gl_pathc; j++) {
                process_file(globbuf.gl_pathv[j], &opts, assess_viability,
                            interactive_prompt, &converted, &failed);
            }
            globfree(&globbuf);
        } else {
            /* No matches, try as literal filename */
            process_file(pattern, &opts, assess_viability, interactive_prompt,
                        &converted, &failed);
        }
    }

    /* Print summary if batch processing */
    if (file_count > 1 || failed > 0) {
        printf("\n=== Summary ===\n");
        printf("Processed: %d files\n", converted + failed);
        printf("Converted: %d\n", converted);
        if (failed > 0) {
            printf("Failed:    %d\n", failed);
        }
        printf("===============\n");
    }

    return failed > 0 ? 1 : 0;
}
