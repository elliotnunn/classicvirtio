/* Copyright (c) 2023 Elliot Nunn */
/* Licensed under the MIT license */

/*
Mac OS expects to be able to keep track of files by 32-bit "catalog node IDs",
but the rest of the world requires a textual path. Bridge this gap.

This database is accessed intensively but can also grow arbitrarily large,
so we devote some complexity to spilling from RAM to disk when needed.

There is a tiny bit of trickiness about files that get their name-cases changed!
*/

#include <stdint.h>
#include <string.h>

#include <Errors.h>

#include "9p.h"
#include "fids.h"
#include "panic.h"
#include "printf.h"
#include "unicode.h"

#include "catalog.h"

enum {
	CATALOGFID = FIRSTFID_CATALOG,
	TMPFID,

	// tunable:
	BUCKETS = 32,
	BUCKETSLOTS = 32,
	BUCKETBYTES = 300,
};

struct slot {
	int32_t cnid;
	int32_t parent;
	bool dirty; // can't discard without saving to disk
	uint16_t offset;
};

struct bucket {
	struct slot slots[BUCKETSLOTS];
	uint16_t usedSlots, usedBytes;
	char names[BUCKETBYTES];
};

static bool isAbsolute(int32_t cnid, const unsigned char *path);
static int bubbleUp(int bucket, int slot);
static int spill(int bucket);
static int unspill(int bucket, int32_t cnid);
static int whichBucket(int32_t cnid);
static int whichSlot(int bucket, uint32_t cnid);
static char *slotName(int bucket, int slot);
static void deleteSlotName(int bucket, int slot);
static bool ciEqual(const char *a, const char *b);

static struct bucket cache[BUCKETS];
static struct Qid9 rootQID;
static char *lastSetName;

void CatalogInit(struct Qid9 root) {
	int err = Mkdir9(DOTDIRFID, 0777, 0, "catalog", NULL);
	if (err && err!=EEXIST)
		panic("failed create /catalog");

	if (WalkPath9(DOTDIRFID, CATALOGFID, "catalog"))
		panic("failed walk /catalog");

	rootQID = root;
}

// Only dumps the RAM part of the catalog
void CatalogDump(void) {
	for (int bucket=0; bucket<BUCKETS; bucket++) {
		printf("% 3d: ", bucket);
		for (int i=0; i<cache[bucket].usedBytes; i++) {
			if (cache[bucket].names[i] == 0) {
				printf(".");
			} else {
				printf("%c", cache[bucket].names[i]);
			}
		}
		printf("\n");
		for (int slot=0; slot<cache[bucket].usedSlots; slot++) {
			printf("    %08x: (p=%08x, n=\"%s\", dirty=%d)\n",
				cache[bucket].slots[slot].cnid,
				cache[bucket].slots[slot].parent,
				cache[bucket].names + cache[bucket].slots[slot].offset,
				cache[bucket].slots[slot].dirty);
		}
	}
}

