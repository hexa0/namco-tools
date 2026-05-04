#include "riffwave.h"
#include <string.h>
#include <stdlib.h>

#define WAVE_FORMAT_PCM			0x0001
#define WAVE_FORMAT_IEEE_FLOAT  0x0003
#define WAVE_FORMAT_EXTENSIBLE  0xFFFE

int wav_open(FILE* f, WavInfo* info) {
	char chunk_id[4];
	uint32_t chunk_size;
	char format[4];

	if (fread(chunk_id, 4, 1, f) != 1 || memcmp(chunk_id, "RIFF", 4) != 0) return 0;
	(void)fseek(f, 4, SEEK_CUR);
	if (fread(format, 4, 1, f) != 1 || memcmp(format, "WAVE", 4) != 0) return 0;

	while (fread(chunk_id, 4, 1, f) == 1) {
		if (fread(&chunk_size, 4, 1, f) != 1) break;
		long next_chunk = ftell(f) + chunk_size;

		if (memcmp(chunk_id, "fmt ", 4) == 0) {
			if (fread(info, 16, 1, f) != 1) return 0;

			if (info->format_tag == WAVE_FORMAT_EXTENSIBLE) {
				uint16_t ext_size;
				if (fread(&ext_size, 2, 1, f) == 1 && ext_size >= 22) {
					(void)fseek(f, 2, SEEK_CUR);
					(void)fseek(f, 4, SEEK_CUR);
					uint16_t sub_format;
					if (fread(&sub_format, 2, 1, f) == 1) {
						info->format_tag = sub_format;
					}
				}
			}
		} 
		else if (memcmp(chunk_id, "data", 4) == 0) {
			info->data_offset = (uint32_t)ftell(f);
			info->data_size = chunk_size;
			return 1;
		}

		fseek(f, next_chunk + (next_chunk % 2), SEEK_SET);
	}

	return 0;
}

double wav_get_sample(FILE* f, const WavInfo* info) {
	if (info->format_tag == WAVE_FORMAT_IEEE_FLOAT) {
		float fv;
		if (fread(&fv, 4, 1, f) != 1) return 0.0;
		return (double)fv * 32768.0;
	} 

	if (info->format_tag == WAVE_FORMAT_PCM) {
		switch (info->bits_per_sample) {
			case 8: {
				uint8_t u8;
				if (fread(&u8, 1, 1, f) != 1) return 0.0;
				return (double)(u8 - 128) * 256.0;
			}
			case 16: {
				int16_t s16;
				if (fread(&s16, 2, 1, f) != 1) return 0.0;
				return (double)s16;
			}
			case 24: {
				uint8_t b[3];
				if (fread(b, 3, 1, f) != 1) return 0.0;

				int32_t s24 = (b[0] << 0) | (b[1] << 8) | (b[2] << 16);
				if (s24 & 0x800000) s24 |= 0xFF000000;
				return (double)s24 / 256.0;
			}
			case 32: {
				int32_t s32;
				if (fread(&s32, 4, 1, f) != 1) return 0.0;
				return (double)s32 / 65536.0;
			}
		}
	}
	return 0.0;
}