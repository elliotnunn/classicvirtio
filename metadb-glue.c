// The bread around the sqlite3.c sandwich

#include "9p.h"
#include "panic.h"
#include "printf.h"
#include "sqlite3.h"

#include "metadb-glue.h"

sqlite3_vfs *sqlite3_demovfs(void);

// Firstly, have a single db pointer that all code should use
sqlite3 *metadb;

// Secondly, provide a startup hook that sets global sqlite3 configuration
// (called by sqlite3 itself)
int sqlite3_os_init(void) {
	int sqerr;

	sqerr = sqlite3_vfs_register(sqlite3_demovfs(), 0);
	if (sqerr != SQLITE_OK) panic("sqlite3_vfs_register");

	return SQLITE_OK;
}

// Thirdly, stub out libc functions that sqlite links but does not use
// (due to our use of MEMSYS5)
void *malloc(size_t size) {
	panic("sqlite should not call malloc");
}

void free(void *buf) {
	panic("sqlite should not call free");
}

void *realloc(void *buf, size_t size) {
	panic("sqlite should not call realloc");
}

void localtime(void) {
	panic("tried localtime");
}

// Fourthly, give sqlite3 a "VFS" that backs onto our 9P API,
// derived from example code for a minimal VFS

/*
** 2010 April 7
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
*************************************************************************
**
** This file implements an example of a simple VFS implementation that
** omits complex features often not required or not possible on embedded
** platforms.  Code is included to buffer writes to the journal file,
** which can be a significant performance improvement on some embedded
** platforms.
**
** OVERVIEW
**
**   The code in this file implements a minimal SQLite VFS that can be
**   used on Linux and other posix-like operating systems. The following
**   system calls are used:
**
**    File-system: access(), unlink(), getcwd()
**    File IO:     open(), read(), write(), fsync(), close(), fstat()
**    Other:       sleep(), usleep(), time()
**
**   The following VFS features are omitted:
**
**     1. File locking. The user must ensure that there is at most one
**        connection to each database when using this VFS. Multiple
**        connections to a single shared-cache count as a single connection
**        for the purposes of the previous statement.
**
**     2. The loading of dynamic extensions (shared libraries).
**
**     3. Temporary files. The user must configure SQLite to use in-memory
**        temp files when using this VFS. The easiest way to do this is to
**        compile with:
**
**          -DSQLITE_TEMP_STORE=3
**
**     4. File truncation. As of version 3.6.24, SQLite may run without
**        a working xTruncate() call, providing the user does not configure
**        SQLite to use "journal_mode=truncate", or use both
**        "journal_mode=persist" and ATTACHed databases.
**
**   It is assumed that the system uses UNIX-like path-names. Specifically,
**   that '/' characters are used to separate path components and that
**   a path-name is a relative path unless it begins with a '/'. And that
**   no UTF-8 encoded paths are greater than 512 bytes in length.
**
** JOURNAL WRITE-BUFFERING
**
**   To commit a transaction to the database, SQLite first writes rollback
**   information into the journal file. This usually consists of 4 steps:
**
**     1. The rollback information is sequentially written into the journal
**        file, starting at the start of the file.
**     2. The journal file is synced to disk.
**     3. A modification is made to the first few bytes of the journal file.
**     4. The journal file is synced to disk again.
**
**   Most of the data is written in step 1 using a series of calls to the
**   VFS xWrite() method. The buffers passed to the xWrite() calls are of
**   various sizes. For example, as of version 3.6.24, when committing a
**   transaction that modifies 3 pages of a database file that uses 4096
**   byte pages residing on a media with 512 byte sectors, SQLite makes
**   eleven calls to the xWrite() method to create the rollback journal,
**   as follows:
**
**             Write offset | Bytes written
**             ----------------------------
**                        0            512
**                      512              4
**                      516           4096
**                     4612              4
**                     4616              4
**                     4620           4096
**                     8716              4
**                     8720              4
**                     8724           4096
**                    12820              4
**             ++++++++++++SYNC+++++++++++
**                        0             12
**             ++++++++++++SYNC+++++++++++
**
**   On many operating systems, this is an efficient way to write to a file.
**   However, on some embedded systems that do not cache writes in OS
**   buffers it is much more efficient to write data in blocks that are
**   an integer multiple of the sector-size in size and aligned at the
**   start of a sector.
**
**   To work around this, the code in this file allocates a fixed size
**   buffer of SQLITE_DEMOVFS_BUFFERSZ using sqlite3_malloc() whenever a
**   journal file is opened. It uses the buffer to coalesce sequential
**   writes into aligned SQLITE_DEMOVFS_BUFFERSZ blocks. When SQLite
**   invokes the xSync() method to sync the contents of the file to disk,
**   all accumulated data is written out, even if it does not constitute
**   a complete block. This means the actual IO to create the rollback
**   journal for the example transaction above is this:
**
**             Write offset | Bytes written
**             ----------------------------
**                        0           8192
**                     8192           4632
**             ++++++++++++SYNC+++++++++++
**                        0             12
**             ++++++++++++SYNC+++++++++++
**
**   Much more efficient if the underlying OS is not caching write
**   operations.
*/

