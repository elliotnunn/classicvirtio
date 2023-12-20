/* Copyright (c) 2023 Elliot Nunn */
/* Licensed under the MIT license */

#include <stdarg.h>
#include <stddef.h>
#include <string.h>

// Globals
void *ofcode; // Client Interface (raw machine code, not a tvector)
long stdout; // OF ihandle

// Prototypes
int of(const char *s, int narg, ...);
void ofprint(const char *s);
void ofhex(long x);
long dtroot(void);
long dtstep(long prev);
void putNDRVs(void);
long readhex(const char *s, int len);
void chain9p(void);
void chainNormalBoot(void);
int virtiotype(int deviceid);

// Entry point (via the asm glue in ndrvloader.s)
void ofmain(void *initrd, long initrdsize, void *ci) {
	ofcode = ci; // the vector for calling into Open Firmware
	of("interpret",
		1, "stdout @",
		2, NULL, &stdout); // get handle for logging

	putNDRVs();
	chain9p();
	chainNormalBoot();
}

// Call wrapper for Open Firmware Client Interface
// Call as: if (of("name",
//                 narg, arg1, arg2, ...
//                 nret, ret1, ret2, ...)) {panic("failed")}
int of(const char *s, int narg, ...) {
	// array to contain {nameptr, narg, arg1..., nret, ret1...}
	long array[16] = {(long)s, narg, 0 /*nret will go here*/};
	va_list list;

	va_start(list, narg);
	for (int i=0; i<narg; i++) {
		array[3+i] = va_arg(list, long);
	}

	int nret = array[2] = va_arg(list, int);

	// Need asm glue because ofcode is a raw code pointer, not a full function ptr
	int result;
	asm volatile (
		"mtctr   %[ofcode]  \n"
		"mr      3,%[array] \n"
		"bctrl              \n"
		"mr      %[result],3\n"
		: [result] "=r" (result)
		: [array] "r" (array), [ofcode] "r" (ofcode) // args
		: "ctr", "lr", "r3", "r4", "r5", "r6", "r7", "r8", "memory" // clobbers
	);

	if (result == 0) {
		for (int i=0; i<nret; i++) {
			long *ptr = va_arg(list, long *);
			if (ptr) *ptr = array[3+narg+i];
		}
	}
	va_end(list);

	return result;
}

// I couldn't bear to static-link another printf implementation
void ofprint(const char *s) {
	of("write",
		3, stdout, s, strlen(s),
		1, NULL); // discard "bytes written"
}

void ofhex(long x) {
	const char *hex = "0123456789abcdef";
	char s[] = "00000000 ";
	for (int i=0; i<8; i++) {
		s[i] = hex[15 & (x >> (28-i*4))];
	}
	ofprint(s);
}

long dtroot(void) {
	long phandle;
	of("finddevice",
		1, "/",
		1, &phandle);
	return phandle;
}

long dtstep(long prev) {
	long phandle = 0;
	of("child",
		1, prev,
		1, &phandle);
	if (phandle != 0) return phandle;

	for (;;) {
		of("peer",
			1, prev,
			1, &phandle);
		if (phandle != 0) return phandle;
		of("parent",
			1, prev,
			1, &prev);
		if (prev == 0) return 0; // finished
	}
}

// Acquire large blob of concatenated NDRVs
extern const char allndrv[];
extern const long allndrvlen;
asm (
	".section .data                 \n"
	".balign 4                      \n"
	".global allndrv, allndrvlen    \n"
	"allndrvlen:                    \n"
	".long allndrvend-allndrv       \n"
	"allndrv:                       \n"
	".incbin \"build/ndrv/allndrv\" \n"
	"allndrvend:                    \n"
	".section .text                 \n"
);

