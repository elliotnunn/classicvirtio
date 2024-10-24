/* Copyright (c) Elliot Nunn */
/* Licensed under the MIT license */

#include "constnames.h"

const char *PosModeName(char mode) {
	switch (mode & 3) {
	case 0: return "fsAtMark";
	case 1: return "fsFromStart";
	case 2: return "fsFromLEOF";
	case 3: return "fsFromMark";
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
		case -58: return "extFSErr";
		case -59: return "fsRnErr";
		case -60: return "badMDBErr";
		case -61: return "wrPermErr";
		case -65: return "offLinErr";
		case -120: return "dirNFErr";
		case -121: return "tmwdoErr";
		case -122: return "badMovErr";
		default: return "unknownErr";
	}
}