#include <string.h>

enum {
	BASEFID = 40000, // the directory where all db files are made
	FIRSTFID = 50000, // first of 32 scratch fids
	SCRATCHFID = 49999, // for playing around
};

static uint32_t usedfids; // our own private fid range

/*
** Size of the write buffer used by journal files in bytes.
*/
#ifndef SQLITE_DEMOVFS_BUFFERSZ
# define SQLITE_DEMOVFS_BUFFERSZ 8192
#endif

/*
** The maximum pathname length supported by this VFS.
*/
#define MAXPATHNAME 512

/*
** When using this VFS, the sqlite3_file* handles that SQLite uses are
** actually pointers to instances of type DemoFile.
*/
typedef struct DemoFile DemoFile;
struct DemoFile {
  sqlite3_file base;              /* Base class. Must be first. */
  const char *path;
  uint32_t fid; // 9P file ID
};

// #define dbgprintf printf
// #define dbgprintreturn(x) {printf("         = " #x "\n"); return (x);}
#define dbgprintf(...)
#define dbgprintreturn(x) return x;

/*
** Close a file.
*/
static int demoClose(sqlite3_file *pFile){
	DemoFile *p = (DemoFile *)pFile;
	dbgprintf("**** %s %s\n", __func__, p->path);
	Clunk9(p->fid);
	usedfids &= ~(1UL << (p->fid - FIRSTFID));
	dbgprintreturn(SQLITE_OK);
}

/*
** Read data from a file.
*/
static int demoRead(
  sqlite3_file *pFile,
  void *zBuf,
  int iAmt,
  sqlite_int64 iOfst
){
	DemoFile *p = (DemoFile*)pFile;
	dbgprintf("**** %s %s iOfst=%#lx iAmt=%#x\n", __func__, p->path, (long)iOfst, iAmt);

	uint32_t gotbytes;
	if (Read9(p->fid, zBuf, iOfst, iAmt, &gotbytes)) {
		dbgprintreturn(SQLITE_IOERR); // Actual IO error, not just short
	}

	// Unread parts of the buffer must be zero-filled
	memset(zBuf + gotbytes, 0, iAmt - gotbytes);

	if (gotbytes == iAmt) {
		dbgprintreturn(SQLITE_OK);
	} else {
		dbgprintreturn(SQLITE_IOERR_SHORT_READ);
	}
}

/*
** Write data to a crash-file.
*/
static int demoWrite(
  sqlite3_file *pFile,
  const void *zBuf,
  int iAmt,
  sqlite_int64 iOfst
){
	DemoFile *p = (DemoFile*)pFile;
	dbgprintf("**** %s %s iOfst=%#lx iAmt=%#x\n", __func__, p->path, (long)iOfst, iAmt);

	uint32_t gotbytes;
	if (Write9(p->fid, zBuf, iOfst, iAmt, &gotbytes)) {
		dbgprintreturn(SQLITE_IOERR);
	}

	if (gotbytes == iAmt) {
		dbgprintreturn(SQLITE_OK);
	} else {
		dbgprintreturn(SQLITE_IOERR);
	}
}

