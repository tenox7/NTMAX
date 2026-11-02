/*++

Module Name:

    decscc.h

Abstract:

    This module is the header file that describes the serial communications
    controllers (SCC, Z85C30) of the DECstation 5000/1xx and 5000/240: the
    layout of the registers of a channel in the system slot, the read and
    write registers of a channel and their bits, and the serial lines of the
    system.

--*/

#ifndef _DECSCC_
#define _DECSCC_

//
// Define the layout of an SCC.  Each channel has a control register and a
// data register, a longword apart, and the byte of a register is bits 8 to
// 15 of its longword.  Channel B is at the start of the SCC, channel A
// follows it.  A write to the control register of a channel selects one of
// its registers, then the next read or write of the control register
// reads or writes that register.
//

typedef struct _SCC_CHANNEL {
    ULONG Control;
    ULONG Data;
} SCC_CHANNEL, *PSCC_CHANNEL;

#define SCC_CHANNEL_B_OFFSET 0x0
#define SCC_CHANNEL_A_OFFSET 0x8
#define SCC_CHANNEL_SIZE 0x8
#define SCC_SIZE 0x10

#define SCC_BYTE_SHIFT 8

//
// Define the serial lines of the system, the lines of the DZ of the
// DECstation 5000/200.  Each is a channel of one of the two SCCs: the
// keyboard (SCC 1, channel A), the mouse (SCC 0, channel A), the
// communications port (SCC 0, channel B) and the printer port (SCC 1,
// channel B), on which the console firmware has its console.
//
// The communications and printer ports have their data on channel B of
// their SCC and most of their modem signals on channel A: DTR and RTS are
// outputs of channel A, DSR and RI its SYNC and DCD inputs.  CTS and CD
// are inputs of channel B, and the RTS output of channel B is the speed
// select of the port.
//

#define SCC_KEYBOARD_LINE 0
#define SCC_MOUSE_LINE 1
#define SCC_COMMUNICATIONS_LINE 2
#define SCC_PRINTER_LINE 3

#define SCC_LINE_SCC(Line) ((((Line) == 0) || ((Line) == 3)) ? 1 : 0)
#define SCC_LINE_OFFSET(Line) \
    (((Line) < 2) ? SCC_CHANNEL_A_OFFSET : SCC_CHANNEL_B_OFFSET)

//
// Define the clock of the baud rate generators (PCLK) and the time
// constant of a baud rate, with the clock of a channel the generator
// divided by 16.
//

#define SCC_CLOCK 7372800
#define SCC_TIME_CONSTANT(BaudRate) ((SCC_CLOCK / (32 * (BaudRate))) - 2)

//
// Define the read registers.
//

#define SCC_RR0 0                       // transmit and receive status
#define SCC_RR1 1                       // special receive condition status
#define SCC_RR2 2                       // interrupt vector
#define SCC_RR3 3                       // interrupt pending, channel A only
#define SCC_RR10 10                     // miscellaneous status
#define SCC_RR15 15                     // external status interrupt enables

//
// Define the write registers.
//

#define SCC_WR0 0                       // command
#define SCC_WR1 1                       // interrupt enables
#define SCC_WR2 2                       // interrupt vector
#define SCC_WR3 3                       // receive parameters
#define SCC_WR4 4                       // clock mode and character format
#define SCC_WR5 5                       // transmit parameters
#define SCC_WR9 9                       // master interrupt control and reset
#define SCC_WR10 10                     // miscellaneous transmit and receive
#define SCC_WR11 11                     // clock sources
#define SCC_WR12 12                     // time constant, low byte
#define SCC_WR13 13                     // time constant, high byte
#define SCC_WR14 14                     // baud rate generator
#define SCC_WR15 15                     // external status interrupt enables

//
// Define the bits of the status register (RR0).
//

