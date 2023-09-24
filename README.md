# µQOA

µQOA (Micro QOA) is an encoder/decoder implementation for the [Quite OK Audio Format (QOA)](https://qoaformat.org/), targeted at non-desktop environments (e.g. microcontrollers). Features include:
- Around 2 KiB compiled
- Explicit integer width
- Stripped-down data format
  - Single-channel only
  - No file header; frame header reduced to filter state (16 B)

## Usage

```c
#define uQOA_IMPL  // Define in one and only one place
#include "uqoa.h"  // Include where necessary
```

The structure `qoa_lms` holds a 16-byte internal state. There is one function for decoding a 64-bit slice into 20 samples, and another for the other way round. Refer to `test.c` for a minimal example.

## Data format

Rather than the standard QOA file format, µQOA works with individual 64-bit slices. The internal LMS filter state can be dumped occasionally to support random seeking and avoid overflows.

`qoa_conv.c` is a program that converts between binary raw sample data and a QOA data format where the state is dumped every 256 slices. It can be combined with FFmpeg to produce data for on-site use. Refer to the code for details.
