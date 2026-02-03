/*
 * viability.h - SF2 to WFB conversion viability assessment
 */

#ifndef VIABILITY_H
#define VIABILITY_H

#include <stddef.h>
#include <stdint.h>

/* Maximum warnings and suggestions */
#define MAX_WARNINGS 16
#define MAX_SUGGESTIONS 8
#define MAX_TOP_TRUNCATED 10

/* Top programs affected by layer truncation */
struct TopTruncated {
    int program_num;
    char name[20];
    int layers_before;
    int layers_after;
    int layers_lost;
};

/* Detailed viability assessment report */
struct ViabilityReport {
    /* File info */
    char filename[256];
    size_t sf2_size_bytes;

    /* Preset analysis */
    int total_presets;
    int bank0_presets;          /* Melodic: programs 0-127 */
    int bank128_presets;        /* Drums: channel 10 */
    int other_bank_presets;     /* Will be skipped */

    /* Sample budget (ONLY samples used by Bank 0/128 after truncation) */
    int total_samples_in_sf2;
    int samples_referenced_by_gm;       /* Before truncation */
    int samples_after_truncation;       /* After 4-layer limit */
    int samples_unused;                 /* Never referenced */

    /* Layer truncation analysis */
    int total_programs;                 /* Should be 128 melodic + 1 drum */
    int programs_with_truncation;       /* Programs losing layers */
    float avg_layers_before;
    float avg_layers_after;

    /* Top affected programs */
    struct TopTruncated top_truncated[MAX_TOP_TRUNCATED];
    int top_truncated_count;

    /* Feature compatibility */
    int programs_using_filter_q;
    int programs_with_complex_mods;
    int stereo_pairs_found;
    int stereo_pairs_convertible;

    /* Size estimates */
    size_t estimated_wfb_size;
    size_t estimated_ram_usage;
    float size_reduction_pct;

    /* Overall assessment */
    char grade;                         /* 'A', 'B', 'C', 'D', 'F' */
    int warning_count;
    char *warnings[MAX_WARNINGS];       /* Dynamic warnings */
    char *recommendation;               /* Main recommendation */
    char *suggestions[MAX_SUGGESTIONS]; /* Actionable tips */
    int suggestion_count;
};

/* Assessment configuration */
struct ViabilityConfig {
    int verbose;        /* 0=summary, 1=detailed */
    int interactive;    /* Prompt user if warnings */
    int auto_yes;       /* Skip prompt, always proceed */
};

/* Function prototypes */

/* Main assessment API */
int assess_sf2_viability(const char *sf2_path,
                        struct ViabilityReport *report,
                        const struct ViabilityConfig *config);

/* Report generation */
void print_viability_summary(const struct ViabilityReport *report);
void print_viability_verbose(const struct ViabilityReport *report);

/* User interaction */
int prompt_user_proceed(const struct ViabilityReport *report);

/* Cleanup */
void free_viability_report(struct ViabilityReport *report);

#endif /* VIABILITY_H */
