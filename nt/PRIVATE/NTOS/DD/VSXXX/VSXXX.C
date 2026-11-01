/*++

Module Name:

    vsxxx.c

Abstract:

    This module implements the pointer port driver of the DECstation
    5000/200: the VSXXX mouse on line 1 of the DZ multiplexer, which the
    driver reaches through the DZ port driver (dzport.h).  To the mouse
    class driver it is a pointer port, as sermouse is: the class driver
    connects to \Device\PointerPort0, and the port gives it the motion and
    the buttons of the mouse.

    The driver has the mouse run its self-test, and when the mouse reports
    a self-test (the one asked for, or one after the mouse was plugged in),
    puts it in incremental stream mode, where it sends a report whenever it
    moves or a button changes: three bytes, the first one with the start of
    frame bit, the directions of the motion and the buttons that are down,
    and then how far it moved in X and in Y.  The mouse misses a command
    that comes right after its self-test report, so the driver sends the
    mode command a little later, from a timer.

    The left, right and middle buttons of the mouse are buttons 1, 2 and 3
    of the class driver.  The mouse counts Y up, the class driver down.

Environment:

    Kernel mode only.

Revision History:

--*/

#include "ntddk.h"
#include "kbdmou.h"
#include "dzport.h"

//
// Define the line of the mouse and its parameters: 4800 baud, eight bit
// characters, one stop bit, odd parity.
//

#define MOUSE_LINE 1
#define MOUSE_BAUD_RATE 4800

//
// Define the commands of the mouse.
//

#define VS_CMD_INCREMENTAL 'R'          // incremental stream mode
#define VS_CMD_SELF_TEST 'T'            // run the self-test and report it

//
// Define the reports of the mouse.  The first byte of a report has the
// start of frame bit and the type of the report; the other bytes have the
// start of frame bit clear.
//

#define VS_START_FRAME 0x80             // first byte of a report
#define VS_REPORT_TYPE 0xe0             // the type bits of the first byte
#define VS_REPORT_MOTION 0x80           // incremental report
#define VS_REPORT_SELF_TEST 0xa0        // self-test report

#define VS_MOTION_BYTES 3               // bytes of an incremental report
#define VS_SELF_TEST_BYTES 4            // bytes of a self-test report

//
// Define the bits of an incremental report: the directions and the buttons
// of the first byte, and the distance of the second (X) and the third (Y).
//

#define VS_X_POSITIVE 0x10              // the motion in X is to the right
#define VS_Y_POSITIVE 0x08              // the motion in Y is up
#define VS_LEFT_BUTTON 0x04
#define VS_MIDDLE_BUTTON 0x02
#define VS_RIGHT_BUTTON 0x01
#define VS_DISTANCE 0x7f

//
// Define the device type of the second byte of a self-test report.
//

#define VS_DEVICE_TYPE 0x0f
#define VS_DEVICE_MOUSE 0x02

//
// Define the attributes of the mouse: a serial mouse with three buttons.
// The sample rate is the most reports the line carries: 4800 baud, eleven
// bits a character, three characters a report.
//

#define MOUSE_NUMBER_OF_BUTTONS 3
#define MOUSE_SAMPLE_RATE 145

//
// Define the raw buttons of an input record: left, right and middle.
//

#define MOUSE_BUTTON_1 0x01
#define MOUSE_BUTTON_2 0x02
#define MOUSE_BUTTON_3 0x04

//
// Define the default number of entries of the input data queue, the size
// of the command queue, and how long after a self-test report the mode
// command is sent: 10 milliseconds, in 100 nanosecond units.
//

#define MOUSE_DATA_QUEUE_SIZE_DEFAULT 100
#define MOUSE_OUTPUT_SIZE 8
#define MOUSE_MODE_DELAY (10 * 1000 * 10)

//
// Define the error log values of the port.
//

#define VSXXX_ERROR_VALUE_BASE 6000

//
// Define the device extension of the pointer port.
//

typedef struct _VSXXX_EXTENSION {

    //
    // The pointer port device, its unit number and the connection of the
    // mouse class driver.
    //

    PDEVICE_OBJECT DeviceObject;
    USHORT UnitId;
    CONNECT_DATA ConnectData;
    LONG EnableCount;

    //
    // The attributes of the mouse.
    //

    MOUSE_ATTRIBUTES Attributes;

    //
    // The mouse line of the DZ port.
    //

    DZPORT_ACCEPT Port;
    PFILE_OBJECT PortFileObject;

    //
    // The input data queue: records added at the level of the interrupt of
    // the port and given to the class driver by the input DPC.  A record the
    // class driver does not take is given again a second later.
    //

    PMOUSE_INPUT_DATA InputData;
    PMOUSE_INPUT_DATA DataIn;
    PMOUSE_INPUT_DATA DataOut;
    PMOUSE_INPUT_DATA DataEnd;
    ULONG InputCount;
    ULONG QueueSize;
    BOOLEAN DpcActive;
    BOOLEAN DpcAgain;
    KDPC InputDpc;
    KTIMER RetryTimer;

    //
    // The commands for the mouse.
    //

    UCHAR Output[MOUSE_OUTPUT_SIZE];
    ULONG OutputRead;
    ULONG OutputWrite;

    //
    // The report being received: its bytes so far and the number of bytes
    // it has, zero while no report is being received.
    //

    UCHAR Report[VS_SELF_TEST_BYTES];
    ULONG ReportLength;
    ULONG ReportBytes;

    //
    // The buttons that are down in the last record given to the class
    // driver.
    //

    ULONG Buttons;

    //
    // The device type of the last self-test report, the DPC that checks
    // it, and the timer that then puts the mouse in incremental stream mode.
    //

    UCHAR DeviceType;
    KDPC SelfTestDpc;
    KTIMER ModeTimer;
    KDPC ModeDpc;
} VSXXX_EXTENSION, *PVSXXX_EXTENSION;

