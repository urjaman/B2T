
/* File format "spec" part... */

#define PACKED __attribute__((packed))

struct b2_hdr_s {
	/* File Header */
	uint8_t sig; // 0xB2
	uint32_t fwhl_be;
	/* FW header */
	uint32_t blkc_be;
	/* blkc_be block_s follow */
} PACKED;

struct block_s {
	uint8_t type;
	uint8_t len;
	uint8_t d[];
} PACKED;

struct imghdr_s {
	uint8_t sig; // 0x54
	uint8_t ssb1; // 0xFFFF:"number of subsection blocks+1", always 1
	uint8_t unk1_1; // 0x17
	uint8_t unk1_2; // 0x0x0E
	uint8_t unk2m[2]; // 0x00 0x00 or 0x01 0x01 (in the ROFx)
	uint8_t unk3; // 0x00
	uint16_t chksum_be; // 0xFFFF says big endian xorpair "for the image contents"
	uint8_t unk4; // 0x01
	uint32_t len_be;
	uint32_t addr_be; //  0x01050000 for a ROFx, happens to be the target address in flash for it; 0x00000400 for core img 1st.
	uint8_t sum; // sum bytes after sig results in 0xFF
} PACKED; 

struct imghdr_name_s { 
	uint8_t sig; // 0x5D
	uint8_t ssb1; // 0xFFFF:"number of subsection blocks+1", always 1
	uint8_t unkt; // 0x27 or 0x28 (special, not like this header past addr, and 22 bytes longer)
	uint8_t unk[21]; // example: 2D E9 EF F4 BF AA 53 93 21 7C A6 B1 77 55 FC 3E 14 15 65 8E 9A
	uint8_t name[12]; 
	uint8_t unk2m[2]; // 0x00,0x00 or 0x01,0x01
	uint8_t unk3; // 0x00
	uint16_t chksum_be; // 0xFFFF says big endian xorpair "for the image contents"
	uint32_t len_be;
	uint32_t addr_be;
	uint8_t sum; // sum bytes after sig results in 0xFF; not in this position for the 0x28 unkt header.
} PACKED;