/*++

Module Name:

    dxlog.c

Abstract:

    This module shows on the console of the ARC firmware for the DECstation
    5000 what the firmware found on the system module it runs on: the
    module, the processor and its floating point unit, the memory and the
    caches, the modules in the TURBOchannel option slots, the station
    address of the network controller, and what the console handed the
    firmware (the arguments of the boot command and the console variables
    that pick the console).

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

//
// Define the station address ROM of the network controller.  Each of its
// bytes is a byte of a longword: the first byte for the LANCE of the system
// slot, the third byte for the PMAD-AA, whose ROM is at an offset in its
// slot.  The 32 bytes are the station address and its checksum, these
// eight bytes in reverse order, the eight bytes again, and a test pattern.
//

#define STATION_ROM_LENGTH 32
#define STATION_ROM_PMAD_OFFSET 0x1c0000

UCHAR FwStationRomPattern[8] = {0xff, 0x00, 0x55, 0xaa, 0xff, 0x00, 0x55, 0xaa};

VOID
FwPrintEthernet (
    VOID
    )

/*++

Routine Description:

    This routine shows the station address of the network controller, read
    from its station address ROM as the network driver of the system reads
    it (lance.sys), and whether the ROM holds the copies of the address and
    the test pattern, which shows that the right byte of the longwords was
    read.  On the 5000/1xx and the 5000/240 the routine shows the system
    support register of the IO Control ASIC too: its enables of the LANCE
    and of its DMA are as the console left them.

Arguments:

    None.

Return Value:

    None.

--*/

{
    ULONG Index;
    PULONG Rom;
    ULONG Shift;
    UCHAR Station[STATION_ROM_LENGTH];
    BOOLEAN Valid;
    ULONG Value;

    if (FwSystem->EthernetSystemSlot != FALSE) {
        Rom = (PULONG)(KSEG1_BASE | FwSystem->EthernetRomPhysicalBase);
        Shift = 0;

    } else {
        Rom = (PULONG)(KSEG1_BASE |
                       (FwSystem->EthernetPhysicalBase + STATION_ROM_PMAD_OFFSET));
        Shift = 16;
    }

    Valid = TRUE;
    for (Index = 0; Index < STATION_ROM_LENGTH; Index += 1) {
        if (FwProbeRead(&Rom[Index], &Value) == FALSE) {
            Valid = FALSE;
            break;
        }

        Station[Index] = (UCHAR)(Value >> Shift);
    }

    if (Valid == FALSE) {
        FwPrint(FW_ETHERNET_NO_ROM_MSG, FwSystem->EthernetIdentifier);

    } else {
        for (Index = 0; Index < 8; Index += 1) {
            if ((Station[15 - Index] != Station[Index]) ||
                (Station[16 + Index] != Station[Index]) ||
                (Station[24 + Index] != FwStationRomPattern[Index])) {
                Valid = FALSE;
            }
        }

        FwPrint(FW_ETHERNET_MSG,
                FwSystem->EthernetIdentifier,
                Station[0],
                Station[1],
                Station[2],
                Station[3],
                Station[4],
                Station[5],
                (Valid != FALSE) ? FW_ETHERNET_ROM_VALID_MSG : FW_ETHERNET_ROM_INVALID_MSG);
    }

    if (FwSystem->SystemSupportRegister != 0) {
        FwPrint(FW_SYSTEM_SUPPORT_MSG,
                *(volatile ULONG *)(KSEG1_BASE | FwSystem->SystemSupportRegister));
    }

    return;
}

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

    FwPrintEthernet();

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