/*
** Truncate a file. This is a no-op for this VFS (see header comments at
** the top of the file).
*/
static int demoTruncate(sqlite3_file *pFile, sqlite_int64 size){
	panic("trunc");
// #if 0
//   if( ftruncate(((DemoFile *)pFile)->fd, size) ) return SQLITE_IOERR_TRUNCATE;
// #endif
//   return SQLITE_OK;
}

/*
** Sync the contents of the file to the persistent media.
*/
static int demoSync(sqlite3_file *pFile, int flags){
// ??? the Tfsync call doesn't work?

	DemoFile *p = (DemoFile*)pFile;
	dbgprintf("**** %s %s is a nop\n", __func__, p->path);
// 	if (Fsync9(p->fid)) {
// 		dbgprintreturn(SQLITE_IOERR_FSYNC);
// 	} else {
		dbgprintreturn(SQLITE_OK);
// 	}
}

/*
** Write the size of the file in bytes to *pSize.
*/
static int demoFileSize(sqlite3_file *pFile, sqlite_int64 *pSize){
	DemoFile *p = (DemoFile*)pFile;
	dbgprintf("**** %s %s\n", __func__, p->path);

	struct Stat9 stat;

	if (Getattr9(p->fid, 0x00000200, &stat)) {
		panic("Getattr9 of open sqlite file failed");
	}

	*pSize = stat.size;
	dbgprintf("**** %s = %#lx\n", __func__, (long)stat.size);

	dbgprintreturn(SQLITE_OK);
}

/*
** Locking functions. The xLock() and xUnlock() methods are both no-ops.
** The xCheckReservedLock() always indicates that no other process holds
** a reserved lock on the database file. This ensures that if a hot-journal
** file is found in the file-system it is rolled back.
*/
static int demoLock(sqlite3_file *pFile, int eLock){
	DemoFile *p = (DemoFile*)pFile;
	dbgprintf("**** %s %s is a nop\n", __func__, p->path);
  dbgprintreturn(SQLITE_OK);
}
static int demoUnlock(sqlite3_file *pFile, int eLock){
	DemoFile *p = (DemoFile*)pFile;
	dbgprintf("**** %s %s is a nop\n", __func__, p->path);
  dbgprintreturn(SQLITE_OK);
}
static int demoCheckReservedLock(sqlite3_file *pFile, int *pResOut){
	DemoFile *p = (DemoFile*)pFile;
	dbgprintf("**** %s %s is a nop\n", __func__, p->path);
  *pResOut = 0;
  dbgprintreturn(SQLITE_OK);
}

/*
** No xFileControl() verbs are implemented by this VFS.
*/
static int demoFileControl(sqlite3_file *pFile, int op, void *pArg){
	DemoFile *p = (DemoFile*)pFile;
	dbgprintf("**** %s %s op=%d is a nop\n", __func__, p->path, op);
  dbgprintreturn(SQLITE_NOTFOUND);
}

/*
** The xSectorSize() and xDeviceCharacteristics() methods. These two
** may return special values allowing SQLite to optimize file-system
** access to some extent. But it is also safe to simply return 0.
*/
static int demoSectorSize(sqlite3_file *pFile){
  return 0;
}
static int demoDeviceCharacteristics(sqlite3_file *pFile){
  return 0;
}

