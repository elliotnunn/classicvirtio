// 3.1.4. Zero-malloc memory allocator
//
// When SQLite is compiled with the SQLITE_ENABLE_MEMSYS5 option, an
// alternative memory allocator that does not use malloc() is included in
// the build. The SQLite developers refer to this alternative memory
// allocator as "memsys5". Even when it is included in the build, memsys5
// is disabled by default. To enable memsys5, the application must invoke
// the following SQLite interface at start-time:
//
// sqlite3_config(SQLITE_CONFIG_HEAP, pBuf, szBuf, mnReq); In the call
// above, pBuf is a pointer to a large, contiguous chunk of memory space
// that SQLite will use to satisfy all of its memory allocation needs. pBuf
// might point to a static array or it might be memory obtained from some
// other application-specific mechanism. szBuf is an integer that is the
// number of bytes of memory space pointed to by pBuf. mnReq is another
// integer that is the minimum size of an allocation. Any call to
// sqlite3_malloc(N) where N is less than mnReq will be rounded up to
// mnReq. mnReq must be a power of two. We shall see later that the mnReq
// parameter is important in reducing the value of n and hence the minimum
// memory size requirement in the Robson proof.
//
// The memsys5 allocator is designed for use on embedded systems, though
// there is nothing to prevent its use on workstations. The szBuf is
// typically between a few hundred kilobytes up to a few dozen megabytes,
// depending on system requirements and memory budget.
//
// The algorithm used by memsys5 can be called "power-of-two, first-fit".
// The sizes of all memory allocation requests are rounded up to a power of
// two and the request is satisfied by the first free slot in pBuf that is
// large enough. Adjacent freed allocations are coalesced using a buddy
// system. When used appropriately, this algorithm provides mathematical
// guarantees against fragmentation and breakdown, as described further
// below.

#include "9p.h"
#include "panic.h"
#include "printf.h"
#include "sqlite3.h"

// These three functions are statically linked but will never be called
void *malloc(size_t size) {
	panic("tried malloc");
}

void free(void *buf) {
	panic("tried free");
}

void *realloc(void *buf, size_t size) {
	panic("tried realloc");
}

void localtime(void) {
	panic("tried localtime");
}



sqlite3_vfs *sqlite3_demovfs(void);

int sqlite3_os_init(void) {
	int reg = sqlite3_vfs_register(sqlite3_demovfs(), 0);
	return SQLITE_OK;
}




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
  uint32_t fid; // 9P file ID
};

/*
** Write directly to the file passed as the first argument. Even if the
** file has a write-buffer (DemoFile.aBuffer), ignore it.
*/
static int demoDirectWrite(
  DemoFile *p,                    /* File handle */
  const void *zBuf,               /* Buffer containing data to write */
  int iAmt,                       /* Size of data to write in bytes */
  sqlite_int64 iOfst              /* File offset to write to */
){
	panic("directwrite");
//   off_t ofst;                     /* Return value from lseek() */
//   size_t nWrite;                  /* Return value from write() */
//
//   ofst = lseek(p->fd, iOfst, SEEK_SET);
//   if( ofst!=iOfst ){
//     return SQLITE_IOERR_WRITE;
//   }
//
//   nWrite = write(p->fd, zBuf, iAmt);
//   if( nWrite!=iAmt ){
//     return SQLITE_IOERR_WRITE;
//   }
//
//   return SQLITE_OK;
}

/*
** Flush the contents of the DemoFile.aBuffer buffer to disk. This is a
** no-op if this particular file does not have a buffer (i.e. it is not
** a journal file) or if the buffer is currently empty.
*/
static int demoFlushBuffer(DemoFile *p){
	panic("flushbuffer");
//   int rc = SQLITE_OK;
//   if( p->nBuffer ){
//     rc = demoDirectWrite(p, p->aBuffer, p->nBuffer, p->iBufferOfst);
//     p->nBuffer = 0;
//   }
//   return rc;
}

