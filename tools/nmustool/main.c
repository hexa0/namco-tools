#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "riffwave.h"

#define BLOCK_SIZE 0x4800
#define HEADER_SIZE 0x800
#define PAYLOAD_SIZE (BLOCK_SIZE - HEADER_SIZE)

typedef struct svds_block_s {
	char head[4];
	unsigned char version;
	uint8_t pad1[4];
	unsigned char size;
	uint16_t streamId;
	uint16_t sequenceId;
	uint8_t pad2[2];
	uint16_t pitch;
	uint16_t pad3;
	uint32_t channels;
	uint8_t pad4[2021];
	char end[3];
} svds_block_t;

// sony ADPCM decoding heavily references https://git.ffmpeg.org/gitweb/ffmpeg.git/blob/HEAD:/libavcodec/adpcm.c 

static const int xa_adpcm_table[5][2] = {
	{   0,   0 },
	{  60,   0 },
	{ 115, -52 },
	{  98, -55 },
	{ 122, -60 }
};

// yes this is a duplicate of xa_adpcm_table lmao
static const int f0[] = { 0, 60, 115, 98, 122 };
static const int f1[] = { 0, 0, -52, -55, -60 };

typedef struct sony_adpcm_decode_state_s {
	int32_t s1, s2;
} sony_adpcm_decode_state_t;

void write_wav_header(FILE* f, uint32_t sampleRate, uint32_t channels, uint32_t dataSize) {
	uint32_t totalSize = 36 + dataSize;
	uint32_t byteRate = sampleRate * channels * 2;
	uint16_t blockAlign = channels * 2;

	fwrite("RIFF", 4, 1, f);
	fwrite(&totalSize, 4, 1, f);
	fwrite("WAVE", 4, 1, f);
	fwrite("fmt ", 4, 1, f);
	uint32_t fmtLen = 16;
	uint16_t fmtType = 1;
	fwrite(&fmtLen, 4, 1, f);
	fwrite(&fmtType, 2, 1, f);
	uint16_t chan16 = (uint16_t)channels;
	fwrite(&chan16, 2, 1, f);
	fwrite(&sampleRate, 4, 1, f);
	fwrite(&byteRate, 4, 1, f);
	fwrite(&blockAlign, 2, 1, f);
	uint16_t bps = 16;
	fwrite(&bps, 2, 1, f);
	fwrite("data", 4, 1, f);
	fwrite(&dataSize, 4, 1, f);
}

int16_t decode_sample(uint8_t nibble, int shift, int filter, sony_adpcm_decode_state_t* state) {
	int32_t scale = (int8_t)(nibble << 4) >> 4;
	scale <<= 12;
	int32_t sample = (scale >> shift) + ((state->s1 * xa_adpcm_table[filter][0] + state->s2 * xa_adpcm_table[filter][1]) / 64);
	state->s2 = state->s1;
	state->s1 = sample;

	if (sample > 32767) sample = 32767;
	else if (sample < -32768) sample = -32768;
	return (int16_t)sample;
}

void decode_mus(const char* in_file, const char* out_file) {
	FILE* f = fopen(in_file, "rb");
	FILE* out = fopen(out_file, "wb");
	if (!f || !out) return;

	svds_block_t first_block;
	if (fread(&first_block, sizeof(svds_block_t), 1, f) != 1) {
		fclose(f); fclose(out);
		return;
	}

	uint32_t channels = first_block.channels;
	if (channels == 0) channels = 2;
	uint32_t sampleRate = (uint32_t)((first_block.pitch * 48000.0f) / 4096.0f);
	
	fseek(f, 0, SEEK_SET);
	write_wav_header(out, sampleRate, channels, 0);

	uint8_t** stream_bufs = malloc(sizeof(uint8_t*) * channels);
	sony_adpcm_decode_state_t* states = calloc(channels, sizeof(sony_adpcm_decode_state_t));
	int16_t* interleave_row = malloc(sizeof(int16_t) * channels);

	for (uint32_t i = 0; i < channels; i++) {
		stream_bufs[i] = malloc(PAYLOAD_SIZE);
	}

	svds_block_t current_header;
	while (1) {
		int blocks_read = 0;
		for (uint32_t c = 0; c < channels; c++) {
			if (fread(&current_header, sizeof(svds_block_t), 1, f) != 1) break;
			
			int idx = current_header.streamId - 1;
			if (idx < 0 || idx >= (int)channels) idx = c; 

			if (fread(stream_bufs[idx], PAYLOAD_SIZE, 1, f) != 1) break;
			blocks_read++;
		}
		if (blocks_read < (int)channels) break;

		for (int i = 0; i < PAYLOAD_SIZE; i += 16) {
			for (int s = 0; s < 28; s++) {
				for (uint32_t ch = 0; ch < channels; ch++) {
					uint8_t* vag = &stream_bufs[ch][i];
					int shift = vag[0] & 0x0F;
					int filter = (vag[0] >> 4) & 0x0F;
					
					uint8_t byte = vag[2 + (s / 2)];
					uint8_t nibble = (s % 2 == 0) ? (byte & 0x0F) : (byte >> 4);
					
					interleave_row[ch] = decode_sample(nibble, shift, filter, &states[ch]);
				}
				fwrite(interleave_row, sizeof(int16_t), channels, out);
			}
		}
	}

	uint32_t dataSize = ftell(out) - 44;
	fseek(out, 0, SEEK_SET);
	write_wav_header(out, sampleRate, channels, dataSize);

	for (uint32_t i = 0; i < channels; i++) free(stream_bufs[i]);
	free(stream_bufs);
	free(states);
	free(interleave_row);
	fclose(f);
	fclose(out);
}

