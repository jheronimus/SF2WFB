# SF2WFB - SoundFont 2 to WaveFront Bank Converter

A standalone command-line tool for converting SoundFont 2 (.SF2) files into Turtle Beach WaveFront Bank (.WFB) format.

## Features

- **Pure C Implementation**: Written in C11 standard with zero external dependencies
- **Cross-Platform**: Compatible with macOS and Linux (x86_64 and ARM64)
- **Batch Processing**: Convert multiple files using glob patterns
- **Auto-Resampling**: Automatically resamples audio > 44.1kHz using linear interpolation
- **Stereo Detection**: Detects and properly pairs stereo samples (CH_LEFT/CH_RIGHT)
- **Device Targeting**: Support for Maui, Rio, Tropez, and TBS-2001
- **Memory Limit Enforcement**: Validates against device-specific RAM limits
- **Sample Limit Handling**: Enforces hard limit of 512 samples
- **Drum Kit Merging**: Merge drum kits from separate SF2 files
- **Instrument Patching**: Replace specific programs with instruments from other SF2 files
- **File Verification**: Inspect existing WFB files
- **Auto-Increment Filenames**: Prevents accidental overwrites

## Building

### Requirements
- C compiler (gcc or clang)
- Standard C library (libc)
- make

### Compile

```bash
cd Tools/SF2WFB
make
```

To use a specific compiler:

```bash
make CC=gcc
# or
make CC=clang
```

### Install (Optional)

```bash
sudo make install
```

This installs `sf2wfb` to `/usr/local/bin`.

## Usage

### Basic Conversion

```bash
./sf2wfb piano.sf2
```

This creates `piano.wfb` in the same directory.

### Batch Processing

```bash
./sf2wfb *.sf2
```

### Specify Target Device

```bash
./sf2wfb -d TBS-2001 bank.sf2
```

Valid devices:
- `Maui` (8.25 MB RAM, default)
- `Rio` (4 MB RAM)
- `Tropez` (8.25 MB RAM)
- `TBS-2001` (12.25 MB RAM, Tropez Plus model number)

### Drum Kit Merging

```bash
./sf2wfb -D drums.sf2 melodic.sf2
```

Uses `drums.sf2` for the drum kit (Bank 128) and `melodic.sf2` for melodic programs (Bank 0).

### Instrument Patching

```bash
./sf2wfb -p violin.sf2:40 orchestra.sf2
```

Replaces program 40 (Violin) in the output with the first preset from `violin.sf2`.

Multiple patches:

```bash
./sf2wfb -p violin.sf2:40 -p cello.sf2:42 orchestra.sf2
```

### Explicit Output Filename

```bash
./sf2wfb -o custom.wfb bank.sf2
```

**Note**: `-o` can only be used with a single input file.

### Verify WFB File

```bash
./sf2wfb existing.wfb
```

Displays header information, counts, memory usage, and offsets.

### Retarget WFB File

```bash
./sf2wfb -d Rio existing.wfb
```

Updates the target device field in an existing WFB file.

## Command-Line Options

| Flag | Long Flag | Description |
|------|-----------|-------------|
| `-d` | `--device <name>` | Target device (Maui, Rio, Tropez, TBS-2001) |
| `-D` | `--drums <path>` | SF2 file to use for drum kit |
| `-p` | `--patch <file>:<id>` | Replace program ID (0-127) with preset from file |
| `-o` | `--output <path>` | Explicit output filename (single file only) |
| `-h` | `--help` | Show help message |

## Technical Details

### Binary Structure

The tool uses packed structures (`__attribute__((packed))`) to ensure exact binary layout matching the WaveFront specification. The C++ inheritance from the original SDK (`struct WaveFrontProgram : public PROGRAM`) is simulated using struct composition.

### Endianness

- **Little Endian**: All fields except `nFreqBias`
- **Big Endian (Motorola format)**: `nFreqBias` field in the `PATCH` structure

### Limits

- **Programs**: 128 (GM Bank 0)
- **Patches**: 256
- **Samples**: 512 (stereo samples count as 2)
- **Layers per Program**: 4

### Bank Mapping

- **Bank 0**: Melodic programs (0-127)
- **Bank 128**: Drum kit (GM percussion, MIDI keys 35-81)

Non-GM banks are ignored to stay within limits.

### Resampling

Samples with sample rates > 44.1kHz are automatically downsampled to 44.1kHz using linear interpolation. A warning is displayed when this occurs.

### File Safety

If an output file already exists, the tool automatically appends a counter (`file2.wfb`, `file3.wfb`, etc.) to prevent data loss.

## Examples

### Convert a single file
```bash
./sf2wfb piano.sf2
# Output: piano.wfb
```

### Batch convert with device targeting
```bash
./sf2wfb -d Tropez banks/*.sf2
# Output: Multiple .wfb files in banks/
```

### Create GM bank with separate drums
```bash
./sf2wfb -D gm_drums.sf2 gm_melodic.sf2 -o GM_Bank.wfb
```

### Patch specific instruments
```bash
./sf2wfb -p strings.sf2:48 -p brass.sf2:56 orchestra.sf2
```

### Verify converted file
```bash
./sf2wfb orchestra.wfb
```

Output:
```
=== WaveFront Bank Information ===
Synth Name:      Maui
File Type:       Bank
Version:         1.20

--- Counts ---
Programs:        128
Patches:         256
Samples:         412
Drumkits:        1

--- Memory ---
RAM Required:    7340032 bytes (7.00 MB)
Embedded:        Yes
...
```

## Error Handling

The tool provides clear error messages and warnings:

- **Invalid SF2**: "Not a valid SF2 file"
- **Sample overflow**: "Source exceeded 512 sample limit. X samples were discarded."
- **Memory limit**: "Total sample memory (X bytes) exceeds [Device] limit (Y bytes)"
- **Resampling**: "Resampling audio from XHz to 44.1kHz. This may result in reduced sound quality."

## License

This tool is provided as-is for use with Turtle Beach WaveFront synthesizer cards.

## Author

SF2WFB was developed as part of the WaveFront preservation project.

## See Also

- `Specs/Spec.md` - Detailed functional specification
- `Specs/WFB.md` - WaveFront file format specification
- `Specs/Content.md` - WaveFront ecosystem overview
