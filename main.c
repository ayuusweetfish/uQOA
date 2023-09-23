#include <math.h>
#include <stdio.h>

#define uQOA_IMPL
#include "uqoa.h"

int main()
{
  uint32_t seed = 20230923;

  qoa_lms lms = { 0 };
  uint8_t sf = 0;

  for (int block = 0; block < 3; block++) {
    puts("------");
    int16_t samples[20];
    for (int i = 0; i < 20; i++) {
      seed = (seed * 1103515245 + 12345);
      samples[i] = (int16_t)floorf(sinf((i + 20 * block) * 0.9f) * 32767.5f) + (seed % 1024);
      printf("%6d%c", samples[i], i == 19 ? '\n' : ' ');
    }

    qoa_lms lms_start = lms;
    for (int i = 0; i < 4; i++) printf("%04hx", lms.history[i]);
    for (int i = 0; i < 4; i++) printf("%04hx", lms.weights[i]);
    putchar(' ');

    sf = 0;
    uint64_t slice = qoa_encode_slice(&lms, samples, &sf);
    printf("%016llx\n", slice);

    lms = lms_start;
    qoa_decode_slice(&lms, slice, samples);
    for (int i = 0; i < 20; i++)
      printf("%6d%c", samples[i], i == 19 ? '\n' : ' ');
  }

  return 0;
}
