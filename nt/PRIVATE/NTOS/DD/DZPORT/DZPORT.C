/*++

Module Name:

    dzport.c

Abstract:

    This module implements the port driver of the DZ serial line
    multiplexer (DC7085) of the DECstation 5000/200 (dzport.h).  The
    multiplexer has four lines: the keyboard (line 0), the mouse (line 1),
    the communications line with the modem signals (line 2) and the printer
    line (line 3).  The firmware describes the lines as controllers of the
    internal bus of the system module, the keyboard controller, the pointer
    controller and two serial controllers, which share the registers and
    the interrupt of the multiplexer.

    A client driver connects to one line with an internal device control
    request: it gives the port its input and output services and gets the
    routines of the port.  The port calls the input service with each
    character the line receives and the output service each time the
    transmitter of the line is ready, both at the level of the interrupt of
    the multiplexer; a client calls the routines of the port at that level
    too, from its services or through KeSynchronizeExecution with the
    interrupt object of the port.

    The kernel debugger owns line DEBUG_LINE (the d3port.c of the HAL), which
    it polls at the highest level.  The port driver does not connect that
    line and never changes its parameters or its transmitter; a character
    of it that the receive silo holds when the debugger is not reading is
    discarded, as are the characters of the lines no client has connected.

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
#include "decdz.h"
#include "dzport.h"

//
// Define the number of lines of the multiplexer.
//

#define DZ_NUMBER_OF_LINES 4

//
// Define the error log values of the port.
//

#define DZPORT_ERROR_VALUE_BASE 4000

//
// Define the state of a line.
//

typedef struct _DZ_LINE {
    struct _DZ_EXTENSION *Extension;
    ULONG LineNumber;
    USHORT LineParameters;
    BOOLEAN Connected;
    BOOLEAN Enabled;
    PVOID ClientContext;
    PDZPORT_INPUT_SERVICE InputService;
    PDZPORT_OUTPUT_SERVICE OutputService;
} DZ_LINE, *PDZ_LINE;

//
// Define the device extension of the port.  The read and the write
// registers of the multiplexer are at the same addresses.
//

typedef struct _DZ_EXTENSION {
    PDEVICE_OBJECT DeviceObject;
    PDZ_READ_REGISTERS ReadRegisters;
    PDZ_WRITE_REGISTERS WriteRegisters;
    PKINTERRUPT Interrupt;
    DZ_LINE Line[DZ_NUMBER_OF_LINES];
    ULONG ReceiveErrors;
} DZ_EXTENSION, *PDZ_EXTENSION;

//
// Define the context of a connect or disconnect request that is
// synchronized with the interrupt of the port.
//

typedef struct _DZ_REQUEST {
    PDZ_LINE Line;
    PDZPORT_CONNECT Connect;
    USHORT LineParameters;
} DZ_REQUEST, *PDZ_REQUEST;

//
// Define the resources of the multiplexer that the firmware describes.
//

typedef struct _DZ_CONFIGURATION {
    BOOLEAN Found;
    INTERFACE_TYPE InterfaceType;
    ULONG BusNumber;
    CM_PARTIAL_RESOURCE_DESCRIPTOR Port;
    CM_PARTIAL_RESOURCE_DESCRIPTOR Interrupt;
} DZ_CONFIGURATION, *PDZ_CONFIGURATION;

//
// Define the speed codes of the baud rates of the multiplexer.
//

typedef struct _DZ_BAUD_RATE {
    ULONG BaudRate;
    USHORT SpeedCode;
} DZ_BAUD_RATE, *PDZ_BAUD_RATE;

DZ_BAUD_RATE DzpBaudRates[] = {
    {   50, 0x0 },
    {   75, 0x1 },
    {  110, 0x2 },
    {  134, 0x3 },
    {  150, 0x4 },
    {  300, 0x5 },
    {  600, 0x6 },
    { 1200, 0x7 },
    { 1800, 0x8 },
    { 2000, 0x9 },
    { 2400, 0xa },
    { 3600, 0xb },
    { 4800, BAUD_4800 },
    { 7200, BAUD_7200 },
    { 9600, BAUD_9600 },
    { 19200, BAUD_19200 },
    { 0, 0 }
};

//
// Define the controllers of the internal bus that are lines of the
// multiplexer.
//

CONFIGURATION_TYPE DzpControllerTypes[] = {
    KeyboardController,
    PointerController,
    SerialController
};

NTSTATUS
DriverEntry(
    IN PDRIVER_OBJECT DriverObject,
    IN PUNICODE_STRING RegistryPath
    );

NTSTATUS
DzpConfigurationCallout(
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
DzpLogError(
    IN PVOID IoObject,
    IN NTSTATUS ErrorCode,
    IN ULONG UniqueErrorValue,
    IN NTSTATUS FinalStatus
    );

NTSTATUS
DzpDispatch(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp
    );

BOOLEAN
DzpEnableInterrupts(
    IN PVOID Context
    );

BOOLEAN
DzpInterruptService(
    IN PKINTERRUPT Interrupt,
    IN PVOID Context
    );

BOOLEAN
DzpConnectLine(
    IN PVOID Context
    );

BOOLEAN
DzpDisconnectLine(
    IN PVOID Context
    );

BOOLEAN
DzpLineParameters(
    IN ULONG LineNumber,
    IN ULONG BaudRate,
    IN ULONG CharacterLength,
    IN ULONG StopBits,
    IN ULONG Polarity,
    IN BOOLEAN ParityEnable,
    OUT PUSHORT LineParameters
    );

VOID
DzpEnableLine(
    IN PVOID PortContext
    );

VOID
DzpDisableLine(
    IN PVOID PortContext
    );

VOID
DzpEnableTransmit(
    IN PVOID PortContext
    );

VOID
DzpDisableTransmit(
    IN PDZ_LINE Line
    );

VOID
DzpOutputByte(
    IN PVOID PortContext,
    IN UCHAR OutputByte
    );

BOOLEAN
DzpSetLineParameters(
    IN PVOID PortContext,
    IN ULONG BaudRate,
    IN ULONG CharacterLength,
    IN ULONG StopBits,
    IN ULONG Polarity,
    IN BOOLEAN ParityEnable
    );

#ifdef ALLOC_PRAGMA
#pragma alloc_text(INIT,DriverEntry)
#pragma alloc_text(INIT,DzpConfigurationCallout)
#endif

NTSTATUS
DriverEntry(
    IN PDRIVER_OBJECT DriverObject,
    IN PUNICODE_STRING RegistryPath
    )

/*++

Routine Description:

    This routine initializes the DZ port driver: it finds the multiplexer in
    the configuration that the firmware describes, reports its registers
    and its interrupt as the resources of the driver, creates the port
    device \Device\DzPort, maps the registers, connects the interrupt and
    enables the receive and transmit interrupts of the multiplexer.  The
    lines stay as they are until a client connects one of them.

Arguments:

    DriverObject - Supplies a pointer to the driver object of the port.

    RegistryPath - Supplies the path of the service key of the driver.

Return Value:

    The status of the initialization.

--*/

