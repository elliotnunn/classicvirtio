/* Copyright (c) Elliot Nunn */
/* Licensed under the MIT license */

// Called by device-9p.h
// Calls down to 9p.h, catalog.h and multifork.h

// Background:

// - Mac OS before 8.1 depends somewhat on RelString-ordered return of filenames.
//   - Definitely for Extensions loading order and StandardFile presentation order
// - 9P lists directories in arbitrary order (except on Windows!)
// - Sorting a directory is most efficient when it fits entirely in memory.
// - Some directories are too large to fit in memory.
// - It is not possible to guess a directory's size: you must read the whole thing before you know you're done.
// - We have limited persistent storage (handful KB) but much larger stack storage (hundreds KB).

// What we do to list a directory:

// 1. Read the entire directory into a sorted collection called a "skiplist".
//    As stack space runs out, evict the alphabetically later ones.
//    - This is the meat of populate().
// 2. Compress the sorted directory into a persistent cache.
//    - See the functions with "pack" in the name.
// 3. Progressively unpack the array as file #1, #2 etc are requested via GetFileInfo/GetCatInfo.
// 4. When we run out of packed names, call populate() again as in (1), but with the name
//    of the most recently listed file, so that all files alphabetically earlier than
//    this one are skipped.
// 5. Repeat this until populate() signals that the directory has been fully listed.
// 6. Start again at (1) when a different directory is accessed.

// In practice this seems to be O(n log n), although for very large directories could be O(n^2).

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <Errors.h>
#include <StringCompare.h>

#include "9p.h"
#include "catalog.h"
#include "fids.h"
#include "multifork.h"
#include "panic.h"
#include "printf.h"
#include "unicode.h"

#include "sortdir.h"

enum {
	DIRFID = FIRSTFID_SORTDIR,
	LISTFID,
};

// Insert "new" just to the left of "right" in the skiplist
// Hash is an arbitrary int used to determine how "tall" to insert the element
// (A macro is used to avoid coupling to the exact structure X.
// It just needs to contain this element: struct {struct X *l, *r;} link[MAX];)
#define SKIPLIST_INSERT(right, new, hash) do { \
	int maxlevel = sizeof (new->link) / sizeof (new->link[0]); \
	int d = 0; \
	do { \
		new->link[d].r = right; \
		new->link[d].l = right->link[d].l; \
		right->link[d].l->link[d].r = new; \
		right->link[d].l = new; \
		d++; \
	} while (d<maxlevel && (hash & 1<<d)); \
} while (0)

#define SKIPLIST_DELETE(el) do { \
	int maxlevel = sizeof (el->link) / sizeof (el->link[0]); \
	for (int d=0; d<maxlevel; d++) { \
		el->link[d].l->link[d].r = el->link[d].r; \
		el->link[d].r->link[d].l = el->link[d].l; \
		el->link[d].r = el->link[d].l = NULL; \
	} \
} while (0)

static void populate(const char *ignore, bool dirOK, bool *isComplete);
static struct Qid9 fixQID(struct Qid9 qid, char linuxType);
static void startPacking(void);
static bool pack(int32_t cnid, const char *name); // returns false when no more room
static void startUnpacking(void);
static bool unpack(int32_t *cnid, char *name); // returns false when done

int32_t ReadDirSorted(uint32_t navfid, int32_t pcnid, int16_t index, bool dirOK, char retname[MAXNAME]) {
	static int32_t lastCNID;
	static int16_t lastIndex;
	static bool lastDirOK;
	static bool isComplete;
	static char lastName[MAXNAME];

	if (index <= 0) panic("invalid child index");

	// have we changed directory? in that case, invalidate everything
	if (pcnid != lastCNID || dirOK != lastDirOK) {
		lastCNID = 0;
		lastIndex = 0x7fff;
		lastName[0] = 0;
		startPacking();
		startUnpacking(); // ensures the next unpack() will fail

		// and navigate to the new target directory
		pcnid = CatalogWalk(DIRFID, pcnid, NULL, NULL, NULL);
		if (pcnid == fnfErr) return dirNFErr;
		if (IsErr(pcnid)) return pcnid;
		lastCNID = pcnid;
		lastDirOK = dirOK;
	}

	if (index <= lastIndex) { // backwards enumeration of a directory not supported
		startPacking(); // clear out the cache and relist from beginning
		startUnpacking();
		lastIndex = 0;
		lastName[0] = 0;
		isComplete = false;
	}

	int32_t childCNID = -1;
	while (lastIndex != index) {
		bool ok = unpack(&childCNID, lastName);
		if (!ok) {
			if (isComplete) {
				return fnfErr;
			}
			populate(lastName, dirOK, &isComplete); // make costly FS call when unpack fails
			ok = unpack(&childCNID, lastName);
			if (!ok) {
				return fnfErr; // have fully iterated the directory
			}
		}

		// if this listed name was stale then just skip it
		if (WalkPath9(DIRFID, navfid, lastName) == 0) lastIndex++;
	}
	if (childCNID == -1) panic("impossible");

	if (retname != NULL) strcpy(retname, lastName);
	return childCNID;
}

