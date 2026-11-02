#!/bin/sh
#
#  build-crt.sh -- build libcntpr.lib, the C run-time of the system and the
#  firmware, for the R3000, as CRTLIB\MAKEFILE makes it: libcnt.lib, CRT32NT
#  as BUILDCRT NT builds it (CRTLIBTYPE NT), and trannt.lib, the math
#  functions of FP32NT\TRAN built the same way, compiled with C900 /QMR3000
#  on the MIPS NT machine (vm.sh).
#
#  The objects of libcnt.lib are the NT list of CRT32NT\MAKEFILE: OBJECTS
#  (with the helper objects) and the MIPS CRT_OBJECTS.  crt/mips has the
#  R3000 versions of MISC\MIPS\SETJMP.S, SETJMPEX.S and LONGJMP.S (single
#  word FP loads and stores); the C sources include build/r3000.h first (no
#  sqrt.d).  build-lib.sh builds trannt.lib (its R3000
#  sources: 3MAX/nt/PRIVATE/FP32NT/TRAN).  Output:
#  T:\NTOS\3MAX\out\libcntpr\libcntpr.lib, and in T:\NTOS\3MAX\out\crt
#  libcnt.lib, the objects and a .log for each source; every object is
#  checked with tools/r3kcheck.pl (obj/crt.o/r3kcheck.txt).

set -e
. "$(dirname "$0")/vm.sh"
CRT=$nt35/PRIVATE/CRT32NT
cmd=$SHARE/build/crt.cmd

ntcopy PRIVATE/CRT32NT PUBLIC/SDK/INC
mkdir -p "$SHARE/build" "$SHARE/crt"
cp "$here/build/r3000.h" "$SHARE/build/"
rsync -a --inplace --delete --exclude='*.ntdiff' "$here/crt/" "$SHARE/crt/"

# name DIR FILE: the name of FILE in DIR as the tree spells it
name() {
	ls "$CRT/$1" | grep -ix "$2" || { echo "build-crt: no $1/$2" >&2; exit 1; }
}

# sources DIR: the sources (and MIPS sources) a SOURCES file lists
sources() {
	tr -d '\r' < "$CRT/$1/SOURCES" | awk '
		/^(MIPS_)?SOURCES *=/ { p = 1; m = /^MIPS_/; sub(/^[A-Z_]* *= */, "") }
		p { s = $0; c = sub(/\\ *$/, "", s); n = split(s, w)
		    for (i = 1; i <= n; i++) printf "%s%s ", (m && w[i] !~ /\\/) ? "mips\\" : "", w[i]
		    if (!c) p = 0 }'
}

# The C (and helper) sources of OBJECTS, as DIR FILE pairs.
tr -d '\r' < "$CRT/MAKEFILE" |
awk '/TARGETNAMESUFFIX\)" == "nt"/ {n++} n == 2 && /OBJECTS=/ {p = 1} p {print} p && !/\\$/ {exit}' |
sed 's/\$(HELPER_OBJECTS)/helper\\obj\\*.obj/' | grep -oiE '[a-z]+\\obj\\[a-z0-9_*]+\.obj' | tr '\\' ' ' |
while read -r d o f; do
	D=$(echo $d | tr a-z A-Z)
	if [ "$f" = "*.obj" ]; then
		for s in $(sources $D); do
			case $s in
			mips\\*)	echo "$D/MIPS $(name $D/MIPS ${s#mips\\})" ;;
			*\\*)		;;
			*)		echo "$D $(name $D $s)" ;;
			esac
		done
	else
		echo "$D $(name $D ${f%.obj}.c)"
	fi
done > "$SHARE/build/crt.list"

# The MIPS CRT_OBJECTS: DIR\obj\*\NAME.obj, an assembler or C source in DIR\MIPS.
tr -d '\r' < "$CRT/MAKEFILE" |
awk '/^!IF [$][(]MIPS[)]/ {m = NR} NR == m + 1 && /^CRT_OBJECTS=/ {p = 1} p {print} p && !/\\$/ {exit}' |
grep -oiE '[a-z]+\\obj\\\*\\[a-z0-9_]+\.obj' | tr '\\' ' ' | while read -r d o x f; do
	D=$(echo $d | tr a-z A-Z)/MIPS
	b=${f%.obj}
	if [ -f "$here/crt/mips/$b.s" ]; then
		echo "3MAX $b.s"
	else
		echo "$D $(ls "$CRT/$D" | grep -ixE "$b\.(s|c)")"
	fi
done >> "$SHARE/build/crt.list"

