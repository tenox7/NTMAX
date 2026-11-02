/*++

Module Name:

    dzline.c

Abstract:

    This module contains the code of the serial driver that is specific to
    the lines of the DZ serial line multiplexer (DC7085) of the DECstation
    5000/200.  The driver does not touch the multiplexer: it connects the
    line of each port to the DZ port driver (dzport.h) and reaches the line
    through the routines of the port driver, at the level of the interrupt
    of the multiplexer, from the services that the port driver calls or
    through KeSynchronizeExecution with the interrupt object of the port
    driver.

    The rest of the serial driver was written for the 8250 family, whose
    registers the multiplexer does not have.  This module gives that code
    what it reads from those registers:

    - The line status.  The port driver gives the input service each
      character with its parity, framing and overrun errors, and the driver
      keeps the character and an 8250 line status for it until they are
      read.  The multiplexer does not detect a break: a character of zero
      with a framing error is one.  The holding register is empty when the
      line is ready and has nothing more to send.  The multiplexer does not
      tell when the transmitter is empty, so it is counted empty one
      character time later, at the resolution of the clock tick.

    - The modem status.  The multiplexer has the state of the modem
      signals of the communications and printer lines, but no interrupt
      and no delta bits for them.  The driver makes the delta bits of the
      8250 from the state that was read last, and reads the state at every
      clock tick while the port is open.

    - The modem control.  DTR and RTS are set through the port driver.

    - The baud rate.  The multiplexer has sixteen fixed baud rates.  A baud
      rate within one percent of one of them runs at that rate, as the
      serial driver allows a divisor up to one percent off; the others are
      refused.

Environment:

    Kernel mode

Revision History:

--*/

#include "precomp.h"

//
// The baud rates of the multiplexer (134 is 134.5).
//

static const ULONG SerialLineBauds[] = {
    50, 75, 110, 134, 150, 300, 600, 1200,
    1800, 2000, 2400, 3600, 4800, 7200, 9600, 19200
    };

VOID
SerialLineParameters(
    IN PSERIAL_DEVICE_EXTENSION Extension,
    OUT PULONG CharacterLength,
    OUT PULONG StopBits,
    OUT PULONG Polarity,
    OUT PBOOLEAN ParityEnable
    );

UCHAR
SerialLineModemStatus(
    IN PSERIAL_DEVICE_EXTENSION Extension
    );

BOOLEAN
SerialTransmitterEmpty(
    IN PSERIAL_DEVICE_EXTENSION Extension
    );

BOOLEAN
SerialReadLineStatusSynch(
    IN PVOID Context
    );

BOOLEAN
SerialReadModemControlSynch(
    IN PVOID Context
    );

BOOLEAN
SerialPollModemStatus(
    IN PVOID Context
    );

#ifdef ALLOC_PRAGMA
#pragma alloc_text(INIT,SerialConnectLine)
#pragma alloc_text(PAGESER,SerialDisconnectLine)
#pragma alloc_text(PAGESER,SerialGetLineBaudFromBaud)
#pragma alloc_text(PAGESER,SerialGetLineStatus)
#pragma alloc_text(PAGESER,SerialGetModemControl)
#pragma alloc_text(PAGESER,SerialTransmitXon)
#pragma alloc_text(PAGESER,SerialStartModemPoll)
#pragma alloc_text(PAGESER,SerialStopModemPoll)
#endif


NTSTATUS
SerialConnectLine(
    IN PSERIAL_DEVICE_EXTENSION Extension,
    IN ULONG LineNumber
    )

/*++

Routine Description:

    This routine connects the line of a port to the DZ port driver.  The
    port driver gets the input and output services of the port, and the
    input service gets each character with its receive status; the driver
    gets the routines and the interrupt object of the port driver.  The
    line starts with the line control and the baud rate of the extension.

    The port driver refuses the line of the kernel debugger while the
    debugger is enabled (STATUS_DEVICE_BUSY), and a line that another
    driver has (STATUS_DEVICE_ALREADY_ATTACHED).

Arguments:

    Extension - The device extension of the port.

    LineNumber - The line of the multiplexer.

Return Value:

    The status of the connect request.

--*/

