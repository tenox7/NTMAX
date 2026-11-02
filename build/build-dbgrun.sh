#!/bin/sh
#
#  build-dbgrun.sh -- build tools/dbgrun.c, the small debugger that runs a
#  program of the MIPS NT machine (vm.sh) and reports its faults on the
#  console instead of in an error box on the machine's screen, as a program
#  of the machine with its MSDEV compiler and C library:
#
#	T:\NTOS\3MAX\host\dbgrun.exe	(dbgrun PROGRAM [ARGUMENTS...])

set -e
. "$(dirname "$0")/vm.sh"
mkdir -p "$SHARE/host" "$SHARE/build"
sed 's/$/\r/' "$here/tools/dbgrun.c" > "$SHARE/build/dbgrun.c"
rm -f "$SHARE/host/dbgrun.exe"
printf '%s\r\n' '@echo off' 'set PATH=%MSDevDir%\BIN;%SystemRoot%\system32' 'set INCLUDE=%MSDevDir%\INCLUDE' \
	'set LIB=%MSDevDir%\LIB' 'cd /d T:\NTOS\3MAX\build' \
	'cl -nologo -O2 -W3 -Fodbgrun.obj -FeT:\NTOS\3MAX\host\dbgrun.exe dbgrun.c > dbgrun.log 2>&1' \
	> "$SHARE/build/dbgrun.cmd"
vmrun 'T:\NTOS\3MAX\build\dbgrun.cmd'
tr -d '\r' < "$SHARE/build/dbgrun.log" | grep -v '^dbgrun\.c$' || true
for i in 1 2 3 4 5 6 7 8 9 10; do [ ! -f "$SHARE/host/dbgrun.exe" ] || break; sleep 1; done
[ -f "$SHARE/host/dbgrun.exe" ] || { echo "build-dbgrun: no dbgrun.exe"; exit 1; }
echo "build-dbgrun: T:\\NTOS\\3MAX\\host\\dbgrun.exe"
