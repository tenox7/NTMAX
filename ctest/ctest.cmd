@echo off
rem ctest.cmd -- compile the tests and genuine NT 3.5 kernel files with C900,
rem once for the R4000 (the default) and once for the R3000 (/QMR3000).
rem Objects, logs and disassembly (VC++ 4.0 DUMPBIN, *.dis) go to
rem ..\out\r4000 and ..\out\r3000.

call T:\NTOS\3MAX\ctest\ctest1.cmd r4000 -DR4000
call T:\NTOS\3MAX\ctest\ctest1.cmd r3000 -DR3000 /QMR3000
