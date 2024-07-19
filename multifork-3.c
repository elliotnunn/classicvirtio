/* Copyright (c) 2023-2024 Elliot Nunn */
/* Licensed under the MIT license */

/*
Implementation of the 3-file multifork format
(best for development)

FILE       = data fork
FILE.rdump = resource fork in Rez format
FILE.idump = first 8 bytes of Finder info (i.e. type/creator)

Directory metadata is discarded
*/

#include <string.h>
#include <OSUtils.h>

#include "9buf.h"
#include "9p.h"
#include "FSM.h"
#include "catalog.h"
#include "derez.h"
#include "fids.h"
#include "panic.h"
#include "printf.h"
#include "rez.h"
#include "universalfcb.h"

#include "multifork.h"

#include <stdint.h>
#include <stdlib.h>

// Fids 8-15 are reserved for the multifork layer
enum {
	DIRFID = FIRSTFID_MULTIFORK,
	RESFORKFID, // actual resource formatted file in a hidden place
	CLEANRECFID,
	REZFID, // Rez code in a public place
	FINFOFID,
	TMPFID,
	DIRTYFLAG = 1,
};

static void statResourceFork(int32_t cnid, uint32_t mainfid, const char *name, struct Stat9 *stat);
static void pullResourceFork(int32_t cnid, uint32_t mainfid, const char *name, struct Stat9 *stat);
static void pushResourceFork(int32_t cnid, uint32_t mainfid, const char *name);
static uint32_t fidof(struct MyFCB *fcb);
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

	// Linear-search for a free directory name; TODO: delete stale directories
	for (uint32_t i=0;; i++) {
		char name[16];
		sprintf(name, "%0ld");
		err = Mkdir9(DIRFID, 0777, 0, name, NULL);
		if (!err) {
			if (WalkPath9(DIRFID, DIRFID, name)) {
				panic("unexpected mkdir-walk err");
			}
			break;
		}
	}

	// yay, now everything is discardable!

	return 0;
}

static int open3(struct MyFCB *fcb, int32_t cnid, uint32_t fid, const char *name) {
	int err = 0;
	if (fcb->fcbFlags&fcbResourceMask) {
		struct Stat9 junk;
		statResourceFork(cnid, fid, name, &junk);

		char path[9];
		sprintf(path, "%08x", cnid);
		if (WalkPath9(DIRFID, fidof(fcb), path)) {
			panic("could not open even a stattable res fork");
		}
	} else {
		// Data fork is relatively simple: the file can only be opened if it exists
		WalkPath9(fid, fidof(fcb), "");
	}

	if (fcb->fcbFlags&fcbWriteMask) {
		err = Lopen9(fidof(fcb), O_RDWR, NULL, NULL);
		if (err == 0) return 0;
	}
	err = Lopen9(fidof(fcb), O_RDONLY, NULL, NULL);
	return err;
}

static int close3(struct MyFCB *fcb) {
	if ((fcb->fcbFlags&fcbResourceMask) && (fcb->mfFlags&DIRTYFLAG)) {
		for (struct MyFCB *i=UnivFirst(fcb->fcbFlNm, true); i!=NULL; i=UnivNext(fcb)) {
			i->mfFlags &= ~DIRTYFLAG;
		}
		pushResourceFork(fcb->fcbFlNm, fidof(fcb), getDBName(fcb->fcbFlNm));
	}

	return Clunk9(fidof(fcb));
}

static int read3(struct MyFCB *fcb, void *buf, uint64_t offset, uint32_t count, uint32_t *actual_count) {
	return Read9(fidof(fcb), buf, offset, count, actual_count);
}

static int write3(struct MyFCB *fcb, const void *buf, uint64_t offset, uint32_t count, uint32_t *actual_count) {
	if ((fcb->fcbFlags & fcbResourceMask) && !(fcb->mfFlags & DIRTYFLAG)) {
		for (struct MyFCB *i=UnivFirst(fcb->fcbFlNm, true); i!=NULL; i=UnivNext(fcb)) {
			i->mfFlags |= DIRTYFLAG;
		}
	}
	return Write9(fidof(fcb), buf, offset, count, actual_count);
}

