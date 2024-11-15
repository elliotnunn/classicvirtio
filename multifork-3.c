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
	PARENTFID,
	DIRTYFLAG = 1,
};

static void statResourceFork(int32_t cnid, uint32_t parentfid, const char *name, struct Stat9 *stat);
static void pullResourceFork(int32_t cnid, uint32_t parentfid, const char *name, struct Stat9 *stat);
static void pushResourceFork(int32_t cnid, uint32_t parentfid, const char *name);
static int flagsToText(char *buf, const char finfo[16], const char fxinfo[16]);
static void textToFlags(char finfo[16], char fxinfo[16], const char * text, int len);
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
		char name[MAXNAME];
		sprintf(name, "%ld", i);
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
		WalkPath9(fid, PARENTFID, "..");
		statResourceFork(cnid, PARENTFID, name, &junk);

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
			i->mfFlags &= ~DIRTYFLAG; // clear it
		}
		char name[MAXNAME];
		int32_t parent = CatalogGet(fcb->fcbFlNm, name);
		if (IsErr(parent)) {
			panic("file was deleted while open");
		}
		if (IsErr(CatalogWalk(PARENTFID, parent, NULL, NULL, NULL))) {
			panic("file went missing while open");
		}
		pushResourceFork(fcb->fcbFlNm, PARENTFID, name);
	}

	return Clunk9(fidof(fcb));
}

static int read3(struct MyFCB *fcb, void *buf, uint64_t offset, uint32_t count, uint32_t *actual_count) {
	return Read9(fidof(fcb), buf, offset, count, actual_count);
}