{

    KAFFINITY Affinity;
    DZ_CONFIGURATION Configuration;
    BOOLEAN Conflict;
    CONFIGURATION_TYPE ControllerType;
    PDEVICE_OBJECT DeviceObject;
    PDZ_EXTENSION Extension;
    ULONG Index;
    INTERFACE_TYPE InterfaceType;
    KIRQL Irql;
    UNICODE_STRING Name;
    PHYSICAL_ADDRESS PortAddress;
    ULONG PortSpace;
    PCM_RESOURCE_LIST ResourceList;
    ULONG ResourceListSize;
    NTSTATUS Status;
    ULONG Vector;

    UNREFERENCED_PARAMETER(RegistryPath);

    //
    // Find the multiplexer: the resources of the first of its lines that the
    // configuration has.
    //

    RtlZeroMemory(&Configuration, sizeof(DZ_CONFIGURATION));
    for (Index = 0;
         Index < sizeof(DzpControllerTypes) / sizeof(CONFIGURATION_TYPE);
         Index += 1) {

        InterfaceType = Internal;
        ControllerType = DzpControllerTypes[Index];
        IoQueryDeviceDescription(&InterfaceType, NULL, &ControllerType, NULL,
            NULL, NULL, DzpConfigurationCallout, &Configuration);

        if (Configuration.Found != FALSE) {
            break;
        }
    }

    if (Configuration.Found == FALSE) {
        DzpLogError(DriverObject, IO_ERR_CONFIGURATION_ERROR,
            DZPORT_ERROR_VALUE_BASE + 1, STATUS_NO_SUCH_DEVICE);
        return STATUS_NO_SUCH_DEVICE;
    }

    //
    // Report the registers and the interrupt of the multiplexer as the
    // resources of the driver.
    //

    ResourceListSize =
        sizeof(CM_RESOURCE_LIST) + sizeof(CM_PARTIAL_RESOURCE_DESCRIPTOR);
    ResourceList = ExAllocatePool(PagedPool, ResourceListSize);
    if (ResourceList == NULL) {
        DzpLogError(DriverObject, IO_ERR_INSUFFICIENT_RESOURCES,
            DZPORT_ERROR_VALUE_BASE + 2, STATUS_INSUFFICIENT_RESOURCES);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(ResourceList, ResourceListSize);
    ResourceList->Count = 1;
    ResourceList->List[0].InterfaceType = Configuration.InterfaceType;
    ResourceList->List[0].BusNumber = Configuration.BusNumber;
    ResourceList->List[0].PartialResourceList.Count = 2;
    ResourceList->List[0].PartialResourceList.PartialDescriptors[0] =
        Configuration.Port;
    ResourceList->List[0].PartialResourceList.PartialDescriptors[0].ShareDisposition =
        CmResourceShareDeviceExclusive;
    ResourceList->List[0].PartialResourceList.PartialDescriptors[1] =
        Configuration.Interrupt;
    ResourceList->List[0].PartialResourceList.PartialDescriptors[1].ShareDisposition =
        CmResourceShareDeviceExclusive;

    Status = IoReportResourceUsage(NULL, DriverObject, ResourceList,
        ResourceListSize, NULL, NULL, 0, FALSE, &Conflict);

    ExFreePool(ResourceList);
    if (!NT_SUCCESS(Status) || (Conflict != FALSE)) {
        DzpLogError(DriverObject, IO_ERR_PORT_RESOURCE_CONFLICT,
            DZPORT_ERROR_VALUE_BASE + 3, STATUS_INSUFFICIENT_RESOURCES);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    //
    // Create the port device.
    //

    RtlInitUnicodeString(&Name, L"\\Device\\DzPort");
    Status = IoCreateDevice(DriverObject, sizeof(DZ_EXTENSION), &Name,
        FILE_DEVICE_SERIAL_PORT, 0, FALSE, &DeviceObject);

    if (!NT_SUCCESS(Status)) {
        DzpLogError(DriverObject, IO_ERR_INSUFFICIENT_RESOURCES,
            DZPORT_ERROR_VALUE_BASE + 4, Status);
        return Status;
    }

    DeviceObject->Flags |= DO_BUFFERED_IO;
    Extension = (PDZ_EXTENSION)DeviceObject->DeviceExtension;
    RtlZeroMemory(Extension, sizeof(DZ_EXTENSION));
    Extension->DeviceObject = DeviceObject;
    for (Index = 0; Index < DZ_NUMBER_OF_LINES; Index += 1) {
        Extension->Line[Index].Extension = Extension;
        Extension->Line[Index].LineNumber = Index;
    }

    //
    // Map the registers of the multiplexer.
    //

    PortSpace = 1;
    if (HalTranslateBusAddress(Configuration.InterfaceType,
            Configuration.BusNumber, Configuration.Port.u.Port.Start,
            &PortSpace, &PortAddress) == FALSE) {

        DzpLogError(DeviceObject, IO_ERR_INVALID_IOBASE,
            DZPORT_ERROR_VALUE_BASE + 5, STATUS_NO_SUCH_DEVICE);
        IoDeleteDevice(DeviceObject);
        return STATUS_NO_SUCH_DEVICE;
    }

    if (PortSpace == 0) {
        Extension->ReadRegisters = (PDZ_READ_REGISTERS)MmMapIoSpace(PortAddress,
            Configuration.Port.u.Port.Length, FALSE);

    } else {
        Extension->ReadRegisters = (PDZ_READ_REGISTERS)PortAddress.LowPart;
    }

    if (Extension->ReadRegisters == NULL) {
        DzpLogError(DeviceObject, IO_ERR_INSUFFICIENT_RESOURCES,
            DZPORT_ERROR_VALUE_BASE + 6, STATUS_INSUFFICIENT_RESOURCES);
        IoDeleteDevice(DeviceObject);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Extension->WriteRegisters = (PDZ_WRITE_REGISTERS)Extension->ReadRegisters;

    //
    // Connect the interrupt of the multiplexer.
    //

    Vector = HalGetInterruptVector(Configuration.InterfaceType,
        Configuration.BusNumber, Configuration.Interrupt.u.Interrupt.Level,
        Configuration.Interrupt.u.Interrupt.Vector, &Irql, &Affinity);

    Status = IoConnectInterrupt(&Extension->Interrupt, DzpInterruptService,
        Extension, NULL, Vector, Irql, Irql,
        ((Configuration.Interrupt.Flags & CM_RESOURCE_INTERRUPT_LATCHED) != 0) ?
            Latched : LevelSensitive,
        FALSE, Affinity, FALSE);

    if (!NT_SUCCESS(Status)) {
        DzpLogError(DeviceObject, IO_ERR_INTERRUPT_RESOURCE_CONFLICT,
            DZPORT_ERROR_VALUE_BASE + 7, Status);
        if (PortSpace == 0) {
            MmUnmapIoSpace(Extension->ReadRegisters,
                Configuration.Port.u.Port.Length);
        }

        IoDeleteDevice(DeviceObject);
        return Status;
    }

    DriverObject->MajorFunction[IRP_MJ_CREATE] = DzpDispatch;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = DzpDispatch;
    DriverObject->MajorFunction[IRP_MJ_CLEANUP] = DzpDispatch;
    DriverObject->MajorFunction[IRP_MJ_INTERNAL_DEVICE_CONTROL] = DzpDispatch;

    //
    // Enable the receive and transmit interrupts.  The multiplexer is not
    // cleared: the kernel debugger may have set up its line and scan it.  A
    // transmitter interrupts only while the port enables it, which it does
    // while a client has characters to send.
    //

    KeSynchronizeExecution(Extension->Interrupt, DzpEnableInterrupts,
        Extension);

    return STATUS_SUCCESS;
}

NTSTATUS
DzpConfigurationCallout(
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

    This routine is the callout of IoQueryDeviceDescription for a controller
    that is a line of the multiplexer: it takes the registers and the
    interrupt of the multiplexer from the configuration data of the
    controller.

Arguments:

    Context - Supplies a pointer to the configuration of the port.

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

    PDZ_CONFIGURATION Configuration;
    PKEY_VALUE_FULL_INFORMATION Data;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor;
    ULONG Index;
    PCM_PARTIAL_RESOURCE_LIST List;
    BOOLEAN Interrupt;
    BOOLEAN Port;

    UNREFERENCED_PARAMETER(PathName);
    UNREFERENCED_PARAMETER(BusInformation);
    UNREFERENCED_PARAMETER(ControllerType);
    UNREFERENCED_PARAMETER(ControllerNumber);
    UNREFERENCED_PARAMETER(PeripheralType);
    UNREFERENCED_PARAMETER(PeripheralNumber);
    UNREFERENCED_PARAMETER(PeripheralInformation);

    Configuration = (PDZ_CONFIGURATION)Context;
    Data = ControllerInformation[IoQueryDeviceConfigurationData];
    if ((Configuration->Found != FALSE) || (Data == NULL) ||
        (Data->DataLength == 0)) {
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
            Configuration->Port = *Descriptor;
            Port = TRUE;
            break;

        case CmResourceTypeInterrupt:
            Configuration->Interrupt = *Descriptor;
            Interrupt = TRUE;
            break;

        default:
            break;
        }
    }

    if ((Port != FALSE) && (Interrupt != FALSE)) {
        Configuration->InterfaceType = BusType;
        Configuration->BusNumber = BusNumber;
        Configuration->Found = TRUE;
    }

    return STATUS_SUCCESS;
}

VOID
DzpLogError(
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

BOOLEAN
DzpEnableInterrupts(
    IN PVOID Context
    )

/*++

Routine Description:

    This routine enables the scan of the multiplexer and its receive and
    transmit interrupts, at the level of the interrupt of the port.

Arguments:

    Context - Supplies a pointer to the device extension of the port.

Return Value:

    TRUE.

--*/

{

    USHORT DataShort;
    PDZ_EXTENSION Extension;

    Extension = (PDZ_EXTENSION)Context;
    DataShort = READ_REGISTER_USHORT(&Extension->ReadRegisters->ControlStatus.Short);
    ((PDZ_CONTROL_STATUS)(&DataShort))->Maintenance = 0;
    ((PDZ_CONTROL_STATUS)(&DataShort))->MasterClear = 0;
    ((PDZ_CONTROL_STATUS)(&DataShort))->MasterScanEnable = 1;
    ((PDZ_CONTROL_STATUS)(&DataShort))->ReceiveInterruptEnable = 1;
    ((PDZ_CONTROL_STATUS)(&DataShort))->TransmitInterruptEnable = 1;
    WRITE_REGISTER_USHORT(&Extension->WriteRegisters->ControlStatus.Short, DataShort);
    return TRUE;
}

NTSTATUS
DzpDispatch(
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
    PDZ_EXTENSION Extension;
    PIO_STACK_LOCATION IrpSp;
    ULONG LineNumber;
    DZ_REQUEST Request;
    NTSTATUS Status;

    Extension = (PDZ_EXTENSION)DeviceObject->DeviceExtension;
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
        // Connect a line: check the line and its parameters, set the
        // parameters and enable the receiver, and return the routines of
        // the port.
        //

        case IOCTL_MN_DZPORT_CONNECT:
            Connect = (PDZPORT_CONNECT)Irp->AssociatedIrp.SystemBuffer;
            if ((IrpSp->Parameters.DeviceIoControl.InputBufferLength <
                    sizeof(DZPORT_CONNECT)) ||
                (IrpSp->Parameters.DeviceIoControl.OutputBufferLength <
                    sizeof(DZPORT_ACCEPT))) {
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            LineNumber = Connect->LineNumber;
            if ((LineNumber >= DZ_NUMBER_OF_LINES) ||
                (LineNumber == DEBUG_LINE) ||
                (Connect->InputService == NULL) ||
                (Connect->OutputService == NULL) ||
                (DzpLineParameters(LineNumber, Connect->BaudRate,
                                   Connect->CharacterLength,
                                   Connect->StopBits,
                                   Connect->Polarity,
                                   Connect->ParityEnable,
                                   &Request.LineParameters) == FALSE)) {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            Request.Line = &Extension->Line[LineNumber];
            Request.Connect = Connect;
            if (KeSynchronizeExecution(Extension->Interrupt, DzpConnectLine,
                    &Request) == FALSE) {
                Status = STATUS_DEVICE_ALREADY_ATTACHED;
                break;
            }

            //
            // The accept message goes in the same buffer as the connect
            // message.
            //

            Accept = (PDZPORT_ACCEPT)Irp->AssociatedIrp.SystemBuffer;
            Accept->PortContext = Request.Line;
            Accept->EnableLine = DzpEnableLine;
            Accept->DisableLine = DzpDisableLine;
            Accept->EnableTransmit = DzpEnableTransmit;
            Accept->OutputByte = DzpOutputByte;
            Accept->SetLineParameters = DzpSetLineParameters;
            Accept->Interrupt = Extension->Interrupt;
            Irp->IoStatus.Information = sizeof(DZPORT_ACCEPT);
            Status = STATUS_SUCCESS;
            break;

        //
        // Disconnect a line: disable its receiver and transmitter.
        //

        case IOCTL_MN_DZPORT_DISCONNECT:
            if (IrpSp->Parameters.DeviceIoControl.InputBufferLength <
                    sizeof(DZPORT_DISCONNECT)) {
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            LineNumber =
                ((PDZPORT_DISCONNECT)Irp->AssociatedIrp.SystemBuffer)->LineNumber;
            if ((LineNumber >= DZ_NUMBER_OF_LINES) ||
                (LineNumber == DEBUG_LINE)) {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            Request.Line = &Extension->Line[LineNumber];
            KeSynchronizeExecution(Extension->Interrupt, DzpDisconnectLine,
                &Request);

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
DzpInterruptService(
    IN PKINTERRUPT Interrupt,
    IN PVOID Context
    )

/*++

Routine Description:

    This routine is the interrupt service routine of the multiplexer.  It
    empties the receive silo, giving each character to the input service of
    the client of its line, and serves the transmitters that are ready: the
    output service of the client of the line sends the next character, and
    a line that has nothing more to send has its transmitter disabled.  It
    loops until the silo is empty and no transmitter is ready.

Arguments:

    Interrupt - Supplies a pointer to the interrupt object of the port.

    Context - Supplies a pointer to the device extension of the port.

Return Value:

    TRUE if the multiplexer had a request, FALSE otherwise.

--*/

{

    USHORT ControlStatus;
    PDZ_EXTENSION Extension;
    PDZ_LINE Line;
    USHORT ReceiveBuffer;
    BOOLEAN Serviced;

    UNREFERENCED_PARAMETER(Interrupt);

    Extension = (PDZ_EXTENSION)Context;
    Serviced = FALSE;
    for (;;) {

        //
        // Take the next character from the receive silo.  A character with
        // a parity or framing error is counted and discarded.
        //

        ReceiveBuffer =
            READ_REGISTER_USHORT(&Extension->ReadRegisters->ReceiveBuffer.Short);

        if (((PDZ_RECEIVE_BUFFER)(&ReceiveBuffer))->DataValid != 0) {
            Serviced = TRUE;
            Line = &Extension->Line[((PDZ_RECEIVE_BUFFER)(&ReceiveBuffer))->ReceiveLine];
            if ((((PDZ_RECEIVE_BUFFER)(&ReceiveBuffer))->ParityError != 0) ||
                (((PDZ_RECEIVE_BUFFER)(&ReceiveBuffer))->FramingError != 0)) {
                Extension->ReceiveErrors += 1;

            } else if ((Line->Connected != FALSE) && (Line->Enabled != FALSE)) {
                (Line->InputService)(Line->ClientContext,
                    (UCHAR)((PDZ_RECEIVE_BUFFER)(&ReceiveBuffer))->ReceiveData);
            }

            continue;
        }

        //
        // Serve the transmitter that the scan of the multiplexer reports
        // ready, if any.
        //

        ControlStatus =
            READ_REGISTER_USHORT(&Extension->ReadRegisters->ControlStatus.Short);

        if (((PDZ_CONTROL_STATUS)(&ControlStatus))->TransmitReady == 0) {
            break;
        }

        Serviced = TRUE;
        Line = &Extension->Line[((PDZ_CONTROL_STATUS)(&ControlStatus))->TransmitLine];
        if ((Line->Connected == FALSE) ||
            ((Line->OutputService)(Line->ClientContext) == FALSE)) {
            DzpDisableTransmit(Line);
        }
    }

    return Serviced;
}

BOOLEAN
DzpConnectLine(
    IN PVOID Context
    )

/*++

Routine Description:

    This routine connects a client to a line at the level of the interrupt
    of the port: it records the services of the client, sets the parameters
    of the line and enables its receiver.

Arguments:

    Context - Supplies the request: the line, the connect message and the
        line parameters.

Return Value:

    TRUE if the line is connected, FALSE if another client has it.

--*/

{

    PDZ_LINE Line;
    PDZ_REQUEST Request;

    Request = (PDZ_REQUEST)Context;
    Line = Request->Line;
    if (Line->Connected != FALSE) {
        return FALSE;
    }

    Line->ClientContext = Request->Connect->ClientContext;
    Line->InputService = Request->Connect->InputService;
    Line->OutputService = Request->Connect->OutputService;
    Line->LineParameters = Request->LineParameters;
    Line->Connected = TRUE;
    DzpEnableLine(Line);
    return TRUE;
}

BOOLEAN
DzpDisconnectLine(
    IN PVOID Context
    )

/*++

Routine Description:

    This routine disconnects the client of a line at the level of the
    interrupt of the port: the receiver and the transmitter of the line are
    disabled.

Arguments:

    Context - Supplies the request: the line.

Return Value:

    TRUE.

--*/

{

    PDZ_LINE Line;

    Line = ((PDZ_REQUEST)Context)->Line;
    if (Line->Connected != FALSE) {
        DzpDisableLine(Line);
        Line->Connected = FALSE;
    }

    return TRUE;
}

BOOLEAN
DzpLineParameters(
    IN ULONG LineNumber,
    IN ULONG BaudRate,
    IN ULONG CharacterLength,
    IN ULONG StopBits,
    IN ULONG Polarity,
    IN BOOLEAN ParityEnable,
    OUT PUSHORT LineParameters
    )

/*++

Routine Description:

    This routine computes the line parameter register value of a line,
    without the receiver enable.

Arguments:

    LineNumber - Supplies the number of the line.

    BaudRate - Supplies the speed of the line in bits per second.

    CharacterLength - Supplies the number of bits of a character, 5 to 8.

    StopBits - Supplies the number of stop bits, 1 or 2.

    Polarity - Supplies the parity: 0 for even, 1 for odd.

    ParityEnable - Supplies whether the line has a parity bit.

    LineParameters - Supplies a pointer to a variable that receives the
        value of the line parameter register.

Return Value:

    TRUE if the parameters are possible on the multiplexer, FALSE otherwise.

--*/

{

    USHORT DataShort;
    PDZ_BAUD_RATE Rate;

    for (Rate = &DzpBaudRates[0]; Rate->BaudRate != 0; Rate += 1) {
        if (Rate->BaudRate == BaudRate) {
            break;
        }
    }

    if ((Rate->BaudRate == 0) ||
        (CharacterLength < 5) || (CharacterLength > 8) ||
        (StopBits < 1) || (StopBits > 2) || (Polarity > 1)) {
        return FALSE;
    }

    DataShort = 0;
    ((PDZ_LINE_PARAMETERS)(&DataShort))->LineNumber = (USHORT)LineNumber;
    ((PDZ_LINE_PARAMETERS)(&DataShort))->CharacterLength = (USHORT)(CharacterLength - 5);
    ((PDZ_LINE_PARAMETERS)(&DataShort))->StopCode =
        (StopBits == 2) ? TWO_STOP_BIT : ONE_STOP_BIT;
    ((PDZ_LINE_PARAMETERS)(&DataShort))->ParityEnable = (ParityEnable != FALSE);
    ((PDZ_LINE_PARAMETERS)(&DataShort))->OddParity = (USHORT)Polarity;
    ((PDZ_LINE_PARAMETERS)(&DataShort))->SpeedCode = Rate->SpeedCode;
    *LineParameters = DataShort;
    return TRUE;
}

VOID
DzpEnableLine(
    IN PVOID PortContext
    )

/*++

Routine Description:

    This routine sets the parameters of a line and enables its receiver.
    It is called at the level of the interrupt of the port.

Arguments:

    PortContext - Supplies the line.

Return Value:

    None.

--*/

{

    USHORT DataShort;
    PDZ_LINE Line;

    Line = (PDZ_LINE)PortContext;
    DataShort = Line->LineParameters;
    ((PDZ_LINE_PARAMETERS)(&DataShort))->ReceiveEnable = 1;
    WRITE_REGISTER_USHORT(&Line->Extension->WriteRegisters->LineParameters.Short,
        DataShort);

    Line->Enabled = TRUE;
    return;
}

VOID
DzpDisableLine(
    IN PVOID PortContext
    )

/*++

Routine Description:

    This routine disables the receiver and the transmitter of a line.  It
    is called at the level of the interrupt of the port.

Arguments:

    PortContext - Supplies the line.

Return Value:

    None.

--*/

{

    PDZ_LINE Line;

    Line = (PDZ_LINE)PortContext;
    WRITE_REGISTER_USHORT(&Line->Extension->WriteRegisters->LineParameters.Short,
        Line->LineParameters);

    DzpDisableTransmit(Line);
    Line->Enabled = FALSE;
    return;
}

VOID
DzpEnableTransmit(
    IN PVOID PortContext
    )

/*++

Routine Description:

    This routine enables the transmitter of a line: when the scan of the
    multiplexer finds it ready, the port calls the output service of the
    client of the line, which sends the next character.  It is called at
    the level of the interrupt of the port.

Arguments:

    PortContext - Supplies the line.

Return Value:

    None.

--*/

{

    USHORT DataShort;
    PDZ_LINE Line;

    Line = (PDZ_LINE)PortContext;
    DataShort = READ_REGISTER_USHORT(
        &Line->Extension->ReadRegisters->TransmitControlRead.Short);

    if ((DataShort & (1 << Line->LineNumber)) == 0) {
        DataShort |= (1 << Line->LineNumber);
        WRITE_REGISTER_USHORT(
            &Line->Extension->WriteRegisters->TransmitControlWrite.Short,
            DataShort);
    }

    return;
}

VOID
DzpDisableTransmit(
    IN PDZ_LINE Line
    )

/*++

Routine Description:

    This routine disables the transmitter of a line.  The modem control
    bits of the transmit control register are kept.

Arguments:

    Line - Supplies the line.

Return Value:

    None.

--*/

{

    USHORT DataShort;

    DataShort = READ_REGISTER_USHORT(
        &Line->Extension->ReadRegisters->TransmitControlRead.Short);

    DataShort &= ~(1 << Line->LineNumber);
    WRITE_REGISTER_USHORT(
        &Line->Extension->WriteRegisters->TransmitControlWrite.Short,
        DataShort);

    return;
}

VOID
DzpOutputByte(
    IN PVOID PortContext,
    IN UCHAR OutputByte
    )

/*++

Routine Description:

    This routine sends a character on a line.  It is called by the output
    service of the client of the line, when the transmitter of the line is
    ready.

Arguments:

    PortContext - Supplies the line.

    OutputByte - Supplies the character.

Return Value:

    None.

--*/

{

    PDZ_LINE Line;

    Line = (PDZ_LINE)PortContext;
    WRITE_REGISTER_USHORT(
        &Line->Extension->WriteRegisters->TransmitBuffer.Short,
        (USHORT)OutputByte);

    return;
}

BOOLEAN
DzpSetLineParameters(
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
    level of the interrupt of the port.

Arguments:

    PortContext - Supplies the line.

    BaudRate, CharacterLength, StopBits, Polarity, ParityEnable - Supply
        the parameters (see DzpLineParameters).

Return Value:

    TRUE if the parameters are set, FALSE if the multiplexer cannot have
    them.

--*/

{

    USHORT DataShort;
    PDZ_LINE Line;

    Line = (PDZ_LINE)PortContext;
    if (DzpLineParameters(Line->LineNumber, BaudRate, CharacterLength,
                          StopBits, Polarity, ParityEnable,
                          &Line->LineParameters) == FALSE) {
        return FALSE;
    }

    DataShort = Line->LineParameters;
    if (Line->Enabled != FALSE) {
        ((PDZ_LINE_PARAMETERS)(&DataShort))->ReceiveEnable = 1;
    }

    WRITE_REGISTER_USHORT(&Line->Extension->WriteRegisters->LineParameters.Short,
        DataShort);

    return TRUE;
}
