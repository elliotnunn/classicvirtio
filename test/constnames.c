/* Copyright (c) Elliot Nunn */
/* Licensed under the MIT license */

#include <Files.h>
#include <Devices.h>

#include "constnames.h"

const char *PosModeName(char mode) {
	switch (mode & 3) {
	case 0: return "fsAtMark";
	case 1: return "fsFromStart";
	case 2: return "fsFromLEOF";
	case 3: return "fsFromMark";
	default: return "unknown";
	}
}

const char *PermissionName(char mode) {
	switch (mode) {
	case 0: return "fsCurPerm";
	case 1: return "fsRdPerm";
	case 2: return "fsWrPerm";
	case 3: return "fsRdWrPerm";
	case 4: return "fsRdWrShPerm";
	default: return "unknown";
	}
}
const char *ErrName(short err) {
	switch (err) {
		case 0: return "noErr";
		case -17: return "controlErr";
		case -18: return "statusErr";
		case -19: return "readErr";
		case -20: return "writErr";
		case -23: return "openErr";
		case -24: return "closErr";
		case -33: return "dirFulErr";
		case -34: return "dskFulErr";
		case -35: return "nsvErr";
		case -36: return "ioErr";
		case -37: return "bdNamErr";
		case -38: return "fnOpnErr";
		case -39: return "eofErr";
		case -40: return "posErr";
		case -42: return "mfoErr";
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
		case -58: return "extFSErr";
		case -59: return "fsRnErr";
		case -60: return "badMDBErr";
		case -61: return "wrPermErr";
		case -65: return "offLinErr";
		case -120: return "dirNFErr";
		case -121: return "mwdoErr";
		case -122: return "badMovErr";
		default: return "unknownErr";
	}
}

const char *FieldName(int fieldCode) {
	switch (fieldCode) {
	case ioResult: return "ioResult";
	case ioFileName: return "ioFileName";
	case ioNamePtr: return "ioNamePtr";
	case ioVRefNum: return "ioVRefNum";
	case ioDstVRefNum: return "ioDstVRefNum";
	case ioFRefNum: return "ioFRefNum";
	case ioRefNum: return "ioRefNum";
	case ioDenyModes: return "ioDenyModes";
	case ioFVersNum: return "ioFVersNum";
	case ioVersNum: return "ioVersNum";
	case ioWDIndex: return "ioWDIndex";
	case ioPermssn: return "ioPermssn";
	case ioFCBIndx: return "ioFCBIndx";
	case ioFDirIndex: return "ioFDirIndex";
	case ioNewName: return "ioNewName";
	case ioVolIndex: return "ioVolIndex";
	case ioWDProcID: return "ioWDProcID";
	case ioFlAttrib: return "ioFlAttrib";
	case ioVCrDate: return "ioVCrDate";
	case ioFlVersNum: return "ioFlVersNum";
	case ioBuffer: return "ioBuffer";
	case ioCopyName: return "ioCopyName";
	case ioDrUsrWds: return "ioDrUsrWds";
	case ioFCBFlNm: return "ioFCBFlNm";
	case ioFlFndrInfo: return "ioFlFndrInfo";
	case ioFlags: return "ioFlags";
	case ioWDVRefNum: return "ioWDVRefNum";
	case ioSlot: return "ioSlot";
	case ioVLsBkUp: return "ioVLsBkUp";
	case ioVLsMod: return "ioVLsMod";
	case ioFCBFlags: return "ioFCBFlags";
	case ioNewDirID: return "ioNewDirID";
	case ioReqCount: return "ioReqCount";
	case ioFCBStBlk: return "ioFCBStBlk";
	case ioVAtrb: return "ioVAtrb";
	case ioActCount: return "ioActCount";
	case ioFCBEOF: return "ioFCBEOF";
	case ioVNmFls: return "ioVNmFls";
	case ioVBitMap: return "ioVBitMap";
	case ioVDirSt: return "ioVDirSt";
	case ioFCBPLen: return "ioFCBPLen";
	case ioPosMode: return "ioPosMode";
	case ioVAllocPtr: return "ioVAllocPtr";
	case ioVBlLn: return "ioVBlLn";
	case ioPosOffset: return "ioPosOffset";
	case ioVNmAlBlks: return "ioVNmAlBlks";
	case ioDirID: return "ioDirID";
	case ioDrDirID: return "ioDrDirID";
	case ioFCBCrPs: return "ioFCBCrPs";
	case ioFlNum: return "ioFlNum";
	case ioVAlBlkSiz: return "ioVAlBlkSiz";
	case ioWDDirID: return "ioWDDirID";
	case ioDrNmFls: return "ioDrNmFls";
	case ioFCBVRefNum: return "ioFCBVRefNum";
	case ioFlStBlk: return "ioFlStBlk";
	case ioVClpSiz: return "ioVClpSiz";
	case ioFCBClpSiz: return "ioFCBClpSiz";
	case ioFlLgLen: return "ioFlLgLen";
	case ioAlBlSt: return "ioAlBlSt";
	case ioFCBParID: return "ioFCBParID";
	case ioFlPyLen: return "ioFlPyLen";
	case ioVNxtFNum: return "ioVNxtFNum";
	case ioFlRStBlk: return "ioFlRStBlk";
	case ioVFrBlk: return "ioVFrBlk";
	case ioFlRLgLen: return "ioFlRLgLen";
	case ioVSigWord: return "ioVSigWord";
	case ioVDrvInfo: return "ioVDrvInfo";
	case ioFlRPyLen: return "ioFlRPyLen";
	case ioVDRefNum: return "ioVDRefNum";
	case ioVFSID: return "ioVFSID";
	case ioDrCrDat: return "ioDrCrDat";
	case ioFlCrDat: return "ioFlCrDat";
	case ioVBkUp: return "ioVBkUp";
	case ioDrMdDat: return "ioDrMdDat";
	case ioFlMdDat: return "ioFlMdDat";
	case ioVSeqNum: return "ioVSeqNum";
	case ioVWrCnt: return "ioVWrCnt";
	case ioDrBkDat: return "ioDrBkDat";
	case ioFlBkDat: return "ioFlBkDat";
	case ioVFilCnt: return "ioVFilCnt";
	case ioDrFndrInfo: return "ioDrFndrInfo";
	case ioFlXFndrInfo: return "ioFlXFndrInfo";
	case ioVDirCnt: return "ioVDirCnt";
	case ioVFndrInfo: return "ioVFndrInfo";
	case ioDrParID: return "ioDrParID";
	case ioFlParID: return "ioFlParID";
	case ioFlClpSiz: return "ioFlClpSiz";
	default: return "unknown";
	}
}

