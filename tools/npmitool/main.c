#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <sysexits.h>

#include "pmi.h"
#include "vendor/lodepng.h"

typedef struct config_s {
	int dont_convert_alpha;
	int render_transparency;
	int force_mode;
	int palette_size;
	char *input;
	char *output;
} config_t;

void unswizzle_palette(uint32_t *pal, uint16_t clutLen) {
	if (!pal || clutLen == 0) return;

	uint32_t temp[256] = {0};
	int colors_to_swizzle = clutLen / 4;
	
	for (int i = 0; i < colors_to_swizzle && i < 256; i++) {
		int j = (i & 0xE7) | ((i & 0x08) << 1) | ((i & 0x10) >> 1);

		if (j < 256) {
			temp[j] = pal[i];
		}
	}

	memcpy(pal, temp, clutLen);
}

const char* psm_to_str(GS_PSM mode) {
	switch (mode) {
		case GS_PSM_CT32:  return "CT32 (RGBA 32-bit)";
		case GS_PSM_CT24:  return "CT24 (RGB 24-bit)";
		case GS_PSM_CT16:  return "CT16 (RGBA 16-bit 5551)";
		case GS_PSM_CT16S: return "CT16S (16-bit Signed)";
		case GS_PSM_T8:	return "T8 (8-bit Indexed)";
		case GS_PSM_T4:	return "T4 (4-bit Indexed)";
		case GS_PSM_T8H:   return "T8H (8-bit Indexed High)";
		case GS_PSM_T4HL:  return "T4HL (4-bit Indexed High-Low)";
		case GS_PSM_T4HH:  return "T4HH (4-bit Indexed High-High)";
		case GS_PSM_Z32:   return "Z32 (32-bit Depth)";
		case GS_PSM_Z24:   return "Z24 (24-bit Depth)";
		case GS_PSM_Z16:   return "Z16 (16-bit Depth)";
		case GS_PSM_Z16S:  return "Z16S (16-bit Signed Depth)";
		default:		   return "invalid mode";
	}
}

int check_pmi(char* filename) {
	FILE *f = fopen(filename, "rb");
	if (!f) return EX_NOINPUT;

	PMI header;
	if (fread(&header, sizeof(PMI), 1, f) != 1) {
		fclose(f);
		return EX_DATAERR;
	}

	if (memcmp(header.magic, "PMI\0", 4) != 0) {
		printf("bad magic (expected 'PMI\\0')\n");
		fclose(f);
		return EX_DATAERR;
	}

	printf("%s:\n", filename);
	printf("    dimensions: %dx%d\n", header.width, header.height);
	printf("    bit depth: %d-bit\n", header.depth);
	printf("    mode: %s (0x%0X)\n", psm_to_str(header.pmode), header.pmode);
	printf("    CLUT palette: %d bytes\n", header.clutLen);

	fclose(f);
	return EX_OK;
}

static uint8_t ALPHA_PS2_TO_PC[256] = {
	[0] = 0, [64] = 128, [128] = 255, 
};

static uint8_t ALPHA_PC_TO_PS2[256] = {
	[0] = 0, [128] = 64, [255] = 128,
};

void init_alpha_luts() {
	for (int i = 0; i < 256; i++) {
		float scale = (float)i / 128.0f;
		int val = (int)(scale * 255.0f);
		ALPHA_PS2_TO_PC[i] = (val > 255) ? 255 : (uint8_t)val;
	}
	ALPHA_PS2_TO_PC[128] = 255;

	for (int i = 0; i < 256; i++) {
		float scale = (float)i / 255.0f;
		ALPHA_PC_TO_PS2[i] = (uint8_t)(scale * 128.0f);
	}
	ALPHA_PC_TO_PS2[255] = 128;
}

