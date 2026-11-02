@echo off
rem chello.cmd -- compile chello.c for the R3000 with C900 and link it with
rem the VC++ 4.0 linker at 0x80030000 (the DEC PROM load address)
set TOP=T:\NTOS\3MAX
set O=%TOP%\out\chello
if not exist %O% mkdir %O%
if exist %O%\*.* del /q %O%\*.*
cd /d %TOP%\ctest
set PATH=%TOP%\C900\BIN;%SystemRoot%\system32
cl -nologo -c -Ox -Gt0 -W3 /QMR3000 -Fo%O%\chello.obj chello.c
cd /d %O%
%MSDevDir%\BIN\LINK.EXE -nologo -machine:mips -fixed -base:0x80030000 -entry:entry -subsystem:native -nodefaultlib -map:chello.map -out:chello.exe chello.obj
%MSDevDir%\BIN\DUMPBIN.EXE /HEADERS chello.exe > chello.hdr
%MSDevDir%\BIN\DUMPBIN.EXE /DISASM chello.obj > chello.dis