static int geteof3(struct MyFCB *fcb, uint64_t *len) {
	struct Stat9 stat = {};
	int err = Getattr9(fidof(fcb), STAT_SIZE, &stat);
	if (err) return err;
	*len = stat.size;

	return 0;
}

static int seteof3(struct MyFCB *fcb, uint64_t len) {
	int err = Setattr9(fidof(fcb), SET_SIZE, (struct Stat9){.size=len});
	if (err) return err;

	if ((fcb->fcbFlags&fcbResourceMask) && (fcb->mfFlags&DIRTYFLAG)) {
		for (struct MyFCB *i=UnivFirst(fcb->fcbFlNm, true); i!=NULL; i=UnivNext(fcb)) {
			i->mfFlags &= ~DIRTYFLAG;
		}
		pushResourceFork(fcb->fcbFlNm, fidof(fcb), getDBName(fcb->fcbFlNm));
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
	if (len >= 10 && !strcmp(name+len-10, ".rdump.tmp")) return true;
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
static void statResourceFork(int32_t cnid, uint32_t mainfid, const char *name, struct Stat9 *stat) {
	int err;

	// printf("statResourceFork cnid=%08x mainfid=%d name=%s\n", cnid, mainfid, name);

	// Delightfully quick case
	struct MyFCB *alreadyopen = UnivFirst(cnid, true);
	if (alreadyopen) {
		Getattr9(fidof(alreadyopen), STAT_SIZE|STAT_MTIME, stat);
		return;
	}

	char forkname[9], rsname[15], sidecarname[MAXNAME+12];
	sprintf(forkname, "%08lx", cnid);
	sprintf(rsname, "%08lx-rezstat", cnid);
	sprintf(sidecarname, "../%s.rdump", name);

	if (WalkPath9(DIRFID, CLEANRECFID, rsname)) {
		pullResourceFork(cnid, mainfid, name, stat);
		return;
	}

	struct Stat9 expect = {};
	bool emptyrf = Read9(CLEANRECFID, &expect, 0, sizeof expect, NULL) != 0;
	Clunk9(CLEANRECFID);

	bool nosidecar = WalkPath9(mainfid, REZFID, sidecarname) != 0;
	if (emptyrf && nosidecar) {
		memset(stat, 0, sizeof *stat); // no resource fork
		return;
	} else if (emptyrf || nosidecar) {
		pullResourceFork(cnid, mainfid, name, stat);
		return;
	}

	struct Stat9 scstat = {};
	Getattr9(REZFID, STAT_SIZE|STAT_MTIME, &scstat);
	if (memcmp(&scstat, &expect, sizeof expect)) {
		pullResourceFork(cnid, mainfid, name, stat);
		return;
	}

	// actually we are up to date, which is nice
	WalkPath9(DIRFID, RESFORKFID, forkname);
	Getattr9(RESFORKFID, STAT_SIZE, stat);
	stat->mtime_sec = expect.mtime_sec;
	stat->mtime_nsec = expect.mtime_nsec;
}

static void pullResourceFork(int32_t cnid, uint32_t mainfid, const char *name, struct Stat9 *stat) {
	char forkname[9], rsname[15], sidecarname[MAXNAME+12];
	sprintf(forkname, "%08lx", cnid);
	sprintf(rsname, "%08lx-rezstat", cnid);
	sprintf(sidecarname, "../%s.rdump", name);

	bool empty = WalkPath9(mainfid, REZFID, sidecarname);

	if (empty) {
		WalkPath9(DIRFID, RESFORKFID, "");
		Lcreate9(RESFORKFID, O_WRONLY|O_TRUNC, 0666, 0, forkname, NULL, NULL);
		Clunk9(RESFORKFID);

		// empty cleanrecfid
		WalkPath9(DIRFID, CLEANRECFID, "");
		if (Lcreate9(CLEANRECFID, O_WRONLY|O_TRUNC, 0666, 0, rsname, NULL, NULL)) {
			panic("failed create empty rezstat file");
		}
		Clunk9(CLEANRECFID);

		memset(stat, 0, sizeof *stat);
	} else {
		struct Stat9 scstat = {};
		Getattr9(REZFID, STAT_MTIME|STAT_SIZE, &scstat);
		if (Lopen9(REZFID, O_RDONLY, NULL, NULL)) {
			panic("failed open extant sidecar");
		}

		WalkPath9(DIRFID, RESFORKFID, "");
		if (Lcreate9(RESFORKFID, O_WRONLY|O_TRUNC, 0666, 0, forkname, NULL, NULL)) {
			panic("failed create rf cache");
		}

		uint32_t size = Rez(REZFID, RESFORKFID);
		Setattr9(RESFORKFID, SET_MTIME|SET_MTIME_SET, scstat);

		WalkPath9(DIRFID, CLEANRECFID, "");
		if (Lcreate9(CLEANRECFID, O_WRONLY|O_TRUNC, 0666, 0, rsname, NULL, NULL)) {
			panic("failed create rezstat file");
		}
		Write9(CLEANRECFID, &scstat, 0, sizeof scstat, NULL);

		Clunk9(REZFID);
		Clunk9(RESFORKFID);
		Clunk9(CLEANRECFID);

		stat->size = size;
		stat->mtime_sec = scstat.mtime_sec;
		stat->mtime_nsec = scstat.mtime_nsec;
	}
}

static void pushResourceFork(int32_t cnid, uint32_t mainfid, const char *name) {
	char forkname[9], rsname[15], sidecarname[MAXNAME+16];
	sprintf(forkname, "%08lx", cnid);
	sprintf(rsname, "%08lx-rezstat", cnid);
	sprintf(sidecarname, "../%s.rdump.tmp", name);

	if (WalkPath9(DIRFID, RESFORKFID, forkname)) {
		panic("pushResourceFork no fork to see");
	}
	struct Stat9 forkstat = {};
	Getattr9(REZFID, STAT_SIZE, &forkstat);

	if (forkstat.size == 0) {
		Unlinkat9(DIRFID, rsname, 0); // no "clean" record
		Unlinkat9(mainfid, sidecarname, 0); // no "rdump" file
	} else {
		WalkPath9(mainfid, REZFID, "");
		if (!Lcreate9(REZFID, O_WRONLY|O_TRUNC, 0666, 0, sidecarname, NULL, NULL)) {
			panic("unable to create sidecar file");
		}
		Lopen9(RESFORKFID, O_RDONLY, NULL, NULL);

		DeRez(RESFORKFID, REZFID);
		struct Stat9 scstat = {};
		Getattr9(REZFID, STAT_SIZE|STAT_MTIME, &scstat);
		Clunk9(REZFID);
		Clunk9(RESFORKFID);

		char n1[MAXNAME+16], n2[MAXNAME+16];
		sprintf(n1, "../%s.rdump.tmp", name);
		sprintf(n2, "../%s.rdump", name);
		WalkPath9(mainfid, TMPFID, "..");
		Renameat9(TMPFID, n1, TMPFID, n2);

		WalkPath9(DIRFID, CLEANRECFID, "");
		if (Lcreate9(CLEANRECFID, O_WRONLY|O_TRUNC, 0666, 0, rsname, NULL, NULL)) {
			panic("failed create rezstat file");
		}
		Write9(CLEANRECFID, &scstat, 0, sizeof scstat, NULL);

		Clunk9(RESFORKFID);
		Clunk9(CLEANRECFID);
	}
}

static uint32_t fidof(struct MyFCB *fcb) {
	return 32UL + fcb->refNum;
}
