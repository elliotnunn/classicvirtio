/* Copyright (c) 2023 Elliot Nunn */
/* Licensed under the MIT license */

/*
Implementation of the 3-file multifork format
(best for development)

FILE       = data fork
FILE.rdump = resource fork in Rez format
FILE.idump = first 8 bytes of Finder info (i.e. type/creator)

Directory metadata is discarded

Uses the hashtab (mea culpa)
*/

#include <string.h>
#include <OSUtils.h>

#include "9buf.h"
#include "9p.h"
#include "derez.h"
#include "hashtab.h"
#include "panic.h"
#include "printf.h"
#include "rez.h"

#include "multifork.h"

#include <stdint.h>
#include <stdlib.h>

// Fids 8-15 are reserved for the multifork layer
enum {
	ROOTFID = 0,
	FIDBIG = 8,
	FID1 = 9,
	MAXRES = 5459, // max resources per file
	MAXFORK = 0x1100000,
};

// Per-open-file information hidden inside FCB, maximum 12 bytes
struct openfile {
	bool resfork;
	uint32_t fid;
	int32_t cnid;
};

struct res {
	uint32_t type;
	int16_t id;
	uint16_t nameoff;
	uint32_t attrandoff;
};

int OpenSidecar(uint32_t fid, int32_t cnid, int flags, const char *fmt); // borrowed from device-9p.c

static struct cacherec *cacheResourceFork(int32_t cnid, uint32_t fid, const char *name);
static uint32_t readin(uint32_t rezfid, uint32_t scratchfid, uint64_t offset);
static void writeout(uint32_t scratchfid, uint64_t offset, uint32_t rezfid);
static int resorder(const void *a, const void *b);
static int tempfile(void);
// no need to prototype init3, open3 etc... they are used once at bottom of file

uint64_t tempfilelen;

static int init3(void) {
	int err;

	err = tempfile();
	if (err) return err;

	return 0;
}

struct cacherec {
	uint64_t sec, nsec, offset;
	uint32_t size;
	uint16_t opencount;
	bool dirty;
};

static int open3(void *opaque, short refnum, int32_t cnid, uint32_t fid, const char *name, bool resfork, bool write) {
	struct openfile *s = opaque;
	int err = 0;

	s->resfork = resfork;
	s->fid = 32 + refnum;
	s->cnid = cnid;

	if (!resfork) {
		// Data fork is relatively simple: the file can only be opened if it exists
		Walk9(fid, s->fid, 0, NULL, NULL, NULL);

		if (write) {
			err = Lopen9(s->fid, O_RDWR, NULL, NULL);
			if (err == 0) return 0;
		}

		err = Lopen9(s->fid, O_RDONLY, NULL, NULL);
		return err;
	} else {
		cacheResourceFork(cnid, fid, name);
		return noErr;
	}
}

static int close3(void *opaque) {
	struct openfile *s = opaque;

	if (!s->resfork) {
		return Clunk9(s->fid);
	} else {
		struct cacherec *c = HTlookup('R', &s->cnid, sizeof s->cnid);
		c->opencount--;
		if (c->opencount>0 || !c->dirty) return 0;

		if (c->size == 0) { // sidecar file should be absent
			int err = OpenSidecar(FID1, s->cnid, O_WRONLY, "%s.rdump");
			if (!err) Remove9(FID1);

			c->sec = 0;
			c->nsec = 1;
		} else { // sidecar file should be present, even if empty
			int err = OpenSidecar(FID1, s->cnid, O_WRONLY|O_CREAT|O_TRUNC, "%s.rdump");
			if (err) panic("sidecar failure");

			writeout(FIDBIG, c->offset, FID1);

			struct Stat9 stat;
			if (Getattr9(FID1, STAT_MTIME, &stat)) panic("stat failed should be ok");
			printf("stat sec %x\n", (int)stat.mtime_sec);
			c->sec = stat.mtime_sec;
			c->nsec = stat.mtime_nsec;

			Clunk9(FID1);
		}
		return 0;
	}
}

