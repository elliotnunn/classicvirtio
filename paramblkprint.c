/* Copyright (c) 2023 Elliot Nunn */
/* Licensed under the MIT license */

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <Errors.h>

#include "printf.h"

#include "paramblkprint.h"

static const char *callname(unsigned short selector);
static const char *errname(short err);
static const char *controlname(short code);
static const char *statusname(short code);
static const char *drvrgestaltname(long code);
static const char *drvrconfname(long code);
static const char *minilang(const char *pb, unsigned short selector, short status);

char *PBPrint(void *pb, unsigned short selector, short errcode) {
	static char blob[4096];
	blob[0] = 0;
	char *str = blob;

#	define SPRINTF(...) str += sprintf(str, __VA_ARGS__)
#	define NEWLINE() SPRINTF(errcode>0 ? "\n -> " : "\n<-  ");

	if (errcode>0) {
		char name[128] = {};
		strcpy(name, callname(selector));
		if ((selector&0x200) == 0 && name[0] == 'H')
			memmove(name, name+1, 127); // cut off the H
		SPRINTF("%s(%p)", name, pb);
	}
	NEWLINE();

	if (errcode<=0) {
		SPRINTF("result      %d %sErr", errcode, errname(errcode));
		NEWLINE();
	}

	char program[2048] = {};
	char *prog = program;
	if (errcode>0) strcpy(program, "ioTrap6w ");
	strcat(program, minilang(pb, selector, errcode));

	while (*prog) {
		if (prog[0]=='H' && prog[1]==' ') {
			prog += 2;
			if ((selector&0x200) == 0) {
				break; // stop here for hierarchical call
			}
		}

		// Field name string
		int n=0;
		while (('a'<=prog[n] && prog[n]<='z') || ('A'<=prog[n] && prog[n]<='Z')) n++;
		SPRINTF("%-12.*s", n, prog);
		prog += n;

		// Offset
		unsigned int offset = 0;
		while ('0'<=*prog && *prog<='9') {
			offset = offset*10 + *prog++ - '0';
		}

		// Get field size and print field
		switch (*prog++) {
		case 'l':
			SPRINTF("%04x%04x",
				*(unsigned short *)(pb+offset), *(unsigned short *)(pb+offset+2));
			break;
		case 'w':
			SPRINTF("%04x", *(unsigned short *)(pb+offset));
			break;
		case 'b':
			SPRINTF("%02x", *(unsigned char *)(pb+offset));
			break;
		case 'V': // 32-bit volume Finder info
			for (int i=0; i<32; i+=2) SPRINTF("%04x ", *(unsigned short *)(pb+offset+i));
			break;
		case 'F': // 16-bit file Finder info
			for (int i=0; i<16; i+=2) SPRINTF("%04x ", *(unsigned short *)(pb+offset+i));
			break;
		case 'M': // generic Control call name
			SPRINTF("%d %s", *(unsigned short *)(pb+offset), controlname(*(unsigned short *)(pb+offset)));
			break;
		case 'N': // generic Status call name
			SPRINTF("%d %s", *(unsigned short *)(pb+offset), statusname(*(unsigned short *)(pb+offset)));
			break;
		case 'O': // Driver Configure name (special Control call)
			SPRINTF("'%.4s' %s", pb+offset, drvrconfname(*(long *)(pb+offset)));
			break;
		case 'P': // Driver Gestalt name (special Status call)
			SPRINTF("'%.4s' %s", pb+offset, drvrgestaltname(*(long *)(pb+offset)));
			break;
		case 'Q': // ioFDirIndex for GetFileInfo
		case 'R': // ioFDirIndex for GetCatInfo
			int16_t idx = *(unsigned short *)(pb+offset);
			if (prog[-1]=='Q' && idx<0) idx = 0;
			SPRINTF("%d %s", idx, idx>0 ? "dirID+index" : idx==0 ? "dirID+path" : "dirID only");
			break;
		case 's': // string
			{
				unsigned char *pstring = *(unsigned char **)((char *)pb+offset);
				SPRINTF("%08x", (uintptr_t)pstring);
				if (pstring /*&& (uintptr_t)pstring<*(uintptr_t *)0x39c*/) // check MemTop!
					SPRINTF(" \"%.*s\"", pstring[0], pstring+1);
			}
			break;
		case 'S': // FSSpec
			{
				char *spec = *(char **)(pb+offset);
				SPRINTF("FSSpec(%04x, %08x, \"%.*s\")",
					*(uint16_t *)spec,
					*(uint32_t *)(spec + 2),
					*(uint8_t *)(spec + 6),
					spec + 7);
			}
			break;
		default:
			SPRINTF("unimplemented field");
			break;
		}
		NEWLINE();

		while (*prog == ' ') prog++;
	}

	// Strip off all but the last newline
	while (str[-1] != '\n') {
		str[-1] = 0;
		str--;
	}

	// Strip off the first newline
	str = blob;
	while (str[0] == '\n') str++;
	return str;
}