// The CNID arg must be a directory, unless an absolute path causes it to be ignored.
// On success return a CNID. On failure, returns bdNamErr/dirNFErr/fnfErr.
// These can be distinguished using IsErr().
int32_t CatalogWalk(uint32_t fid, int32_t cnid, const unsigned char *paspath, int32_t *retparent, char *retname) {
 	if (paspath == NULL) paspath = (const unsigned char *)""; // happens all the time
	const char *p = (const char *)paspath + 1;
	const char *pend = (const char *)paspath + 1 + paspath[0];

	printf("       CatalogWalk(%08x, \"%.*s\")\n", cnid, *paspath, paspath+1);
	if (retname != NULL) retname[0] = 0; // assume failure
	if (retparent != NULL) *retparent = 0; // assume failure

	char scratch[512]; // enough bytes in the path?
	char *el[32]; // surely that's deep enough
	int nbyte = 0, nel = 0;

	if (isAbsolute(cnid, paspath)) { // absolute path, strip disk name (it's ours)
		if (p<pend && *p==':') p++; // one leading colon can be ignored
		if (p==pend || *p==':') return fnfErr; // then text is absolutely mandatory
		while (p<pend && *p!=':') p++;
	} else { // relative path, convert the supplied CNID to a path using the database
		if (!IsDir(cnid)) {
			return fnfErr;
		}
		for (int32_t trail=cnid; trail!=2/*special "root" CNID*/;) {
			if (nbyte+MAXNAME > sizeof scratch || nel == sizeof el/sizeof *el) {
				return bdNamErr; // path too long
			}
			memmove(el+1, el, nel*sizeof *el); // yes this is O(n^2)
			el[0] = scratch + nbyte;
			nel++;
			trail = CatalogGet(trail, scratch + nbyte);
			if (IsErr(trail)) {
				return fnfErr;
			}
			nbyte += strlen(scratch + nbyte) + 1;
		}
	}
	int nelByID = nel; // portion of path components found by ID

	if (p<pend && *p==':') p++; // remove up to 1 leading colon

	while (p<pend) {
		if (*p != ':') { // process 1 textual component
			if (nel == sizeof el/sizeof *el) {
				return bdNamErr; // too many components
			}
			el[nel++] = scratch + nbyte;
			while (p<pend && *p!=':') {
				long uc = utf8char(*p++);
				if (uc == '/') uc = ':';
				do {
					if (nbyte == sizeof scratch) {
						return bdNamErr; // path too long
					}
					scratch[nbyte++] = uc & 0xff;
					uc >>= 8;
				} while (uc != 0);
			}
			scratch[nbyte++] = 0;
		}

		if (p<pend && *p==':') { // expect 1 following colon
			p++;
		}

		while (p<pend && *p==':') { // but more means dot-dot
			if (nel == sizeof el/sizeof *el) {
				return bdNamErr; // too many components
			}
			el[nel++] = "..";
			p++;
		}
	}

	struct Qid9 qids[sizeof el/sizeof *el];
	uint16_t got = 0;
	Walk9(ROOTFID, fid, nel, (const char *const *)el, &got, qids);

	for (int i=0; i<got-1; i++) { // Not allowed to ".." from a file
		if ((qids[i].type&0x80) == 0) {
			return dirNFErr;
		}
	}

	if (got == nel-1) {
		return fnfErr;
	} else if (got < nel) {
		return dirNFErr;
	} else if (nelByID>0 && cnid!=QID2CNID(qids[nelByID-1])) {
		return fnfErr; // a new file has been moved into place, DB out of date!
	}

	// Fold dot-dots in the element list to ensure the DB
	// connects the return CNID to the root, or it will be useless.
	// (If there are dot-dots then the element list will be shortened.)
	lastSetName = NULL;
	int nelTotal = nel;
	nel = nelByID; // rewind
	for (int i=nelByID; i<nelTotal; i++) {
		if (!strcmp(el[i], "..")) {
			nel--;
		} else {
			qids[nel] = qids[i];
			el[nel] = el[i];
			nel++;

			CatalogSet(QID2CNID(qids[nel-1]), nel>=2 ? QID2CNID(qids[nel-2]) : 2, el[nel-1], false);
		}
	}

	// retname/retparent are optimisations to reduce subsequent costly CatalogGet calls.
	if (retname != NULL) {
		if (lastSetName != NULL) {
			strcpy(retname, lastSetName); // lastSetName = the definitive-case name from CatalogSet
		} else if (nel > 0) {
			strcpy(retname, el[nel-1]);
		} else {
			CatalogGet(2, retname); // name of disk
		}
		printf("        name = %s\n", retname);
	}

	if (retparent != NULL) {
		if (nel == 0) {
			*retparent = 1; // "parent of root"
		} else if (nel == 1) {
			*retparent = 2; // "root"
		} else {
			*retparent = QID2CNID(qids[nel-2]);
		}
		printf("        parent = %08x\n", *retparent);
	}

	if (got > 0) {
		cnid = QID2CNID(qids[got-1]);
	} else {
		cnid = 2; // root
	}
		printf("        cnid = %08x\n", cnid);

	printf("        path = ");
	for (int i=0; i<nel; i++) {
		printf("/%s", el[i]);
	}
	printf("\n");

	return cnid;
}

