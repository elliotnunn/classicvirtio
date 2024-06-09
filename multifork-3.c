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
#include "fids.h"
#include "hashtab.h"
#include "panic.h"
#include "printf.h"
#include "rez.h"

#include "multifork.h"

#include <stdint.h>
#include <stdlib.h>

// Fids 8-15 are reserved for the multifork layer
enum {
	DIRFID = FIRSTFID_MULTIFORK,
	RESFORKFID, // actual resource formatted file in a hidden place
	REZFID, // Rez code in a public place
	FINFOFID,
	TMPFID,
	MAXFORK = 0x1100000,
};

// Per-open-file information hidden inside FCB, maximum 12 bytes
struct openfile {
	bool resfork;
	uint32_t fid;
	int32_t cnid;
};

int OpenSidecar(uint32_t fid, int32_t cnid, int flags, const char *fmt); // borrowed from device-9p.c
int DeleteSidecar(int32_t cnid, const char *fmt);

static void statResourceFork(int32_t cnid, uint32_t mainfid, const char *name, struct Stat9 *stat);
static void openResourceFork(int32_t cnid, uint32_t mainfid, const char *name, uint32_t cachefid);
static void flushResourceFork(int32_t cnid, uint32_t cachefid);
// no need to prototype init3, open3 etc... they are used once at bottom of file

static int init3(void) {
	int err;

	for (;;) { // essentially mkdir -p
		err = WalkPath9(DOTDIRFID, DIRFID, "resforks");
		if (!err) break;
		if (err != ENOENT) panic("unexpected mkdir-walk err");
		err = Mkdir9(1, 0777, 0, "resforks", NULL);
		if (err && err != EEXIST)  panic("unexpected mkdir err");
	}

	return 0;
}

static int open3(void *opaque, short refnum, int32_t cnid, uint32_t fid, const char *name, bool resfork, bool write) {
	struct openfile *s = opaque;
	int err = 0;

	s->resfork = resfork;
	s->fid = 32 + refnum;
	s->cnid = cnid;

	if (!resfork) {
		// Data fork is relatively simple: the file can only be opened if it exists
		WalkPath9(fid, s->fid, "");

		if (write) {
			err = Lopen9(s->fid, O_RDWR, NULL, NULL);
			if (err == 0) return 0;
		}

		err = Lopen9(s->fid, O_RDONLY, NULL, NULL);
		return err;
	} else {
		openResourceFork(cnid, fid, name, s->fid);
		return noErr;
	}
}

static int close3(void *opaque) {
	struct openfile *s = opaque;

	if (!s->resfork) {
		return Clunk9(s->fid);
	} else {
		flushResourceFork(s->cnid, s->fid);
		Clunk9(s->fid);
		return 0;
	}
}

static int read3(void *opaque, void *buf, uint64_t offset, uint32_t count, uint32_t *actual_count) {
	struct openfile *s = opaque;

	return Read9(s->fid, buf, offset, count, actual_count);
}

static int write3(void *opaque, const void *buf, uint64_t offset, uint32_t count, uint32_t *actual_count) {
	struct openfile *s = opaque;

	return Write9(s->fid, buf, offset, count, actual_count);
}

static int geteof3(void *opaque, uint64_t *len) {
	struct openfile *s = opaque;

	struct Stat9 stat = {};
	int err = Getattr9(s->fid, STAT_SIZE, &stat);
	if (err) return err;
	*len = stat.size;

	return 0;
}

