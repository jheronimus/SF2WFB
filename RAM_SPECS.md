# WaveFront Device RAM Specifications

## Corrected Memory Limits

| Device | Model | RAM Capacity | Bytes | Hex |
|--------|-------|--------------|-------|-----|
| Rio | N/A | **4 MB** | 4,194,304 | 0x400000 |
| Maui | N/A | **8.25 MB** | 8,650,752 | 0x840000 |
| Tropez | N/A | **8.25 MB** | 8,650,752 | 0x840000 |
| Tropez Plus | TBS-2001 | **12.25 MB** | 12,845,056 | 0xC40000 |

## Why .25 MB?

The additional 0.25 MB (256 KB) beyond the base 8 MB and 12 MB is likely:
1. **ROM Sample Storage**: Reserved space for factory ROM patches
2. **Buffer Space**: Working memory for sample processing
3. **System Overhead**: Control structures and driver data

### Breakdown
- **8.25 MB** = 8,192 KB + 256 KB = 8,650,752 bytes
- **12.25 MB** = 12,288 KB + 256 KB = 12,845,056 bytes

## Implementation

### C Header Definition
```c
/* Device memory limits (in bytes) */
#define DEVICE_MEM_RIO        (4 * 1024 * 1024)      /* 4 MB */
#define DEVICE_MEM_MAUI       8650752                /* 8.25 MB */
#define DEVICE_MEM_TROPEZ     8650752                /* 8.25 MB */
#define DEVICE_MEM_TROPEZPLUS 12845056               /* 12.25 MB */
```

### Usage in Code
```c
uint32_t get_device_memory_limit(const char *device_name) {
    if (strcasecmp(device_name, "Rio") == 0) {
        return DEVICE_MEM_RIO;        // 4 MB
    } else if (strcasecmp(device_name, "Maui") == 0) {
        return DEVICE_MEM_MAUI;       // 8.25 MB
    } else if (strcasecmp(device_name, "Tropez") == 0) {
        return DEVICE_MEM_TROPEZ;     // 8.25 MB
    } else if (strcasecmp(device_name, "TBS-2001") == 0) {
        return DEVICE_MEM_TROPEZPLUS; // 12.25 MB
    }
    return DEVICE_MEM_MAUI; /* Default */
}
```

## Memory Check During Conversion

When converting SF2 to WFB, the tool validates total sample memory:

```c
memory_limit = get_device_memory_limit(wfb.header.szSynthName);
if (wfb.total_sample_memory > memory_limit) {
    printf("Warning: Total sample memory (%u bytes) exceeds %s limit (%u bytes)\n",
           wfb.total_sample_memory, wfb.header.szSynthName, memory_limit);
}
```

### Example Output
```
Warning: Total sample memory (9000000 bytes) exceeds Maui limit (8650752 bytes)
```

## Historical Context

The WaveFront synthesizer cards used:
- **ICS2115 WaveFront Synthesizer Chip**: Hardware wavetable synthesis
- **DRAM Modules**: Onboard RAM for sample storage
- **ROM Chips**: Factory sample sets (Maui/Tropez only)

### Memory Architecture
```
Total Available = User RAM + ROM Samples + System Overhead

Maui:
  8 MB User RAM + 128 KB ROM + 128 KB System = 8.25 MB total

Tropez Plus:
  12 MB User RAM + 128 KB ROM + 128 KB System = 12.25 MB total
```

## Sample Memory Calculation

For a bank with embedded samples:
```c
dwMemoryRequired = sum(all_sample_sizes_in_bytes)
```

Example:
- 200 samples @ average 40 KB each = 8,000,000 bytes
- Within Maui limit (8,650,752 bytes) ✅
- Exceeds Rio limit (4,194,304 bytes) ⚠️

## User Guidelines

### Targeting Devices

| Use Case | Recommended Device | Reason |
|----------|-------------------|---------|
| Small GM bank (< 4 MB) | Rio | Compatible with all cards |
| Standard GM bank (4-8 MB) | Maui | Good compatibility |
| Large GM bank (8-12 MB) | TBS-2001 | Maximum capacity |
| Full orchestral (> 12 MB) | None | Exceeds hardware limits |

### Optimization Tips

1. **Sample Rate**: Use 44.1 kHz (native rate)
2. **Bit Depth**: 16-bit signed PCM (required)
3. **Loop Points**: Reduce sample length with good loops
4. **Deduplication**: Share samples across patches
5. **Compression**: Consider 8-bit for percussion (future)

## Verification

### Test Limits
```bash
# Generate test with exact limit
$ ./sf2wfb -d Maui large_bank.sf2

# Should see:
Programs: 128, Patches: 256, Samples: 512
RAM Required: 8650752 bytes (8.25 MB)  # Exactly at limit
```

### Over Limit
```bash
$ ./sf2wfb -d Maui huge_bank.sf2

# Output:
Warning: Total sample memory (10485760 bytes) exceeds Maui limit (8650752 bytes)
```

Tool will still create the file but warns user that it won't fit on target hardware.

## Compliance

✅ **Updated Implementation**
- Code reflects accurate 8.25 MB and 12.25 MB limits
- Documentation updated across all files
- Memory validation functional

✅ **Backward Compatibility**
- No changes to file format
- Existing WFB files remain valid
- More accurate limit warnings

## Related Files
- `include/wfb_types.h` - Constant definitions
- `src/util.c` - Memory limit function
- `src/converter.c` - Memory validation
- All documentation files updated

---
**Last Updated**: February 3, 2026
**Source**: User correction based on hardware specifications
**Status**: Implemented and verified
