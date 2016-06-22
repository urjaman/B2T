#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>

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
};

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
	
	

/* Codey part ... */
const int wm=1; /* Whether we're writing stuff or just parsing ... */

static uint32_t rbe32(const void*d_) {
	const uint8_t *d = d_;
	return ((uint32_t)d[0] << 24)|((uint32_t)d[1] << 16)|(d[2] << 8)|d[3];
}

static uint16_t rbe16(const void*d_) {
	const uint8_t *d = d_;
	return (d[0] << 8)|d[1];
}


void die(const char* why)
{
	perror(why);
	exit(1);
}

void mdie(const char *why)
{
	fprintf(stderr,"die: %s\n",why);
	exit(2);
}

uint16_t xorpair(const void *d, size_t l) {
	uint16_t r=0;
	const uint16_t* b = d;
	for ( l>>=1;l--;b=b+1)
		r^=*b;
	return r;
}

uint8_t sum8(const void *d_, size_t l) {
	const uint8_t* d = d_;
	uint8_t r=0;
	for (size_t n=0;n<l;n++) {
		r += d[n];
	}
	return r;
}


void* open_map_file(const char* filename, int *fd, off_t *size)
{
	/* Open and somehow map the file to be visible in 
	   arcfile_data. If porting to a place where mmap() doesnt work,
	   malloc + copy is fine.
	*/
	int arcfile_fd = open(filename, O_RDONLY);
	if (arcfile_fd < 0) die("open");	
	if (fd) *fd = arcfile_fd;
	
	off_t filesize = lseek(arcfile_fd, 0, SEEK_END);
	if (filesize == (off_t)-1) die("lseek");
	if (size) *size = filesize;
	
#if 1
	void * arcfile_data = mmap(0, filesize, PROT_READ, MAP_SHARED, 
				arcfile_fd, 0);
	if (arcfile_data == MAP_FAILED) die("mmap");
#else
	void* arcfile_data = malloc(filesize);
	if (!arcfile_data) die("malloc");
	if (lseek(arcfile_fd, 0, SEEK_SET) != 0) die("lseek");
	off_t read_offset = 0;
	do {
		ssize_t rv = read(arcfile_fd, arcfile_data+read_offset, 
					filesize - read_offset);
		if (rv<=0) die("read");
		read_offset += rv;
	} while (read_offset < filesize);
#endif
	return arcfile_data;
}

void splurt_buf(const char*fn, const void*d, size_t l) {
	if (!wm) return;
	FILE *f = fopen(fn, "wb");
	if (!f) die("open_header_file");
	if (fwrite(d, l, 1, f)!=1) die("fwrite");
	fclose(f);
}

void hd_buf(const void*d_, size_t l) {
	const uint8_t *d = d_;
	if (!l) {
		printf("\n");
		return;
	}
	if ((l>=2)&&(d[l-1] == 0)) { // test for a string
		int string = 1;
		for (size_t n=0;n<(l-1);n++) {
			if ((d[n]=='\n')||(d[n]=='\r')||((d[n]>=32)&&(d[n]<127))) {
				continue;
			}
			string = 0;
			break;
		}
		if (string) {
			printf("'%s'\n", (char*)d_);
			return;
		}
	}
	int bm = l>32;
	size_t chksz = bm?16:l;
	if (bm) printf("\n");
	for (size_t n=0;n<l;n+=chksz) {
		for(size_t o=0;o<chksz;o++) {
			if ((o+n)>=l) {
				if(bm) printf("   ");
				continue;
			}
			printf("%02X ", d[n+o]);
		}
		printf("| ");
		for(size_t o=0;o<chksz;o++) {
			if ((o+n)>=l)
				break;
			uint8_t c = d[n+o];
			if ((c >=127)||(c<32)) c = '.';
			printf("%c", c);
		}
		printf("\n");
	}
}
		
static int hdr_cnt;
size_t splurt_header(struct block_s *h) {
	const char *dir = "headers";
	mkdir(dir, 0777);
	char fn[32];
	snprintf(fn,31,"%s/%03d-%02X", dir, hdr_cnt++, h->type);
	printf("Block type %02X len %d: ", h->type, h->len);
	hd_buf(h->d, h->len);
	splurt_buf(fn, h->d, h->len);
	return h->len + 2;
}