{

    DZPORT_CONNECT_STATUS connect;
    KEVENT event;
    PFILE_OBJECT fileObject;
    PIRP irp;
    IO_STATUS_BLOCK ioStatus;
    UNICODE_STRING portName;
    PDEVICE_OBJECT portDevice;
    NTSTATUS status;

    RtlInitUnicodeString(
        &portName,
        L"\\Device\\DzPort"
        );

    status = IoGetDeviceObjectPointer(
                 &portName,
                 FILE_READ_ATTRIBUTES,
                 &fileObject,
                 &portDevice
                 );

    if (!NT_SUCCESS(status)) {

        SerialDump(
            SERERRORS,
            ("SERIAL: Couldn't open the DZ port driver for %wZ\n",
             &Extension->DeviceName)
            );
        return status;

    }

    connect.Connect.ClientContext = Extension;
    connect.Connect.InputService = NULL;
    connect.Connect.OutputService = SerialOutputService;
    connect.Connect.LineNumber = LineNumber;
    connect.Connect.BaudRate = Extension->LineBaud;
    SerialLineParameters(
        Extension,
        &connect.Connect.CharacterLength,
        &connect.Connect.StopBits,
        &connect.Connect.Polarity,
        &connect.Connect.ParityEnable
        );
    connect.InputStatusService = SerialInputService;

    KeInitializeEvent(
        &event,
        NotificationEvent,
        FALSE
        );

    irp = IoBuildDeviceIoControlRequest(
              IOCTL_MN_DZPORT_CONNECT_STATUS,
              portDevice,
              &connect,
              sizeof(DZPORT_CONNECT_STATUS),
              &Extension->Port,
              sizeof(DZPORT_ACCEPT),
              TRUE,
              &event,
              &ioStatus
              );

    if (!irp) {

        ObDereferenceObject(fileObject);
        return STATUS_INSUFFICIENT_RESOURCES;

    }

    status = IoCallDriver(
                 portDevice,
                 irp
                 );

    if (status == STATUS_PENDING) {

        KeWaitForSingleObject(
            &event,
            Executive,
            KernelMode,
            FALSE,
            NULL
            );
        status = ioStatus.Status;

    }

    if (!NT_SUCCESS(status)) {

        SerialDump(
            SERERRORS,
            ("SERIAL: The DZ port driver did not connect line %d\n"
             "------  for %wZ: %x\n",
             LineNumber,&Extension->DeviceName,status)
            );
        ObDereferenceObject(fileObject);
        return status;

    }

    Extension->PortDeviceObject = portDevice;
    Extension->PortFileObject = fileObject;
    Extension->LineNumber = LineNumber;
    Extension->Interrupt = Extension->Port.Interrupt;
    return STATUS_SUCCESS;

}

VOID
SerialDisconnectLine(
    IN PSERIAL_DEVICE_EXTENSION Extension
    )

/*++

Routine Description:

    This routine disconnects the line of a port from the DZ port driver,
    which disables the receiver and the transmitter of the line, and lets
    go of the port driver.

Arguments:

    Extension - The device extension of the port.

Return Value:

    None.

--*/

