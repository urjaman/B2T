/* B2RP - Nokia "0xB2" firmware format Re-Packer (undoes B2X :P) */

#define _SVID_SOURCE
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>

#include "B2.h"	

static void die(const char* why)
{
	perror(why);
	exit(1);
}

static void mdie(const char *why)
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

static uint32_t rbe32(const void*d_) {
	const uint8_t *d = d_;
	return ((uint32_t)d[0] << 24)|((uint32_t)d[1] << 16)|(d[2] << 8)|d[3];
}

static void wbe32(void* a_, uint32_t d)
{
	uint8_t *a = a_;
	a[0] = (d >> 24) &0xFF;
	a[1] = (d >> 16) &0xFF;
	a[2] = (d >>  8) &0xFF;
	a[3] = (d      ) &0xFF;
}

static int header_filter(const struct dirent *d)
{
	if (d->d_name[0] == '.') return 0;
	unsigned int a,b;
	if (sscanf(d->d_name,"%3u-%2x", &a,&b)==2) return 1;
	return 0;
}

long file_size(FILE *s)
{
	long sz;
	if (fseek(s, 0, SEEK_END)) die("fseek");
	sz = ftell(s);
	if (sz < 0) die("ftell");
	if (fseek(s, 0, SEEK_SET)) die("fseek");
	return sz;
}

void recreate_header (FILE *output, const char* basedir)
{
	struct b2_hdr_s out_hdr = { 0xB2, 0, 0 };
	char dn[256];
    struct dirent **namelist;
    int count;
	strcpy(dn, basedir);
	strcat(dn, "/headers");

	count = scandir(dn, &namelist, header_filter, alphasort);
	if (count < 0) die("scandir");
	char hbuf[4096];
	unsigned int hbo=0;
	wbe32(&out_hdr.blkc_be, count);
	
	for (int n=0;n<count;n++) {
		unsigned int t;
		if (sscanf(namelist[n]->d_name, "%*3u-%2x",&t)!=1) die("bad fn");
		char fulln[256];
		strcpy(fulln, dn);
		strcat(fulln, "/");
		strcat(fulln, namelist[n]->d_name);
		free(namelist[n]);
		FILE *hd = fopen(fulln, "rb");
		if (!hd) die("fopen");
		long sz = file_size(hd);
		if (sz > 255) mdie("too big header file");
		if ((hbo+sz+3) > 4095) mdie("insufficient header buffer");
		struct block_s *hb = (struct block_s*)(hbuf+hbo);
		hb->type = t;
		hb->len = sz;
		if (sz) if (fread(hb->d, sz, 1, hd)!=1) die("fread");
		hbo += (sizeof(struct block_s) + sz);
    }
	free(namelist);
	const char exnm[] = "extra-header-data";
	char exfulln[256];
	strcpy(exfulln, basedir);
	strcat(exfulln, "/");
	strcat(exfulln, exnm);
	FILE *ed = fopen(exfulln, "rb");
	if (ed) {
		long sz = file_size(ed);
		if ((hbo+sz) > 4095) mdie("insufficient header buffer");
		printf("Reading extra data sz=%ld\n", sz);
		if (fread(hbuf+hbo, sz, 1, ed)!=1) die("fread");
		hbo += sz;
	}
	wbe32(&out_hdr.fwhl_be, hbo+4); /* include the count of blocks */
	if (fwrite(&out_hdr, sizeof(struct b2_hdr_s), 1, output)!=1) die("fwrite");
	if (fwrite(hbuf, hbo, 1, output)!=1) die("fwrite");
}

static int header5x_filter(const struct dirent *d)
{
	if (d->d_name[0] == '.') return 0;
	unsigned int a,b;
	if (sscanf(d->d_name,"B%4u-T%1u", &a,&b)==2) return 1;
	return 0;
}

