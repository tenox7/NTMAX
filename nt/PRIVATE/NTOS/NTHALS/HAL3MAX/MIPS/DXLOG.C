/*++

Module Name:

    dxlog.c

Abstract:

    This module implements the serial log of the HAL of the DECstation 5000.
    The HAL sends the text it displays to the console line too, the serial
    line that the console and the ARC firmware have their console on (the
    printer port, line 3), and it adds lines of its own: what it found when
    it starts, the error registers at a bug check, and how it gives the
    machine back to the firmware.  The text the HAL displays is the start of
    the system, the messages of the programs that run before the display
    driver starts (autochk), and the text of a bug check.  A terminal on the
    line (9600 baud, eight bits, no parity, one stop bit) keeps what the
    system did when the screen shows nothing or no longer shows it; on a
    system module without a frame buffer, the line is the only display.

    The firmware leaves the line set up when it starts the system.  The
    serial drivers of the system keep its speed and its transmitter (the
    port driver of the SCCs keeps the transmitter of this line enabled while
    it disables the others, sccport.c) until the driver of the
    communications ports starts, after logon, and connects the line as its
    second port, set up as its clients ask.  So the HAL sets the line up
    again whenever it takes the display back from the display driver (a bug
    check, or a program that displays a string), and before it gives the
    machine back to the firmware.

    The characters are sent by polling.  A character that the line does not
    take within four character times is dropped, and the log stops until
    the line is set up again, so a line that does not work does not hold
    the system up.

    On the 5000/200 the line is a line of the DZ.  The DZ is reached through
    its transmit control register, which the port driver of the DZ writes
    too, so each character is sent at HIGH_LEVEL, as the kernel debugger
    sends its characters (d3port.c).  On the 5000/1xx and the 5000/240 the
    line is channel B of SCC 1, whose channel A is the keyboard: the status
    register and the data register of the channel are each read or written
    in one access, so the characters are sent at the IRQL of the caller.
    A string is sent at DISPATCH_LEVEL at least, so that a thread switch
    does not cut it.

Environment:

    Kernel mode only.

--*/

#include "halp.h"
#include "stdarg.h"
#include "stdio.h"

#if defined(KN02_AA)

#include "decdz.h"

#else

#include "decscc.h"

#endif

//
// Define the console line, the time of a character on it in microseconds
// (ten bits at 9600 baud), the time between two polls of the transmitter
// and the number of polls before a character is dropped.
//

#define LOG_LINE 3
#define LOG_BAUD_RATE 9600
#define LOG_CHARACTER_TIME 1042
#define LOG_POLL_TIME 10
#define LOG_TRANSMIT_POLLS ((4 * LOG_CHARACTER_TIME) / LOG_POLL_TIME)

//
// Define the size of the buffer of a formatted line.
//

#define LOG_BUFFER_SIZE 256

//
// Define the name of the system module of the HAL.
//

#if defined(KN02_AA)

#define LOG_SYSTEM_NAME "DECstation 5000/200 (KN02)"

#elif defined(KN02_BA)

#define LOG_SYSTEM_NAME "DECstation 5000/1xx (KN02-BA)"

#else

#define LOG_SYSTEM_NAME "DECstation 5000/240 (KN03)"

#endif

#if !defined(KN02_AA)

//
// Define the channel of the console line, and the routines of d3port.c
// that read and write a register of a channel.
//

#define LOG_SCC_PHYSICAL_BASE \
    ((SCC_LINE_SCC(LOG_LINE) == 0) ? SCC_PHYSICAL_BASE : SCC1_PHYSICAL_BASE)

#define LOG_CHANNEL \
    ((PSCC_CHANNEL_REGISTERS)(KSEG1_BASE | \
                              (LOG_SCC_PHYSICAL_BASE + SCC_LINE_OFFSET(LOG_LINE))))

UCHAR
HalpReadSccRegister (
    IN PSCC_CHANNEL_REGISTERS Channel,
    IN ULONG Register
    );

VOID
HalpWriteSccRegister (
    IN PSCC_CHANNEL_REGISTERS Channel,
    IN ULONG Register,
    IN UCHAR Value
    );

#endif

//
// Define whether the log is sent.  It stops when the line does not take a
// character, until the line is set up again.
//

BOOLEAN HalpLogEnabled = TRUE;

//
// Define whether the line is, or may be, in the middle of a line, and
// whether the next text starts a new line.  A serial terminal cannot place
// text as the display does: the text that the HAL displays on a screen it
// has just cleared, or at a place it has moved to, starts a new line, as
// does each line of the HAL's own.  The text before it on the line (the
// progress dots of the OS loader, the text of a serial driver) can end in
// the middle of a line.
//

