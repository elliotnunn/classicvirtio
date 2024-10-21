/* Copyright (c) 2023 Elliot Nunn */
/* Licensed under the MIT license */

// A synchronous 9P2000.u interface backing onto Virtio.
// Functions return true on failure.

// Track use of FID 0-31 and automatically clunk when reuse is attempted

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

enum {
	O_RDONLY    = 00000000,
	O_WRONLY    = 00000001,
	O_RDWR      = 00000002,
	O_CREAT     = 00000100,
	O_EXCL      = 00000200,
	O_TRUNC     = 00001000,
	O_APPEND    = 00002000,
	O_NONBLOCK  = 00004000,
	O_DSYNC     = 00010000,
	O_DIRECTORY = 00200000,
	O_NOFOLLOW  = 00400000,
	O_NOATIME   = 01000000,
};

enum {
	EPERM = 1,
	ENOENT = 2,
	ESRCH = 3,
	EINTR = 4,
	EIO = 5,
	ENXIO = 6,
	E2BIG = 7,
	ENOEXEC = 8,
	EBADF = 9,
	ECHILD = 10,
	EAGAIN = 11,
	ENOMEM = 12,
	EACCES = 13,
	EFAULT = 14,
	ENOTBLK = 15,
	EBUSY = 16,
	EEXIST = 17,
	EXDEV = 18,
	ENODEV = 19,
	ENOTDIR = 20,
	EISDIR = 21,
	EINVAL = 22,
	ENFILE = 23,
	EMFILE = 24,
	ENOTTY = 25,
	ETXTBSY = 26,
	EFBIG = 27,
	ENOSPC = 28,
	ESPIPE = 29,
	EROFS = 30,
	EMLINK = 31,
	EPIPE = 32,
	EDOM = 33,
	ERANGE = 34,
	EDEADLK = 35,
	ENAMETOOLONG = 36,
	ENOLCK = 37,
	ENOSYS = 38,
	ENOTEMPTY = 39,
	ELOOP = 40,
	EWOULDBLOCK = 41,
	ENOMSG = 42,
	EIDRM = 43,
	ECHRNG = 44,
	EL2NSYNC = 45,
	EL3HLT = 46,
	EL3RST = 47,
	ELNRNG = 48,
	EUNATCH = 49,
	ENOCSI = 50,
	EL2HLT = 51,
	EBADE = 52,
	EBADR = 53,
	EXFULL = 54,
	ENOANO = 55,
	EBADRQC = 56,
	EBADSLT = 57,
	EDEADLOCK = 58,
	EBFONT = 59,
	ENOSTR = 60,
	ENODATA = 61,
	ETIME = 62,
	ENOSR = 63,
	ENONET = 64,
	ENOPKG = 65,
	EREMOTE = 66,
	ENOLINK = 67,
	EADV = 68,
	ESRMNT = 69,
	ECOMM = 70,
	EPROTO = 71,
	EMULTIHOP = 72,
	EDOTDOT = 73,
	EBADMSG = 74,
	EOVERFLOW = 75,
	ENOTUNIQ = 76,
	EBADFD = 77,
	EREMCHG = 78,
	ELIBACC = 79,
	ELIBBAD = 80,
	ELIBSCN = 81,
	ELIBMAX = 82,
	ELIBEXEC = 83,
	EILSEQ = 84,
	ERESTART = 85,
	ESTRPIPE = 86,
	EUSERS = 87,
	ENOTSOCK = 88,
	EDESTADDRREQ = 89,
	EMSGSIZE = 90,
	EPROTOTYPE = 91,
	ENOPROTOOPT = 92,
	EPROTONOSUPPORT = 93,
	ESOCKTNOSUPPORT = 94,
	EOPNOTSUPP = 95,
	EPFNOSUPPORT = 96,
	EAFNOSUPPORT = 97,
	EADDRINUSE = 98,
	EADDRNOTAVAIL = 99,
	ENETDOWN = 100,
	ENETUNREACH = 101,
	ENETRESET = 102,
	ECONNABORTED = 103,
	ECONNRESET = 104,
	ENOBUFS = 105,
	EISCONN = 106,
	ENOTCONN = 107,
	ESHUTDOWN = 108,
	ETOOMANYREFS = 109,
	ETIMEDOUT = 110,
	ECONNREFUSED = 111,
	EHOSTDOWN = 112,
	EHOSTUNREACH = 113,
	EALREADY = 114,
	EINPROGRESS = 115,
	ESTALE = 116,
	EUCLEAN = 117,
	ENOTNAM = 118,
	ENAVAIL = 119,
	EISNAM = 120,
	EREMOTEIO = 121,
};