static const char *callname(unsigned short selector) {
	switch (selector & 0xf0ff) {
		case 0xa000: return "HOpen";
		case 0xa001: return "Close";
		case 0xa002: return "Read";
		case 0xa003: return "Write";
		case 0xa004: return "Control";
		case 0xa005: return "Status";
		case 0xa007: return "HGetVolInfo";
		case 0xa008: return "HCreate";
		case 0xa009: return "HDelete";
		case 0xa00a: return "HOpenRF";
		case 0xa00b: return "HRename";
		case 0xa00c: return "HGetFileInfo";
		case 0xa00d: return "HSetFileInfo";
		case 0xa00e: return "UnmountVol";
		case 0xa00f: return "MountVol";
		case 0xa010: return "Allocate";
		case 0xa011: return "GetEOF";
		case 0xa012: return "SetEOF";
		case 0xa013: return "FlushVol";
		case 0xa014: return "HGetVol";
		case 0xa015: return "HSetVol";
		case 0xa017: return "Eject";
		case 0xa018: return "GetFPos";
		case 0xa035: return "Offline";
		case 0xa041: return "SetFilLock";
		case 0xa042: return "RstFilLock";
		case 0xa043: return "SetFilType";
		case 0xa044: return "SetFPos";
		case 0xa045: return "FlushFile";
		case 0x0001: return "OpenWD";
		case 0x0002: return "CloseWD";
		case 0x0005: return "CatMove";
		case 0x0006: return "DirCreate";
		case 0x0007: return "GetWDInfo";
		case 0x0008: return "GetFCBInfo";
		case 0x0009: return "GetCatInfo";
		case 0x000a: return "SetCatInfo";
		case 0x000b: return "SetVolInfo";
		case 0x0010: return "LockRng";
		case 0x0011: return "UnlockRng";
		case 0x0012: return "XGetVolInfo";
		case 0x0014: return "CreateFileIDRef";
		case 0x0015: return "DeleteFileIDRef";
		case 0x0016: return "ResolveFileIDRef";
		case 0x0017: return "ExchangeFiles";
		case 0x0018: return "CatSearch";
		case 0x001a: return "OpenDF";
		case 0x001b: return "MakeFSSpec";
		case 0x0020: return "DTGetPath";
		case 0x0021: return "DTCloseDown";
		case 0x0022: return "DTAddIcon";
		case 0x0023: return "DTGetIcon";
		case 0x0024: return "DTGetIconInfo";
		case 0x0025: return "DTAddAPPL";
		case 0x0026: return "DTRemoveAPPL";
		case 0x0027: return "DTGetAPPL";
		case 0x0028: return "DTSetComment";
		case 0x0029: return "DTRemoveComment";
		case 0x002a: return "DTGetComment";
		case 0x002b: return "DTFlush";
		case 0x002c: return "DTReset";
		case 0x002d: return "DTGetInfo";
		case 0x002e: return "DTOpenInform";
		case 0x002f: return "DTDelete";
		case 0x0030: return "GetVolParms";
		case 0x0031: return "GetLogInInfo";
		case 0x0032: return "GetDirAccess";
		case 0x0033: return "SetDirAccess";
		case 0x0034: return "MapID";
		case 0x0035: return "MapName";
		case 0x0036: return "CopyFile";
		case 0x0037: return "MoveRename";
		case 0x0038: return "OpenDeny";
		case 0x0039: return "OpenRFDeny";
		case 0x003a: return "GetXCatInfo";
		case 0x003f: return "GetVolMountInfoSize";
		case 0x0040: return "GetVolMountInfo";
		case 0x0041: return "VolumeMount";
		case 0x0042: return "Share";
		case 0x0043: return "UnShare";
		case 0x0044: return "GetUGEntry";
		case 0x0060: return "GetForeignPrivs";
		case 0x0061: return "SetForeignPrivs";
		case 0x001d: return "GetVolumeInfo";
		case 0x001e: return "SetVolumeInfo";
		case 0x0051: return "ReadFork";
		case 0x0052: return "WriteFork";
		case 0x0053: return "GetForkPosition";
		case 0x0054: return "SetForkPosition";
		case 0x0055: return "GetForkSize";
		case 0x0056: return "SetForkSize";
		case 0x0057: return "AllocateFork";
		case 0x0058: return "FlushFork";
		case 0x0059: return "CloseFork";
		case 0x005a: return "GetForkCBInfo";
		case 0x005b: return "CloseIterator";
		case 0x005c: return "GetCatalogInfoBulk";
		case 0x005d: return "CatalogSearch";
		case 0x006e: return "MakeFSRef";
		case 0x0070: return "CreateFileUnicode";
		case 0x0071: return "CreateDirUnicode";
		case 0x0072: return "DeleteObject";
		case 0x0073: return "MoveObject";
		case 0x0074: return "RenameUnicode";
		case 0x0075: return "ExchangeObjects";
		case 0x0076: return "GetCatalogInfo";
		case 0x0077: return "SetCatalogInfo";
		case 0x0078: return "OpenIterator";
		case 0x0079: return "OpenFork";
		case 0x007a: return "MakeFSRefUnicode";
		case 0x007c: return "CompareFSRefs";
		case 0x007d: return "CreateFork";
		case 0x007e: return "DeleteFork";
		case 0x007f: return "IterateForks";
		default: return "(Unknown)";
	}
}

