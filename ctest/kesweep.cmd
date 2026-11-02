@echo off
rem kesweep.cmd -- compile the NT 3.5 kernel (KE, as KE\UP builds it) for the
rem R3000 with C900, every file on its own; objects and logs to ..\out\ke3000

set TOP=T:\NTOS\3MAX
set NT=%TOP%\NT
set O=%TOP%\out\ke3000
set PATH=%TOP%\C900\BIN;%SystemRoot%\system32
set INCLUDE=%NT%\PUBLIC\SDK\INC;%NT%\PUBLIC\SDK\INC\CRT
set DEFS=-DMIPS=1 -D_MIPS_=1 -DMIPSEL -DNO_EXT_KEYS -DCONDITION_HANDLING=1 -DWIN32_LEAN_AND_MEAN=1 -DNT_UP=1 -DNT_INST=0 -DWIN32=100 -D_NT1X_=100 -DDBG=0 -DDEVL=1 -D_NTSYSTEM_ -DR3000 /QMR3000
set CFLAGS=-nologo -c /Zel /Zp8 /Gy /W3 -Ox -Gt32 -D__stdcall= -D__cdecl= -D_LANGUAGE_C -DLANGUAGE_C
set AFLAGS=-nologo -c -Gt32 -Ge -O -D_LANGUAGE_ASSEMBLY
set NTINC=-I..\mips -I. -I.. -I..\..\inc -I..\..\..\inc -I%NT%\GEN -I%NT%\PUBLIC\SDK\INC -I%NT%\PUBLIC\SDK\INC\CRT -I%NT%\PUBLIC\OAK\INC

if not exist %O% mkdir %O%
if exist %O%\*.* del /q %O%\*.*
cd /d %NT%\PRIVATE\NTOS\KE\UP

for %%f in (apcobj apcsup balmgr bugcheck config debug devquobj dpcobj dpcsup eventobj kernldat kiinit miscc mutntobj procobj profobj queueobj raisexcp semphobj thredobj thredsup timerobj timersup wait waitsup) do cl %CFLAGS% %NTINC% %DEFS% -Fo%O%\%%f.obj ..\%%f.c > %O%\%%f.log 2>&1

for %%f in (alignem allproc apcuser branchem buserror dmpstate exceptn floatem flush getsetrg thredini xxflshtb xxmpipi initkr intobj vdm) do cl %CFLAGS% %NTINC% %DEFS% -Fo%O%\%%f.obj ..\mips\%%f.c > %O%\%%f.log 2>&1

for %%f in (timindex xxapcint xxclock xxintsup xxirql xxmiscs x4mpipi xxregsv xxspinlk x4ctxsw x4trap threadbg wakedbg x4start x4sqrt) do cl %AFLAGS% %NTINC% %DEFS% -Fo%O%\%%f.obj ..\mips\%%f.s > %O%\%%f.log 2>&1
