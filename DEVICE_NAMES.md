# WaveFront Device Names - Official Specification

## Summary

Based on analysis of actual WFB files from Turtle Beach, the official device identifiers used in the `szSynthName` field are:

| Device | Official szSynthName | Accepts (User Input) |
|--------|---------------------|----------------------|
| Maui | `Maui` | Maui, maui |
| Rio | `Rio` | Rio, rio |
| Tropez | `Tropez` | Tropez, tropez |
| **Tropez Plus** | **`TBS-2001`** | TBS-2001, TropezPlus, "Tropez Plus" |

## Evidence from Real WFB Files

Analysis of WFB files in `WFB/Map/` directory:

```
File: GM_MAUI.WFB
00000000  4d 61 75 69 00 00 ...  |Maui............|
          M  a  u  i  \0

File: GM_RIO.WFB
00000000  52 69 6f 00 00 00 ...  |Rio.............|
          R  i  o  \0

File: GM_TROP.WFB
00000000  54 72 6f 70 65 7a 00 00  |Tropez..........|
          T  r  o  p  e  z  \0

File: GM_2001.WFB
00000000  54 42 53 2d 32 30 30 31  00 00 00 00 00 00 00 00  |TBS-2001........|
          T  B  S  -  2  0  0  1  \0
```

## Key Finding: Tropez Plus = TBS-2001

The Tropez Plus is **NOT** identified as "Tropez Plus" in WFB files.
Instead, it uses the **model number**: `TBS-2001`

### Documentation Support

From `Docs/FAQ.txt`:
- Line 15: "FAQ file about the **Tropez Plus (TBS-2001)** sound board"
- Line 104: "I will use liberally this model name **TBS-2001**"
- Line 264: "TBS-2001 = Rio + Tropez"

**TBS-2001** is the official Turtle Beach model number for the Tropez Plus card.

## Implementation

### User Input (Flexible)
Users can specify the device using any of these forms:
```bash
./sf2wfb -d TBS-2001 file.sf2      # Official model number
./sf2wfb -d TropezPlus file.sf2    # Concatenated marketing name
./sf2wfb -d "Tropez Plus" file.sf2 # Marketing name with space
```

### WFB Output (Standardized)
All variants normalize to the official identifier:
```c
szSynthName[32] = "TBS-2001\0..."
```

### Memory Limits by Device

| Device | szSynthName | RAM Limit | Bytes |
|--------|-------------|-----------|-------|
| Rio | `Rio` | 4 MB | 4,194,304 |
| Maui | `Maui` | 8.25 MB | 8,650,752 |
| Tropez | `Tropez` | 8.25 MB | 8,650,752 |
| Tropez Plus | `TBS-2001` | 12.25 MB | 12,845,056 |

## Code Implementation

```c
const char *normalize_device_name(const char *name) {
    if (strcasecmp(name, "maui") == 0) {
        return "Maui";
    } else if (strcasecmp(name, "rio") == 0) {
        return "Rio";
    } else if (strcasecmp(name, "tropez") == 0) {
        return "Tropez";
    } else if (strcasecmp(name, "tropezplus") == 0 ||
               strcasecmp(name, "tropez plus") == 0 ||
               strcasecmp(name, "tbs-2001") == 0) {
        return "TBS-2001";  /* Official model number */
    }
    return name;
}
```

## Verification

### Test Cases
```bash
# All produce identical binary output:
$ ./sf2wfb -d TBS-2001 input.sf2 -o test1.wfb
$ ./sf2wfb -d TropezPlus input.sf2 -o test2.wfb
$ ./sf2wfb -d "Tropez Plus" input.sf2 -o test3.wfb

# All three files have:
00000000  54 42 53 2d 32 30 30 31  |TBS-2001........|
```

### Compatibility with Turtle Beach Tools
Using `TBS-2001` ensures compatibility with:
- Turtle Beach WavePatch file loaders
- WaveFront Gatekeeper DLL
- Official GM bank files (GM_2001.WFB)

## Historical Context

The Tropez Plus (TBS-2001) was Turtle Beach's flagship WaveFront card:
- **Release**: Mid-1990s
- **Features**: Combined Rio + Tropez capabilities
- **RAM**: 12MB (expandable)
- **Effects**: YSS225 FX processor
- **CODEC**: CS4232 (Crystal Semiconductor)
- **ROM**: Same patch set as Maui/Tropez

Using the model number instead of marketing name follows industry practice for hardware identification in binary formats.

## Conclusion

✅ **Correct Device Names in WFB Files:**
- Maui → `Maui`
- Rio → `Rio`
- Tropez → `Tropez`
- Tropez Plus → `TBS-2001` (model number)

This matches the actual WFB files distributed by Turtle Beach and ensures full compatibility with the WaveFront ecosystem.