{

    DZPORT_DISCONNECT disconnect;
    KEVENT event;
    PIRP irp;
    IO_STATUS_BLOCK ioStatus;
    NTSTATUS status;

    if (!Extension->PortFileObject) {

        return;

    }

    disconnect.LineNumber = Extension->LineNumber;
    KeInitializeEvent(
        &event,
        NotificationEvent,
        FALSE
        );

    irp = IoBuildDeviceIoControlRequest(
              IOCTL_MN_DZPORT_DISCONNECT,
              Extension->PortDeviceObject,
              &disconnect,
              sizeof(DZPORT_DISCONNECT),
              NULL,
              0,
              TRUE,
              &event,
              &ioStatus
              );

    if (irp) {

        status = IoCallDriver(
                     Extension->PortDeviceObject,
                     irp
                     );

        if (status == STATUS_PENDING) {

            KeWaitForSingleObject(
                &event,
                Executive,
                KernelMode,
                FALSE,
                NULL
                );

        }

    } else {

        //
        // The line stays connected.  The port is closed, so the
        // port driver has the receiver and the transmitter of the
        // line disabled and does not call its services.
        //

        SerialDump(
            SERERRORS,
            ("SERIAL: Couldn't disconnect line %d for %wZ\n",
             Extension->LineNumber,&Extension->DeviceName)
            );

    }

    ObDereferenceObject(Extension->PortFileObject);
    Extension->PortFileObject = NULL;
    Extension->PortDeviceObject = NULL;

}

NTSTATUS
SerialGetLineBaudFromBaud(
    IN LONG DesiredBaud,
    OUT PULONG LineBaud
    )

/*++

Routine Description:

    This routine finds the baud rate of the multiplexer for an
    unvalidated baud rate: the rate of the multiplexer within one
    percent of it.

Arguments:

    DesiredBaud - The baud rate for whose rate of the multiplexer we
    seek.

    LineBaud - Given that the DesiredBaud is valid, the ULONG pointed to
    by this parameter will be set to the rate of the multiplexer.  NOTE:
    The ULONG is undefined if the DesiredBaud is not supported.

Return Value:

    This function will return STATUS_SUCCESS if the baud is supported.
    If the value is not supported it will return STATUS_INVALID_PARAMETER.

--*/

{

    ULONG difference;
    ULONG i;

    if (DesiredBaud > 0) {

        for (
            i = 0;
            i < (sizeof(SerialLineBauds) / sizeof(SerialLineBauds[0]));
            i++
            ) {

            if ((ULONG)DesiredBaud > SerialLineBauds[i]) {

                difference = (ULONG)DesiredBaud - SerialLineBauds[i];

            } else {

                difference = SerialLineBauds[i] - (ULONG)DesiredBaud;

            }

            if (difference <= (SerialLineBauds[i] / 100)) {

                *LineBaud = SerialLineBauds[i];
                return STATUS_SUCCESS;

            }

        }

    }

    return STATUS_INVALID_PARAMETER;

}

VOID
SerialLineParameters(
    IN PSERIAL_DEVICE_EXTENSION Extension,
    OUT PULONG CharacterLength,
    OUT PULONG StopBits,
    OUT PULONG Polarity,
    OUT PBOOLEAN ParityEnable
    )

/*++

Routine Description:

    This routine gives the line parameters of the port driver for the
    line control of the extension, an 8250 line control register.  With
    five data bits the stop bit of the line control is one and a half
    stop bits, as the multiplexer has it too.  The line control has no
    mark or space parity: the multiplexer has none.

Arguments:

    Extension - The device extension of the port.

    CharacterLength - Receives the number of data bits.

    StopBits - Receives the number of stop bits, 1 or 2.

    Polarity - Receives the parity: 0 for even, 1 for odd.

    ParityEnable - Receives whether the characters have a parity bit.

Return Value:

    None.

--*/

{

    *CharacterLength = (ULONG)(Extension->LineControl & SERIAL_DATA_MASK) + 5;
    *StopBits = (Extension->LineControl & SERIAL_STOP_MASK)?(2):(1);
    *ParityEnable = (BOOLEAN)((Extension->LineControl & SERIAL_PARITY_MASK) !=
                              SERIAL_NONE_PARITY);
    *Polarity = ((Extension->LineControl & SERIAL_PARITY_MASK) ==
                 SERIAL_EVEN_PARITY)?(0):(1);

}

VOID
SerialSetLineParameters(
    IN PSERIAL_DEVICE_EXTENSION Extension
    )

/*++

Routine Description:

    This routine sets the line control and the baud rate of the line
    from the extension, and the time the transmitter takes for a
    character.

    NOTE: This routine assumes that it is called at interrupt level.

Arguments:

    Extension - The device extension of the port.

Return Value:

    None.

--*/

