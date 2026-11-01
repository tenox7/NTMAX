#!/bin/sh
#
#  build-crtdll.sh -- build crtdll.dll, the C run-time DLL, and its import
#  library crtdll.lib for the R3000 the way the MIPS rules of
#  CRTLIB\MAKEFILE build them, on the MIPS NT machine (vm.sh):
#
#	build-crtdll.sh
#
#  It takes what build-crt32.sh made: libcdll.lib, convdll.lib, trandll.lib
#  and the extra objects of crt32dll (DLL), the txtmode.obj, ncommode.obj
#  and tlssup.obj of crt32 (MT) and the chkstk.obj of libc (ST); and the
#  import library of kernel32 (build-image.sh PRIVATE/WINDOWS/BASE/CLIENT).
#
#  1. crtdll.def: lib\crtdll.def and lib\mips\crtdll.mip (the MIPS
#     helpers: __C_specific_handler, the __ll_ routines, setjmp), with copy
#     /b (the makefile's copy of A+B writes a Ctrl-Z after them).
#  2. crtdll.lib and crtdll.exp: lib -def, then the objects of
#     COMPONENTS_SUPPOBJS go in the library: the startup code that the
#     programs and DLLs of crtdll.dll take from it (crtexe.obj, crtexew.obj,
#     crtdll.obj, dllargv.obj, dllmain.obj, cinitexe.obj, txtmode.obj,
#     ncommode.obj, tlssup.obj, chkstk.obj).  The thunks of the import
#     library are made R3000 code (r3kimplib.pl).
#  3. crtdll.res: rc with RCMIPS_DEFS of the makefile (-DR3000
#     -D_M_MRX000=3000) and RC_INCL.
#  4. crtdll.dll: link with the options of the makefile (-dll
#     -base:0x10010000 -entry:_CRTDLL_INIT, no makefile.def options) and
#     -incremental:no and a map, of crtdll.exp, crtdll.res, libcdll.lib,
#     convdll.lib, trandll.lib and kernel32.lib; checked with r3kcheck.pl
#     and marked R3000 (r3kmark.pl), in obj/system32.
#  The import library, the export file and the DLL stay in
#  T:\NTOS\3MAX\out\img\crtdll, where build-image.sh finds crtdll.lib for
#  the images of USE_CRTDLL.

