/* Copyright (c) 2023 Elliot Nunn */
/* Licensed under the MIT license */

/*
Enough of the 9P2000.L protocol to support the Mac OS File Manager

Why the dot-L variant?

TODO:
- Reentrancy (for Virtual Memory, not for the single-threaded File Manager)
- Yield back to the File Manager while idle (rather than spinning)
- Range-based IO (rather than a virtio buffer for each page)
*/

#include <DriverServices.h>

#include <stdalign.h>
#include <stdarg.h>
#include <string.h>

#include "allocator.h"
#include "printf.h"
#include "panic.h"
#include "virtqueue.h"

#include "9p.h"

enum {
	NOTAG = 0,
	ONLYTAG = 1,
	STRMAX = 127, // not including the null
};

#define READ16LE(S) ((255 & ((char *)S)[1]) << 8 | (255 & ((char *)S)[0]))
#define READ32LE(S) \
  ((uint32_t)(255 & ((char *)S)[3]) << 24 | (uint32_t)(255 & ((char *)S)[2]) << 16 | \
   (uint32_t)(255 & ((char *)S)[1]) << 8 | (uint32_t)(255 & ((char *)S)[0]))
#define READ64LE(S)                                                    \
  ((uint64_t)(255 & ((char *)S)[7]) << 070 | (uint64_t)(255 & ((char *)S)[6]) << 060 | \
   (uint64_t)(255 & ((char *)S)[5]) << 050 | (uint64_t)(255 & ((char *)S)[4]) << 040 | \
   (uint64_t)(255 & ((char *)S)[3]) << 030 | (uint64_t)(255 & ((char *)S)[2]) << 020 | \
   (uint64_t)(255 & ((char *)S)[1]) << 010 | (uint64_t)(255 & ((char *)S)[0]) << 000)
#define WRITE16LE(P, V)                        \
  (((char *)P)[0] = (0x000000FF & (V)), \
   ((char *)P)[1] = (0x0000FF00 & (V)) >> 8, ((char *)P) + 2)
#define WRITE32LE(P, V)                        \
  (((char *)P)[0] = (0x000000FF & (V)), \
   ((char *)P)[1] = (0x0000FF00 & (V)) >> 8, \
   ((char *)P)[2] = (0x00FF0000 & (V)) >> 16, \
   ((char *)P)[3] = (0xFF000000 & (V)) >> 24, ((char *)P) + 4)
#define WRITE64LE(P, V)                        \
  (((char *)P)[0] = (0x00000000000000FF & (V)) >> 000, \
   ((char *)P)[1] = (0x000000000000FF00 & (V)) >> 010, \
   ((char *)P)[2] = (0x0000000000FF0000 & (V)) >> 020, \
   ((char *)P)[3] = (0x00000000FF000000 & (V)) >> 030, \
   ((char *)P)[4] = (0x000000FF00000000 & (V)) >> 040, \
   ((char *)P)[5] = (0x0000FF0000000000 & (V)) >> 050, \
   ((char *)P)[6] = (0x00FF000000000000 & (V)) >> 060, \
   ((char *)P)[7] = (0xFF00000000000000 & (V)) >> 070, ((char *)P) + 8)

uint32_t Max9;

static uint32_t openfids;

static volatile bool flag;

static void **physicals; // newptr allocated block

int bufcnt;

#define QIDF "0x%02x.%x.%x"
#define QIDA(qid) qid.type, qid.version, (uint32_t)qid.path
#define READQID(ptr) (struct Qid9){*(char *)(ptr), READ32LE((char *)(ptr)+1), READ64LE((char *)(ptr)+5)}

static int transact(uint8_t cmd, const char *tfmt, const char *rfmt, ...);

int Init9(int bufs) {
	enum {Tversion = 100}; // size[4] Tversion tag[2] msize[4] version[s]
	enum {Rversion = 101}; // size[4] Rversion tag[2] msize[4] version[s]

	if (bufs > 256) bufs = 256;
	bufcnt = bufs;

	Max9 = 4096 * (bufs - 4);

	int err;
	char proto[128];
	err = transact(Tversion, "ds", "ds",
		Max9, "9P2000.L",
		&Max9, proto);
	if (err) return err;

	if (strcmp(proto, "9P2000.L")) {
		return EPROTONOSUPPORT;
	}

	return 0;
}

