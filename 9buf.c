/* Copyright (c) 2024 Elliot Nunn */
/* Licensed under the MIT license */

#include "9buf.h"

char *rbuf;
long rbufsize, rbufat, rbufcnt, rbufseek;
uint32_t rfid;

char *wbuf;
long wbufsize, wbufat, wbufcnt, wbufseek;
uint32_t wfid;

void SetRead(uint32_t fid, void *buffer, long buflen) {
	rbuf = buffer;
	rbufsize = buflen;
	rbufat = rbufcnt = rbufseek = 0;
	rfid = fid;
}

void SetWrite(uint32_t fid, void *buffer, long buflen) {
	wbuf = buffer;
	wbufsize = buflen;
	wbufat = wbufcnt = wbufseek = 0;
	wfid = fid;
}

int WriteBuf(void *x, size_t n) {
	if (wbufcnt + n > wbufsize) {
		int err = Flush();
		if (err) return err;
	}

	memcpy(wbuf + wbufcnt, x, n);
	wbufcnt += n;
	wbufseek += n;
	return 0;
}

// Overwrite: special case for resource forks
// Optimises so we usually don't seek backwards
// *Must* have already written these bytes with Write(0)
int Overwrite(void *buf, long at, long cnt) {
	long ok = 0;
	for (long i=0; i<cnt; i++) {
		if (at+i >= wbufat && at+i < wbufat+wbufcnt) {
			wbuf[at+i-wbufat] = ((char *)buf)[i];
			ok++;
		}
	}

	// Sad case: need an expensive syscall because bytes already flushed
	if (ok < cnt) {
		return Write9(wfid, buf, at, cnt, NULL);
	}

	return 0;
}

int Flush(void) {
	int err = Write9(wfid, wbuf, wbufat, wbufcnt, NULL);
	if (err) return err;
	wbufat = wbufseek;
	wbufcnt = 0;
	return 0;
}