{

    ULONG characterLength;
    ULONG stopBits;
    ULONG polarity;
    BOOLEAN parityEnable;
    ULONG bitCount;
    ULONG charTime;
    ULONG timeIncrement;

    SerialLineParameters(
        Extension,
        &characterLength,
        &stopBits,
        &polarity,
        &parityEnable
        );

    if (!(Extension->Port.SetLineParameters)(
             Extension->Port.PortContext,
             Extension->LineBaud,
             characterLength,
             stopBits,
             polarity,
             parityEnable
             )) {

        SerialDump(
            SERERRORS,
            ("SERIAL: The DZ port driver refused the parameters of line %d\n",
             Extension->LineNumber)
            );

    }

    //
    // A character is a start bit, the data bits, the parity bit and the
    // stop bits (one and a half stop bits count as two).  The time is in
    // hundreds of nanoseconds.  The transmitter is counted empty one tick
    // more than a character time after the line was ready, as the tick
    // count can change right after it is read.
    //

    bitCount = 1 + characterLength + ((parityEnable)?(1):(0)) + stopBits;
    charTime = ((bitCount * 10000000) + (Extension->LineBaud - 1)) /
               Extension->LineBaud;
    timeIncrement = KeQueryTimeIncrement();
    Extension->CharTicks = 1 + ((charTime + (timeIncrement - 1)) /
                                timeIncrement);

}

UCHAR
SerialReadLineStatus(
    IN PSERIAL_DEVICE_EXTENSION Extension
    )

/*++

Routine Description:

    This routine reads the line status of the port, as the line status
    register of the 8250 has it: data ready while the received character
    has not been read, the receive errors of that character, which the
    read clears, and the holding register and transmitter empty bits.

    NOTE: This routine assumes that it is called at interrupt level.

Arguments:

    Extension - The device extension of the port.

Return Value:

    The line status.

--*/

{

    UCHAR lineStatus = Extension->LineStatus;

    Extension->LineStatus &= SERIAL_LSR_DR;

    if (Extension->HoldingEmpty) {

        lineStatus |= SERIAL_LSR_THRE;

        if (SerialTransmitterEmpty(Extension)) {

            lineStatus |= SERIAL_LSR_TEMT;

        }

    }

    return lineStatus;

}

BOOLEAN
SerialReadLineStatusSynch(
    IN PVOID Context
    )

/*++

Routine Description:

    This routine reads the line status for SerialGetLineStatus at the
    level of the interrupt of the port.

Arguments:

    Context - Pointer to a structure that contains a pointer to the
              device extension and a pointer to the UCHAR that receives
              the line status.

Return Value:

    This routine always returns FALSE.

--*/

{

    PSERIAL_IOCTL_SYNC S = Context;

    *((PUCHAR)S->Data) = SerialReadLineStatus(S->Extension);

    return FALSE;

}

UCHAR
SerialGetLineStatus(
    IN PSERIAL_DEVICE_EXTENSION Extension
    )

/*++

Routine Description:

    This routine reads the line status of the port (SerialReadLineStatus)
    for code that is not at interrupt level.

Arguments:

    Extension - The device extension of the port.

Return Value:

    The line status.

--*/

{

    SERIAL_IOCTL_SYNC S;
    UCHAR lineStatus;

    S.Extension = Extension;
    S.Data = &lineStatus;
    KeSynchronizeExecution(
        Extension->Interrupt,
        SerialReadLineStatusSynch,
        &S
        );

    return lineStatus;

}

UCHAR
SerialReadReceiveBuffer(
    IN PSERIAL_DEVICE_EXTENSION Extension
    )

/*++

Routine Description:

    This routine reads the received character, as the receive buffer
    register of the 8250 has it.  The data is no longer ready.

    NOTE: This routine assumes that it is called at interrupt level.

Arguments:

    Extension - The device extension of the port.

Return Value:

    The character.

--*/

