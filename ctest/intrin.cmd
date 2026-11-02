@echo off
set TOP=T:\NTOS\3MAX
set O=%TOP%\out\intrin
if not exist %O% mkdir %O%
cd /d %TOP%\ctest
set PATH=%TOP%\C900\BIN;%SystemRoot%\system32
cl -nologo -c -Ox -W3 /QMR3000 -Fo%O%\intrin1.obj intrin.c
cl -nologo -c -Ox -W3 /QMR3000 -DCALLS -Fo%O%\intrin2.obj intrin.c
cl -nologo -c -Ox -Oi- -W3 /QMR3000 -Fo%O%\intrin3.obj intrin.c
for %%f in (1 2 3) do %MSDevDir%\BIN\DUMPBIN.EXE /DISASM %O%\intrin%%f.obj > %O%\intrin%%f.dis