static int read3(void *opaque, void *buf, uint64_t offset, uint32_t count, uint32_t *actual_count) {
	struct openfile *s = opaque;

	if (!s->resfork) {
		return Read9(s->fid, buf, offset, count, actual_count);
	} else {
		struct cacherec *c = HTlookup('R', &s->cnid, sizeof s->cnid);

		// Prevent reads beyond bounds
		if (offset > c->size) {
			if (actual_count) *actual_count = 0;
			return 0;
		} else if (offset + count > c->size) {
			count = c->size - offset;
		}

		return Read9(FIDBIG, buf, c->offset+offset, count, actual_count);
	}
}

static int write3(void *opaque, const void *buf, uint64_t offset, uint32_t count, uint32_t *actual_count) {
	struct openfile *s = opaque;

	if (!s->resfork) {
		return Write9(s->fid, buf, offset, count, actual_count);
	} else {
		struct cacherec *c = HTlookup('R', &s->cnid, sizeof s->cnid);

		// Prevent reads beyond bounds
		if (offset + count > MAXFORK) {
			if (actual_count) *actual_count = 0;
			return EFBIG;
		}

		c->dirty = true;

		return Write9(FIDBIG, buf, c->offset+offset, count, actual_count);
	}
}

static int geteof3(void *opaque, uint64_t *len) {
	struct openfile *s = opaque;

	if (!s->resfork) {
		struct Stat9 stat = {};
		int err = Getattr9(s->fid, STAT_SIZE, &stat);
		if (err) return err;
		*len = stat.size;
	} else {
		struct cacherec *c = HTlookup('R', &s->cnid, sizeof s->cnid);
		*len = c->size;
	}
	return 0;
}

static int seteof3(void *opaque, uint64_t len) {
	struct openfile *s = opaque;

	if (!s->resfork) {
		return Setattr9(s->fid, SET_SIZE, (struct Stat9){.size=len});
	} else {
		if (len > MAXFORK) return EFBIG;
		struct cacherec *c = HTlookup('R', &s->cnid, sizeof s->cnid);
		if (len != c->size) c->dirty = true;
		c->size = len;
	}

	return 0;
}

int fgetattr3(int32_t cnid, uint32_t fid, const char *name, unsigned fields, struct MFAttr *attr) {
	// To be really clear, all these fields are zero until proven otherwise
	memset(attr, 0, sizeof *attr);

	// Costly: stat the data fork
	// The data fork is essential, so this is the only operation that can make the function fail
	if ((fields & MF_DSIZE) || (fields & MF_TIME)) {
		struct Stat9 dstat = {};
		int err = Getattr9(fid,
			((fields & MF_DSIZE) ? STAT_SIZE : 0) |
			((fields & MF_TIME) ? STAT_MTIME : 0),
			&dstat);
		if (err) return err;

		attr->dsize = dstat.size;
		attr->unixtime = dstat.mtime_sec;
	}

	// Very costly: ensure the resource fork has been Rezzed into the cache
	if ((fields & MF_RSIZE) || (fields & MF_TIME)) {
		struct cacherec *c = cacheResourceFork(cnid, fid, name);
		attr->rsize = c->size;
		if (attr->unixtime < c->sec) attr->unixtime = c->sec;
	}

	// Costly: read the Finder info
	// Unanswered question: what is the difference between four-nulls and four-question-marks?
	if (fields & MF_FINFO) {
		char iname[1024];
		strcpy(iname, name);
		strcat(iname, ".idump");

		if (!Walk9(fid, FID1, 2, (const char *[]){"..", iname}, NULL, NULL)
				&&
				!Lopen9(FID1, O_RDONLY, NULL, NULL)) {
			Read9(FID1, attr->finfo, 0, 8, NULL);
			Clunk9(FID1);
		}
	}

	return 0;
}

