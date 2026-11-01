/*++

Module Name:

    decfw.h

Abstract:

    This module defines the parts of the ARC firmware that are particular to
    the DECstation 5000/200 (KN02, MIPS R3000): the values the REX console
    hands to the firmware, the console services the firmware uses, the
    memory layout, and the image of the non-volatile configuration.

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
// Define the system identifier value of the DECstation 5000/200 and the
// mask of the processor field in it.
//

#define REX_SYSID_KN02 0x82000000
#define REX_SYSID_SYSTEM_MASK 0xff000000
#define REX_SYSID_PROCESSOR_SHIFT 24

//
// Define the size of the R3000 primary caches searched by the cache sizing
// routine.
//

#define R3000_MINIMUM_CACHE_SIZE (4 * 1024)
#define R3000_MAXIMUM_CACHE_SIZE (256 * 1024)
#define R3000_CACHE_LINE_SIZE 4
#define R3000_CACHE_FILL_SIZE 16

//
// Define the physical address and the size of a TURBOchannel slot.  The
// slots of the system module (5 SCSI, 6 Ethernet, 7 system) follow the
// three option slots.
//

#define TURBOCHANNEL_PHYSICAL_BASE 0x1e000000
#define TURBOCHANNEL_SLOT_SIZE 0x400000
#define TURBOCHANNEL_SLOT_PHYSICAL_BASE(Slot) \
    (TURBOCHANNEL_PHYSICAL_BASE + ((Slot) * TURBOCHANNEL_SLOT_SIZE))

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

//
// Define the physical address of the error address register of the KN02.
// A read from an empty TURBOchannel slot takes a bus error and leaves the
// address in the register, until a write clears it.
//

#define KN02_ERROR_ADDRESS_PHYSICAL_BASE 0x1fd80000

#if !defined(_LANGUAGE_ASSEMBLY)

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
