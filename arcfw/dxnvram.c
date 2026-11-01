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

Environment:

    Kernel mode only.

--*/

#include "fwp.h"

//
// Define the memory copy of the non-volatile configuration.
//

UCHAR FwNvram[FW_NVRAM_SIZE];

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

Arguments:

    None.

Return Value:

    ESUCCESS if the image was written, otherwise EIO.

--*/

{
    if (FwRexBootWrite(FW_NVRAM_BLOCK, FwNvram, FW_NVRAM_SIZE) != FW_NVRAM_SIZE) {
        return EIO;
    }

    return ESUCCESS;
}
