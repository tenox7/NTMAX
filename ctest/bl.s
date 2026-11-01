//
//  bl.s -- branch-likely (MIPS II): R3000 mode should reject it
//

#include "kxmips.h"

        LEAF_ENTRY(TestBranchLikely)

        beql    a0,zero,20f
        addu    v0,v0,1
20:     j       ra

        .end    TestBranchLikely
