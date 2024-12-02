#define uQOA_IMPL
#include "uqoa.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>

// Encode
// Accepts mono audio in big-endian signed 16-bit PCM from stdin
// ffmpeg -i $NAME.mp3 -ar 44100 -ac 1 -f s16be -acodec pcm_s16be - | ./uqoa_conv > $NAME.bin
// Consider `-af 'pan=mono|c0=FL'` instead of `-ac 1`, if straight mixdown cancels sounds

// Output: blocks of 258 uint64's
// [0] - history (all big-endian)
// [1] - weights
// [2 .. 257] - slices, each uint64 represents 20 samples
// The trailing samples are truncated
// if the total number of samples is not divisible by 20

// If the `-p` (plain) option is supplied, history and weights are omitted

static inline void put_u64(uint64_t x)
{
  for (int i = 0; i < 8; i++) {
    fputc(x >> 56, stdout);
    x <<= 8;
  }
}

static int eof = 0;
static inline uint8_t silent_getchar()
{
  int c = fgetc(stdin);
  if (c == -1) eof = 1;
  return (uint8_t)c;
}

void encode(bool is_plain)
{
  qoa_lms lms = { 0 };
  uint8_t sf = 0;

  int16_t samples[20];
  int sample_count = 0;
  int slice_count = 0;

  while (!eof) {
    uint8_t msb = silent_getchar();
    uint8_t lsb = silent_getchar();
    if (eof) break;
    int16_t sample = (int16_t)(((uint16_t)msb << 8) | lsb);
    samples[sample_count++] = sample;
    if (sample_count == 20) {
      if (slice_count == 0) {
        // New frame, refresh state to eliminate near-overflows
        // Save state in data stream, if not plain format
        qoa_start_frame(&lms);
        if (!is_plain) {
          put_u64(
            (uint64_t)(uint16_t)lms.history[0] << 48 |
            (uint64_t)(uint16_t)lms.history[1] << 32 |
            (uint64_t)(uint16_t)lms.history[2] << 16 |
            (uint64_t)(uint16_t)lms.history[3] <<  0
          );
          put_u64(
            (uint64_t)(uint16_t)lms.weights[0] << 48 |
            (uint64_t)(uint16_t)lms.weights[1] << 32 |
            (uint64_t)(uint16_t)lms.weights[2] << 16 |
            (uint64_t)(uint16_t)lms.weights[3] <<  0
          );
        }
      }
      uint64_t slice = qoa_encode_slice(&lms, samples, &sf);
      put_u64(slice);
      sample_count = 0;
      if (++slice_count == 256) slice_count = 0;
    }
  }
}

// Decode
// ./uqoa_conv -d < $NAME.bin | ffmpeg -ar 44100 -ac 1 -f s16be -acodec pcm_s16be -i - $NAME.re.wav

static inline uint64_t get_u64()
{
  uint64_t x = 0;
  for (int i = 0; i < 8; i++)
    x = (x << 8) | silent_getchar();
  return x;
}

void decode(bool is_plain)
{
  qoa_lms lms = { 0 };

  while (!eof) {
    if (!is_plain) {
      // Load state from data stream
      uint64_t history = get_u64();
      for (int i = 0; i < 4; i++)
        lms.history[i] = (int16_t)((history >> ((3 - i) * 16)) & 0xFFFF);
      uint64_t weights = get_u64();
      for (int i = 0; i < 4; i++)
        lms.weights[i] = (int16_t)((weights >> ((3 - i) * 16)) & 0xFFFF);
    } else {
      // Simply reset near-overflows if any
      qoa_start_frame(&lms);
    }
    for (int n_slice = 0; n_slice < 256 && !feof(stdin); n_slice++) {
      uint64_t slice = get_u64();
      if (eof) break;
      int16_t samples[20];
      qoa_decode_slice(&lms, slice, samples);
      for (int i = 0; i < 20; i++) {
        fputc((uint8_t)((samples[i] >> 8) & 0xFF), stdout);
        fputc((uint8_t)((samples[i] >> 0) & 0xFF), stdout);
      }
    }
  }
}

int main(int argc, char *argv[])
{
  bool is_plain = false;
  bool is_decode = false;
  for (int i = 1; i < argc; i++)
    if (argv[i][0] == '-') switch (argv[i][1]) {
      case 'p': is_plain = true; break;
      case 'd': is_decode = true; break;
      case 'h':
        fprintf(stderr, "Usage: %s [-d] [-p]\n"
          "  -d  Decode (otherwise, encode)\n"
          "  -p  Plain (do not periodically dump codec state)\n"
          , argv[0]);
        return 0;
    }

  (is_decode ? decode : encode)(is_plain);

  return 0;
}