int Attach9(uint32_t fid, uint32_t afid, const char *uname, const char *aname, uint32_t n_uname, struct Qid9 *retqid) {
	enum {Tattach = 104}; // size[4] Tattach tag[2] fid[4] afid[4] uname[s] aname[s] n_uname[4]
	enum {Rattach = 105}; // size[4] Rattach tag[2] qid[13]

	return transact(Tattach, "ddssd", "Q",
		fid, afid, uname, aname, n_uname,
		retqid);
}

int Statfs9(uint32_t fid, struct Statfs9 *ret) {
	enum {Tstatfs = 8}; // size[4] Tstatfs tag[2] fid[4]
	enum {Rstatfs = 9}; // size[4] Rstatfs tag[2] type[4] bsize[4] blocks[8] bfree[8]
	                    // bavail[8] files[8] ffree[8] fsid[8] namelen[4]

	return transact(Tstatfs, "d", "ddqqqqqqd",
		fid,
		&ret->type, &ret->bsize, &ret->blocks, &ret->bfree,
		&ret->bavail, &ret->files, &ret->ffree, &ret->fsid, &ret->namelen);
}

// Respects the protocol's 16-component maximum
// call with nwname 0 to duplicate a fid
int Walk9(uint32_t fid, uint32_t newfid, uint16_t nwname, const char *const *name, uint16_t *retnwqid, struct Qid9 *retqid) {
	enum {Twalk = 110}; // size[4] Twalk tag[2] fid[4] newfid[4] nwname[2] nwname*(wname[s])
	enum {Rwalk = 111}; // size[4] Rwalk tag[2] nwqid[2] nwqid*(wqid[13])

	if (newfid < 32 && fid != newfid && (openfids & (1<<newfid))) Clunk9(newfid);

	if (retnwqid) *retnwqid = 0;

	int done = 0;
	do {
		char path[1024];
		int willdo = 0, pathbytes = 0;

		// Pack the names into a buffer, and increment willdo
		while (done+willdo < nwname && willdo < 16) {
			int slen = strlen(name[done+willdo]);

			// buffer getting too big for us?
			if (pathbytes+2+slen >= sizeof path) break;

			WRITE16LE(path+pathbytes, slen);
			memcpy(path+pathbytes+2, name[done+willdo], slen);

			pathbytes += 2+slen;
			willdo++;
		}

		// Failed to pack even one name into the buffer?
		// (except for the nwname 0 case, to duplicate a fid)
		if (willdo == 0 && nwname != 0) return ENOMEM;

		char qids[16*13];
		uint16_t ok = 0;

		int err = transact(Twalk, "ddwB", "wB",
			fid, newfid, willdo, path, pathbytes,
			&ok, qids, sizeof qids);

		if (err) return err;

		if (retnwqid) *retnwqid += ok;
		if (retqid) {
			for (int i=0; i<ok; i++) {
				char *rawqid = qids + 13*i;
				retqid[done+i] =
					(struct Qid9){*rawqid, READ32LE(rawqid+1), READ64LE(rawqid+5)};
			}
		}

		done += ok;

		if (ok < willdo) return ENOENT;
	} while (done < nwname);

	if (newfid < 32) openfids |= 1<<newfid;

	return 0;
}

// Panics if you exceed the maximum 16 components
// Returns 0 if any components of the walk fail (for easy error checking)
int WalkPath9(uint32_t fid, uint32_t newfid, const char *path) {
	enum {Twalk = 110}; // size[4] Twalk tag[2] fid[4] newfid[4] nwname[2] nwname*(wname[s])
	enum {Rwalk = 111}; // size[4] Rwalk tag[2] nwqid[2] nwqid*(wqid[13])

	if (newfid < 32 && fid != newfid && (openfids & (1<<newfid))) Clunk9(newfid);

	const char *lookhere = path;
	char packed[1024];
	char *packhere = packed;
	int components = 0;

	for (;;) {
		int len = 0;
		while (lookhere[len]!=0 && lookhere[len]!='/') len++;

		if (len > 0) {
			if (packhere + 2 + len > packed + sizeof packed) {
				panic("WalkPath9 too many characters");
			}
			if (components == 16) {
				panic("WalkPath9 too many components");
			}

			packhere[0] = len;
			packhere[1] = len >> 8;
			memcpy(packhere+2, lookhere, len);
			packhere += 2 + len;

			components++;
		}

		if (lookhere[len] == 0) break;
		lookhere += len + 1;
	}

	uint16_t ok = 0;
	char qids[16*13];

	int err = transact(Twalk, "ddwB", "wB",
		fid, newfid, components, packed, packhere - packed,
		&ok, qids, sizeof qids);

	if (err && components==0) {
		panic("Twalk with 0 components should never fail");
	}

	if (err) return err;
	if (ok != components) return ENOENT;

	if (newfid < 32) openfids |= 1<<newfid;
	return 0;
}

