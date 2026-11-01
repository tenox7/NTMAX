//
//  dbl.s -- ldc1/sdc1 (MIPS II): R3000 mode should reject them
//

#include "kxmips.h"

        LEAF_ENTRY(TestDouble)

        ldc1    f0,0(a0)
        sdc1    f0,8(a0)
        j       ra

        .end    TestDouble
