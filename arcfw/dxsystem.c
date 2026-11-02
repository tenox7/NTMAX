/*++

Module Name:

    dxsystem.c

Abstract:

    This module describes a system module of the DECstation 5000 for the
    ARC firmware: the 5000/200 (KN02), the 5000/1xx (KN02-BA) or the
    5000/240 (KN03).  It is compiled once for each module, with the define
    of the module (KN02_AA, KN02_BA or KN03_AA), and describes the module
    with the addresses, interrupt levels and vectors that decdef.h defines
    for it.  The rest of the firmware is compiled without a module define:
    FwSelftest selects the description of the module it runs on by the
    system identifier of the console.

Environment:

    Kernel mode only.

--*/

#include "fwp.h"
#include "decscc.h"

//
// Define the name of the description and what decdef.h does not define for
// the module: the system type of the console, the identifiers of the
// configuration, the vendor of the system identifier, the HAL, and the
// error register that a bus error leaves its address in.
//
// The 5000/200 keeps the HAL the system disk has as hal.dll.  The loader
// loads the HAL of the other modules by the name the firmware gives it in
// the load options.
//

#if defined(KN02_AA)

#define SYSTEM_DESCRIPTION FwSystemKn02
#define SYSTEM_TYPE REX_SYSTEM_KN02
#define SYSTEM_IDENTIFIER "DEC-KN02"
#define SYSTEM_INTERNAL_BUS "KN02-Internal Bus"
#define SYSTEM_VENDOR_ID "DEC KN02"
#define SYSTEM_HAL NULL
#define SYSTEM_ERROR_REGISTER ERROR_ADDRESS_PHYSICAL_BASE

#elif defined(KN02_BA)

#define SYSTEM_DESCRIPTION FwSystemKn02Ba
#define SYSTEM_TYPE REX_SYSTEM_KN02_BA
#define SYSTEM_IDENTIFIER "DEC-KN02-BA"
#define SYSTEM_INTERNAL_BUS "KN02-BA-Internal Bus"
#define SYSTEM_VENDOR_ID "DEC KMIN"
#define SYSTEM_HAL "hal3min.dll"
#define SYSTEM_ERROR_REGISTER TIMEOUT_PHYSICAL_BASE

#elif defined(KN03_AA)

#define SYSTEM_DESCRIPTION FwSystemKn03
#define SYSTEM_TYPE REX_SYSTEM_KN03
#define SYSTEM_IDENTIFIER "DEC-KN03"
#define SYSTEM_INTERNAL_BUS "KN03-Internal Bus"
#define SYSTEM_VENDOR_ID "DEC KN03"
#define SYSTEM_HAL "hal3maxp.dll"
#define SYSTEM_ERROR_REGISTER ERROR_ADDRESS_PHYSICAL_BASE

#endif

FW_SYSTEM SYSTEM_DESCRIPTION = {
    SYSTEM_TYPE,                        // SystemType
    SYSTEM_IDENTIFIER,                  // Identifier
    SYSTEM_INTERNAL_BUS,                // InternalBus
    SYSTEM_VENDOR_ID,                   // VendorId
    SYSTEM_HAL,                         // HalName
    TURBOCHANNEL0_PHYSICAL_BASE,        // TurboChannelBase
    TURBOCHANNEL_SLOT_SIZE,             // TurboChannelSlotSize
    3,                                  // TurboChannelSlots
    SYSTEM_ERROR_REGISTER,              // ErrorRegister
    RTC_PHYSICAL_BASE,                  // RtcPhysicalBase
    DEVICE_LEVEL,                       // DeviceLevel

#if defined(KN02_AA)

    //
    // The serial lines of the 5000/200 are the lines of its DZ, the network
    // controller is the PMAD-AA of TURBOchannel slot 6.
    //

    FwSerialDz,                         // SerialType
    { DZ_PHYSICAL_BASE, DZ_PHYSICAL_BASE }, // SerialPhysicalBase
    { DZ_VECTOR, DZ_VECTOR },           // SerialVector
    "PMAZ-AA",                          // ScsiIdentifier
    SCSI_PHYSICAL_BASE,                 // ScsiPhysicalBase
    SCSI_VECTOR,                        // ScsiVector
    SCSI_CHANNEL,                       // ScsiChannel
    "PMAD-AA",                          // EthernetIdentifier
    TRUE,                               // EthernetTurboChannel
    TURBOCHANNEL0_PHYSICAL_BASE +
        (ETHERNET_CHANNEL * TURBOCHANNEL_SLOT_SIZE), // EthernetPhysicalBase
    ETHERNET_CHANNEL                    // EthernetVector

#else

    //
    // The serial lines of the 5000/1xx and the 5000/240 are the channels of
    // their two SCCs, the network controller is the LANCE of the system
    // slot, which the IO Control ASIC gives DMA to memory.
    //

    FwSerialScc,                        // SerialType
    { SCC_PHYSICAL_BASE, SCC1_PHYSICAL_BASE }, // SerialPhysicalBase
    { DEVICE_VECTORS + SCC_CHANNEL,
      DEVICE_VECTORS + SCC1_CHANNEL },  // SerialVector
    "NCR53C94",                         // ScsiIdentifier
    SCSI_PHYSICAL_BASE,                 // ScsiPhysicalBase
    SCSI_VECTOR,                        // ScsiVector
    SCSI_CHANNEL,                       // ScsiChannel
    "AM7990",                           // EthernetIdentifier
    FALSE,                              // EthernetTurboChannel
    ETHERNET_PHYSICAL_BASE,             // EthernetPhysicalBase
    LANCE_VECTOR                        // EthernetVector

#endif

};