int Lopen9(uint32_t fid, uint32_t flags, struct Qid9 *retqid, uint32_t *retiounit) {
	enum {Tlopen = 12}; // size[4] Tlopen tag[2] fid[4] flags[4]
	enum {Rlopen = 13}; // size[4] Rlopen tag[2] qid[13] iounit[4]

	return transact(Tlopen, "dd", "Qd",
		fid, flags,
		retqid, retiounit);
}

int Lcreate9(uint32_t fid, uint32_t flags, uint32_t mode, uint32_t gid, const char *name, struct Qid9 *retqid, uint32_t *retiounit) {
	enum {Tlcreate = 14}; // size[4] Tlcreate tag[2] fid[4] name[s] flags[4] mode[4] gid[4]
	enum {Rlcreate = 15}; // size[4] Rlcreate tag[2] qid[13] iounit[4]

	return transact(Tlcreate, "dsddd", "Qd",
		fid, name, flags, mode, gid,
		retqid, retiounit);
}

int Xattrwalk9(uint32_t fid, uint32_t newfid, const char *name, uint64_t *retsize) {
	enum {Txattrwalk = 30}; // size[4] Txattrwalk tag[2] fid[4] newfid[4] name[s]
	enum {Rxattrwalk = 31}; // size[4] Rxattrwalk tag[2] size[8]

	if (newfid < 32 && fid != newfid && (openfids & (1<<newfid))) Clunk9(newfid);

	int err = transact(Txattrwalk, "dds", "q",
		fid, newfid, name,
		retsize);
	if (err) return err;

	if (newfid < 32) openfids |= 1<<newfid;
	return 0;
}

int Xattrcreate9(uint32_t fid, const char *name, uint64_t size, uint32_t flags) {
	enum {Txattrcreate = 32}; // size[4] Txattrcreate tag[2] fid[4] name[s] attr_size[8] flags[4]
	enum {Rxattrcreate = 33}; // size[4] Rxattrcreate tag[2]

	return transact(Txattrcreate, "dsqd", "",
		fid, name, size, flags);
}

int Remove9(uint32_t fid) {
	enum {Tremove = 122}; // size[4] Tremove tag[2] fid[4]
	enum {Rremove = 123}; // size[4] Rremove tag[2]

	return transact(Tremove, "d", "",
		fid);
}

int Unlinkat9(uint32_t fid, const char *name, uint32_t flags) {
	enum {Tunlinkat = 76}; // size[4] Tunlinkat tag[2] dirfd[4] name[s] flags[4]
	enum {Runlinkat = 77}; // size[4] Runlinkat tag[2]
	// only flag is AT_REMOVEDIR = 0x200

	return transact(Tunlinkat, "dsd", "",
		fid, name, flags);
}

int Renameat9(uint32_t olddirfid, const char *oldname, uint32_t newdirfid, const char *newname) {
	enum {Trenameat = 74}; // size[4] Trenameat tag[2] olddirfid[4] oldname[s] newdirfid[4] newname[s]
	enum {Rrenameat = 75}; // size[4] Rrenameat tag[2]

	return transact(Trenameat, "dsds", "",
		olddirfid, oldname, newdirfid, newname);
}

int Mkdir9(uint32_t dfid, uint32_t mode, uint32_t gid, const char *name, struct Qid9 *retqid) {
	enum {Tmkdir = 72}; // size[4] Tmkdir tag[2] dfid[4] name[s] mode[4] gid[4]
	enum {Rmkdir = 73}; // size[4] Rmkdir tag[2] qid[13]

	return transact(Tmkdir, "dsdd", "Q",
		dfid, name, mode, gid,
		retqid);
}

struct rdbuf {
	uint32_t fid;
	uint64_t nextRequest;
	uint32_t size, recvd, used;
	char data[];
};

#define RDBUFALIGN(voidptr) ((struct rdbuf *) \
		(((uintptr_t)buf + alignof (struct rdbuf) - 1) & -alignof (struct rdbuf)))

void InitReaddir9(uint32_t fid, void *buf, size_t bufsize) {
	struct rdbuf *rdbuf = RDBUFALIGN(buf);

	rdbuf->fid = fid;
	rdbuf->nextRequest = 0;
	rdbuf->size = (char *)buf + bufsize - rdbuf->data;
	rdbuf->recvd = 0;
	rdbuf->used = 0;
}

