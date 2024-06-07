#include <stdint.h>
#include <string.h>

#include <Errors.h>

#include "9p.h"
#include "fids.h"
#include "metadb-glue.h"
#include "panic.h"
#include "printf.h"
#include "sqlite3.h"
#include "unicode.h"

#include "catalog.h"

enum {
	FIDBROWSE = FIRSTFID_CATALOG,
};

#define MAXDEPTH 16

static bool setPath(int32_t cnid);
static bool appendRelativePath(const unsigned char *path);
static void pathSplitRoot(const unsigned char *path, unsigned char *root, unsigned char *shorter);
static bool isAbs(const unsigned char *path);
static int32_t qid2cnid(struct Qid9 qid);
static struct Qid9 qidTypeFix(struct Qid9 qid, char linuxType);
static void setDB(int32_t cnid, int32_t pcnid, const char *name);

// Single statically allocated array of path components
// UTF-8, null-terminated
// (Final component can be edited safely)
static char *pathComps[MAXDEPTH];
static int32_t expectCNID[MAXDEPTH];
static int pathCompCnt;
static char pathBlob[512];
static int pathBlobSize;

// used in device-9p.c
struct Qid9 root;

int32_t browse(uint32_t fid, int32_t cnid, const unsigned char *paspath) {
	if (paspath == NULL) paspath = ""; // happens a lot

	// Parsing MacOS:paths is tricky at the edges
	// Supposedly, a path is absolute IFF it contains a colon and does not start with one
	// But cnid==1 ("parent of root") also causes the path to be treated as absolute
	if (isAbs(paspath) || cnid == 1 /*"parent of root"*/) {
		// Path is "Macintosh HD:something"
		// Or a special case: "Macintosh HD" or even ":Macintosh HD:something",
		// only if cnid == 1, despite this looking like a relative path.
		// Get this wrong and the Finder can't rename disks.
		setPath(2); // go to root (zero components)
		unsigned char root[256], relative[256];
		pathSplitRoot(paspath, root, relative);
		if (root[0] == 0) return fnfErr;
		if (appendRelativePath(relative)) return bdNamErr;
	} else {
		if (setPath(cnid)) return dirNFErr;
		if (appendRelativePath(paspath)) return bdNamErr;
	}


	printf("Browsing for: /");
	const char *suffix = "/";
	for (int i=0; i<pathCompCnt; i++) {
		printf(i<pathCompCnt-1 ? "%s/" : "%s", pathComps[i]);
	}
	printf("\n");

	// Fast case: root only
	if (pathCompCnt == 0) {
		Walk9(ROOTFID, fid, 0, NULL, NULL, NULL); // dupe shouldn't fail
		return 2;
	}

	struct Qid9 qidarray[100] = {root};
	struct Qid9 *qids = qidarray + 1; // so that root is index -1
	int progress = 0;
	uint32_t tip = ROOTFID; // as soon as a Walk9 succeeds, this equals fid

	while (progress < pathCompCnt) {
		// The aim of a loop iteration is to advance "progress"
		// (an index into pathComps) by up to 16 steps.
		// The complexity is mainly in the error handling (Tolstoyan).

		uint16_t curDepth = progress;
		uint16_t tryDepth = pathCompCnt;
		if (tryDepth > curDepth+16) tryDepth = curDepth+16; // a 9P protocol limitation

		uint16_t numOK = 0;
		Walk9(tip, fid, tryDepth-curDepth, (const char **)pathComps+curDepth, &numOK, qids+curDepth);
		// cast is unfortunate... values won't change while Walk9 is running

		// The call fully succeeded, so fid does indeed point where requested
		// (if only a lesser number of steps succeeded, fid didn't move)
		if (curDepth+numOK == tryDepth) {
			curDepth = tryDepth;
			tip = fid;
		}

		// Some of the inodes might be wrong though: discard these
		int16_t keepDepth = curDepth;

		// Discard components that have the "wrong" CNID
		for (int i=progress; i<keepDepth; i++) {
			if (expectCNID[i] != 0) {
				if (expectCNID[i] != qid2cnid(qids[i])) {
					keepDepth = i;
					break;
				}
			}
		}

		// Point tip to the final correct path member
		if (curDepth > keepDepth) {
			const char *const dotDot[] = {
				"..", "..", "..", "..", "..", "..", "..", "..",
				"..", "..", "..", "..", "..", "..", "..", ".."};

			Walk9(tip, fid, curDepth-keepDepth, dotDot, NULL, NULL);
			tip = fid;
			curDepth = keepDepth;
		} else if (curDepth < keepDepth) {
			if (Walk9(tip, fid, keepDepth-curDepth, (const char **)pathComps+progress, NULL, NULL))
				return fnfErr; // these components worked before... must be a race
			tip = fid;
			curDepth = keepDepth;
		}

		// There has been a lookup failure...
		// Do an exhaustive directory search to resolve it
		if (curDepth < tryDepth) {
			// Are we looking for a name match, or a number match?
			int32_t wantCNID = expectCNID[curDepth];
			const char *wantName = pathComps[curDepth];

			// If there is no chance of the name matching then we might fail here:
			// (lookup is by name and not CNID)
			// AND
			// (fs case insensitive OR no letters in name)
			// AND
			// (fs norm insensitive OR no accents in name)
			// AND
			// (name is not mangled for length)

			// Exhaustive directory listing
			char scratch[4096];
			Walk9(tip, FIDBROWSE, 0, NULL, NULL, NULL); // dupe shouldn't fail
			if (Lopen9(FIDBROWSE, O_RDONLY|O_DIRECTORY, NULL, NULL)) return fnfErr;
			InitReaddir9(FIDBROWSE, scratch, sizeof scratch);

			int err;
			struct Qid9 qid;
			char type;
			char filename[512];
			while ((err=Readdir9(scratch, &qid, &type, filename)) == 0) {
				qid = qidTypeFix(qid, type);

				if (wantCNID) {
					// Check for a number match
					if (qid2cnid(qid) == wantCNID) {
						break;
					}
				} else {
					// Check for a name match
					// TODO: fuzzy filename comparison
				}
			}
			Clunk9(FIDBROWSE);

			if (err != 0) return fnfErr;

			if (Walk9(tip, fid, 1, (const char *[]){filename}, NULL, NULL))
				return fnfErr;
			tip = fid;

			qids[curDepth] = qid;

			curDepth++;
		}

		progress = curDepth;
	}

	// We are about to return a CNID to the caller, which MUST be connected
	// to the root by the hash-table CNID database, otherwise attempts to use it
	// will fnfErr.

	// Build a breadcrumb trail of filenames and QIDs, with the dot-dots removed,
	// so we can clearly see the parent-child relationships:
	const char *nametrail[100];
	struct Qid9 qidtrail[100];
	int ntrail = 0;
	for (int i=0; i<pathCompCnt; i++) {
		if (!strcmp(pathComps[i], "..")) {
			ntrail--;
		} else {
			nametrail[ntrail] = pathComps[i];
			qidtrail[ntrail] = qids[i];
			ntrail++;
		}

		const char *theName = nametrail[ntrail-1];
		int32_t theCNID = qid2cnid(qidtrail[ntrail-1]);
		int32_t parentCNID = (ntrail == 1) ? 2 : qid2cnid(qidtrail[ntrail-2]); // "2" means root

		// If this was a CNID component, then it is already in the database,
		// and possibly with more correct case than we have here
		if (expectCNID[i] == 0) {
			setDB(theCNID, parentCNID, theName);
		}
	}

	return qid2cnid(qids[pathCompCnt-1]);
}