/*
** Open a file handle.
*/
static int demoOpen(
  sqlite3_vfs *pVfs,              /* VFS */
  const char *zName,              /* File to open, or 0 for a temp file */
  sqlite3_file *pFile,            /* Pointer to DemoFile struct to populate */
  int flags,                      /* Input SQLITE_OPEN_XXX flags */
  int *pOutFlags                  /* Output SQLITE_OPEN_XXX flags (or NULL) */
){
  static const sqlite3_io_methods demoio = {
    1,                            /* iVersion */
    demoClose,                    /* xClose */
    demoRead,                     /* xRead */
    demoWrite,                    /* xWrite */
    demoTruncate,                 /* xTruncate */
    demoSync,                     /* xSync */
    demoFileSize,                 /* xFileSize */
    demoLock,                     /* xLock */
    demoUnlock,                   /* xUnlock */
    demoCheckReservedLock,        /* xCheckReservedLock */
    demoFileControl,              /* xFileControl */
    demoSectorSize,               /* xSectorSize */
    demoDeviceCharacteristics     /* xDeviceCharacteristics */
  };

	dbgprintf("**** %s %s flags=%#x\n", __func__, zName, flags);

	uint32_t fid = 0;
	for (int i=0; i<32; i++) {
		if ((usedfids & (1UL << i)) == 0) {
			usedfids |= (1UL << i);
			fid = FIRSTFID + i;
			break;
		}
	}
	if (!fid) panic("sql tmfo");

	dbgprintf(" ... fid=%d\n", fid);

	int oflags = 0;
	if (flags&SQLITE_OPEN_READONLY) oflags |= O_RDONLY;
	if (flags&SQLITE_OPEN_READWRITE) oflags |= O_RDWR;
	if (flags&SQLITE_OPEN_EXCLUSIVE) oflags |= O_EXCL;

	int err = 1;

	Walk9(BASEFID, fid, 0, NULL, NULL, NULL);

	if (flags&SQLITE_OPEN_CREATE) {
		if (Lcreate9(fid, oflags, 0666, 0, zName, NULL, NULL)) dbgprintreturn(SQLITE_CANTOPEN);
	} else {
		if (Walk9(fid, fid, 1, (const char *[]){zName}, NULL, NULL)) dbgprintreturn(SQLITE_CANTOPEN);
		if (Lopen9(fid, oflags, NULL, NULL)) dbgprintreturn(SQLITE_CANTOPEN);
	}

	DemoFile *p = (DemoFile *)pFile; /* Populate this structure */
	memset(p, 0, sizeof(DemoFile));
	p->fid = fid;
	p->path = zName;
	p->base.pMethods = &demoio;
	if (pOutFlags) {
		*pOutFlags = flags;
	}

	dbgprintreturn(SQLITE_OK);
}

/*
** Delete the file identified by argument zPath. If the dirSync parameter
** is non-zero, then ensure the file-system modification to delete the
** file has been synced to disk before returning.
*/
static int demoDelete(sqlite3_vfs *pVfs, const char *zPath, int dirSync){
	dbgprintf("**** %s %s\n", __func__, zPath);
	if (Unlinkat9(BASEFID, zPath, 0)) {
		dbgprintreturn(SQLITE_IOERR_DELETE);
	} else {
		dbgprintreturn(SQLITE_OK);
	}
}

#ifndef F_OK
# define F_OK 0
#endif
#ifndef R_OK
# define R_OK 4
#endif
#ifndef W_OK
# define W_OK 2
#endif

/*
** Query the file-system to see if the named file exists, is readable or
** is both readable and writable.
*/
static int demoAccess(
  sqlite3_vfs *pVfs,
  const char *zPath,
  int flags,
  int *pResOut
){
	dbgprintf("**** %s %s flags=%#x\n", __func__, zPath, flags);
	int err9 = Walk9(BASEFID, SCRATCHFID, 1, (const char *[]){zPath}, NULL, NULL);

	if (err9) {
		*pResOut = 0;
	} else {
		*pResOut = 1;
		Clunk9(SCRATCHFID);
	}

	dbgprintf(" ... exists=%d\n", *pResOut);

	dbgprintreturn(SQLITE_OK);
}



// The flags argument to xAccess() may be SQLITE_ACCESS_EXISTS to test for
// the existence of a file, or SQLITE_ACCESS_READWRITE to test whether a
// file is readable and writable, or SQLITE_ACCESS_READ to test whether a
// file is at least readable. The SQLITE_ACCESS_READ flag is never actually
// used and is not implemented in the built-in VFSes of SQLite. The file is
// named by the second argument and can be a directory. The xAccess method
// returns SQLITE_OK on success or some non-zero error code if there is an
// I/O error or if the name of the file given in the second argument is
// illegal. If SQLITE_OK is returned, then non-zero or zero is written into
// *pResOut to indicate whether or not the file is accessible.