{

    Extension->LineStatus &= ~SERIAL_LSR_DR;

    return Extension->ReceiveData;

}

UCHAR
SerialLineModemStatus(
    IN PSERIAL_DEVICE_EXTENSION Extension
    )

/*++

Routine Description:

    This routine reads the state of the modem signals of the line, as the
    state bits of the modem status register of the 8250 have it.  A line
    without modem signals has none.

    NOTE: This routine assumes that it is called at interrupt level.

Arguments:

    Extension - The device extension of the port.

Return Value:

    The state of the modem signals.

--*/

{

    ULONG lineStatus;
    UCHAR modemStatus = 0;

    lineStatus = (Extension->Port.QueryModemStatus)(
                     Extension->Port.PortContext
                     );

    if (lineStatus & DZPORT_CTS) {

        modemStatus |= SERIAL_MSR_CTS;

    }

    if (lineStatus & DZPORT_DSR) {

        modemStatus |= SERIAL_MSR_DSR;

    }

    if (lineStatus & DZPORT_RI) {

        modemStatus |= SERIAL_MSR_RI;

    }

    if (lineStatus & DZPORT_CD) {

        modemStatus |= SERIAL_MSR_DCD;

    }

    return modemStatus;

}

UCHAR
SerialReadModemStatus(
    IN PSERIAL_DEVICE_EXTENSION Extension
    )

/*++

Routine Description:

    This routine reads the modem status of the line, as the modem status
    register of the 8250 has it: the state of the modem signals, and the
    delta bits of the signals that changed since the last read (for the
    ring indicator, a ring that ended).  As on the 8250, the read clears
    the delta bits.

    NOTE: This routine assumes that it is called at interrupt level.

Arguments:

    Extension - The device extension of the port.

Return Value:

    The modem status.

--*/

{

    UCHAR modemStatus;
    UCHAR changed;

    modemStatus = SerialLineModemStatus(Extension);
    changed = (UCHAR)(modemStatus ^ Extension->LastModemStatus);
    Extension->LastModemStatus = modemStatus;

    if (changed & SERIAL_MSR_CTS) {

        modemStatus |= SERIAL_MSR_DCTS;

    }

    if (changed & SERIAL_MSR_DSR) {

        modemStatus |= SERIAL_MSR_DDSR;

    }

    if ((changed & SERIAL_MSR_RI) && !(modemStatus & SERIAL_MSR_RI)) {

        modemStatus |= SERIAL_MSR_TERI;

    }

    if (changed & SERIAL_MSR_DCD) {

        modemStatus |= SERIAL_MSR_DDCD;

    }

    return modemStatus;

}

UCHAR
SerialReadModemControl(
    IN PSERIAL_DEVICE_EXTENSION Extension
    )

/*++

Routine Description:

    This routine reads the modem control of the line, as the DTR and RTS
    bits of the modem control register of the 8250 have it.

    NOTE: This routine assumes that it is called at interrupt level.

Arguments:

    Extension - The device extension of the port.

Return Value:

    The modem control.

--*/

{

    ULONG lineControl;
    UCHAR modemControl = 0;

    lineControl = (Extension->Port.QueryModemControl)(
                      Extension->Port.PortContext
                      );

    if (lineControl & DZPORT_DTR) {

        modemControl |= SERIAL_MCR_DTR;

    }

    if (lineControl & DZPORT_RTS) {

        modemControl |= SERIAL_MCR_RTS;

    }

    return modemControl;

}

VOID
SerialWriteModemControl(
    IN PSERIAL_DEVICE_EXTENSION Extension,
    IN UCHAR ModemControl
    )

/*++

Routine Description:

    This routine sets DTR and RTS of the line from the DTR and RTS bits of
    an 8250 modem control register.  The multiplexer has no OUT1, OUT2 or
    loop bits.

    NOTE: This routine assumes that it is called at interrupt level.

Arguments:

    Extension - The device extension of the port.

    ModemControl - The modem control.

Return Value:

    None.

--*/

