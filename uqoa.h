// SPDX-License-Identifier: MIT

#ifndef _uQOA_H_
#define _uQOA_H_

#include <stdint.h>

typedef struct {
  int16_t history[4];
  int16_t weights[4];
} qoa_lms;

void qoa_start_frame(qoa_lms *lms);
uint64_t qoa_encode_slice(qoa_lms *lms, int16_t samples[20], uint8_t *sf_hint);

void qoa_decode_slice(qoa_lms *lms, uint64_t slice, int16_t out_samples[20]);

#ifdef uQOA_IMPL
static int32_t qoa_lms_predict(qoa_lms *lms)
{
  int64_t prediction = 0;
  for (int i = 0; i < 4; i++)
    prediction += (lms->weights[i] * lms->history[i]) >> 2;
  return prediction >> 11;
}

static void qoa_lms_update(qoa_lms *lms, int16_t sample, int16_t residual)
{
  int16_t delta = residual >> 4;
  for (int i = 0; i < 4; i++)
    lms->weights[i] += lms->history[i] < 0 ? -delta : delta;
  for (int i = 0; i < 3; i++)
    lms->history[i] = lms->history[i + 1];
  lms->history[3] = sample;
}

// Quantization and dequantization

// scalefactor[s] = round(pow(s + 1, 2.75))
// dqt = {0.75, 2.5, 4.5, 7}

static inline int16_t qoa_quant(int32_t v, int16_t sf)
{
  // |v| <= 65535
  // Calculate r = v / scalefactor[sf]
  // reciprocal[s] = ceil(32768 / scalefactor[s])
  static const uint16_t reciprocal[16] = {
    32768, 4682, 1561, 729, 391, 238, 156, 108, 78, 59, 45, 36, 29, 24, 20, 16
  };
  int16_t q = (v * reciprocal[sf] + (1 << 14)) >> 15;
  q = q + ((v > 0) - (v < 0)) - ((q > 0) - (q < 0));  // Round away from 0

  // Clamp into [-8, 8]
  if (q < -8) q = -8;
  if (q >  8) q = 8;
  static const uint8_t quant[17] = {
    7, 7, 7, 5, 5, 3, 3, 1, // -8..-1
    0,                      //  0
    0, 2, 2, 4, 4, 6, 6, 6  //  1.. 8
  };
  return quant[8 + q];
}

static inline int16_t qoa_dequant(int32_t s, int8_t q)
{
  // lookup[s][q] = round_away_0(scalefactor[s] * dqt[q])
  static const int16_t lookup[16][4] = {
    {   1,    3,    5,     7},
    {   5,   18,   32,    49},
    {  16,   53,   95,   147},
    {  34,  113,  203,   315},
    {  63,  210,  378,   588},
    { 104,  345,  621,   966},
    { 158,  528,  950,  1477},
    { 228,  760, 1368,  2128},
    { 316, 1053, 1895,  2947},
    { 422, 1405, 2529,  3934},
    { 548, 1828, 3290,  5117},
    { 696, 2320, 4176,  6496},
    { 868, 2893, 5207,  8099},
    {1064, 3548, 6386,  9933},
    {1286, 4288, 7718, 12005},
    {1536, 5120, 9216, 14336},
  };
  int16_t r = lookup[s][q / 2];
  return (q & 1) ? -r : r;
}

static inline int16_t qoa_sat_s16(int32_t x)
{
  if (x >  0x7fff) return  0x7fff;
  if (x < -0x8000) return -0x8000;
  return x;
}

// Public API implementations
// Encoder

void qoa_start_frame(qoa_lms *lms)
{
  // Cannot check once at the end due to the edge case
  // where all four weights equal 32768 and `sq_sum` wraps around to 0
  uint32_t sq_sum = 0;
  for (int i = 0; i < 4; i++) {
    sq_sum += (int32_t)lms->weights[i] * lms->weights[i];
    if (sq_sum > 0x2fffffff) {
      for (int i = 0; i < 4; i++) lms->weights[i] = 0;
      break;
    }
  }
  // Otherwise if all weights are zero, initialize weights to {0, 0, -1, 2}
  if (sq_sum == 0) {
    lms->weights[2] = -(1 << 13);
    lms->weights[3] =  (1 << 14);
  }
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
      int16_t quantized = qoa_quant(sample - predicted, sf);

      // Reconstruct sample by quantized value and calculate error
      int16_t dequantized = qoa_dequant(sf, quantized);
      int16_t reconstructed = qoa_sat_s16(predicted + dequantized);

      int16_t error = sample - reconstructed;
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
  }

  *lms = best_lms;
  *sf_hint = best_sf;
  return best_slice;
}

// Decoder

void qoa_decode_slice(qoa_lms *lms, uint64_t slice, int16_t out_samples[20])
{
  uint8_t sf = slice >> 60;
  for (int i = 0; i < 20; i++) {
    int32_t predicted = qoa_lms_predict(lms);
    int16_t quantized = (slice >> 57) & 0x7;
    int16_t dequantized = qoa_dequant(sf, quantized);
    int16_t reconstructed = qoa_sat_s16(predicted + dequantized);
    out_samples[i] = reconstructed;
    slice <<= 3;
    qoa_lms_update(lms, reconstructed, dequantized);
  }
}
#endif

#endif