//
// Define the context of the input DPC's routines that are synchronized
// with the interrupt of the port.
//

typedef struct _MOUSE_INPUT_REQUEST {
    PVSXXX_EXTENSION Extension;
    PMOUSE_INPUT_DATA Start;
    ULONG Count;
    ULONG Consumed;
} MOUSE_INPUT_REQUEST, *PMOUSE_INPUT_REQUEST;

//
// Define the context of the other routines that are synchronized with the
// interrupt of the port.
//

typedef struct _MOUSE_REQUEST {
    PVSXXX_EXTENSION Extension;
    PVOID Data;
} MOUSE_REQUEST, *PMOUSE_REQUEST;

NTSTATUS
DriverEntry(
    IN PDRIVER_OBJECT DriverObject,
    IN PUNICODE_STRING RegistryPath
    );

NTSTATUS
MoupConfigurationCallout(
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
MoupServiceParameters(
    IN PUNICODE_STRING RegistryPath,
    OUT PUNICODE_STRING BaseName,
    OUT PULONG QueueSize
    );

NTSTATUS
MoupConnectPort(
    IN PVSXXX_EXTENSION Extension
    );

VOID
MoupLogError(
    IN PVOID IoObject,
    IN NTSTATUS ErrorCode,
    IN ULONG UniqueErrorValue,
    IN NTSTATUS FinalStatus
    );

NTSTATUS
MoupOpenClose(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp
    );

NTSTATUS
MoupInternalDeviceControl(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp
    );

BOOLEAN
MoupConnect(
    IN PVOID Context
    );

BOOLEAN
MoupEnable(
    IN PVOID Context
    );

BOOLEAN
MoupDisable(
    IN PVOID Context
    );

VOID
MoupInputService(
    IN PVOID ClientContext,
    IN UCHAR InputByte
    );

BOOLEAN
MoupOutputService(
    IN PVOID ClientContext
    );

VOID
MoupSendByte(
    IN PVSXXX_EXTENSION Extension,
    IN UCHAR Byte
    );

BOOLEAN
MoupSelfTest(
    IN PVOID Context
    );

BOOLEAN
MoupSetIncremental(
    IN PVOID Context
    );

VOID
MoupQueueInput(
    IN PVSXXX_EXTENSION Extension,
    IN ULONG Buttons,
    IN LONG LastX,
    IN LONG LastY
    );

VOID
MoupInputDpc(
    IN PKDPC Dpc,
    IN PVOID DeferredContext,
    IN PVOID SystemArgument1,
    IN PVOID SystemArgument2
    );

BOOLEAN
MoupBeginInput(
    IN PVOID Context
    );

BOOLEAN
MoupGetInput(
    IN PVOID Context
    );

BOOLEAN
MoupTakeInput(
    IN PVOID Context
    );

BOOLEAN
MoupEndInput(
    IN PVOID Context
    );

VOID
MoupSelfTestDpc(
    IN PKDPC Dpc,
    IN PVOID DeferredContext,
    IN PVOID SystemArgument1,
    IN PVOID SystemArgument2
    );

VOID
MoupModeDpc(
    IN PKDPC Dpc,
    IN PVOID DeferredContext,
    IN PVOID SystemArgument1,
    IN PVOID SystemArgument2
    );

#ifdef ALLOC_PRAGMA
#pragma alloc_text(INIT,DriverEntry)
#pragma alloc_text(INIT,MoupConfigurationCallout)
#pragma alloc_text(INIT,MoupServiceParameters)
#pragma alloc_text(INIT,MoupConnectPort)
#endif

NTSTATUS
DriverEntry(
    IN PDRIVER_OBJECT DriverObject,
    IN PUNICODE_STRING RegistryPath
    )

/*++

Routine Description:

    This routine initializes the pointer port driver: it finds the mouse in
    the configuration that the firmware describes, creates the pointer port
    device, connects the mouse line of the DZ port, has the mouse run its
    self-test and records the port in the device map for the mouse class
    driver.

Arguments:

    DriverObject - Supplies a pointer to the driver object of the port.

    RegistryPath - Supplies the path of the service key of the driver.

Return Value:

    The status of the initialization.

--*/

{

    UNICODE_STRING BaseName;
    CONFIGURATION_TYPE ControllerType;
    PDEVICE_OBJECT DeviceObject;
    PVSXXX_EXTENSION Extension;
    BOOLEAN Found;
    ULONG Index;
    INTERFACE_TYPE InterfaceType;
    UNICODE_STRING Name;
    WCHAR NameBuffer[64];
    UNICODE_STRING Number;
    WCHAR NumberBuffer[8];
    CONFIGURATION_TYPE PeripheralType;
    ULONG QueueSize;
    NTSTATUS Status;

    //
    // Find the mouse of the pointer controller of the internal bus.
    //

    Found = FALSE;
    InterfaceType = Internal;
    ControllerType = PointerController;
    PeripheralType = PointerPeripheral;
    IoQueryDeviceDescription(&InterfaceType, NULL, &ControllerType, NULL,
        &PeripheralType, NULL, MoupConfigurationCallout, &Found);

    if (Found == FALSE) {
        return STATUS_NO_SUCH_DEVICE;
    }

    //
    // Create the pointer port device, the first of the base name of the
    // parameters (PointerPort) and a number that is free.
    //

    MoupServiceParameters(RegistryPath, &BaseName, &QueueSize);
    Name.Buffer = NameBuffer;
    Name.MaximumLength = sizeof(NameBuffer);
    Number.Buffer = NumberBuffer;
    Number.MaximumLength = sizeof(NumberBuffer);
    Status = STATUS_OBJECT_NAME_COLLISION;
    for (Index = 0; Index < POINTER_PORTS_MAXIMUM; Index += 1) {
        Name.Length = 0;
        RtlAppendUnicodeToString(&Name, L"\\Device\\");
        RtlAppendUnicodeStringToString(&Name, &BaseName);
        RtlIntegerToUnicodeString(Index, 10, &Number);
        RtlAppendUnicodeStringToString(&Name, &Number);
        Status = IoCreateDevice(DriverObject, sizeof(VSXXX_EXTENSION),
            &Name, FILE_DEVICE_MOUSE, 0, FALSE, &DeviceObject);

        if (Status != STATUS_OBJECT_NAME_COLLISION) {
            break;
        }
    }

    if (!NT_SUCCESS(Status)) {
        MoupLogError(DriverObject, IO_ERR_INSUFFICIENT_RESOURCES,
            VSXXX_ERROR_VALUE_BASE + 1, Status);
        return Status;
    }

    DeviceObject->Flags |= DO_BUFFERED_IO;
    Extension = (PVSXXX_EXTENSION)DeviceObject->DeviceExtension;
    RtlZeroMemory(Extension, sizeof(VSXXX_EXTENSION));
    Extension->DeviceObject = DeviceObject;
    Extension->UnitId = (USHORT)Index;

    //
    // Allocate the input data queue.
    //

    Extension->QueueSize = QueueSize;
    Extension->InputData = (PMOUSE_INPUT_DATA)ExAllocatePool(NonPagedPool,
        QueueSize * sizeof(MOUSE_INPUT_DATA));

    if (Extension->InputData == NULL) {
        MoupLogError(DeviceObject, IO_ERR_INSUFFICIENT_RESOURCES,
            VSXXX_ERROR_VALUE_BASE + 2, STATUS_INSUFFICIENT_RESOURCES);
        IoDeleteDevice(DeviceObject);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Extension->DataIn = Extension->InputData;
    Extension->DataOut = Extension->InputData;
    Extension->DataEnd = Extension->InputData + QueueSize;
    KeInitializeDpc(&Extension->InputDpc, MoupInputDpc, Extension);
    KeInitializeTimer(&Extension->RetryTimer);
    KeInitializeDpc(&Extension->SelfTestDpc, MoupSelfTestDpc, Extension);
    KeInitializeDpc(&Extension->ModeDpc, MoupModeDpc, Extension);
    KeInitializeTimer(&Extension->ModeTimer);

    //
    // The mouse is a serial mouse of the class driver, with three buttons.
    //

    Extension->Attributes.MouseIdentifier = MOUSE_SERIAL_HARDWARE;
    Extension->Attributes.NumberOfButtons = MOUSE_NUMBER_OF_BUTTONS;
    Extension->Attributes.SampleRate = MOUSE_SAMPLE_RATE;
    Extension->Attributes.InputDataQueueLength =
        QueueSize * sizeof(MOUSE_INPUT_DATA);

    //
    // Connect the mouse line of the DZ port and have the mouse run its
    // self-test, after which it is put in incremental stream mode.
    //

    Status = MoupConnectPort(Extension);
    if (!NT_SUCCESS(Status)) {
        MoupLogError(DeviceObject, IO_ERR_LAYERED_FAILURE,
            VSXXX_ERROR_VALUE_BASE + 3, Status);
        ExFreePool(Extension->InputData);
        IoDeleteDevice(DeviceObject);
        return Status;
    }

    KeSynchronizeExecution(Extension->Port.Interrupt, MoupSelfTest,
        Extension);

    DriverObject->MajorFunction[IRP_MJ_CREATE] = MoupOpenClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = MoupOpenClose;
    DriverObject->MajorFunction[IRP_MJ_INTERNAL_DEVICE_CONTROL] =
        MoupInternalDeviceControl;

    //
    // Record the port in the device map, where the class driver finds it.
    //

    RtlWriteRegistryValue(RTL_REGISTRY_DEVICEMAP, DD_POINTER_PORT_BASE_NAME_U,
        Name.Buffer, REG_SZ, RegistryPath->Buffer,
        RegistryPath->Length + sizeof(UNICODE_NULL));

    return STATUS_SUCCESS;
}

NTSTATUS
MoupConfigurationCallout(
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

    This routine is the callout of IoQueryDeviceDescription for the mouse:
    it records that the configuration has it.

Arguments:

    Context - Supplies a pointer to the variable that is set to TRUE.

    The others - Not used.

Return Value:

    STATUS_SUCCESS.

--*/

{

    UNREFERENCED_PARAMETER(PathName);
    UNREFERENCED_PARAMETER(BusType);
    UNREFERENCED_PARAMETER(BusNumber);
    UNREFERENCED_PARAMETER(BusInformation);
    UNREFERENCED_PARAMETER(ControllerType);
    UNREFERENCED_PARAMETER(ControllerNumber);
    UNREFERENCED_PARAMETER(ControllerInformation);
    UNREFERENCED_PARAMETER(PeripheralType);
    UNREFERENCED_PARAMETER(PeripheralNumber);
    UNREFERENCED_PARAMETER(PeripheralInformation);

    *(PBOOLEAN)Context = TRUE;
    return STATUS_SUCCESS;
}

VOID
MoupServiceParameters(
    IN PUNICODE_STRING RegistryPath,
    OUT PUNICODE_STRING BaseName,
    OUT PULONG QueueSize
    )

/*++

Routine Description:

    This routine reads the parameters of the driver from the Parameters key
    of its service key: the base name of the pointer port device
    (PointerDeviceBaseName) and the number of entries of the input data
    queue (MouseDataQueueSize).  A parameter that is missing has its default
    value.

Arguments:

    RegistryPath - Supplies the path of the service key of the driver.

    BaseName - Supplies a pointer to the string that receives the base
        name; its buffer is allocated from the paged pool, or is the
        default name.

    QueueSize - Supplies a pointer to the variable that receives the number
        of entries of the queue.

Return Value:

    None.

--*/

{

    ULONG DefaultQueueSize;
    UNICODE_STRING DefaultName;
    UNICODE_STRING ParametersPath;
    RTL_QUERY_REGISTRY_TABLE Parameters[3];
    NTSTATUS Status;

    DefaultQueueSize = MOUSE_DATA_QUEUE_SIZE_DEFAULT;
    RtlInitUnicodeString(&DefaultName, DD_POINTER_PORT_BASE_NAME_U);
    *QueueSize = DefaultQueueSize;
    RtlInitUnicodeString(BaseName, NULL);

    ParametersPath.MaximumLength = RegistryPath->Length + sizeof(L"\\Parameters");
    ParametersPath.Length = 0;
    ParametersPath.Buffer = ExAllocatePool(PagedPool, ParametersPath.MaximumLength);
    if (ParametersPath.Buffer != NULL) {
        RtlAppendUnicodeStringToString(&ParametersPath, RegistryPath);
        RtlAppendUnicodeToString(&ParametersPath, L"\\Parameters");
        RtlZeroMemory(Parameters, sizeof(Parameters));
        Parameters[0].Flags = RTL_QUERY_REGISTRY_DIRECT;
        Parameters[0].Name = L"MouseDataQueueSize";
        Parameters[0].EntryContext = QueueSize;
        Parameters[0].DefaultType = REG_DWORD;
        Parameters[0].DefaultData = &DefaultQueueSize;
        Parameters[0].DefaultLength = sizeof(ULONG);
        Parameters[1].Flags = RTL_QUERY_REGISTRY_DIRECT;
        Parameters[1].Name = L"PointerDeviceBaseName";
        Parameters[1].EntryContext = BaseName;
        Parameters[1].DefaultType = REG_SZ;
        Parameters[1].DefaultData = DefaultName.Buffer;
        Parameters[1].DefaultLength = 0;
        Status = RtlQueryRegistryValues(RTL_REGISTRY_ABSOLUTE,
            ParametersPath.Buffer, Parameters, NULL, NULL);

        ExFreePool(ParametersPath.Buffer);
        if (!NT_SUCCESS(Status)) {
            *QueueSize = DefaultQueueSize;
        }
    }

    if (BaseName->Length == 0) {
        *BaseName = DefaultName;
    }

    if (*QueueSize == 0) {
        *QueueSize = DefaultQueueSize;
    }

    return;
}

NTSTATUS
MoupConnectPort(
    IN PVSXXX_EXTENSION Extension
    )

/*++

Routine Description:

    This routine connects the mouse line of the DZ port: the port gets the
    input and output services of the mouse driver, and the driver gets the
    routines of the port.

Arguments:

    Extension - Supplies a pointer to the device extension of the pointer
        port.

Return Value:

    The status of the connection.

--*/

{

    DZPORT_CONNECT Connect;
    KEVENT Event;
    PFILE_OBJECT FileObject;
    PIRP Irp;
    IO_STATUS_BLOCK IoStatus;
    UNICODE_STRING Name;
    PDEVICE_OBJECT PortDevice;
    NTSTATUS Status;

    RtlInitUnicodeString(&Name, L"\\Device\\DzPort");
    Status = IoGetDeviceObjectPointer(&Name, FILE_READ_ATTRIBUTES,
        &FileObject, &PortDevice);

    if (!NT_SUCCESS(Status)) {
        return Status;
    }

    Connect.ClientContext = Extension;
    Connect.InputService = MoupInputService;
    Connect.OutputService = MoupOutputService;
    Connect.LineNumber = MOUSE_LINE;
    Connect.BaudRate = MOUSE_BAUD_RATE;
    Connect.CharacterLength = 8;
    Connect.StopBits = 1;
    Connect.Polarity = 1;
    Connect.ParityEnable = TRUE;
    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp = IoBuildDeviceIoControlRequest(IOCTL_MN_DZPORT_CONNECT, PortDevice,
        &Connect, sizeof(DZPORT_CONNECT), &Extension->Port,
        sizeof(DZPORT_ACCEPT), TRUE, &Event, &IoStatus);

    if (Irp == NULL) {
        ObDereferenceObject(FileObject);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = IoCallDriver(PortDevice, Irp);
    if (Status == STATUS_PENDING) {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatus.Status;
    }

    if (!NT_SUCCESS(Status)) {
        ObDereferenceObject(FileObject);
        return Status;
    }

    Extension->PortFileObject = FileObject;
    return STATUS_SUCCESS;
}

VOID
MoupLogError(
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

NTSTATUS
MoupOpenClose(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp
    )

/*++

Routine Description:

    This routine is the dispatch routine of the create and close requests:
    they succeed.

Arguments:

    DeviceObject - Supplies a pointer to the pointer port device object.

    Irp - Supplies a pointer to the request packet.

Return Value:

    STATUS_SUCCESS.

--*/

{

    UNREFERENCED_PARAMETER(DeviceObject);

    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

NTSTATUS
MoupInternalDeviceControl(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp
    )

/*++

Routine Description:

    This routine is the dispatch routine of the internal device control
    requests of the mouse class driver: the connection, enabling and
    disabling the input, and the attributes of the mouse.

Arguments:

    DeviceObject - Supplies a pointer to the pointer port device object.

    Irp - Supplies a pointer to the request packet.

Return Value:

    The status of the request.

--*/

{

    PVOID Buffer;
    PVSXXX_EXTENSION Extension;
    ULONG InputLength;
    PIO_STACK_LOCATION IrpSp;
    ULONG OutputLength;
    MOUSE_REQUEST Request;
    NTSTATUS Status;

    Extension = (PVSXXX_EXTENSION)DeviceObject->DeviceExtension;
    IrpSp = IoGetCurrentIrpStackLocation(Irp);
    InputLength = IrpSp->Parameters.DeviceIoControl.InputBufferLength;
    OutputLength = IrpSp->Parameters.DeviceIoControl.OutputBufferLength;
    Buffer = Irp->AssociatedIrp.SystemBuffer;
    Request.Extension = Extension;
    Irp->IoStatus.Information = 0;
    Status = STATUS_SUCCESS;
    switch (IrpSp->Parameters.DeviceIoControl.IoControlCode) {

    //
    // Connect the class driver.  Only one class driver connects.
    //

    case IOCTL_INTERNAL_MOUSE_CONNECT:
        if (Extension->ConnectData.ClassService != NULL) {
            Status = STATUS_SHARING_VIOLATION;

        } else if (InputLength < sizeof(CONNECT_DATA)) {
            Status = STATUS_INVALID_PARAMETER;

        } else {
            Request.Data = IrpSp->Parameters.DeviceIoControl.Type3InputBuffer;
            KeSynchronizeExecution(Extension->Port.Interrupt, MoupConnect,
                &Request);
        }

        break;

    case IOCTL_INTERNAL_MOUSE_DISCONNECT:
        Status = STATUS_NOT_IMPLEMENTED;
        break;

    //
    // Enable or disable the input: the reports are given to the class
    // driver while it has enabled the input more times than disabled it.
    //

    case IOCTL_INTERNAL_MOUSE_ENABLE:
        KeSynchronizeExecution(Extension->Port.Interrupt, MoupEnable,
            &Request);
        break;

    case IOCTL_INTERNAL_MOUSE_DISABLE:
        if (KeSynchronizeExecution(Extension->Port.Interrupt, MoupDisable,
                &Request) == FALSE) {
            Status = STATUS_DEVICE_DATA_ERROR;
        }

        break;

    case IOCTL_MOUSE_QUERY_ATTRIBUTES:
        if (OutputLength < sizeof(MOUSE_ATTRIBUTES)) {
            Status = STATUS_BUFFER_TOO_SMALL;

        } else {
            *(PMOUSE_ATTRIBUTES)Buffer = Extension->Attributes;
            Irp->IoStatus.Information = sizeof(MOUSE_ATTRIBUTES);
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
MoupConnect(
    IN PVOID Context
    )

/*++

Routine Description:

    This routine records the connection of the class driver and empties
    the input data queue, at the level of the interrupt of the port.

Arguments:

    Context - Supplies the request: the device extension and the connect
        data of the class driver.

Return Value:

    TRUE.

--*/

{

    PVSXXX_EXTENSION Extension;
    PMOUSE_REQUEST Request;

    Request = (PMOUSE_REQUEST)Context;
    Extension = Request->Extension;
    Extension->ConnectData = *(PCONNECT_DATA)Request->Data;
    Extension->DataIn = Extension->InputData;
    Extension->DataOut = Extension->InputData;
    Extension->InputCount = 0;
    return TRUE;
}

BOOLEAN
MoupEnable(
    IN PVOID Context
    )

/*++

Routine Description:

    This routine enables the input once more, at the level of the interrupt
    of the port.

Arguments:

    Context - Supplies the request: the device extension.

Return Value:

    TRUE.

--*/

{

    ((PMOUSE_REQUEST)Context)->Extension->EnableCount += 1;
    return TRUE;
}

BOOLEAN
MoupDisable(
    IN PVOID Context
    )

/*++

Routine Description:

    This routine disables the input once more, at the level of the
    interrupt of the port.

Arguments:

    Context - Supplies the request: the device extension.

Return Value:

    TRUE if the input was enabled, FALSE otherwise.

--*/

{

    PVSXXX_EXTENSION Extension;

    Extension = ((PMOUSE_REQUEST)Context)->Extension;
    if (Extension->EnableCount == 0) {
        return FALSE;
    }

    Extension->EnableCount -= 1;
    return TRUE;
}

VOID
MoupInputService(
    IN PVOID ClientContext,
    IN UCHAR InputByte
    )

/*++

Routine Description:

    This routine is the input service of the mouse line: the DZ port calls
    it, at the level of its interrupt, with each character from the mouse.
    A character with the start of frame bit begins a report, and the others
    are its bytes; the bytes of a report of another type (the absolute
    reports of a tablet), and those that no report begins, are discarded.
    An incremental report is given to the class driver.  A self-test report
    means that the mouse was reset: the buttons that were down are up, and
    the mouse is to be put in incremental stream mode again.

Arguments:

    ClientContext - Supplies a pointer to the device extension of the
        pointer port.

    InputByte - Supplies the character.

Return Value:

    None.

--*/

{

    ULONG Buttons;
    PVSXXX_EXTENSION Extension;
    LONG LastX;
    LONG LastY;
    PUCHAR Report;

    Extension = (PVSXXX_EXTENSION)ClientContext;
    Report = Extension->Report;
    if ((InputByte & VS_START_FRAME) != 0) {
        switch (InputByte & VS_REPORT_TYPE) {
        case VS_REPORT_MOTION:
            Extension->ReportBytes = VS_MOTION_BYTES;
            break;

        case VS_REPORT_SELF_TEST:
            Extension->ReportBytes = VS_SELF_TEST_BYTES;
            break;

        default:
            Extension->ReportBytes = 0;
            break;
        }

        Report[0] = InputByte;
        Extension->ReportLength = 1;

    } else if (Extension->ReportLength < Extension->ReportBytes) {
        Report[Extension->ReportLength] = InputByte;
        Extension->ReportLength += 1;

    } else {
        return;
    }

    if (Extension->ReportLength != Extension->ReportBytes) {
        return;
    }

    //
    // The report is complete.
    //

    Extension->ReportBytes = 0;
    if ((Report[0] & VS_REPORT_TYPE) == VS_REPORT_MOTION) {
        Buttons = 0;
        if ((Report[0] & VS_LEFT_BUTTON) != 0) {
            Buttons |= MOUSE_BUTTON_1;
        }

        if ((Report[0] & VS_RIGHT_BUTTON) != 0) {
            Buttons |= MOUSE_BUTTON_2;
        }

        if ((Report[0] & VS_MIDDLE_BUTTON) != 0) {
            Buttons |= MOUSE_BUTTON_3;
        }

        LastX = Report[1] & VS_DISTANCE;
        if ((Report[0] & VS_X_POSITIVE) == 0) {
            LastX = -LastX;
        }

        LastY = Report[2] & VS_DISTANCE;
        if ((Report[0] & VS_Y_POSITIVE) != 0) {
            LastY = -LastY;
        }

        MoupQueueInput(Extension, Buttons, LastX, LastY);

    } else {
        MoupQueueInput(Extension, 0, 0, 0);
        Extension->DeviceType = (UCHAR)(Report[1] & VS_DEVICE_TYPE);
        KeInsertQueueDpc(&Extension->SelfTestDpc, NULL, NULL);
    }

    return;
}

BOOLEAN
MoupOutputService(
    IN PVOID ClientContext
    )

/*++

Routine Description:

    This routine is the output service of the mouse line: the DZ port calls
    it, at the level of its interrupt, when the transmitter of the line is
    ready.  It sends the next command to the mouse.

Arguments:

    ClientContext - Supplies a pointer to the device extension of the
        pointer port.

Return Value:

    TRUE if a byte was sent, FALSE if there is nothing to send.

--*/

{

    PVSXXX_EXTENSION Extension;

    Extension = (PVSXXX_EXTENSION)ClientContext;
    if (Extension->OutputRead == Extension->OutputWrite) {
        return FALSE;
    }

    (Extension->Port.OutputByte)(Extension->Port.PortContext,
        Extension->Output[Extension->OutputRead]);

    Extension->OutputRead = (Extension->OutputRead + 1) % MOUSE_OUTPUT_SIZE;
    return TRUE;
}

VOID
MoupSendByte(
    IN PVSXXX_EXTENSION Extension,
    IN UCHAR Byte
    )

/*++

Routine Description:

    This routine queues a command for the mouse and enables the transmitter
    of the line.  It is called at the level of the interrupt of the port.

Arguments:

    Extension - Supplies a pointer to the device extension of the pointer
        port.

    Byte - Supplies the command.

Return Value:

    None.

--*/

{

    ULONG Next;

    Next = (Extension->OutputWrite + 1) % MOUSE_OUTPUT_SIZE;
    if (Next != Extension->OutputRead) {
        Extension->Output[Extension->OutputWrite] = Byte;
        Extension->OutputWrite = Next;
        (Extension->Port.EnableTransmit)(Extension->Port.PortContext);
    }

    return;
}

BOOLEAN
MoupSelfTest(
    IN PVOID Context
    )

/*++

Routine Description:

    This routine has the mouse run its self-test, which it reports.  It is
    called at the level of the interrupt of the port.

Arguments:

    Context - Supplies a pointer to the device extension of the pointer
        port.

Return Value:

    TRUE.

--*/

{

    MoupSendByte((PVSXXX_EXTENSION)Context, VS_CMD_SELF_TEST);
    return TRUE;
}

BOOLEAN
MoupSetIncremental(
    IN PVOID Context
    )

/*++

Routine Description:

    This routine puts the mouse in incremental stream mode.  It is called at
    the level of the interrupt of the port.

Arguments:

    Context - Supplies a pointer to the device extension of the pointer
        port.

Return Value:

    TRUE.

--*/

{

    MoupSendByte((PVSXXX_EXTENSION)Context, VS_CMD_INCREMENTAL);
    return TRUE;
}

VOID
MoupQueueInput(
    IN PVSXXX_EXTENSION Extension,
    IN ULONG Buttons,
    IN LONG LastX,
    IN LONG LastY
    )

/*++

Routine Description:

    This routine puts a record in the input data queue: the motion, and the
    buttons that went down or up since the last record.  It then queues the
    input DPC, which gives the record to the class driver.  The input is
    discarded while no class driver is connected or the input is disabled,
    and when nothing moved or changed; a record the full queue has no room
    for is lost, and the next one has its button changes.  It is called at
    the level of the interrupt of the port.

Arguments:

    Extension - Supplies a pointer to the device extension of the pointer
        port.

    Buttons - Supplies the buttons that are down (MOUSE_BUTTON_1 to 3).

    LastX - Supplies the motion in X, to the right.

    LastY - Supplies the motion in Y, down.

Return Value:

    None.

--*/

{

    ULONG Changed;
    PMOUSE_INPUT_DATA Input;
    ULONG Transitions;

    if ((Extension->ConnectData.ClassService == NULL) ||
        (Extension->EnableCount == 0)) {
        return;
    }

    Changed = Extension->Buttons ^ Buttons;
    if ((Changed == 0) && (LastX == 0) && (LastY == 0)) {
        return;
    }

    if (Extension->InputCount == Extension->QueueSize) {
        return;
    }

    Transitions = 0;
    if ((Changed & MOUSE_BUTTON_1) != 0) {
        Transitions |= ((Buttons & MOUSE_BUTTON_1) != 0) ?
            MOUSE_BUTTON_1_DOWN : MOUSE_BUTTON_1_UP;
    }

    if ((Changed & MOUSE_BUTTON_2) != 0) {
        Transitions |= ((Buttons & MOUSE_BUTTON_2) != 0) ?
            MOUSE_BUTTON_2_DOWN : MOUSE_BUTTON_2_UP;
    }

    if ((Changed & MOUSE_BUTTON_3) != 0) {
        Transitions |= ((Buttons & MOUSE_BUTTON_3) != 0) ?
            MOUSE_BUTTON_3_DOWN : MOUSE_BUTTON_3_UP;
    }

    Extension->Buttons = Buttons;
    Input = Extension->DataIn;
    Input->UnitId = Extension->UnitId;
    Input->Flags = MOUSE_MOVE_RELATIVE;
    Input->Buttons = Transitions;
    Input->RawButtons = Buttons;
    Input->LastX = LastX;
    Input->LastY = LastY;
    Input->ExtraInformation = 0;
    Extension->DataIn += 1;
    if (Extension->DataIn == Extension->DataEnd) {
        Extension->DataIn = Extension->InputData;
    }

    Extension->InputCount += 1;
    KeInsertQueueDpc(&Extension->InputDpc, NULL, NULL);
    return;
}

VOID
MoupInputDpc(
    IN PKDPC Dpc,
    IN PVOID DeferredContext,
    IN PVOID SystemArgument1,
    IN PVOID SystemArgument2
    )

/*++

Routine Description:

    This routine is the input DPC: it gives the records of the input data
    queue to the class driver, through the service callback of its
    connection, the records up to the end of the buffer first.  If the
    class driver does not take them all, they are given again a second
    later.  One processor runs the routine at a time: a processor that
    finds it running has the one that runs it go round once more.

Arguments:

    Dpc - Supplies a pointer to the DPC object.

    DeferredContext - Supplies a pointer to the device extension of the
        pointer port.

    SystemArgument1, SystemArgument2 - Not used.

Return Value:

    None.

--*/

{

    LARGE_INTEGER DueTime;
    PVSXXX_EXTENSION Extension;
    MOUSE_INPUT_REQUEST Request;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    Extension = (PVSXXX_EXTENSION)DeferredContext;
    Request.Extension = Extension;
    if (KeSynchronizeExecution(Extension->Port.Interrupt, MoupBeginInput,
            &Request) == FALSE) {
        return;
    }

    do {
        while (KeSynchronizeExecution(Extension->Port.Interrupt, MoupGetInput,
                   &Request) != FALSE) {

            Request.Consumed = 0;
            (*(PSERVICE_CALLBACK_ROUTINE)Extension->ConnectData.ClassService)(
                Extension->ConnectData.ClassDeviceObject,
                Request.Start,
                Request.Start + Request.Count,
                &Request.Consumed);

            KeSynchronizeExecution(Extension->Port.Interrupt, MoupTakeInput,
                &Request);

            if (Request.Consumed < Request.Count) {
                DueTime.LowPart = (ULONG)(-10 * 1000 * 1000);
                DueTime.HighPart = -1;
                KeSetTimer(&Extension->RetryTimer, DueTime,
                    &Extension->InputDpc);

                break;
            }
        }

    } while (KeSynchronizeExecution(Extension->Port.Interrupt, MoupEndInput,
                 &Request) == FALSE);

    return;
}

BOOLEAN
MoupBeginInput(
    IN PVOID Context
    )

/*++

Routine Description:

    This routine marks the input DPC running, unless it already runs, at
    the level of the interrupt of the port.

Arguments:

    Context - Supplies the request: the device extension.

Return Value:

    TRUE if the DPC is to give the input, FALSE if another processor runs
    it, which is to go round once more.

--*/

{

    PVSXXX_EXTENSION Extension;

    Extension = ((PMOUSE_INPUT_REQUEST)Context)->Extension;
    if (Extension->DpcActive != FALSE) {
        Extension->DpcAgain = TRUE;
        return FALSE;
    }

    Extension->DpcActive = TRUE;
    Extension->DpcAgain = FALSE;
    return TRUE;
}

BOOLEAN
MoupGetInput(
    IN PVOID Context
    )

/*++

Routine Description:

    This routine gets the records of the input data queue up to the end of
    its buffer, at the level of the interrupt of the port.

Arguments:

    Context - Supplies the request: the device extension, which receives
        the first record and the number of records.

Return Value:

    TRUE if the queue has records, FALSE otherwise.

--*/

{

    PVSXXX_EXTENSION Extension;
    PMOUSE_INPUT_REQUEST Request;

    Request = (PMOUSE_INPUT_REQUEST)Context;
    Extension = Request->Extension;
    if ((Extension->InputCount == 0) ||
        (Extension->ConnectData.ClassService == NULL)) {
        return FALSE;
    }

    Request->Start = Extension->DataOut;
    Request->Count = Extension->DataEnd - Extension->DataOut;
    if (Request->Count > Extension->InputCount) {
        Request->Count = Extension->InputCount;
    }

    return TRUE;
}

BOOLEAN
MoupTakeInput(
    IN PVOID Context
    )

/*++

Routine Description:

    This routine removes the records the class driver took from the input
    data queue, at the level of the interrupt of the port.

Arguments:

    Context - Supplies the request: the device extension and the number of
        records taken.

Return Value:

    TRUE.

--*/

{

    PVSXXX_EXTENSION Extension;
    PMOUSE_INPUT_REQUEST Request;

    Request = (PMOUSE_INPUT_REQUEST)Context;
    Extension = Request->Extension;
    Extension->DataOut += Request->Consumed;
    if (Extension->DataOut == Extension->DataEnd) {
        Extension->DataOut = Extension->InputData;
    }

    Extension->InputCount -= Request->Consumed;
    return TRUE;
}

BOOLEAN
MoupEndInput(
    IN PVOID Context
    )

/*++

Routine Description:

    This routine marks the input DPC done, unless another processor found
    it running, at the level of the interrupt of the port.

Arguments:

    Context - Supplies the request: the device extension.

Return Value:

    TRUE if the DPC is done, FALSE if it is to go round once more.

--*/

{

    PVSXXX_EXTENSION Extension;

    Extension = ((PMOUSE_INPUT_REQUEST)Context)->Extension;
    if (Extension->DpcAgain != FALSE) {
        Extension->DpcAgain = FALSE;
        return FALSE;
    }

    Extension->DpcActive = FALSE;
    return TRUE;
}

VOID
MoupSelfTestDpc(
    IN PKDPC Dpc,
    IN PVOID DeferredContext,
    IN PVOID SystemArgument1,
    IN PVOID SystemArgument2
    )

/*++

Routine Description:

    This routine is the DPC of a self-test report.  It writes an entry of
    the error log if the device on the line is not a mouse, and sets the
    mode timer, which puts the device in incremental stream mode a little
    later: the mouse misses a command that comes right after its self-test
    report.

Arguments:

    Dpc - Supplies a pointer to the DPC object.

    DeferredContext - Supplies a pointer to the device extension of the
        pointer port.

    SystemArgument1, SystemArgument2 - Not used.

Return Value:

    None.

--*/

{

    LARGE_INTEGER DueTime;
    PVSXXX_EXTENSION Extension;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    Extension = (PVSXXX_EXTENSION)DeferredContext;
    if (Extension->DeviceType != VS_DEVICE_MOUSE) {
        MoupLogError(Extension->DeviceObject, IO_ERR_CONFIGURATION_ERROR,
            VSXXX_ERROR_VALUE_BASE + 4, STATUS_DEVICE_CONFIGURATION_ERROR);
    }

    DueTime.LowPart = (ULONG)(-MOUSE_MODE_DELAY);
    DueTime.HighPart = -1;
    KeSetTimer(&Extension->ModeTimer, DueTime, &Extension->ModeDpc);
    return;
}

VOID
MoupModeDpc(
    IN PKDPC Dpc,
    IN PVOID DeferredContext,
    IN PVOID SystemArgument1,
    IN PVOID SystemArgument2
    )

/*++

Routine Description:

    This routine is the DPC of the mode timer: it puts the mouse in
    incremental stream mode.

Arguments:

    Dpc - Supplies a pointer to the DPC object.

    DeferredContext - Supplies a pointer to the device extension of the
        pointer port.

    SystemArgument1, SystemArgument2 - Not used.

Return Value:

    None.

--*/

{

    PVSXXX_EXTENSION Extension;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    Extension = (PVSXXX_EXTENSION)DeferredContext;
    KeSynchronizeExecution(Extension->Port.Interrupt, MoupSetIncremental,
        Extension);

    return;
}