typedef struct encoder_state_s {
	double s1, s2;
} encoder_state_t;

void encode_vag_block(const double* samples, uint8_t* out_vag, encoder_state_t* state, uint8_t flags) {
	int best_filter = 0;
	int best_shift = 0;
	int64_t min_error = INT64_MAX;
	int32_t best_nibbles[28];

	int32_t s32[28];
	for (int i = 0; i < 28; i++) s32[i] = (int32_t)samples[i];

	for (int f = 0; f < 5; f++) {
		for (int s = 0; s <= 12; s++) {
			int64_t err = 0;
			int32_t s1 = (int32_t)state->s1, s2 = (int32_t)state->s2;
			int32_t test_nibbles[28];
			int shift_val = 12 - s;

			for (int i = 0; i < 28; i++) {
				int32_t predict = (s1 * f0[f] + s2 * f1[f]) >> 6;
				int32_t target = s32[i] - predict;
				
				int32_t nibble = (target + (1 << (shift_val - 1))) >> shift_val;
				if (nibble > 7) nibble = 7;
				else if (nibble < -8) nibble = -8;

				test_nibbles[i] = nibble;
				
				int32_t decoded = (nibble << shift_val) + predict;
				s2 = s1;
				s1 = decoded;

				int64_t diff = s32[i] - decoded;
				err += diff * diff;

				if (err >= min_error) break; 
			}

			if (err < min_error) {
				min_error = err;
				best_filter = f;
				best_shift = s;
				memcpy(best_nibbles, test_nibbles, sizeof(test_nibbles));
			}
		}
	}

	out_vag[0] = (best_filter << 4) | (best_shift & 0x0F);
	out_vag[1] = flags;
	for (int i = 0; i < 28; i += 2) {
		out_vag[2 + (i / 2)] = (uint8_t)((best_nibbles[i + 1] & 0x0F) << 4) | (best_nibbles[i] & 0x0F);
	}

	for (int i = 0; i < 28; i++) {
		double predict = (state->s1 * (double)f0[best_filter] + state->s2 * (double)f1[best_filter]) / 64.0;
		state->s2 = state->s1;
		state->s1 = (double)(best_nibbles[i] << (12 - best_shift)) + predict;
	}
}