static int write3(struct MyFCB *fcb, const void *buf, uint64_t offset, uint32_t count, uint32_t *actual_count) {
	if ((fcb->fcbFlags & fcbResourceMask) && !(fcb->mfFlags & DIRTYFLAG)) {
		for (struct MyFCB *i=UnivFirst(fcb->fcbFlNm, true); i!=NULL; i=UnivNext(fcb)) {
			i->mfFlags |= DIRTYFLAG; // set it
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

	// Take this as a promise that a resource file is consistent,
	// and an opportunity to write it out
	if ((fcb->fcbFlags&fcbResourceMask) && ((fcb->mfFlags&DIRTYFLAG) || len==0)) {
		for (struct MyFCB *i=UnivFirst(fcb->fcbFlNm, true); i!=NULL; i=UnivNext(fcb)) {
			i->mfFlags &= ~DIRTYFLAG; // clear it
		}
		char name[MAXNAME];
		int32_t parent = CatalogGet(fcb->fcbFlNm, name);
		if (IsErr(parent)) {
			panic("file was deleted while open");
		}
		if (IsErr(CatalogWalk(PARENTFID, parent, NULL, NULL, NULL))) {
			panic("file went missing while open");
		}
		pushResourceFork(fcb->fcbFlNm, PARENTFID, name);
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

	if ((fields & MF_RSIZE) || (fields & MF_TIME) || (fields & MF_FINFO)) {
		WalkPath9(fid, PARENTFID, "..");
	}

	// Very costly: ensure the resource fork has been Rezzed into the cache
	if ((fields & MF_RSIZE) || (fields & MF_TIME)) {
		struct Stat9 rstat = {};
		statResourceFork(cnid, PARENTFID, name, &rstat);

		attr->rsize = rstat.size;
		if (attr->unixtime < rstat.mtime_sec) attr->unixtime = rstat.mtime_sec;
	}

	// Costly: read the Finder info
	if (fields & MF_FINFO) {
		char ipath[MAXNAME+12];
		sprintf(ipath, "../%s.idump", name);

		if (!WalkPath9(fid, FINFOFID, ipath)
				&&
				!Lopen9(FINFOFID, O_RDONLY, NULL, NULL)) {
			uint32_t len = 0;
			char buffer[512];
			Read9(FINFOFID, buffer, 0, sizeof buffer-1, &len);
			Clunk9(FINFOFID);
			buffer[len] = 0;
			textToFlags(attr->finfo, attr->fxinfo, buffer, len);
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

		char blob[512];
		int len = flagsToText(blob, attr->finfo, attr->fxinfo);
		err = Write9(FINFOFID, blob, 0, len, NULL);
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
static void statResourceFork(int32_t cnid, uint32_t parentfid, const char *name, struct Stat9 *stat) {
	int err;

	// printf("statResourceFork cnid=%08x parentfid=%d name=%s\n", cnid, parentfid, name);

	// Delightfully quick case
	struct MyFCB *alreadyopen = UnivFirst(cnid, true);
	if (alreadyopen) {
		printf("resource fork cache authoritative because open\n");
		Getattr9(fidof(alreadyopen), STAT_SIZE|STAT_MTIME, stat);
		return;
	}

	char forkname[MAXNAME], rsname[MAXNAME], sidecarname[MAXNAME+12];
	sprintf(forkname, "%08lx", cnid);
	sprintf(rsname, "%08lx-rezstat", cnid);
	sprintf(sidecarname, "%s.rdump", name);

	if (WalkPath9(DIRFID, CLEANRECFID, rsname)) {
		printf("(because no -rezstat file) ");
		pullResourceFork(cnid, parentfid, name, stat);
		return;
	}

	struct Stat9 expect = {};
	if (Lopen9(CLEANRECFID, O_RDONLY, NULL, NULL)) {
		panic("could not open existing -rezstat");
	}
	uint32_t statfilesize = 0;
	Read9(CLEANRECFID, &expect, 0, sizeof expect, &statfilesize);
	Clunk9(CLEANRECFID);

	bool nosidecar = WalkPath9(parentfid, REZFID, sidecarname) != 0;
	if (statfilesize==0 && nosidecar) {
		printf("resource fork cache agreed empty\n");
		memset(stat, 0, sizeof *stat); // agree, empty resource fork
		return;
	} else if (statfilesize==0) {
		printf("(because rdump newly created) ");
		pullResourceFork(cnid, parentfid, name, stat);
		return;
	} else if (nosidecar) {
		printf("(because sidecar newly deleted) ");
		pullResourceFork(cnid, parentfid, name, stat);
		return;
	}

	struct Stat9 scstat = {};
	Getattr9(REZFID, STAT_SIZE|STAT_MTIME, &scstat);
	if (scstat.size!=expect.size || scstat.mtime_sec!=expect.mtime_sec || scstat.mtime_nsec!=expect.mtime_nsec) {
		printf("(because of stat mismatch) ");
		pullResourceFork(cnid, parentfid, name, stat);
		return;
	}

	printf("resource fork cache up to date\n");

	// actually we are up to date, which is nice
	WalkPath9(DIRFID, RESFORKFID, forkname);
	Getattr9(RESFORKFID, STAT_SIZE, stat);
	stat->mtime_sec = expect.mtime_sec;
	stat->mtime_nsec = expect.mtime_nsec;
}

static void pullResourceFork(int32_t cnid, uint32_t parentfid, const char *name, struct Stat9 *stat) {
	printf("pullResourceFork\n");
	char forkname[MAXNAME], rsname[MAXNAME], sidecarname[MAXNAME+12];
	sprintf(forkname, "%08lx", cnid);
	sprintf(rsname, "%08lx-rezstat", cnid);
	sprintf(sidecarname, "%s.rdump", name);

	bool empty = WalkPath9(parentfid, REZFID, sidecarname);

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

static void pushResourceFork(int32_t cnid, uint32_t parentfid, const char *name) {
	printf("pushResourceFork %s", name);
	char forkname[MAXNAME], rsname[MAXNAME], sidecarname[MAXNAME+12], sidecartmpname[MAXNAME+12];
	sprintf(forkname, "%08lx", cnid);
	sprintf(rsname, "%08lx-rezstat", cnid);
	sprintf(sidecarname, "%s.rdump", name);
	sprintf(sidecartmpname, "%s.rdump.tmp", name);

	if (WalkPath9(DIRFID, RESFORKFID, forkname)) {
		panic("pushResourceFork no fork to see");
	}
	struct Stat9 forkstat = {};
	Getattr9(RESFORKFID, STAT_SIZE, &forkstat);

	if (forkstat.size == 0) {
		printf(" = empty fork\n");
		// empty "clean" record
		WalkPath9(DIRFID, CLEANRECFID, "");
		if (Lcreate9(CLEANRECFID, O_WRONLY|O_TRUNC, 0666, 0, rsname, NULL, NULL)) {
			panic("failed create rezstat file");
		}
		Clunk9(CLEANRECFID);
		Unlinkat9(parentfid, sidecarname, 0); // no "rdump" file
	} else {
		printf(" = full fork\n");
		WalkPath9(parentfid, REZFID, "");
		if (Lcreate9(REZFID, O_WRONLY|O_TRUNC, 0666, 0, sidecartmpname, NULL, NULL)) {
			panic("unable to create sidecar file");
		}
		Lopen9(RESFORKFID, O_RDONLY, NULL, NULL);

		DeRez(RESFORKFID, REZFID);
		struct Stat9 scstat = {};
		Getattr9(REZFID, STAT_SIZE|STAT_MTIME, &scstat);
		Clunk9(REZFID);
		Clunk9(RESFORKFID);

		char n1[MAXNAME+12], n2[MAXNAME+12];
		sprintf(n1, "%s.rdump.tmp", name);
		sprintf(n2, "%s.rdump", name);
		Renameat9(parentfid, n1, parentfid, n2);

		WalkPath9(DIRFID, CLEANRECFID, "");
		if (Lcreate9(CLEANRECFID, O_WRONLY|O_TRUNC, 0666, 0, rsname, NULL, NULL)) {
			panic("failed create rezstat file");
		}
		Write9(CLEANRECFID, &scstat, 0, sizeof scstat, NULL);
		Clunk9(CLEANRECFID);
	}
}

struct P {
	long val;
	const char *name;
};

static const struct P FLAGNAMES[] = {
	{0x000e, "kColor7"},
	{0x0006, "kColor3"},
	{0x000a, "kColor5"},
	{0x000c, "kColor6"},
	{0x0002, "kColor1"},
	{0x0004, "kColor2"},
	{0x0008, "kColor4"},
	{0x0040, "kIsShared"},
	{0x0080, "kHasNoINITs"},
	{0x0100, "kHasBeenInited"},
	{0x0400, "kHasCustomIcon"},
	{0x0800, "kIsStationery"},
	{0x1000, "kNameLocked"},
	{0x2000, "kHasBundle"},
	{0x4000, "kIsInvisible"},
	{0x8000, "kIsAlias"},
	{0}
};

static int flagsToText(char *buf, const char finfo[16], const char fxinfo[16]) {
	char *base = buf;
	// type and creator code
	memcpy(buf, finfo, 8);
	if (!memcmp(buf, "\0\0\0\0", 4)) {
		memset(buf, '?', 4);
	}
	if (!memcmp(buf+4, "\0\0\0\0", 4)) {
		memset(buf+4, '?', 4);
	}
	buf += 8;
	*buf++ = '\n';

	if (finfo[9] & 0x0e) {
		buf = stpcpy(buf, "kColor!\n");
		buf[-2] = '0' + ((finfo[9] >> 1) & 7);
	}
	if (finfo[9] & 0x40) {
		buf = stpcpy(buf, "kIsShared\n");
	}
	if (finfo[9] & 0x80) {
		buf = stpcpy(buf, "kHasNoINITs\n");
	}
	if (finfo[8] & 0x01) {
		buf = stpcpy(buf, "kHasBeenInited\n");
	}
	if (finfo[8] & 0x02) {
		buf = stpcpy(buf, "aoce-letter\n"); // need a better name
	}
	if (finfo[8] & 0x04) {
		buf = stpcpy(buf, "kHasCustomIcon\n");
	}
	if (finfo[8] & 0x08) {
		buf = stpcpy(buf, "kIsStationery\n");
	}
	if (finfo[8] & 0x10) {
		buf = stpcpy(buf, "kNameLocked\n");
	}
	if (finfo[8] & 0x20) {
		buf = stpcpy(buf, "kHasBundle\n");
	}
	if (finfo[8] & 0x40) {
		buf = stpcpy(buf, "kIsInvisible\n");
	}
	if (finfo[8] & 0x80) {
		buf = stpcpy(buf, "kIsAlias\n");
	}
	return buf - base;
}

static void textToFlags(char finfo[16], char fxinfo[16], const char *text, int len) {
	if (len < 8) {
		return;
	}

	memcpy(finfo, text, 8);
	if (!memcmp(finfo, "????", 4)) {
		memset(finfo, 0, 4);
	}
	if (!memcmp(finfo+4, "????", 4)) {
		memset(finfo+4, 0, 4);
	}

	long flags = 0;
	int i = 9;
	while (i < len) { // for each line
		for (const struct P *p = FLAGNAMES; p->val; p++) { // for each match
			const char *n = p->name;
			int l = 0;
			for (;;) { // for each char
				if (n[l] == 0 && text[i+l] == '\n') {
					flags |= p->val;
					i += l + 1;
					goto nextline;
				} else if (n[l] != text[i+l]) {
					goto nextmatch;
				}
				l++;
			}
		nextmatch:
		}
		i++; // skate over chars until return to matches
	nextline:
	}

	finfo[8] = flags >> 8;
	finfo[9] = flags;
}

static uint32_t fidof(struct MyFCB *fcb) {
	return 32UL + fcb->refNum;
}