// Hash a 31-bit CNID from a 64-bit 9P QID (approximately an inode number).
// Negative CNIDs are reserved for MacOS error numbers,
// and the 0x40000000 bit means "not a dir".
// Warning: the "type" field of a Rreaddir QID is nonsense, causing this
// function to give a garbage result, so qidTypeFix() it before calling me.
int32_t QID2CNID(struct Qid9 qid) {
	if (qid.path == rootQID.path) return 2;

	int32_t cnid = 0;
	cnid ^= (0x3fffffffULL & qid.path);
	cnid ^= ((0x0fffffffc0000000ULL & qid.path) >> 30);
	cnid ^= ((0xf000000000000000ULL & qid.path) >> 40); // don't forget the upper 4 bits
	if (cnid < 16) cnid += 0x12342454; // low numbers reserved for system

	if ((qid.type & 0x80) == 0) cnid |= 0x40000000;

	return cnid;
}

bool IsErr(int32_t cnid) {
	return cnid < 0;
}

bool IsDir(int32_t cnid) {
	return (cnid & 0x40000000) == 0;
}

// Documented definition of an absolute path:
//     *contains a colon but does not start with a colon*
// However, if the supplied CNID is 1 then it is treated as absolute too.
// (Get this wrong and the Finder can't rename disks.)
static bool isAbsolute(int32_t cnid, const unsigned char *path) {
	if (cnid == 1) {
		return true;
	}
	unsigned char *firstColon = memchr(path+1, ':', path[0]);
	return (firstColon != NULL && firstColon != path+1);
}

// "definitive" means "I am sure about the case"
void CatalogSet(int32_t cnid, int32_t pcnid, const char *name, bool nameDefinitive) {
	int bucket = whichBucket(cnid);
	int slot = whichSlot(bucket, cnid);
	int namelen = strlen(name) + 1;

	if (slot < 0) {
		// New slot (evict as many as we need to)
		if (cache[bucket].usedSlots == BUCKETSLOTS) {
			spill(bucket);
		}
		while (cache[bucket].usedBytes + namelen > BUCKETBYTES) {
			spill(bucket);
		}

		slot = cache[bucket].usedSlots++;
		cache[bucket].slots[slot] = (struct slot){
			.cnid = cnid,
			.parent = pcnid,
			.dirty = true,
			.offset = cache[bucket].usedBytes,
		};
		memcpy(slotName(bucket, slot), name, namelen);
		cache[bucket].usedBytes += namelen;
	} else {
		// correct an existing entry (happens a lot because GetCatInfo is a pain)
		if (cache[bucket].slots[slot].parent != pcnid) {
			cache[bucket].slots[slot].parent = pcnid;
			cache[bucket].slots[slot].dirty = true;
		}

		int oldnamelen = strlen(slotName(bucket, slot)) + 1;
		if (namelen == oldnamelen) {
			// Name length hasn't changed, so overwrite in place
			// (unless the change is only to capitalisation)
			if (nameDefinitive || !ciEqual(slotName(bucket, slot), name)) {
				memcpy(slotName(bucket, slot), name, namelen);
			}
		} else {
			// Name length has changed -- delete slots until we can fit the new name
			deleteSlotName(bucket, slot);
			while (cache[bucket].usedBytes + namelen > BUCKETBYTES) {
				// trick... don't spill the one we're trying to expand!
				if (slot == cache[bucket].usedSlots - 1) {
					slot = bubbleUp(bucket, slot);
				}
				spill(bucket);
			}
			cache[bucket].slots[slot].offset = cache[bucket].usedBytes;
			memcpy(slotName(bucket, slot), name, namelen);
			cache[bucket].usedBytes += namelen;
		}
	}
	lastSetName = slotName(bucket, slot); // hack to reduce CatalogGet calls
}

int32_t CatalogGet(int32_t cnid, char *retname) {
	int bucket = whichBucket(cnid);
	int slot = whichSlot(bucket, cnid);

	if (slot < 0) {
		slot = unspill(bucket, cnid);
	}

	if (slot < 0) {
		if (retname != NULL) {
			retname[0] = 0;
		}
		return fnfErr;
	}

	slot = bubbleUp(bucket, slot);
	if (retname != NULL) {
		strcpy(retname, slotName(bucket, slot));
	}
	return cache[bucket].slots[slot].parent;
}

static int bubbleUp(int bucket, int slot) {
	if (slot == 0) {
		return 0;
	}

	struct slot tmp = cache[bucket].slots[slot];
	cache[bucket].slots[slot] = cache[bucket].slots[slot-1];
	cache[bucket].slots[slot-1] = tmp;

	return slot - 1;
}

