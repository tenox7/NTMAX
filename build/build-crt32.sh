#!/bin/sh
#
#  build-crt32.sh -- build a Win32 C run-time of CRT32 and FP32 for the R3000
#  the way BUILDCRT and CRTLIB\MAKEFILE build it, with C900 /QMR3000 on the
#  MIPS NT machine (vm.sh):
#
#	build-crt32.sh ST|MT|DLL
#	FROM=COMPONENT build-crt32.sh ST|MT|DLL
#
#  ST makes libc.lib, in T:\NTOS\3MAX\out\libc with the link option
#  objects (binmode.obj, commode.obj, setargv.obj, chkstk.obj), MT libcmt.lib
#  in out\libcmt, DLL libcdll.lib with the startup objects of the images that
#  use crtdll.dll (crtexe.obj, crtexew.obj, crtdll.obj, dllargv.obj,
#  dllmain.obj, cinitexe.obj) in out\crt32dll, and convdll.lib and
#  trandll.lib, from which build-crtdll.sh links crtdll.dll.
#
#  1. BUILDCRT TYPE: the components of CRT32 that CRT32\BUILDCRT.CMD builds
#     for TYPE (CRT32\DIRS and the OPTIONAL_DIRS it names), each with
#     build-lib.sh and CRTLIBTYPE=TYPE (NTENV), into out\crt32st\COMPONENT
#     (crt32 for MT, crt32dll): the copies of CRT32 that CRTLIB takes the
#     libraries of (..\crt32st, ..\crt32, ..\crt32dll).  The R3000 versions
#     of MISC\MIPS\SETJMP.S, SETJMPEX.S and LONGJMP.S are in 3MAX/nt.
#  2. CRT32\MAKEFILE: the objects of each component go to COMPONENT\obj\mips
#     of the tree on the share, where the makefile takes them, and nmake
#     makes obj\mips\libcSUFFIX.lib (libc.lib, libcmt.lib, libcdll.lib) and
#     the extra objects there, with the objects of CRT32 that are in the
#     tree (the initializers, COMPONENT\MIPS\CINIT*.OBJ, CRT0INIT.OBJ).
#  3. FP32\BUILDCRT.CMD: CONV and TRAN with build-lib.sh, CRTLIBTYPE=TYPE:
#     convSUFFIX.lib and tranSUFFIX.lib (the R3000 versions of TRAN in
#     3MAX/nt, as for FP32NT).
#  4. CRTLIB\MAKEFILE: libc.lib = CRT32's libc.lib + conv.lib + tran.lib,
#     libcmt.lib = libcmt.lib + convmt.lib + tranmt.lib; ST also puts the
#     small.lib of SMALL in out\small.
#
#  Every object is checked with tools/r3kcheck.pl (build-lib.sh).

set -e
. "$(dirname "$0")/vm.sh"
[ $# -eq 1 ] || { echo "usage: build-crt32.sh ST|MT|DLL" >&2; exit 1; }
TYPE=$1
# (BUILDCRT.CMD: the components, after those of DIRS: convert misc startup
# stdio string)
case $TYPE in
ST)	sfx= tree=crt32st final=libc
	comps="linkopts startup time winheap direct dos exec iostream lowio small mbstring helper" ;;
MT)	sfx=mt tree=crt32 final=libcmt
	comps="startup time winheap direct dos exec iostream lowio mbstring helper" ;;
DLL)	sfx=dll tree=crt32dll final=
	comps="startup time winheap dllstuff direct dos exec iostream lowio mbstring helper" ;;
*)	echo "build-crt32: TYPE is ST, MT or DLL" >&2; exit 1 ;;
esac
comps="convert misc stdio string $comps"
export NTENV="CRTLIBTYPE=$TYPE"

# (CRT32\SMALL\MIPS\HUGE.S includes ..\..\fp32st\tran\mips\huge.s: the copy
# of FP32 that CRTLIB builds for ST, which GHOSTFP.CMD ST enlists beside it)
if [ $TYPE = ST ]; then
	mkdir -p "$SHARE/NT/PRIVATE/FP32ST"
	rsync -a --inplace "$nt35/PRIVATE/FP32/" "$SHARE/NT/PRIVATE/FP32ST/"
	rsync -a --inplace --exclude='*.ntdiff' "$here/nt/PRIVATE/FP32/" "$SHARE/NT/PRIVATE/FP32ST/"
fi

# 1. the components (FROM=COMPONENT: from that one on, the others as they
# were built)
for c in $comps; do
	[ -z "$FROM" ] || [ "$c" = "$FROM" ] || { echo "build-crt32: $c as built"; continue; }
	FROM=
	C=$(echo $c | tr a-z A-Z)
	log=$SHARE/build/crt32-$tree-$c.out
	OUT=$tree/$c sh "$here/build/build-lib.sh" PRIVATE/CRT32/$C > "$log" 2>&1 ||
		{ cat "$log"; echo "build-crt32: $C failed"; exit 1; }
	grep -E '^(--- |[a-z0-9_]+\.obj:)' "$log" || true
	tail -1 "$log"
	grep -q '^build-lib: 0 failed' "$log" || { echo "build-crt32: $C failed"; exit 1; }