BOOLEAN HalpLogMidLine = TRUE;
BOOLEAN HalpLogNewLine = TRUE;

VOID
HalpLogSetup (
    VOID
    )

/*++

Routine Description:

    This routine sets the console line up for the log, as the firmware sets
    it up: 9600 baud, eight bits, one stop bit and no parity, with its
    receiver and its transmitter enabled.  The characters that a driver
    gave the line are sent first.  The interrupt enables of the line belong
    to the port driver and are left as they are.  The log starts again if
    it had stopped.

Arguments:

    None.

Return Value:

    None.

--*/

{

    KIRQL OldIrql;

#if defined(KN02_AA)

    USHORT DataShort;

#else

    ULONG Wait;

#endif

    KeRaiseIrql(HIGH_LEVEL, &OldIrql);

#if defined(KN02_AA)

    //
    // The DZ does not report an empty transmitter: wait for the two
    // characters the line holds to be sent.  Then set the line parameters,
    // set data terminal ready and request to send of the line, and enable
    // the scan of the lines of the multiplexer.
    //

    KeStallExecutionProcessor(2 * LOG_CHARACTER_TIME);
    DataShort = 0;
    ((PDZ_LINE_PARAMETERS)(&DataShort))->LineNumber = LOG_LINE;
    ((PDZ_LINE_PARAMETERS)(&DataShort))->CharacterLength = EIGHT_BIT_CHARACTERS;
    ((PDZ_LINE_PARAMETERS)(&DataShort))->StopCode = ONE_STOP_BIT;
    ((PDZ_LINE_PARAMETERS)(&DataShort))->SpeedCode = BAUD_9600;
    ((PDZ_LINE_PARAMETERS)(&DataShort))->ReceiveEnable = 1;
    WRITE_REGISTER_USHORT(&DZ_WRITE->LineParameters.Short, DataShort);

    DataShort = READ_REGISTER_USHORT(&DZ_READ->TransmitControlRead.Short);
    ((PDZ_TRANSMIT_CONTROL)(&DataShort))->DataTerminalReady3 = 1;
    ((PDZ_TRANSMIT_CONTROL)(&DataShort))->RequestToSend3 = 1;
    WRITE_REGISTER_USHORT(&DZ_WRITE->TransmitControlWrite.Short, DataShort);

    DataShort = READ_REGISTER_USHORT(&DZ_READ->ControlStatus.Short);
    ((PDZ_CONTROL_STATUS)(&DataShort))->MasterScanEnable = 1;
    WRITE_REGISTER_USHORT(&DZ_WRITE->ControlStatus.Short, DataShort);

#else

    //
    // A read of the control register points the channel at register 0,
    // wherever a driver that a bug check stopped left it.  The transmitter
    // sends what it has for up to four character times.  Then the channel
    // is set to eight bits, one stop bit and no parity, with the clocks of
    // the receiver and the transmitter from the baud rate generator, and
    // its receiver and its transmitter are enabled, with data terminal
    // ready and request to send (the speed select of the port).
    //

    HalpReadSccRegister(LOG_CHANNEL, SCC_RR0);
    for (Wait = 0; Wait < LOG_TRANSMIT_POLLS; Wait += 1) {
        if ((HalpReadSccRegister(LOG_CHANNEL, SCC_RR1) & SCC_RR1_ALL_SENT) != 0) {
            break;
        }

        KeStallExecutionProcessor(LOG_POLL_TIME);
    }

    HalpWriteSccRegister(LOG_CHANNEL, SCC_WR4,
                         SCC_WR4_CLOCK_X16 | SCC_WR4_ONE_STOP_BIT);

    HalpWriteSccRegister(LOG_CHANNEL, SCC_WR3, SCC_WR3_RECEIVE_8_BITS);
    HalpWriteSccRegister(LOG_CHANNEL, SCC_WR5,
                         SCC_WR5_DTR | SCC_WR5_TRANSMIT_8_BITS | SCC_WR5_RTS);

    HalpWriteSccRegister(LOG_CHANNEL, SCC_WR11, SCC_WR11_CLOCKS_BRG);
    HalpWriteSccRegister(LOG_CHANNEL, SCC_WR14, SCC_WR14_BRG_PCLK);
    HalpWriteSccRegister(LOG_CHANNEL, SCC_WR12,
                         (UCHAR)SCC_TIME_CONSTANT(LOG_BAUD_RATE));

    HalpWriteSccRegister(LOG_CHANNEL, SCC_WR13,
                         (UCHAR)(SCC_TIME_CONSTANT(LOG_BAUD_RATE) >> 8));

    HalpWriteSccRegister(LOG_CHANNEL, SCC_WR14,
                         SCC_WR14_BRG_PCLK | SCC_WR14_BRG_ENABLE);

    HalpWriteSccRegister(LOG_CHANNEL, SCC_WR3,
                         SCC_WR3_RECEIVE_8_BITS | SCC_WR3_RECEIVE_ENABLE);

    HalpWriteSccRegister(LOG_CHANNEL, SCC_WR5,
                         SCC_WR5_DTR | SCC_WR5_TRANSMIT_8_BITS |
                         SCC_WR5_RTS | SCC_WR5_TRANSMIT_ENABLE);

#endif

    HalpLogEnabled = TRUE;
    HalpLogMidLine = TRUE;
    HalpLogNewLine = TRUE;
    KeLowerIrql(OldIrql);
    return;
}

