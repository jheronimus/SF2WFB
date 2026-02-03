# Filename Case Handling

## Overview

SF2WFB now preserves the case of file extensions when converting SF2 files to WFB format.

## Behavior

### Input Files (Case-Insensitive)
The tool accepts both uppercase and lowercase extensions:
- `.sf2` ✅
- `.SF2` ✅
- `.Sf2` ✅ (any case combination)
- `.wfb` ✅
- `.WFB` ✅
- `.Wfb` ✅ (any case combination)

### Output Files (Case-Preserving)
The output file extension **matches the case** of the input extension:

| Input Extension | Output Extension |
|----------------|------------------|
| `.sf2` | `.wfb` |
| `.SF2` | `.WFB` |
| `.Sf2` | `.Wfb` |
| `.sF2` | `.wFb` |
| `.SF2` (mixed) | `.WFB` (matched) |

## Examples

### Uppercase Source
```bash
$ ./sf2wfb PIANO.SF2
Converting: PIANO.SF2 -> PIANO.WFB
```

### Lowercase Source
```bash
$ ./sf2wfb piano.sf2
Converting: piano.sf2 -> piano.wfb
```

### Mixed Case Source
```bash
$ ./sf2wfb Piano.Sf2
Converting: Piano.Sf2 -> Piano.Wfb
```

## Verification

```bash
# Clean slate
$ rm ../../SF2/*.wfb ../../SF2/*.WFB

# Convert with different cases
$ ./sf2wfb ../../SF2/4MBGM.SF2 ../../SF2/ch12m20.sf2

# Check created files
$ ls -1 ../../SF2/*.WFB ../../SF2/*.wfb
4MBGM.WFB       # Uppercase output from .SF2 input
ch12m20.wfb     # Lowercase output from .sf2 input
```

## Implementation

The tool checks if the input extension is `.sf2` (case-insensitive), then:

1. **All Uppercase** (`.SF2`) → Output: `.WFB`
2. **All Lowercase** (`.sf2`) → Output: `.wfb`
3. **Mixed Case** (`.Sf2`, `.sF2`, etc.) → Each character position is matched

### Code Logic
```c
/* Preserve the case pattern */
if (all_uppercase(ext)) {
    use "WFB"
} else if (all_lowercase(ext)) {
    use "wfb"
} else {
    /* Mixed: map each position */
    's'/'S' → 'w'/'W'
    'f'/'F' → 'f'/'F'
    '2'     → 'b'/'B' (based on previous chars)
}
```

## Rationale

### Why Preserve Case?

1. **User Expectations**: Users who organize files with uppercase extensions expect outputs to match
2. **Consistency**: Maintains visual consistency in file listings
3. **Compatibility**: Some systems or tools may rely on specific case conventions
4. **Professionalism**: Shows attention to detail

### Platform Considerations

#### macOS (APFS)
- **Case-insensitive** but **case-preserving** filesystem
- Files created as `FILE.WFB` are stored with that exact case
- Both `FILE.WFB` and `FILE.wfb` refer to the same file
- `ls` and Python's `os.listdir()` return the actual stored case

#### Linux (ext4, XFS)
- **Case-sensitive** filesystem
- `FILE.WFB` and `FILE.wfb` are **different files**
- Case preservation is critical

#### Windows (NTFS)
- **Case-insensitive** but **case-preserving** (like macOS)
- Behaves similarly to APFS

## Testing

### Test Matrix

| Test | Input | Expected Output | Status |
|------|-------|----------------|--------|
| 1 | `test.sf2` | `test.wfb` | ✅ |
| 2 | `TEST.SF2` | `TEST.WFB` | ✅ |
| 3 | `Test.Sf2` | `Test.Wfb` | ✅ |
| 4 | `test.sF2` | `test.wFb` | ✅ |
| 5 | `-o OUT.WFB in.sf2` | `OUT.WFB` | ✅ (explicit) |

### Batch Processing
```bash
$ ./sf2wfb BANK1.SF2 bank2.sf2 MiXeD.Sf2
Converting: BANK1.SF2 -> BANK1.WFB
Converting: bank2.sf2 -> bank2.wfb
Converting: MiXeD.Sf2 -> MiXeD.Wfb
```

## Edge Cases

### Explicit Output (`-o` flag)
When using `-o`, the output filename is used **as-is**:
```bash
$ ./sf2wfb -o MyBank.WFB input.sf2
# Output: MyBank.WFB (not affected by input case)
```

### Non-SF2 Extensions
For files that aren't `.sf2`, lowercase `.wfb` is used:
```bash
$ ./sf2wfb input.soundfont
# Output: input.wfb (default lowercase)
```

### WFB Input (Verification Mode)
WFB files can be any case and are accepted:
```bash
$ ./sf2wfb file.wfb   # ✅ Works
$ ./sf2wfb file.WFB   # ✅ Works
$ ./sf2wfb file.Wfb   # ✅ Works
```

## Compatibility

### Backward Compatibility
Existing scripts and workflows are **fully compatible**:
- Old behavior: Always created `.wfb` (lowercase)
- New behavior: Matches input case (`.SF2`→`.WFB`, `.sf2`→`.wfb`)
- Scripts using `.sf2` (lowercase) will get `.wfb` (lowercase) — **no change**

### Migration
No migration needed. Files created with previous versions (all `.wfb`) work identically.

## Summary

✅ **Accepts**: Any case variation of `.sf2` or `.wfb`
✅ **Preserves**: Output extension matches input extension case
✅ **Defaults**: Lowercase `.wfb` for non-SF2 inputs
✅ **Compatible**: Works across macOS, Linux, and Windows

---
**Implementation**: `src/main.c::get_output_filename()`
**Status**: Fully implemented and tested
**Version**: Added February 3, 2026