void encode_mus(const char* in_wav, const char* out_mus) {
	FILE* wf = fopen(in_wav, "rb");
	FILE* mf = fopen(out_mus, "wb");

	if (!wf) {
		fprintf(stderr, "specified input doesn't exist\n");
		return;
	}

	if (!mf) {
		fprintf(stderr, "specified output doesn't exist\n");
		return;
	}

	WavInfo wav;
	if (!wav_open(wf, &wav)) {
		fprintf(stderr, "failed to parse riff wave\n");
		fclose(wf); fclose(mf);
		return;
	}

	uint32_t pitch = (uint32_t)((wav.sample_rate * 4096.0) / 48000.0);
	encoder_state_t* states = calloc(wav.channels, sizeof(encoder_state_t));
	uint8_t* payload_bufs[8]; 
	for (int i = 0; i < wav.channels; i++) payload_bufs[i] = calloc(1, PAYLOAD_SIZE);

	int16_t seq = 0;
	int vags_per_block = PAYLOAD_SIZE / 16;
	double* block_samples = malloc(sizeof(double) * 28 * wav.channels);

	printf("encoding %u channels at %uHz (SPU1 0x%04X)...\n", wav.channels, wav.sample_rate, pitch);

	while (1) {
		long current_pos = ftell(wf);
		if (current_pos >= (wav.data_offset + wav.data_size)) break;

		long bytes_left = (wav.data_offset + wav.data_size) - current_pos;
		long samples_left = bytes_left / wav.block_align;
		
		int vags_to_encode = vags_per_block;
		if (samples_left < (vags_per_block * 28)) {
			vags_to_encode = (int)(samples_left / 28);
		}

		uint16_t adpcm_data_len = (uint16_t)(vags_to_encode * 16);
		int seq_set = seq / wav.channels;

		for (int v = 0; v < vags_to_encode; v++) {
			for (int s = 0; s < 28; s++) {
				for (int ch = 0; ch < wav.channels; ch++) {
					block_samples[s * wav.channels + ch] = wav_get_sample(wf, &wav);
				}
			}

			for (int ch = 0; ch < wav.channels; ch++) {
				double chan_temp[28];
				for (int s = 0; s < 28; s++) chan_temp[s] = block_samples[s * wav.channels + ch];
				
				encode_vag_block(chan_temp, &payload_bufs[ch][v * 16], &states[ch], 0x00);
			}
		}

		if (vags_to_encode < vags_per_block) {
			long rem = samples_left % 28;
			for(long i = 0; i < rem * wav.channels; i++) wav_get_sample(wf, &wav);
			
			for (int ch = 0; ch < wav.channels; ch++) {
				memset(&payload_bufs[ch][vags_to_encode * 16], 0, PAYLOAD_SIZE - (vags_to_encode * 16));
			}
		}

		for (int ch = 0; ch < wav.channels; ch++) {
			if (seq_set % 2 == 0) {
				payload_bufs[ch][1] = 0x06;
			} else {
				payload_bufs[ch][(vags_per_block - 1) * 16 + 1] = 0x03;
			}
		}

		for (int ch = 0; ch < wav.channels; ch++) {
			svds_block_t header = {0};
			memcpy(header.head, "Svds", 4);
			header.version = 0x01;
			header.streamId = ch + 1;
			*((uint16_t*)&header.pad1[3]) = adpcm_data_len; 
			header.sequenceId = seq;
			header.pitch = pitch;
			header.channels = wav.channels;
			memset(header.pad4, 0, 2021);
			memcpy(header.end, "end", 3);
			
			fwrite(&header, sizeof(svds_block_t), 1, mf);
			fwrite(payload_bufs[ch], PAYLOAD_SIZE, 1, mf);
			seq++;
		}
		
		printf("\rencoded sequence: %d", seq);
		fflush(stdout);

		if (vags_to_encode < vags_per_block) break;
	}

	long current_fpos = ftell(mf);
	const long ALIGNMENT = 0x9000; 
	long target_size = ((current_fpos + (ALIGNMENT - 1)) / ALIGNMENT) * ALIGNMENT;

	int final_seq_set = (seq / wav.channels) - 1;
	if (final_seq_set % 2 == 0) {
		target_size += (BLOCK_SIZE * wav.channels);
	}

	uint8_t* null_payload = calloc(1, PAYLOAD_SIZE);
	while (ftell(mf) < target_size) {
		int seq_set = seq / wav.channels;
		for (int ch = 0; ch < wav.channels; ch++) {
			svds_block_t pad_header = {0};
			memcpy(pad_header.head, "Svds", 4);
			pad_header.version = 0x01;
			pad_header.streamId = ch + 1;
			*((uint16_t*)&pad_header.pad1[3]) = 0;
			pad_header.sequenceId = seq;
			pad_header.pitch = pitch;
			pad_header.channels = wav.channels;
			memcpy(pad_header.end, "end", 3);

			memset(null_payload, 0, PAYLOAD_SIZE);
			if (seq_set % 2 == 0) {
				null_payload[1] = 0x06;
			} else {
				null_payload[(vags_per_block - 1) * 16 + 1] = 0x03;
			}

			fwrite(&pad_header, sizeof(svds_block_t), 1, mf);
			fwrite(null_payload, PAYLOAD_SIZE, 1, mf);
		}
		seq++;
	}
	free(null_payload);

	for (int i = 0; i < wav.channels; i++) {
		free(payload_bufs[i]);
	}

	free(states);
	free(block_samples);
	fclose(wf);
	fclose(mf);
}

void usage(char* name) {
	printf("namco mus tool (AUDIO.IRX 1.24)\n");
	printf("usage:\n");
	printf("    %s dec <input.mus> <output.wav>\n", name);
	printf("    %s enc <input.wav> <output.mus>\n", name);
}

int main(int argc, char** argv) {
	if (argc < 4) {
		usage(argv[0]);
		return 64;
	}

	if (strcmp(argv[1], "dec") == 0) {
		decode_mus(argv[2], argv[3]);
		return 0;
	}

	if (strcmp(argv[1], "enc") == 0) {
		encode_mus(argv[2], argv[3]);
		return 0;
	}

	printf("bad args\n");
	usage(argv[0]);
	return 64;
}