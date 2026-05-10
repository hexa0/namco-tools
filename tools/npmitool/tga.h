#ifndef TGA_H
#define TGA_H

#include <stdint.h>

#pragma pack(push, 1)
struct TGAHeader {
	uint8_t idLength;
	uint8_t colorMapType;
	uint8_t imageType;
	uint16_t colorMapStart;
	uint16_t colorMapLength;
	uint8_t colorMapDepth;
	uint16_t xOffset;
	uint16_t yOffset;
	uint16_t width;
	uint16_t height;
	uint8_t pixelDepth;
	uint8_t imageDescriptor;
};

#pragma pack(pop)

#endif // TGA_H