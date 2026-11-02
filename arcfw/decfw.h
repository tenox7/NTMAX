/*++

Module Name:

    decfw.h

Abstract:

    This module defines the parts of the ARC firmware that are particular to
    the DECstation 5000 (MIPS R3000): the values the REX console hands to
    the firmware, the console services the firmware uses, the memory layout,
    the image of the non-volatile configuration, and the description of the
    system modules the firmware runs on (the 5000/200, KN02, the 5000/1xx,
    KN02-BA, and the 5000/240, KN03).

    The REX console loads the firmware from the boot block of the boot
    device at FW_BASE_ADDRESS and keeps its own work area below it.

--*/

#ifndef _DECFW_
#define _DECFW_

//
// Define the address at which the boot block loads the firmware, the size
// of the firmware stack, and the work area the console keeps below the
// firmware.
//

#define FW_BASE_ADDRESS 0x80030000
#define FW_STACK_SIZE 0x8000
#define REX_WORK_AREA_PAGES (0x30000 >> PAGE_SHIFT)

//
// Define the physical address of the PCR page, which the firmware maps at
// KiPcr with the fixed TB entry PCR_ENTRY for the HAL routines it shares
// with the system.
//

#define PCR_PHYSICAL_BASE 0x7ff000

//
// Define the magic number the REX console passes in a2.
//

#define REX_MAGIC 0x30464354

//
// Define the offsets in the console callback vector (dectccbv.h) of the
// printf service and the command service, for the start of the firmware.
//

#define REX_PRINTF_OFFSET 0x30
#define REX_COMMAND_OFFSET 0xac

//
// Define the fields of the system identifier of the console: the processor
// (bits 24 to 31) and the system module (bits 16 to 23), and the system
// modules the firmware runs on.
//

#define REX_SYSID_PROCESSOR_SHIFT 24
#define REX_SYSID_SYSTEM_SHIFT 16
#define REX_SYSID_FIELD_MASK 0xff

#define REX_SYSTEM_KN02 2               // DECstation 5000/200 (3MAX)
#define REX_SYSTEM_KN02_BA 3            // DECstation 5000/1xx (3MIN)
#define REX_SYSTEM_KN03 4               // DECstation 5000/240 (3MAX+)

//
// Define the implementation number of the R3000 in the processor
// identification register (bits 8 to 15): the firmware and the system run
// on an R3000, and not on the R4000 of the 5000/150 and 5000/260.
//

#define PRID_IMPLEMENTATION_SHIFT 8
#define PRID_R3000 2

//
// Define the size of the R3000 primary caches searched by the cache sizing
// routine.
//

#define R3000_MINIMUM_CACHE_SIZE (4 * 1024)
#define R3000_MAXIMUM_CACHE_SIZE (256 * 1024)
#define R3000_CACHE_LINE_SIZE 4
#define R3000_CACHE_FILL_SIZE 16

//
// Define the physical address of a TURBOchannel slot of the system module
// the firmware runs on.  The option slots are the first slots; on the
// 5000/200 the slots of the system module (5 SCSI, 6 Ethernet, 7 system)
// follow them.
//

#define TURBOCHANNEL_SLOT_PHYSICAL_BASE(Slot) \
    (FwSystem->TurboChannelBase + ((Slot) * FwSystem->TurboChannelSlotSize))

//
// Define the layout of the slot of the PMAG-BA color frame buffer: the
// video memory (1024 lines of 1024 pixels, 8 bits each) at the start of the
// slot, the Bt459 display controller and the register that clears the
// vertical retrace interrupt.
//

#define CFB_FRAME_OFFSET 0x0
#define CFB_FRAME_SIZE 0x100000
#define CFB_CONTROL_OFFSET 0x200000
#define CFB_CONTROL_SIZE 0x100000
#define CFB_INTERRUPT_OFFSET 0x300000

//
// Define the offsets in a TURBOchannel slot of the header of the option
// ROM: an option has its ROM at the start of the slot or at the offset of
// the prototype ROM.  The ROM has a byte in each longword.  The header has
// the width and the stride of the ROM, its test pattern (0x55 0x00 0xaa
// 0xff) and the name of the module (8 characters).
//

#define TURBOCHANNEL_ROM_OFFSET 0x3e0
#define TURBOCHANNEL_PROTOTYPE_ROM_OFFSET 0x3c03e0
#define TURBOCHANNEL_ROM_WIDTH 0x00
#define TURBOCHANNEL_ROM_STRIDE 0x04
#define TURBOCHANNEL_ROM_TEST 0x10
#define TURBOCHANNEL_ROM_MODULE_NAME 0x60
#define TURBOCHANNEL_MODULE_NAME_LENGTH 8

#if !defined(_LANGUAGE_ASSEMBLY)

//
// Define the description of a system module.  dxsystem.c is compiled once
// for each system module, with the define of decdef.h for the module, and
// describes it with the addresses, interrupt levels and vectors of
// decdef.h.  The rest of the firmware is the same for every system module
// and reads the description of the module it runs on.
//
// The serial lines of the 5000/200 are the lines of its DZ, the lines of
// the 5000/1xx and the 5000/240 the channels of their two SCCs (decscc.h).
// A bus error leaves its address in the error register of the module,
// which a write clears.
//

