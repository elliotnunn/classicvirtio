/* Copyright (c) 2023 Elliot Nunn */
/* Licensed under the MIT license */

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "printf.h"

#include "paramblkprint.h"

static const char *minilang(const char *pb, unsigned short selector, int pre);
static const char *callname(unsigned short selector);
static const char *errname(short err);
static const char *controlname(short code);
static const char *statusname(short code);
static const char *drvrgestaltname(long code);
static const char *drvrconfname(long code);

// No need to worry about the "usual" fields like ioTrap
static const char *minilang(const char *pb, unsigned short selector, int pre) {
	switch (((long)selector & 0xf0ff) * (pre ? -1 : 1)) {
	case -0xa004: // Control
		if (*(short *)(pb+26) == 43) {
			return "ioVRefNum22w ioRefNum24w csCode26g dcSelector28t dcParameter32l";
		} else {
			return "ioVRefNum22w ioRefNum24w csCode26g csParam28l";
		}
	case 0xa004:
		return "";
	case -0xa005: // Status
		if (*(short *)(pb+26) == 43) {
			return "ioVRefNum22w ioRefNum24w csCode26G dgSelector28T";
		} else {
			return "ioVRefNum22w ioRefNum24w csCode26G csParam28l";
		}
	case 0xa005:
		if (*(short *)(pb+26) == 43) {
			return "ioVRefNum22w ioRefNum24w csCode26g dgResponse32l dgResponse36l dgResponse40l dgResponse44l";
		} else {
			return "csParam28l";
		}
	case -0xa007: // GetVolInfo
		return "ioNamePtr18s ioVRefNum22w ioVolIndex28w";
	case 0xa007:
		return "ioNamePtr18s ioVCrDate30l ioVLsBkUp34l ioVAtrb38w ioVNmFls40w ioVDirSt42w ioVBlLn44w ioVNmAlBlks46w ioVAlBlkSiz48l ioVClpSiz52l ioAlBlSt56w ioVNxtFNum58l ioVFrBlk62w H ioVSigWord64w ioVDrvInfo66w ioVDRefNum68w ioVFSID70w ioVBkUp72l ioVSeqNum76w ioVWrCnt78l ioVFilCnt82l ioVDirCnt86l ioVFndrInfo90X";
	case -0xa00f: // MountVol
		return "ioVRefNum22w";
	case -0x000a: // SetCatInfo
		return "ioNamePtr18s ioVRefNum22w ioFlAttrib30b ioFlFndrInfo32x ioDirID48l ioFlCrDat72l ioFlMdDat76l ioFlBkDat80l ioFlXFndrInfo84x ioFlClpSiz104l";
	case 0x000a:
		return "ioNamePtr18s";
	case -0xa00c: // GetFileInfo
		if (*(short *)(pb+28) <= 0) {
			return "ioNamePtr18s ioVRefNum22w ioFDirIndex28w";
		} else {
			return "ioVRefNum22w ioFDirIndex28w";
		}
	case 0xa00c:
#		define FINFOCOMMON "ioFRefNum24w ioFlAttrib30b ioFlFndrInfo32x ioFlNum48l ioFlStBlk52w ioFlLgLen54l ioFlPyLen58l ioFlRStBlk62w ioFlRLgLen64l ioFlRPyLen68l ioFlCrDat72l ioFlMdDat72l";
		if (*(short *)(pb+28) <= 0) {
			return FINFOCOMMON;
		} else {
			return "ioNamePtr18s " FINFOCOMMON;
		}
	case -0xa013: // FlushVol
		return "ioNamePtr18s ioVRefNum22w";
	case -0x0001: // OpenWD
		return "ioNamePtr18s ioVRefNum22w ioWDProcID28l ioWDDirID48l";
	case 0x0001:
		return "ioVRefNum22w";
	case -0x0006: // DirCreate
		return "ioNamePtr18s ioVRefNum22w ioDirID48l";
	case 0x0006:
		return "ioDirID48l";
	case -0x0007: // GetWDInfo
		return "ioVRefNum22w ioWDIndex26w ioWDProcID28l";
	case 0x0007:
		return "ioNamePtr18s ioVRefNum22w ioWDProcID28l ioWDVRefNum32w";
	case -0x0008: // GetFCBInfo
		return "ioVRefNum22w ioRefNum24w ioFCBIndx28w ioFCBVRefNum52w";
	case 0x0008:
		return "ioNamePtr18s ioRefNum24w ioFCBFlNm32l ioFCBFlags36w ioFCBStBlk38w ioFCBEOF40l ioFCBPLen44l ioFCBCrPs48l ioFCBVRefNum52w ioFCBClpSiz54l ioFCBParID58l";
	case -0x0009: // GetCatInfo
		if (*(short *)(pb+28) == 0) {
			return "ioNamePtr18s ioVRefNum22w ioFDirIndex28w ioDirID48l";
		} else {
			return "ioVRefNum22w ioFDirIndex28w ioDirID48l";
		}
	case 0x0009:
#		define CATINFOCOMMON "ioFRefNum24w ioFlAttrib30b ioFlFndrInfo32x ioDirID48l ioFlStBlk52w ioFlLgLen54l ioFlPyLen58l ioFlRStBlk62w ioFlRLgLen64l ioFlRPyLen68l ioFlCrDat72l ioFlMdDat72l ioFlBkDat80l ioFlXFndrInfo84x ioFlParID100l ioFlClpSiz104l";
		if (*(short *)(pb+28) == 0) {
			return CATINFOCOMMON;
		} else {
			return "ioNamePtr18s " CATINFOCOMMON;
		}
	case -0x001b: // MakeFSSpec
		return "ioNamePtr18s ioVRefNum22w ioDirID48l";
	case 0x001b:
		return "ioMisc28c";
	case -0x0030: // GetVolParms
		return "ioFileName18l ioVRefNum22w ioBuffer32l ioReqCount36l";
	case 0x0030:
		return "ioActCount40l";
	case -0xa000: case -0xa00a: case -0x001a: // Open
		return "ioNamePtr18s ioVRefNum22w ioPermssn27b ioDirID48l";
	case 0xa000: case 0xa00a: case 0x001a:
		return "ioRefNum24w";
	case -0xa002: // Read
		return "ioRefNum24w ioBuffer32l ioReqCount36l ioPosMode44w ioPosOffset46l";
	case 0xa002:
		return "ioActCount40l ioPosOffset46l";
	case -0xa003: // Write
		return "ioRefNum24w ioBuffer32l ioReqCount36l ioPosMode44w ioPosOffset46l";
	case 0xa003:
		return "ioActCount40l ioPosOffset46l";
	case -0xa011: // GetEOF
		return "ioRefNum24w";
	case 0xa011:
		return "ioMisc28l";
	case -0xa00b: // Rename
		return "ioNamePtr18s ioVRefNum22w ioMisc28s ioDirID48l";
	case 0xa00b:
		return "";
	case -0xa012: // SetEOF
		return "ioRefNum24w ioMisc28l";
	case 0xa012:
		return "";
	case -0xa015: // SetVol
		return "ioNamePtr18s ioVRefNum22w ioDirID48l";
	case 0xa015:
		return "";
	case -0xa044: // SetFPos
		return "ioRefNum24w ioReqCount36l ioPosMode44w ioPosOffset46l";
	case 0xa044:
		return "ioPosOffset46l";
	}
	return "";
}

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
		SPRINTF("result      %d %s", errcode, errname(errcode));
		NEWLINE();
	}

	// Don't show the rest of the block if there was an err
	if (errcode>=0) {
		char program[1024] = {};
		char *prog = program;
		if (errcode>0) strcpy(program, "ioTrap6w ");
		strcat(program, minilang(pb, selector, errcode>0));

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
			case 'x':
				for (int i=0; i<16; i+=2) SPRINTF("%04x ", *(unsigned short *)(pb+offset+i));
				break;
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
			case 't':
				SPRINTF("'%.4s' %s", pb+offset, drvrconfname(*(long *)(pb+offset)));
				break;
			case 'T':
				SPRINTF("'%.4s' %s", pb+offset, drvrgestaltname(*(long *)(pb+offset)));
				break;
			case 'g':
				SPRINTF("%d %s", *(unsigned short *)(pb+offset), controlname(*(unsigned short *)(pb+offset)));
				break;
			case 'G':
				SPRINTF("%d %s", *(unsigned short *)(pb+offset), statusname(*(unsigned short *)(pb+offset)));
				break;
			case 's':
				{
					unsigned char *pstring = *(unsigned char **)((char *)pb+offset);
					SPRINTF("%08x", (uintptr_t)pstring);
					if (pstring /*&& (uintptr_t)pstring<*(uintptr_t *)0x39c*/) // check MemTop!
						SPRINTF(" \"%.*s\"", pstring[0], pstring+1);
				}
				break;
			case 'c': // FSSpec
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

static const char *errname(short err) {
	switch (err) {
		case 0: return "noErr";
		case -1: return "qErr";
		case -2: return "vTypErr";
		case -3: return "corErr";
		case -4: return "unimpErr";
		case -5: return "SlpTypeErr";
		case -17: return "controlErr";
		case -18: return "statusErr";
		case -19: return "readErr";
		case -20: return "writErr";
		case -21: return "badUnitErr";
		case -22: return "unitEmptyErr";
		case -23: return "openErr";
		case -24: return "closErr";
		case -25: return "dRemovErr";
		case -26: return "dInstErr";
		case -27: return "abortErr";
		case -28: return "notOpenErr";
		case -29: return "unitTblFullErr";
		case -30: return "dceExtErr";
		case -33: return "dirFulErr";
		case -34: return "dskFulErr";
		case -35: return "nsvErr";
		case -36: return "ioErr";
		case -37: return "bdNamErr";
		case -38: return "fnOpnErr";
		case -39: return "eofErr";
		case -40: return "posErr";
		case -41: return "mFulErr";
		case -42: return "tmfoErr";
		case -43: return "fnfErr";
		case -44: return "wPrErr";
		case -45: return "fLckdErr";
		case -46: return "vLckdErr";
		case -47: return "fBsyErr";
		case -48: return "dupFNErr";
		case -49: return "opWrErr";
		case -50: return "paramErr";
		case -51: return "rfNumErr";
		case -52: return "gfpErr";
		case -53: return "volOffLinErr";
		case -54: return "permErr";
		case -55: return "volOnLinErr";
		case -56: return "nsDrvErr";
		case -57: return "noMacDskErr";
		case -58: return "extFSErr";
		case -59: return "fsRnErr";
		case -60: return "badMDBErr";
		case -61: return "wrPermErr";
		case -64: return "noDriveErr";
		case -65: return "offLinErr";
		case -66: return "noNybErr";
		case -67: return "noAdrMkErr";
		case -68: return "dataVerErr";
		case -69: return "badCksmErr";
		case -70: return "badBtSlpErr";
		case -71: return "noDtaMkErr";
		case -75: return "cantStepErr";
		case -76: return "tk0BadErr";
		case -77: return "initIWMErr";
		case -78: return "twoSideErr";
		case -79: return "spdAdjErr";
		case -80: return "seekErr";
		case -81: return "sectNFErr";
		case -82: return "fmt1Err";
		case -83: return "fmt2Err";
		case -84: return "verErr";
		case -85: return "clkRdErr";
		case -86: return "clkWrErr";
		case -87: return "prWrErr";
		case -88: return "prInitErr";
		case -89: return "rcvrErr";
		case -91: return "ddpSktErr";
		case -92: return "ddpLenErr";
		case -93: return "noBridgeErr";
		case -94: return "lapProtErr";
		case -99: return "memROZErr";
		case -100: return "noScrapErr";
		case -102: return "noTypeErr";
		case -108: return "memFullErr";
		case -109: return "nilHandleErr";
		case -110: return "memAdrErr";
		case -111: return "memWZErr";
		case -112: return "memPurErr";
		case -113: return "memAZErr";
		case -114: return "memPCErr";
		case -115: return "memBCErr";
		case -116: return "memSCErr";
		case -117: return "memLockedErr";
		case -120: return "dirNFErr";
		case -121: return "tmwdoErr";
		case -122: return "badMovErr";
		case -123: return "wrgVolTypErr";
		case -124: return "volGoneErr";
		case -125: return "updPixMemErr";
		case -128: return "userCanceledErr";
		default: return "(unknown)";
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