static void populate(const char *ignore, bool dirOK, bool *isComplete) {
	*isComplete = true;

	// "Leaderboard" of the lexically-lowest children in this directory,
	// kept on the stack as a many-kilobyte skiplist
	enum {POWER = 8};
	struct leader {
		struct {struct leader *l, *r;} link[POWER];
		int32_t cnid;
		char name[MAXNAME];
	} ldboard[1<<POWER] = {};
	int nlead = 0;

	// Special limiting elements
	struct leader leftmost = {}, rightmost = {};
	strcpy(leftmost.name, ignore);
	for (int d=0; d<POWER; d++) {
		leftmost.link[d].r = &rightmost;
		rightmost.link[d].l = &leftmost;
	}

	WalkPath9(DIRFID, LISTFID, "");
	if (Lopen9(LISTFID, O_RDONLY|O_DIRECTORY, NULL, NULL)) panic("failed simple open for readdir");

	// Exhaustively list the host directory
	char rdbuf[100000]; // we have a huge stack, might as well use it
	uint64_t magic = 0;
	uint32_t count = 0;
	while (Readdir9(LISTFID, magic, sizeof rdbuf, &count, rdbuf), count>0) {
		// Iterate over these packed records: "qid[13] offset[8] type[1] name[s]"
		char *ptr = rdbuf;
		while (ptr < rdbuf + count) {
			struct Qid9 qid = {};
			char type = 0;
			char name[MAXNAME] = "";
			unsigned char name31[32] = "";

			DirRecord9(&ptr, &qid, &magic, &type, name);
			int32_t cnid = QID2CNID(fixQID(qid, type));
			mr31name(name31, name);

			if (!dirOK && type == 4) goto skipFile; // been asked not to return directories
			if (name31[0] == 0) goto skipFile; // unrepresentable name
			if (name[0] == '.' || MF.IsSidecar(name)) goto skipFile; // . or .. or some other hidden metadata file

			// search the skiplist for where to insert
			struct leader *right = &rightmost;
			for (int d=POWER-1; d>=0; d--) {
				for (;;) {
					struct leader *stepleft = right->link[d].l;
					unsigned char try31[32] = "";
					mr31name(try31, stepleft->name);
					if (RelString(name31, try31, true, true) > 0) break;
					right = stepleft;
					if (right == &leftmost) goto skipFile;
				}
			}

			if (nlead < sizeof ldboard/sizeof *ldboard) { // empty slots available, use one
				struct leader *el = &ldboard[nlead++];
				el->cnid = cnid;
				strcpy(el->name, name);
				SKIPLIST_INSERT(right, el, el->cnid);
				goto skipFile;
			}

			if (right == &rightmost) { // list full, is lexically later than all items, discard
				*isComplete = false;
				goto skipFile;
			}

			struct leader *el = rightmost.link[0].l; // steal the slot of the lexically latest item
			el->cnid = cnid;
			strcpy(el->name, name);
			if (el == right) { // straight replacement (quite rare case)
				goto skipFile;
			}

			SKIPLIST_DELETE(el);
			SKIPLIST_INSERT(right, el, el->cnid);
		skipFile:;
		}
	}
	Clunk9(LISTFID);

	if (0) {
		printf("dumping leaderboard skiplist:\n");
		for (struct leader *el=leftmost.link[0].r; el!=&rightmost; el=el->link[0].r) {
			for (int i=0; i<POWER; i++) printf(el->link[i].l ? "O" : "|");
			printf(" cnid=%#x name=\"%s\"\n", el->cnid, el->name);
		}
	}

	startPacking();
	for (struct leader *el=leftmost.link[0].r; el!=&rightmost; el=el->link[0].r) {
		if (!pack(el->cnid, el->name)) {
			*isComplete = false;
			break;
		}
	}
	startUnpacking();
}

static struct Qid9 fixQID(struct Qid9 qid, char linuxType) {
	if (linuxType == 4) {
		qid.type = 0x80;
	} else {
		qid.type = 0;
	}
	return qid;
}

static char packed[2048];
static int packedSize, packedPtr;
static char packedLastName[MAXNAME];
static int32_t packedLastID;

// cycle is startPacking, [pack...], startUnpacking, [unpack...]
static void startPacking(void) {
	packedSize = packedLastName[0] = packedLastID = 0;
}

static bool pack(int32_t cnid, const char *name) {
	int reuseID = 0, reuseName = 0;

	// determine common prefixes
	while (((char *)&packedLastID)[reuseID] == ((char *)&cnid)[reuseID] && reuseID < 3) reuseID++;
	while (packedLastName[reuseName] == name[reuseName] && reuseName < 0x3f) reuseName++;

	int changeID = 4 - reuseID;
	int changeName = strlen(name) + 1 - reuseName;

	packedLastID = cnid;
	memcpy(packedLastName + reuseName, name + reuseName, changeName);

	if (packedSize + 1 + changeID + changeName > sizeof packed) return false; // out of room

	packed[packedSize++] = reuseID<<6 | reuseName;
	memcpy(packed + packedSize, (char *)&cnid + reuseID, changeID);
	packedSize += changeID;
	memcpy(packed + packedSize, name + reuseName, changeName);
	packedSize += changeName;
	return true;
}

static void startUnpacking(void) {
	packedPtr = packedLastName[0] = packedLastID = 0;
}

static bool unpack(int32_t *cnid, char *name) {
	if (packedPtr >= packedSize) return false;

	int reuseID = (0xc0 & packed[packedPtr]) >> 6;
	int reuseName = 0x3f & packed[packedPtr];
	packedPtr++;

	int changeID = 4 - reuseID;
	memcpy((char *)&packedLastID + reuseID, packed + packedPtr, changeID);
	packedPtr += changeID;

	int changeName = strlen(packed + packedPtr) + 1;
	memcpy(packedLastName + reuseName, packed + packedPtr, changeName);
	packedPtr += changeName;

	if (cnid != NULL) *cnid = packedLastID;
	if (name != NULL) memcpy(name, packedLastName, reuseName + changeName);
	return true;
}
