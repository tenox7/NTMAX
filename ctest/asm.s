//
//  asm.s -- the assembler (cl on a .s file) with kxmips.h, in R3000 mode:
//  ll/sc, a load-use hazard with and without reorder, branch-likely, ldc1
//

#include "kxmips.h"

        LEAF_ENTRY(TestInterlockedIncrement)

10:     ll      v0,0(a0)                // R3000: kxmips.h makes ll lw
        addu    v0,v0,1
        move    t0,v0
        sc      t0,0(a0)                // R3000: kxmips.h makes sc sw
        beq     zero,t0,10b
        j       ra

        .end    TestInterlockedIncrement

        LEAF_ENTRY(TestReorder)

        lw      t0,0(a0)
        addu    v0,t0,1                 // reorder: the assembler must separate these
        j       ra

        .end    TestReorder

        .set    noreorder
        LEAF_ENTRY(TestNoReorder)

        lw      t0,0(a0)
        addu    v0,t0,1                 // noreorder: R3000 load-delay hazard as written
        j       ra
        nop

        .end    TestNoReorder
        .set    reorder
