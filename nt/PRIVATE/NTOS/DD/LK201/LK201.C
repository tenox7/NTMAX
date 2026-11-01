/*++

Module Name:

    lk201.c

Abstract:

    This module implements the keyboard port driver of the DECstation
    5000/200: the LK201 keyboard (or an LK401) on line 0 of the DZ
    multiplexer, which the driver reaches through the DZ port driver
    (dzport.h).  To the keyboard class driver it is a keyboard port, as
    i8042prt is: the class driver connects to \Device\KeyboardPort0, and the
    port gives it the keys as an enhanced keyboard gives them, scan codes of
    set 1.

    The keyboard is set up so that every key has a down and an up: all its
    divisions in down/up mode, where it sends the code of a key when the
    key goes down and again when it goes up, or ALL UPS when the last key
    goes up.  The keyboard does not repeat keys in that mode, so the driver
    repeats them, at the typematic rate and after the typematic delay, but
    not the shift, control, alternate and lock keys.  When the keyboard
    reports a power-up self-test (it was plugged in or reset), it has its
    default modes again and is set up again.

    The keys of the LK201 that the PC keyboard has are given their PC codes,
    by their legends.  The others: F11 is Escape (the VT220's F11 sends
    ESC), F12 and F13 are F11 and F12, F14, Help and Do are Print Screen,
    Scroll Lock and Pause; Compose is the left Alt (the LK401's Alt keys and
    right Compose are Alt too), Lock is Caps Lock, <X] is Backspace; Find,
    Insert Here, Remove, Select, Prev Screen and Next Screen are Home,
    Insert, Delete, End, Page Up and Page Down; PF1 to PF4 are Num Lock and
    the keypad's /, * and -, and the keypad's comma is its +.  F17 to F20
    have no PC key and are not reported.  The indicators are the LEDs: Caps
    Lock is Lock, Scroll Lock is Hold Screen and Num Lock is Wait.

Environment:

    Kernel mode only.

Revision History:

--*/

#include "ntddk.h"
#include "kbdmou.h"
#include "dzport.h"

//
// Define the line of the keyboard and its parameters: 4800 baud, eight bit
// characters, one stop bit, no parity.
//

#define KEYBOARD_LINE 0
#define KEYBOARD_BAUD_RATE 4800

//
// Define the codes the keyboard sends.
//

#define LK_LOWEST 0x56                  // lowest key code
#define LK_ALL_UPS 0xb3                 // the last key went up
#define LK_METRONOME 0xb4               // auto-repeat of the last key
#define LK_OUTPUT_ERROR 0xb5            // keys lost while inhibited
#define LK_INPUT_ERROR 0xb6             // invalid command
#define LK_LOCKED_ACK 0xb7              // keyboard locked acknowledge
#define LK_TEST_MODE_ACK 0xb8           // test mode acknowledge
#define LK_PREFIX_KEYS_DOWN 0xb9        // prefix to the keys that are down
#define LK_MODE_CHANGE_ACK 0xba         // mode change acknowledge
#define LK_RESERVED 0x7f                // reserved code

#define LK_MAXIMUM_ID 0x04              // first byte of a power-up report
#define LK_POWER_UP_BYTES 3             // bytes of the report after it

#define LK_KEY_CTRL 0xaf                // the Ctrl key

//
// Define the commands to the keyboard.
//

#define LK_CMD_LED_OFF 0x11             // turn LEDs off (one parameter)
#define LK_CMD_LED_ON 0x13              // turn LEDs on (one parameter)
#define LK_CMD_RING_BELL 0xa7           // sound the bell
#define LK_CMD_SET_DEFAULTS 0xd3        // reinstate the default modes
#define LK_CMD_ENABLE_LK401 0xe9        // LK401: report its own keys
#define LK_CMD_MODE_UP_DOWN 0x86        // division mode: down/up
#define LK_DIVISIONS 14                 // number of divisions

#define LK_LED_PARAMETER 0x80           // last parameter of a command
#define LK_LED_WAIT 0x01
#define LK_LED_COMPOSE 0x02
#define LK_LED_LOCK 0x04
#define LK_LED_HOLD_SCREEN 0x08
#define LK_LED_ALL 0x0f

//
// Define the keyboard the port reports: an enhanced keyboard of 101 keys
// that gives scan codes of set 1.
//

#define KEYBOARD_TYPE_ENHANCED 4
#define KEYBOARD_SCAN_CODE_SET 1
#define KEYBOARD_FUNCTION_KEYS 12
#define KEYBOARD_KEYS 101

//
// Minimum, maximum, and default values for keyboard typematic rate and delay.
//

#define KEYBOARD_TYPEMATIC_RATE_MINIMUM     2
#define KEYBOARD_TYPEMATIC_RATE_MAXIMUM    30
#define KEYBOARD_TYPEMATIC_RATE_DEFAULT    30
#define KEYBOARD_TYPEMATIC_DELAY_MINIMUM  250
#define KEYBOARD_TYPEMATIC_DELAY_MAXIMUM 1000
#define KEYBOARD_TYPEMATIC_DELAY_DEFAULT  250

//
// Define the default number of entries of the input data queue, and the
// size of the command queue.
//

#define KEYBOARD_DATA_QUEUE_SIZE_DEFAULT 100
#define KEYBOARD_OUTPUT_SIZE 64

//
// Define the error log values of the port.
//

#define LK201_ERROR_VALUE_BASE 5000

//
// Define the device extension of the keyboard port.
//

typedef struct _LK201_EXTENSION {

    //
    // The keyboard port device, its unit number and the connection of the
    // keyboard class driver.
    //

    PDEVICE_OBJECT DeviceObject;
    USHORT UnitId;
    CONNECT_DATA ConnectData;
    LONG EnableCount;

    //
    // The attributes, indicators and typematic parameters of the keyboard.
    //

    KEYBOARD_ATTRIBUTES Attributes;
    KEYBOARD_INDICATOR_PARAMETERS Indicators;
    KEYBOARD_TYPEMATIC_PARAMETERS Typematic;

    //
    // The keyboard line of the DZ port.
    //

    DZPORT_ACCEPT Port;
    PFILE_OBJECT PortFileObject;

    //
    // The input data queue: records added at the level of the interrupt of
    // the port and given to the class driver by the input DPC.  A record the
    // class driver does not take is given again a second later.
    //

    PKEYBOARD_INPUT_DATA InputData;
    PKEYBOARD_INPUT_DATA DataIn;
    PKEYBOARD_INPUT_DATA DataOut;
    PKEYBOARD_INPUT_DATA DataEnd;
    ULONG InputCount;
    ULONG QueueSize;
    BOOLEAN Overflow;
    BOOLEAN DpcActive;
    BOOLEAN DpcAgain;
    KDPC InputDpc;
    KTIMER RetryTimer;

    //
    // The commands for the keyboard.
    //

    UCHAR Output[KEYBOARD_OUTPUT_SIZE];
    ULONG OutputRead;
    ULONG OutputWrite;

    //
    // The keys that are down, the bytes of a power-up report still to come,
    // and the key that repeats.
    //

    ULONG KeyDown[256 / 32];
    ULONG PowerUpBytes;
    UCHAR TypematicKey;
    BOOLEAN TypematicStart;
    KTIMER TypematicTimer;
    KDPC TypematicDpc;
    KDPC StartDpc;
} LK201_EXTENSION, *PLK201_EXTENSION;