int fsetattr3(int32_t cnid, uint32_t fid, const char *name, unsigned fields, const struct MFAttr *attr) {
	// (This function never needs to truncate/extend the file)

	// For now, let us not implement time-setting
	// (It might be possible to restrict it to the cases of Now and The Epoch, signalling a corrupt file in MPW)
	if (fields & MF_TIME) {
	}

	if (fields & MF_FINFO) {
		int err = Walk9(fid, FID1, 1, (const char *[]){".."}, NULL, NULL);
		if (err) panic("dot-dot should never fail");

		char iname[1024];
		strcpy(iname, name);
		strcat(iname, ".idump");
		err = Lcreate9(FID1, O_WRONLY|O_TRUNC|O_CREAT, 0666, 0, iname, NULL, NULL);
		if (err) return err;

		err = Write9(FID1, attr->finfo, 0, 8, NULL);
		if (err) return err;

		Clunk9(FID1);
	}

	return 0;
}

int dgetattr3(int32_t cnid, uint32_t fid, const char *name, unsigned fields, struct MFAttr *attr) {
	// Benignly unimplemented
	memset(attr, 0, sizeof *attr);
	return 0;
}

int dsetattr3(int32_t cnid, uint32_t fid, const char *name, unsigned fields, const struct MFAttr *attr) {
	// Benignly unimplemented
	return 0;
}

static int move3(uint32_t fid1, const char *name1, uint32_t fid2, const char *name2) {
	int err = Renameat9(fid1, name1, fid2, name2);
	if (err) return err;

	int worsterr = 0;

	const char *sidecars[] = {"%s.rdump", "%s.idump"};
	for (int i=0; i<sizeof sidecars/sizeof *sidecars; i++) {
		char sidename1[1024], sidename2[1024];
		sprintf(sidename1, sidecars[i], name1);
		sprintf(sidename2, sidecars[i], name2);
		err = Renameat9(fid1, sidename1, fid2, sidename2);
		if (err && err != ENOENT) worsterr = err;
	}

	return worsterr;
}

static int del3(uint32_t fid, const char *name, bool isdir) {
	Walk9(fid, FID1, 1, (const char *[]){".."}, NULL, NULL);

	if (isdir) {
		return Unlinkat9(FID1, name, 0x200 /*AT_REMOVEDIR*/);
	} else {
		// The main file must be deleted,
		// then delete the sidecars on a best-effort basis
		const char *sidecars[] = {"%s", "%s.rdump", "%s.idump"};
		for (int i=0; i<sizeof sidecars/sizeof *sidecars; i++) {
			char delname[1024];
			sprintf(delname, sidecars[i], name);
			int err = Unlinkat9(FID1, delname, 0);
			if (err && i==0) return err;
		}
	}

	return 0;
}

static bool issidecar3(const char *name) {
	int len = strlen(name);
	if (len >= 6 && !strcmp(name+len-6, ".rdump")) return true;
	if (len >= 6 && !strcmp(name+len-6, ".idump")) return true;

	return false;
}

struct MFImpl MF3 = {
	.Name = ".idump/.rdump",
	.Init = &init3,
	.Open = &open3,
	.Close = &close3,
	.Read = &read3,
	.Write = &write3,
	.GetEOF = &geteof3,
	.SetEOF = &seteof3,
	.FGetAttr = &fgetattr3,
	.FSetAttr = &fsetattr3,
	.DGetAttr = &dgetattr3,
	.DSetAttr = &dsetattr3,
	.Move = &move3,
	.Del = &del3,
	.IsSidecar = &issidecar3,
};

static struct cacherec *cacheResourceFork(int32_t cnid, uint32_t fid, const char *name) {
	// Ensure the file has a cache entry (by CNID), and get a pointer
	struct cacherec *c = HTlookup('R', &cnid, sizeof cnid);
	if (!c) {
		struct cacherec new = {.offset = tempfilelen};
		tempfilelen += MAXFORK;
		HTinstall('R', &cnid, sizeof cnid, &new, sizeof new);
		c = HTlookup('R', &cnid, sizeof cnid);
	}

	// Navigate to the sidecar file (.rdump)
	char rname[1024];
	strcpy(rname, name);
	strcat(rname, ".rdump");
	int sidecarerr = Walk9(fid, FID1, 2, (const char *[]){"..", rname}, NULL, NULL);

	// Get the sidecar file's age (only if it exists and isn't already open)
	struct Stat9 stat = {.mtime_nsec=1}; // fake time matches nothing
	if (c->opencount==0 && sidecarerr==0) {
		Getattr9(FID1, STAT_MTIME, &stat);
	}