# crt.cmd: compile each source in its directory, as build.exe does.
{
	cat <<-'EOF'
	@echo off
	rem crt.cmd -- made by build-crt.sh: compile CRT32NT for the R3000 and
	rem make libcnt.lib in T:\NTOS\3MAX\out\crt
	set TOP=T:\NTOS\3MAX
	set NT=%TOP%\NT
	set CRT=%NT%\PRIVATE\CRT32NT
	set O=%TOP%\out\crt
	set PATH=%TOP%\C900\BIN;%SystemRoot%\system32
	set INCLUDE=
	set DEFS=-DMIPS=1 -D_MIPS_=1 -DMIPSEL -DNO_EXT_KEYS -DCONDITION_HANDLING=1 -DWIN32_LEAN_AND_MEAN=1 -DNT_UP=1 -DNT_INST=0 -DWIN32=100 -D_NT1X_=100 -DR3000 -D_M_MRX000=3000 /QMR3000 -DDBG=0 -DDEVL=1 -D_WIN32_=1 -D_NTSUBSET_=1 -D_NTSYSTEM_=1 -D_INTL
	set CFLAGS=-nologo -c /Zel /Zp8 /Gy /W3 -Ox -Gt0 -D__stdcall= -D__cdecl= -D_LANGUAGE_C -DLANGUAGE_C -DNDEBUG -FI%TOP%\build\r3000.h
	set AFLAGS=-nologo -c /Zel /Zp8 /Gy /W3 -Gt0 -Ge -O -D_LANGUAGE_ASSEMBLY
	set INC=-Imips\ -I. -I%CRT%\H -I%NT%\PUBLIC\SDK\INC -I%NT%\PUBLIC\SDK\INC\CRT
	if not exist %O% mkdir %O%
	del /q %O%\*.* > nul 2>&1
	EOF
	while read -r d f; do
		b=$(echo "${f%.*}" | tr A-Z a-z)
		x=
		if [ "$d" = 3MAX ]; then
			dir='%TOP%\crt\mips'; f=$b.s
		else
			dir="%CRT%\\$(echo $d | tr / '\\')"
			# the C_DEFINES the SOURCES file of the directory adds
			x=$(tr -d '\r' < "$CRT/${d%%/*}/SOURCES" | sed -n 's/^C_DEFINES *= *\$(C_DEFINES) *//p')
		fi
		case $f in
		*.[sS])	printf '%s\n' "cd /d $dir & cl %AFLAGS% %INC% %DEFS% $x -Fo%O%\\$b.obj $f > %O%\\$b.log 2>&1" ;;
		*)	printf '%s\n' "cd /d $dir & cl %CFLAGS% %INC% %DEFS% $x -Fo%O%\\$b.obj $f > %O%\\$b.log 2>&1" ;;
		esac
	done < "$SHARE/build/crt.list"
	printf '%s\n' 'cd /d %O%'
	printf '%s\n' '%MSDevDir%\BIN\LIB.EXE -nologo -machine:mips -out:libcnt.lib *.obj > lib.log 2>&1'
} | sed 's/$/\r/' > "$cmd"

echo "build-crt: $(wc -l < "$SHARE/build/crt.list" | tr -d ' ') sources"
vmrun 'T:\NTOS\3MAX\build\crt.cmd'

# Report the sources that did not compile, then check the objects.
fail=0
while read -r d f; do
	b=$(echo "${f%.*}" | tr A-Z a-z)
	if [ ! -f "$SHARE/out/crt/$b.obj" ]; then
		echo "--- $d/$f failed:"; tr -d '\r' < "$SHARE/out/crt/$b.log" | grep -v '^$' | head -20
		fail=$((fail + 1))
	fi
done < "$SHARE/build/crt.list"
tr -d '\r' < "$SHARE/out/crt/lib.log"
[ -f "$SHARE/out/crt/libcnt.lib" ] || { echo "build-crt: no libcnt.lib ($fail failed)"; exit 1; }
# Docker gets copies of the objects: the files of the share it reads stay
# open in the SMB client for a while, and the next build cannot replace them.
mkdir -p "$here/obj"
rm -rf "$here/obj/crt.o"; mkdir "$here/obj/crt.o"
cp "$SHARE/out/crt/"*.obj "$here/obj/crt.o/"
docker run --rm -v "$here":/work/3MAX -w /work/3MAX/obj/crt.o --entrypoint sh 3max-build \
	-c 'perl /work/3MAX/tools/r3kcheck.pl *.obj' > "$here/obj/crt.o/r3kcheck.txt" 2>&1 || true
grep -E '^[a-z0-9_]+\.obj:' "$here/obj/crt.o/r3kcheck.txt" | grep -vE ': R3000, [0-9]+ instructions: (clean|[0-9]+ load in jump slot)$' || true
echo "build-crt: $fail failed, libcnt.lib: $(ls -l "$SHARE/out/crt/libcnt.lib" | awk '{print $5}') bytes"

# trannt.lib, then libcntpr.lib.
NTENV=CRTLIBTYPE=NT "$here/build/build-lib.sh" PRIVATE/FP32NT/TRAN
mkdir -p "$SHARE/out/libcntpr"
rm -f "$SHARE/out/libcntpr/"*
printf '%s\r\n' '@echo off' 'cd /d T:\NTOS\3MAX\out\libcntpr' \
	'%MSDevDir%\BIN\LIB.EXE -nologo -machine:mips -out:libcntpr.lib ..\crt\libcnt.lib ..\trannt\trannt.lib > lib.log 2>&1' \
	> "$SHARE/build/libcntpr.cmd"
vmrun 'T:\NTOS\3MAX\build\libcntpr.cmd'
i=0
while [ ! -f "$SHARE/out/libcntpr/libcntpr.lib" ] && [ $i -lt 20 ]; do sleep 1; i=$((i + 1)); done
tr -d '\r' < "$SHARE/out/libcntpr/lib.log"
[ -f "$SHARE/out/libcntpr/libcntpr.lib" ] || { echo "build-crt: no libcntpr.lib"; exit 1; }
echo "build-crt: libcntpr.lib: $(ls -l "$SHARE/out/libcntpr/libcntpr.lib" | awk '{print $5}') bytes"
