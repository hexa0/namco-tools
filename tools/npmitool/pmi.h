#ifndef PMI_H
#define PMI_H

/// @brief referenced from GS_Users_Manual.pdf at https://github.com/DarrenRainey/PS2-Programming-Docs
/// PMW2 uses GS_PSM_CT16, GS_PSM_CT32, GS_PSM_T4, GS_PSM_T8, everything else goes unused
typedef enum GS_PSM : unsigned char {
	GS_PSM_CT32   = 0x00, // RGBA 32-bit
	GS_PSM_CT24   = 0x01, // RGB 24-bit
	GS_PSM_CT16   = 0x02, // RGBA 16-bit (5551)
	GS_PSM_CT16S  = 0x0A, // 16-bit Signed
	GS_PSM_T8     = 0x13, // 8-bit Indexed (Your star.pmi)
	GS_PSM_T4     = 0x14, // 4-bit Indexed
	GS_PSM_T8H    = 0x1B, // 8-bit Indexed (High)
	GS_PSM_T4HL   = 0x24, // 4-bit Indexed (High-Low)
	GS_PSM_T4HH   = 0x2C, // 4-bit Indexed (High-High)
	GS_PSM_Z32    = 0x30, // 32-bit Depth
	GS_PSM_Z24    = 0x31, // 24-bit Depth
	GS_PSM_Z16    = 0x32, // 16-bit Depth
	GS_PSM_Z16S   = 0x3A  // 16-bit Signed Depth
} GS_PSM;

/// @brief more like struct PMO am i right?
/// this is the header for .pmi textures
typedef struct PMI {
	/// @brief this should always be 'PMI\0'
	unsigned char magic[4];
	/// @brief in star.pmi this is 24
	unsigned int header;
	/// @brief this is just a float (0.2, 0.33, etc) however if 0.33 is incorrectly interpreted as uint32 it is 1051260355 which as a unix epoch is Friday, April 25, 2003 at 8:45:55 AM, do not follow in my footsteps and let this confuse you as it is absolutely just by pure coincidence and WILL waste your time
	float version;
	/// @brief image width in pixels
	short unsigned int width;
	/// @brief image height in pixels
	short unsigned int height;
	/// @brief image bitdepth
	unsigned char depth;
	/// @brief 0x00 = Opaque, 0x01 = Transparent
	unsigned char trans;
	/// @brief log2 width
	unsigned char tw;
	/// @brief log2 height
	unsigned char th;
	/// @brief clut (color look up table) color depth
	short unsigned int clutDepth;
	/// @brief clut (color look up table) length in bytes
	short unsigned int clutLen;
	/// @brief scanline width
	short unsigned int rowLen;
	/// @brief misc flags
	unsigned char flags;
	/// @brief pixel mode
	GS_PSM pmode;
	/// @brief texture data pointer (set at runtime)
	short unsigned int texBP;
	/// @brief clut data pointer (set at runtime)
	short unsigned int clutBP;
} PMI;

#endif // PMI_H