/*
** Argument zPath points to a nul-terminated string containing a file path.
** If zPath is an absolute path, then it is copied as is into the output
** buffer. Otherwise, if it is a relative path, then the equivalent full
** path is written to the output buffer.
**
** This function assumes that paths are UNIX style. Specifically, that:
**
**   1. Path components are separated by a '/'. and
**   2. Full paths begin with a '/' character.
*/
static int demoFullPathname(
  sqlite3_vfs *pVfs,              /* VFS */
  const char *zPath,              /* Input path (possibly a relative path) */
  int nPathOut,                   /* Size of output buffer in bytes */
  char *zPathOut                  /* Pointer to output buffer */
){
	dbgprintf("**** %s %s\n", __func__, zPath);
	strcpy(zPathOut, zPath);
	dbgprintreturn(SQLITE_OK);
}

/*
** The following four VFS methods:
**
**   xDlOpen
**   xDlError
**   xDlSym
**   xDlClose
**
** are supposed to implement the functionality needed by SQLite to load
** extensions compiled as shared objects. This simple VFS does not support
** this functionality, so the following functions are no-ops.
*/
static void *demoDlOpen(sqlite3_vfs *pVfs, const char *zPath){
  return 0;
}
static void demoDlError(sqlite3_vfs *pVfs, int nByte, char *zErrMsg){
  sqlite3_snprintf(nByte, zErrMsg, "Loadable extensions are not supported");
  zErrMsg[nByte-1] = '\0';
}
static void (*demoDlSym(sqlite3_vfs *pVfs, void *pH, const char *z))(void){
  return 0;
}
static void demoDlClose(sqlite3_vfs *pVfs, void *pHandle){
  return;
}

/*
** Parameter zByte points to a buffer nByte bytes in size. Populate this
** buffer with pseudo-random data.
*/
static int demoRandomness(sqlite3_vfs *pVfs, int nByte, char *zByte){
  return SQLITE_OK;
}

/*
** Sleep for at least nMicro microseconds. Return the (approximate) number
** of microseconds slept for.
*/
static int demoSleep(sqlite3_vfs *pVfs, int nMicro){
	panic("sleep");
//   sleep(nMicro / 1000000);
//   usleep(nMicro % 1000000);
//   return nMicro;
}

/*
** Set *pTime to the current UTC time expressed as a Julian day. Return
** SQLITE_OK if successful, or an error code otherwise.
**
**   http://en.wikipedia.org/wiki/Julian_day
**
** This implementation is not very good. The current time is rounded to
** an integer number of seconds. Also, assuming time_t is a signed 32-bit
** value, it will stop working some time in the year 2038 AD (the so-called
** "year 2038" problem that afflicts systems that store time this way).
*/
static int demoCurrentTime(sqlite3_vfs *pVfs, double *pTime){
	*pTime = 0.0;
//   time_t t = time(0);
//   *pTime = t/86400.0 + 2440587.5;
//   return SQLITE_OK;
}

/*
** This function returns a pointer to the VFS implemented in this file.
** To make the VFS available to SQLite:
**
**   sqlite3_vfs_register(sqlite3_demovfs(), 0);
*/
sqlite3_vfs *sqlite3_demovfs(void){
  static sqlite3_vfs demovfs = {
    1,                            /* iVersion */
    sizeof(DemoFile),             /* szOsFile */
    MAXPATHNAME,                  /* mxPathname */
    0,                            /* pNext */
    "9p",                         /* zName */
    0,                            /* pAppData */
    demoOpen,                     /* xOpen */
    demoDelete,                   /* xDelete */
    demoAccess,                   /* xAccess */
    demoFullPathname,             /* xFullPathname */
    demoDlOpen,                   /* xDlOpen */
    demoDlError,                  /* xDlError */
    demoDlSym,                    /* xDlSym */
    demoDlClose,                  /* xDlClose */
    demoRandomness,               /* xRandomness */
    demoSleep,                    /* xSleep */
    demoCurrentTime,              /* xCurrentTime */
  };
  return &demovfs;
}
