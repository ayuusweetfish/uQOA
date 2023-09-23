#ifndef _uQOA_H_
#define _uQOA_H_

#include <stdint.h>

#define QOA_LMS_LEN 4
typedef struct {
  int16_t history[QOA_LMS_LEN];
  int16_t weights[QOA_LMS_LEN];
} qoa_lms;

void qoa_start_frame(qoa_lms *lms);
uint64_t qoa_encode_slice(qoa_lms *lms, int16_t samples[20], uint8_t *sf_hint);

void qoa_decode_slice(qoa_lms *lms, uint64_t slice, int16_t out_samples[20]);

#ifdef uQOA_IMPL
static int32_t qoa_lms_predict(qoa_lms *lms)
{
  int64_t prediction = 0;
  for (int i = 0; i < QOA_LMS_LEN; i++)
    prediction += (lms->weights[i] * lms->history[i]) >> 2;
  return prediction >> 11;
}

static void qoa_lms_update(qoa_lms *lms, int16_t sample, int16_t residual)
{
  int16_t delta = residual >> 4;
  for (int i = 0; i < QOA_LMS_LEN; i++)
    lms->weights[i] += lms->history[i] < 0 ? -delta : delta;
  for (int i = 0; i < QOA_LMS_LEN - 1; i++)
    lms->history[i] = lms->history[i + 1];
  lms->history[QOA_LMS_LEN - 1] = sample;
}

static const int16_t qoa_scalefactor_tab[16] = {
  1, 7, 21, 45, 84, 138, 211, 304, 421, 562, 731, 928, 1157, 1419, 1715, 2048
};
static const int32_t qoa_reciprocal_tab[16] = {
  65536, 9363, 3121, 1457, 781, 475, 311, 216, 156, 117, 90, 71, 57, 47, 39, 32
};
static const int16_t qoa_dequant_tab[16][8] = {
  {   1,    -1,    3,    -3,    5,    -5,     7,     -7},
  {   5,    -5,   18,   -18,   32,   -32,    49,    -49},
  {  16,   -16,   53,   -53,   95,   -95,   147,   -147},
  {  34,   -34,  113,  -113,  203,  -203,   315,   -315},
  {  63,   -63,  210,  -210,  378,  -378,   588,   -588},
  { 104,  -104,  345,  -345,  621,  -621,   966,   -966},
  { 158,  -158,  528,  -528,  950,  -950,  1477,  -1477},
  { 228,  -228,  760,  -760, 1368, -1368,  2128,  -2128},
  { 316,  -316, 1053, -1053, 1895, -1895,  2947,  -2947},
  { 422,  -422, 1405, -1405, 2529, -2529,  3934,  -3934},
  { 548,  -548, 1828, -1828, 3290, -3290,  5117,  -5117},
  { 696,  -696, 2320, -2320, 4176, -4176,  6496,  -6496},
  { 868,  -868, 2893, -2893, 5207, -5207,  8099,  -8099},
  {1064, -1064, 3548, -3548, 6386, -6386,  9933,  -9933},
  {1286, -1286, 4288, -4288, 7718, -7718, 12005, -12005},
  {1536, -1536, 5120, -5120, 9216, -9216, 14336, -14336},
};
static const uint8_t qoa_quant_tab[17] = {
  7, 7, 7, 5, 5, 3, 3, 1, /* -8..-1 */
  0,                      /*  0     */
  0, 2, 2, 4, 4, 6, 6, 6  /*  1.. 8 */
};

static inline int16_t qoa_div(int32_t v, int16_t scalefactor) {
  int32_t reciprocal = qoa_reciprocal_tab[scalefactor];
  int16_t n = (v * reciprocal + (1 << 15)) >> 16;
  n = n + ((v > 0) - (v < 0)) - ((n > 0) - (n < 0)); /* round away from 0 */
  return n;
}
#define qoa_clamp(_x, _l, _h) ((_x) < (_l) ? (_l) : (_x) > (_h) ? (_h) : (_x))

static inline int16_t qoa_sat_s16(int32_t x)
{
  if (x >  0x7fff) return  0x7fff;
  if (x < -0x8000) return -0x8000;
  return x;
}

void qoa_start_frame(qoa_lms *lms)
{
  uint32_t sq_sum = 0;
  for (int i = 0; i < QOA_LMS_LEN; i++)
    sq_sum += (int32_t)lms->weights[i] * lms->weights[i];
  if (sq_sum > 0x2fffffff)
    for (int i = 0; i < QOA_LMS_LEN; i++) lms->weights[i] = 0;
}

uint64_t qoa_encode_slice(qoa_lms *lms, int16_t samples[20], uint8_t *sf_hint)
{
  uint64_t best_error = (uint64_t)-1;
  uint64_t best_slice;
  qoa_lms best_lms;
  uint8_t best_sf;

  for (uint8_t sfi = 0; sfi < 16; sfi++) {
    uint8_t sf = (*sf_hint + sfi) % 16;
    qoa_lms current_lms = *lms;
    uint64_t current_error = 0;
    uint64_t current_slice = sf;

    for (int i = 0; i < 20; i++) {
      int16_t sample = samples[i];
      int32_t predicted = qoa_lms_predict(&current_lms);
      int32_t residual = sample - predicted;
      int16_t scaled = qoa_div(residual, sf);
      int16_t quantized = qoa_quant_tab[8 + qoa_clamp(scaled, -8, 8)];
      int16_t dequantized = qoa_dequant_tab[sf][quantized];
      int16_t reconstructed = qoa_sat_s16(predicted + dequantized);

      int16_t error = sample - reconstructed;
      // printf("%d %d %d %d\n", (int)sample, (int)quantized, (int)reconstructed, (int)error);
      current_error += (int32_t)error * error;
      if (current_error > best_error) break;

      qoa_lms_update(&current_lms, reconstructed, dequantized);
      current_slice = (current_slice << 3) | quantized;
    }

    if (current_error < best_error) {
      best_error = current_error;
      best_slice = current_slice;
      best_lms = current_lms;
      best_sf = sf;
    }
    // printf("%2d %016llx %llu\n", sf, current_slice, current_error / 20);
  }

  *lms = best_lms;
  *sf_hint = best_sf;
  return best_slice;
}

void qoa_decode_slice(qoa_lms *lms, uint64_t slice, int16_t out_samples[20])
{
  int sf = slice >> 60;
  for (int i = 0; i < 20; i++) {
    int32_t predicted = qoa_lms_predict(lms);
    int16_t quantized = (slice >> 57) & 0x7;
    int16_t dequantized = qoa_dequant_tab[sf][quantized];
    int16_t reconstructed = qoa_sat_s16(predicted + dequantized);
    out_samples[i] = reconstructed;
    slice <<= 3;
    qoa_lms_update(lms, reconstructed, dequantized);
  }
}
#endif

#endif
