/* Copyright (c) 2023 Elliot Nunn */
/* Licensed under the MIT license */

/*
Implementation of the 1-file multifork format
(the actual forks and metadata exposed by Mac OS X)
*/

#include <Memory.h>

#include "9p.h"
#include "panic.h"
#include "printf.h"

#include "multifork.h"

#include <string.h>

// Fids 8-15 are reserved for the multifork layer
enum {
	FID1 = 8,
};

// Per-open-file information hidden inside FCB, maximum 12 bytes
struct openfile {
	short fid;
	bool resfork; // means doing serious shenanigans
};

struct enorm {
	short fid;
	bool dirty;
	uint64_t size;
	char buf[17*1024*1024];
};

struct enorm *enorm;

static int init1(void) {
	enorm = (void *)NewPtrSysClear(sizeof (struct enorm));
	if (enorm == NULL) return ENOMEM;
	return 0;
}

// cannot fail, can only panic
static void flushrf(void) {
	if (enorm->fid == 0 || !enorm->dirty) return;

	int err;

	err = Walk9(enorm->fid, FID1, 0, NULL, NULL, NULL);
	if (err) panic("mf1, saved RF fid bad");

	err = Xattrcreate9(FID1, "com.apple.ResourceFork", enorm->size, 0 /*don't mind create/replace*/);
	if (err) panic("mf1, Xattrcreate");

	err = Write9(FID1, enorm->buf, 0, enorm->size, NULL);
	if (err) panic("mf1, xattr write");

	enorm->dirty = false;

	Clunk9(FID1);
}

static int slurprf(uint32_t fid) {
	// already sitting in the slurped buffer
	if (enorm->fid == fid) return 0;

	// flush out the existing file
	flushrf();

	enorm->fid = fid;
	enorm->dirty = false;
	enorm->size = 0;

	uint64_t size = 0;
	int err = Xattrwalk9(fid, FID1, "com.apple.ResourceFork", &enorm->size);
	if (err && err != ENODATA) return err;
	if (enorm->size > sizeof enorm->buf) {
		Clunk9(FID1);
		return E2BIG; // upper layer should panic
	}

	uint32_t succeededbytes = 0;
	Read9(FID1, enorm->buf, 0, enorm->size, &succeededbytes);
	printf("read %x\n", succeededbytes);
	enorm->size = succeededbytes;
	Clunk9(FID1);

	return 0;
}

static int open1(void *opaque, short refnum, uint32_t fid, const char *name, bool resfork, bool write) {
	// The upper layer has promised us that this exists as a regular file
	struct openfile *mystruct = opaque;
	int err = 0;

	uint32_t newfid = mystruct->fid = 32 + refnum;
	mystruct->resfork = resfork;

	if (!resfork) {
		// Data fork is easy case, access directly
		Walk9(fid, newfid, 0, NULL, NULL, NULL);
		if (write) {
			err = Lopen9(newfid, O_RDWR, NULL, NULL);
			if (err == 0) return 0;
		}

		err = Lopen9(newfid, O_RDONLY, NULL, NULL);
		return err;
	} else {
		Walk9(fid, newfid, 0, NULL, NULL, NULL);
		// Defer reading the resource fork until the actual read call
		// Resource fork is trickier
		return 0;
	}

}

static int close1(void *opaque) {
	struct openfile *mystruct = opaque;
	Clunk9(mystruct->fid);
	return 0;
}

static int read1(void *opaque, void *buf, uint64_t offset, uint32_t count, uint32_t *actual_count) {
	struct openfile *mystruct = opaque;

	// the easy path
	if (!mystruct->resfork) {
		return Read9(mystruct->fid, buf, offset, count, actual_count);
	}

	int err = slurprf(mystruct->fid);
	if (err) return err;

	if (offset >= enorm->size) *actual_count = 0;
	else if (count > enorm->size - offset) *actual_count = enorm->size - offset;
	else *actual_count = count;

	memcpy(buf, enorm->buf + offset, *actual_count);
	return 0;
}

static int write1(void *opaque, const void *buf, uint64_t offset, uint32_t count, uint32_t *actual_count) {
	struct openfile *mystruct = opaque;

	*actual_count = 0;

	// the easy path
	if (!mystruct->resfork) {
		return Write9(mystruct->fid, buf, offset, count, actual_count);
	}

	int err = slurprf(mystruct->fid);
	if (err) return err;

	if (offset + count > sizeof enorm->buf) panic("resource fork over 17 MB");

	if (offset > enorm->size) {
		memset(enorm->buf + enorm->size, 0, offset - enorm->size);
	}

	memcpy(enorm->buf + offset, buf, count);
	*actual_count = count;
	return 0;
}

