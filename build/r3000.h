/*
 *  r3000.h -- included first in every C file compiled for the R3000
 *  (cl -FI, build-lib.sh and build-crt.sh)
 *
 *  With -Ox, C900 makes sqrt a built-in function that computes the square
 *  root with sqrt.d, a MIPS II instruction the R3000 does not have, also
 *  with /QMR3000: sqrt is called as a function instead (the sqrt of the C
 *  run-time, sqrt3000.c for the R3000).  The C++ compiler takes the pragma
 *  only for a function declared before it.
 */

#ifdef __cplusplus
extern "C" double sqrt(double);
#endif
#pragma function(sqrt)