// Erase the global path variables and set them to the known path of a CNID
static bool setPath(int32_t cnid) {
	sqlite3_stmt *S = PERSISTENT_STMT(metadb,
		"WITH RECURSIVE hierarchy (id, parentid, name, level) AS ( "
			"SELECT e.id, e.parentid, e.name, 0 "
			"FROM catalog e "
			"WHERE e.id = ? "

			"UNION ALL "

			"SELECT e.id, e.parentid, e.name, c.level + 1 "
			"FROM catalog e "
			"JOIN hierarchy c ON c.parentid = e.id AND c.parentid != 2 "
		") "
		"SELECT id, name FROM hierarchy ORDER BY level DESC; "
	);
	// This query is a little hefty, perhaps it could be simplified
	// and some complexity moved to C code

	if (sqlite3_bind_int(S, 1, cnid)) panic("bind1");

	pathCompCnt = 0;
	pathBlobSize = 0;

	int sqerr;
	while ((sqerr = sqlite3_step(S)) == SQLITE_ROW) {
		expectCNID[pathCompCnt] = sqlite3_column_int(S, 0);
		pathComps[pathCompCnt] = pathBlob + pathBlobSize; // a string we will populate below...
		pathCompCnt++;

		strcpy(pathBlob + pathBlobSize, sqlite3_column_text(S, 1));
		pathBlobSize += strlen(pathBlob + pathBlobSize) + 1;
	}

	sqlite3_reset(S);
	sqlite3_clear_bindings(S);

	if (sqerr != SQLITE_DONE) panic("bad path query");
	return false;
}