const char *TrapName(uint32_t trap) {
	switch (trap) {
	case tOpen: return "Open";
	case tHOpen: return "HOpen";
	case tClose: return "Close";
	case tRead: return "Read";
	case tWrite: return "Write";
	case tGetVInfo: return "GetVInfo";
	case tHGetVInfo: return "HGetVInfo";
	case tCreate: return "Create";
	case tHCreate: return "HCreate";
	case tDelete: return "Delete";
	case tHDelete: return "HDelete";
	case tOpenRF: return "OpenRF";
	case tHOpenRF: return "HOpenRF";
	case tRename: return "Rename";
	case tHRename: return "HRename";
	case tGetFInfo: return "GetFInfo";
	case tHGetFInfo: return "HGetFInfo";
	case tSetFInfo: return "SetFInfo";
	case tHSetFInfo: return "HSetFInfo";
	case tUnmountVol: return "UnmountVol";
	case tMountVol: return "MountVol";
	case tAllocate: return "Allocate";
	case tAllocContig: return "AllocContig";
	case tGetEOF: return "GetEOF";
	case tSetEOF: return "SetEOF";
	case tFlushVol: return "FlushVol";
	case tHTrashVolumeCaches: return "HTrashVolumeCaches";
	case tGetVol: return "GetVol";
	case tHGetVol: return "HGetVol";
	case tSetVol: return "SetVol";
	case tHSetVol: return "HSetVol";
	case tEject: return "Eject";
	case tGetFPos: return "GetFPos";
	case tOffLine: return "OffLine";
	case tSetFLock: return "SetFLock";
	case tHSetFLock: return "HSetFLock";
	case tRstFLock: return "RstFLock";
	case tHRstFLock: return "HRstFLock";
	case tSetFVers: return "SetFVers";
	case tSetFPos: return "SetFPos";
	case tFlushFile: return "FlushFile";
	case tOpenWD: return "OpenWD";
	case tCloseWD: return "CloseWD";
	case tCatMove: return "CatMove";
	case tDirCreate: return "DirCreate";
	case tGetWDInfo: return "GetWDInfo";
	case tGetFCBInfo: return "GetFCBInfo";
	case tGetCatInfo: return "GetCatInfo";
	case tSetCatInfo: return "SetCatInfo";
	case tSetVInfo: return "SetVInfo";
	case tLockRange: return "LockRange";
	case tUnlockRange: return "UnlockRange";
	case tXGetVolInfo: return "XGetVolInfo";
	case tCreateFileIDRef: return "CreateFileIDRef";
	case tDeleteFileIDRef: return "DeleteFileIDRef";
	case tResolveFileIDRef: return "ResolveFileIDRef";
	case tExchangeFiles: return "ExchangeFiles";
	case tCatSearch: return "CatSearch";
	case tOpenDF: return "OpenDF";
	case tHOpenDF: return "HOpenDF";
	case tMakeFSSpec: return "MakeFSSpec";
	case tGetVolumeInfo: return "GetVolumeInfo";
	case tSetVolumeInfo: return "SetVolumeInfo";
	case tDTGetPath: return "DTGetPath";
	case tDTCloseDown: return "DTCloseDown";
	case tDTAddIcon: return "DTAddIcon";
	case tDTGetIcon: return "DTGetIcon";
	case tDTGetIconInfo: return "DTGetIconInfo";
	case tDTAddAPPL: return "DTAddAPPL";
	case tDTRemoveAPPL: return "DTRemoveAPPL";
	case tDTGetAPPL: return "DTGetAPPL";
	case tDTSetComment: return "DTSetComment";
	case tDTRemoveComment: return "DTRemoveComment";
	case tDTGetComment: return "DTGetComment";
	case tDTFlush: return "DTFlush";
	case tDTReset: return "DTReset";
	case tDTGetInfo: return "DTGetInfo";
	case tDTOpenInform: return "DTOpenInform";
	case tDTDelete: return "DTDelete";
	case tHGetVolParms: return "HGetVolParms";
	case tHGetLogInInfo: return "HGetLogInInfo";
	case tHGetDirAccess: return "HGetDirAccess";
	case tHSetDirAccess: return "HSetDirAccess";
	case tHMapID: return "HMapID";
	case tHMapName: return "HMapName";
	case tHCopyFile: return "HCopyFile";
	case tHMoveRename: return "HMoveRename";
	case tHOpenDeny: return "HOpenDeny";
	case tHOpenRFDeny: return "HOpenRFDeny";
	case tGetXCatInfo: return "GetXCatInfo";
	case tGetVolMountInfoSize: return "GetVolMountInfoSize";
	case tGetVolMountInfo: return "GetVolMountInfo";
	case tVolumeMount: return "VolumeMount";
	case tShare: return "Share";
	case tUnshare: return "Unshare";
	case tGetUGEntry: return "GetUGEntry";
	case tGetForkPosition: return "GetForkPosition";
	case tSetForkPosition: return "SetForkPosition";
	case tGetForkSize: return "GetForkSize";
	case tSetForkSize: return "SetForkSize";
	case tAllocateFork: return "AllocateFork";
	case tFlushFork: return "FlushFork";
	case tCloseFork: return "CloseFork";
	case tGetForkCBInfo: return "GetForkCBInfo";
	case tCloseIterator: return "CloseIterator";
	case tGetCatalogInfoBulk: return "GetCatalogInfoBulk";
	case tCatalogSearch: return "CatalogSearch";
	case tGetAltAccess: return "GetAltAccess";
	case tGetForeignPrivs: return "GetForeignPrivs";
	case tSetAltAccess: return "SetAltAccess";
	case tSetForeignPrivs: return "SetForeignPrivs";
	case tMakeFSRef: return "MakeFSRef";
	case tCreateFileUnicode: return "CreateFileUnicode";
	case tCreateDirectoryUnicode: return "CreateDirectoryUnicode";
	case tDeleteObject: return "DeleteObject";
	case tMoveObject: return "MoveObject";
	case tRenameUnicode: return "RenameUnicode";
	case tExchangeObjects: return "ExchangeObjects";
	case tGetCatalogInfo: return "GetCatalogInfo";
	case tSetCatalogInfo: return "SetCatalogInfo";
	case tOpenIterator: return "OpenIterator";
	case tOpenFork: return "OpenFork";
	case tMakeFSRefUnicode: return "MakeFSRefUnicode";
	case tCompareFSRefs: return "CompareFSRefs";
	case tCreateFork: return "CreateFork";
	case tDeleteFork: return "DeleteFork";
	case tIterateForks: return "IterateForks";
	case tReadFork: return "ReadFork";
	case tWriteFork: return "WriteFork";
	default: return "unknown";
	}
}

