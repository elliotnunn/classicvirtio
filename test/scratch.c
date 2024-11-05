/* Copyright (c) Elliot Nunn */
/* Licensed under the MIT license */

/*
A closely monitored playground for File Manager operations
*/

#include <stdio.h>
#include <string.h>

#include <Devices.h>
#include <Files.h>

#include "tap.h"

#include "scratch.h"

// operate at the root of this volume
static short vol;
static unsigned char volName[28];

// keep track of the current "scratch tree"
struct trackedFile {
	long id;
	short wd;
	char name[32];	
};
static struct trackedFile tracked[100];
static int trackCount;

static void rmdir(short vol, long dir);
static void closeAllWDs(void);

void InitScratch(void) {
	long nonsense;
	HGetVol(volName, &vol, &nonsense);
}

// Create a hierarchy of directories and files in the root
// Syntax is like: "*Folder(File,AnotherFolder())"
// (Asterisk means SetVol and double-asterisk means HSetVol)
long MkScratchTree(const char *spec) {
	static char old[100];
	if (strcmp(old, spec)) {
		printf("# Scratch tree = %s\n", spec);
		strcpy(old, spec);
	}
	short vol; // the root of the volume containing the app
	long nonsense;
	HGetVol(NULL, &vol, &nonsense);

	long trail[10] = {2}; // 2 means root directory
	int n = 1;

	unsigned char name[32] = {};
	bool first = true;

	closeAllWDs();
	trackCount = 0;
	tracked[trackCount++] = (struct trackedFile){.id=0, .wd=0, .name="zero"};
	tracked[trackCount++] = (struct trackedFile){.id=1, .wd=5555, .name="uber"}; // "parent of root"
	tracked[trackCount++] = (struct trackedFile){.id=2, .wd=vol, .name="root"};
	tracked[trackCount++] = (struct trackedFile){.id=4444, .wd=4444, .name="fake"};

	int stars = 0;
	for (; *spec!=0; spec++) {
		if (*spec == '(') {
			// delete the original
			if (first) {
				first = false;
				HFileInfo pb = {.ioVRefNum=vol, .ioDirID=trail[n-1], .ioNamePtr=name, .ioFDirIndex=0};
				if (PBGetCatInfoSync((void *)&pb) == noErr) {
					rmdir(vol, pb.ioDirID);
				}
			}
			// create a directory and descend into it
			HFileInfo pb = {.ioVRefNum=vol, .ioDirID=trail[n-1], .ioNamePtr=name};
			if (PBDirCreateSync((void *)&pb)) TAPBailOut("could not make scratch dir");
			trail[n++] = pb.ioDirID;
			// track the folders for later use by the tests
			WDParam wdpb = {.ioVRefNum=vol, .ioWDDirID=pb.ioDirID, .ioNamePtr=NULL, .ioWDProcID='test'};
			if (PBOpenWDSync((void *)&wdpb)) TAPBailOut("could not make WD from scratch dir");
			tracked[trackCount].id = pb.ioDirID;
			tracked[trackCount].wd = wdpb.ioVRefNum;
			p2cstrcpy(tracked[trackCount].name, name);
			trackCount++;
			if (stars == 1) {
				PBSetVolSync((void *)&(WDPBRec){.ioVRefNum=wdpb.ioVRefNum});
			} else if (stars == 2) {
				PBHSetVolSync((void *)&(WDPBRec){.ioVRefNum=vol, .ioWDDirID=pb.ioDirID});
			}
			stars = 0;
			name[0] = 0; // reset name
		} else if (*spec == ')' || *spec == ',') {
			// create a file if the most recent token was a file token
			if (name[0] != 0) {
				HFileInfo pb = {.ioVRefNum=vol, .ioDirID=trail[n-1], .ioNamePtr=name};
				if (PBHCreateSync((void *)&pb)) TAPBailOut("could not make scratch file");
				FIDParam ifpb = {.ioVRefNum=vol, .ioSrcDirID=trail[n-1], .ioNamePtr=name};
				if (PBCreateFileIDRefSync((void *)&ifpb)) TAPBailOut("could not get ID for scratch file");
				tracked[trackCount].id = ifpb.ioFileID;
				tracked[trackCount].wd = 1111; // no such thing!
				p2cstrcpy(tracked[trackCount].name, name);
				trackCount++;
				name[0] = 0;
			}
			// ascend out of the last directory
			if (*spec == ')') {
				n--;
			}
		} else if (*spec == '*') {
			stars++;
		} else {
			// append this character to the pending file/directory name
			name[1+name[0]++] = *spec;
		}
	}

	return trail[1]; // ID of the directory created
}