	// Do we need a costly parse of the sidecar file?
	if (sidecarerr) {
		// Empty resource fork
		c->size = 0;
	} else if (c->opencount==0 && (c->sec!=stat.mtime_sec || c->nsec!=stat.mtime_nsec)) {
		// Stale cached resource fork needs rereading
		if (Lopen9(FID1, O_RDONLY, NULL, NULL)) panic("could not open rdump");
		c->size = readin(FID1, FIDBIG, c->offset);
		Clunk9(FID1);
	} else {
		// Valid cached resource fork
	}

	c->opencount++;
	return c;
}

static uint32_t readin(uint32_t rezfid, uint32_t scratchfid, uint64_t offset) {
	int err;
	int nres = 0;
	struct res resources[MAXRES];
	char namelist[0x10000];

	// sizes for pointer calculations
	size_t contentsize=0, namesize=0;

	// Hefty stack IO buffer, rededicate to reading after writes done
	enum {WB = 8*1024, RB = 32*1024};
	char buf[WB+RB];
	SetWrite(scratchfid, buf, WB);
	SetRead(rezfid, buf+WB, RB);
	wbufat = wbufseek = offset + 256;

	// Slurp the Rez file sequentially, acquiring:
	// type/id/attrib/bodyoffset/nameoffset into resources array
	// name into namelist
	// actual data into scratch file
	for (;;) {
		struct res r;
		uint8_t attrib;
		bool hasname;
		unsigned char name[256];

		if (RezHeader(&attrib, &r.type, &r.id, &hasname, name)) break;

		int32_t rezoffset = rbufseek;

		int32_t bodylen;
		int32_t fixup = wbufseek;
		for (int i=0; i<4; i++) Write(0);
		bodylen = RezBody();
		err = Overwrite(&bodylen, fixup, 4);
		if (err) return 0;
		if (bodylen < 0) panic("failed to read Rez body");

		if (nres >= sizeof resources/sizeof *resources) {
			panic("too many resources in file");
		}

		// append the name to the packed name list
		if (hasname) {
			if (namesize + 1 + name[0] > 0x10000)
				panic("filled name buffer");
			r.nameoff = namesize;
			memcpy(namelist + namesize, name, 1 + name[0]);
			namesize += 1 + name[0];
		} else {
			r.nameoff = 0xffff;
		}

		r.attrandoff = contentsize | ((uint32_t)attrib << 24);

		resources[nres++] = r;
		contentsize += 4 + bodylen;
	}

	// We will no longer use the read buffer, rededicate it to write buffer
	wbufsize = WB + RB;

	// Group resources of the same type together and count unique types
	qsort(resources, nres, sizeof *resources, resorder);
	int ntype = 0;
	for (int i=0; i<nres; i++) {
		if (i==0 || resources[i-1].type!=resources[i].type) ntype++;
	}

	// Resource map header
	for (int i=0; i<25; i++) Write(0);
	Write(28); // offset to type list
	Write((28 + 2 + 8*ntype + 12*nres) >> 8); // offset to name
	Write((28 + 2 + 8*ntype + 12*nres) >> 0);
	Write((ntype - 1) >> 8); // resource types in the map minus 1
	Write((ntype - 1) >> 0);

	// Resource type list
	int base = 2 + 8*ntype;
	int ott = 0;
	for (int i=0; i<nres; i++) {
		if (i==nres-1 || resources[i].type!=resources[i+1].type) {
			// last resource of this type
			Write(resources[i].type >> 24);
			Write(resources[i].type >> 16);
			Write(resources[i].type >> 8);
			Write(resources[i].type >> 0);
			Write(ott >> 8);
			Write(ott >> 0);
			Write(base >> 8);
			Write(base >> 0);
			base += 12 * (ott + 1);
			ott = 0;
		} else {
			ott++;
		}
	}

	// Resource reference list
	for (int i=0; i<nres; i++) {
		Write(resources[i].id >> 8);
		Write(resources[i].id >> 0);
		Write(resources[i].nameoff >> 8);
		Write(resources[i].nameoff >> 0);
		Write(resources[i].attrandoff >> 24);
		Write(resources[i].attrandoff >> 16);
		Write(resources[i].attrandoff >> 8);
		Write(resources[i].attrandoff >> 0);
		for (int i=0; i<4; i++) Write(0);
	}

	for (int i=0; i<namesize; i++) {
		Write(namelist[i]);
	}

	Flush();

	uint32_t head[4] = {
		256,
		256+contentsize,
		contentsize,
		28+2+8*ntype+12*nres+namesize
	};

	Write9(scratchfid, head, offset, sizeof head, NULL);

	return 256+contentsize+28+2+8*ntype+12*nres+namesize;
}