{

    ULONG lineControl = 0;

    if (ModemControl & SERIAL_MCR_DTR) {

        lineControl |= DZPORT_DTR;

    }

    if (ModemControl & SERIAL_MCR_RTS) {

        lineControl |= DZPORT_RTS;

    }

    (Extension->Port.SetModemControl)(
        Extension->Port.PortContext,
        lineControl
        );

}

BOOLEAN
SerialReadModemControlSynch(
    IN PVOID Context
    )

/*++

Routine Description:

    This routine reads the modem control for SerialGetModemControl at the
    level of the interrupt of the port.

Arguments:

    Context - Pointer to a structure that contains a pointer to the
              device extension and a pointer to the ULONG that receives
              the modem control.

Return Value:

    This routine always returns FALSE.

--*/

{

    PSERIAL_IOCTL_SYNC S = Context;

    *((PULONG)S->Data) = SerialReadModemControl(S->Extension);

    return FALSE;

}

ULONG
SerialGetModemControl(
    IN PSERIAL_DEVICE_EXTENSION Extension
    )

/*++

Routine Description:

    This routine reads the modem control of the line
    (SerialReadModemControl) for code that is not at interrupt level.

Arguments:

    Extension - The device extension of the port.

Return Value:

    The modem control.

--*/

{

    SERIAL_IOCTL_SYNC S;
    ULONG modemControl;

    S.Extension = Extension;
    S.Data = &modemControl;
    KeSynchronizeExecution(
        Extension->Interrupt,
        SerialReadModemControlSynch,
        &S
        );

    return modemControl;

}

VOID
SerialTransmitterBusy(
    IN PSERIAL_DEVICE_EXTENSION Extension
    )

/*++

Routine Description:

    This routine notes that the transmitter of the line has a character
    to send: one was loaded, or the port driver loads one when the line is
    ready to change the break of the line.

    NOTE: This routine assumes that it is called at interrupt level.

Arguments:

    Extension - The device extension of the port.

Return Value:

    None.

--*/

{

    Extension->TransmitterEmpty = FALSE;
    Extension->TransmitterIdle = FALSE;

}

VOID
SerialTransmitterIdle(
    IN PSERIAL_DEVICE_EXTENSION Extension
    )

/*++

Routine Description:

    This routine notes that the line is ready and has nothing more to
    send.  The transmitter is still sending the last character, and is
    counted empty CharTicks clock ticks from now.

    NOTE: This routine assumes that it is called at interrupt level.

Arguments:

    Extension - The device extension of the port.

Return Value:

    None.

--*/

{

    LARGE_INTEGER tickCount;

    if (!Extension->TransmitterEmpty) {

        KeQueryTickCount(&tickCount);
        Extension->TransmitterEmptyTick = tickCount.LowPart +
                                          Extension->CharTicks;
        Extension->TransmitterIdle = TRUE;

    }

}

BOOLEAN
SerialTransmitterEmpty(
    IN PSERIAL_DEVICE_EXTENSION Extension
    )

/*++

Routine Description:

    This routine tells whether the transmitter of the line is empty: the
    line was ready with nothing more to send at least a character time
    ago.

    NOTE: This routine assumes that it is called at interrupt level.

Arguments:

    Extension - The device extension of the port.

Return Value:

    TRUE if the transmitter is empty.

--*/

{

    LARGE_INTEGER tickCount;

    if (!Extension->TransmitterEmpty && Extension->TransmitterIdle) {

        KeQueryTickCount(&tickCount);

        if ((LONG)(tickCount.LowPart - Extension->TransmitterEmptyTick) >= 0) {

            Extension->TransmitterEmpty = TRUE;
            Extension->TransmitterIdle = FALSE;

        }

    }

    return Extension->TransmitterEmpty;

}

BOOLEAN
SerialTransmitXon(
    IN PVOID Context
    )