//
// Define the context of the input DPC's routines that are synchronized
// with the interrupt of the port.
//

typedef struct _KEYBOARD_INPUT_REQUEST {
    PLK201_EXTENSION Extension;
    PKEYBOARD_INPUT_DATA Start;
    ULONG Count;
    ULONG Consumed;
} KEYBOARD_INPUT_REQUEST, *PKEYBOARD_INPUT_REQUEST;

//
// Define the context of the other routines that are synchronized with the
// interrupt of the port.
//

typedef struct _KEYBOARD_REQUEST {
    PLK201_EXTENSION Extension;
    PVOID Data;
} KEYBOARD_REQUEST, *PKEYBOARD_REQUEST;

//
// The make codes of the indicator keys.
//

INDICATOR_LIST KbdIndicatorList[] = {
    {0x3a, KEYBOARD_CAPS_LOCK_ON},
    {0x45, KEYBOARD_NUM_LOCK_ON},
    {0x46, KEYBOARD_SCROLL_LOCK_ON}
};

#define KEYBOARD_INDICATORS (sizeof(KbdIndicatorList) / sizeof(INDICATOR_LIST))

//
// The scan code (set 1) of each key code of the keyboard, zero for the
// codes that are not keys of the PC keyboard.  KEY_E0 above the code marks
// the keys of the enhanced keyboard that send an E0 prefix, KEY_E1 Pause,
// which sends E1 1D 45.
//

#define E0(Code) ((KEY_E0 << 8) | (Code))
#define E1(Code) ((KEY_E1 << 8) | (Code))

USHORT KbdScanCode[256] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,             // 00
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,             // 08
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,             // 10
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,             // 18
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,             // 20
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,             // 28
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,             // 30
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,             // 38
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,             // 40
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,             // 48
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3b, 0x3c,             // 50 f1 f2
    0x3d, 0x3e, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00,             // 58 f3 - f5
    0x00, 0x00, 0x00, 0x00, 0x40, 0x41, 0x42, 0x43,             // 60 f6 - f9
    0x44, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,             // 68 f10
    0x00, 0x01, 0x57, 0x58, E0(0x37), 0x00, 0x00, 0x00,         // 70 f11 - f14
    0x00, 0x00, 0x00, 0x00, 0x46, E1(0x1d), 0x00, 0x00,         // 78 help do
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,             // 80 f17 - f20
    0x00, 0x00, E0(0x47), E0(0x52), E0(0x53), E0(0x4f), E0(0x49), E0(0x51), // 88 find - next
    0x00, 0x00, 0x52, 0x00, 0x53, E0(0x1c), 0x4f, 0x50,         // 90 kp0 kp. kpenter kp1 kp2
    0x51, 0x4b, 0x4c, 0x4d, 0x4e, 0x47, 0x48, 0x49,             // 98 kp3 - kp6 kp, kp7 - kp9
    0x4a, 0x45, E0(0x35), 0x37, 0x4a, 0x00, 0x00, E0(0x4b),     // a0 kp- pf1 - pf4 left
    E0(0x4d), E0(0x50), E0(0x48), 0x36, 0x38, E0(0x38), 0x2a, 0x1d, // a8 right down up rshift alt alt shift ctrl
    0x3a, 0x38, E0(0x38), 0x00, 0x00, 0x00, 0x00, 0x00,         // b0 lock compose compose
    0x00, 0x00, 0x00, 0x00, 0x0e, 0x1c, 0x0f, 0x29,             // b8 <x] return tab `
    0x02, 0x10, 0x1e, 0x2c, 0x00, 0x03, 0x11, 0x1f,             // c0 1 q a z 2 w s
    0x2d, 0x56, 0x00, 0x04, 0x12, 0x20, 0x2e, 0x00,             // c8 x < 3 e d c
    0x05, 0x13, 0x21, 0x2f, 0x39, 0x00, 0x06, 0x14,             // d0 4 r f v space 5 t
    0x22, 0x30, 0x00, 0x07, 0x15, 0x23, 0x31, 0x00,             // d8 g b 6 y h n
    0x08, 0x16, 0x24, 0x32, 0x00, 0x09, 0x17, 0x25,             // e0 7 u j m 8 i k
    0x33, 0x00, 0x0a, 0x18, 0x26, 0x34, 0x00, 0x0b,             // e8 , 9 o l . 0
    0x19, 0x00, 0x27, 0x35, 0x00, 0x0d, 0x1b, 0x2b,             // f0 p ; / = ] backslash
    0x00, 0x0c, 0x1a, 0x28, 0x00, 0x00, 0x00, 0x00              // f8 - [ '
};

//
// Define the scan codes of the keys that do not repeat.
//

#define SCAN_LEFT_CONTROL 0x1d
#define SCAN_LEFT_SHIFT 0x2a
#define SCAN_RIGHT_SHIFT 0x36
#define SCAN_ALT 0x38
#define SCAN_CAPS_LOCK 0x3a
#define SCAN_NUM_LOCK 0x45
#define SCAN_SCROLL_LOCK 0x46

NTSTATUS
DriverEntry(
    IN PDRIVER_OBJECT DriverObject,
    IN PUNICODE_STRING RegistryPath
    );