enum {
	STAT_ALL    = 0x000007ff,
	STAT_MODE   = 0x00000001,
	STAT_NLINK  = 0x00000002,
	STAT_UID    = 0x00000004,
	STAT_GID    = 0x00000008,
	STAT_RDEV   = 0x00000010,
	STAT_ATIME  = 0x00000020,
	STAT_MTIME  = 0x00000040,
	STAT_CTIME  = 0x00000080,
	STAT_INO    = 0x00000100,
	STAT_SIZE   = 0x00000200,
	STAT_BLOCKS = 0x00000400,
};

enum {
	SET_MODE      = 0x00000001,
	SET_UID       = 0x00000002,
	SET_GID       = 0x00000004,
	SET_SIZE      = 0x00000008,
	SET_ATIME     = 0x00000010,
	SET_MTIME     = 0x00000020,
	SET_CTIME     = 0x00000040,
	SET_ATIME_SET = 0x00000080, // if these aren't set, times are "current time"
	SET_MTIME_SET = 0x00000100,
};

enum {
	NOFID = -1,
	MAXNAME = 94, // including the null terminator,
	// the largest UTF-8 name representable as 31-byte Mac Roman
};

extern uint32_t Max9;

struct Qid9 {
	uint8_t type;
	uint32_t version;
	uint64_t path;
};

struct Stat9 {
	uint64_t valid;
	struct Qid9 qid;
	uint32_t mode;
	uint32_t uid;
	uint32_t gid;
	uint64_t nlink;
	uint64_t rdev;
	uint64_t size;
	uint64_t blksize;
	uint64_t blocks;
	uint64_t atime_sec;
	uint64_t atime_nsec;
	uint64_t mtime_sec;
	uint64_t mtime_nsec;
	uint64_t ctime_sec;
	uint64_t ctime_nsec;
};

struct Statfs9 {
	uint32_t type;
	uint32_t bsize;
	uint64_t blocks;
	uint64_t bfree;
	uint64_t bavail;
	uint64_t files;
	uint64_t ffree;
	uint64_t fsid;
	uint32_t namelen;
};

int Init9(int bufs);
int Attach9(uint32_t fid, uint32_t afid, const char *uname, const char *aname, uint32_t n_uname, struct Qid9 *retqid);
int Statfs9(uint32_t fid, struct Statfs9 *ret);
int Walk9(uint32_t fid, uint32_t newfid, uint16_t nwname, const char *const *name, uint16_t *retnwqid, struct Qid9 *retqid);
int WalkPath9(uint32_t fid, uint32_t newfid, const char *path);
int Lopen9(uint32_t fid, uint32_t flags, struct Qid9 *retqid, uint32_t *retiounit);
int Lcreate9(uint32_t fid, uint32_t flags, uint32_t mode, uint32_t gid, const char *name, struct Qid9 *retqid, uint32_t *retiounit);
int Xattrwalk9(uint32_t fid, uint32_t newfid, const char *name, uint64_t *retsize);
int Xattrcreate9(uint32_t fid, const char *name, uint64_t size, uint32_t flags);
int Remove9(uint32_t fid);
int Unlinkat9(uint32_t fid, const char *name, uint32_t flags);
int Renameat9(uint32_t olddirfid, const char *oldname, uint32_t newdirfid, const char *newname);
int Mkdir9(uint32_t dfid, uint32_t mode, uint32_t gid, const char *name, struct Qid9 *retqid);
void InitReaddir9(uint32_t fid, void *buf, size_t bufsize);
int Readdir9(void *buf, struct Qid9 *retqid, char *rettype, char retname[MAXNAME]);
int Getattr9(uint32_t fid, uint64_t request_mask, struct Stat9 *ret);
int Setattr9(uint32_t fid, uint32_t request_mask, struct Stat9 to);
int Clunk9(uint32_t fid);
int Read9(uint32_t fid, void *buf, uint64_t offset, uint32_t count, uint32_t *actual_count);
int Write9(uint32_t fid, const void *buf, uint64_t offset, uint32_t count, uint32_t *actual_count);
int Fsync9(uint32_t fid);