static int seteof3(void *opaque, uint64_t len) {
	struct openfile *s = opaque;

	int err = Setattr9(s->fid, SET_SIZE, (struct Stat9){.size=len});
	if (err) return err;

	if (s->resfork) {
		flushResourceFork(s->cnid, s->fid);
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
		struct Stat9 rstat = {};
		statResourceFork(cnid, fid, name, &rstat);

		attr->rsize = rstat.size;
		if (attr->unixtime < rstat.mtime_sec) attr->unixtime = rstat.mtime_sec;
	}

	// Costly: read the Finder info
	// Unanswered question: what is the difference between four-nulls and four-question-marks?
	if (fields & MF_FINFO) {
		char ipath[MAXNAME];
		sprintf(ipath, "../%s.idump", name);

		if (!WalkPath9(fid, FINFOFID, ipath)
				&&
				!Lopen9(FINFOFID, O_RDONLY, NULL, NULL)) {
			Read9(FINFOFID, attr->finfo, 0, 8, NULL);
			Clunk9(FINFOFID);
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
		int err = WalkPath9(fid, FINFOFID, "..");
		if (err) panic("dot-dot should never fail");

		char iname[MAXNAME];
		strcpy(iname, name);
		strcat(iname, ".idump");
		err = Lcreate9(FINFOFID, O_WRONLY|O_TRUNC|O_CREAT, 0666, 0, iname, NULL, NULL);
		if (err) return err;

		err = Write9(FINFOFID, attr->finfo, 0, 8, NULL);
		if (err) return err;

		Clunk9(FINFOFID);
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
		char sidename1[MAXNAME], sidename2[MAXNAME];
		sprintf(sidename1, sidecars[i], name1);
		sprintf(sidename2, sidecars[i], name2);
		err = Renameat9(fid1, sidename1, fid2, sidename2);
		if (err && err != ENOENT) worsterr = err;
	}

	return worsterr;
}

static int del3(uint32_t fid, const char *name, bool isdir) {
	WalkPath9(fid, TMPFID, "..");

	if (isdir) {
		return Unlinkat9(TMPFID, name, 0x200 /*AT_REMOVEDIR*/);
	} else {
		// The main file must be deleted,
		// then delete the sidecars on a best-effort basis
		const char *sidecars[] = {"%s", "%s.rdump", "%s.idump"};
		for (int i=0; i<sizeof sidecars/sizeof *sidecars; i++) {
			char delname[MAXNAME];
			sprintf(delname, sidecars[i], name);
			int err = Unlinkat9(TMPFID, delname, 0);
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

// This function is idempotent. It brings the cached resource fork up to date and stats it.
// Nothing is left open upon return.

// For posterity: while writing this code I gave up on writing atomic AND consistent 9P calls,
// and decided to implement the "conch" system.
static void statResourceFork(int32_t cnid, uint32_t mainfid, const char *name, struct Stat9 *stat) {
	int err;

// 	printf("statResourceFork cnid=%08x mainfid=%d name=%s\n", cnid, mainfid, name);

	bool sidecarExists = false; // if so, it will be open read-only at REZFID
	bool cacheExists = false;
	struct Stat9 sidecarStat, cacheStat;

	// Navigate to the sidecar file (.rdump), check for existence, get mtime (if applicable), don't open
	char rpath[MAXNAME];
	sprintf(rpath, "../%s.rdump", name);
	err = WalkPath9(mainfid, REZFID, rpath);
	if (!err) {
		if (Getattr9(REZFID, STAT_MTIME|STAT_SIZE, &sidecarStat))
			panic("failed stat sidecar");
		sidecarExists = true;
	}

	// Navigate to the hidden cache file, check for existence, get mtime (if applicable), don't open
	char cachename[12]; // enough room to append .tmp also
	sprintf(cachename, "%08x", cnid);
	if (!WalkPath9(DIRFID, RESFORKFID, cachename)) {
		if (Getattr9(RESFORKFID, STAT_MTIME|STAT_SIZE, &cacheStat))
			panic("failed stat extant res cache");
		cacheExists = true;
	}

	// Simple case: is a costly parse needed?
// 	printf("rdump file: exists=%d mtime=%08lx%08lx.%08lx\n",
// 		sidecarExists, (long)(sidecarStat.mtime_sec >> 32), (long)sidecarStat.mtime_sec, (long)sidecarStat.mtime_nsec);
// 	printf("cache file: exists=%d mtime=%08lx%08lx.%08lx\n",
// 		cacheExists, (long)(cacheStat.mtime_sec >> 32), (long)cacheStat.mtime_sec, (long)cacheStat.mtime_nsec);

	char tmpname[16];
	sprintf(tmpname, "%08x.tmp", cnid);

	if (sidecarExists) {
		if (!cacheExists ||
			sidecarStat.mtime_sec>cacheStat.mtime_sec ||
			sidecarStat.mtime_sec==cacheStat.mtime_sec && sidecarStat.mtime_nsec>cacheStat.mtime_nsec
		) {
// 			printf("... cache stale or nonexistent, doing a costly parse ...\n");

			WalkPath9(DIRFID, RESFORKFID, "");
			if (Lcreate9(RESFORKFID, O_WRONLY|O_TRUNC, 0666, 0, tmpname, NULL, NULL))
				panic("failed create new res cache");
			if (Lopen9(REZFID, O_RDONLY, NULL, NULL))
				panic("failed open extant sidecar");

			uint32_t size = Rez(REZFID, RESFORKFID);
			Clunk9(REZFID);
			Clunk9(RESFORKFID);

			if (WalkPath9(DIRFID, RESFORKFID, tmpname))
				panic("failed walk new tmp res cache");
			if (Setattr9(RESFORKFID, SET_MTIME|SET_MTIME_SET, sidecarStat))
				panic("failed setmtime res cache");
			Clunk9(RESFORKFID);

			if (Renameat9(DIRFID, tmpname, DIRFID, cachename))
				panic("failed rename res cache");

			stat->size = size;
			stat->mtime_sec = sidecarStat.mtime_sec;
			stat->mtime_nsec = sidecarStat.mtime_nsec;
		} else {
// 			printf("... cache is already in date, doing nothing\n");

			stat->size = cacheStat.size;
			stat->mtime_sec = cacheStat.mtime_sec;
			stat->mtime_nsec = cacheStat.mtime_nsec;
		}
	} else {
		// sidecar nonexistent: make an empty resfork cache file,
		// but make its mtime far in the past, not to pollute the overall file's mtime
		WalkPath9(DIRFID, RESFORKFID, "");
		if (Lcreate9(RESFORKFID, O_WRONLY|O_TRUNC, 0666, 0, cachename, NULL, NULL))
			panic("failed create empty res cache");
		if (Setattr9(RESFORKFID, SET_MTIME|SET_MTIME_SET, (struct Stat9){.mtime_sec=0, .mtime_nsec=0}))
			panic("failed setmtime empty res cache");
		Clunk9(RESFORKFID);
	}
}

static void openResourceFork(int32_t cnid, uint32_t mainfid, const char *name, uint32_t cachefid) {
	struct Stat9 stat;
	statResourceFork(cnid, mainfid, name, &stat);

	char cachename[MAXNAME];
	sprintf(cachename, "%08x", cnid);
	if (WalkPath9(DIRFID, cachefid, cachename))
		panic("failed walk extant res cache");
	if (Lopen9(cachefid, O_RDWR, NULL, NULL))
		panic("failed open extant res cache");
}

static void flushResourceFork(int32_t cnid, uint32_t cachefid) {
	struct Stat9 cachetime;
	if (Getattr9(cachefid, STAT_MTIME|STAT_SIZE, &cachetime))
		panic("flushResourceFork getattr cachefid");

	if (cachetime.size == 0) {
		DeleteSidecar(cnid, "%s.rdump");
		// problem: the file's mtime won't be updated
		return;
	}

	// not atomic, needs a rethink
	if (OpenSidecar(REZFID, cnid, O_WRONLY|O_CREAT|O_TRUNC, "%s.rdump"))
		panic("failed OpenSidecar for writeout");

	struct Stat9 sidecartime;
	if (Getattr9(REZFID, STAT_MTIME, &sidecartime))
		panic("flushResourceFork getattr REZFID");

	if (cachetime.mtime_sec==sidecartime.mtime_sec && cachetime.mtime_nsec==sidecartime.mtime_nsec) {
		Clunk9(REZFID);
		return;
	}


	DeRez(cachefid, REZFID);

	if (Setattr9(REZFID, SET_MTIME|SET_MTIME_SET, cachetime))
		panic("flushResourceFork setattr");

	Clunk9(REZFID);
}