#define READ16BE(S) ((255 & (S)[0]) << 8 | (255 & (S)[1]))
#define READ24BE(S) \
  ((uint32_t)(255 & (S)[0]) << 16 | \
   (uint32_t)(255 & (S)[1]) << 8 | \
   (uint32_t)(255 & (S)[2]))

static void writeout(uint32_t scratchfid, uint64_t offset, uint32_t rezfid) {
	char rbuf[8*1024], wbuf[32*1024];

	uint32_t head[4];
	Read9(scratchfid, head, offset, sizeof head, NULL);
	char map[64*1024];
	Read9(scratchfid, map, offset + head[1], head[3], NULL);

	printf("map base %ld len %ld\n", head[1], head[3]);
	for (long i=0; i<head[3]; i++) printf("%02x ", 255&map[i]);
	printf("\n");

	char *tl = map + READ16BE(map+24);
	char *nl = map + READ16BE(map+26);

	printf("namelist starts with %.*s\n", nl[0], nl+1);

	int nt = (uint16_t)(READ16BE(tl) + 1);
	printf("nt=%d\n", nt);

	SetRead(scratchfid, rbuf, sizeof rbuf);
	SetWrite(rezfid, wbuf, sizeof wbuf);

	for (int i=0; i<nt; i++) {
		char *t = tl + 2 + 8*i;
		int nr = READ16BE(t+4) + 1;
		int r1 = READ16BE(t+6);
		for (int j=0; j<nr; j++) {
			char *r = tl + r1 + 12*j;

			int16_t id = READ16BE(r);
			uint16_t nameoff = READ16BE(r+2);
			printf("nameoff %d\n", nameoff);
			uint8_t *name = (nameoff==0xffff) ? NULL : (nl+nameoff);
			uint8_t attr = *(r+4);
			uint32_t contoff = READ24BE(r+5);

			DerezHeader(attr, t, id, name);

			printf("relevant are %ld %ld %ld\n", (long)offset, (long)head[0], (long)contoff);
			rbufseek = offset + head[0] + contoff;
			uint32_t len = 0;
			for (int i=0; i<4; i++) {
				len = (len << 8) | (uint8_t)Read();
			}
			DerezBody(len);
			Write('}');
			Write(';');
			Write('\n');
			Write('\n');
		}
	}

	Flush();
}

static int resorder(const void *a, const void *b) {
	const struct res *aa = a, *bb = b;

	if (aa->type > bb->type) {
		return 1;
	} else if (aa->type == bb->type) {
		if (aa->id > bb->id) {
			return 1;
		} else {
			return -1;
		}
	} else {
		return -1;
	}
}

// Attempt to create a single large unlinked tempfile
// (Use exponential backoff: VM lacks entropy to randomize the filename)
static int tempfile(void) {
	const char name[] = ".9temp";
	int err;

	Walk9(ROOTFID, FIDBIG, 0, NULL, NULL, NULL);

	uint32_t sleep = 1;
	for (;;) {
		err = Lcreate9(FIDBIG, O_CREAT|O_EXCL|O_RDWR, 0600, 0, name, NULL, NULL);
		if (err == 0) break;
		else if (err != EEXIST) return err;

		uint32_t t0 = TickCount();
		while (TickCount()-sleep < 2) {};
		sleep *= 2;
	}

	err = Unlinkat9(ROOTFID, name, 0);
	if (err) panic("Failed to unlink our temp resource file");

	return 0;
}