// Append to the global path variables a MacOS-style path
// (consecutive colons will become dot-dot)
// It is okay to append things to the final path component
static bool appendRelativePath(const unsigned char *path) {
	// Divide path components so each is either:
	// [^:]*:
	// [^:]*$
	// So an empty component conveniently corresponds with dot-dot

	const unsigned char *component = path + 1;
	const unsigned char *limit = path + 1 + path[0];

	// Preprocess path: remove leading colon (means "relative path" not dot-dot)
	if (component != limit && component[0] == ':') {
		component++;
	}

	// Component conversion loop
	int len = -1;
	while ((component += len + 1) < limit) {
		len = 0;
		while (component + len < limit && component[len] != ':') len++;

		if (pathCompCnt >= sizeof pathComps/sizeof *pathComps) return true; // oom

		expectCNID[pathCompCnt] = 0;
		pathComps[pathCompCnt] = pathBlob + pathBlobSize;
		pathCompCnt++;

		if (len == 0) {
			strcpy(pathBlob + pathBlobSize, "..");
			pathBlobSize += 3;
		} else {
			for (int i=0; i<len; i++) {
				long bytes = utf8char(component[i]);
				if (bytes == '/') bytes = ':';
				do {
					if (pathBlobSize >= sizeof pathBlob) return true; // oom
					pathBlob[pathBlobSize++] = bytes;
					bytes >>= 8;
				} while (bytes);
			}
			if (pathBlobSize >= sizeof pathBlob) return true; // oom
			pathBlob[pathBlobSize++] = 0;
		}
	}

	return false;
}

static void pathSplitRoot(const unsigned char *path, unsigned char *root, unsigned char *shorter) {
	int strip = 0, rootlen = 0;

	if (path[0] > 0 && path[1] == ':') strip++; // remove a leading colon if any

	while (path[0] > strip+rootlen && path[1+strip+rootlen] != ':') rootlen++; // remove non-colons

	if (root) {
		root[0] = rootlen;
		memcpy(root+1, path+1+strip, rootlen);
	}

	if (shorter) {
		int start = strip + rootlen;
		int len = path[0] - start;
		shorter[0] = len;
		memcpy(shorter+1, path+1+start, len);
	}
}

static bool isAbs(const unsigned char *path) {
	unsigned char *firstColon = memchr(path+1, ':', path[0]);
	return (firstColon != NULL && firstColon != path+1);
}

// a copy, might soon belong to this file
static int32_t qid2cnid(struct Qid9 qid) {
	if (qid.path == root.path) return 2;

	int32_t cnid = 0;
	cnid ^= (0x3fffffffULL & qid.path);
	cnid ^= ((0x0fffffffc0000000ULL & qid.path) >> 30);
	cnid ^= ((0xf000000000000000ULL & qid.path) >> 40); // don't forget the upper 4 bits
	if (cnid < 16) cnid += 0x12342454; // low numbers reserved for system

	if ((qid.type & 0x80) == 0) cnid |= 0x40000000;

	return cnid;
}

static struct Qid9 qidTypeFix(struct Qid9 qid, char linuxType) {
	if (linuxType == 4) {
		qid.type = 0x80;
	} else {
		qid.type = 0;
	}
	return qid;
}

static void setDB(int32_t cnid, int32_t pcnid, const char *name) {
	sqlite3_stmt *S = PERSISTENT_STMT(metadb, "INSERT OR REPLACE INTO catalog (id, parentid, name) VALUES (?, ?, ?);");

	if (sqlite3_bind_int(S, 1, cnid)) panic("bind1");
	if (sqlite3_bind_int(S, 2, pcnid)) panic("bind2");
	if (sqlite3_bind_text(S, 3, name, -1, NULL)) panic("bind3");

	if (sqlite3_step(S) != SQLITE_DONE) panic("step");

	sqlite3_reset(S);
	sqlite3_clear_bindings(S);
}
