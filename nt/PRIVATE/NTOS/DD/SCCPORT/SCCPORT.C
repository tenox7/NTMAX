/*++

Module Name:

    sccport.c

Abstract:

    This module implements the port driver of the two serial communications
    controllers (SCC, Z85C30) of the DECstation 5000/1xx and 5000/240
    (decscc.h), with the interface of the port driver of the serial lines
    of the system (dzport.h).  The channels of the SCCs are the four lines
    of the DZ multiplexer of the 5000/200, with the same numbers: the
    keyboard (line 0) is channel A of SCC 1, the mouse (line 1) channel A
    of SCC 0, the communications line (line 2) channel B of SCC 0 and the
    printer line (line 3) channel B of SCC 1.  The firmware describes the
    lines as controllers of the internal bus of the system module, each
    with the registers of its channel and the interrupt of its SCC, and
    names the device of the lines in the identifier of the keyboard and the
    pointer controllers: SCC.  Each SCC interrupts with one bit of the
    system interrupt register of the IO Control ASIC.

    A client driver connects to one line with an internal device control
    request: it gives the port its input and output services and gets the
    routines of the port.  The port calls the input service with each
    character the line receives and the output service each time the
    transmitter of the line is ready, both at the level of the interrupts
    of the SCCs; a client calls the routines of the port at that level too,
    from its services or through KeSynchronizeExecution with the interrupt
    object of the port.  The interrupt objects of the two SCCs share one
    spin lock, so either synchronizes with the whole port.

    A client that connects with receive status (the serial driver) gets
    each character with its parity, framing and overrun errors; a client
    that connects without it gets only the characters that have no parity
    or framing error.  A break that a channel receives ends with a null
    character in its receive buffer: the port gives that character with a
    framing error, which is how the DZ multiplexer receives a break.  The
    port sets the break of a line and, on the communications and printer
    lines, the modem control signals (DTR and RTS), and returns the modem
    status of those lines (CTS, DSR, CD and RI).

    The transmitter of a channel interrupts when its buffer becomes empty,
    not while it is empty.  When a client enables the transmitter of a line
    that has nothing in its buffer, a DPC of the port calls the output
    service of the client at the level of the interrupts of the SCCs, as
    the interrupt of the transmitter would.

    The communications and printer connectors have their data on channel B
    of their SCC and most of their modem signals on channel A, whose data
    lines are the keyboard or the mouse: DTR and RTS are the outputs of
    channel A, DSR and RI its SYNC and DCD inputs, and CTS and CD are the
    inputs of channel B.  The RTS output of channel B is the speed select of
    the connector, which the port does not assert.

    The kernel debugger owns line DEBUG_LINE (the d3port.c of the HAL) when
    it is enabled, and polls it at the highest level.  The port driver does
    not connect that line then and never accesses the registers of its
    channel, whose interrupts the debugger disables; the port keeps DTR and
    RTS of the line asserted, as the debugger sets them.  When the kernel
    debugger is not enabled, a client can connect the line.

    The SCCs are not reset: the kernel debugger may have set up its line.
    Each line is set up when a client connects it, and the lines no client
    has connected do not interrupt.

Environment:

    Kernel mode only.

Revision History:

--*/

#include "ntddk.h"

//
// The DECstation profiles with the interval clock: decdef.h defines the
// profile level and the profile intervals of the system in place of the
// R3000 values of ntddk.h, as the halp.h of the HAL has it.
//

#undef PROFILE_LEVEL
#undef DEFAULT_PROFILE_INTERVAL
#undef MINIMUM_PROFILE_INTERVAL

#include "decdef.h"
#include "decscc.h"
#include "dzport.h"

//
// Define the number of lines and the number of SCCs.
//

#define SCC_NUMBER_OF_LINES 4
#define SCC_NUMBER_OF_CONTROLLERS 2

//
// Define the error log values of the port.
//

#define SCCPORT_ERROR_VALUE_BASE 5000

//
// Define the time the SCC needs to recover after an access of one of its
// registers, in microseconds.
//

#define SCC_RECOVERY_TIME 2

//
// Define the baud rates of a line: the baud rate generator makes a rate
// from 50 to 115200 baud that is within one percent of the rate it is
// asked for.
//

#define SCC_MINIMUM_BAUD_RATE 50
#define SCC_MAXIMUM_BAUD_RATE 115200

//
// Define the register values of the parameters of a line: the character
// format (WR4), the receive parameters without the receive enable (WR3),
// the bits of a transmitted character (WR5), the time constant of the
// baud rate generator (WR12 and WR13), and the mask of the bits of a
// character.
//

typedef struct _SCC_PARAMETERS {
    USHORT TimeConstant;
    UCHAR CharacterFormat;
    UCHAR ReceiveParameters;
    UCHAR TransmitParameters;
    UCHAR CharacterMask;
} SCC_PARAMETERS, *PSCC_PARAMETERS;

//
// Define the state of a line.  A connected line has its transmitter
// enabled, an enabled line its receiver.  The transmit enable is set while
// the client has characters to send, and the transmitter is busy while
// a character is in its buffer.  The modem control of the communications
// and printer lines has the DTR and RTS bits of the transmit parameter
// register of channel A.
//

typedef struct _SCC_LINE {
    struct _SCC_EXTENSION *Extension;
    struct _SCC_CONTROLLER *Controller;
    ULONG LineNumber;
    PSCC_CHANNEL Channel;
    SCC_PARAMETERS Parameters;
    UCHAR ModemControl;
    BOOLEAN Connected;
    BOOLEAN Enabled;
    BOOLEAN Break;
    BOOLEAN TransmitEnable;
    BOOLEAN TransmitBusy;
    BOOLEAN BreakReceived;
    BOOLEAN BreakEnded;
    KDPC TransmitDpc;
    PVOID ClientContext;
    PDZPORT_INPUT_SERVICE InputService;
    PDZPORT_INPUT_STATUS_SERVICE InputStatusService;
    PDZPORT_OUTPUT_SERVICE OutputService;
} SCC_LINE, *PSCC_LINE;

//
// Define the state of an SCC: its registers, its interrupt object and the
// lines of its channels.  Only channel A has the interrupt pending
// register.
//

typedef struct _SCC_CONTROLLER {
    PVOID Registers;
    BOOLEAN Mapped;
    PKINTERRUPT Interrupt;
    PSCC_LINE ChannelA;
    PSCC_LINE ChannelB;
} SCC_CONTROLLER, *PSCC_CONTROLLER;

//
// Define the device extension of the port.
//

typedef struct _SCC_EXTENSION {
    PDEVICE_OBJECT DeviceObject;
    KSPIN_LOCK InterruptSpinLock;
    SCC_CONTROLLER Controller[SCC_NUMBER_OF_CONTROLLERS];
    SCC_LINE Line[SCC_NUMBER_OF_LINES];
    ULONG ReceiveErrors;
    BOOLEAN DebuggerEnabled;
} SCC_EXTENSION, *PSCC_EXTENSION;

//
// Define the context of a connect or disconnect request that is
// synchronized with the interrupts of the port.
//

typedef struct _SCC_REQUEST {
    PSCC_LINE Line;
    PDZPORT_CONNECT Connect;
    PDZPORT_INPUT_STATUS_SERVICE InputStatusService;
    SCC_PARAMETERS Parameters;
} SCC_REQUEST, *PSCC_REQUEST;

//
// Define the resources of the SCCs that the firmware describes: the
// registers and the interrupt of each SCC, from the controller of the
// line of its channel A.
//

typedef struct _SCC_CONFIGURATION {
    ULONG LineNumber;
    BOOLEAN Found[SCC_NUMBER_OF_CONTROLLERS];
    INTERFACE_TYPE InterfaceType;
    ULONG BusNumber;
    CM_PARTIAL_RESOURCE_DESCRIPTOR Port[SCC_NUMBER_OF_CONTROLLERS];
    CM_PARTIAL_RESOURCE_DESCRIPTOR Interrupt[SCC_NUMBER_OF_CONTROLLERS];
} SCC_CONFIGURATION, *PSCC_CONFIGURATION;

//
// Define the controllers of the internal bus that are the lines of
// channel A of the SCCs, the keyboard of SCC 1 and the mouse of SCC 0.
//

typedef struct _SCC_CONTROLLER_LINE {
    CONFIGURATION_TYPE ControllerType;
    ULONG LineNumber;
} SCC_CONTROLLER_LINE, *PSCC_CONTROLLER_LINE;

SCC_CONTROLLER_LINE SccpControllerLines[] = {
    { KeyboardController, DZPORT_KEYBOARD_LINE },
    { PointerController, DZPORT_MOUSE_LINE }
};