// Only lists errors that might be returned by an FS driver
static const char *errname(short err) {
	switch (err) {
		case 0: return "no";
		case -17: return "control";
		case -18: return "status";
		case -19: return "read";
		case -20: return "writ";
		case -23: return "open";
		case -24: return "clos";
		case -33: return "dirFul";
		case -34: return "dskFul";
		case -35: return "nsv";
		case -36: return "io";
		case -37: return "bdNam";
		case -38: return "fnOpn";
		case -39: return "eof";
		case -40: return "pos";
		case -42: return "tmfo";
		case -43: return "fnf";
		case -44: return "wPr";
		case -45: return "fLckd";
		case -46: return "vLckd";
		case -47: return "fBsy";
		case -48: return "dupFN";
		case -49: return "opWr";
		case -50: return "param";
		case -51: return "rfNum";
		case -52: return "gfp";
		case -53: return "volOffLin";
		case -54: return "perm";
		case -55: return "volOnLin";
		case -58: return "extFS";
		case -59: return "fsRn";
		case -60: return "badMDB";
		case -61: return "wrPerm";
		case -65: return "offLin";
		case -120: return "dirNF";
		case -121: return "tmwdo";
		case -122: return "badMov";
		case -1302: return "notAFileErr";
		default: return "unknown";
	}
}

static const char *controlname(short code) {
	switch (code) {
		case 5: return "kVerify";
		case 6: return "kFormat";
		case 7: return "kEject";
		case 8: return "kSetTagBuffer";
		case 9: return "kTrackCache";
		case 21: return "kDriveIcon";
		case 22: return "kMediaIcon";
		case 23: return "kDriveInfo";
		case 43: return "kDriverConfigureCode";
		case 44: return "kSetStartupPartition";
		case 45: return "kSetStartupMount";
		case 46: return "kLockPartition";
		case 48: return "kClearPartitionMount";
		case 49: return "kUnlockPartition";
		case 50: return "kRegisterPartition";
		case 51: return "kGetADrive";
		case 52: return "kProhibitMounting";
		case 60: return "kMountVolume";
		case 70: return "kdgLowPowerMode";
		default: return "(unknown)";
	}
}

static const char *statusname(short code) {
	switch (code) {
		case 6: return "kReturnFormatList";
		case 8: return "kDriveStatus";
		case 10: return "kMFMStatus";
		case 43: return "kDriverGestaltCode";
		case 44: return "kGetStartupStatus";
		case 45: return "kGetMountStatus";
		case 46: return "kGetLockStatus";
		case 50: return "kGetPartitionStatus";
		case 51: return "kGetPartInfo";
		case 70: return "kdgLowPowerMode";
		case 120: return "kdgReturnDeviceID";
		case 121: return "kdgGetCDDeviceInfo";
		case 123: return "kGetErrorInfo";
		case 124: return "kGetDriveInfo";
		case 125: return "kGetDriveCapacity";
		default: return "(unknown)";
	}
}

static const char *drvrgestaltname(long code) {
	switch (code) {
		case 'vers': return "kdgVersion";
		case 'devt': return "kdgDeviceType";
		case 'intf': return "kdgInterface";
		case 'sync': return "kdgSync";
		case 'boot': return "kdgBoot";
		case 'wide': return "kdgWide";
		case 'purg': return "kdgPurge";
		case 'lpwr': return "kdgSupportsSwitching";
		case 'pmn3': return "kdgMin3VPower";
		case 'pmn5': return "kdgMin5VPower";
		case 'pmx3': return "kdgMax3VPower";
		case 'pmx5': return "kdgMax5VPower";
		case 'psta': return "kdgInHighPower";
		case 'psup': return "kdgSupportsPowerCtl";
		case 'dAPI': return "kdgAPI";
		case 'ejec': return "kdgEject";
		case 'flus': return "kdgFlush";
		case 'vmop': return "kdgVMOptions";
		case 'minf': return "kdgMediaInfo";
		case 'dics': return "kdgPhysDriveIconSuite";
		case 'mics': return "kdgMediaIconSuite";
		case 'mnam': return "kdgMediaName";
		case 'digt': return "kdgGetDriveAddInfo";
		case 'diad': return "kdcAddDriveWithInfo";
		case 'dev1': return "kdgATADev1";
		case 'dvrf': return "kdgDeviceReference";
		case 'nmrg': return "kdgNameRegistryEntry";
		case 'info': return "kdgDeviceModelInfo";
		case 'mdty': return "kdgSupportedMediaTypes";
		case 'ofpt': return "kdgOpenFirmwareBootSupport";
		case 'ofbt': return "kdgOpenFirmwareBootingSupport";
		default: return "(unknown)";
	}
}