int decode_pmi(config_t *cfg) {
	FILE *ifp = fopen(cfg->input, "rb");
	if (!ifp) return EX_NOINPUT;

	PMI header;
	if (fread(&header, sizeof(PMI), 1, ifp) != 1) {
		fclose(ifp);
		return EX_DATAERR;
	}

	if (memcmp(header.magic, "PMI\0", 4) != 0) {
		fclose(ifp);
		return EX_DATAERR;
	}

	uint32_t pixelDataSize = (header.width * header.height * header.depth) / 8;
	uint8_t *palette = (header.clutLen > 0) ? malloc(header.clutLen) : NULL;
	uint8_t *raw_data = malloc(pixelDataSize);

	fseek(ifp, 0x20, SEEK_SET);
	if (palette) if (!fread(palette, 1, header.clutLen, ifp)) { return EX_NOINPUT; };
	if (!fread(raw_data, 1, pixelDataSize, ifp)) { return EX_NOINPUT; };
	fclose(ifp);

	LodePNGState state;
	lodepng_state_init(&state);
	unsigned char* out_pixels = NULL;
	int status = EX_OK;

	if (header.pmode == GS_PSM_T8 || header.pmode == GS_PSM_T4) {
		if (palette) {
			unswizzle_palette((uint32_t*)palette, header.clutLen);
			
			if (!cfg->dont_convert_alpha) {
				for (int i = 0; i < (header.clutLen / 4); i++) {
					palette[i * 4 + 3] = ALPHA_PS2_TO_PC[palette[i * 4 + 3]];
				}
			}

			state.info_raw.colortype = LCT_PALETTE;
			state.info_raw.bitdepth = 8;
			state.info_png.color.colortype = LCT_PALETTE;
			state.info_png.color.bitdepth = 8;

			for (size_t i = 0; i < header.clutLen / 4; i++) {
				lodepng_palette_add(&state.info_png.color, palette[i*4], palette[i*4+1], palette[i*4+2], palette[i*4+3]);
				lodepng_palette_add(&state.info_raw, palette[i*4], palette[i*4+1], palette[i*4+2], palette[i*4+3]);
			}
		}

		uint32_t total_pixels = header.width * header.height;
		out_pixels = malloc(total_pixels);

		if (header.pmode == GS_PSM_T8) {
			memcpy(out_pixels, raw_data, total_pixels);
		} else {
			for (size_t i = 0; i < total_pixels; i++) {
				uint8_t byte = raw_data[i >> 1];
				out_pixels[i] = (i & 1) ? (byte >> 4) : (byte & 0x0F);
			}   
		}
	} else if (header.pmode == GS_PSM_CT16) {
		state.info_raw.colortype = LCT_RGBA;
		state.info_raw.bitdepth = 8;
		state.info_png.color.colortype = LCT_RGBA;
		state.info_png.color.bitdepth = 8;

		out_pixels = malloc(header.width * header.height * 4);
		uint16_t *p16 = (uint16_t*)raw_data;

		for (size_t i = 0; i < (size_t)header.width * header.height; i++) {
			uint16_t p = p16[i];
			uint8_t r_5 = (p & 0x1F), g_5 = (p >> 5) & 0x1F, b_5 = (p >> 10) & 0x1F;

			out_pixels[i * 4 + 0] = (r_5 << 3) | (r_5 >> 2);
			out_pixels[i * 4 + 1] = (g_5 << 3) | (g_5 >> 2);
			out_pixels[i * 4 + 2] = (b_5 << 3) | (b_5 >> 2);
			out_pixels[i * 4 + 3] = (p & 0x8000) ? (cfg->dont_convert_alpha ? 128 : 255) : 0;
		}
	} else if (header.pmode == GS_PSM_CT32) {
		state.info_raw.colortype = LCT_RGBA;
		state.info_raw.bitdepth = 8;
		state.info_png.color.colortype = LCT_RGBA;
		state.info_png.color.bitdepth = 8;

		uint32_t total_pixels = header.width * header.height;
		out_pixels = malloc(total_pixels * 4);

		for (size_t i = 0; i < total_pixels; i++) {
			out_pixels[i * 4 + 0] = raw_data[i * 4 + 0];
			out_pixels[i * 4 + 1] = raw_data[i * 4 + 1];
			out_pixels[i * 4 + 2] = raw_data[i * 4 + 2];
			
			uint8_t a = raw_data[i * 4 + 3];
			if (!cfg->dont_convert_alpha) {
				out_pixels[i * 4 + 3] = ALPHA_PS2_TO_PC[a];
			} else {
				out_pixels[i * 4 + 3] = a;
			}
		}
	} else {
		printf("mode %s (0x%0X) not supported for decoding\n", psm_to_str(header.pmode), header.pmode);
		status = EX_SOFTWARE;
	}

	if (status == EX_OK) {
		unsigned char* buffer;
		size_t buffersize;
		unsigned error = lodepng_encode(&buffer, &buffersize, out_pixels, header.width, header.height, &state);
		if (!error) {
			lodepng_save_file(buffer, buffersize, cfg->output);
		} else {
			fprintf(stderr, "lodepng error %u: %s\n", error, lodepng_error_text(error));
			status = EX_SOFTWARE;
		}
		free(buffer);
	}

	lodepng_state_cleanup(&state);
	if (out_pixels) free(out_pixels);
	if (raw_data) free(raw_data);
	if (palette) free(palette);

	return status;
}