// 0 = ok, negative = eof, positive = linux errno
int Readdir9(void *buf, struct Qid9 *retqid, char *rettype, char retname[MAXNAME]) {
	enum {Treaddir = 40}; // size[4] Treaddir tag[2] fid[4] offset[8] count[4]
	enum {Rreaddir = 41}; // size[4] Rreaddir tag[2] count[4] data[count]
	                      // "data" = qid[13] offset[8] type[1] name[s]

	struct rdbuf *rdbuf = RDBUFALIGN(buf);

	if (rdbuf->used >= rdbuf->recvd) {
		int err = transact(Treaddir, "dqd", "dB",
			rdbuf->fid, rdbuf->nextRequest, rdbuf->size,
			&rdbuf->recvd, rdbuf->data, rdbuf->size);

		if (err) return err;

		rdbuf->used = 0;

		if (rdbuf->recvd == 0) return -1;
	}

	// qid field at +0
	if (retqid) {
		*retqid = READQID(rdbuf->data + rdbuf->used);
	}

	// offset field at +13
	rdbuf->nextRequest = READ64LE(rdbuf->data + rdbuf->used + 13);

	// type field at +21
	if (rettype) {
		*rettype = *(rdbuf->data + rdbuf->used + 21);
	}

	// name field at +22
	uint16_t nlen = READ16LE(rdbuf->data + rdbuf->used + 22);

	if (retname) {
		uint16_t copylen = nlen;
		if (copylen > 511) copylen = 511;
		memcpy(retname, rdbuf->data + rdbuf->used + 24, copylen);
		retname[copylen] = 0;
	}

	rdbuf->used += 24 + nlen;

	return 0;
}

int Getattr9(uint32_t fid, uint64_t request_mask, struct Stat9 *ret) {
	enum {Tgetattr = 24}; // size[4] Tgetattr tag[2] fid[4] request_mask[8]
	enum {Rgetattr = 25}; // size[4] Rgetattr tag[2] valid[8] qid[13]
	                      // mode[4] uid[4] gid[4] nlink[8] rdev[8]
	                      // size[8] blksize[8] blocks[8] atime_sec[8]
	                      // atime_nsec[8] mtime_sec[8] mtime_nsec[8]
	                      // ctime_sec[8] ctime_nsec[8] btime_sec[8]
	                      // btime_nsec[8] gen[8] data_version[8]

	return transact(Tgetattr, "dq", "qQdddqqqqqqqqqqqqqqq",
		fid, request_mask,

		// very many return fields
		&ret->valid, &ret->qid, &ret->mode, &ret->uid, &ret->gid,
		&ret->nlink, &ret->rdev, &ret->size, &ret->blksize, &ret->blocks,
		&ret->atime_sec, &ret->atime_nsec,
		&ret->mtime_sec, &ret->mtime_nsec,
		&ret->ctime_sec, &ret->ctime_nsec,
		NULL, NULL, NULL, NULL); // discard btime, gen and data_version fields
}

int Setattr9(uint32_t fid, uint32_t request_mask, struct Stat9 to) {
	enum {Tsetattr = 26}; // size[4] Tsetattr tag[2] fid[4] valid[4]
	                      // mode[4] uid[4] gid[4] size[8]
                          // atime_sec[8] atime_nsec[8] mtime_sec[8] mtime_nsec[8]
	enum {Rsetattr = 27}; // size[4] Rsetattr tag[2]

	return transact(Tsetattr, "dddddqqqqq", "",
		fid, request_mask,
		to.mode, to.uid, to.gid, to.size,
		to.atime_sec, to.atime_nsec, to.mtime_sec, to.mtime_nsec);
}

int Clunk9(uint32_t fid) {
	enum {Tclunk = 120}; // size[4] Tclunk tag[2] fid[4]
	enum {Rclunk = 121}; // size[4] Rclunk tag[2]

	if (fid < 32) openfids &= ~(1<<fid);

	return transact(Tclunk, "d", "",
		fid);
}

int Read9(uint32_t fid, void *buf, uint64_t offset, uint32_t count, uint32_t *actual_count) {
	enum {Tread = 116}; // size[4] Tread tag[2] fid[4] offset[8] count[4]
	enum {Rread = 117}; // size[4] Rread tag[2] count[4] data[count]

	// In event of failure, emphasise that no bytes were read
	if (actual_count) {
		*actual_count = 0;
	}

	return transact(Tread, "dqd", "dB",
		fid, offset, count,
		actual_count, buf, count);
}

