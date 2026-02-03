# SF2WFB Quick Start Guide

## Installation

### Build from source
```bash
cd Tools/SF2WFB
make
```

### Install system-wide (optional)
```bash
sudo make install
```

## Basic Usage

### Convert a single file
```bash
./sf2wfb piano.sf2
```
Output: `piano.wfb` (same directory)

### Batch convert
```bash
./sf2wfb *.sf2
./sf2wfb banks/*.sf2
./sf2wfb /path/to/soundfonts/*.sf2
```

### Specify output file
```bash
./sf2wfb -o custom_name.wfb input.sf2
```

### Target specific device
```bash
./sf2wfb -d Tropez bank.sf2        # 8MB limit
./sf2wfb -d TBS-2001 bank.sf2    # 12MB limit
./sf2wfb -d Rio bank.sf2           # 4MB limit
```

### Inspect WFB file
```bash
./sf2wfb existing.wfb
```

Shows:
- Synth name and version
- Program/patch/sample counts
- Memory requirements
- File offsets

### Change device target
```bash
./sf2wfb -d TBS-2001 existing.wfb
```

## Common Scenarios

### Convert GM bank
```bash
./sf2wfb gm_bank.sf2
```

### Use separate drum kit
```bash
./sf2wfb -D gm_drums.sf2 gm_melodic.sf2
```

### Convert entire directory
```bash
./sf2wfb -d Maui ~/Music/SoundFonts/*.sf2
```

### Check converted file
```bash
./sf2wfb output.wfb
```

## Understanding Output

### Successful conversion
```
Converting: piano.sf2 -> piano.wfb
Conversion complete: 'piano.sf2' -> 'piano.wfb'
  Programs: 128, Patches: 84, Samples: 156
```

### With resampling
```
Warning: Resampling audio from 48000 Hz to 44100 Hz.
This may result in reduced sound quality...
  Resampled: 23 samples
```

### Memory warning
```
Warning: Total sample memory (10485760 bytes) exceeds
Maui limit (8388608 bytes)
```
→ Use `-d TBS-2001` for larger banks

### Sample limit warning
```
Warning: Source exceeded 512 sample limit.
67 samples were discarded.
```
→ SF2 has too many samples for WaveFront hardware

## Exit Codes

- `0` - Success
- `1` - Error (conversion failed, invalid arguments, etc.)

## Tips & Tricks

### Prevent overwrites
The tool automatically creates numbered files:
```
piano.wfb exists → creates piano2.wfb
piano2.wfb exists → creates piano3.wfb
```

### Device memory limits
- **Rio**: 4 MB RAM
- **Maui**: 8.25 MB RAM
- **Tropez**: 8.25 MB RAM
- **TBS-2001**: 12.25 MB RAM

### Sample rate
WaveFront hardware is optimized for **44.1kHz**.
Higher rates are automatically downsampled.

### File sizes
WFB files are typically **larger** than SF2 files because:
- Samples are always embedded
- No compression
- Simpler structure

## Troubleshooting

### "Not a valid SF2 file"
→ File is corrupted or not SoundFont 2 format

### "Cannot open file"
→ Check file path and permissions

### "Failed to write"
→ Check disk space and write permissions

### Programs/Patches/Samples = 0
→ SF2 file may be empty or use non-GM banks

## Examples

### Example 1: Basic conversion
```bash
$ ./sf2wfb FluidR3_GM.sf2
Converting: FluidR3_GM.sf2 -> FluidR3_GM.wfb
Conversion complete: 'FluidR3_GM.sf2' -> 'FluidR3_GM.wfb'
  Programs: 128, Patches: 256, Samples: 487
```

### Example 2: Batch with device targeting
```bash
$ ./sf2wfb -d TBS-2001 *.sf2
Converting: Piano.sf2 -> Piano.wfb
Conversion complete...
Converting: Strings.sf2 -> Strings.wfb
Conversion complete...

=== Summary ===
Processed: 2 files
Converted: 2
===============
```

### Example 3: Inspection
```bash
$ ./sf2wfb Piano.wfb

=== WaveFront Bank Information ===
Synth Name:      TBS-2001
File Type:       Bank
Version:         1.20

--- Counts ---
Programs:        128
Patches:         184
Samples:         256

--- Memory ---
RAM Required:    6291456 bytes (6.00 MB)
Embedded:        Yes
```

## Getting Help

```bash
./sf2wfb --help
./sf2wfb -h
```

## See Also

- `README.md` - Full documentation
- `IMPLEMENTATION.md` - Technical details
- `../Specs/Spec.md` - Detailed specification
