@echo off
rem one.cmd FILE CPU-SWITCHES -- compile one RTL file (for timing)
set TOP=T:\NTOS\3MAX
set NT=%TOP%\NT
set PATH=%TOP%\C900\BIN;%SystemRoot%\system32
set INCLUDE=%NT%\PUBLIC\SDK\INC;%NT%\PUBLIC\SDK\INC\CRT
set DEFS=-DMIPS=1 -D_MIPS_=1 -DMIPSEL -DNO_EXT_KEYS -DCONDITION_HANDLING=1 -DWIN32_LEAN_AND_MEAN=1 -DNT_UP=1 -DNT_INST=0 -DWIN32=100 -D_NT1X_=100 -DDBG=0 -DDEVL=1
set CFLAGS=-nologo -c /Zel /Zp8 /Gy /W3 -Ox -Gt32 -D__stdcall= -D__cdecl= -D_LANGUAGE_C -DLANGUAGE_C
set NTINC=-I mips\ -I. -I.. -I..\..\inc -I..\..\..\inc -I%NT%\GEN -I%NT%\PUBLIC\SDK\INC -I%NT%\PUBLIC\SDK\INC\CRT -I%NT%\PUBLIC\OAK\INC
cd /d %NT%\PRIVATE\NTOS\RTL\UP
cl %CFLAGS% %NTINC% %DEFS% -D_NTSYSTEM_ -DNTOS_KERNEL_RUNTIME=1 %2 %3 -Fo%TOP%\out\one.obj %1