int Write9(uint32_t fid, const void *buf, uint64_t offset, uint32_t count, uint32_t *actual_count) {
	enum {Twrite = 118}; // size[4] Twrite tag[2] fid[4] offset[8] count[4] data[count]
	enum {Rwrite = 119}; // size[4] Rwrite tag[2] count[4]

	// In event of failure, emphasise that no bytes were read
	if (actual_count) {
		*actual_count = 0;
	}

	return transact(Twrite, "dqdB", "d",
		fid, offset, count, buf, count,
		actual_count);
}

int Fsync9(uint32_t fid) {
	enum {Tfsync = 50}; // size[4] Tfsync tag[2] fid[4]
	enum {Rfsync = 51}; // size[4] Rfsync tag[2]

	return transact(Tfsync, "d", "",
		fid);
}

void QueueNotified9(void) {
	flag = true;
}

/*
letter |  Tx  |  Rx  | Tx args      | Rx args       | comment
b         ok     ok    uint8_t        uint8_t *       byte
w         ok     ok    uint16_t       uint16_t *      word(16)
d         ok     ok    uint32_t       uint32_t *      dword(32)
q         ok     ok    uint64_t       uint64_t *      qword(64)
s         ok    @end   const char *   char *          string(16-prefix)
Q                ok                   struct Qid9 *   qid
B        @end   @end   const void *   void *          large trailing buffer
                        + uint32_t      + uint32_t
*/