set -e
. "$(dirname "$0")/vm.sh"
[ $# -eq 0 ] || { echo "usage: build-crtdll.sh" >&2; exit 1; }

# COMPONENTS_SUPPOBJS of the MIPS rules (cinitexe.obj: the copy of
# dllstuff\mips\cinitexe.obj that CRT32\MAKEFILE makes; chkstk.obj:
# lib\mips\chkstk.obj, the one of libc)
supp="crt32dll\\crtexe.obj crt32dll\\crtexew.obj crt32dll\\crtdll.obj crt32dll\\dllargv.obj crt32dll\\dllmain.obj
crt32\\lowio\\txtmode.obj crt32\\stdio\\ncommode.obj crt32\\startup\\tlssup.obj crt32dll\\cinitexe.obj libc\\chkstk.obj"
# COMPONENTS_CRTDLL and LINKLIBS
libs="crt32dll\\libcdll.lib convdll\\convdll.lib trandll\\trandll.lib img\\kernel32\\kernel32.lib"
for f in $supp $libs; do
	[ -f "$SHARE/out/$(printf "%s\n" "$f" | tr '\\' /)" ] ||
		{ echo "build-crtdll: no out\\$f (build-crt32.sh ST, MT and DLL; build-image.sh)" >&2; exit 1; }
done
objs=$(for f in $supp; do printf ' %%L%%\\%s' "$f"; done)
lnk=$(for f in $libs; do printf ' %%L%%\\%s' "$f"; done)

ntcopy PRIVATE/CRTLIB PUBLIC/SDK/INC PUBLIC/OAK/INC
out=$SHARE/out/img/crtdll
mkdir -p "$out" "$SHARE/build"
rm -f "$out/"*
{
	cat <<-'EOF'
	@echo off
	rem crtdll.cmd -- made by build-crtdll.sh: crtdll.lib and crtdll.dll for the R3000
	set TOP=T:\NTOS\3MAX
	set NT=%TOP%\NT
	set PATH=%TOP%\C900\BIN;C:\MSDEV\BIN;C:\WINNT\system32
	set INCLUDE=
	set L=%TOP%\out
	set O=%L%\img\crtdll
	cd /d %NT%\PRIVATE\CRTLIB
	copy /b lib\crtdll.def+lib\mips\crtdll.mip %O%\crtdll.def > nul
	C:\MSDEV\BIN\LIB.EXE -nologo -machine:mips -def:%O%\crtdll.def -out:%O%\tmp.lib > %O%\def.log 2>&1
	ren %O%\tmp.exp crtdll.exp
	EOF
	printf '%s\n' "C:\\MSDEV\\BIN\\LIB.EXE -nologo -machine:mips -out:%O%\\crtdll.lib %O%\\tmp.lib$objs > %O%\\lib.log 2>&1" \
		'del %O%\tmp.lib'
	# (RCMIPS_DEFS and RC_INCL of the makefile)
	printf '%s\n' "rc -l 409 -r -fo %O%\\crtdll.res -DMIPS=1 -D_MIPS_=1 -DNO_EXT_KEYS -DWIN32_LEAN_AND_MEAN=1 -DMIPSEL -DCONDITION_HANDLING=1 -DNT_INST=0 -DJAZZ -DNT_UP=1 -DR3000 -D_M_MRX000=3000 -DDBG=0 -DDEVL=1 -DNOFIREWALLS -DNDEBUG -D_WIN32_=1 -D__stdcall= -D__cdecl= -D_INTL -x -I%NT%\\PUBLIC\\OAK\\INC -I%NT%\\PUBLIC\\SDK\\INC -I%NT%\\PUBLIC\\SDK\\INC\\CRT crtdll.rc > %O%\\rc.log 2>&1"
	printf '%s\n' "C:\\MSDEV\\BIN\\LINK.EXE -nologo -dll -out:%O%\\crtdll.dll -debugtype:both -debug:minimal -machine:mips -release -pdb:none -incremental:no -base:0x10010000 -entry:_CRTDLL_INIT -map:%O%\\crtdll.map %O%\\crtdll.exp %O%\\crtdll.res$lnk > %O%\\link.log 2>&1"
} | sed 's/$/\r/' > "$SHARE/build/crtdll.cmd"
vmrun "T:\\NTOS\\3MAX\\build\\crtdll.cmd" || echo "build-crtdll: crtdll.cmd ended with an error"

# (the share's client can see the files of the machine a few seconds late)
i=0
while [ ! -f "$out/link.log" ] && [ $i -lt 20 ]; do sleep 1; i=$((i + 1)); done
for f in def lib rc link; do
	[ ! -f "$out/$f.log" ] || tr -d '\r' < "$out/$f.log" | grep -v '^$' | grep -v 'LNK4087\|LNK4088' | sed "s/^/$f: /" || true
done
[ -f "$out/crtdll.lib" ] || { echo "build-crtdll: no crtdll.lib"; exit 1; }
[ -f "$out/crtdll.dll" ] || { echo "build-crtdll: no crtdll.dll"; exit 1; }

# The thunks of the import library (Docker gets a copy, see build-lib.sh).
rm -rf "$here/obj/implib/crtdll"; mkdir -p "$here/obj/implib/crtdll"
cp "$out/crtdll.lib" "$here/obj/implib/crtdll/"
docker run --rm -v "$here":/work/3MAX -w /work/3MAX/obj/implib/crtdll --entrypoint sh 3max-build \
	-c "perl /work/3MAX/tools/r3kimplib.pl crtdll.lib r3k.lib"
cp "$here/obj/implib/crtdll/r3k.lib" "$out/crtdll.lib"

# The DLL, checked and marked R3000.
mkdir -p "$here/obj/system32"
cp "$out/crtdll.dll" "$out/crtdll.map" "$here/obj/system32/"
docker run --rm -v "$here":/work/3MAX -w /work/3MAX --entrypoint sh 3max-build \
	-c "perl tools/r3kcheck.pl obj/system32/crtdll.dll && perl tools/r3kmark.pl obj/system32/crtdll.dll" \
	> "$here/obj/system32/crtdll-r3k.txt" 2>&1 || true
grep -F "crtdll.dll:" "$here/obj/system32/crtdll-r3k.txt" || cat "$here/obj/system32/crtdll-r3k.txt"
ls -l "$here/obj/system32/crtdll.dll"