static const char *drvrconfname(long code) {
	switch (code) {
		case 'flus': return "kdcFlush";
		case 'vmop': return "kdcVMOptions";
		default: return "(unknown)";
	}
}

// No need to worry about the "usual" fields like ioTrap
static const char *minilang(const char *pb, unsigned short selector, short status) {
	int16_t ioFDirIndex = *(int16_t *)(pb+28);
	switch (selector & 0xf0ff) {
	case 0xa000: // Open
		if (status > 0) {
			// parameters
			return "ioNamePtr18s "
			       "ioVRefNum22w "
			       "ioPermssn27b "
			       "ioMisc28l "
			       "ioDirID48l";
		} else if (status == 0) {
			// return on noErr
			return "ioRefNum24w";
		} else {
			// return on failure
			return "";
		}
	case 0xa001: // Close
		if (status > 0) {
			// parameters
			return "ioRefNum24w";
		} else if (status == 0) {
			// return on noErr
			return "";
		} else {
			// return on failure
			return "";
		}
	case 0xa002: // Read
		if (status > 0) {
			// parameters
			return "ioRefNum24w "
			       "ioBuffer32l "
			       "ioReqCount36l "
			       "ioPosMode44w "
			       "ioPosOffset46l";
		} else {
			return "ioActCount40l "
			       "ioPosOffset46l";
		}
	case 0xa003: // Write
		if (status > 0) {
			// parameters
			return "ioRefNum24w "
			       "ioBuffer32l "
			       "ioReqCount36l "
			       "ioPosMode44w "
			       "ioPosOffset46l";
		} else {
			return "ioActCount40l "
			       "ioPosOffset46l";
		}
	case 0xa004: // Control
		if (status > 0) {
			// parameters
			if (*(short *)(pb+26) == 43) {
				// Driver Configure
				return "ioVRefNum22w "
				       "ioRefNum24w "
				       "csCode26M "
				       "dcSelector28O "
				       "dcParameter32F";
			} else {
				// generic Control call
				return "ioVRefNum22w "
				       "ioRefNum24w "
				       "csCode26M "
				       "csParam28F";
			}
		} else if (status == 0) {
			// return on noErr
			return "";
		} else {
			// return on failure
			return "";
		}
	case 0xa005: // Status
		if (status > 0) {
			// parameters
			if (*(short *)(pb+26) == 43) {
				// Driver Gestalt
				return "ioVRefNum22w "
				       "ioRefNum24w "
				       "csCode26N "
				       "dgSelector28P";
			} else {
				// Generic Status call
				return "ioVRefNum22w "
				       "ioRefNum24w "
				       "csCode26N";
			}
		} else if (status == 0) {
			// return on noErr
			if (*(short *)(pb+26) == 43) {
				// Driver Gestalt response
				return "dgResponse32F";
			} else {
				// Generic Control response
				return "csParam28F";
			}
		} else {
			// return on failure
			return "";
		}
	case 0xa006: // KillIO
		if (status > 0) {
			// parameters
			return "ioRefNum24w";
		} else if (status == 0) {
			// return on noErr
			return "";
		} else {
			// return on failure
			return "";
		}
	case 0xa007: // GetVolInfo
		if (status > 0) {
			// parameters
			return "ioNamePtr18s "
			       "ioVRefNum22w "
			       "ioVolIndex28w";
		} else if (status == 0) {
			// return on noErr
			return "ioNamePtr18s "
			       "ioVRefNum22w "
			       "ioVCrDate30l "
			       "ioVLsMod34l "
			       "ioVAtrb38w "
			       "ioVNmFls40w "
			       "ioVBitMap42w "
			       "ioVAllocPtr44w "
			       "ioVNmAlBlks46w "
			       "ioVAlBlkSiz48l "
			       "ioVClpSiz52l "
			       "ioAlBlSt56w "
			       "ioVNxtFNum58l "
			       "ioVFrBlk62w "
			       "ioVSigWord64w "
			       "ioVDrvInfo66w "
			       "ioVDRefNum68w "
			       "ioVFSID70w "
			       "ioVBkUp72l "
			       "ioVSeqNum76w "
			       "ioVWrCnt78l "
			       "ioVFilCnt82l "
			       "ioVDirCnt86l "
			       "ioVFndrInfo90V";
		} else {
			// return on failure
			return "";
		}
	case 0xa008: // Create
		if (status > 0) {
			// parameters
			return "ioNamePtr18s "
			       "ioVRefNum22w "
			       "ioDirID48l";
		} else if (status == 0) {
			// return on noErr
			return "";
		} else {
			// return on failure
			return "";
		}
	case 0xa009: // Delete
		if (status > 0) {
			// parameters
			return "ioNamePtr18s "
			       "ioVRefNum22w "
			       "ioDirID48l";
		} else if (status == 0) {
			// return on noErr
			return "";
		} else {
			// return on failure
			return "";
		}
	case 0xa00a: // OpenRF
		if (status > 0) {
			// parameters
			return "ioNamePtr18s "
			       "ioVRefNum22w "
			       "ioPermssn27b "
			       "ioMisc28l "
			       "ioDirID48l";
		} else if (status == 0) {
			// return on noErr
			return "ioRefNum24w";
		} else {
			// return on failure
			return "";
		}
	case 0xa00b: // Rename
		if (status > 0) {
			// parameters
			return "ioNamePtr18s "
			       "ioVRefNum22w "
			       "ioMisc28l "
			       "ioDirID48l";
		} else if (status == 0) {
			// return on noErr
			return "";
		} else {
			// return on failure
			return "";
		}
	case 0xa00c: // GetFileInfo
		if (status > 0) {
			// parameters
			return "ioNamePtr18s " // argument in once mode
			       "ioVRefNum22w "
			       "ioFDirIndex28Q "
			       "ioDirID48l" + 13*(ioFDirIndex>0);
		} else if (status == 0) {
			// return on noErr
			return "ioNamePtr18s " // return in the other mode
			       "ioFRefNum24w "
			       "ioFlAttrib30b "
			       "ioFlFndrInfo32F "
			       "ioDirID48l "
			       "ioFlStBlk52w "
			       "ioFlLgLen54l "
			       "ioFlPyLen58l "
			       "ioFlRStBlk62w "
			       "ioFlRLgLen64l "
			       "ioFlRPyLen68l "
			       "ioFlCrDat72l "
			       "ioFlMdDat76l" + 13*(ioFDirIndex<=0);
		} else {
			// return on failure
			return "";
		}
	case 0xa00d: // SetFileInfo
		if (status > 0) {
			// parameters
			return "ioNamePtr18s "
			       "ioVRefNum22w "
			       "ioFlFndrInfo32F "
			       "ioDirID48l "
			       "ioFlCrDat72l "
			       "ioFlMdDat76l";
		} else if (status == 0) {
			// return on noErr
			return "";
		} else {
			// return on failure
			return "";
		}
	case 0xa00e: // UnmountVol
		if (status > 0) {
			// parameters
			return "ioNamePtr18s "
			       "ioVRefNum22w";
		} else if (status == 0) {
			// return on noErr
			return "";
		} else {
			// return on failure
			return "";
		}
	case 0xa00f: // MountVol
		if (status > 0) {
			// parameters
			return "ioVRefNum22w";
		} else if (status == 0) {
			// return on noErr
			return "ioVRefNum22w";
		} else {
			// return on failure
			return "";
		}
	case 0xa010: // Allocate
		if (status > 0) {
			// parameters
			return "ioRefNum24w "
			       "ioReqCount36l";
		} else if (status == 0) {
			// return on noErr
			return "ioActCount40l";
		} else {
			// return on failure
			return "";
		}
	case 0xa011: // GetEOF
		if (status > 0) {
			// parameters
			return "ioRefNum24w";
		} else if (status == 0) {
			// return on noErr
			return "ioMisc28l";
		} else {
			// return on failure
			return "";
		}
	case 0xa012: // SetEOF
		if (status > 0) {
			// parameters
			return "ioRefNum24w "
			       "ioMisc28l";
		} else if (status == 0) {
			// return on noErr
			return "";
		} else {
			// return on failure
			return "";
		}
	case 0xa013: // FlushVol
		if (status > 0) {
			// parameters
			return "ioNamePtr18s "
			       "ioVRefNum22w";
		} else if (status == 0) {
			// return on noErr
			return "";
		} else {
			// return on failure
			return "";
		}
	case 0xa014: // GetVol
		if (status > 0) {
			// parameters
		} else if (status == 0) {
			// return on noErr
			return "ioNamePtr18s "
			       "ioVRefNum22w "
			       "ioWDProcID28l "
			       "ioWDVRefNum32w "
			       "ioWDDirID48l";
		} else {
			// return on failure
			return "";
		}
	case 0xa015: // SetVol
		if (status > 0) {
			// parameters
			return "ioNamePtr18s "
			       "ioVRefNum22w "
			       "ioWDDirID48l";
		} else if (status == 0) {
			// return on noErr
			return "";
		} else {
			// return on failure
			return "";
		}
	case 0xa017: // Eject
		if (status > 0) {
			// parameters
			return "ioNamePtr18s "
			       "ioVRefNum22w";
		} else if (status == 0) {
			// return on noErr
			return "";
		} else {
			// return on failure
			return "";
		}
	case 0xa018: // GetFPos
		if (status > 0) {
			// parameters
			return "ioRefNum24w";
		} else if (status == 0) {
			// return on noErr
			return "ioReqCount36l "
			       "ioActCount40l "
			       "ioPosMode44w "
			       "ioPosOffset46l";
		} else {
			// return on failure
			return "";
		}
	case 0xa035: // Offline
		if (status > 0) {
			// parameters
			return "ioNamePtr18s "
			       "ioVRefNum22w";
		} else if (status == 0) {
			// return on noErr
			return "";
		} else {
			// return on failure
			return "";
		}
	case 0xa041: // SetFilLock
		if (status > 0) {
			// parameters
			return "ioNamePtr18s "
			       "ioVRefNum22w "
			       "ioDirID48l";
		} else if (status == 0) {
			// return on noErr
			return "";
		} else {
			// return on failure
			return "";
		}
	case 0xa042: // RstFilLock
		if (status > 0) {
			// parameters
			return "ioNamePtr18s "
			       "ioVRefNum22w "
			       "ioDirID48l";
		} else if (status == 0) {
			// return on noErr
			return "";
		} else {
			// return on failure
			return "";
		}
	case 0xa044: // SetFPos
		if (status > 0) {
			// parameters
			return "ioRefNum24w "
			       "ioPosMode44w "
			       "ioPosOffset46l";
		} else if (status == 0) {
			// return on noErr
			return "ioPosOffset46l";
		} else {
			// return on failure
			return "";
		}
	case 0xa045: // FlushFile
		if (status > 0) {
			// parameters
			return "ioRefNum24w";
		} else if (status == 0) {
			// return on noErr
			return "";
		} else {
			// return on failure
			return "";
		}
	case 0x01: // OpenWD
		if (status > 0) {
			// parameters
			return "ioNamePtr18s "
			       "ioVRefNum22w "
			       "ioWDProcID28l "
			       "ioWDDirID48l";
		} else if (status == 0) {
			// return on noErr
			return "ioVRefNum22w";
		} else {
			// return on failure
			return "";
		}
	case 0x02: // CloseWD
		if (status > 0) {
			// parameters
			return "ioVRefNum22w";
		} else if (status == 0) {
			// return on noErr
			return "";
		} else {
			// return on failure
			return "";
		}
	case 0x05: // CatMove
		if (status > 0) {
			// parameters
			return "ioNamePtr18s "
			       "ioVRefNum22w "
			       "ioNewName28s "
			       "ioNewDirID36l "
			       "ioDirID48l";
		} else if (status == 0) {
			// return on noErr
			return "";
		} else {
			// return on failure
			return "";
		}
	case 0x06: // DirCreate
		if (status > 0) {
			// parameters
			return "ioNamePtr18s "
			       "ioVRefNum22w "
			       "ioDirID48l";
		} else if (status == 0) {
			// return on noErr
			return "ioNamePtr18s "
			       "ioDirID48l";
		} else {
			// return on failure
			return "";
		}
	case 0x07: // GetWDInfo
		if (status > 0) {
			// parameters
			return "ioVRefNum22w "
			       "ioWDIndex26w "
			       "ioWDProcID28l "
			       "ioWDVRefNum32w";
		} else if (status == 0) {
			// return on noErr
			return "ioNamePtr18s "
			       "ioVRefNum22w "
			       "ioWDProcID28l "
			       "ioWDVRefNum32w "
			       "ioWDDirID48l";
		} else {
			// return on failure
			return "";
		}
	case 0x08: // GetFCBInfo
		if (status > 0) {
			// parameters
			return "ioVRefNum22w "
			       "ioRefNum24w "
			       "ioFCBIndx28l";
		} else if (status == 0) {
			// return on noErr
			return "ioNamePtr18s "
			       "ioVRefNum22w "
			       "ioRefNum24w "
			       "ioFCBFlNm32l "
			       "ioFCBFlags36w "
			       "ioFCBStBlk38w "
			       "ioFCBEOF40l "
			       "ioFCBPLen44l "
			       "ioFCBCrPs48l "
			       "ioFCBVRefNum52w "
			       "ioFCBClpSiz54l "
			       "ioFCBParID58l";
		} else {
			// return on failure
			return "";
		}
	case 0x09: // GetCatInfo
		if (status > 0) {
			// parameters
			return "ioNamePtr18s " // argument in one mode
			       "ioVRefNum22w "
			       "ioFDirIndex28R "
			       "ioDirID48l" + 13*(ioFDirIndex!=0);
		} else if (status == 0) {
			// return on noErr
			if (pb[30] & 0x10) { // directory
				return "ioNamePtr18s " // return in the other 2 modes
				       "ioFRefNum24w "
				       "ioFlAttrib30b "
				       "ioACUser31b "
				       "ioDrUsrWds32F "
				       "ioDrDirID48l "
				       "ioDrNmFls52w "
				       "ioDrCrDat72l "
				       "ioDrMdDat76l "
				       "ioDrBkDat80l "
				       "ioDrFndrInfo84F "
				       "ioDrParID100l" + 13*(ioFDirIndex==0);
			} else { // file
				return "ioNamePtr18s " // return in the other 2 modes
				       "ioFRefNum24w "
				       "ioFlAttrib30b "
				       "ioACUser31b "
				       "ioFlFndrInfo32F "
				       "ioDirID48l "
				       "ioFlStBlk52w "
				       "ioFlLgLen54l "
				       "ioFlPyLen58l "
				       "ioFlRStBlk62w "
				       "ioFlRLgLen64l "
				       "ioFlRPyLen68l "
				       "ioFlCrDat72l "
				       "ioFlMdDat76l "
				       "ioFlBkDat80l "
				       "ioFlXFndrInfo84F "
				       "ioFlParID100l "
				       "ioFlClpSiz104l" + 13*(ioFDirIndex==0);
			}
		} else {
			// return on failure
			return "";
		}
	case 0x0a: // SetCatInfo
		if (status > 0) {
			// parameters
			return "ioNamePtr18s "
			       "ioVRefNum22w "
			       "ioFlAttrib30b "
			       "ioFlFndrInfo32F "
			       "ioDirID48l "
			       "ioFlCrDat72l "
			       "ioFlMdDat76l "
			       "ioFlBkDat80l "
			       "ioFlXFndrInfo84F "
			       "ioFlClpSiz104l";
		} else if (status == 0) {
			// return on noErr
			return "ioNamePtr18s";
		} else {
			// return on failure
			return "";
		}
	case 0x0b: // SetVolInfo
		if (status > 0) {
			// parameters
			return "ioNamePtr18s "
			       "ioVRefNum22w "
			       "ioVCrDate30l "
			       "ioVLsMod34l "
			       "ioVAtrb38w "
			       "ioVClpSiz52l "
			       "ioVBkUp72l "
			       "ioVSeqNum76w "
			       "ioVFndrInfo90V";
		} else if (status == 0) {
			// return on noErr
			return "";
		} else {
			// return on failure
			return "";
		}
	case 0x10: // LockRng
		if (status > 0) {
			// parameters
			return "ioRefNum24w "
			       "ioReqCount36l "
			       "ioPosMode44w "
			       "ioPosOffset46l";
		} else if (status == 0) {
			// return on noErr
			return "";
		} else {
			// return on failure
			return "";
		}
	case 0x11: // UnlockRng
		if (status > 0) {
			// parameters
			return "ioRefNum24w "
			       "ioReqCount36l "
			       "ioPosMode44w "
			       "ioPosOffset46l";
		} else if (status == 0) {
			// return on noErr
			return "";
		} else {
			// return on failure
			return "";
		}
	case 0x12: // XGetVolInfo
		return "";
	case 0x14: // CreateFileIDRef
		if (status > 0) {
			// parameters
			return "ioNamePtr18s "
			       "ioVRefNum22w "
			       "ioDirID48l";
		} else if (status == 0 || status == notAFileErr) {
			return "ioFileID54l";
		} else {
			// return on failure
			return "";
		}
	case 0x15: // DeleteFileIDRef
		if (status > 0) {
			// parameters
			return "ioFileID54l";
		} else {
			return "";
		}
	case 0x16: // ResolveFileIDRef
		if (status > 0) {
			// parameters
			return "ioFileID54l";
		} else {
			return "ioNamePtr18s "
			       "ioDirID48l";
		}
	case 0x17: // ExchangeFiles
		return "";
	case 0x18: // CatSearch
		return "";
	case 0x1a: // OpenDF
		if (status > 0) {
			// parameters
			return "ioNamePtr18s "
			       "ioVRefNum22w "
			       "ioPermssn27b "
			       "ioMisc28l "
			       "ioDirID48l";
		} else if (status == 0) {
			// return on noErr
			return "ioRefNum24w";
		} else {
			// return on failure
			return "";
		}
	case 0x1b: // MakeFSSpec
		if (status > 0) {
			// parameters
			return "ioNamePtr18s "
			       "ioVRefNum22w "
			       "ioDirID48l";
		} else if (status == 0 || status == -43) {
			// return on noErr or fnfErr
			return "ioMisc28S";
		} else {
			// return on failure
			return "";
		}
	case 0x20: // DTGetPath
		return "";
	case 0x21: // DTCloseDown
		return "";
	case 0x22: // DTAddIcon
		return "";
	case 0x23: // DTGetIcon
		return "";
	case 0x24: // DTGetIconInfo
		return "";
	case 0x25: // DTAddAPPL
		return "";
	case 0x26: // DTRemoveAPPL
		return "";
	case 0x27: // DTGetAPPL
		return "";
	case 0x28: // DTSetComment
		return "";
	case 0x29: // DTRemoveComment
		return "";
	case 0x2a: // DTGetComment
		return "";
	case 0x2b: // DTFlush
		return "";
	case 0x2c: // DTReset
		return "";
	case 0x2d: // DTGetInfo
		return "";
	case 0x2e: // DTOpenInform
		return "";
	case 0x2f: // DTDelete
		return "";
	case 0x30: // GetVolParms
		if (status > 0) {
			// parameters
			return "ioFileName18s "
			       "ioVRefNum22w "
			       "ioReqCount36l";
		} else if (status == 0) {
			// return on noErr
			return "ioBuffer32l "
			       "ioActCount40l";
		} else {
			// return on failure
			return "";
		}
	case 0x31: // GetLogInInfo
		if (status > 0) {
			// parameters
			return "ioVRefNum22w";
		} else if (status == 0) {
			// return on noErr
			return "ioObjType26w "
			       "ioObjNamePtr28s";
		} else {
			// return on failure
			return "";
		}
	case 0x32: // GetDirAccess
		if (status > 0) {
			// parameters
			return "ioFileName18s "
			       "ioVRefNum22w "
			       "ioDirID48l";
		} else if (status == 0) {
			// return on noErr
			return "ioACOwnerID36l "
			       "ioACGroupID40l "
			       "ioACAccess44l";
		} else {
			// return on failure
			return "";
		}
	case 0x33: // SetDirAccess
		if (status > 0) {
			// parameters
			return "ioFileName18s "
			       "ioVRefNum22w "
			       "ioACOwnerID36l "
			       "ioACGroupID40l "
			       "ioACAccess44l "
			       "ioDirID48l";
		} else if (status == 0) {
			// return on noErr
			return "";
		} else {
			// return on failure
			return "";
		}
	case 0x34: // MapID
		if (status > 0) {
			// parameters
			return "ioFileName18s "
			       "ioVRefNum22w "
			       "ioObjType26w "
			       "ioObjID32l";
		} else if (status == 0) {
			// return on noErr
			return "ioObjNamePtr28s";
		} else {
			// return on failure
			return "";
		}
	case 0x35: // MapName
		if (status > 0) {
			// parameters
			return "ioFileName18s "
			       "ioVRefNum22w "
			       "ioObjNamePtr28s "
			       "ioObjType26w";
		} else if (status == 0) {
			// return on noErr
			return "ioObjID32l";
		} else {
			// return on failure
			return "";
		}
	case 0x36: // CopyFile
		if (status > 0) {
			// parameters
			return "ioFileName18s "
			       "ioVRefNum22w "
			       "ioDstVRefNum24w "
			       "ioNewName28s "
			       "ioCopyName32s "
			       "ioNewDirID36l "
			       "ioDirID48l";
		} else if (status == 0) {
			// return on noErr
			return "";
		} else {
			// return on failure
			return "";
		}
	case 0x37: // MoveRename
		if (status > 0) {
			// parameters
			return "ioFileName18s "
			       "ioVRefNum22w "
			       "ioNewName28s "
			       "ioBuffer32l "
			       "ioNewDirID36l "
			       "ioDirID48l";
		} else if (status == 0) {
			// return on noErr
			return "";
		} else {
			// return on failure
			return "";
		}
	case 0x38: // OpenDeny
		if (status > 0) {
			// parameters
			return "ioFileName18s "
			       "ioVRefNum22w "
			       "ioDenyModes26w "
			       "ioDirID48l";
		} else if (status == 0) {
			// return on noErr
			return "ioRefNum24w";
		} else {
			// return on failure
			return "";
		}
	case 0x39: // OpenRFDeny
		if (status > 0) {
			// parameters
			return "ioFileName18s "
			       "ioVRefNum22w "
			       "ioDenyModes26w "
			       "ioDirID48l";
		} else if (status == 0) {
			// return on noErr
			return "ioRefNum24w";
		} else {
			// return on failure
			return "";
		}
	case 0x3a: // GetXCatInfo
		return "";
	case 0x3f: // GetVolMountInfoSize
		return "";
	case 0x40: // GetVolMountInfo
		return "";
	case 0x41: // VolumeMount
		return "";
	case 0x42: // Share
		return "";
	case 0x43: // UnShare
		return "";
	case 0x44: // GetUGEntry
		return "";
	case 0x60: // GetForeignPrivs
		return "";
	case 0x61: // SetForeignPrivs
		return "";
	case 0x1d: // GetVolumeInfo
		return "";
	case 0x1e: // SetVolumeInfo
		return "";
	case 0x51: // ReadFork
		return "";
	case 0x52: // WriteFork
		return "";
	case 0x53: // GetForkPosition
		return "";
	case 0x54: // SetForkPosition
		return "";
	case 0x55: // GetForkSize
		return "";
	case 0x56: // SetForkSize
		return "";
	case 0x57: // AllocateFork
		return "";
	case 0x58: // FlushFork
		return "";
	case 0x59: // CloseFork
		return "";
	case 0x5a: // GetForkCBInfo
		return "";
	case 0x5b: // CloseIterator
		return "";
	case 0x5c: // GetCatalogInfoBulk
		return "";
	case 0x5d: // CatalogSearch
		return "";
	case 0x6e: // MakeFSRef
		return "";
	case 0x70: // CreateFileUnicode
		return "";
	case 0x71: // CreateDirUnicode
		return "";
	case 0x72: // DeleteObject
		return "";
	case 0x73: // MoveObject
		return "";
	case 0x74: // RenameUnicode
		return "";
	case 0x75: // ExchangeObjects
		return "";
	case 0x76: // GetCatalogInfo
		return "";
	case 0x77: // SetCatalogInfo
		return "";
	case 0x78: // OpenIterator
		return "";
	case 0x79: // OpenFork
		return "";
	case 0x7a: // MakeFSRefUnicode
		return "";
	case 0x7c: // CompareFSRefs
		return "";
	case 0x7d: // CreateFork
		return "";
	case 0x7e: // DeleteFork
		return "";
	case 0x7f: // IterateForks
		return "";
	default:
		return "";
	}
}
