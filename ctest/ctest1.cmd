@echo off
rem ctest1.cmd OUT CPU-SWITCHES -- one pass of ctest.cmd

set TOP=T:\NTOS\3MAX
set NT=%TOP%\NT
set O=%TOP%\out\%1
set CPU=%2 %3
set PATH=%TOP%\C900\BIN;C:\WINNT\system32
set INCLUDE=%NT%\PUBLIC\SDK\INC;%NT%\PUBLIC\SDK\INC\CRT
set LIB=
set DEFS=-DMIPS=1 -D_MIPS_=1 -DMIPSEL -DNO_EXT_KEYS -DCONDITION_HANDLING=1 -DWIN32_LEAN_AND_MEAN=1 -DNT_UP=1 -DNT_INST=0 -DWIN32=100 -D_NT1X_=100 -DDBG=0 -DDEVL=1
set CFLAGS=-nologo -c /Zel /Zp8 /Gy /W3 -Ox -Gt32 -D__stdcall= -D__cdecl= -D_LANGUAGE_C -DLANGUAGE_C
set AFLAGS=-nologo -c -Gt32 -Ge -O -D_LANGUAGE_ASSEMBLY
set NTINC=-I mips\ -I. -I.. -I..\..\inc -I..\..\..\inc -I%NT%\GEN -I%NT%\PUBLIC\SDK\INC -I%NT%\PUBLIC\SDK\INC\CRT -I%NT%\PUBLIC\OAK\INC

if not exist %TOP%\out mkdir %TOP%\out
if not exist %O% mkdir %O%
if exist %O%\*.* del /q %O%\*.*

cd /d %TOP%\ctest
cl %CFLAGS% %DEFS% %CPU% -Fo%O%\ int.c fp.c seh.c > %O%\c.log 2>&1
cl -nologo -c -Ox -GX %CPU% -Fo%O%\ cxx.cpp > %O%\cxx.log 2>&1
for %%f in (asm bl dbl trap) do cl %AFLAGS% %DEFS% %CPU% -Fo%O%\%%f.obj %%f.s > %O%\%%f.log 2>&1

rem the kernel run-time library, as RTL\UP builds it
cd /d %NT%\PRIVATE\NTOS\RTL\UP
cl %CFLAGS% %NTINC% %DEFS% -D_NTSYSTEM_ -DNTOS_KERNEL_RUNTIME=1 %CPU% -Fo%O%\ ..\bitmap.c ..\heap.c ..\string.c ..\time.c ..\mips\exdsptch.c ..\mips\chandler.c > %O%\rtl.log 2>&1
for %%f in (xxmvmem largeint xcptmisc chkstk) do cl %AFLAGS% %NTINC% %DEFS% -DNTOS_KERNEL_RUNTIME=1 %CPU% -Fo%O%\%%f.obj ..\mips\%%f.s > %O%\%%f.log 2>&1

rem the executive interlocked functions, the R3000 and the R4000 versions
cd /d %NT%\PRIVATE\NTOS\EX\UP
for %%f in (x3intrlk x4intrlk) do cl %AFLAGS% %NTINC% %DEFS% -D_NTSYSTEM_ %CPU% -Fo%O%\%%f.obj ..\mips\%%f.s > %O%\%%f.log 2>&1

cd /d %O%
for %%f in (*.obj) do C:\MSDEV\BIN\DUMPBIN.EXE /DISASM /HEADERS %%f > %%f.dis 2>&1