#define SCC_RR0_RECEIVE_AVAILABLE 0x01
#define SCC_RR0_TRANSMIT_EMPTY 0x04
#define SCC_RR0_DCD 0x08
#define SCC_RR0_SYNC 0x10
#define SCC_RR0_CTS 0x20
#define SCC_RR0_TRANSMIT_UNDERRUN 0x40
#define SCC_RR0_BREAK 0x80

//
// Define the bits of the special receive condition register (RR1).
//

#define SCC_RR1_ALL_SENT 0x01
#define SCC_RR1_PARITY_ERROR 0x10
#define SCC_RR1_OVERRUN_ERROR 0x20
#define SCC_RR1_FRAMING_ERROR 0x40

//
// Define the bits of the interrupt pending register (RR3).
//

#define SCC_RR3_B_EXTERNAL 0x01
#define SCC_RR3_B_TRANSMIT 0x02
#define SCC_RR3_B_RECEIVE 0x04
#define SCC_RR3_A_EXTERNAL 0x08
#define SCC_RR3_A_TRANSMIT 0x10
#define SCC_RR3_A_RECEIVE 0x20

//
// Define the commands of the command register (WR0).
//

#define SCC_WR0_RESET_EXTERNAL_STATUS 0x10
#define SCC_WR0_RESET_TRANSMIT_PENDING 0x28
#define SCC_WR0_ERROR_RESET 0x30
#define SCC_WR0_RESET_HIGHEST_IUS 0x38

//
// Define the bits of the interrupt enable register (WR1).
//

#define SCC_WR1_EXTERNAL_ENABLE 0x01
#define SCC_WR1_TRANSMIT_ENABLE 0x02
#define SCC_WR1_PARITY_SPECIAL 0x04
#define SCC_WR1_RECEIVE_ALL 0x10

//
// Define the bits of the receive parameter register (WR3).
//

#define SCC_WR3_RECEIVE_ENABLE 0x01
#define SCC_WR3_RECEIVE_5_BITS 0x00
#define SCC_WR3_RECEIVE_7_BITS 0x40
#define SCC_WR3_RECEIVE_6_BITS 0x80
#define SCC_WR3_RECEIVE_8_BITS 0xc0

//
// Define the bits of the clock mode and character format register (WR4).
//

#define SCC_WR4_PARITY_ENABLE 0x01
#define SCC_WR4_EVEN_PARITY 0x02
#define SCC_WR4_ONE_STOP_BIT 0x04
#define SCC_WR4_TWO_STOP_BITS 0x0c
#define SCC_WR4_CLOCK_X16 0x40

//
// Define the bits of the transmit parameter register (WR5).
//

#define SCC_WR5_RTS 0x02
#define SCC_WR5_TRANSMIT_ENABLE 0x08
#define SCC_WR5_SEND_BREAK 0x10
#define SCC_WR5_TRANSMIT_5_BITS 0x00
#define SCC_WR5_TRANSMIT_7_BITS 0x20
#define SCC_WR5_TRANSMIT_6_BITS 0x40
#define SCC_WR5_TRANSMIT_8_BITS 0x60
#define SCC_WR5_DTR 0x80

//
// Define the bits of the master interrupt control register (WR9).
//

#define SCC_WR9_NO_VECTOR 0x02
#define SCC_WR9_MASTER_ENABLE 0x08
#define SCC_WR9_RESET_CHANNEL_B 0x40
#define SCC_WR9_RESET_CHANNEL_A 0x80

//
// Define the bits of the clock source register (WR11): the receive and
// transmit clocks are the baud rate generator.
//

#define SCC_WR11_CLOCKS_BRG 0x50

//
// Define the bits of the baud rate generator register (WR14).
//

#define SCC_WR14_BRG_ENABLE 0x01
#define SCC_WR14_BRG_PCLK 0x02

//
// Define the bits of the external status interrupt enable register (WR15).
//

#define SCC_WR15_DCD_ENABLE 0x08
#define SCC_WR15_CTS_ENABLE 0x20
#define SCC_WR15_BREAK_ENABLE 0x80

#endif // _DECSCC_