NTSTATUS
KbdpConfigurationCallout(
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
KbdpServiceParameters(
    IN PUNICODE_STRING RegistryPath,
    OUT PUNICODE_STRING BaseName,
    OUT PULONG QueueSize
    );

NTSTATUS
KbdpConnectPort(
    IN PLK201_EXTENSION Extension
    );

VOID
KbdpLogError(
    IN PVOID IoObject,
    IN NTSTATUS ErrorCode,
    IN ULONG UniqueErrorValue,
    IN NTSTATUS FinalStatus
    );

NTSTATUS
KbdpOpenClose(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp
    );

NTSTATUS
KbdpInternalDeviceControl(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp
    );

BOOLEAN
KbdpConnect(
    IN PVOID Context
    );

BOOLEAN
KbdpEnable(
    IN PVOID Context
    );

BOOLEAN
KbdpDisable(
    IN PVOID Context
    );

BOOLEAN
KbdpSetIndicators(
    IN PVOID Context
    );

VOID
KbdpInputService(
    IN PVOID ClientContext,
    IN UCHAR InputByte
    );

BOOLEAN
KbdpOutputService(
    IN PVOID ClientContext
    );

VOID
KbdpSendByte(
    IN PLK201_EXTENSION Extension,
    IN UCHAR Byte
    );

VOID
KbdpSendIndicators(
    IN PLK201_EXTENSION Extension
    );

BOOLEAN
KbdpSetupKeyboard(
    IN PVOID Context
    );

VOID
KbdpKeyDown(
    IN PLK201_EXTENSION Extension,
    IN UCHAR KeyCode
    );

VOID
KbdpKeyUp(
    IN PLK201_EXTENSION Extension,
    IN UCHAR KeyCode
    );

VOID
KbdpAllKeysUp(
    IN PLK201_EXTENSION Extension
    );

VOID
KbdpQueueKey(
    IN PLK201_EXTENSION Extension,
    IN UCHAR KeyCode,
    IN USHORT Flags
    );

VOID
KbdpQueueInput(
    IN PLK201_EXTENSION Extension,
    IN USHORT MakeCode,
    IN USHORT Flags
    );

VOID
KbdpInputDpc(
    IN PKDPC Dpc,
    IN PVOID DeferredContext,
    IN PVOID SystemArgument1,
    IN PVOID SystemArgument2
    );

BOOLEAN
KbdpBeginInput(
    IN PVOID Context
    );

BOOLEAN
KbdpGetInput(
    IN PVOID Context
    );

BOOLEAN
KbdpTakeInput(
    IN PVOID Context
    );

BOOLEAN
KbdpEndInput(
    IN PVOID Context
    );

VOID
KbdpStartDpc(
    IN PKDPC Dpc,
    IN PVOID DeferredContext,
    IN PVOID SystemArgument1,
    IN PVOID SystemArgument2
    );

VOID
KbdpTypematicDpc(
    IN PKDPC Dpc,
    IN PVOID DeferredContext,
    IN PVOID SystemArgument1,
    IN PVOID SystemArgument2
    );

BOOLEAN
KbdpStartTypematic(
    IN PVOID Context
    );

BOOLEAN
KbdpRepeatKey(
    IN PVOID Context
    );

#ifdef ALLOC_PRAGMA
#pragma alloc_text(INIT,DriverEntry)
#pragma alloc_text(INIT,KbdpConfigurationCallout)
#pragma alloc_text(INIT,KbdpServiceParameters)
#pragma alloc_text(INIT,KbdpConnectPort)
#endif

NTSTATUS
DriverEntry(
    IN PDRIVER_OBJECT DriverObject,
    IN PUNICODE_STRING RegistryPath
    )

/*++

Routine Description:

    This routine initializes the keyboard port driver: it finds the keyboard
    in the configuration that the firmware describes, creates the keyboard
    port device, connects the keyboard line of the DZ port, sets up the
    keyboard and records the port in the device map for the keyboard class
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
    PLK201_EXTENSION Extension;
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
    // Find the keyboard of the keyboard controller of the internal bus.
    //

    Found = FALSE;
    InterfaceType = Internal;
    ControllerType = KeyboardController;
    PeripheralType = KeyboardPeripheral;
    IoQueryDeviceDescription(&InterfaceType, NULL, &ControllerType, NULL,
        &PeripheralType, NULL, KbdpConfigurationCallout, &Found);

    if (Found == FALSE) {
        return STATUS_NO_SUCH_DEVICE;
    }

    //
    // Create the keyboard port device, the first of the base name of the
    // parameters (KeyboardPort) and a number that is free.
    //

    KbdpServiceParameters(RegistryPath, &BaseName, &QueueSize);
    Name.Buffer = NameBuffer;
    Name.MaximumLength = sizeof(NameBuffer);
    Number.Buffer = NumberBuffer;
    Number.MaximumLength = sizeof(NumberBuffer);
    Status = STATUS_OBJECT_NAME_COLLISION;
    for (Index = 0; Index < KEYBOARD_PORTS_MAXIMUM; Index += 1) {
        Name.Length = 0;
        RtlAppendUnicodeToString(&Name, L"\\Device\\");
        RtlAppendUnicodeStringToString(&Name, &BaseName);
        RtlIntegerToUnicodeString(Index, 10, &Number);
        RtlAppendUnicodeStringToString(&Name, &Number);
        Status = IoCreateDevice(DriverObject, sizeof(LK201_EXTENSION),
            &Name, FILE_DEVICE_KEYBOARD, 0, FALSE, &DeviceObject);

        if (Status != STATUS_OBJECT_NAME_COLLISION) {
            break;
        }
    }

    if (!NT_SUCCESS(Status)) {
        KbdpLogError(DriverObject, IO_ERR_INSUFFICIENT_RESOURCES,
            LK201_ERROR_VALUE_BASE + 1, Status);
        return Status;
    }

    DeviceObject->Flags |= DO_BUFFERED_IO;
    Extension = (PLK201_EXTENSION)DeviceObject->DeviceExtension;
    RtlZeroMemory(Extension, sizeof(LK201_EXTENSION));
    Extension->DeviceObject = DeviceObject;
    Extension->UnitId = (USHORT)Index;

    //
    // Allocate the input data queue.
    //

    Extension->QueueSize = QueueSize;
    Extension->InputData = (PKEYBOARD_INPUT_DATA)ExAllocatePool(NonPagedPool,
        QueueSize * sizeof(KEYBOARD_INPUT_DATA));

    if (Extension->InputData == NULL) {
        KbdpLogError(DeviceObject, IO_ERR_INSUFFICIENT_RESOURCES,
            LK201_ERROR_VALUE_BASE + 2, STATUS_INSUFFICIENT_RESOURCES);
        IoDeleteDevice(DeviceObject);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Extension->DataIn = Extension->InputData;
    Extension->DataOut = Extension->InputData;
    Extension->DataEnd = Extension->InputData + QueueSize;
    KeInitializeDpc(&Extension->InputDpc, KbdpInputDpc, Extension);
    KeInitializeTimer(&Extension->RetryTimer);
    KeInitializeDpc(&Extension->StartDpc, KbdpStartDpc, Extension);
    KeInitializeDpc(&Extension->TypematicDpc, KbdpTypematicDpc, Extension);
    KeInitializeTimer(&Extension->TypematicTimer);

    //
    // The keyboard is an enhanced keyboard of the class driver.
    //

    Extension->Attributes.KeyboardIdentifier.Type = KEYBOARD_TYPE_ENHANCED;
    Extension->Attributes.KeyboardIdentifier.Subtype = 0;
    Extension->Attributes.KeyboardMode = KEYBOARD_SCAN_CODE_SET;
    Extension->Attributes.NumberOfFunctionKeys = KEYBOARD_FUNCTION_KEYS;
    Extension->Attributes.NumberOfIndicators = KEYBOARD_INDICATORS;
    Extension->Attributes.NumberOfKeysTotal = KEYBOARD_KEYS;
    Extension->Attributes.InputDataQueueLength =
        QueueSize * sizeof(KEYBOARD_INPUT_DATA);
    Extension->Attributes.KeyRepeatMinimum.Rate = KEYBOARD_TYPEMATIC_RATE_MINIMUM;
    Extension->Attributes.KeyRepeatMinimum.Delay = KEYBOARD_TYPEMATIC_DELAY_MINIMUM;
    Extension->Attributes.KeyRepeatMaximum.Rate = KEYBOARD_TYPEMATIC_RATE_MAXIMUM;
    Extension->Attributes.KeyRepeatMaximum.Delay = KEYBOARD_TYPEMATIC_DELAY_MAXIMUM;
    Extension->Typematic.Rate = KEYBOARD_TYPEMATIC_RATE_DEFAULT;
    Extension->Typematic.Delay = KEYBOARD_TYPEMATIC_DELAY_DEFAULT;

    //
    // Connect the keyboard line of the DZ port and set up the keyboard.
    //

    Status = KbdpConnectPort(Extension);
    if (!NT_SUCCESS(Status)) {
        KbdpLogError(DeviceObject, IO_ERR_LAYERED_FAILURE,
            LK201_ERROR_VALUE_BASE + 3, Status);
        ExFreePool(Extension->InputData);
        IoDeleteDevice(DeviceObject);
        return Status;
    }

    KeSynchronizeExecution(Extension->Port.Interrupt, KbdpSetupKeyboard,
        Extension);

    DriverObject->MajorFunction[IRP_MJ_CREATE] = KbdpOpenClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = KbdpOpenClose;
    DriverObject->MajorFunction[IRP_MJ_INTERNAL_DEVICE_CONTROL] =
        KbdpInternalDeviceControl;

    //
    // Record the port in the device map, where the class driver finds it.
    //

    RtlWriteRegistryValue(RTL_REGISTRY_DEVICEMAP, DD_KEYBOARD_PORT_BASE_NAME_U,
        Name.Buffer, REG_SZ, RegistryPath->Buffer,
        RegistryPath->Length + sizeof(UNICODE_NULL));

    return STATUS_SUCCESS;
}

NTSTATUS
KbdpConfigurationCallout(
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
    keyboard: it records that the configuration has it.

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
KbdpServiceParameters(
    IN PUNICODE_STRING RegistryPath,
    OUT PUNICODE_STRING BaseName,
    OUT PULONG QueueSize
    )

/*++

Routine Description:

    This routine reads the parameters of the driver from the Parameters key
    of its service key: the base name of the keyboard port device
    (KeyboardDeviceBaseName) and the number of entries of the input data
    queue (KeyboardDataQueueSize).  A parameter that is missing has its
    default value.

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

    DefaultQueueSize = KEYBOARD_DATA_QUEUE_SIZE_DEFAULT;
    RtlInitUnicodeString(&DefaultName, DD_KEYBOARD_PORT_BASE_NAME_U);
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
        Parameters[0].Name = L"KeyboardDataQueueSize";
        Parameters[0].EntryContext = QueueSize;
        Parameters[0].DefaultType = REG_DWORD;
        Parameters[0].DefaultData = &DefaultQueueSize;
        Parameters[0].DefaultLength = sizeof(ULONG);
        Parameters[1].Flags = RTL_QUERY_REGISTRY_DIRECT;
        Parameters[1].Name = L"KeyboardDeviceBaseName";
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
KbdpConnectPort(
    IN PLK201_EXTENSION Extension
    )

/*++

Routine Description:

    This routine connects the keyboard line of the DZ port: the port gets
    the input and output services of the keyboard driver, and the driver
    gets the routines of the port.

Arguments:

    Extension - Supplies a pointer to the device extension of the keyboard
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
    Connect.InputService = KbdpInputService;
    Connect.OutputService = KbdpOutputService;
    Connect.LineNumber = KEYBOARD_LINE;
    Connect.BaudRate = KEYBOARD_BAUD_RATE;
    Connect.CharacterLength = 8;
    Connect.StopBits = 1;
    Connect.Polarity = 0;
    Connect.ParityEnable = FALSE;
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
KbdpLogError(
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
KbdpOpenClose(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp
    )

/*++

Routine Description:

    This routine is the dispatch routine of the create and close requests:
    they succeed.

Arguments:

    DeviceObject - Supplies a pointer to the keyboard port device object.

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
KbdpInternalDeviceControl(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp
    )

/*++

Routine Description:

    This routine is the dispatch routine of the internal device control
    requests of the keyboard class driver: the connection, enabling and
    disabling the input, and the attributes, indicators and typematic
    parameters of the keyboard.

Arguments:

    DeviceObject - Supplies a pointer to the keyboard port device object.

    Irp - Supplies a pointer to the request packet.

Return Value:

    The status of the request.

--*/

{

    PVOID Buffer;
    PLK201_EXTENSION Extension;
    ULONG InputLength;
    PIO_STACK_LOCATION IrpSp;
    ULONG OutputLength;
    KEYBOARD_REQUEST Request;
    ULONG Size;
    NTSTATUS Status;
    PKEYBOARD_TYPEMATIC_PARAMETERS Typematic;

    Extension = (PLK201_EXTENSION)DeviceObject->DeviceExtension;
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

    case IOCTL_INTERNAL_KEYBOARD_CONNECT:
        if (Extension->ConnectData.ClassService != NULL) {
            Status = STATUS_SHARING_VIOLATION;

        } else if (InputLength < sizeof(CONNECT_DATA)) {
            Status = STATUS_INVALID_PARAMETER;

        } else {
            Request.Data = IrpSp->Parameters.DeviceIoControl.Type3InputBuffer;
            KeSynchronizeExecution(Extension->Port.Interrupt, KbdpConnect,
                &Request);
        }

        break;

    case IOCTL_INTERNAL_KEYBOARD_DISCONNECT:
        Status = STATUS_NOT_IMPLEMENTED;
        break;

    //
    // Enable or disable the input: the keys are given to the class driver
    // while it has enabled the input more times than disabled it.
    //

    case IOCTL_INTERNAL_KEYBOARD_ENABLE:
        KeSynchronizeExecution(Extension->Port.Interrupt, KbdpEnable,
            &Request);
        break;

    case IOCTL_INTERNAL_KEYBOARD_DISABLE:
        if (KeSynchronizeExecution(Extension->Port.Interrupt, KbdpDisable,
                &Request) == FALSE) {
            Status = STATUS_DEVICE_DATA_ERROR;
        }

        break;

    case IOCTL_KEYBOARD_QUERY_ATTRIBUTES:
        if (OutputLength < sizeof(KEYBOARD_ATTRIBUTES)) {
            Status = STATUS_BUFFER_TOO_SMALL;

        } else {
            *(PKEYBOARD_ATTRIBUTES)Buffer = Extension->Attributes;
            Irp->IoStatus.Information = sizeof(KEYBOARD_ATTRIBUTES);
        }

        break;

    case IOCTL_KEYBOARD_QUERY_INDICATOR_TRANSLATION:
        Size = sizeof(KEYBOARD_INDICATOR_TRANSLATION) +
            sizeof(INDICATOR_LIST) * (KEYBOARD_INDICATORS - 1);

        if (OutputLength < Size) {
            Status = STATUS_BUFFER_TOO_SMALL;

        } else {
            ((PKEYBOARD_INDICATOR_TRANSLATION)Buffer)->NumberOfIndicatorKeys =
                KEYBOARD_INDICATORS;

            RtlMoveMemory(((PKEYBOARD_INDICATOR_TRANSLATION)Buffer)->IndicatorList,
                KbdIndicatorList, sizeof(KbdIndicatorList));

            Irp->IoStatus.Information = Size;
        }

        break;

    case IOCTL_KEYBOARD_QUERY_INDICATORS:
        if (OutputLength < sizeof(KEYBOARD_INDICATOR_PARAMETERS)) {
            Status = STATUS_BUFFER_TOO_SMALL;

        } else {
            *(PKEYBOARD_INDICATOR_PARAMETERS)Buffer = Extension->Indicators;
            Irp->IoStatus.Information = sizeof(KEYBOARD_INDICATOR_PARAMETERS);
        }

        break;

    case IOCTL_KEYBOARD_SET_INDICATORS:
        if ((InputLength < sizeof(KEYBOARD_INDICATOR_PARAMETERS)) ||
            ((((PKEYBOARD_INDICATOR_PARAMETERS)Buffer)->LedFlags &
              ~(KEYBOARD_SCROLL_LOCK_ON | KEYBOARD_NUM_LOCK_ON |
                KEYBOARD_CAPS_LOCK_ON)) != 0)) {
            Status = STATUS_INVALID_PARAMETER;

        } else {
            Request.Data = Buffer;
            KeSynchronizeExecution(Extension->Port.Interrupt,
                KbdpSetIndicators, &Request);
        }

        break;

    case IOCTL_KEYBOARD_QUERY_TYPEMATIC:
        if (OutputLength < sizeof(KEYBOARD_TYPEMATIC_PARAMETERS)) {
            Status = STATUS_BUFFER_TOO_SMALL;

        } else {
            *(PKEYBOARD_TYPEMATIC_PARAMETERS)Buffer = Extension->Typematic;
            Irp->IoStatus.Information = sizeof(KEYBOARD_TYPEMATIC_PARAMETERS);
        }

        break;

    //
    // The typematic parameters are those of the repeat of the driver.
    //

    case IOCTL_KEYBOARD_SET_TYPEMATIC:
        Typematic = (PKEYBOARD_TYPEMATIC_PARAMETERS)Buffer;
        if ((InputLength < sizeof(KEYBOARD_TYPEMATIC_PARAMETERS)) ||
            (Typematic->Rate < Extension->Attributes.KeyRepeatMinimum.Rate) ||
            (Typematic->Rate > Extension->Attributes.KeyRepeatMaximum.Rate) ||
            (Typematic->Delay < Extension->Attributes.KeyRepeatMinimum.Delay) ||
            (Typematic->Delay > Extension->Attributes.KeyRepeatMaximum.Delay)) {
            Status = STATUS_INVALID_PARAMETER;

        } else {
            Extension->Typematic = *Typematic;
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
KbdpConnect(
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

    PLK201_EXTENSION Extension;
    PKEYBOARD_REQUEST Request;

    Request = (PKEYBOARD_REQUEST)Context;
    Extension = Request->Extension;
    Extension->ConnectData = *(PCONNECT_DATA)Request->Data;
    Extension->DataIn = Extension->InputData;
    Extension->DataOut = Extension->InputData;
    Extension->InputCount = 0;
    return TRUE;
}

BOOLEAN
KbdpEnable(
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

    ((PKEYBOARD_REQUEST)Context)->Extension->EnableCount += 1;
    return TRUE;
}

BOOLEAN
KbdpDisable(
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

    PLK201_EXTENSION Extension;

    Extension = ((PKEYBOARD_REQUEST)Context)->Extension;
    if (Extension->EnableCount == 0) {
        return FALSE;
    }

    Extension->EnableCount -= 1;
    return TRUE;
}

BOOLEAN
KbdpSetIndicators(
    IN PVOID Context
    )

/*++

Routine Description:

    This routine sets the indicators, at the level of the interrupt of the
    port.

Arguments:

    Context - Supplies the request: the device extension and the indicator
        parameters.

Return Value:

    TRUE.

--*/

{

    PLK201_EXTENSION Extension;
    PKEYBOARD_REQUEST Request;

    Request = (PKEYBOARD_REQUEST)Context;
    Extension = Request->Extension;
    Extension->Indicators = *(PKEYBOARD_INDICATOR_PARAMETERS)Request->Data;
    KbdpSendIndicators(Extension);
    return TRUE;
}

VOID
KbdpInputService(
    IN PVOID ClientContext,
    IN UCHAR InputByte
    )

/*++

Routine Description:

    This routine is the input service of the keyboard line: the DZ port
    calls it, at the level of its interrupt, with each character from the
    keyboard.  A key code is a key going down, or going up if the key is
    down; ALL UPS is every key that is down going up.  A power-up self-test
    report (four bytes, the first one the identification of the keyboard)
    means the keyboard was reset: it is set up again.  The other codes are
    acknowledgements and errors, which need nothing.

Arguments:

    ClientContext - Supplies a pointer to the device extension of the
        keyboard port.

    InputByte - Supplies the character.

Return Value:

    None.

--*/

{

    PLK201_EXTENSION Extension;

    Extension = (PLK201_EXTENSION)ClientContext;
    if (Extension->PowerUpBytes != 0) {
        Extension->PowerUpBytes -= 1;
        if (Extension->PowerUpBytes == 0) {
            KbdpAllKeysUp(Extension);
            KbdpSetupKeyboard(Extension);
        }

        return;
    }

    switch (InputByte) {
    case LK_ALL_UPS:
        KbdpAllKeysUp(Extension);
        return;

    case LK_METRONOME:
    case LK_OUTPUT_ERROR:
    case LK_INPUT_ERROR:
    case LK_LOCKED_ACK:
    case LK_TEST_MODE_ACK:
    case LK_PREFIX_KEYS_DOWN:
    case LK_MODE_CHANGE_ACK:
    case LK_RESERVED:
        return;

    default:
        break;
    }

    if (InputByte < LK_LOWEST) {
        if ((InputByte != 0) && (InputByte <= LK_MAXIMUM_ID)) {
            Extension->PowerUpBytes = LK_POWER_UP_BYTES;
        }

        return;
    }

    if ((Extension->KeyDown[InputByte / 32] & (1 << (InputByte % 32))) != 0) {
        KbdpKeyUp(Extension, InputByte);

    } else {
        KbdpKeyDown(Extension, InputByte);
    }

    return;
}

BOOLEAN
KbdpOutputService(
    IN PVOID ClientContext
    )

/*++

Routine Description:

    This routine is the output service of the keyboard line: the DZ port
    calls it, at the level of its interrupt, when the transmitter of the
    line is ready.  It sends the next command byte to the keyboard.

Arguments:

    ClientContext - Supplies a pointer to the device extension of the
        keyboard port.

Return Value:

    TRUE if a byte was sent, FALSE if there is nothing to send.

--*/

{

    PLK201_EXTENSION Extension;

    Extension = (PLK201_EXTENSION)ClientContext;
    if (Extension->OutputRead == Extension->OutputWrite) {
        return FALSE;
    }

    (Extension->Port.OutputByte)(Extension->Port.PortContext,
        Extension->Output[Extension->OutputRead]);

    Extension->OutputRead = (Extension->OutputRead + 1) % KEYBOARD_OUTPUT_SIZE;
    return TRUE;
}

VOID
KbdpSendByte(
    IN PLK201_EXTENSION Extension,
    IN UCHAR Byte
    )

/*++

Routine Description:

    This routine queues a command byte for the keyboard and enables the
    transmitter of the line.  It is called at the level of the interrupt of
    the port.

Arguments:

    Extension - Supplies a pointer to the device extension of the keyboard
        port.

    Byte - Supplies the byte.

Return Value:

    None.

--*/

{

    ULONG Next;

    Next = (Extension->OutputWrite + 1) % KEYBOARD_OUTPUT_SIZE;
    if (Next != Extension->OutputRead) {
        Extension->Output[Extension->OutputWrite] = Byte;
        Extension->OutputWrite = Next;
        (Extension->Port.EnableTransmit)(Extension->Port.PortContext);
    }

    return;
}

VOID
KbdpSendIndicators(
    IN PLK201_EXTENSION Extension
    )

/*++

Routine Description:

    This routine sends the LEDs of the indicators to the keyboard.  It is
    called at the level of the interrupt of the port.

Arguments:

    Extension - Supplies a pointer to the device extension of the keyboard
        port.

Return Value:

    None.

--*/

{

    UCHAR Leds;

    Leds = 0;
    if ((Extension->Indicators.LedFlags & KEYBOARD_CAPS_LOCK_ON) != 0) {
        Leds |= LK_LED_LOCK;
    }

    if ((Extension->Indicators.LedFlags & KEYBOARD_SCROLL_LOCK_ON) != 0) {
        Leds |= LK_LED_HOLD_SCREEN;
    }

    if ((Extension->Indicators.LedFlags & KEYBOARD_NUM_LOCK_ON) != 0) {
        Leds |= LK_LED_WAIT;
    }

    KbdpSendByte(Extension, LK_CMD_LED_OFF);
    KbdpSendByte(Extension, (UCHAR)(LK_LED_PARAMETER | (LK_LED_ALL & ~Leds)));
    if (Leds != 0) {
        KbdpSendByte(Extension, LK_CMD_LED_ON);
        KbdpSendByte(Extension, (UCHAR)(LK_LED_PARAMETER | Leds));
    }

    return;
}

BOOLEAN
KbdpSetupKeyboard(
    IN PVOID Context
    )

/*++

Routine Description:

    This routine sets up the keyboard: its default modes, the keys of the
    LK401 reported as its own, every division in down/up mode, and the LEDs
    of the indicators.  It is called at the level of the interrupt of the
    port.

Arguments:

    Context - Supplies a pointer to the device extension of the keyboard
        port.

Return Value:

    TRUE.

--*/

{

    ULONG Division;
    PLK201_EXTENSION Extension;

    Extension = (PLK201_EXTENSION)Context;
    KbdpSendByte(Extension, LK_CMD_SET_DEFAULTS);
    KbdpSendByte(Extension, LK_CMD_ENABLE_LK401);
    for (Division = 1; Division <= LK_DIVISIONS; Division += 1) {
        KbdpSendByte(Extension, (UCHAR)(LK_CMD_MODE_UP_DOWN | (Division << 3)));
    }

    KbdpSendIndicators(Extension);
    return TRUE;
}

VOID
KbdpKeyDown(
    IN PLK201_EXTENSION Extension,
    IN UCHAR KeyCode
    )

/*++

Routine Description:

    This routine reports a key going down and makes it the key that
    repeats, unless it is a shift, control, alternate, lock or pause key.
    It is called at the level of the interrupt of the port.

Arguments:

    Extension - Supplies a pointer to the device extension of the keyboard
        port.

    KeyCode - Supplies the key code.

Return Value:

    None.

--*/

{

    USHORT ScanCode;

    ScanCode = KbdScanCode[KeyCode];
    if (ScanCode == 0) {
        return;
    }

    Extension->KeyDown[KeyCode / 32] |= (1 << (KeyCode % 32));
    KbdpQueueKey(Extension, KeyCode, KEY_MAKE);
    if (((ScanCode >> 8) & KEY_E1) != 0) {
        return;
    }

    switch (ScanCode & 0xff) {
    case SCAN_LEFT_CONTROL:
    case SCAN_LEFT_SHIFT:
    case SCAN_RIGHT_SHIFT:
    case SCAN_ALT:
    case SCAN_CAPS_LOCK:
    case SCAN_NUM_LOCK:
    case SCAN_SCROLL_LOCK:
        break;

    default:
        Extension->TypematicKey = KeyCode;
        Extension->TypematicStart = TRUE;
        KeInsertQueueDpc(&Extension->StartDpc, NULL, NULL);
        break;
    }

    return;
}

VOID
KbdpKeyUp(
    IN PLK201_EXTENSION Extension,
    IN UCHAR KeyCode
    )

/*++

Routine Description:

    This routine reports a key going up; if it is the key that repeats, it
    stops.  It is called at the level of the interrupt of the port.

Arguments:

    Extension - Supplies a pointer to the device extension of the keyboard
        port.

    KeyCode - Supplies the key code.

Return Value:

    None.

--*/

{

    Extension->KeyDown[KeyCode / 32] &= ~(1 << (KeyCode % 32));
    KbdpQueueKey(Extension, KeyCode, KEY_BREAK);
    if (Extension->TypematicKey == KeyCode) {
        Extension->TypematicKey = 0;
    }

    return;
}

VOID
KbdpAllKeysUp(
    IN PLK201_EXTENSION Extension
    )

/*++

Routine Description:

    This routine reports each key that is down going up.  It is called at
    the level of the interrupt of the port.

Arguments:

    Extension - Supplies a pointer to the device extension of the keyboard
        port.

Return Value:

    None.

--*/

{

    ULONG KeyCode;

    for (KeyCode = 0; KeyCode < 256; KeyCode += 1) {
        if ((Extension->KeyDown[KeyCode / 32] & (1 << (KeyCode % 32))) != 0) {
            KbdpKeyUp(Extension, (UCHAR)KeyCode);
        }
    }

    Extension->TypematicKey = 0;
    return;
}

VOID
KbdpQueueKey(
    IN PLK201_EXTENSION Extension,
    IN UCHAR KeyCode,
    IN USHORT Flags
    )

/*++

Routine Description:

    This routine queues the input data of a key going down or up, as the
    enhanced keyboard sends it: the scan code of the key, with the E0
    prefix for the keys that have it.  Pause sends E1 1D 45 and then its
    break, E1 9D C5, when it goes down, and nothing when it goes up; with
    Ctrl down it is Break, which sends E0 46 E0 C6.  It is called at the
    level of the interrupt of the port.

Arguments:

    Extension - Supplies a pointer to the device extension of the keyboard
        port.

    KeyCode - Supplies the key code.

    Flags - Supplies KEY_MAKE or KEY_BREAK.

Return Value:

    None.

--*/

{

    USHORT ScanCode;

    ScanCode = KbdScanCode[KeyCode];
    if (((ScanCode >> 8) & KEY_E1) != 0) {
        if (Flags == KEY_MAKE) {
            if ((Extension->KeyDown[LK_KEY_CTRL / 32] &
                 (1 << (LK_KEY_CTRL % 32))) != 0) {
                KbdpQueueInput(Extension, 0x46, KEY_E0 | KEY_MAKE);
                KbdpQueueInput(Extension, 0x46, KEY_E0 | KEY_BREAK);

            } else {
                KbdpQueueInput(Extension, ScanCode & 0xff, KEY_E1 | KEY_MAKE);
                KbdpQueueInput(Extension, 0x45, KEY_MAKE);
                KbdpQueueInput(Extension, ScanCode & 0xff, KEY_E1 | KEY_BREAK);
                KbdpQueueInput(Extension, 0x45, KEY_BREAK);
            }
        }

        return;
    }

    KbdpQueueInput(Extension, (USHORT)(ScanCode & 0xff),
        (USHORT)((ScanCode >> 8) | Flags));

    return;
}

VOID
KbdpQueueInput(
    IN PLK201_EXTENSION Extension,
    IN USHORT MakeCode,
    IN USHORT Flags
    )

/*++

Routine Description:

    This routine puts a record in the input data queue and queues the input
    DPC, which gives it to the class driver.  The keys are discarded while
    no class driver is connected or the input is disabled; a key the full
    queue has no room for is lost, and the bell of the keyboard sounds.  It
    is called at the level of the interrupt of the port.

Arguments:

    Extension - Supplies a pointer to the device extension of the keyboard
        port.

    MakeCode - Supplies the scan code.

    Flags - Supplies the flags of the record: KEY_MAKE or KEY_BREAK, and
        KEY_E0 or KEY_E1 for a prefix.

Return Value:

    None.

--*/

{

    PKEYBOARD_INPUT_DATA Input;

    if ((Extension->ConnectData.ClassService == NULL) ||
        (Extension->EnableCount == 0)) {
        return;
    }

    if (Extension->InputCount == Extension->QueueSize) {
        if (Extension->Overflow == FALSE) {
            Extension->Overflow = TRUE;
            KbdpSendByte(Extension, LK_CMD_RING_BELL);
        }

        return;
    }

    Extension->Overflow = FALSE;
    Input = Extension->DataIn;
    Input->UnitId = Extension->UnitId;
    Input->MakeCode = MakeCode;
    Input->Flags = Flags;
    Input->Reserved = 0;
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
KbdpInputDpc(
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
        keyboard port.

    SystemArgument1, SystemArgument2 - Not used.

Return Value:

    None.

--*/

{

    LARGE_INTEGER DueTime;
    PLK201_EXTENSION Extension;
    KEYBOARD_INPUT_REQUEST Request;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    Extension = (PLK201_EXTENSION)DeferredContext;
    Request.Extension = Extension;
    if (KeSynchronizeExecution(Extension->Port.Interrupt, KbdpBeginInput,
            &Request) == FALSE) {
        return;
    }

    do {
        while (KeSynchronizeExecution(Extension->Port.Interrupt, KbdpGetInput,
                   &Request) != FALSE) {

            Request.Consumed = 0;
            (*(PSERVICE_CALLBACK_ROUTINE)Extension->ConnectData.ClassService)(
                Extension->ConnectData.ClassDeviceObject,
                Request.Start,
                Request.Start + Request.Count,
                &Request.Consumed);

            KeSynchronizeExecution(Extension->Port.Interrupt, KbdpTakeInput,
                &Request);

            if (Request.Consumed < Request.Count) {
                DueTime.LowPart = (ULONG)(-10 * 1000 * 1000);
                DueTime.HighPart = -1;
                KeSetTimer(&Extension->RetryTimer, DueTime,
                    &Extension->InputDpc);

                break;
            }
        }

    } while (KeSynchronizeExecution(Extension->Port.Interrupt, KbdpEndInput,
                 &Request) == FALSE);

    return;
}

BOOLEAN
KbdpBeginInput(
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

    PLK201_EXTENSION Extension;

    Extension = ((PKEYBOARD_INPUT_REQUEST)Context)->Extension;
    if (Extension->DpcActive != FALSE) {
        Extension->DpcAgain = TRUE;
        return FALSE;
    }

    Extension->DpcActive = TRUE;
    Extension->DpcAgain = FALSE;
    return TRUE;
}

BOOLEAN
KbdpGetInput(
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

    PLK201_EXTENSION Extension;
    PKEYBOARD_INPUT_REQUEST Request;

    Request = (PKEYBOARD_INPUT_REQUEST)Context;
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
KbdpTakeInput(
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

    PLK201_EXTENSION Extension;
    PKEYBOARD_INPUT_REQUEST Request;

    Request = (PKEYBOARD_INPUT_REQUEST)Context;
    Extension = Request->Extension;
    Extension->DataOut += Request->Consumed;
    if (Extension->DataOut == Extension->DataEnd) {
        Extension->DataOut = Extension->InputData;
    }

    Extension->InputCount -= Request->Consumed;
    return TRUE;
}

BOOLEAN
KbdpEndInput(
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

    PLK201_EXTENSION Extension;

    Extension = ((PKEYBOARD_INPUT_REQUEST)Context)->Extension;
    if (Extension->DpcAgain != FALSE) {
        Extension->DpcAgain = FALSE;
        return FALSE;
    }

    Extension->DpcActive = FALSE;
    return TRUE;
}

VOID
KbdpStartDpc(
    IN PKDPC Dpc,
    IN PVOID DeferredContext,
    IN PVOID SystemArgument1,
    IN PVOID SystemArgument2
    )

/*++

Routine Description:

    This routine starts the repeat of a key that went down: the key first
    repeats after the typematic delay.  A repeat of the previous key that
    is due is dropped.

Arguments:

    Dpc - Supplies a pointer to the DPC object.

    DeferredContext - Supplies a pointer to the device extension of the
        keyboard port.

    SystemArgument1, SystemArgument2 - Not used.

Return Value:

    None.

--*/

{

    LARGE_INTEGER DueTime;
    PLK201_EXTENSION Extension;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    Extension = (PLK201_EXTENSION)DeferredContext;
    KeCancelTimer(&Extension->TypematicTimer);
    KeRemoveQueueDpc(&Extension->TypematicDpc);
    if (KeSynchronizeExecution(Extension->Port.Interrupt, KbdpStartTypematic,
            Extension) != FALSE) {

        DueTime.LowPart = (ULONG)(-10 * 1000 * (LONG)Extension->Typematic.Delay);
        DueTime.HighPart = -1;
        KeSetTimer(&Extension->TypematicTimer, DueTime,
            &Extension->TypematicDpc);
    }

    return;
}

VOID
KbdpTypematicDpc(
    IN PKDPC Dpc,
    IN PVOID DeferredContext,
    IN PVOID SystemArgument1,
    IN PVOID SystemArgument2
    )

/*++

Routine Description:

    This routine is the DPC of the typematic timer: the key that repeats is
    reported down again, and the timer is set for the next repeat.

Arguments:

    Dpc - Supplies a pointer to the DPC object.

    DeferredContext - Supplies a pointer to the device extension of the
        keyboard port.

    SystemArgument1, SystemArgument2 - Not used.

Return Value:

    None.

--*/

{

    LARGE_INTEGER DueTime;
    PLK201_EXTENSION Extension;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    Extension = (PLK201_EXTENSION)DeferredContext;
    if (KeSynchronizeExecution(Extension->Port.Interrupt, KbdpRepeatKey,
            Extension) != FALSE) {

        DueTime.LowPart = (ULONG)(-10 * 1000 * 1000 / (LONG)Extension->Typematic.Rate);
        DueTime.HighPart = -1;
        KeSetTimer(&Extension->TypematicTimer, DueTime,
            &Extension->TypematicDpc);
    }

    return;
}

BOOLEAN
KbdpStartTypematic(
    IN PVOID Context
    )

/*++

Routine Description:

    This routine marks the repeat of the key that went down as started, at
    the level of the interrupt of the port.

Arguments:

    Context - Supplies a pointer to the device extension of the keyboard
        port.

Return Value:

    TRUE if a key repeats, FALSE otherwise.

--*/

{

    PLK201_EXTENSION Extension;

    Extension = (PLK201_EXTENSION)Context;
    Extension->TypematicStart = FALSE;
    return (BOOLEAN)(Extension->TypematicKey != 0);
}

BOOLEAN
KbdpRepeatKey(
    IN PVOID Context
    )

/*++

Routine Description:

    This routine reports the key that repeats down again, at the level of
    the interrupt of the port.  Nothing repeats while the repeat of a new
    key has not started.

Arguments:

    Context - Supplies a pointer to the device extension of the keyboard
        port.

Return Value:

    TRUE if the key was repeated, FALSE otherwise.

--*/

{

    PLK201_EXTENSION Extension;

    Extension = (PLK201_EXTENSION)Context;
    if ((Extension->TypematicStart != FALSE) || (Extension->TypematicKey == 0)) {
        return FALSE;
    }

    KbdpQueueKey(Extension, Extension->TypematicKey, KEY_MAKE);
    return TRUE;
}