VOID
HalpLogCharacter (
    IN UCHAR Character
    )

/*++

Routine Description:

    This routine sends a character on the console line.  If the line does
    not take it within four character times, the character is dropped and
    the log stops.

Arguments:

    Character - Supplies the character.

Return Value:

    None.

--*/

{

#if defined(KN02_AA)

    USHORT DataShort;
    USHORT Enable;
    KIRQL OldIrql;
    USHORT TransmitControl;
    ULONG Wait;

    //
    // Enable the transmitter of the line and wait for transmit ready of the
    // line.  The multiplexer reports a line that can take a character in
    // the control status register; another line that it reports is
    // disabled until the character is sent.
    //

    KeRaiseIrql(HIGH_LEVEL, &OldIrql);
    TransmitControl = READ_REGISTER_USHORT(&DZ_READ->TransmitControlRead.Short);
    Enable = TransmitControl | (1 << LOG_LINE);
    WRITE_REGISTER_USHORT(&DZ_WRITE->TransmitControlWrite.Short, Enable);
    for (Wait = 0; ; Wait += 1) {
        DataShort = READ_REGISTER_USHORT(&DZ_READ->ControlStatus.Short);
        if (((PDZ_CONTROL_STATUS)(&DataShort))->TransmitReady != 0) {
            if (((PDZ_CONTROL_STATUS)(&DataShort))->TransmitLine == LOG_LINE) {
                break;
            }

            Enable &= ~(1 << ((PDZ_CONTROL_STATUS)(&DataShort))->TransmitLine);
            WRITE_REGISTER_USHORT(&DZ_WRITE->TransmitControlWrite.Short, Enable);
        }

        if (Wait == LOG_TRANSMIT_POLLS) {
            WRITE_REGISTER_USHORT(&DZ_WRITE->TransmitControlWrite.Short,
                                  TransmitControl);

            HalpLogEnabled = FALSE;
            KeLowerIrql(OldIrql);
            return;
        }

        KeStallExecutionProcessor(LOG_POLL_TIME);
    }

    //
    // Send the character and restore the transmitter enables of the lines.
    //

    WRITE_REGISTER_USHORT(&DZ_WRITE->TransmitBuffer.Short, (USHORT)Character);
    WRITE_REGISTER_USHORT(&DZ_WRITE->TransmitControlWrite.Short, TransmitControl);
    KeLowerIrql(OldIrql);
    return;

#else

    ULONG Wait;

    //
    // Wait for the transmit buffer of the channel to be empty, then send
    // the character.
    //

    for (Wait = 0;
         (HalpReadSccRegister(LOG_CHANNEL, SCC_RR0) & SCC_RR0_TRANSMIT_EMPTY) == 0;
         Wait += 1) {

        if (Wait == LOG_TRANSMIT_POLLS) {
            HalpLogEnabled = FALSE;
            return;
        }

        KeStallExecutionProcessor(LOG_POLL_TIME);
    }

    WRITE_REGISTER_ULONG(&LOG_CHANNEL->Data, (ULONG)Character << SCC_BYTE_SHIFT);
    KeStallExecutionProcessor(2);
    return;

#endif

}

VOID
HalpLogString (
    IN PUCHAR String
    )

/*++

Routine Description:

    This routine sends a string on the console line, with a carriage return
    before each line feed, on a new line if the HAL has just taken the
    display or moved its cursor.

Arguments:

    String - Supplies a pointer to the string.

Return Value:

    None.

--*/