// Look up the CNID of a directory made by MkScratchTree
long ScratchDirID(const char *name) {
	for (int i=0; i<trackCount; i++) {
		if (!strcmp(name, tracked[i].name)) {
			return tracked[i].id;
		}
	}
	TAPBailOut("no DirID available for \"%s\"", name);
}

const char *ScratchNameOfDirID(long id) {
	for (int i=0; i<trackCount; i++) {
		if (id == tracked[i].id) {
			return tracked[i].name;
		}
	}
	return "unknown DirID";
}


short ScratchWD(const char *name) {
	for (int i=0; i<trackCount; i++) {
		if (!strcmp(name, tracked[i].name)) {
			return tracked[i].wd;
		}
	}
	TAPBailOut("no WDRefNum available for \"%s\"", name);
}

const char *ScratchNameOfWD(short wd) {
	for (int i=0; i<trackCount; i++) {
		if (wd == tracked[i].wd) {
			return tracked[i].name;
		}
	}
	return "unknown WDRefNum";
}

short VolRef(void) {
	return vol;
}

// Replace "%" with the volume name
void SubVolName(unsigned char *pstring) {
	unsigned char tmp[256];
	memcpy(tmp, pstring, 1+pstring[0]);
	pstring[0] = 0;
	for (int i=0; i<tmp[0]; i++) {
		if (tmp[1+i] == '%') {
			memcpy(pstring+1+pstring[0], volName+1, volName[0]);
			pstring[0] += volName[0];
		} else {
			pstring[1+pstring[0]++] = tmp[1+i];
		}
	}
}

short MkScratchFileAlphabetic(int size) {
	struct FileParam dpb = {.ioVRefNum=vol, .ioNamePtr="\pScratchAlphabetic"};
	PBDeleteSync((void *)&dpb); // fear not failure

	struct FileParam cpb = {.ioVRefNum=vol, .ioNamePtr="\pScratchAlphabetic"};
	if (PBCreateSync((void *)&cpb)) TAPBailOut("Could not create scratchx");;

	struct FileParam opb = {.ioVRefNum=vol, .ioNamePtr="\pScratchAlphabetic"};
	if (PBOpenSync((void *)&opb)) TAPBailOut("Could not open scratch");

	short ref = opb.ioFRefNum;

	struct IOParam wpb = {.ioRefNum=ref, .ioBuffer="abcdefghijklmnopqrstuvwxyz", .ioReqCount=size};
	if (PBWriteSync((void *)&wpb) || wpb.ioActCount != size) TAPBailOut("Coult not write scratch");

	return ref;
}

static void rmdir(short vol, long dir) {
	long todelete[20] = {dir};
	int n = 1;

	// make a folder list, appending to it continually
	for (int i=0; i<n; i++) {
		for (int c=1;; c++) {
			unsigned char name[32] = {};
			HFileInfo pb = {.ioVRefNum=vol, .ioDirID=todelete[i], .ioNamePtr=name, .ioFDirIndex=c};
			if (PBGetCatInfoSync((void *)&pb)) break;

			if (pb.ioFlAttrib & ioDirMask) {
				todelete[n++] = pb.ioDirID;
			}
		}
	}

	// reverse iterate the deep folder list, deleting everything we find
	for (int i=n-1; i>=0; i--) {
		for (;;) {
			unsigned char name[32] = {};
			HFileInfo pb = {.ioVRefNum=vol, .ioDirID=todelete[i], .ioNamePtr=name, .ioFDirIndex=1};
			if (PBGetCatInfoSync((void *)&pb)) break;

			pb.ioDirID = todelete[i];
			if (PBHDeleteSync((void *)&pb)) TAPBailOut("failed to delete file");
		}
		HFileInfo pb = {.ioVRefNum=vol, .ioDirID=todelete[i], .ioNamePtr=NULL};
		if (PBHDeleteSync((void *)&pb)) TAPBailOut("failed to delete directory");
	}
}

static void closeAllWDs(void) {
	for (int i=0; i<trackCount; i++) {
		PBCloseWDSync((void *)&(WDParam){.ioVRefNum=tracked[i].wd});
	}
}