int encode_pmi(config_t *cfg) {
	unsigned char* png_data = NULL;
	size_t png_size;
	unsigned char* raw_pixels = NULL;
	unsigned width, height;
	LodePNGState state;

	lodepng_state_init(&state);
	unsigned error = lodepng_load_file(&png_data, &png_size, cfg->input);
	if (error) return EX_NOINPUT;

	error = lodepng_decode32(&raw_pixels, &width, &height, png_data, png_size);
	if (error) {
		lodepng_state_cleanup(&state);
		return EX_DATAERR;
	}

	GS_PSM mode = (cfg->force_mode != -1) ? (GS_PSM)cfg->force_mode : GS_PSM_T8;
	
	PMI pmi = {0};
	memcpy(pmi.magic, "PMI\0", 4);
	pmi.header = 24;
	pmi.version = 0.33f;
	pmi.width = (uint16_t)width;
	pmi.height = (uint16_t)height;
	pmi.tw = (uint8_t)log2(width);
	pmi.th = (uint8_t)log2(height);
	pmi.pmode = mode;
	pmi.trans = cfg->render_transparency;

	uint8_t *palette_buf = NULL;
	uint8_t *final_pixels = NULL;
	uint32_t final_pixel_size = 0;

	if (mode == GS_PSM_T8 || mode == GS_PSM_T4) {
		pmi.depth = (mode == GS_PSM_T8) ? 8 : 4;
		pmi.clutLen = cfg->palette_size;
		palette_buf = calloc(1, pmi.clutLen);
		uint8_t *temp_indices = malloc(width * height);

		LodePNGState check_state;
		lodepng_state_init(&check_state);

		check_state.info_raw.colortype = LCT_PALETTE;
		check_state.info_raw.bitdepth = 8;

		unsigned char* png_pixels = NULL;
		unsigned w, h;
		
		lodepng_decode(&png_pixels, &w, &h, &check_state, png_data, png_size);

		if (check_state.info_png.color.colortype == LCT_PALETTE) {
			printf("writing palette (converted)\n");

			for (size_t i = 0; i < check_state.info_png.color.palettesize && i < 256; i++) {
				uint8_t r = check_state.info_png.color.palette[i * 4 + 0];
				uint8_t g = check_state.info_png.color.palette[i * 4 + 1];
				uint8_t b = check_state.info_png.color.palette[i * 4 + 2];
				uint8_t a = check_state.info_png.color.palette[i * 4 + 3];

				if (!cfg->dont_convert_alpha) {
					a = ALPHA_PC_TO_PS2[a];
				}

				palette_buf[i * 4 + 0] = r;
				palette_buf[i * 4 + 1] = g;
				palette_buf[i * 4 + 2] = b;
				palette_buf[i * 4 + 3] = a;
			}

			memcpy(temp_indices, png_pixels, width * height);
		} else {
			printf("writing palette (generated)\n");

			// lazy algorithm for palettes (this is good unless you use > 255 colors)
			int color_count = 0;
			for (size_t i = 0; i < width * height; i++) {
				uint8_t r = raw_pixels[i*4+0];
				uint8_t g = raw_pixels[i*4+1];
				uint8_t b = raw_pixels[i*4+2];
				uint8_t a = raw_pixels[i*4+3];

				if (!cfg->dont_convert_alpha) {
					a = a / 2; 
				}

				// check for existing colors (skip making a new entry)
				int found_idx = -1;
				for (int c = 0; c < color_count; c++) {
					if (palette_buf[c*4+0] == r && palette_buf[c*4+1] == g && 
						palette_buf[c*4+2] == b && palette_buf[c*4+3] == a) {
						found_idx = c;
						break;
					}
				}

				if (found_idx == -1) {
					if (color_count >= 256) {
						// TODO: use an advanced algorithm to find 256 colors to fit into the limit if we're using > 256 colors
						fprintf(stderr, "too many colors in PNG for mode %s (0x%0X)\n", psm_to_str(mode), mode);
						return EX_DATAERR;
					}

					palette_buf[color_count*4+0] = r;
					palette_buf[color_count*4+1] = g;
					palette_buf[color_count*4+2] = b;
					palette_buf[color_count*4+3] = a;
					found_idx = color_count++;
				}
				temp_indices[i] = (uint8_t)found_idx;
			}
		}

		unswizzle_palette((uint32_t*)palette_buf, pmi.clutLen);

		if (mode == GS_PSM_T8) {
			final_pixel_size = width * height;
			final_pixels = temp_indices;
		} else {
			final_pixel_size = (width * height) / 2;
			final_pixels = malloc(final_pixel_size);
			for (size_t i = 0; i < final_pixel_size; i++) {
				final_pixels[i] = (temp_indices[i*2+1] << 4) | (temp_indices[i*2] & 0x0F);
			}
			free(temp_indices);
		}
		free(png_pixels);
		lodepng_state_cleanup(&check_state);
	}
	else if (mode == GS_PSM_CT16) {
		pmi.depth = 16;
		final_pixel_size = width * height * 2;
		final_pixels = malloc(final_pixel_size);
		uint16_t *p16 = (uint16_t*)final_pixels;
		for (size_t i = 0; i < width * height; i++) {
			uint8_t r = raw_pixels[i*4+0] >> 3;
			uint8_t g = raw_pixels[i*4+1] >> 3;
			uint8_t b = raw_pixels[i*4+2] >> 3;
			uint8_t a = (raw_pixels[i*4+3] > 128) ? 1 : 0;
			p16[i] = (a << 15) | (b << 10) | (g << 5) | r;
		}
	}
	else if (mode == GS_PSM_CT32) {
		pmi.depth = 32;
		final_pixel_size = width * height * 4;
		final_pixels = malloc(final_pixel_size);
		for (size_t i = 0; i < width * height; i++) {
			final_pixels[i*4+0] = raw_pixels[i*4+0];
			final_pixels[i*4+1] = raw_pixels[i*4+1];
			final_pixels[i*4+2] = raw_pixels[i*4+2];
			uint8_t a = raw_pixels[i*4+3];
			if (!cfg->dont_convert_alpha) a = ALPHA_PC_TO_PS2[a];
			final_pixels[i*4+3] = a;
		}
	}
	else {
		printf("mode %s (0x%0X) not supported for encoding\n", psm_to_str(mode), mode);
		return EX_SOFTWARE;
	}

	FILE *ofp = fopen(cfg->output, "wb");
	if (!ofp) return EX_CANTCREAT;

	fwrite(&pmi, sizeof(PMI), 1, ofp);
	fseek(ofp, 0x20, SEEK_SET);
	if (palette_buf) fwrite(palette_buf, 1, pmi.clutLen, ofp);
	fwrite(final_pixels, 1, final_pixel_size, ofp);

	fclose(ofp);
	free(raw_pixels); free(palette_buf); free(final_pixels); free(png_data);
	lodepng_state_cleanup(&state);
	return EX_OK;
}

