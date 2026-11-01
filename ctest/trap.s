//
//  trap.s -- sync and teq (MIPS II): R3000 mode should reject them
//

#include "kxmips.h"

        LEAF_ENTRY(TestSync)

        sync
        teq     a0,zero
        j       ra

        .end    TestSync
