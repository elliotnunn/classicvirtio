/* Copyright (c) Elliot Nunn */
/* Licensed under the MIT license */

// Convert the single DoDriverIO entry point to a simplified set of
// C entry points that can be shared with the DRVR runtime.

#include <DriverServices.h>

#include "cleanup.h"

#include "runtime.h"

OSStatus DoDriverIO(AddressSpaceID spaceID, IOCommandID cmdID, IOCommandContents pb, IOCommandCode code, IOCommandKind kind) {
    int err = paramErr;
    if (code==kInitializeCommand || code==kReplaceCommand) {
        err = DriverStart(pb.initialInfo->refNum);
        if (err != noErr) {
            Cleanup();
        }
    } else if (code==kReadCommand) {
        err = DriverRead(&pb.pb->ioParam);
    } else if (code==kWriteCommand) {
        err = DriverWrite(&pb.pb->ioParam);
    } else if (code==kControlCommand) {
        err = DriverCtl(&pb.pb->cntrlParam);
    } else if (code==kStatusCommand) {
        err = DriverStatus(&pb.pb->cntrlParam);
    } else if (code==kFinalizeCommand || code==kSupersededCommand) {
        err = DriverStop();
        if (err == noErr) {
            Cleanup();
        }
    } else if (code==kOpenCommand || code==kCloseCommand) {
        err = noErr; // ignore these
    }

    if (err<=0 && (kind&kImmediateIOCommandKind)==0) {
		return IOCommandIsComplete(cmdID, err);
    } else {
        return err;
    }
}