//
// Define the bits of a received character and of a transmitted character
// in the receive and the transmit parameter registers, by the length of
// the character less five.
//

UCHAR SccpReceiveBits[] = {
    SCC_WR3_RECEIVE_5_BITS,
    SCC_WR3_RECEIVE_6_BITS,
    SCC_WR3_RECEIVE_7_BITS,
    SCC_WR3_RECEIVE_8_BITS
};

UCHAR SccpTransmitBits[] = {
    SCC_WR5_TRANSMIT_5_BITS,
    SCC_WR5_TRANSMIT_6_BITS,
    SCC_WR5_TRANSMIT_7_BITS,
    SCC_WR5_TRANSMIT_8_BITS
};

NTSTATUS
DriverEntry(
    IN PDRIVER_OBJECT DriverObject,
    IN PUNICODE_STRING RegistryPath
    );

NTSTATUS
SccpConfigurationCallout(
    IN PVOID Context,
    IN PUNICODE_STRING PathName,
    IN INTERFACE_TYPE BusType,
    IN ULONG BusNumber,
    IN PKEY_VALUE_FULL_INFORMATION *BusInformation,
    IN CONFIGURATION_TYPE ControllerType,
    IN ULONG ControllerNumber,
    IN PKEY_VALUE_FULL_INFORMATION *ControllerInformation,
    IN CONFIGURATION_TYPE PeripheralType,
    IN ULONG PeripheralNumber,
    IN PKEY_VALUE_FULL_INFORMATION *PeripheralInformation
    );

VOID
SccpLogError(
    IN PVOID IoObject,
    IN NTSTATUS ErrorCode,
    IN ULONG UniqueErrorValue,
    IN NTSTATUS FinalStatus
    );

VOID
SccpReleaseResources(
    IN PSCC_EXTENSION Extension
    );

NTSTATUS
SccpDispatch(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp
    );

BOOLEAN
SccpInitializeControllers(
    IN PVOID Context
    );

BOOLEAN
SccpInterruptService(
    IN PKINTERRUPT Interrupt,
    IN PVOID Context
    );

VOID
SccpReceiveInterrupt(
    IN PSCC_LINE Line
    );

VOID
SccpTransmitInterrupt(
    IN PSCC_LINE Line
    );

VOID
SccpExternalStatusInterrupt(
    IN PSCC_LINE Line
    );

VOID
SccpTransmitDpc(
    IN PKDPC Dpc,
    IN PVOID DeferredContext,
    IN PVOID SystemArgument1,
    IN PVOID SystemArgument2
    );

BOOLEAN
SccpStartTransmit(
    IN PVOID Context
    );

BOOLEAN
SccpConnectLine(
    IN PVOID Context
    );

BOOLEAN
SccpDisconnectLine(
    IN PVOID Context
    );

BOOLEAN
SccpLineParameters(
    IN ULONG BaudRate,
    IN ULONG CharacterLength,
    IN ULONG StopBits,
    IN ULONG Polarity,
    IN BOOLEAN ParityEnable,
    OUT PSCC_PARAMETERS Parameters
    );

VOID
SccpLoadParameters(
    IN PSCC_LINE Line
    );

UCHAR
SccpTransmitControl(
    IN PSCC_LINE Line
    );

VOID
SccpWriteInterruptEnables(
    IN PSCC_LINE Line
    );

VOID
SccpEnableLine(
    IN PVOID PortContext
    );

VOID
SccpDisableLine(
    IN PVOID PortContext
    );

VOID
SccpEnableTransmit(
    IN PVOID PortContext
    );

VOID
SccpOutputByte(
    IN PVOID PortContext,
    IN UCHAR OutputByte
    );

BOOLEAN
SccpSetLineParameters(
    IN PVOID PortContext,
    IN ULONG BaudRate,
    IN ULONG CharacterLength,
    IN ULONG StopBits,
    IN ULONG Polarity,
    IN BOOLEAN ParityEnable
    );

VOID
SccpSetModemControl(
    IN PVOID PortContext,
    IN ULONG ModemControl
    );

ULONG
SccpQueryModemControl(
    IN PVOID PortContext
    );

ULONG
SccpQueryModemStatus(
    IN PVOID PortContext
    );

VOID
SccpSetBreak(
    IN PVOID PortContext,
    IN BOOLEAN Break
    );

UCHAR
SccpReadRegister(
    IN PSCC_CHANNEL Channel,
    IN ULONG Register
    );

VOID
SccpWriteRegister(
    IN PSCC_CHANNEL Channel,
    IN ULONG Register,
    IN UCHAR Value
    );

#ifdef ALLOC_PRAGMA
#pragma alloc_text(INIT,DriverEntry)
#pragma alloc_text(INIT,SccpConfigurationCallout)
#endif

NTSTATUS
DriverEntry(
    IN PDRIVER_OBJECT DriverObject,
    IN PUNICODE_STRING RegistryPath
    )

/*++

Routine Description:

    This routine initializes the SCC port driver: it finds the SCCs in the
    configuration that the firmware describes, reports their registers and
    their interrupts as the resources of the driver, creates the port
    device \Device\DzPort, maps the registers, connects the interrupts and
    enables the interrupts of the SCCs.  A system module that has no SCCs
    (the 5000/200 has a DZ multiplexer, dzport.c) has no controller with
    the identifier SCC, and the driver does not load.

Arguments:

    DriverObject - Supplies a pointer to the driver object of the port.

    RegistryPath - Supplies the path of the service key of the driver.

Return Value:

    The status of the initialization.

--*/

