/* Copyright (c) 2023 Elliot Nunn */
/* Licensed under the MIT license */

/*
Implementation of the 3-file multifork format
(best for development)

FILE       = data fork
FILE.rsrc  = resource fork (TODO conver to Rez format)
FILE.idump = first 8 bytes of Finder info (i.e. type/creator)

Directory metadata is discarded
*/

#include <string.h>

#include "9p.h"
#include "panic.h"
#include "printf.h"

#include "multifork.h"

// Fids 8-15 are reserved for the multifork layer
enum {
	FID1 = 8,
};

// Per-open-file information hidden inside FCB, maximum 12 bytes
struct openfile {
	short fid;
};

static int init3(void) {
	return 0;
}

static int open3(void *opaque, short refnum, uint32_t fid, const char *name, bool resfork, bool write) {
	struct openfile *mystruct = opaque;
	int err = 0;

	uint32_t newfid = mystruct->fid = 32 + refnum;

	if (!resfork) {
		// Data fork is relatively simple: the file can only be opened if it exists
		Walk9(fid, newfid, 0, NULL, NULL, NULL);

		if (write) {
			err = Lopen9(newfid, O_RDWR, NULL, NULL);
			if (err == 0) return 0;
		}

		err = Lopen9(newfid, O_RDONLY, NULL, NULL);
		return err;
	} else {
		// Res fork is trickier: we must create a sidecar file silently
		// open(3) with the right arguments would do this,
		// but 9P forces combination of Tcreate and Tlopen
		char rname[1024];
		strcpy(rname, name);
		strcat(rname, ".rsrc");
		int access = write ? O_RDWR : O_RDONLY;

		Walk9(fid, newfid, 1, (const char *[]){".."}, NULL, NULL);

		// This is potentially racy with another client, so retry, but not forever
		for (int i=0; i<16; i++) {
			err = Lcreate9(newfid, O_CREAT|O_EXCL|access, 0777, 0, rname, NULL, NULL);
			if (err == 0) return 0;
			else if (err != EEXIST) return err;

			err = Walk9(newfid, newfid, 1, (const char *[]){rname}, NULL, NULL);
			if (err) continue; // file was deleted since we tried Lcreate9

			err = Lopen9(newfid, access, NULL, NULL);
			if (err == 0) return 0;

			if (err == EACCES) {
				err = Lopen9(newfid, O_RDONLY, NULL, NULL);
				if (err == 0) return 0;
			}

			// Go around again
			Walk9(newfid, newfid, 1, (const char *[]){".."}, NULL, NULL);
		}
	}
}

static int close3(void *opaque) {
	struct openfile *mystruct = opaque;
	Clunk9(mystruct->fid);
	return 0;
}

static int read3(void *opaque, void *buf, uint64_t offset, uint32_t count, uint32_t *actual_count) {
	struct openfile *mystruct = opaque;

	return Read9(mystruct->fid, buf, offset, count, actual_count);
}

static int write3(void *opaque, const void *buf, uint64_t offset, uint32_t count, uint32_t *actual_count) {
	struct openfile *mystruct = opaque;

	return Write9(mystruct->fid, buf, offset, count, actual_count);
}

static int seteof3(void *opaque, uint64_t len) {
	struct openfile *mystruct = opaque;

	return Setattr9(mystruct->fid, SET_SIZE, (struct Stat9){.size=len});
}

int fgetattr3(uint32_t fid, const char *name, unsigned fields, struct MFAttr *attr) {
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

	// Costly: stat the resource fork
	if ((fields & MF_RSIZE) || (fields & MF_TIME)) {
		struct Stat9 rstat = {};
		char rname[1024];
		strcpy(rname, name);
		strcat(rname, ".rsrc");

		if (!Walk9(fid, FID1, 2, (const char *[]){"..", rname}, NULL, NULL))
			if (!Getattr9(FID1,
					((fields & MF_DSIZE) ? STAT_SIZE : 0) |
					((fields & MF_TIME) ? STAT_MTIME : 0),
					&rstat)) {
				attr->rsize = rstat.size;
				if (attr->unixtime < rstat.mtime_sec) attr->unixtime = rstat.mtime_sec;
			}
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

int fsetattr3(uint32_t fid, const char *name, unsigned fields, const struct MFAttr *attr) {
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

int dgetattr3(uint32_t fid, const char *name, unsigned fields, struct MFAttr *attr) {
	// Benignly unimplemented
	memset(attr, 0, sizeof *attr);
	return 0;
}

int dsetattr3(uint32_t fid, const char *name, unsigned fields, const struct MFAttr *attr) {
	// Benignly unimplemented
	return 0;
}

static int move3(uint32_t fid1, const char *name1, uint32_t fid2, const char *name2) {
	int err = Renameat9(fid1, name1, fid2, name2);
	if (err) return err;

	int worsterr = 0;

	const char *sidecars[] = {"%s.rsrc", "%s.idump"};
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
		const char *sidecars[] = {"%s", "%s.rsrc", "%s.idump"};
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
	if (len >= 5 && !strcmp(name+len-5, ".rsrc")) return true;
	if (len >= 6 && !strcmp(name+len-6, ".idump")) return true;

	return false;
}

struct MFImpl MF3 = {
	.Name = ".idump/.rsrc",
	.Init = &init3,
	.Open = &open3,
	.Close = &close3,
	.Read = &read3,
	.Write = &write3,
	.SetEOF = &seteof3,
	.FGetAttr = &fgetattr3,
	.FSetAttr = &fsetattr3,
	.DGetAttr = &dgetattr3,
	.DSetAttr = &dsetattr3,
	.Move = &move3,
	.Del = &del3,
	.IsSidecar = &issidecar3,
};