done

# 2. CRT32\MAKEFILE, in the tree on the share
ntcopytop PRIVATE/CRT32
for c in $comps; do
	d=$SHARE/NT/PRIVATE/CRT32/$(echo $c | tr a-z A-Z)/obj/mips
	mkdir -p "$d"; rm -f "$d"/*
	cp "$SHARE/out/$tree/$c/"*.obj "$d/"
done
o=$SHARE/NT/PRIVATE/CRT32/obj/mips
mkdir -p "$o"; rm -f "$o"/*
printf '%s\r\n' '@echo off' 'set PATH=C:\MSDEV\BIN;C:\WINNT\system32' 'cd /d T:\NTOS\3MAX\NT\PRIVATE\CRT32' \
	"C:\\MSDEV\\BIN\\NMAKE.EXE -nologo -f makefile MIPS=1 386=0 ALPHA=0 CRTLIBTYPE=$TYPE > T:\\NTOS\\3MAX\\out\\$tree\\makefile.log 2>&1" \
	> "$SHARE/build/crt32-$tree.cmd"
vmrun "T:\\NTOS\\3MAX\\build\\crt32-$tree.cmd"
i=0
while [ ! -f "$o/libc$sfx.lib" ] && [ $i -lt 20 ]; do sleep 1; i=$((i + 1)); done
tr -d '\r' < "$SHARE/out/$tree/makefile.log" | grep -v '^$' | grep -iv '^ *lib -out\|^ *copy ' || true
[ -f "$o/libc$sfx.lib" ] || { echo "build-crt32: no libc$sfx.lib"; exit 1; }
rm -f "$SHARE/out/$tree/"*.lib "$SHARE/out/$tree/"*.obj
cp "$o/"* "$SHARE/out/$tree/"
echo "build-crt32: $tree libc$sfx.lib: $(ls -l "$SHARE/out/$tree/libc$sfx.lib" | awk '{print $5}') bytes," \
	"$(cd "$SHARE/out/$tree" && ls *.obj 2>/dev/null | tr '\n' ' ')"

# 3. FP32 CONV and TRAN
for c in CONV TRAN; do
	log=$SHARE/build/crt32-fp32-$c$sfx.out
	sh "$here/build/build-lib.sh" PRIVATE/FP32/$c > "$log" 2>&1 ||
		{ cat "$log"; echo "build-crt32: FP32 $c failed"; exit 1; }
	grep -E '^(--- |[a-z0-9_]+\.obj:)' "$log" || true
	tail -1 "$log"
	grep -q '^build-lib: 0 failed' "$log" || { echo "build-crt32: FP32 $c failed"; exit 1; }
done

# 4. CRTLIB\MAKEFILE (for DLL, build-crtdll.sh)
[ -n "$final" ] || exit 0
mkdir -p "$SHARE/out/$final"
rm -f "$SHARE/out/$final/"*
{
	printf '%s\n' '@echo off' "cd /d T:\\NTOS\\3MAX\\out\\$final"
	printf '%s\n' "C:\\MSDEV\\BIN\\LIB.EXE -nologo -machine:mips -out:$final.lib ..\\$tree\\libc$sfx.lib ..\\conv$sfx\\conv$sfx.lib ..\\tran$sfx\\tran$sfx.lib > lib.log 2>&1"
	# (RELEASE_OBJECTS, of libc)
	[ $TYPE != ST ] || printf '%s\n' "for %%f in (binmode commode setargv chkstk) do copy ..\\$tree\\%%f.obj . > nul"
} | sed 's/$/\r/' > "$SHARE/build/crt32-$final.cmd"
vmrun "T:\\NTOS\\3MAX\\build\\crt32-$final.cmd"
i=0
while [ ! -f "$SHARE/out/$final/$final.lib" ] && [ $i -lt 20 ]; do sleep 1; i=$((i + 1)); done
tr -d '\r' < "$SHARE/out/$final/lib.log"
[ -f "$SHARE/out/$final/$final.lib" ] || { echo "build-crt32: no $final.lib"; exit 1; }
echo "build-crt32: $final.lib: $(ls -l "$SHARE/out/$final/$final.lib" | awk '{print $5}') bytes" \
	"$(cd "$SHARE/out/$final" && ls *.obj 2>/dev/null | tr '\n' ' ')"
# (RELEASE_SMALL: small.lib, of crt32st\small, for the images of a GPSIZE)
if [ $TYPE = ST ]; then
	mkdir -p "$SHARE/out/small"
	cp "$SHARE/out/$tree/small/small.lib" "$SHARE/out/small/small.lib"
fi