int usage(char* name) {
	printf("namco pmi tool\n");
	printf("usage:\n");
	printf("    %s hed <input.pmi>\n", name);
	printf("    %s dec [options] <input.pmi> <output.png>\n", name);
	printf("    %s enc [options] <input.png> <output.pmi>\n", name);
	printf("options:\n");
	printf("    -a, --dont-convert-alpha    bypasses the PS2/PC alpha LUT conversion, you probably don't want to do this\n");
	printf("    -t, --render-transparency   set 'trans' header flag to 0x01, this may be needed for some sprites\n");
	printf("    -m, --mode <id>             set the GS_PSM mode to use for encoding (0:CT32, 2:CT16, 19:T8, 20:T4)\n");
	printf("    -p, --palette-size <len>    sets the CLUT length in bytes (default: 1024)\n");

	return EX_USAGE;
}

int main(int argc, char** argv) {
	if (argc < 3) return usage(argv[0]);
	init_alpha_luts();

	config_t cfg = {0};
	cfg.palette_size = 1024;
	cfg.force_mode = -1;
	char *cmd = argv[1];

	for (int i = 2; i < argc; i++) {
		if (strcmp(argv[i], "-a") == 0 || strcmp(argv[i], "--dont-convert-alpha") == 0) {
			cfg.dont_convert_alpha = 1;
		} else if (strcmp(argv[i], "-t") == 0 || strcmp(argv[i], "--render-transparency") == 0) {
			cfg.render_transparency = 1;
		} else if (strcmp(argv[i], "-m") == 0 && i + 1 < argc) {
			cfg.force_mode = atoi(argv[++i]);
		} else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
			cfg.palette_size = atoi(argv[++i]);
		} else if (cfg.input == NULL) {
			cfg.input = argv[i];
		} else if (cfg.output == NULL) {
			cfg.output = argv[i];
		}
	}

	if (cfg.input != NULL && cfg.output != NULL) {
		if (strcmp(cfg.input, cfg.output) == 0) {
			printf("input and output cannot be the same\n");
			return EX_USAGE;
		}
	}

	if (strcmp(cmd, "hed") == 0) return check_pmi(cfg.input);
	if (strcmp(cmd, "dec") == 0) return decode_pmi(&cfg);
	if (strcmp(cmd, "enc") == 0) return encode_pmi(&cfg);

	return usage(argv[0]);
}