typedef enum _FW_SERIAL_TYPE {
    FwSerialDz,
    FwSerialScc
} FW_SERIAL_TYPE;

typedef struct _FW_SYSTEM {
    ULONG SystemType;                   // system module, REX_SYSTEM_*
    PCHAR Identifier;                   // identifier of the configuration root
    PCHAR InternalBus;                  // identifier of its internal bus
    PCHAR VendorId;                     // vendor of the system identifier
    PCHAR HalName;                      // HAL of the module, NULL for hal.dll
    ULONG TurboChannelBase;             // physical address of option slot 0
    ULONG TurboChannelSlotSize;         // size of a slot
    ULONG TurboChannelSlots;            // number of option slots
    ULONG ErrorRegister;                // physical address of the error register
    ULONG RtcPhysicalBase;              // realtime clock
    ULONG DeviceLevel;                  // interrupt level of the builtin devices
    FW_SERIAL_TYPE SerialType;          // DZ or SCC
    ULONG SerialPhysicalBase[2];        // the DZ, or SCC 0 and SCC 1
    ULONG SerialVector[2];
    PCHAR ScsiIdentifier;               // the SCSI controller
    ULONG ScsiPhysicalBase;
    ULONG ScsiVector;
    ULONG ScsiChannel;
    PCHAR EthernetIdentifier;           // the network controller
    BOOLEAN EthernetTurboChannel;       // a TURBOchannel slot, or the system slot
    ULONG EthernetPhysicalBase;
    ULONG EthernetVector;
} FW_SYSTEM, *PFW_SYSTEM;

extern PFW_SYSTEM FwSystem;

//
// Define the error register for FwProbeRead: its address in KSEG1.
//

extern ULONG FwErrorRegister;

//
// Define the addresses of decdef.h the firmware uses that are those of the
// system module it runs on.  The address of the realtime clock is also in
// a variable of its own for the clock routines of the HAL the firmware
// links (hal/halp.h).
//

extern ULONG FwRtcPhysicalBase;

#if !defined(RTC_PHYSICAL_BASE)
#define RTC_PHYSICAL_BASE FwRtcPhysicalBase
#endif

#if !defined(DZ_PHYSICAL_BASE)
#define DZ_PHYSICAL_BASE (FwSystem->SerialPhysicalBase[0])
#endif

//
// Define the size of the non-volatile configuration image and where it is
// kept on the boot device.  The DECstation has no NVRAM for it: the boot
// device keeps the image in the blocks after the firmware.
//

#define FW_NVRAM_SIZE 0x4000
#define FW_NVRAM_BLOCK 2016
#define FW_NVRAM_BLOCKS (FW_NVRAM_SIZE / 512)

extern UCHAR FwNvram[FW_NVRAM_SIZE];

//
// Define the memory map of the console: the size of a page in bytes, then
// one bit for each page that the console found good (up to 480 MB).
//

#define REX_MEMORY_MAP_SIZE (4 + 15360)

extern ULONG FwRexMemoryMap[REX_MEMORY_MAP_SIZE / 4];

//
// Define the values saved from the console at entry.
//

extern ULONG FwRexArgc;
extern PCHAR *FwRexArgv;
extern ULONG FwRexMagic;
extern PVOID FwRexVector;

//
// Define the console services.
//

LONG
FwRexBootRead (
    IN ULONG Block,
    OUT PVOID Buffer,
    IN ULONG Length
    );

LONG
FwRexBootWrite (
    IN ULONG Block,
    IN PVOID Buffer,
    IN ULONG Length
    );

PCHAR
FwRexGetEnvironment (
    IN PCHAR Name
    );

ULONG
FwRexGetSystemId (
    VOID
    );

ULONG
FwRexGetMemoryMap (
    VOID
    );

VOID
FwRexHalt (
    VOID
    );

VOID
FwRexReboot (
    VOID
    );

//
// Define the routines for the file the console boot command names
// ("boot 5/rz0/osloader"), which the boot manager starts at once.
//

BOOLEAN
FwGetConsoleBootFile (
    OUT PCHAR FileName,
    IN ULONG Length
    );

BOOLEAN
FwMatchBootFile (
    IN PCHAR PathName,
    IN PCHAR FileName
    );

//
// Define the boot device unit and the nonvolatile image routines.
//

extern ULONG FwBootUnit;

ARC_STATUS
FwNvramLoad (
    VOID
    );

ARC_STATUS
FwNvramFlush (
    VOID
    );

//
// Define the routine that reads a longword where a read may take a bus
// error, and the routine that reads the module name of a TURBOchannel slot.
//

BOOLEAN
FwProbeRead (
    IN PULONG Address,
    OUT PULONG Value
    );

BOOLEAN
FwGetTurboChannelModule (
    IN ULONG Slot,
    OUT PCHAR ModuleName
    );

//
// Define the R3000 cache routines.
//

VOID
HalpSizeCaches (
    OUT PULONG IcacheSize,
    OUT PULONG DcacheSize
    );

#endif // !defined(_LANGUAGE_ASSEMBLY)

#endif // _DECFW_