{

    KIRQL OldIrql;

    if (HalpLogEnabled == FALSE) {
        return;
    }

    OldIrql = KeGetCurrentIrql();
    if (OldIrql < DISPATCH_LEVEL) {
        KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);
    }

    if ((HalpLogNewLine != FALSE) && (*String != 0)) {
        HalpLogNewLine = FALSE;
        if ((HalpLogMidLine != FALSE) && (*String != '\n')) {
            HalpLogCharacter('\r');
            HalpLogCharacter('\n');
        }
    }

    while ((*String != 0) && (HalpLogEnabled != FALSE)) {
        if (*String == '\n') {
            HalpLogCharacter('\r');
            HalpLogMidLine = FALSE;

        } else {
            HalpLogMidLine = TRUE;
        }

        HalpLogCharacter(*String);
        String += 1;
    }

    if (OldIrql < DISPATCH_LEVEL) {
        KeLowerIrql(OldIrql);
    }

    return;
}

VOID
HalpLogStartLine (
    VOID
    )

/*++

Routine Description:

    This routine starts a new line of the log for the next text, unless
    the line is at the start of a line.  The display calls it when its
    cursor is moved.

Arguments:

    None.

Return Value:

    None.

--*/

{

    HalpLogNewLine = TRUE;
    return;
}

VOID
HalpLogPrint (
    IN PCHAR Format,
    ...
    )

/*++

Routine Description:

    This routine formats a line of the HAL's own and sends it on the console
    line, at the start of a line.

Arguments:

    Format - Supplies a pointer to the format string, followed by its
        arguments.

Return Value:

    None.

--*/

{

    va_list Arguments;
    UCHAR Buffer[LOG_BUFFER_SIZE];

    va_start(Arguments, Format);
    _vsnprintf((PCHAR)Buffer, sizeof(Buffer) - 1, Format, Arguments);
    va_end(Arguments);
    Buffer[sizeof(Buffer) - 1] = 0;
    HalpLogStartLine();
    HalpLogString(Buffer);
    return;
}

VOID
HalpLogSystem (
    VOID
    )

/*++

Routine Description:

    This routine sends the line of the HAL when it starts: the system
    module of the HAL, the processor identification, the size of the
    caches, and the stall scale factor that the HAL measured (the speed of
    the processor).  It is called in phase 1, once the stall is calibrated.

Arguments:

    None.

Return Value:

    None.

--*/

{

    HalpLogPrint("HAL: %s, processor id %04lx, caches %d KB instruction"
                 " and %d KB data, stall scale factor %d.\n",
                 LOG_SYSTEM_NAME,
                 PCR->ProcessorId & 0xffff,
                 PCR->FirstLevelIcacheSize >> 10,
                 PCR->FirstLevelDcacheSize >> 10,
                 PCR->StallScaleFactor);

    return;
}

VOID
HalpLogErrorRegisters (
    IN ULONG FailedAddress,
    IN ULONG CheckSyndrome,
    IN ULONG SystemControl
    )

/*++

Routine Description:

    This routine sends the memory error registers and the register of the
    system interrupts that the bug check callback of the HAL read.

Arguments:

    FailedAddress - Supplies the error address register (the address error
        register on the KN02-BA).

    CheckSyndrome - Supplies the check syndrome register (the memory error
        register on the KN02-BA).

    SystemControl - Supplies the system control and status register on the
        KN02, the system interrupt register on the KN02-BA and the KN03.

Return Value:

    None.

--*/

{

#if defined(KN02_AA)

    HalpLogPrint("HAL: error address %08lx, check syndrome %08lx,"
                 " system control and status %08lx.\n",
                 FailedAddress,
                 CheckSyndrome,
                 SystemControl);

#elif defined(KN02_BA)

    HalpLogPrint("HAL: address error %08lx, memory error %08lx,"
                 " system interrupts %08lx.\n",
                 FailedAddress,
                 CheckSyndrome,
                 SystemControl);

#else

    HalpLogPrint("HAL: error address %08lx, check syndrome %08lx,"
                 " system interrupts %08lx.\n",
                 FailedAddress,
                 CheckSyndrome,
                 SystemControl);

#endif

    return;
}

VOID
HalpLogReturnToFirmware (
    IN FIRMWARE_REENTRY Routine
    )

/*++

Routine Description:

    This routine sets the console line up again, since the driver of the
    communications ports may have had it, and sends the line of the HAL
    when it gives the machine back to the firmware.

Arguments:

    Routine - Supplies the firmware routine the HAL returns to.

Return Value:

    None.

--*/

{

    PCHAR Name;

    switch (Routine) {
    case HalHaltRoutine:
        Name = "halt";
        break;

    case HalPowerDownRoutine:
        Name = "power down";
        break;

    case HalRestartRoutine:
        Name = "restart";
        break;

    case HalRebootRoutine:
        Name = "reboot";
        break;

    case HalInteractiveModeRoutine:
        Name = "interactive mode";
        break;

    default:
        Name = "unknown routine";
        break;
    }

    HalpLogSetup();
    HalpLogPrint("HAL: return to the firmware: %s.\n", Name);
    return;
}