static int geteof1(void *opaque, uint64_t *len) {
	struct openfile *mystruct = opaque;

	// the easy path
	if (!mystruct->resfork) {
		struct Stat9 stat = {};
		int err = Getattr9(mystruct->fid, STAT_SIZE, &stat);
		if (err) return err;
		*len = stat.size;
	}

	return Xattrwalk9(mystruct->fid, FID1, "com.apple.ResourceFork", len);
}

static int seteof1(void *opaque, uint64_t len) {
	struct openfile *mystruct = opaque;

	// the easy path
	if (!mystruct->resfork) {
		return Setattr9(mystruct->fid, SET_SIZE, (struct Stat9){.size=len});
	}

	int err = slurprf(mystruct->fid);
	if (err) return err;

	if (len > sizeof enorm->buf) panic("resource fork over 17 MB");

	if (len > enorm->size) {
		memset(enorm->buf + enorm->size, 0, len - enorm->size);
	}

	enorm->size = len;
	return 0;
}

int fgetattr1(uint32_t fid, const char *name, unsigned fields, struct MFAttr *attr) {
	// To be really clear, all these fields are zero until proven otherwise
	memset(attr, 0, sizeof *attr);

	// Costly: stat the data fork
	// The data fork is essential, so this is the only operation that can make the function fail
	if ((fields & MF_DSIZE) || (fields && MF_TIME)) {
		struct Stat9 dstat = {};
		int err = Getattr9(fid,
			((fields & MF_DSIZE) ? STAT_SIZE : 0) |
			((fields & MF_TIME) ? STAT_MTIME : 0),
			&dstat);
		if (err) return err;

		attr->dsize = dstat.size;
		attr->unixtime = dstat.mtime_sec;
	}

	// Costly: stat the resource fork
	if (fields & MF_RSIZE) {
		attr->rsize = 0;
		Xattrwalk9(fid, FID1, "com.apple.ResourceFork", &attr->rsize);
	}

	// Costly: read the Finder info
	if (fields & MF_FINFO) {
		if (!Xattrwalk9(fid, FID1, "com.apple.FinderInfo", NULL /*discard size*/)) {
			char finfo[32] = {};
			Read9(FID1, finfo, 0, 32, NULL); // read as much as possible up to this limit
			Clunk9(FID1);
			memcpy(attr->finfo, finfo, 16);
			memcpy(attr->fxinfo, finfo+16, 16);
		}
	}

	return 0;
}

int fsetattr1(uint32_t fid, const char *name, unsigned fields, const struct MFAttr *attr) {
	// (This function never needs to truncate/extend the file)

	printf("fsetattr1\n");

	// For now, let us not implement time-setting
	// (It might be possible to restrict it to the cases of Now and The Epoch, signalling a corrupt file in MPW)
	if (fields & MF_TIME) {
	}

	if (fields & MF_FINFO) {
		int Xattrcreate9(uint32_t fid, const char *name, uint64_t size, uint32_t flags);
		Walk9(fid, FID1, 0, NULL, NULL, NULL);

		int err = Xattrcreate9(FID1, "com.apple.FinderInfo", 32, 0 /*flags*/);
		if (err) return err;

		char finfo[32] = {};
		memcpy(finfo, attr->finfo, 16);
		memcpy(finfo+16, attr->fxinfo, 16);
		err = Write9(FID1, finfo, 0, 32, NULL);
		Clunk9(FID1);
		if (err) return err;
	}

	return 0;
}

int dgetattr1(uint32_t fid, const char *name, unsigned fields, struct MFAttr *attr) {
	// Benignly unimplemented
	memset(attr, 0, sizeof *attr);
	return 0;
}

int dsetattr1(uint32_t fid, const char *name, unsigned fields, const struct MFAttr *attr) {
	// Benignly unimplemented
	return 0;
}

static int move1(uint32_t fid1, const char *name1, uint32_t fid2, const char *name2) {
	return Renameat9(fid1, name1, fid2, name2);
}

static int del1(uint32_t fid, const char *name, bool isdir) {
	Walk9(fid, FID1, 1, (const char *[]){".."}, NULL, NULL);

	if (isdir) {
		return Unlinkat9(FID1, name, 0x200 /*AT_REMOVEDIR*/);
	} else {
		return Unlinkat9(FID1, name, 0);
	}
}

static bool issidecar1(const char *name) {
	return false;
}

struct MFImpl MF1 = {
	.Name = "1:1 Darwin metadata",
	.Init = &init1,
	.Open = &open1,
	.Close = &close1,
	.Read = &read1,
	.Write = &write1,
	.SetEOF = &seteof1,
	.FGetAttr = &fgetattr1,
	.FSetAttr = &fsetattr1,
	.DGetAttr = &dgetattr1,
	.DSetAttr = &dsetattr1,
	.Move = &move1,
	.Del = &del1,
	.IsSidecar = &issidecar1,
};