{

    KAFFINITY Affinity[SCC_NUMBER_OF_CONTROLLERS];
    SCC_CONFIGURATION Configuration;
    BOOLEAN Conflict;
    PSCC_CONTROLLER Controller;
    CONFIGURATION_TYPE ControllerType;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor;
    PDEVICE_OBJECT DeviceObject;
    PSCC_EXTENSION Extension;
    ULONG Index;
    INTERFACE_TYPE InterfaceType;
    KIRQL Irql[SCC_NUMBER_OF_CONTROLLERS];
    PSCC_LINE Line;
    UNICODE_STRING Name;
    PHYSICAL_ADDRESS PortAddress;
    ULONG PortSpace;
    PCM_RESOURCE_LIST ResourceList;
    ULONG ResourceListSize;
    NTSTATUS Status;
    KIRQL SynchronizeIrql;
    ULONG Vector[SCC_NUMBER_OF_CONTROLLERS];

    UNREFERENCED_PARAMETER(RegistryPath);

    //
    // Find the SCCs: the registers and the interrupt of each, from the
    // controller of the line of its channel A.
    //

    RtlZeroMemory(&Configuration, sizeof(SCC_CONFIGURATION));
    for (Index = 0;
         Index < sizeof(SccpControllerLines) / sizeof(SCC_CONTROLLER_LINE);
         Index += 1) {

        InterfaceType = Internal;
        ControllerType = SccpControllerLines[Index].ControllerType;
        Configuration.LineNumber = SccpControllerLines[Index].LineNumber;
        IoQueryDeviceDescription(&InterfaceType, NULL, &ControllerType, NULL,
            NULL, NULL, SccpConfigurationCallout, &Configuration);
    }

    if ((Configuration.Found[0] == FALSE) && (Configuration.Found[1] == FALSE)) {
        return STATUS_NO_SUCH_DEVICE;
    }

    if ((Configuration.Found[0] == FALSE) || (Configuration.Found[1] == FALSE)) {
        SccpLogError(DriverObject, IO_ERR_CONFIGURATION_ERROR,
            SCCPORT_ERROR_VALUE_BASE + 1, STATUS_NO_SUCH_DEVICE);
        return STATUS_NO_SUCH_DEVICE;
    }

    //
    // Report the registers and the interrupts of the SCCs as the resources
    // of the driver.
    //

    ResourceListSize = sizeof(CM_RESOURCE_LIST) +
        ((SCC_NUMBER_OF_CONTROLLERS * 2) - 1) *
            sizeof(CM_PARTIAL_RESOURCE_DESCRIPTOR);

    ResourceList = ExAllocatePool(PagedPool, ResourceListSize);
    if (ResourceList == NULL) {
        SccpLogError(DriverObject, IO_ERR_INSUFFICIENT_RESOURCES,
            SCCPORT_ERROR_VALUE_BASE + 2, STATUS_INSUFFICIENT_RESOURCES);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(ResourceList, ResourceListSize);
    ResourceList->Count = 1;
    ResourceList->List[0].InterfaceType = Configuration.InterfaceType;
    ResourceList->List[0].BusNumber = Configuration.BusNumber;
    ResourceList->List[0].PartialResourceList.Count =
        SCC_NUMBER_OF_CONTROLLERS * 2;

    Descriptor = ResourceList->List[0].PartialResourceList.PartialDescriptors;
    for (Index = 0; Index < SCC_NUMBER_OF_CONTROLLERS; Index += 1) {
        *Descriptor = Configuration.Port[Index];
        Descriptor->ShareDisposition = CmResourceShareDeviceExclusive;
        Descriptor += 1;
        *Descriptor = Configuration.Interrupt[Index];
        Descriptor->ShareDisposition = CmResourceShareDeviceExclusive;
        Descriptor += 1;
    }

    Status = IoReportResourceUsage(NULL, DriverObject, ResourceList,
        ResourceListSize, NULL, NULL, 0, FALSE, &Conflict);

    ExFreePool(ResourceList);
    if (!NT_SUCCESS(Status) || (Conflict != FALSE)) {
        SccpLogError(DriverObject, IO_ERR_PORT_RESOURCE_CONFLICT,
            SCCPORT_ERROR_VALUE_BASE + 3, STATUS_INSUFFICIENT_RESOURCES);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    //
    // Create the port device.
    //

    RtlInitUnicodeString(&Name, L"\\Device\\DzPort");
    Status = IoCreateDevice(DriverObject, sizeof(SCC_EXTENSION), &Name,
        FILE_DEVICE_SERIAL_PORT, 0, FALSE, &DeviceObject);

    if (!NT_SUCCESS(Status)) {
        SccpLogError(DriverObject, IO_ERR_INSUFFICIENT_RESOURCES,
            SCCPORT_ERROR_VALUE_BASE + 4, Status);
        return Status;
    }

    DeviceObject->Flags |= DO_BUFFERED_IO;
    Extension = (PSCC_EXTENSION)DeviceObject->DeviceExtension;
    RtlZeroMemory(Extension, sizeof(SCC_EXTENSION));
    Extension->DeviceObject = DeviceObject;
    KeInitializeSpinLock(&Extension->InterruptSpinLock);

    //
    // The kernel debugger owns its line when it is enabled.
    //
    // N.B. The kernel exports KdDebuggerEnabled as a constant: the
    //      external is the address of the variable of the kernel.
    //

    Extension->DebuggerEnabled = *((PBOOLEAN)(*(PULONG)&KdDebuggerEnabled));

    //
    // Map the registers of each SCC.
    //

    for (Index = 0; Index < SCC_NUMBER_OF_CONTROLLERS; Index += 1) {
        Controller = &Extension->Controller[Index];
        PortSpace = 1;
        if (HalTranslateBusAddress(Configuration.InterfaceType,
                Configuration.BusNumber, Configuration.Port[Index].u.Port.Start,
                &PortSpace, &PortAddress) == FALSE) {

            SccpLogError(DeviceObject, IO_ERR_INVALID_IOBASE,
                SCCPORT_ERROR_VALUE_BASE + 5, STATUS_NO_SUCH_DEVICE);
            SccpReleaseResources(Extension);
            return STATUS_NO_SUCH_DEVICE;
        }

        if (PortSpace == 0) {
            Controller->Registers = MmMapIoSpace(PortAddress, SCC_SIZE, FALSE);
            Controller->Mapped = TRUE;

        } else {
            Controller->Registers = (PVOID)PortAddress.LowPart;
        }

        if (Controller->Registers == NULL) {
            Controller->Mapped = FALSE;
            SccpLogError(DeviceObject, IO_ERR_INSUFFICIENT_RESOURCES,
                SCCPORT_ERROR_VALUE_BASE + 6, STATUS_INSUFFICIENT_RESOURCES);
            SccpReleaseResources(Extension);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
    }

    //
    // Set up the lines: each is a channel of one of the SCCs.  The
    // parameters of a line are those of the keyboard and the mouse, 4800
    // baud with eight bit characters, until a client connects it.
    //

    for (Index = 0; Index < SCC_NUMBER_OF_LINES; Index += 1) {
        Line = &Extension->Line[Index];
        Controller = &Extension->Controller[SCC_LINE_SCC(Index)];
        Line->Extension = Extension;
        Line->Controller = Controller;
        Line->LineNumber = Index;
        Line->Channel = (PSCC_CHANNEL)((PUCHAR)Controller->Registers +
                                       SCC_LINE_OFFSET(Index));

        if (SCC_LINE_OFFSET(Index) == SCC_CHANNEL_A_OFFSET) {
            Controller->ChannelA = Line;

        } else {
            Controller->ChannelB = Line;
        }

        SccpLineParameters(4800, 8, 1, 0, FALSE, &Line->Parameters);
        KeInitializeDpc(&Line->TransmitDpc, SccpTransmitDpc, Line);
    }

    if (Extension->DebuggerEnabled != FALSE) {
        Extension->Line[DEBUG_LINE].ModemControl = SCC_WR5_DTR | SCC_WR5_RTS;
    }

    //
    // Connect the interrupts of the SCCs, with one spin lock and the higher
    // of their levels as the synchronize level.
    //

    SynchronizeIrql = 0;
    for (Index = 0; Index < SCC_NUMBER_OF_CONTROLLERS; Index += 1) {
        Vector[Index] = HalGetInterruptVector(Configuration.InterfaceType,
            Configuration.BusNumber,
            Configuration.Interrupt[Index].u.Interrupt.Level,
            Configuration.Interrupt[Index].u.Interrupt.Vector,
            &Irql[Index], &Affinity[Index]);

        if (Irql[Index] > SynchronizeIrql) {
            SynchronizeIrql = Irql[Index];
        }
    }

    for (Index = 0; Index < SCC_NUMBER_OF_CONTROLLERS; Index += 1) {
        Controller = &Extension->Controller[Index];
        Status = IoConnectInterrupt(&Controller->Interrupt,
            SccpInterruptService, Controller, &Extension->InterruptSpinLock,
            Vector[Index], Irql[Index], SynchronizeIrql,
            ((Configuration.Interrupt[Index].Flags &
              CM_RESOURCE_INTERRUPT_LATCHED) != 0) ? Latched : LevelSensitive,
            FALSE, Affinity[Index], FALSE);

        if (!NT_SUCCESS(Status)) {
            Controller->Interrupt = NULL;
            SccpLogError(DeviceObject, IO_ERR_INTERRUPT_RESOURCE_CONFLICT,
                SCCPORT_ERROR_VALUE_BASE + 7, Status);
            SccpReleaseResources(Extension);
            return Status;
        }
    }

    DriverObject->MajorFunction[IRP_MJ_CREATE] = SccpDispatch;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = SccpDispatch;
    DriverObject->MajorFunction[IRP_MJ_CLEANUP] = SccpDispatch;
    DriverObject->MajorFunction[IRP_MJ_INTERNAL_DEVICE_CONTROL] = SccpDispatch;

    //
    // Disable the interrupts of the lines and enable the interrupts of the
    // SCCs.
    //

    KeSynchronizeExecution(Extension->Controller[0].Interrupt,
        SccpInitializeControllers, Extension);

    return STATUS_SUCCESS;
}

NTSTATUS
SccpConfigurationCallout(
    IN PVOID Context,
    IN PUNICODE_STRING PathName,
    IN INTERFACE_TYPE BusType,
    IN ULONG BusNumber,
    IN PKEY_VALUE_FULL_INFORMATION *BusInformation,
    IN CONFIGURATION_TYPE ControllerType,
    IN ULONG ControllerNumber,
    IN PKEY_VALUE_FULL_INFORMATION *ControllerInformation,
    IN CONFIGURATION_TYPE PeripheralType,
    IN ULONG PeripheralNumber,
    IN PKEY_VALUE_FULL_INFORMATION *PeripheralInformation
    )

/*++

Routine Description:

    This routine is the callout of IoQueryDeviceDescription for the
    controller of the line of channel A of an SCC: if the identifier of the
    controller is SCC, it takes the registers of the SCC and its interrupt
    from the configuration data of the controller, which describes the
    registers of the channel.

Arguments:

    Context - Supplies a pointer to the configuration of the port, with the
        line of the controller.

    PathName - Supplies the path of the key of the controller.

    BusType, BusNumber - Supply the bus of the controller.

    BusInformation - Supplies the information of the bus.

    ControllerType, ControllerNumber - Supply the controller.

    ControllerInformation - Supplies the information of the controller.

    PeripheralType, PeripheralNumber, PeripheralInformation - Not used.

Return Value:

    STATUS_SUCCESS.

--*/

{

    PSCC_CONFIGURATION Configuration;
    PKEY_VALUE_FULL_INFORMATION Data;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor;
    PKEY_VALUE_FULL_INFORMATION Identifier;
    UNICODE_STRING IdentifierName;
    ULONG Index;
    PCM_PARTIAL_RESOURCE_LIST List;
    BOOLEAN Interrupt;
    BOOLEAN Port;
    ULONG Scc;
    UNICODE_STRING SccName;

    UNREFERENCED_PARAMETER(PathName);
    UNREFERENCED_PARAMETER(BusInformation);
    UNREFERENCED_PARAMETER(ControllerType);
    UNREFERENCED_PARAMETER(ControllerNumber);
    UNREFERENCED_PARAMETER(PeripheralType);
    UNREFERENCED_PARAMETER(PeripheralNumber);
    UNREFERENCED_PARAMETER(PeripheralInformation);

    Configuration = (PSCC_CONFIGURATION)Context;
    Scc = SCC_LINE_SCC(Configuration->LineNumber);
    Data = ControllerInformation[IoQueryDeviceConfigurationData];
    Identifier = ControllerInformation[IoQueryDeviceIdentifier];
    if ((Configuration->Found[Scc] != FALSE) || (Data == NULL) ||
        (Data->DataLength == 0) || (Identifier == NULL)) {
        return STATUS_SUCCESS;
    }

    //
    // The identifier is a string that ends with a null.
    //

    IdentifierName.Buffer = (PWSTR)((PUCHAR)Identifier + Identifier->DataOffset);
    IdentifierName.Length = (USHORT)Identifier->DataLength;
    IdentifierName.MaximumLength = IdentifierName.Length;
    while ((IdentifierName.Length >= sizeof(WCHAR)) &&
           (IdentifierName.Buffer[(IdentifierName.Length / sizeof(WCHAR)) - 1] ==
                UNICODE_NULL)) {
        IdentifierName.Length -= sizeof(WCHAR);
    }

    RtlInitUnicodeString(&SccName, L"SCC");
    if (RtlEqualUnicodeString(&IdentifierName, &SccName, TRUE) == FALSE) {
        return STATUS_SUCCESS;
    }

    List = (PCM_PARTIAL_RESOURCE_LIST)((PUCHAR)Data + Data->DataOffset +
        FIELD_OFFSET(CM_FULL_RESOURCE_DESCRIPTOR, PartialResourceList));

    Interrupt = FALSE;
    Port = FALSE;
    Descriptor = List->PartialDescriptors;
    for (Index = 0; Index < List->Count; Index += 1, Descriptor += 1) {
        switch (Descriptor->Type) {
        case CmResourceTypePort:
            Configuration->Port[Scc] = *Descriptor;
            Port = TRUE;
            break;

        case CmResourceTypeInterrupt:
            Configuration->Interrupt[Scc] = *Descriptor;
            Interrupt = TRUE;
            break;

        default:
            break;
        }
    }

    //
    // The registers of the SCC start with channel B.
    //

    if ((Port != FALSE) && (Interrupt != FALSE)) {
        Configuration->Port[Scc].u.Port.Start.LowPart -=
            SCC_LINE_OFFSET(Configuration->LineNumber);
        Configuration->Port[Scc].u.Port.Length = SCC_SIZE;
        Configuration->InterfaceType = BusType;
        Configuration->BusNumber = BusNumber;
        Configuration->Found[Scc] = TRUE;
    }

    return STATUS_SUCCESS;
}

VOID
SccpLogError(
    IN PVOID IoObject,
    IN NTSTATUS ErrorCode,
    IN ULONG UniqueErrorValue,
    IN NTSTATUS FinalStatus
    )

/*++

Routine Description:

    This routine writes an entry of the error log.

Arguments:

    IoObject - Supplies a pointer to the driver object or the device
        object the entry is about.

    ErrorCode - Supplies the error code (ntiologc.h).

    UniqueErrorValue - Supplies the value that tells where the error was
        found.

    FinalStatus - Supplies the status the operation ends with.

Return Value:

    None.

--*/

{

    PIO_ERROR_LOG_PACKET Entry;

    Entry = (PIO_ERROR_LOG_PACKET)IoAllocateErrorLogEntry(IoObject,
        sizeof(IO_ERROR_LOG_PACKET));

    if (Entry != NULL) {
        RtlZeroMemory(Entry, sizeof(IO_ERROR_LOG_PACKET));
        Entry->ErrorCode = ErrorCode;
        Entry->UniqueErrorValue = UniqueErrorValue;
        Entry->FinalStatus = FinalStatus;
        IoWriteErrorLogEntry(Entry);
    }

    return;
}

VOID
SccpReleaseResources(
    IN PSCC_EXTENSION Extension
    )

/*++

Routine Description:

    This routine releases what an initialization that fails has taken: it
    disconnects the interrupts that are connected, unmaps the registers
    that are mapped, and deletes the port device.

Arguments:

    Extension - Supplies a pointer to the device extension of the port.

Return Value:

    None.

--*/

{

    PSCC_CONTROLLER Controller;
    ULONG Index;

    for (Index = 0; Index < SCC_NUMBER_OF_CONTROLLERS; Index += 1) {
        Controller = &Extension->Controller[Index];
        if (Controller->Interrupt != NULL) {
            IoDisconnectInterrupt(Controller->Interrupt);
        }

        if (Controller->Mapped != FALSE) {
            MmUnmapIoSpace(Controller->Registers, SCC_SIZE);
        }
    }

    IoDeleteDevice(Extension->DeviceObject);
    return;
}

BOOLEAN
SccpInitializeControllers(
    IN PVOID Context
    )

/*++

Routine Description:

    This routine disables the interrupts of the lines, except the line of
    the kernel debugger when it owns it, and enables the interrupts of the
    SCCs, without vectors.  The SCCs interrupt for the lines that clients
    connect.  It is called at the level of the interrupts of the port.

Arguments:

    Context - Supplies a pointer to the device extension of the port.

Return Value:

    TRUE.

--*/

{

    PSCC_EXTENSION Extension;
    ULONG Index;
    PSCC_LINE Line;

    Extension = (PSCC_EXTENSION)Context;
    for (Index = 0; Index < SCC_NUMBER_OF_LINES; Index += 1) {
        Line = &Extension->Line[Index];
        if ((Line->LineNumber == DEBUG_LINE) &&
            (Extension->DebuggerEnabled != FALSE)) {
            continue;
        }

        SccpWriteInterruptEnables(Line);
        SccpWriteRegister(Line->Channel, SCC_WR15, 0);
        SccpWriteRegister(Line->Channel, SCC_WR0, SCC_WR0_RESET_TRANSMIT_PENDING);
        SccpWriteRegister(Line->Channel, SCC_WR0, SCC_WR0_RESET_EXTERNAL_STATUS);
        SccpWriteRegister(Line->Channel, SCC_WR5, SccpTransmitControl(Line));
    }

    for (Index = 0; Index < SCC_NUMBER_OF_CONTROLLERS; Index += 1) {
        SccpWriteRegister(Extension->Controller[Index].ChannelA->Channel,
            SCC_WR9, SCC_WR9_MASTER_ENABLE | SCC_WR9_NO_VECTOR);
    }

    return TRUE;
}

NTSTATUS
SccpDispatch(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp
    )

/*++

Routine Description:

    This routine is the dispatch routine of the port: opening and closing
    the port succeed, and the internal device control requests connect and
    disconnect a line.

Arguments:

    DeviceObject - Supplies a pointer to the port device object.

    Irp - Supplies a pointer to the request packet.

Return Value:

    The status of the request.

--*/

{

    PDZPORT_ACCEPT Accept;
    PDZPORT_CONNECT Connect;
    PSCC_EXTENSION Extension;
    BOOLEAN InputValid;
    PIO_STACK_LOCATION IrpSp;
    ULONG LineNumber;
    SCC_REQUEST Request;
    NTSTATUS Status;

    Extension = (PSCC_EXTENSION)DeviceObject->DeviceExtension;
    IrpSp = IoGetCurrentIrpStackLocation(Irp);
    Irp->IoStatus.Information = 0;
    switch (IrpSp->MajorFunction) {
    case IRP_MJ_CREATE:
    case IRP_MJ_CLOSE:
    case IRP_MJ_CLEANUP:
        Status = STATUS_SUCCESS;
        break;

    case IRP_MJ_INTERNAL_DEVICE_CONTROL:
        switch (IrpSp->Parameters.DeviceIoControl.IoControlCode) {

        //
        // Connect a line: check the line and its parameters, set up the
        // channel of the line and enable its receiver and transmitter, and
        // return the routines of the port.  A connect with receive status
        // gives the port an input status service in place of the input
        // service.  A client whose accept message ends with the interrupt
        // object gets the routines it has room for.
        //

        case IOCTL_MN_DZPORT_CONNECT:
        case IOCTL_MN_DZPORT_CONNECT_STATUS:
            Connect = (PDZPORT_CONNECT)Irp->AssociatedIrp.SystemBuffer;
            if (IrpSp->Parameters.DeviceIoControl.IoControlCode ==
                    IOCTL_MN_DZPORT_CONNECT_STATUS) {
                if ((IrpSp->Parameters.DeviceIoControl.InputBufferLength <
                        sizeof(DZPORT_CONNECT_STATUS)) ||
                    (IrpSp->Parameters.DeviceIoControl.OutputBufferLength <
                        sizeof(DZPORT_ACCEPT))) {
                    Status = STATUS_BUFFER_TOO_SMALL;
                    break;
                }

                Request.InputStatusService =
                    ((PDZPORT_CONNECT_STATUS)Connect)->InputStatusService;

                InputValid = (BOOLEAN)(Request.InputStatusService != NULL);

            } else {
                if ((IrpSp->Parameters.DeviceIoControl.InputBufferLength <
                        sizeof(DZPORT_CONNECT)) ||
                    (IrpSp->Parameters.DeviceIoControl.OutputBufferLength <
                        FIELD_OFFSET(DZPORT_ACCEPT, SetModemControl))) {
                    Status = STATUS_BUFFER_TOO_SMALL;
                    break;
                }

                Request.InputStatusService = NULL;
                InputValid = (BOOLEAN)(Connect->InputService != NULL);
            }

            LineNumber = Connect->LineNumber;
            if ((LineNumber >= SCC_NUMBER_OF_LINES) ||
                (InputValid == FALSE) ||
                (Connect->OutputService == NULL) ||
                (SccpLineParameters(Connect->BaudRate,
                                    Connect->CharacterLength,
                                    Connect->StopBits,
                                    Connect->Polarity,
                                    Connect->ParityEnable,
                                    &Request.Parameters) == FALSE)) {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            //
            // The kernel debugger owns its line when it is enabled.
            //

            if ((LineNumber == DEBUG_LINE) &&
                (Extension->DebuggerEnabled != FALSE)) {
                Status = STATUS_DEVICE_BUSY;
                break;
            }

            Request.Line = &Extension->Line[LineNumber];
            Request.Connect = Connect;
            if (KeSynchronizeExecution(Request.Line->Controller->Interrupt,
                    SccpConnectLine, &Request) == FALSE) {
                Status = STATUS_DEVICE_ALREADY_ATTACHED;
                break;
            }

            //
            // The accept message goes in the same buffer as the connect
            // message.
            //

            Accept = (PDZPORT_ACCEPT)Irp->AssociatedIrp.SystemBuffer;
            Accept->PortContext = Request.Line;
            Accept->EnableLine = SccpEnableLine;
            Accept->DisableLine = SccpDisableLine;
            Accept->EnableTransmit = SccpEnableTransmit;
            Accept->OutputByte = SccpOutputByte;
            Accept->SetLineParameters = SccpSetLineParameters;
            Accept->Interrupt = Request.Line->Controller->Interrupt;
            if (IrpSp->Parameters.DeviceIoControl.OutputBufferLength >=
                    sizeof(DZPORT_ACCEPT)) {
                Accept->SetModemControl = SccpSetModemControl;
                Accept->QueryModemControl = SccpQueryModemControl;
                Accept->QueryModemStatus = SccpQueryModemStatus;
                Accept->SetBreak = SccpSetBreak;
                Irp->IoStatus.Information = sizeof(DZPORT_ACCEPT);

            } else {
                Irp->IoStatus.Information =
                    FIELD_OFFSET(DZPORT_ACCEPT, SetModemControl);
            }

            Status = STATUS_SUCCESS;
            break;

        //
        // Disconnect a line: disable its receiver, its transmitter and its
        // interrupts.  The line of the kernel debugger is not connected
        // while the debugger is enabled, so a disconnect of it has no
        // effect then.
        //

        case IOCTL_MN_DZPORT_DISCONNECT:
            if (IrpSp->Parameters.DeviceIoControl.InputBufferLength <
                    sizeof(DZPORT_DISCONNECT)) {
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            LineNumber =
                ((PDZPORT_DISCONNECT)Irp->AssociatedIrp.SystemBuffer)->LineNumber;
            if (LineNumber >= SCC_NUMBER_OF_LINES) {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            Request.Line = &Extension->Line[LineNumber];
            KeSynchronizeExecution(Request.Line->Controller->Interrupt,
                SccpDisconnectLine, &Request);

            Status = STATUS_SUCCESS;
            break;

        default:
            Status = STATUS_INVALID_DEVICE_REQUEST;
            break;
        }

        break;

    default:
        Status = STATUS_INVALID_DEVICE_REQUEST;
        break;
    }

    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

BOOLEAN
SccpInterruptService(
    IN PKINTERRUPT Interrupt,
    IN PVOID Context
    )

/*++

Routine Description:

    This routine is the interrupt service routine of an SCC.  It reads the
    interrupt pending register of the SCC and serves the receivers that
    have characters, the transmitters whose buffers became empty and the
    external status changes of its channels, channel A first, until no
    interrupt is pending, and resets the highest interrupt under service of
    each channel it served.  The interrupts of the line of the kernel
    debugger are disabled, and its channel is not accessed.

Arguments:

    Interrupt - Supplies a pointer to the interrupt object of the SCC.

    Context - Supplies a pointer to the state of the SCC.

Return Value:

    TRUE if the SCC had a pending interrupt, FALSE otherwise.

--*/

{

    PSCC_CONTROLLER Controller;
    UCHAR Pending;
    BOOLEAN Serviced;

    UNREFERENCED_PARAMETER(Interrupt);

    Controller = (PSCC_CONTROLLER)Context;
    Serviced = FALSE;
    for (;;) {
        Pending = SccpReadRegister(Controller->ChannelA->Channel, SCC_RR3);
        if ((Controller->ChannelB->LineNumber == DEBUG_LINE) &&
            (Controller->ChannelB->Extension->DebuggerEnabled != FALSE)) {
            Pending &= ~(SCC_RR3_B_EXTERNAL | SCC_RR3_B_TRANSMIT |
                         SCC_RR3_B_RECEIVE);
        }

        Pending &= SCC_RR3_A_EXTERNAL | SCC_RR3_A_TRANSMIT | SCC_RR3_A_RECEIVE |
                   SCC_RR3_B_EXTERNAL | SCC_RR3_B_TRANSMIT | SCC_RR3_B_RECEIVE;

        if (Pending == 0) {
            break;
        }

        Serviced = TRUE;
        if ((Pending & SCC_RR3_A_RECEIVE) != 0) {
            SccpReceiveInterrupt(Controller->ChannelA);
        }

        if ((Pending & SCC_RR3_A_EXTERNAL) != 0) {
            SccpExternalStatusInterrupt(Controller->ChannelA);
        }

        if ((Pending & SCC_RR3_A_TRANSMIT) != 0) {
            SccpTransmitInterrupt(Controller->ChannelA);
        }

        if ((Pending & (SCC_RR3_A_EXTERNAL | SCC_RR3_A_TRANSMIT |
                        SCC_RR3_A_RECEIVE)) != 0) {
            SccpWriteRegister(Controller->ChannelA->Channel, SCC_WR0,
                SCC_WR0_RESET_HIGHEST_IUS);
        }

        if ((Pending & SCC_RR3_B_RECEIVE) != 0) {
            SccpReceiveInterrupt(Controller->ChannelB);
        }

        if ((Pending & SCC_RR3_B_EXTERNAL) != 0) {
            SccpExternalStatusInterrupt(Controller->ChannelB);
        }

        if ((Pending & SCC_RR3_B_TRANSMIT) != 0) {
            SccpTransmitInterrupt(Controller->ChannelB);
        }

        if ((Pending & (SCC_RR3_B_EXTERNAL | SCC_RR3_B_TRANSMIT |
                        SCC_RR3_B_RECEIVE)) != 0) {
            SccpWriteRegister(Controller->ChannelB->Channel, SCC_WR0,
                SCC_WR0_RESET_HIGHEST_IUS);
        }
    }

    return Serviced;
}

VOID
SccpReceiveInterrupt(
    IN PSCC_LINE Line
    )

/*++

Routine Description:

    This routine empties the receive buffer of the channel of a line,
    giving each character to the input service of the client of the line.
    A character with a parity or framing error is counted, and the client
    of a line connected with receive status gets it with its errors; a
    client connected without receive status does not get it.  The null
    character that ends a break that the channel received is a break.  The
    characters of a line that is not enabled are discarded.

Arguments:

    Line - Supplies the line.

Return Value:

    None.

--*/

{

    UCHAR Condition;
    UCHAR Data;
    ULONG InputStatus;

    while ((SccpReadRegister(Line->Channel, SCC_RR0) &
            SCC_RR0_RECEIVE_AVAILABLE) != 0) {

        //
        // The special receive condition register has the errors of the
        // character at the head of the receive buffer.  The parity and
        // overrun errors stay set until they are reset.
        //

        Condition = SccpReadRegister(Line->Channel, SCC_RR1);
        Data = (UCHAR)(READ_REGISTER_ULONG(&Line->Channel->Data) >>
                       SCC_BYTE_SHIFT);

        KeStallExecutionProcessor(SCC_RECOVERY_TIME);
        if ((Condition & (SCC_RR1_PARITY_ERROR | SCC_RR1_OVERRUN_ERROR |
                          SCC_RR1_FRAMING_ERROR)) != 0) {
            SccpWriteRegister(Line->Channel, SCC_WR0, SCC_WR0_ERROR_RESET);
        }

        InputStatus = 0;
        if ((Condition & SCC_RR1_PARITY_ERROR) != 0) {
            InputStatus |= DZPORT_PARITY_ERROR;
        }

        if ((Condition & SCC_RR1_FRAMING_ERROR) != 0) {
            InputStatus |= DZPORT_FRAMING_ERROR;
        }

        if (Line->BreakEnded != FALSE) {
            Line->BreakEnded = FALSE;
            if (Data == 0) {
                InputStatus |= DZPORT_FRAMING_ERROR;
            }
        }

        if (InputStatus != 0) {
            Line->Extension->ReceiveErrors += 1;
        }

        if ((Condition & SCC_RR1_OVERRUN_ERROR) != 0) {
            InputStatus |= DZPORT_OVERRUN_ERROR;
        }

        Data &= Line->Parameters.CharacterMask;
        if ((Line->Connected != FALSE) && (Line->Enabled != FALSE)) {
            if (Line->InputStatusService != NULL) {
                (Line->InputStatusService)(Line->ClientContext, Data,
                    InputStatus);

            } else if ((InputStatus &
                        (DZPORT_PARITY_ERROR | DZPORT_FRAMING_ERROR)) == 0) {
                (Line->InputService)(Line->ClientContext, Data);
            }
        }
    }

    return;
}

VOID
SccpTransmitInterrupt(
    IN PSCC_LINE Line
    )

/*++

Routine Description:

    This routine serves the transmitter of the channel of a line, whose
    buffer became empty: the output service of the client of the line sends
    the next character.  When the client has nothing more to send, or the
    line is not connected, the transmit enable of the line is cleared and
    the transmit interrupt is reset.

Arguments:

    Line - Supplies the line.

Return Value:

    None.

--*/

{

    Line->TransmitBusy = FALSE;
    if ((Line->Connected != FALSE) && (Line->TransmitEnable != FALSE)) {
        if ((Line->OutputService)(Line->ClientContext) == FALSE) {
            Line->TransmitEnable = FALSE;
        }

    } else {
        Line->TransmitEnable = FALSE;
    }

    if (Line->TransmitBusy == FALSE) {
        SccpWriteRegister(Line->Channel, SCC_WR0,
            SCC_WR0_RESET_TRANSMIT_PENDING);
    }

    return;
}

VOID
SccpExternalStatusInterrupt(
    IN PSCC_LINE Line
    )

/*++

Routine Description:

    This routine serves the external status interrupt of the channel of a
    line, the start or the end of a break that the channel receives (the
    only external status interrupt the port enables).  The status register
    holds the status of the interrupt until the external status is reset.

Arguments:

    Line - Supplies the line.

Return Value:

    None.

--*/

{

    UCHAR Status;

    Status = SccpReadRegister(Line->Channel, SCC_RR0);
    if ((Status & SCC_RR0_BREAK) != 0) {
        Line->BreakReceived = TRUE;

    } else if (Line->BreakReceived != FALSE) {
        Line->BreakReceived = FALSE;
        Line->BreakEnded = TRUE;
    }

    SccpWriteRegister(Line->Channel, SCC_WR0, SCC_WR0_RESET_EXTERNAL_STATUS);
    return;
}

VOID
SccpTransmitDpc(
    IN PKDPC Dpc,
    IN PVOID DeferredContext,
    IN PVOID SystemArgument1,
    IN PVOID SystemArgument2
    )

/*++

Routine Description:

    This routine is the DPC that starts the transmitter of a line whose
    client enabled it while it had nothing in its buffer: it calls the
    output service of the client at the level of the interrupts of the
    port.

Arguments:

    Dpc - Supplies a pointer to the DPC object.

    DeferredContext - Supplies the line.

    SystemArgument1, SystemArgument2 - Not used.

Return Value:

    None.

--*/

{

    PSCC_LINE Line;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    Line = (PSCC_LINE)DeferredContext;
    KeSynchronizeExecution(Line->Controller->Interrupt, SccpStartTransmit,
        Line);

    return;
}

BOOLEAN
SccpStartTransmit(
    IN PVOID Context
    )

/*++

Routine Description:

    This routine starts the transmitter of a line at the level of the
    interrupts of the port: if the client of the line has its transmitter
    enabled and the buffer of the transmitter is empty, the output service
    of the client sends a character.  If a character was loaded since the
    DPC was queued, its transmit interrupt calls the output service.

Arguments:

    Context - Supplies the line.

Return Value:

    TRUE.

--*/

{

    PSCC_LINE Line;

    Line = (PSCC_LINE)Context;
    if ((Line->Connected != FALSE) && (Line->TransmitEnable != FALSE) &&
        (Line->TransmitBusy == FALSE)) {
        if ((Line->OutputService)(Line->ClientContext) == FALSE) {
            Line->TransmitEnable = FALSE;
        }
    }

    return TRUE;
}

BOOLEAN
SccpConnectLine(
    IN PVOID Context
    )

/*++

Routine Description:

    This routine connects a client to a line at the level of the interrupts
    of the port: it records the services of the client and the parameters
    of the line, and sets up the channel of the line with its receiver and
    its transmitter enabled.

Arguments:

    Context - Supplies the request: the line, the connect message and the
        line parameters.

Return Value:

    TRUE if the line is connected, FALSE if another client has it.

--*/

{

    PSCC_LINE Line;
    PSCC_REQUEST Request;

    Request = (PSCC_REQUEST)Context;
    Line = Request->Line;
    if (Line->Connected != FALSE) {
        return FALSE;
    }

    Line->ClientContext = Request->Connect->ClientContext;
    Line->InputService = Request->Connect->InputService;
    Line->InputStatusService = Request->InputStatusService;
    Line->OutputService = Request->Connect->OutputService;
    Line->Parameters = Request->Parameters;
    Line->Break = FALSE;
    Line->TransmitEnable = FALSE;
    Line->TransmitBusy = FALSE;
    Line->Connected = TRUE;
    SccpEnableLine(Line);
    return TRUE;
}

BOOLEAN
SccpDisconnectLine(
    IN PVOID Context
    )

/*++

Routine Description:

    This routine disconnects the client of a line at the level of the
    interrupts of the port: the receiver, the transmitter and the
    interrupts of the channel of the line are disabled.

Arguments:

    Context - Supplies the request: the line.

Return Value:

    TRUE.

--*/

{

    PSCC_LINE Line;

    Line = ((PSCC_REQUEST)Context)->Line;
    if (Line->Connected != FALSE) {
        SccpDisableLine(Line);
        Line->Connected = FALSE;
        Line->TransmitBusy = FALSE;
        SccpWriteInterruptEnables(Line);
        SccpWriteRegister(Line->Channel, SCC_WR5, SccpTransmitControl(Line));
        SccpWriteRegister(Line->Channel, SCC_WR0,
            SCC_WR0_RESET_TRANSMIT_PENDING);
    }

    return TRUE;
}

BOOLEAN
SccpLineParameters(
    IN ULONG BaudRate,
    IN ULONG CharacterLength,
    IN ULONG StopBits,
    IN ULONG Polarity,
    IN BOOLEAN ParityEnable,
    OUT PSCC_PARAMETERS Parameters
    )

/*++

Routine Description:

    This routine computes the register values of the parameters of a line.
    The baud rate generator of a channel divides the clock of the SCC by
    twice its time constant plus two, and the channel divides the clock of
    the generator by 16.

Arguments:

    BaudRate - Supplies the speed of the line in bits per second.

    CharacterLength - Supplies the number of bits of a character, 5 to 8.

    StopBits - Supplies the number of stop bits, 1 or 2.

    Polarity - Supplies the parity: 0 for even, 1 for odd.

    ParityEnable - Supplies whether the line has a parity bit.

    Parameters - Supplies a pointer to a variable that receives the
        register values of the parameters.

Return Value:

    TRUE if the parameters are possible on the SCC, FALSE otherwise.

--*/

{

    ULONG Error;
    ULONG Rate;
    ULONG TimeConstant;

    if ((BaudRate < SCC_MINIMUM_BAUD_RATE) ||
        (BaudRate > SCC_MAXIMUM_BAUD_RATE) ||
        (CharacterLength < 5) || (CharacterLength > 8) ||
        (StopBits < 1) || (StopBits > 2) || (Polarity > 1)) {
        return FALSE;
    }

    TimeConstant = SCC_TIME_CONSTANT(BaudRate);
    Rate = SCC_CLOCK / (32 * (TimeConstant + 2));
    Error = (Rate > BaudRate) ? (Rate - BaudRate) : (BaudRate - Rate);
    if ((Error * 100) > BaudRate) {
        return FALSE;
    }

    Parameters->TimeConstant = (USHORT)TimeConstant;
    Parameters->CharacterFormat = (UCHAR)(SCC_WR4_CLOCK_X16 |
        ((StopBits == 2) ? SCC_WR4_TWO_STOP_BITS : SCC_WR4_ONE_STOP_BIT));

    if (ParityEnable != FALSE) {
        Parameters->CharacterFormat |= SCC_WR4_PARITY_ENABLE;
        if (Polarity == 0) {
            Parameters->CharacterFormat |= SCC_WR4_EVEN_PARITY;
        }
    }

    Parameters->ReceiveParameters = SccpReceiveBits[CharacterLength - 5];
    Parameters->TransmitParameters = SccpTransmitBits[CharacterLength - 5];
    Parameters->CharacterMask = (UCHAR)((1 << CharacterLength) - 1);
    return TRUE;
}

VOID
SccpLoadParameters(
    IN PSCC_LINE Line
    )

/*++

Routine Description:

    This routine loads the parameters of a line in the registers of its
    channel.  The receiver and the transmitter are disabled while the
    character format and the baud rate generator change, and are then
    enabled as the line has them.  It is called at the level of the
    interrupts of the port.

Arguments:

    Line - Supplies the line.

Return Value:

    None.

--*/

{

    PSCC_CHANNEL Channel;
    UCHAR ReceiveParameters;
    UCHAR TransmitControl;

    Channel = Line->Channel;
    ReceiveParameters = Line->Parameters.ReceiveParameters;
    if (Line->Enabled != FALSE) {
        ReceiveParameters |= SCC_WR3_RECEIVE_ENABLE;
    }

    TransmitControl = SccpTransmitControl(Line);
    SccpWriteRegister(Channel, SCC_WR3, Line->Parameters.ReceiveParameters);
    SccpWriteRegister(Channel, SCC_WR5,
        (UCHAR)(TransmitControl & ~SCC_WR5_TRANSMIT_ENABLE));

    SccpWriteRegister(Channel, SCC_WR4, Line->Parameters.CharacterFormat);
    SccpWriteRegister(Channel, SCC_WR10, 0);
    SccpWriteRegister(Channel, SCC_WR14, SCC_WR14_BRG_PCLK);
    SccpWriteRegister(Channel, SCC_WR11, SCC_WR11_CLOCKS_BRG);
    SccpWriteRegister(Channel, SCC_WR12, (UCHAR)Line->Parameters.TimeConstant);
    SccpWriteRegister(Channel, SCC_WR13,
        (UCHAR)(Line->Parameters.TimeConstant >> 8));

    SccpWriteRegister(Channel, SCC_WR14, SCC_WR14_BRG_PCLK | SCC_WR14_BRG_ENABLE);
    SccpWriteRegister(Channel, SCC_WR3, ReceiveParameters);
    SccpWriteRegister(Channel, SCC_WR5, TransmitControl);
    return;
}

UCHAR
SccpTransmitControl(
    IN PSCC_LINE Line
    )

/*++

Routine Description:

    This routine computes the value of the transmit parameter register of
    the channel of a line: the bits of a character, the transmit enable of
    a connected line and the break, and on channel A the DTR and RTS of the
    line of channel B.

Arguments:

    Line - Supplies the line.

Return Value:

    The value of the register.

--*/

{

    UCHAR Value;

    Value = Line->Parameters.TransmitParameters;
    if (Line->Connected != FALSE) {
        Value |= SCC_WR5_TRANSMIT_ENABLE;
    }

    if (Line->Break != FALSE) {
        Value |= SCC_WR5_SEND_BREAK;
    }

    if (Line == Line->Controller->ChannelA) {
        Value |= Line->Controller->ChannelB->ModemControl;
    }

    return Value;
}

VOID
SccpWriteInterruptEnables(
    IN PSCC_LINE Line
    )

/*++

Routine Description:

    This routine writes the interrupt enables of the channel of a line: the
    transmit interrupt of a connected line, and the interrupt for each
    received character, for each special receive condition and for a
    break of an enabled line.  It is called at the level of the interrupts
    of the port.

Arguments:

    Line - Supplies the line.

Return Value:

    None.

--*/

{

    UCHAR Value;

    Value = 0;
    if (Line->Connected != FALSE) {
        Value |= SCC_WR1_TRANSMIT_ENABLE;
    }

    if (Line->Enabled != FALSE) {
        Value |= SCC_WR1_RECEIVE_ALL | SCC_WR1_EXTERNAL_ENABLE;
    }

    SccpWriteRegister(Line->Channel, SCC_WR1, Value);
    return;
}

VOID
SccpEnableLine(
    IN PVOID PortContext
    )

/*++

Routine Description:

    This routine sets the parameters of a line and enables its receiver:
    the characters the receive buffer holds are discarded, the external
    status interrupt is enabled for a break, and the interrupts of the
    line are enabled.  It is called at the level of the interrupts of the
    port.

Arguments:

    PortContext - Supplies the line.

Return Value:

    None.

--*/

{

    PSCC_LINE Line;

    Line = (PSCC_LINE)PortContext;
    while ((SccpReadRegister(Line->Channel, SCC_RR0) &
            SCC_RR0_RECEIVE_AVAILABLE) != 0) {
        (VOID)READ_REGISTER_ULONG(&Line->Channel->Data);
        KeStallExecutionProcessor(SCC_RECOVERY_TIME);
    }

    SccpWriteRegister(Line->Channel, SCC_WR0, SCC_WR0_ERROR_RESET);
    Line->Enabled = TRUE;
    Line->BreakReceived = FALSE;
    Line->BreakEnded = FALSE;
    SccpLoadParameters(Line);

    //
    // The external status is reset twice: the first reset may latch a
    // change of the status, which the second clears.
    //

    SccpWriteRegister(Line->Channel, SCC_WR15, SCC_WR15_BREAK_ENABLE);
    SccpWriteRegister(Line->Channel, SCC_WR0, SCC_WR0_RESET_EXTERNAL_STATUS);
    SccpWriteRegister(Line->Channel, SCC_WR0, SCC_WR0_RESET_EXTERNAL_STATUS);
    SccpWriteInterruptEnables(Line);
    return;
}

VOID
SccpDisableLine(
    IN PVOID PortContext
    )

/*++

Routine Description:

    This routine disables the receiver and the transmitter of a line, and
    ends the break of the line.  The character in the buffer of the
    transmitter is sent, and its transmit interrupt finds the transmitter
    of the client disabled.  It is called at the level of the interrupts of
    the port.

Arguments:

    PortContext - Supplies the line.

Return Value:

    None.

--*/

{

    PSCC_LINE Line;

    Line = (PSCC_LINE)PortContext;
    Line->Enabled = FALSE;
    Line->Break = FALSE;
    Line->TransmitEnable = FALSE;
    SccpWriteInterruptEnables(Line);
    SccpWriteRegister(Line->Channel, SCC_WR3, Line->Parameters.ReceiveParameters);
    SccpWriteRegister(Line->Channel, SCC_WR5, SccpTransmitControl(Line));
    SccpWriteRegister(Line->Channel, SCC_WR0, SCC_WR0_RESET_EXTERNAL_STATUS);
    return;
}

VOID
SccpEnableTransmit(
    IN PVOID PortContext
    )

/*++

Routine Description:

    This routine enables the transmitter of a line: when the buffer of the
    transmitter is empty, the port calls the output service of the client
    of the line, which sends the next character.  If the transmitter has
    nothing in its buffer, the DPC of the line calls the output service.
    It is called at the level of the interrupts of the port.

Arguments:

    PortContext - Supplies the line.

Return Value:

    None.

--*/

{

    PSCC_LINE Line;

    Line = (PSCC_LINE)PortContext;
    Line->TransmitEnable = TRUE;
    if (Line->TransmitBusy == FALSE) {
        KeInsertQueueDpc(&Line->TransmitDpc, NULL, NULL);
    }

    return;
}

VOID
SccpOutputByte(
    IN PVOID PortContext,
    IN UCHAR OutputByte
    )

/*++

Routine Description:

    This routine sends a character on a line.  It is called by the output
    service of the client of the line, when the buffer of the transmitter
    of the line is empty.  A character of five bits has the high bits of
    its byte clear, as the transmitter needs them.

Arguments:

    PortContext - Supplies the line.

    OutputByte - Supplies the character.

Return Value:

    None.

--*/

{

    PSCC_LINE Line;

    Line = (PSCC_LINE)PortContext;
    WRITE_REGISTER_ULONG(&Line->Channel->Data,
        (ULONG)(OutputByte & Line->Parameters.CharacterMask) << SCC_BYTE_SHIFT);

    KeStallExecutionProcessor(SCC_RECOVERY_TIME);
    Line->TransmitBusy = TRUE;
    return;
}

BOOLEAN
SccpSetLineParameters(
    IN PVOID PortContext,
    IN ULONG BaudRate,
    IN ULONG CharacterLength,
    IN ULONG StopBits,
    IN ULONG Polarity,
    IN BOOLEAN ParityEnable
    )

/*++

Routine Description:

    This routine changes the parameters of a line.  It is called at the
    level of the interrupts of the port.

Arguments:

    PortContext - Supplies the line.

    BaudRate, CharacterLength, StopBits, Polarity, ParityEnable - Supply
        the parameters (see SccpLineParameters).

Return Value:

    TRUE if the parameters are set, FALSE if the SCC cannot have them.

--*/

{

    PSCC_LINE Line;
    SCC_PARAMETERS Parameters;

    Line = (PSCC_LINE)PortContext;
    if (SccpLineParameters(BaudRate, CharacterLength, StopBits, Polarity,
                           ParityEnable, &Parameters) == FALSE) {
        return FALSE;
    }

    Line->Parameters = Parameters;
    SccpLoadParameters(Line);
    return TRUE;
}

VOID
SccpSetModemControl(
    IN PVOID PortContext,
    IN ULONG ModemControl
    )

/*++

Routine Description:

    This routine sets the modem control signals of the communications or
    the printer line: data terminal ready and request to send, which are
    outputs of channel A of the SCC of the line.  The other lines have no
    modem control.  It is called at the level of the interrupts of the
    port.

Arguments:

    PortContext - Supplies the line.

    ModemControl - Supplies the signals to assert (DZPORT_DTR, DZPORT_RTS);
        the others are deasserted.

Return Value:

    None.

--*/

{

    PSCC_LINE ControlLine;
    PSCC_LINE Line;

    Line = (PSCC_LINE)PortContext;
    if ((Line->LineNumber != DZPORT_COMMUNICATIONS_LINE) &&
        (Line->LineNumber != DZPORT_PRINTER_LINE)) {
        return;
    }

    Line->ModemControl = 0;
    if ((ModemControl & DZPORT_DTR) != 0) {
        Line->ModemControl |= SCC_WR5_DTR;
    }

    if ((ModemControl & DZPORT_RTS) != 0) {
        Line->ModemControl |= SCC_WR5_RTS;
    }

    ControlLine = Line->Controller->ChannelA;
    SccpWriteRegister(ControlLine->Channel, SCC_WR5,
        SccpTransmitControl(ControlLine));

    return;
}

ULONG
SccpQueryModemControl(
    IN PVOID PortContext
    )

/*++

Routine Description:

    This routine returns the modem control signals of the communications or
    the printer line.  It is called at the level of the interrupts of the
    port.

Arguments:

    PortContext - Supplies the line.

Return Value:

    The signals that are asserted (DZPORT_DTR, DZPORT_RTS); zero for a line
    that has no modem control.

--*/

{

    PSCC_LINE Line;
    ULONG ModemControl;

    Line = (PSCC_LINE)PortContext;
    ModemControl = 0;
    if ((Line->ModemControl & SCC_WR5_DTR) != 0) {
        ModemControl |= DZPORT_DTR;
    }

    if ((Line->ModemControl & SCC_WR5_RTS) != 0) {
        ModemControl |= DZPORT_RTS;
    }

    return ModemControl;
}

ULONG
SccpQueryModemStatus(
    IN PVOID PortContext
    )

/*++

Routine Description:

    This routine returns the modem status of the communications or the
    printer line: clear to send and carrier detect are inputs of the
    channel of the line, data set ready and ring indicator inputs of
    channel A of its SCC.  The port enables no interrupt for a change of
    the modem status, so a client reads it when it needs it.  It is called
    at the level of the interrupts of the port.

Arguments:

    PortContext - Supplies the line.

Return Value:

    The modem status signals that are asserted (DZPORT_CTS, DZPORT_DSR,
    DZPORT_CD, DZPORT_RI); zero for a line that has no modem control.

--*/

{

    PSCC_LINE Line;
    ULONG ModemStatus;
    UCHAR StatusA;
    UCHAR StatusB;

    Line = (PSCC_LINE)PortContext;
    ModemStatus = 0;
    if ((Line->LineNumber != DZPORT_COMMUNICATIONS_LINE) &&
        (Line->LineNumber != DZPORT_PRINTER_LINE)) {
        return ModemStatus;
    }

    StatusB = SccpReadRegister(Line->Channel, SCC_RR0);
    StatusA = SccpReadRegister(Line->Controller->ChannelA->Channel, SCC_RR0);
    if ((StatusB & SCC_RR0_CTS) != 0) {
        ModemStatus |= DZPORT_CTS;
    }

    if ((StatusB & SCC_RR0_DCD) != 0) {
        ModemStatus |= DZPORT_CD;
    }

    if ((StatusA & SCC_RR0_SYNC) != 0) {
        ModemStatus |= DZPORT_DSR;
    }

    if ((StatusA & SCC_RR0_DCD) != 0) {
        ModemStatus |= DZPORT_RI;
    }

    return ModemStatus;
}

VOID
SccpSetBreak(
    IN PVOID PortContext,
    IN BOOLEAN Break
    )

/*++

Routine Description:

    This routine starts or ends the break of a line: the transmitter of the
    channel sends zeros while the break is set.  It is called at the level
    of the interrupts of the port.

Arguments:

    PortContext - Supplies the line.

    Break - Supplies TRUE to start the break, FALSE to end it.

Return Value:

    None.

--*/

{

    PSCC_LINE Line;

    Line = (PSCC_LINE)PortContext;
    Line->Break = Break;
    SccpWriteRegister(Line->Channel, SCC_WR5, SccpTransmitControl(Line));
    return;
}

UCHAR
SccpReadRegister(
    IN PSCC_CHANNEL Channel,
    IN ULONG Register
    )

/*++

Routine Description:

    This routine reads a read register of a channel.  A register other than
    RR0 is selected with a write to the control register of the channel
    first.  The SCC needs time to recover after each access of a register.

Arguments:

    Channel - Supplies a pointer to the registers of the channel.

    Register - Supplies the number of the read register.

Return Value:

    The value of the register.

--*/

{

    UCHAR Value;

    if (Register != SCC_RR0) {
        WRITE_REGISTER_ULONG(&Channel->Control, Register << SCC_BYTE_SHIFT);
        KeStallExecutionProcessor(SCC_RECOVERY_TIME);
    }

    Value = (UCHAR)(READ_REGISTER_ULONG(&Channel->Control) >> SCC_BYTE_SHIFT);
    KeStallExecutionProcessor(SCC_RECOVERY_TIME);
    return Value;
}

VOID
SccpWriteRegister(
    IN PSCC_CHANNEL Channel,
    IN ULONG Register,
    IN UCHAR Value
    )

/*++

Routine Description:

    This routine writes a write register of a channel.  A register other
    than WR0 is selected with a write to the control register of the
    channel first.  The SCC needs time to recover after each access of a
    register.

Arguments:

    Channel - Supplies a pointer to the registers of the channel.

    Register - Supplies the number of the write register.

    Value - Supplies the value that is written.

Return Value:

    None.

--*/

{

    if (Register != SCC_WR0) {
        WRITE_REGISTER_ULONG(&Channel->Control, Register << SCC_BYTE_SHIFT);
        KeStallExecutionProcessor(SCC_RECOVERY_TIME);
    }

    WRITE_REGISTER_ULONG(&Channel->Control, (ULONG)Value << SCC_BYTE_SHIFT);
    KeStallExecutionProcessor(SCC_RECOVERY_TIME);
    return;
}