void putNDRVs(void) {
	struct support {
		const void *ndrv;
		long len;
		const char *name;
	};

	struct support supported[64] = {};

	ofprint("Classic Mac OS Virtio Driver Loader (");
	const char *next = allndrv;
	while (next < allndrv + allndrvlen) {
		const char *this = next;

		for (;;) {
			next++;
			if (next >= allndrv + allndrvlen) break;
			if (next[0]=='J' && next[1]=='o' && next[2]=='y' && next[3]=='!' && next[4]=='p') break;
		}

		// Find TheDriverDescription (better not be compressed)
		const char *mtej = this;
		for (;;) {
			mtej++;
			if (mtej + 0x70 >= next) {
				mtej = NULL;
				break;
			}

			if (!memcmp(mtej, "mtej" "\0\0\0\0" "\x0cpci1af4,", 17)) break;
		}
		if (!mtej) continue;

		int vid = virtiotype(readhex(mtej+17, 4));
		if (!vid) continue;

		supported[vid] = (struct support) {.ndrv=this, .len=next-this, .name=mtej+0x31};
		if (this != allndrv) ofprint(" ");
		ofprint(supported[vid].name);
	}
	ofprint(")\n");

	ofprint("Copying NDRVs to device tree:\n");

	for (long ph=dtroot(); ph!=0; ph=dtstep(ph)) {
		long vendorid = 0, deviceid = 0;
		of("getprop",
			4, ph, "vendor-id", &vendorid, sizeof vendorid,
			1, NULL);
		of("getprop",
			4, ph, "device-id", &deviceid, sizeof deviceid,
			1, NULL);

		// Virtio devices only
		int vid = virtiotype(deviceid);
		if (vendorid != 0x1af4 || vid == 0) continue;

		if (supported[vid].ndrv) {
			long len = 0;
			of("setprop",
				4, ph, "driver,AAPL,MacOS,PowerPC", supported[vid].ndrv, supported[vid].len,
				1, &len);

			ofprint("  ");
			ofprint(supported[vid].name);
			ofprint("\n");
		} else {
			ofprint("  no NDRV for Virtio type ");
			ofhex(vid);
			ofprint("\n");
		}
	}
}

void chain9p(void) {
	void *loadbase = (void *)0x4000000;
	void *bootinfo = (void *)0x4400000;

	if (memcmp(bootinfo, "<CHRP-BOOT", 10)) return;

	ofprint("Chainloading Mac OS ROM file to start from 9P...\n");

	long len=0x400000; // always shorter than 4 MB and never ends with a null
	while (((char *)bootinfo)[len-1] == 0) len--;

	memmove(loadbase, bootinfo, len);

	of("interpret",
		2, "!load-size", len,
		0);

	char path[512] = {};

	// Set the CHOSEN property
	for (long ph=dtroot(); ph!=0; ph=dtstep(ph)) {
		long vendorid = 0, deviceid = 0;
		of("getprop",
			4, ph, "vendor-id", &vendorid, sizeof vendorid,
			1, NULL);
		of("getprop",
			4, ph, "device-id", &deviceid, sizeof deviceid,
			1, NULL);

		// Virtio 9P devices only
		if (vendorid == 0x1af4 && virtiotype(deviceid) == 9) {
			of("package-to-path",
				3, ph, path, sizeof path,
				1, NULL);
			strcat(path, ":,\\\\:tbxi");
			break;
		}
	}

	long chosenph = 0;
	of("finddevice",
		1, "/chosen",
		1, &chosenph);

	if (chosenph) {
		of("setprop",
			4, chosenph, "bootpath", path, strlen(path)+1,
			1, NULL);
	}

	// OpenBIOS doesn't offer the "chain" service
	of("interpret",
		1, "init-program go",
		0);
}

void chainNormalBoot(void) {
	of("interpret",
		1, "boot",
		0);
}

// Very basic hex reader, treat bad chars as zero
long readhex(const char *s, int len) {
	long n = 0;
	for (int i=0; i<len; i++) {
		n <<= 4;
		char c = s[i];
		if (c >= '0' && c <= '9') n += c - '0';
		else if (c >= 'a' && c <= 'f') n += c - 'a' + 16;
		else if (c >= 'A' && c <= 'F') n += c - 'A' + 16;
	}
	return n;
}

int virtiotype(int deviceid) {
	// Legacy Virtio range
	if (deviceid >= 0x1000 && deviceid <= 0x1009) {
		const char table[] = {1, 2, 5, 3, 8, 4, 0, 0, 0, 9};
		return table[deviceid - 0x1000];
	}

	// Virtio v1 range
	if (deviceid >= 0x1041 && deviceid <= 0x107f) return deviceid - 0x1041;

	// Not a Virtio device
	return 0;
}