static int transact(uint8_t cmd, const char *tfmt, const char *rfmt, ...) {
	char t[256] = {}, r[256] = {}; // enough to store just about anything (not page aligned sadly)
	int ts=7, rs=7;

	void *tbig = NULL, *rbig = NULL;
	uint32_t tbigsize = 0, rbigsize = 0;

    va_list va;
    va_start(va, rfmt);

	for (const char *f=tfmt; *f!=0; f++) {
		if (*f == 'b') {
			uint8_t val = va_arg(va, unsigned int); // promoted
			t[ts++] = val;
		} else if (*f == 'w') {
			uint16_t val = va_arg(va, unsigned int); // maybe promoted
			WRITE16LE(t+ts, val);
			ts += 2;
		} else if (*f == 'd') {
			uint32_t val = va_arg(va, uint32_t);
			WRITE32LE(t+ts, val);
			ts += 4;
		} else if (*f == 'q') {
			uint64_t val = va_arg(va, uint64_t);
			WRITE64LE(t+ts, val);
			ts += 8;
		} else if (*f == 's') {
			const char *s = va_arg(va, const char *);
			uint16_t slen = s ? strlen(s) : 0;
			WRITE16LE(t+ts, slen);
			memcpy(t+ts+2, s, slen);
			ts += 2 + slen;
		} else if (*f == 'B') {
			tbig = va_arg(va, void *);
			tbigsize = va_arg(va, size_t);
		}
	}

	WRITE32LE(t, ts + tbigsize); // size field
	*(t+4) = cmd; // T-command number
	WRITE16LE(t+5, 0); // zero is our only tag number (for now...)

// 	printf("> ");
// 	for (int i=0; i<ts; i++) {
// 		printf("%02x", 255 & t[i]);
// 	}
// 	printf(" ");
// 	for (int i=0; i<tbigsize; i++) {
// 		printf("%02x", 255 & ((char *)tbig)[i]);
// 	}
// 	printf("\n");

	// add up rx buffer size
	// (unfortunately need to iterate the VA list just to get the "big" buffer)
    va_list tmpva;
    va_copy(tmpva, va);
	for (const char *f=rfmt; *f!=0; f++) {
		if (*f == 'b') {
			va_arg(tmpva, unsigned int); // promoted
			rs += 1;
		} else if (*f == 'w') {
			va_arg(tmpva, unsigned int); // maybe promoted
			rs += 2;
		} else if (*f == 'd') {
			va_arg(tmpva, uint32_t);
			rs += 4;
		} else if (*f == 'q') {
			va_arg(tmpva, uint64_t);
			rs += 8;
		} else if (*f == 's') { // receiving arbitrary-length strings is yuck!
			va_arg(tmpva, char *);
			rs += 2+STRMAX;
		} else if (*f == 'Q') {
			va_arg(tmpva, struct Qid9 *);
			rs += 13;
		} else if (*f == 'B') {
			rbig = va_arg(tmpva, void *);
			rbigsize = va_arg(tmpva, size_t); // this is problematic... the argument is actually hard to find!
		}
	}
	va_end(tmpva);

	// Make room for an Rlerror response to any request (Tclunk doesn't leave enough)
	// (Assume that if a "B" trailer is supplied, it is large enough)
	if (rs < 11 && rbigsize == 0) rs = 11;

	long txn = 0, rxn = 0;
	PhysicalAddress pa[bufcnt];
	uint32_t sz[bufcnt];

	struct MemoryBlock logiranges[] = { // keep the tx before the rx ranges
		{.address=t, .count=ts},
		{.address=tbig, .count=tbigsize},
		{.address=r, .count=rs},
		{.address=rbig, .count=rbigsize},
	};

#define CLEANUP() {for (int i=0; i<4; i++) {if (beenlocked & (1<<i)) {UnlockMemory(logiranges[i].address, logiranges[i].count);}}}

	int beenlocked = 0; // a bitmask for when we clean up

	for (int i=0; i<4; i++) {
		if (logiranges[i].count == 0) continue;

		if (LockMemory(logiranges[i].address, logiranges[i].count)) {
			CLEANUP();
			panic("cannot lock memory");
		}

		beenlocked |= (1<<i);

		MemoryBlock mbs[256] = {logiranges[i]};
		long extents = 255;

		if (GetPhysical((void *)mbs, &extents) || extents >= 255) {
			CLEANUP();
			panic("cannot get physical memory");
		}

		for (int j=0; j<extents; j++) {
			if (txn+rxn == bufcnt) panic("too discontiguous");

			pa[txn+rxn] = mbs[j+1].address;
			sz[txn+rxn] = mbs[j+1].count;
			if (i < 2) {
				txn++;
			} else {
				rxn++;
			}
		}
	}

	flag = false;
	QSend(0, txn, rxn, (void *)pa, sz, NULL);
	QNotify(0);
	while (!flag) QPoll(0); // spin -- unfortunate

	CLEANUP();

// 	printf("< ");
// 	for (int i=0; i<rs; i++) {
// 		printf("%02x", 255 & r[i]);
// 	}
// 	printf(" ");
// 	for (int i=0; i<rbigsize; i++) {
// 		printf("%02x", 255 & ((char *)rbig)[i]);
// 	}
// 	printf("\n");

	if (r[4] == 7 /*Rlerror*/) {
		// The errno field might be split between a header ("bwd" etc in
		// the format string) and a trailer (the "B" in the format string).
		uint32_t err = 0;
		char *errbyte = r + 7;
		for (int i=0; i<4; i++) {
			if (errbyte == r + rs) errbyte = rbig;
			err = (uint32_t)(255 & *errbyte) << 24 | err >> 8;
			errbyte++;
		}
		return err; // linux E code
	}

	rs = 7; // rewind to just after the tag field
	// and notice that we pick up where "va" left off
	for (const char *f=rfmt; *f!=0; f++) {
		if (*f == 'b') {
			uint8_t *ptr = va_arg(va, uint8_t *);
			if (ptr) *ptr = *(r+rs);
			rs += 1;
		} else if (*f == 'w') {
			uint16_t *ptr = va_arg(va, uint16_t *);
			if (ptr) *ptr = READ16LE(r+rs);
			rs += 2;
		} else if (*f == 'd') {
			uint32_t *ptr = va_arg(va, uint32_t *);
			if (ptr) *ptr = READ32LE(r+rs);
			rs += 4;
		} else if (*f == 'q') {
			uint64_t *ptr = va_arg(va, uint64_t *);
			if (ptr) *ptr = READ64LE(r+rs);
			rs += 8;
		} else if (*f == 's') { // receiving arbitrary-length strings is yuck!
			char *ptr = va_arg(va, char *);
			uint16_t slen = READ16LE(r+rs);
			if (ptr) {
				memcpy(ptr, r+rs+2, slen);
				*(ptr+slen) = 0; // null terminator
			}
			rs += 2 + slen;
		} else if (*f == 'Q') {
			struct Qid9 *ptr = va_arg(va, struct Qid9 *);
			if (ptr) *ptr =
				(struct Qid9){*(r+rs), READ32LE(r+rs+1), READ64LE(r+rs+5)};
			rs += 13;
		} else if (*f == 'B') {
			va_arg(va, void *);
			va_arg(va, size_t); // nothing actually to do
		}
	}

    va_end(va);

    return 0;
}
