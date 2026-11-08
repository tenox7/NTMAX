/*++

Module Name:

    dxnvram.c

Abstract:

    This module keeps the non-volatile configuration of the ARC firmware
    for the DECstation 5000/200: the configuration, the environment and the
    system identifier the Jazz firmware keeps in its NVRAM.  The DECstation
    has no NVRAM for them (the clock keeps 50 bytes), so the boot device
    keeps the image in the FW_NVRAM_BLOCKS blocks from FW_NVRAM_BLOCK,
    between the firmware and the first partition.  The firmware works on the
    copy in memory, FwNvram, and writes it back when the configuration or
    environment routines set their checksum.

    The console's boot driver writes the image, and it drives the SCSI
    controller as if it were the only one to.  While a loaded program runs,
    the controller can belong to the drivers of the system the program
    started (the environment routines are called by the HAL while the
    system's I/O goes on), so the copy is only marked changed, and it is
    written when the program gives control back to the firmware: it
    returns, or it calls a termination routine (the HAL restarts the system
    with ArcReboot).

Environment:

    Kernel mode only.

--*/

#include "fwp.h"
#include "fwstring.h"

//
// Define the memory copy of the non-volatile configuration, whether its
// writes wait for the firmware to get control back, and whether it has
// changed since it was last written.
//

UCHAR FwNvram[FW_NVRAM_SIZE];
BOOLEAN FwNvramDeferred;
BOOLEAN FwNvramChanged;

ARC_STATUS
FwNvramLoad (
    VOID
    )

/*++

Routine Description:

    This routine reads the non-volatile configuration from the boot device.
    If it cannot be read, the copy is cleared, so its checksums are not
    valid.

Arguments:

    None.

Return Value:

    ESUCCESS if the image was read, otherwise EIO.

--*/

{
    if (FwRexBootRead(FW_NVRAM_BLOCK, FwNvram, FW_NVRAM_SIZE) != FW_NVRAM_SIZE) {
        RtlZeroMemory(FwNvram, FW_NVRAM_SIZE);
        return EIO;
    }

    return ESUCCESS;
}

ARC_STATUS
FwNvramFlush (
    VOID
    )

/*++

Routine Description:

    This routine writes the non-volatile configuration to the boot device.
    While its writes are deferred, it only marks the copy changed.

Arguments:

    None.

Return Value:

    ESUCCESS if the image was written or its write deferred, otherwise EIO.

--*/

{
    if (FwNvramDeferred != FALSE) {
        FwNvramChanged = TRUE;
        return ESUCCESS;
    }

    FwNvramChanged = FALSE;
    if (FwRexBootWrite(FW_NVRAM_BLOCK, FwNvram, FW_NVRAM_SIZE) != FW_NVRAM_SIZE) {
        return EIO;
    }

    return ESUCCESS;
}

VOID
FwNvramDefer (
    VOID
    )

/*++

Routine Description:

    This routine is called before the firmware invokes a loaded program:
    the writes of the non-volatile configuration wait until the firmware
    gets control back (FwNvramRelease).

Arguments:

    None.

Return Value:

    None.

--*/

{
    FwNvramDeferred = TRUE;
}

ARC_STATUS
FwNvramRelease (
    VOID
    )

/*++

Routine Description:

    This routine is called when the firmware gets control back from a
    loaded program, which returned or called a termination routine: the
    program, or the system it started, has given the machine back.  The
    copy is written if it changed meanwhile, and later changes are written
    at once.  A failed write is tried again, up to FW_NVRAM_WRITE_TRIES
    times: when the firmware gives the machine back to the console, it
    resets the SCSI bus first (FwRexResetScsiBus), and the first command
    the boot device gets after the reset reports the reset.  Each try and
    its result are shown on the console.

Arguments:

    None.

Return Value:

    ESUCCESS if the image was written or had not changed, otherwise EIO.

--*/

{
    ARC_STATUS Status;
    ULONG Try;

    FwNvramDeferred = FALSE;
    Status = ESUCCESS;
    if (FwNvramChanged != FALSE) {
        for (Try = 1; Try <= FW_NVRAM_WRITE_TRIES; Try += 1) {
            FwPrint(FW_NVRAM_WRITE_MSG, Try);
            Status = FwNvramFlush();
            if (Status == ESUCCESS) {
                FwPrint(FW_OK_MSG);
                FwPrint(FW_CRLF_MSG);
                break;
            }

            FwPrint(FW_NVRAM_FAILED_MSG);
        }
    }

    return Status;
}
