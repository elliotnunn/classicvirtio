/* Copyright (c) 2024 Elliot Nunn */
/* Licensed under the MIT license */

#include <LowMem.h>
#include <Memory.h>

#include "panic.h"

#include "9buf.h"

char *rbuf;
uint32_t rbufsize, rbufcnt;
uint64_t rbufat, rbufseek;
uint32_t rfid;

char *wbuf;
uint32_t wbufsize, wbufcnt;
uint64_t wbufat, wbufseek;
uint32_t wfid;

long bufDiskTime;

void SetRead(uint32_t fid, void *buffer, uint32_t buflen) {
	rbuf = buffer;
	rbufsize = buflen;
	rbufat = rbufcnt = rbufseek = 0;
	rfid = fid;
}

// Appends a null at EOF
size_t FillReadBuf(size_t min) {
	uint32_t used = rbufseek - rbufat;
	uint32_t kept = rbufcnt - used;

	if (kept >= min) return kept;

	// Reposition the buffer (fairly cheap)
	BlockMoveData(rbuf + used, rbuf, kept);
	rbufcnt = kept;
	rbufat += used;

	// Get some more bytes from disk (not sure how expensive really)
	uint32_t gotten;
	bufDiskTime -= LMGetTicks();
	Read9(rfid, rbuf + kept, rbufat + kept, rbufsize - kept, &gotten);
	bufDiskTime += LMGetTicks();
	if (gotten < rbufsize - kept) {
		rbuf[kept + gotten] = 0; // null terminate the file
	}
	rbufcnt += gotten;
	return kept + gotten; // will return less than min only if hitting EOF
}

void SetWrite(uint32_t fid, void *buffer, uint32_t buflen) {
	wbuf = buffer;
	wbufsize = buflen;
	wbufat = wbufcnt = wbufseek = 0;
	wfid = fid;
}

void WriteBuf(void *x, size_t n) {
	if (wbufcnt + n > wbufsize) {
		Flush();
	}

	memcpy(wbuf + wbufcnt, x, n);
	wbufcnt += n;
	wbufseek += n;
}

// Overwrite: special case for resource forks
// Optimises so we usually don't seek backwards
// *Must* have already written these bytes with Write(0)
void Overwrite(void *buf, uint64_t at, uint32_t cnt) {
	uint32_t ok = 0;
	for (uint32_t i=0; i<cnt; i++) {
		if (at+i >= wbufat && at+i < wbufat+wbufcnt) {
			wbuf[at+i-wbufat] = ((char *)buf)[i];
			ok++;
		}
	}

	// Sad case: need an expensive syscall because bytes already flushed
	if (ok < cnt) {
		bufDiskTime -= LMGetTicks();
		if (Write9(wfid, buf, at, cnt, NULL))
			panic("9buf overwrite failure");
		bufDiskTime += LMGetTicks();
	}
}

void Flush(void) {
	if (wbufcnt > 0) {
		int err = Write9(wfid, wbuf, wbufat, wbufcnt, NULL);
		if (err) panic("9buf flush fail");
	}
	wbufat = wbufseek;
	wbufcnt = 0;
}

void FreeWriteBuf(size_t min) {
	size_t free = wbufsize - (wbufseek - wbufat);
	if (free < min) Flush();
}
