#ifndef RIFFWAVE_H
#define RIFFWAVE_H

#include <stdint.h>
#include <stdio.h>

typedef struct {
	uint16_t format_tag;
	uint16_t channels;
	uint32_t sample_rate;
	uint32_t byte_rate;
	uint16_t block_align;
	uint16_t bits_per_sample;
	uint32_t data_offset;
	uint32_t data_size;
} WavInfo;

int wav_open(FILE* f, WavInfo* info);
double wav_get_sample(FILE* f, const WavInfo* info);

#endif // RIFFWAVE_H