/*
** Close a file.
*/
static int demoClose(sqlite3_file *pFile){
	DemoFile *p = (DemoFile *)pFile;
	Clunk9(p->fid);
	return SQLITE_OK;
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

	uint32_t gotbytes;
	if (Read9(p->fid, zBuf, iOfst, iAmt, &gotbytes)) {
		return SQLITE_IOERR; // Actual IO error, not just short
	}

	// Unread parts of the buffer must be zero-filled
	memset(zBuf + gotbytes, 0, iAmt - gotbytes);

	if (gotbytes == iAmt) {
		return SQLITE_OK;
	} else {
		return SQLITE_IOERR_SHORT_READ;
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

	uint32_t gotbytes;
	if (Write9(p->fid, zBuf, iOfst, iAmt, &gotbytes)) {
		return SQLITE_IOERR;
	}

	if (gotbytes == iAmt) {
		return SQLITE_OK;
	} else {
		return SQLITE_IOERR;
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

// 	DemoFile *p = (DemoFile*)pFile;
// 	if (Fsync9(p->fid)) {
// 		return SQLITE_IOERR_FSYNC;
// 	} else {
		return SQLITE_OK;
// 	}
}

/*
** Write the size of the file in bytes to *pSize.
*/
static int demoFileSize(sqlite3_file *pFile, sqlite_int64 *pSize){
	DemoFile *p = (DemoFile*)pFile;

	struct Stat9 stat;

	if (Getattr9(p->fid, 0x00000200, &stat)) {
		panic("Getattr9 of open sqlite file failed");
	}

	*pSize = stat.size;

	return SQLITE_OK;
}

/*
** Locking functions. The xLock() and xUnlock() methods are both no-ops.
** The xCheckReservedLock() always indicates that no other process holds
** a reserved lock on the database file. This ensures that if a hot-journal
** file is found in the file-system it is rolled back.
*/
static int demoLock(sqlite3_file *pFile, int eLock){
  return SQLITE_OK;
}
static int demoUnlock(sqlite3_file *pFile, int eLock){
  return SQLITE_OK;
}
static int demoCheckReservedLock(sqlite3_file *pFile, int *pResOut){
  *pResOut = 0;
  return SQLITE_OK;
}

/*
** No xFileControl() verbs are implemented by this VFS.
*/
static int demoFileControl(sqlite3_file *pFile, int op, void *pArg){
  return SQLITE_NOTFOUND;
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

	static uint32_t fid = 50000; // our own private fid range
	fid++;

	int oflags = 0;
	if (flags&SQLITE_OPEN_READONLY) oflags |= O_RDONLY;
	if (flags&SQLITE_OPEN_READWRITE) oflags |= O_RDWR;
	if (flags&SQLITE_OPEN_EXCLUSIVE) oflags |= O_EXCL;

	int err = 1;

	Walk9(0 /*ROOTFID*/, fid, 0, NULL, NULL, NULL);

	if (flags&SQLITE_OPEN_CREATE) {
		if (Lcreate9(fid, oflags, 0666, 0, zName, NULL, NULL)) return SQLITE_CANTOPEN;
	} else {
		if (Walk9(fid, fid, 1, (const char *[]){zName}, NULL, NULL)) return SQLITE_CANTOPEN;
		if (Lopen9(fid, oflags, NULL, NULL)) return SQLITE_CANTOPEN;
	}

	DemoFile *p = (DemoFile *)pFile; /* Populate this structure */
	memset(p, 0, sizeof(DemoFile));
	p->fid = fid;
	p->base.pMethods = &demoio;
	if (pOutFlags) {
		*pOutFlags = flags;
	}

	return SQLITE_OK;
}

/*
** Delete the file identified by argument zPath. If the dirSync parameter
** is non-zero, then ensure the file-system modification to delete the
** file has been synced to disk before returning.
*/
static int demoDelete(sqlite3_vfs *pVfs, const char *zPath, int dirSync){
	if (Unlinkat9(0, zPath, 0)) {
		return SQLITE_IOERR_DELETE;
	} else {
		return SQLITE_OK;
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
	int err9 = Walk9(0, 4444, 1, (const char *[]){zPath}, NULL, NULL);

	if (err9) {
		*pResOut = 0;
	} else {
		*pResOut = 1;
		Clunk9(4444);
	}

	return SQLITE_OK;
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
	strcpy(zPathOut, zPath);
	return SQLITE_OK;
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
