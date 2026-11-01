//      TITLE("Firmware Startup")
//++
//
// Module Name:
//
//    d3start.s
//
// Abstract:
//
//    This module is the start of the ARC firmware for the DECstation
//    5000/200 (KN02, MIPS R3000).  The REX console loads the firmware from
//    the boot block of the boot device and starts it in KSEG1 with
//
//        a0 - the argument count of the console boot command,
//        a1 - the argument vector of the console boot command,
//        a2 - REX_MAGIC when the console is a REX (TURBOchannel) console,
//        a3 - the address of the console callback vector.
//
//    It also contains the routine that initializes the PCR the firmware
//    keeps for the HAL routines it shares with the system.
//
// Environment:
//
//    Kernel mode only.
//
//--

#include "ksmips.h"
#include "decfw.h"

        SBTTL("Firmware Startup")
//++
//
// VOID
// FwStart (
//    IN ULONG Argc,
//    IN PCHAR Argv[],
//    IN ULONG Magic,
//    IN PVOID CallbackVector
//    )
//
// Routine Description:
//
//    This routine disables interrupts, sweeps the instruction cache, which
//    may hold lines of the memory the console loaded the firmware into,
//    continues in KSEG0, saves the console arguments, sets the firmware
//    stack and calls FwEntry.  FwEntry does not return.
//
// Arguments:
//
//    a0 - a3 as passed by the console.
//
// Return Value:
//
//    None.
//
//--

        LEAF_ENTRY(FwStart)

        .set    noreorder
        .set    noat
        mfc0    t0,psr                  // get processor status
        li      t1,~(1 << PSR_IEC)      // clear current interrupt enable
        and     t0,t0,t1                //
        mtc0    t0,psr                  // disable interrupts
        nop                             //
        move    s0,a0                   // save console arguments
        move    s1,a1                   //
        move    s2,a2                   //
        move    s3,a3                   //
        la      t0,HalSweepIcache       // get address of sweep routine
        li      t1,KSEG1_BASE           // run it uncached
        or      t0,t0,t1                //
        jal     t0                      // sweep the instruction cache
        nop                             //
        la      t0,10f                  // continue in KSEG0
        j       t0                      //
        nop                             //
10:     la      t0,FwRexArgc            // save console arguments
        sw      s0,0(t0)                //
        la      t0,FwRexArgv            //
        sw      s1,0(t0)                //
        la      t0,FwRexMagic           //
        sw      s2,0(t0)                //
        la      t0,FwRexVector          //
        sw      s3,0(t0)                //
        la      sp,FwStack + FW_STACK_SIZE - (4 * 4) // set firmware stack
        move    a0,zero                 // set reset cause
        jal     FwEntry                 // start firmware
        move    a1,zero                 // set argument
20:     b       20b                     // FwEntry does not return
        nop                             //
        .set    at
        .set    reorder

        .end    FwStart

        SBTTL("Initialize PCR")
//++
//
// VOID
// InitializePCR (
//    VOID
//    )
//
// Routine Description:
//
//    This routine maps the PCR page at KiPcr with the fixed TB entry
//    PCR_ENTRY, zeroes it, and sets the primary cache sizes in it.  The
//    R3000 has no configuration register: the caches are sized.
//
// Arguments:
//
//    None.
//
// Return Value:
//
//    None.
//
//--

        NESTED_ENTRY(InitializePCR, 0x20, zero)

        subu    sp,sp,0x20              // allocate stack frame
        sw      ra,0x14(sp)             // save return address

        PROLOGUE_END

        .set    noreorder
        .set    noat
        li      t0,KiPcr                // set virtual address of PCR
        mtc0    t0,entryhi              //
        li      t1,PCR_PHYSICAL_BASE | (1 << ENTRYLO_D) | (1 << ENTRYLO_V) | (1 << ENTRYLO_G)
        mtc0    t1,entrylo              // set PCR page, dirty, valid, global
        li      t2,PCR_ENTRY << INDEX_INDEX // set fixed TB entry index
        mtc0    t2,index                //
        nop                             //
        tlbwi                           // write PCR TB entry
        nop                             //
        .set    at
        .set    reorder

        li      a0,KiPcr                // zero PCR
        li      a1,PAGE_SIZE            //
        jal     RtlZeroMemory           //

        addu    a0,sp,0x18              // size the primary caches
        addu    a1,sp,0x1c              //
        jal     HalpSizeCaches          //

        li      t0,KiPcr                // get PCR address
        lw      t1,0x18(sp)             // set instruction cache size
        sw      t1,PcFirstLevelIcacheSize(t0) //
        li      t1,R3000_CACHE_FILL_SIZE // set instruction cache fill size
        sw      t1,PcFirstLevelIcacheFillSize(t0) //
        sw      t1,PcIcacheFillSize(t0) //
        lw      t1,0x1c(sp)             // set data cache size
        sw      t1,PcFirstLevelDcacheSize(t0) //
        li      t1,R3000_CACHE_FILL_SIZE // set data cache fill size
        sw      t1,PcFirstLevelDcacheFillSize(t0) //
        sw      t1,PcDcacheFillSize(t0) //
        subu    t1,t1,1                 // set data cache alignment
        sw      t1,PcDcacheAlignment(t0) //
        li      t1,R3000_CACHE_LINE_SIZE - 1 // set instruction cache alignment
        sw      t1,PcIcacheAlignment(t0) //

        lw      ra,0x14(sp)             // restore return address
        addu    sp,sp,0x20              // deallocate stack frame
        j       ra                      // return

        .end    InitializePCR

        SBTTL("Disable Interrupts")
//++
//
// VOID
// DisableInterrupts (
//    VOID
//    )
//
// Routine Description:
//
//    This routine disables interrupts and gives the exceptions back to the
//    bootstrap vectors of the console, before the firmware returns to the
//    console.
//
// Arguments:
//
//    None.
//
// Return Value:
//
//    None.
//
//--

        LEAF_ENTRY(DisableInterrupts)

        .set    noreorder
        li      t0,(1 << PSR_BEV)       // disable interrupts, set BEV
        mtc0    t0,psr                  //
        j       ra                      // return
        nop                             //
        .set    reorder

        .end    DisableInterrupts
