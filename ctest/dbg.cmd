@echo off
rem dbg.cmd -- the tests and RTL files unoptimized with debug information
rem (/Od /Z7, as NT debug builds), R3000 mode; objects to ..\out\dbg3000
set TOP=T:\NTOS\3MAX
set NT=%TOP%\NT
set O=%TOP%\out\dbg3000
set PATH=%TOP%\C900\BIN;%SystemRoot%\system32
set INCLUDE=%NT%\PUBLIC\SDK\INC;%NT%\PUBLIC\SDK\INC\CRT
set DEFS=-DMIPS=1 -D_MIPS_=1 -DMIPSEL -DNO_EXT_KEYS -DCONDITION_HANDLING=1 -DWIN32_LEAN_AND_MEAN=1 -DNT_UP=1 -DNT_INST=0 -DWIN32=100 -D_NT1X_=100 -DDBG=1 -DDEVL=1 -DR3000 /QMR3000
set CFLAGS=-nologo -c /Zel /Zp8 /Gy /W3 -Od -Z7 -Gt32 -D__stdcall= -D__cdecl= -D_LANGUAGE_C -DLANGUAGE_C
set NTINC=-I mips\ -I. -I.. -I..\..\inc -I..\..\..\inc -I%NT%\GEN -I%NT%\PUBLIC\SDK\INC -I%NT%\PUBLIC\SDK\INC\CRT -I%NT%\PUBLIC\OAK\INC
if not exist %O% mkdir %O%
if exist %O%\*.* del /q %O%\*.*
cd /d %TOP%\ctest
cl %CFLAGS% %DEFS% -Fo%O%\ int.c fp.c seh.c > %O%\c.log 2>&1
cl -nologo -c -Od -Z7 -GX /QMR3000 -Fo%O%\ cxx.cpp > %O%\cxx.log 2>&1
cd /d %NT%\PRIVATE\NTOS\RTL\UP
cl %CFLAGS% %NTINC% %DEFS% -D_NTSYSTEM_ -DNTOS_KERNEL_RUNTIME=1 -Fo%O%\ ..\bitmap.c ..\heap.c ..\string.c ..\time.c ..\mips\exdsptch.c ..\mips\chandler.c > %O%\rtl.log 2>&1