size_t header_meta(uint8_t *hdr, uint16_t **chk)
{
	size_t hdrl;
	if (hdr[0]==0x54) {
		struct imghdr_s *ih = (struct imghdr_s*)hdr;
		hdrl = sizeof(struct imghdr_s);
		if (chk) *chk = &ih->chksum_be;
	} else if (hdr[0]==0x5D) {
		struct imghdr_name_s *ih = (struct imghdr_name_s*)hdr;
		hdrl = sizeof(struct imghdr_name_s);
		if (ih->unkt==0x28) {
			hdrl += 22; // magic ;P
		}
		if (chk) *chk = &ih->chksum_be;
	} else {
		mdie("Unknown header type");
	}
	return hdrl;
}

int fixup_header(uint8_t *hdr, const void* data, size_t dlen)
{
	int r=0;
	uint16_t* chk;
	size_t hdrl = header_meta(hdr, &chk);
	uint16_t xp = xorpair(data, dlen);
	if (*chk != xp) {
		*chk = xp;
		r++;
	}
	uint8_t sm = 0xFF - sum8(hdr+1,hdrl-2);
	if (hdr[hdrl-1] != sm) {
		hdr[hdrl-1] = sm;
		r++;
	}
	return r;
}

int main(int argc, char**argv) {
	if (argc != 3) mdie("usage: b2rp indir outfile");
	FILE *output = fopen(argv[2], "wb");
	if (!output) die("fopen");
	printf("Re-Creating the header\n");
	recreate_header(output, argv[1]);
	printf("Processing image headers\n");
	struct dirent **namelist;
    int count;
	count = scandir(argv[1], &namelist, header5x_filter, alphasort);
	if (count < 0) die("scandir");
	for (int n=0;n<count;n++) {
		unsigned int hc;
		unsigned int t;
		printf("Procesing %d: '%s'\n", n, namelist[n]->d_name);
		if (sscanf(namelist[n]->d_name, "B%4u-T%1u",&hc,&t)!=2) die("bad fn");
		char fulln[256];
		strcpy(fulln, argv[1]);
		strcat(fulln, "/");
		strcat(fulln, namelist[n]->d_name);
		free(namelist[n]);
		FILE *hd = fopen(fulln, "rb");
		if (!hd) die("fopen");
		long sz = file_size(hd);
		uint8_t *buf = malloc(sz);
		if (!buf) die("malloc");
		if (fread(buf,sz,1,hd)!=1) die("fread");
		fclose(hd);
		if (t!=1) {
			size_t hl = header_meta(buf,0);
			uint8_t *data = buf + hl;
			if (fixup_header(buf, data, sz-hl)) {
				printf("Fixed up header for t=%d\n", t);
			}
			if (fwrite(buf, sz, 1, output)!=1) die("fwrite");
		} else {
			struct imghdr_s *ih = (struct imghdr_s*)buf;
			size_t chunksz = rbe32(&ih->len_be);
			size_t addr = rbe32(&ih->addr_be);
			snprintf(fulln,255,"%s/D%04d-T%d-%08X",argv[1],hc,t,addr);
			FILE *df = fopen(fulln, "rb");
			if (!df) die("fread");
			long datasz = file_size(df);
			uint8_t *db = malloc(chunksz);
			if (!db) die("malloc chunksz");
			do {
				if (datasz < (int)chunksz) {
					chunksz = datasz;
					wbe32(&ih->len_be, chunksz);
				}
				if (fread(db, chunksz, 1, df)!=1) die("fread");
				fixup_header(buf, db, chunksz);
				if (fwrite(buf, sz, 1, output)!=1) die("fwrite");
				if (fwrite(db, chunksz, 1, output)!=1) die("fwrite");
				addr += chunksz;
				wbe32(&ih->addr_be, addr);
				datasz -= chunksz;
			} while (datasz>0);
			fclose(df);
			free(db);
		}
		free(buf);
	}
	fclose(output);
	printf("Done\n");
	return 0;
}