int main(int argc, char**argv) {
	if (argc != 2) mdie("usage: b2x filename");
	off_t filesz;
	const uint8_t * map = open_map_file(argv[1], 0, &filesz);
	struct b2_hdr_s *h1 = (struct b2_hdr_s*)map;
	if (h1->sig != 0xB2) mdie("invalid signature");
	uint32_t header_len = rbe32(&h1->fwhl_be);
	uint32_t block_count = rbe32(&h1->blkc_be);
	if (block_count > 256) mdie("inane block count");
	if (header_len > 256*256) mdie("inane header len");
	struct block_s *b = (struct block_s*)(map + sizeof(struct b2_hdr_s));
	printf("Header len: %lu\n", header_len);
	printf("Block count: %lu\n", block_count);
	for (uint32_t n=0;n<block_count;n++) {
		int n = splurt_header(b);
		b = (struct block_s*)(((uint8_t*)b) + n);
	}
	const uint8_t * img_p = map + 5 +  header_len;
	ssize_t diff = img_p - (uint8_t*)b;
	if (diff<0) {
		printf("header len inconsistent: %d\n", diff);
		return 3;
	}
	if (diff==0) {
		printf("header len consistent\n");
	} else {
		printf("header has extra data (l=%d):", diff);
		hd_buf(b, diff);
		splurt_buf("extra-header-data", b, diff);
	}
	const size_t hdrsz1 = sizeof(struct imghdr_s);
	const size_t hdrsz2 = sizeof(struct imghdr_name_s);
	printf("hdsz1:%02X, 2:%02X\n", hdrsz1, hdrsz2);
	//	printf("data after (off:%lu):", 5 + header_len);
//	hd_buf(img_p, hdrsz);

	int hdrcnt=0;
	int hdr54c=0;
	int hdr5Dc=0;
	off_t pos = 5+header_len;

	struct imghdr_s lih;
	uint32_t naddr = 0;
	FILE *ff= 0;

	while (pos < filesz) {
		uint8_t sig = map[pos];
		uint8_t sum;
		const void* hdp;
		size_t hdl;
		const uint16_t *dp;
		size_t l;
		uint16_t xorp;
		int type=0;
		if (sig==0x54) {
			sum = sum8(map+pos+1,hdrsz1-1);
			const struct imghdr_s *ih = (struct imghdr_s*)(map+pos);
			hdp = ih;
			hdl = hdrsz1;
			printf("hdr54 sum%02X off:%08llu:", sum, pos);
			hd_buf(ih, hdrsz1);
			pos += hdrsz1;
			dp = (const uint16_t*)(map + pos);
			l = rbe32(&ih->len_be);
			xorp = ih->chksum_be;
			pos += l;
			type = 1;
			hdr54c++;
		} else if (sig==0x5D) {
			size_t hsz = hdrsz2;
			const struct imghdr_name_s *ih = (struct imghdr_name_s*)(map+pos);
			if (ih->unkt==0x28) {
				hsz += 22; // magic ;P
				type = 3;
			} else {
				type = 2;
			}
			sum = sum8(map+pos+1,hsz-1);
			printf("hdr5D sum%02X off:%08llu:", sum, pos);
			hd_buf(ih, hsz);
			hdp = ih;
			hdl = hsz;
			pos += hsz;
			dp = (const uint16_t*)(map + pos);
			l = rbe32(&ih->len_be);
			xorp = ih->chksum_be;
			pos += l;
			hdr5Dc++;
		} else {
			printf("Unknown header off:%08llu:", pos);
			hd_buf(map+pos,16);
			break;
		}
		if (sum!=0xFF) {
			printf("sum error! off:%08llu\n", pos);
			if (wm) exit(4);
		}
		uint16_t rxorp = xorpair(dp, l);
		if (xorp != rxorp) {
			printf("data chksum error! %04X vs %04X\n", xorp, rxorp);
			if (wm) exit(4);
		}
		if (ff) {
			const struct imghdr_s *ih = (struct imghdr_s*)hdp;
			const uint32_t addr = rbe32(&ih->addr_be);
			struct imghdr_s sim = lih;
			sim.addr_be = ih->addr_be;
			sim.len_be = ih->len_be;
			sim.chksum_be = xorp;
			sim.sum = ih->sum;
			if ((type != 1)||(addr != naddr)||(memcmp(&sim, ih, hdl))) {
				fclose(ff);
				ff=0;
			}
		}
		if ((type)&&(wm)) {
			if (ff) {
				if (fwrite(dp,l,1,ff)!=1) die("flashw");
				size_t chksz = rbe32(&lih.len_be);
				if (l != chksz) {
					fclose(ff);
					ff=0;
				}
				naddr += l;
			} else {
				char fn[32];
				snprintf(fn,31,"B%04d-T%d",hdrcnt, type);
				size_t ws = hdl;
				if (type!=1) ws += l;
				splurt_buf(fn, hdp, ws);
				if (type==1) {
					const struct imghdr_s *ih = (struct imghdr_s*)hdp;
					const uint32_t addr = rbe32(&ih->addr_be);
					snprintf(fn,31,"D%04d-T%d-%08lX",hdrcnt, type, addr);
					ff = fopen(fn, "wb");
					if (!ff) die("dopen");
					if (fwrite(dp,l,1,ff)!=1) die("flashw2");
					naddr = addr + l;
					lih = *ih;
				}
			}
		}
		hdrcnt++;
		
	}	
	printf("Headers:%d (%d, %d)\n", hdrcnt, hdr54c, hdr5Dc);
	if (pos > filesz) {
		printf("Scan overlow by %llu\n", pos-filesz);
	}
	return 0;
}
