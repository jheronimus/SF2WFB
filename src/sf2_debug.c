/*
 * sf2_debug.c - SF2 file analyzer/debugger
 */

#include "../include/converter.h"
#include <stdio.h>
#include <stdlib.h>

extern int sf2_open(const char *filename, struct SF2Bank *bank);
extern void sf2_close(struct SF2Bank *bank);

int main(int argc, char *argv[]) {
    struct SF2Bank sf2;
    int i;

    if (argc < 2) {
        fprintf(stderr, "Usage: %s <file.sf2>\n", argv[0]);
        return 1;
    }

    if (sf2_open(argv[1], &sf2) != 0) {
        fprintf(stderr, "Failed to open SF2 file\n");
        return 1;
    }

    printf("=== SF2 File Analysis: %s ===\n\n", argv[1]);

    printf("--- Hydra Counts ---\n");
    printf("Presets:      %d\n", sf2.preset_count);
    printf("Preset Bags:  %d\n", sf2.preset_bag_count);
    printf("Preset Mods:  %d\n", sf2.preset_mod_count);
    printf("Preset Gens:  %d\n", sf2.preset_gen_count);
    printf("Instruments:  %d\n", sf2.inst_count);
    printf("Inst Bags:    %d\n", sf2.inst_bag_count);
    printf("Inst Mods:    %d\n", sf2.inst_mod_count);
    printf("Inst Gens:    %d\n", sf2.inst_gen_count);
    printf("Samples:      %d\n", sf2.sample_count);
    printf("Sample Data:  %u bytes (%.2f MB)\n",
           sf2.sample_data_size, sf2.sample_data_size / (1024.0 * 1024.0));

    printf("\n--- Presets (first 20) ---\n");
    for (i = 0; i < sf2.preset_count && i < 20; i++) {
        printf("%3d: Bank %3d, Preset %3d - %s\n",
               i,
               sf2.presets[i].wBank,
               sf2.presets[i].wPreset,
               sf2.presets[i].achPresetName);
    }

    if (sf2.preset_count > 20) {
        printf("... and %d more\n", sf2.preset_count - 20);
    }

    printf("\n--- Samples (first 10) ---\n");
    for (i = 0; i < sf2.sample_count && i < 10; i++) {
        uint32_t sample_len = sf2.samples[i].dwEnd - sf2.samples[i].dwStart;
        printf("%3d: %s\n", i, sf2.samples[i].achSampleName);
        printf("     Start: %u, End: %u, Length: %u samples (%.2f KB)\n",
               sf2.samples[i].dwStart,
               sf2.samples[i].dwEnd,
               sample_len,
               (sample_len * 2) / 1024.0);
        printf("     Rate: %u Hz, Type: %u, Link: %u\n",
               sf2.samples[i].dwSampleRate,
               sf2.samples[i].sfSampleType,
               sf2.samples[i].wSampleLink);
    }

    if (sf2.sample_count > 10) {
        printf("... and %d more\n", sf2.sample_count - 10);
    }

    sf2_close(&sf2);
    return 0;
}
