#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <sysexits.h>

#include "pmi.h"
#include "tga.h"

int usage(char* name) {
	printf("namco pmi tool\n");
	printf("usage:\n");
	printf("    %s hed <input.pmi>\n", name);
	printf("    %s dec <input.pmi> <output.tga>\n", name);
	printf("    %s enc <input.tga> <output.pmi>\n", name);

	return EX_USAGE;
}

int check_pmi(char* filename) {
	return EX_OK;
}

int decode_pmi(char* inputFilename, char* outputFilename) {
	return EX_OK;
}

int encode_pmi(char* inputFilename, char* outputFilename) {
	return EX_OK;
}

int main(int argc, char** argv) {
	if (argc < 2) {
		return usage(argv[0]);
	}

	if (strcmp(argv[1], "hed") == 0) {
		if (argc < 3) {
			return usage(argv[0]);
		}

		return check_pmi(argv[2]);
	}

	if (strcmp(argv[1], "dec") == 0) {
		if (argc < 4) {
			return usage(argv[0]);
		}

		return decode_pmi(argv[2], argv[3]);
	}

	if (strcmp(argv[1], "enc") == 0) {
		if (argc < 4) {
			return usage(argv[0]);
		}

		return encode_pmi(argv[2], argv[3]);
	}

	printf("bad args\n");
	return usage(argv[0]);
}