/* Copyright (c) 2024 Elliot Nunn */
/* Licensed under the MIT license */

#include <LowMem.h>
#include <Memory.h>

#include <stdbool.h>
#include <string.h> // DELETE THIS once MEMCPY gone

#include "9p.h"
#include "panic.h"
#include "printf.h"

#include "9buf.h"

static uint32_t rfid;
static bool rbufok;
static char *rbuf, *rborrow;
static uint32_t rbufsize;
static uint64_t rbufat, rseek;

char *wbuf;
uint32_t wbufsize, wbufcnt;
uint64_t wbufat, wbufseek;
char *wborrow;
uint32_t wfid;

long bufDiskTime;

void SetRead(uint32_t fid, void *buffer, uint32_t buflen) {
	rfid = fid;
	rbufok = false;
	rborrow = NULL;
	rbuf = buffer;
	rbufsize = buflen;
	rbufat = rseek = 0;
}

void RSeek(uint64_t to) {
	if (rborrow) panic("RSeek before returning RBuffer");
	rseek = to;
}

uint64_t RTell(void) {
	return rseek;
}

// Reads past the end of the file are nulls
char *RBuffer(char *giveback, size_t min) {
	//printf("RBuffer(%#010x, %d)\n", giveback, min);

	// Fast path: plenty of room left in this buffer
	if (rbufok && giveback && min!=0 && giveback+min <= rbuf+rbufsize) {
		//printf("Fast path read ...<%.10s> then <%.50s>\n", giveback-10, giveback);
		return giveback;
	}
	//printf("   Slow path\n");

	// Seek forward (by comparing the passed-in ptr to the last one we returned)
	if (giveback) {
		if (!rborrow) panic("giveback without borrowing\n");
		//printf("Seeking fwd by <%.*s>\n", giveback - rborrow, rborrow);
		rseek += giveback - rborrow;
	}

	// Calls that seek but don't otherwise require further data
	if (min == 0) {
		return rborrow = NULL;
	}

	// Try to satisfy the entire request from the buffer
	if (rbufok && rseek>=rbufat && rseek+min<rbufat+rbufsize) {
		//printf("   Satisfied from buffer, returning %p\n", rbuf + (rseek-rbufat));
		return rborrow = rbuf + (rseek-rbufat);
	}
	//printf("   Not satisfied from buffer\n");

	// Instead try to salvage some bytes from the buffer, move them left
	size_t salvaged = 0;
	if (rbufok && rbufat+rbufsize > rseek) {
		salvaged = rbufsize - (rseek - rbufat);
		//printf("   Salvaging %d bytes\n", salvaged);
		BlockMove(rbuf + (rseek - rbufat), rbuf, salvaged);
	}

	// Make an expensive Tread call
	uint32_t gotten;
	//printf("   Read9 %d %p %#x %#x\n", rfid, rbuf+salvaged, (long)rseek, rbufsize-salvaged);
	Read9(rfid, rbuf+salvaged, rseek+salvaged, rbufsize-salvaged, &gotten);
	//printf("   Got %#x chars\n", gotten);
	if (salvaged+gotten < rbufsize) rbuf[salvaged+gotten] = 0; // null-term EOF

	rbufok = true;
	rbufat = rseek;
	//printf("   Returning %p\n", rbuf);
	return rborrow = rbuf;
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

char *BorrowWriteBuf(size_t min) {
	size_t free = wbufsize - (wbufseek - wbufat);
	if (free < min) Flush();
	wborrow = wbuf + wbufseek - wbufat;
	return wborrow;
}

void ReturnWriteBuf(char *borrowed) {
	if (borrowed > wbuf + wbufsize) panic("wrote past end of buffer!");
	wbufseek += borrowed - wborrow;
	wbufcnt += borrowed - wborrow;
	wborrow = NULL;
}
