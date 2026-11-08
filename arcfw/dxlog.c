/*++

Module Name:

    dxlog.c

Abstract:

    This module shows on the console of the ARC firmware for the DECstation
    5000 what the firmware found on the system module it runs on: the
    module, the processor and its floating point unit, the memory and the
    caches, the modules in the TURBOchannel option slots, and what the
    console handed the firmware (the arguments of the boot command and the
    console variables that pick the console).

    The console of the firmware is a serial line, the printer port.  A
    terminal there that keeps what it receives (9600 baud, eight bits, no
    parity, one stop bit) has these lines, the lines of the firmware when it
    gets control back from a loaded program (dxinit.c, dxnvram.c), and what
    the HAL of the system sends to the same line: everything needed to see
    how far a system module that the firmware is brought up on got.

Environment:

    Kernel mode only.

--*/

#include "fwp.h"
#include "fwstring.h"

//
// Define the routine of the boot library that reads the implementation and
// revision registers of the processor and of its floating point unit.
//

VOID
BlQueryImplementationAndRevision (
    OUT PULONG ProcessorId,
    OUT PULONG FloatingId
    );

VOID
FwPrintSystem (
    VOID
    )

/*++

Routine Description:

    This routine shows what the firmware found on the system module.  It is
    called once the firmware is initialized: the floating point unit is
    usable then (FwExceptionInitialize), the caches have been sized
    (InitializePCR), and the TURBOchannel slots can be probed.

Arguments:

    None.

Return Value:

    None.

--*/

{
    PCHAR Console;
    ULONG FloatingId;
    ULONG Index;
    CHAR ModuleName[TURBOCHANNEL_MODULE_NAME_LENGTH + 1];
    PCHAR OsConsole;
    ULONG ProcessorId;
    ULONG Slot;

    FwPrint(FW_SYSTEM_MSG, FwSystem->Name, FwRexGetSystemId());
    BlQueryImplementationAndRevision(&ProcessorId, &FloatingId);
    FwPrint(FW_PROCESSOR_MSG, ProcessorId & 0xffff, FloatingId & 0xffff);
    FwPrint(FW_MEMORY_MSG,
            MemorySize,
            PCR->FirstLevelIcacheSize >> 10,
            PCR->FirstLevelDcacheSize >> 10);

    for (Slot = 0; Slot < FwSystem->TurboChannelSlots; Slot += 1) {
        if (FwGetTurboChannelModule(Slot, ModuleName) != FALSE) {
            FwPrint(FW_SLOT_MSG, Slot, ModuleName);

        } else {
            FwPrint(FW_SLOT_MSG, Slot, FW_SLOT_EMPTY_MSG);
        }
    }

    FwPrint(FW_CONSOLE_ARGUMENTS_MSG);
    if (FwRexArgc == 0) {
        FwPrint(" %s", FW_UNDEFINED_MSG);
    }

    for (Index = 0; Index < FwRexArgc; Index += 1) {
        FwPrint(" %.200s", FwRexArgv[Index]);
    }

    FwPrint(FW_CRLF_MSG);
    if ((Console = FwRexGetEnvironment("console")) == NULL) {
        Console = FW_UNDEFINED_MSG;
    }

    if ((OsConsole = FwRexGetEnvironment("osconsole")) == NULL) {
        OsConsole = FW_UNDEFINED_MSG;
    }

    FwPrint(FW_CONSOLE_VARIABLES_MSG, Console, OsConsole);
    return;
}