// Return the slot number that has been freed
static int spill(int bucket) {
	int killSlot = cache[bucket].usedSlots - 1;
	char *name = slotName(bucket, killSlot);
	int len = strlen(name) + 1;

	// ephemeral file, "quick and dirty" format
	if (cache[bucket].slots[killSlot].dirty) {
		char spillFile[9];
		sprintf(spillFile, "%08x", cache[bucket].slots[killSlot].cnid);
		WalkPath9(CATALOGFID, TMPFID, "");
		if (Lcreate9(TMPFID, O_WRONLY|O_TRUNC, 0666, 0, spillFile, NULL, NULL))
			panic("failed create catalog ent");
		if (Write9(TMPFID, &cache[bucket].slots[killSlot].parent, 0, 4, NULL))
			panic("failed write catalog ent parent");
		if (Write9(TMPFID, name, 4, len, NULL))
			panic("failed write catalog ent name");
		Clunk9(TMPFID);
	}

	deleteSlotName(bucket, killSlot);
	cache[bucket].usedSlots--;
	return killSlot;
}

// Return the slot number it has been retrieved to
static int unspill(int bucket, int32_t cnid) {
	struct fileFormat {
		int32_t parent;
		char name[128];
	};
	struct fileFormat tmp;
	char spillFile[9];
	sprintf(spillFile, "%08x", cnid);

	WalkPath9(CATALOGFID, TMPFID, spillFile);
	if (Lopen9(TMPFID, O_RDONLY, NULL, NULL))
		return -1; // invalid CNIDs don't necessitate panic
	uint32_t got = 0;
	Read9(TMPFID, &tmp, 0, sizeof tmp, &got);
	if (got == 0)
		panic("failed read catalog ent hex");
	Clunk9(TMPFID);

	int namelen = got - 4;

	// Evict enough other files to fit this one
	if (cache[bucket].usedSlots == BUCKETSLOTS) {
		spill(bucket);
	}
	while (cache[bucket].usedBytes + namelen > BUCKETBYTES) {
		spill(bucket);
	}

	cache[bucket].slots[cache[bucket].usedSlots++] = (struct slot){
		.cnid = cnid,
		.parent = tmp.parent,
		.dirty = false,
		.offset = cache[bucket].usedBytes,
	};
	memcpy(cache[bucket].names + cache[bucket].usedBytes, tmp.name, namelen);
	cache[bucket].usedBytes += namelen;
	return cache[bucket].usedSlots - 1;
}

static int whichBucket(int32_t cnid) {
	return cnid & (BUCKETS - 1);
}

static int whichSlot(int bucket, uint32_t cnid) {
	for (int i=0; i<cache[bucket].usedSlots; i++) {
		if (cache[bucket].slots[i].cnid == cnid) {
			return i;
		}
	}
	return -1;
}

static char *slotName(int bucket, int slot) {
	return cache[bucket].names + cache[bucket].slots[slot].offset;
}

// compact all the fellow slot names on the left
// needs to be immediately followed by a repopulation of the slot name, or there will be corruption
static void deleteSlotName(int bucket, int slot) {
	char *names = cache[bucket].names;
	int deleteAt = cache[bucket].slots[slot].offset;
	int deleteLen = strlen(names + deleteAt) + 1;
	memmove(names+deleteAt, names+deleteAt+deleteLen, cache[bucket].usedBytes-deleteAt-deleteLen);
	for (int i=0; i<cache[bucket].usedSlots; i++) {
		if (cache[bucket].slots[i].offset > deleteAt) {
			cache[bucket].slots[i].offset -= deleteLen;
		}
	}
	cache[bucket].slots[slot].offset = 0;
	cache[bucket].usedBytes -= deleteLen;
}

// ASCII case-insens compare, happens to work for the Roman-ish letters in decomposed UTF-8
static bool ciEqual(const char *a, const char *b) {
	for (;;) {
		char ac = (*a>='a' && *a<='z') ? *a+'A'-'a' : *a;
		char bc = (*b>='a' && *b<='z') ? *b+'A'-'a' : *b;
		if (ac != bc) return false;
		if (ac == 0) return true;
		a++;
		b++;
	}
}
