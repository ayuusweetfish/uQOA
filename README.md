# µQOA

µQOA (Micro QOA) is an encoder/decoder implementation for the [Quite OK Audio Format (QOA)](https://qoaformat.org/), targeted at non-desktop environments (e.g. microcontrollers).

µQOA can be ideal if a minimal lossy audio codec is needed in a small environment with reasonable storage (QOA achieves 3.2 bits per sample).

Features include:
- < 400 bytes compiled code and data (decoder only, ARMv7)
- Explicit integer width for calculation
- Agnostic on file formats; works directly with data slices

## Usage

```c
#define uQOA_IMPL  // Define in one and only one place
#include "uqoa.h"  // Include where necessary
```

The structure `qoa_lms` holds a 16-byte internal state. There is one function for decoding a 64-bit slice into 20 samples, and another for the other way round. Refer to `test.c` for a minimal example.

## Data format

Rather than the standard QOA file format, µQOA works with individual 64-bit slices. The internal LMS filter state can be dumped occasionally to support random seeking and avoid overflows.

`qoa_conv.c` is a program that converts between binary raw sample data and a QOA data format where the state is dumped every 256 slices. It can be combined with FFmpeg to produce data for on-site use. Refer to the code for details.
