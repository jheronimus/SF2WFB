/*
 * viability.c - SF2 to WFB conversion viability assessment
 */

#include "../include/viability.h"
#include "../include/converter.h"
#include "../include/wfb_types.h"
#include "../include/sf2_types.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <sys/stat.h>

/* Forward declarations */
extern struct sfPresetHeader *sf2_get_preset(struct SF2Bank *bank, int bank_num, int preset_num);

/* Helper to add warning */
static void add_warning(struct ViabilityReport *r, const char *fmt, ...) {
    if (r->warning_count >= MAX_WARNINGS) return;

    char buffer[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    r->warnings[r->warning_count] = malloc(strlen(buffer) + 1);
    if (r->warnings[r->warning_count]) {
        strcpy(r->warnings[r->warning_count], buffer);
        r->warning_count++;
    }
}

/* Helper to add suggestion */
static void add_suggestion(struct ViabilityReport *r, const char *fmt, ...) {
    if (r->suggestion_count >= MAX_SUGGESTIONS) return;

    char buffer[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    r->suggestions[r->suggestion_count] = malloc(strlen(buffer) + 1);
    if (r->suggestions[r->suggestion_count]) {
        strcpy(r->suggestions[r->suggestion_count], buffer);
        r->suggestion_count++;
    }
}

/* Analyze preset distribution */
static void analyze_presets(struct SF2Bank *sf2, struct ViabilityReport *r) {
    r->total_presets = 0;
    r->bank0_presets = 0;
    r->bank128_presets = 0;
    r->other_bank_presets = 0;

    for (int i = 0; i < sf2->preset_count; i++) {
        struct sfPresetHeader *preset = &sf2->presets[i];
        int bank_num = preset->wBank;
        int preset_num = preset->wPreset;

        /* Skip EOP (End of Preset) marker */
        if (i == sf2->preset_count - 1) {
            continue;
        }

        r->total_presets++;

        if (bank_num == 0 && preset_num < 128) {
            r->bank0_presets++;
        } else if (bank_num == 128) {
            r->bank128_presets++;
        } else {
            r->other_bank_presets++;
        }
    }
}

/* Trace which samples are referenced by GM presets (Bank 0 and 128) */
static void trace_sample_references(struct SF2Bank *sf2, struct ViabilityReport *r,
                                    uint8_t *sample_used) {
    memset(sample_used, 0, sf2->sample_count);

    /* For each GM preset */
    for (int bank_num = 0; bank_num <= 128; bank_num += 128) {
        int preset_limit = (bank_num == 0) ? 128 : 1;

        for (int preset_num = 0; preset_num < preset_limit; preset_num++) {
            struct sfPresetHeader *preset = sf2_get_preset(sf2, bank_num, preset_num);
            if (!preset) continue;

            /* Parse preset bags */
            int bag_start = preset->wPresetBagNdx;
            int bag_end = (preset < &sf2->presets[sf2->preset_count - 1]) ?
                         (preset + 1)->wPresetBagNdx : sf2->preset_bag_count;

            for (int i = bag_start; i < bag_end; i++) {
                struct sfPresetBag *bag = &sf2->preset_bags[i];
                int gen_start = bag->wGenNdx;
                int gen_end = (i + 1 < sf2->preset_bag_count) ?
                             sf2->preset_bags[i + 1].wGenNdx : sf2->preset_gen_count;

                /* Find instrument */
                int instrument_idx = -1;
                for (int j = gen_start; j < gen_end; j++) {
                    if (sf2->preset_gens[j].sfGenOper == GEN_INSTRUMENT) {
                        instrument_idx = sf2->preset_gens[j].genAmount.wAmount;
                        break;
                    }
                }

                if (instrument_idx < 0 || instrument_idx >= sf2->inst_count) {
                    continue;
                }

                /* Parse instrument bags */
                struct sfInst *inst = &sf2->instruments[instrument_idx];
                int inst_bag_start = inst->wInstBagNdx;
                int inst_bag_end = (instrument_idx + 1 < sf2->inst_count) ?
                                  sf2->instruments[instrument_idx + 1].wInstBagNdx :
                                  sf2->inst_bag_count;

                for (int ib = inst_bag_start; ib < inst_bag_end; ib++) {
                    struct sfInstBag *inst_bag = &sf2->inst_bags[ib];
                    int inst_gen_start = inst_bag->wInstGenNdx;
                    int inst_gen_end = (ib + 1 < sf2->inst_bag_count) ?
                                      sf2->inst_bags[ib + 1].wInstGenNdx :
                                      sf2->inst_gen_count;

                    /* Find sample */
                    for (int ig = inst_gen_start; ig < inst_gen_end; ig++) {
                        if (sf2->inst_gens[ig].sfGenOper == GEN_SAMPLE_ID) {
                            int sample_idx = sf2->inst_gens[ig].genAmount.wAmount;
                            if (sample_idx >= 0 && sample_idx < sf2->sample_count) {
                                sample_used[sample_idx] = 1;
                            }
                        }
                    }
                }
            }
        }
    }

    /* Count referenced samples */
    int referenced = 0;
    for (int i = 0; i < sf2->sample_count; i++) {
        if (sample_used[i]) {
            referenced++;
        }
    }

    r->samples_referenced_by_gm = referenced;
    r->samples_unused = sf2->sample_count - referenced;
}

/* Simplified zone grouping simulation (doesn't need full conversion) */
static int simulate_layer_truncation(struct SF2Bank *sf2, struct ViabilityReport *r,
                                     uint8_t *sample_used_after_truncation) {
    memset(sample_used_after_truncation, 0, sf2->sample_count);

    int total_layers_before = 0;
    int total_layers_after = 0;
    int programs_analyzed = 0;

    r->programs_with_truncation = 0;
    r->top_truncated_count = 0;

    /* For each GM melodic program */
    for (int prog_num = 0; prog_num < 128; prog_num++) {
        struct sfPresetHeader *preset = sf2_get_preset(sf2, 0, prog_num);
        if (!preset) continue;

        programs_analyzed++;

        /* Parse preset to count unique layer groups */
        /* Simplified: count instrument zones as rough layer estimate */
        int bag_start = preset->wPresetBagNdx;
        int bag_end = (preset < &sf2->presets[sf2->preset_count - 1]) ?
                     (preset + 1)->wPresetBagNdx : sf2->preset_bag_count;

        int zone_count = 0;
        int sample_indices[128];  /* Track samples for this program */

        for (int i = bag_start; i < bag_end; i++) {
            struct sfPresetBag *bag = &sf2->preset_bags[i];
            int gen_start = bag->wGenNdx;
            int gen_end = (i + 1 < sf2->preset_bag_count) ?
                         sf2->preset_bags[i + 1].wGenNdx : sf2->preset_gen_count;

            /* Find instrument */
            int instrument_idx = -1;
            for (int j = gen_start; j < gen_end; j++) {
                if (sf2->preset_gens[j].sfGenOper == GEN_INSTRUMENT) {
                    instrument_idx = sf2->preset_gens[j].genAmount.wAmount;
                    break;
                }
            }

            if (instrument_idx < 0 || instrument_idx >= sf2->inst_count) {
                continue;
            }

            /* Count instrument zones */
            struct sfInst *inst = &sf2->instruments[instrument_idx];
            int inst_bag_start = inst->wInstBagNdx;
            int inst_bag_end = (instrument_idx + 1 < sf2->inst_count) ?
                              sf2->instruments[instrument_idx + 1].wInstBagNdx :
                              sf2->inst_bag_count;

            for (int ib = inst_bag_start; ib < inst_bag_end; ib++) {
                struct sfInstBag *inst_bag = &sf2->inst_bags[ib];
                int inst_gen_start = inst_bag->wInstGenNdx;
                int inst_gen_end = (ib + 1 < sf2->inst_bag_count) ?
                                  sf2->inst_bags[ib + 1].wInstGenNdx :
                                  sf2->inst_gen_count;

                /* Check if this zone has a sample */
                int has_sample = 0;
                int sample_idx = -1;
                for (int ig = inst_gen_start; ig < inst_gen_end; ig++) {
                    if (sf2->inst_gens[ig].sfGenOper == GEN_SAMPLE_ID) {
                        sample_idx = sf2->inst_gens[ig].genAmount.wAmount;
                        has_sample = 1;
                        break;
                    }
                }

                if (has_sample && sample_idx >= 0 && sample_idx < sf2->sample_count) {
                    if (zone_count < 128) {
                        sample_indices[zone_count] = sample_idx;
                        zone_count++;
                    }
                }
            }
        }

        /* Rough estimate: each zone is a potential layer/group */
        /* This is simplified - real converter groups by velocity/pan/etc */
        int layers_before = zone_count;
        int layers_after = (layers_before > NUM_LAYERS) ? NUM_LAYERS : layers_before;

        total_layers_before += layers_before;
        total_layers_after += layers_after;

        /* Mark samples from "first 4 layers" as used (simplified) */
        int samples_to_keep = (zone_count > NUM_LAYERS) ? NUM_LAYERS : zone_count;
        for (int z = 0; z < samples_to_keep; z++) {
            sample_used_after_truncation[sample_indices[z]] = 1;
        }

        /* Track truncation */
        if (layers_before > NUM_LAYERS) {
            r->programs_with_truncation++;

            /* Add to top truncated list */
            if (r->top_truncated_count < MAX_TOP_TRUNCATED) {
                struct TopTruncated *t = &r->top_truncated[r->top_truncated_count];
                t->program_num = prog_num;
                strncpy(t->name, preset->achPresetName, 20);
                t->name[19] = '\0';
                t->layers_before = layers_before;
                t->layers_after = layers_after;
                t->layers_lost = layers_before - layers_after;
                r->top_truncated_count++;
            }
        }
    }

    /* Calculate averages */
    if (programs_analyzed > 0) {
        r->avg_layers_before = (float)total_layers_before / programs_analyzed;
        r->avg_layers_after = (float)total_layers_after / programs_analyzed;
    }

    r->total_programs = programs_analyzed;

    /* Count samples after truncation */
    int count = 0;
    for (int i = 0; i < sf2->sample_count; i++) {
        if (sample_used_after_truncation[i]) {
            count++;
        }
    }

    return count;
}

/* Detect use of filter Q */
static void detect_filter_q_usage(struct SF2Bank *sf2, struct ViabilityReport *r) {
    uint8_t programs_using_q[128] = {0};

    for (int prog_num = 0; prog_num < 128; prog_num++) {
        struct sfPresetHeader *preset = sf2_get_preset(sf2, 0, prog_num);
        if (!preset) continue;

        /* Check instrument zones for filter Q generators */
        int bag_start = preset->wPresetBagNdx;
        int bag_end = (preset < &sf2->presets[sf2->preset_count - 1]) ?
                     (preset + 1)->wPresetBagNdx : sf2->preset_bag_count;

        for (int i = bag_start; i < bag_end; i++) {
            struct sfPresetBag *bag = &sf2->preset_bags[i];
            int gen_start = bag->wGenNdx;
            int gen_end = (i + 1 < sf2->preset_bag_count) ?
                         sf2->preset_bags[i + 1].wGenNdx : sf2->preset_gen_count;

            /* Check preset-level generators */
            for (int j = gen_start; j < gen_end; j++) {
                if (sf2->preset_gens[j].sfGenOper == GEN_INITIAL_FILTER_Q) {
                    if (sf2->preset_gens[j].genAmount.shAmount > 0) {
                        programs_using_q[prog_num] = 1;
                    }
                }
            }

            /* Find instrument */
            int instrument_idx = -1;
            for (int j = gen_start; j < gen_end; j++) {
                if (sf2->preset_gens[j].sfGenOper == GEN_INSTRUMENT) {
                    instrument_idx = sf2->preset_gens[j].genAmount.wAmount;
                    break;
                }
            }

            if (instrument_idx < 0 || instrument_idx >= sf2->inst_count) {
                continue;
            }

            /* Check instrument-level generators */
            struct sfInst *inst = &sf2->instruments[instrument_idx];
            int inst_bag_start = inst->wInstBagNdx;
            int inst_bag_end = (instrument_idx + 1 < sf2->inst_count) ?
                              sf2->instruments[instrument_idx + 1].wInstBagNdx :
                              sf2->inst_bag_count;

            for (int ib = inst_bag_start; ib < inst_bag_end; ib++) {
                struct sfInstBag *inst_bag = &sf2->inst_bags[ib];
                int inst_gen_start = inst_bag->wInstGenNdx;
                int inst_gen_end = (ib + 1 < sf2->inst_bag_count) ?
                                  sf2->inst_bags[ib + 1].wInstGenNdx :
                                  sf2->inst_gen_count;

                for (int ig = inst_gen_start; ig < inst_gen_end; ig++) {
                    if (sf2->inst_gens[ig].sfGenOper == GEN_INITIAL_FILTER_Q) {
                        if (sf2->inst_gens[ig].genAmount.shAmount > 0) {
                            programs_using_q[prog_num] = 1;
                        }
                    }
                }
            }
        }
    }

    /* Count programs using filter Q */
    int count = 0;
    for (int i = 0; i < 128; i++) {
        if (programs_using_q[i]) {
            count++;
        }
    }

    r->programs_using_filter_q = count;
}

/* Calculate size estimates */
static void calculate_size_estimates(struct SF2Bank *sf2, struct ViabilityReport *r,
                                     uint8_t *sample_used_after_truncation) {
    /* WFB header + patch table */
    size_t header = 256;
    size_t patch_table = WF_MAX_PATCHES * sizeof(struct PATCH);
    size_t sample_table = r->samples_after_truncation * sizeof(struct SAMPLE);

    /* Estimate PCM data size */
    size_t pcm_data = 0;
    for (int i = 0; i < sf2->sample_count; i++) {
        if (sample_used_after_truncation[i]) {
            struct sfSample *s = &sf2->samples[i];
            uint32_t length = s->dwEnd - s->dwStart;
            pcm_data += length * 2;  /* 16-bit samples */
        }
    }

    r->estimated_wfb_size = header + patch_table + sample_table + pcm_data;
    r->estimated_ram_usage = r->estimated_wfb_size;

    if (r->sf2_size_bytes > 0) {
        r->size_reduction_pct = 100.0 * (1.0 - (float)r->estimated_wfb_size / r->sf2_size_bytes);
    }
}

/* Calculate viability grade */
static char calculate_grade(const struct ViabilityReport *r) {
    /* Automatic F grade conditions */
    if (r->samples_after_truncation > WF_MAX_SAMPLES) {
        return 'F';  /* Sample overflow */
    }
    if (r->bank0_presets < 32) {
        return 'F';  /* Too few melodic presets */
    }

    /* Calculate weighted score (0-100) */
    int score = 0;

    /* Preset coverage: 0-30 points */
    float preset_pct = 100.0f * r->bank0_presets / 128.0f;
    score += (int)(preset_pct * 0.3f);

    /* Sample efficiency: 0-25 points */
    float sample_pct = 100.0f * ((float)WF_MAX_SAMPLES - r->samples_after_truncation) / WF_MAX_SAMPLES;
    score += (int)(sample_pct * 0.25f);

    /* Layer retention: 0-30 points */
    float layer_pct = (r->avg_layers_before > 0) ?
                     (r->avg_layers_after / r->avg_layers_before) * 100.0f : 100.0f;
    score += (int)(layer_pct * 0.3f);

    /* Feature compatibility: 0-15 points */
    int compat = 15 - (r->programs_using_filter_q / 10);
    if (compat < 0) compat = 0;
    score += compat;

    /* Grade thresholds */
    if (score >= 90) return 'A';
    if (score >= 75) return 'B';
    if (score >= 60) return 'C';
    if (score >= 40) return 'D';
    return 'F';
}

/* Generate smart suggestions */
static void generate_suggestions(struct ViabilityReport *r) {
    r->suggestion_count = 0;

    /* Sample overflow - CRITICAL */
    if (r->samples_after_truncation > WF_MAX_SAMPLES) {
        int overflow = r->samples_after_truncation - WF_MAX_SAMPLES;
        add_suggestion(r, "CRITICAL: Exceeds %d sample limit by %d samples",
                      WF_MAX_SAMPLES, overflow);
        add_suggestion(r, "Use a smaller GM bank or drop programs to fit");

        /* Estimate programs to drop */
        int programs_to_drop = (overflow / 3) + 1;
        if (programs_to_drop < r->total_programs) {
            add_suggestion(r, "Estimate: Drop ~%d programs to fit within limit", programs_to_drop);
        }
    }

    /* Heavy layer truncation */
    if (r->programs_with_truncation > 10) {
        add_suggestion(r, "%d programs will lose velocity layers (>%d layer limit)",
                      r->programs_with_truncation, NUM_LAYERS);

        if (r->top_truncated_count > 0) {
            add_suggestion(r, "Most affected: %s (loses %d/%d layers)",
                          r->top_truncated[0].name,
                          r->top_truncated[0].layers_lost,
                          r->top_truncated[0].layers_before);
        }

        add_suggestion(r, "Pre-edit SF2 to merge layers, or accept reduced expression");
    }

    /* Filter Q loss */
    if (r->programs_using_filter_q > 20) {
        add_suggestion(r, "%d programs use filter resonance (unsupported on WaveFront)",
                      r->programs_using_filter_q);
        add_suggestion(r, "Timbral character may change without resonance control");
    }

    /* Excellent fit */
    if (r->grade == 'A') {
        add_suggestion(r, "Excellent conversion candidate!");
        add_suggestion(r, "High fidelity expected with minimal quality loss");
    }

    /* Good fit */
    if (r->grade == 'B') {
        add_suggestion(r, "Good conversion candidate with minor compromises");
    }

    /* Marginal fit */
    if (r->grade == 'C' || r->grade == 'D') {
        add_suggestion(r, "Conversion possible but quality will be reduced");
        add_suggestion(r, "Test critical programs on hardware before deployment");
    }

    /* RAM warnings */
    if (r->estimated_ram_usage > 8 * 1024 * 1024) {
        add_suggestion(r, "WARNING: Exceeds 8MB limit (largest WaveFront card)");
    } else if (r->estimated_ram_usage > 4 * 1024 * 1024) {
        add_suggestion(r, "Requires 8MB WaveFront card (Tropez/Maui)");
        add_suggestion(r, "Will NOT fit on 4MB cards (Rio)");
    } else if (r->estimated_ram_usage > 3 * 1024 * 1024) {
        add_suggestion(r, "Will fit on 4MB card but with little headroom");
    }
}

/* Main assessment function */
int assess_sf2_viability(const char *sf2_path,
                        struct ViabilityReport *report,
                        const struct ViabilityConfig *config) {
    struct SF2Bank sf2;
    struct stat st;

    (void)config;  /* Currently unused, reserved for future options */

    memset(report, 0, sizeof(*report));

    /* Get file size */
    if (stat(sf2_path, &st) == 0) {
        report->sf2_size_bytes = st.st_size;
    }

    /* Extract filename */
    const char *filename = strrchr(sf2_path, '/');
    if (filename) {
        filename++;
    } else {
        filename = sf2_path;
    }
    strncpy(report->filename, filename, sizeof(report->filename) - 1);

    /* Open SF2 file */
    if (sf2_open(sf2_path, &sf2) != 0) {
        fprintf(stderr, "Error: Failed to open SF2 file\n");
        return -1;
    }

    report->total_samples_in_sf2 = sf2.sample_count;

    /* Allocate sample tracking arrays */
    uint8_t *sample_used = calloc(sf2.sample_count, 1);
    uint8_t *sample_used_after_truncation = calloc(sf2.sample_count, 1);
    if (!sample_used || !sample_used_after_truncation) {
        fprintf(stderr, "Error: Failed to allocate memory\n");
        sf2_close(&sf2);
        free(sample_used);
        free(sample_used_after_truncation);
        return -1;
    }

    /* Run assessments */
    analyze_presets(&sf2, report);
    trace_sample_references(&sf2, report, sample_used);
    report->samples_after_truncation = simulate_layer_truncation(&sf2, report,
                                                                 sample_used_after_truncation);
    detect_filter_q_usage(&sf2, report);
    calculate_size_estimates(&sf2, report, sample_used_after_truncation);

    /* Calculate grade and generate suggestions */
    report->grade = calculate_grade(report);
    generate_suggestions(report);

    /* Generate warnings based on findings */
    if (report->samples_after_truncation > WF_MAX_SAMPLES) {
        add_warning(report, "Exceeds %d sample limit by %d samples",
                   WF_MAX_SAMPLES, report->samples_after_truncation - WF_MAX_SAMPLES);
    }

    if (report->programs_with_truncation > 5) {
        add_warning(report, "%d programs will have layers truncated (%d-layer limit)",
                   report->programs_with_truncation, NUM_LAYERS);
    }

    if (report->programs_with_truncation > 0 && report->top_truncated_count > 0) {
        struct TopTruncated *t = &report->top_truncated[0];
        add_warning(report, "%s loses %d/%d layers (%d%% reduction)",
                   t->name, t->layers_lost, t->layers_before,
                   (t->layers_lost * 100) / t->layers_before);
    }

    if (report->programs_using_filter_q > 10) {
        add_warning(report, "%d programs use filter Q (will be ignored)",
                   report->programs_using_filter_q);
    }

    if (report->other_bank_presets > 0) {
        add_warning(report, "%d presets in other banks will be skipped",
                   report->other_bank_presets);
    }

    /* Cleanup */
    free(sample_used);
    free(sample_used_after_truncation);
    sf2_close(&sf2);

    return 0;
}

/* Print summary report */
void print_viability_summary(const struct ViabilityReport *r) {
    const char *grade_desc[] = {
        ['A'] = "Excellent - minimal loss",
        ['B'] = "Good with minor compromises",
        ['C'] = "Acceptable with quality loss",
        ['D'] = "Poor - significant quality loss",
        ['F'] = "Not recommended"
    };

    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf(" SF2 Conversion Assessment: %s\n", r->filename);
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n\n");

    printf(" Overall Grade:     %c  (%s)\n\n", r->grade, grade_desc[(int)r->grade]);

    printf(" Bank 0 Presets:    %-3d / 128  (%d%%) %s\n",
           r->bank0_presets, (r->bank0_presets * 100) / 128,
           r->bank0_presets >= 120 ? "✓" : "⚠");

    printf(" Bank 128 Presets:  %-3d / 1    (%d%%) %s\n",
           r->bank128_presets, r->bank128_presets * 100,
           r->bank128_presets > 0 ? "✓" : " ");

    if (r->other_bank_presets > 0) {
        printf(" Unused Presets:    %d from other banks\n", r->other_bank_presets);
    }

    printf("\n Sample Budget:     %-3d / %d  (%d%%)  %s\n",
           r->samples_after_truncation, WF_MAX_SAMPLES,
           (r->samples_after_truncation * 100) / WF_MAX_SAMPLES,
           r->samples_after_truncation <= WF_MAX_SAMPLES ? "✓" : "✗");

    if (r->samples_after_truncation <= WF_MAX_SAMPLES) {
        int headroom = WF_MAX_SAMPLES - r->samples_after_truncation;
        printf(" Sample Headroom:   %d samples available\n", headroom);
    }

    if (r->programs_with_truncation > 0) {
        printf("\n Layer Truncation:  %d programs affected       ⚠\n",
               r->programs_with_truncation);
        printf(" Avg Complexity:    %.1f → %.1f layers per prog\n",
               r->avg_layers_before, r->avg_layers_after);
    }

    printf("\n Estimated Size:    %.1f MB (%.0f%% smaller)\n",
           r->estimated_wfb_size / (1024.0 * 1024.0),
           r->size_reduction_pct);

    printf(" RAM Required:      %.1f MB (Tropez: %d%%, Rio: %d%%)\n",
           r->estimated_ram_usage / (1024.0 * 1024.0),
           (int)((r->estimated_ram_usage * 100) / (8 * 1024 * 1024)),
           (int)((r->estimated_ram_usage * 100) / (4 * 1024 * 1024)));

    /* Warnings section */
    if (r->warning_count > 0) {
        printf("\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
        printf(" WARNINGS\n");
        printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n\n");

        for (int i = 0; i < r->warning_count; i++) {
            printf(" ⚠ %s\n", r->warnings[i]);
        }
    }

    /* Recommendations section */
    if (r->suggestion_count > 0) {
        printf("\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
        printf(" RECOMMENDATIONS\n");
        printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n\n");

        for (int i = 0; i < r->suggestion_count; i++) {
            printf(" • %s\n", r->suggestions[i]);
        }
    }

    printf("\n");
}

/* Print verbose report */
void print_viability_verbose(const struct ViabilityReport *r) {
    /* First print summary */
    print_viability_summary(r);

    /* Then add detailed sections */

    /* Layer truncation details */
    if (r->top_truncated_count > 0) {
        printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
        printf(" LAYER TRUNCATION DETAILS\n");
        printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n\n");

        printf(" Programs losing most layers:\n\n");

        int show_count = r->top_truncated_count < 10 ? r->top_truncated_count : 10;
        for (int i = 0; i < show_count; i++) {
            const struct TopTruncated *t = &r->top_truncated[i];
            printf("   %-3d %-16s %2d → %d  (−%d)  ",
                   t->program_num, t->name,
                   t->layers_before, t->layers_after, t->layers_lost);

            /* Warning symbols based on severity */
            int loss_pct = (t->layers_lost * 100) / t->layers_before;
            if (loss_pct > 66) printf("⚠⚠⚠\n");
            else if (loss_pct > 33) printf("⚠⚠\n");
            else printf("⚠\n");
        }

        int unaffected = r->total_programs - r->programs_with_truncation;
        printf("\n Programs unaffected: %d (already ≤%d layers) ✓\n",
               unaffected, NUM_LAYERS);

        printf("\n");
    }

    /* Feature compatibility details */
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf(" FEATURE COMPATIBILITY\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n\n");

    if (r->programs_using_filter_q > 0) {
        printf(" Filter Q Usage:\n");
        printf("   %d programs use filter resonance\n", r->programs_using_filter_q);
        printf("   WaveFront ICS2115 has no resonance parameter\n");
        printf("   Timbral character may change\n\n");
    } else {
        printf(" Filter Q: Not used ✓\n\n");
    }

    /* Sample analysis details */
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf(" SAMPLE ANALYSIS\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n\n");

    printf(" Total samples in SF2:       %d\n", r->total_samples_in_sf2);
    printf(" Referenced by Bank 0/128:   %d  (%d%%)\n",
           r->samples_referenced_by_gm,
           (r->samples_referenced_by_gm * 100) / r->total_samples_in_sf2);
    printf(" Unused/orphaned samples:    %d  (%d%%)\n\n",
           r->samples_unused,
           (r->samples_unused * 100) / r->total_samples_in_sf2);

    printf(" After %d-layer truncation:   %d samples needed\n",
           NUM_LAYERS, r->samples_after_truncation);
    printf(" WaveFront limit:            %d samples\n", WF_MAX_SAMPLES);
    printf(" Utilization:                %d%%\n",
           (r->samples_after_truncation * 100) / WF_MAX_SAMPLES);

    if (r->samples_after_truncation <= WF_MAX_SAMPLES) {
        int headroom = WF_MAX_SAMPLES - r->samples_after_truncation;
        printf(" Headroom:                   %d samples (%d%%)\n\n",
               headroom, (headroom * 100) / WF_MAX_SAMPLES);
        printf(" No sample overflow ✓\n");
    } else {
        int overflow = r->samples_after_truncation - WF_MAX_SAMPLES;
        printf(" OVERFLOW:                   +%d samples ✗\n", overflow);
    }

    printf("\n");
}

/* Prompt user to proceed */
int prompt_user_proceed(const struct ViabilityReport *report) {
    if (report->warning_count == 0) {
        return 1;  /* No warnings, proceed automatically */
    }

    printf("Proceed with conversion? [Y/n]: ");
    fflush(stdout);

    char response[10];
    if (fgets(response, sizeof(response), stdin) == NULL) {
        return 0;
    }

    /* Default to yes if just Enter pressed */
    if (response[0] == '\n') {
        return 1;
    }

    /* Check for explicit yes/no */
    if (response[0] == 'y' || response[0] == 'Y') {
        return 1;
    }

    if (response[0] == 'n' || response[0] == 'N') {
        return 0;
    }

    /* Default to yes for any other input */
    return 1;
}

/* Free allocated memory in report */
void free_viability_report(struct ViabilityReport *report) {
    for (int i = 0; i < report->warning_count; i++) {
        free(report->warnings[i]);
    }

    for (int i = 0; i < report->suggestion_count; i++) {
        free(report->suggestions[i]);
    }

    free(report->recommendation);

    memset(report, 0, sizeof(*report));
}