/*++

Routine Description:

    This routine is used by the close code to send the xon character
    after an automatic xoff.  The serial driver writes the xon character
    to the holding register of the closed port.  A character can only be
    loaded on the line when it is ready, so the output service sends it
    then, and the holding register is full until then.

    NOTE: This routine assumes that it is called at interrupt level.

Arguments:

    Context - Really a pointer to the device extension.

Return Value:

    This routine always returns FALSE.

--*/

{

    PSERIAL_DEVICE_EXTENSION Extension = Context;

    Extension->SendXonChar = TRUE;
    Extension->HoldingEmpty = FALSE;
    SerialTransmitterBusy(Extension);
    (Extension->Port.EnableTransmit)(Extension->Port.PortContext);

    return FALSE;

}

VOID
SerialStartModemPoll(
    IN PSERIAL_DEVICE_EXTENSION Extension
    )

/*++

Routine Description:

    This routine starts the modem status poll of an open port.  The
    multiplexer does not interrupt on a change of the modem signals: the
    timer reads them at every clock tick, which is the finest a timer
    can do.

Arguments:

    Extension - The device extension of the port.

Return Value:

    None.

--*/

{

    LARGE_INTEGER dueTime;

    Extension->ModemPollActive = TRUE;
    dueTime = RtlConvertLongToLargeInteger(-(LONG)KeQueryTimeIncrement());
    KeSetTimer(
        &Extension->ModemPollTimer,
        dueTime,
        &Extension->ModemPollDpc
        );

}

VOID
SerialStopModemPoll(
    IN PSERIAL_DEVICE_EXTENSION Extension
    )

/*++

Routine Description:

    This routine stops the modem status poll of a port that is closing.
    At dispatch level the dpc cannot run between the steps.

Arguments:

    Extension - The device extension of the port.

Return Value:

    None.

--*/

{

    KIRQL oldIrql;

    KeRaiseIrql(
        DISPATCH_LEVEL,
        &oldIrql
        );

    Extension->ModemPollActive = FALSE;
    KeCancelTimer(&Extension->ModemPollTimer);
    KeRemoveQueueDpc(&Extension->ModemPollDpc);

    KeLowerIrql(oldIrql);

}

VOID
SerialModemPoll(
    IN PKDPC Dpc,
    IN PVOID DeferredContext,
    IN PVOID SystemContext1,
    IN PVOID SystemContext2
    )

/*++

Routine Description:

    This is the dpc of the modem status timer.  It has the modem status
    read at the level of the interrupt of the port and sets the timer
    again for the next clock tick.

Arguments:

    Dpc - Not Used.

    DeferredContext - Really points to the device extension.

    SystemContext1 - Not Used.

    SystemContext2 - Not Used.

Return Value:

    None.

--*/

{

    PSERIAL_DEVICE_EXTENSION Extension = DeferredContext;
    LARGE_INTEGER dueTime;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemContext1);
    UNREFERENCED_PARAMETER(SystemContext2);

    KeSynchronizeExecution(
        Extension->Interrupt,
        SerialPollModemStatus,
        Extension
        );

    if (Extension->ModemPollActive) {

        dueTime = RtlConvertLongToLargeInteger(-(LONG)KeQueryTimeIncrement());
        KeSetTimer(
            &Extension->ModemPollTimer,
            dueTime,
            &Extension->ModemPollDpc
            );

    }

}

BOOLEAN
SerialPollModemStatus(
    IN PVOID Context
    )

/*++

Routine Description:

    This routine does for a change of the modem signals of an open port
    what the interrupt service routine of the 8250 does for a modem
    status interrupt.

    NOTE: This routine assumes that it is called at interrupt level.

Arguments:

    Context - Really a pointer to the device extension.

Return Value:

    This routine always returns FALSE.

--*/

{

    PSERIAL_DEVICE_EXTENSION Extension = Context;

    if (Extension->DeviceIsOpened &&
        (SerialLineModemStatus(Extension) != Extension->LastModemStatus)) {

        SerialHandleModemUpdate(
            Extension,
            FALSE
            );

    }

    return FALSE;

}
