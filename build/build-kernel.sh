#!/bin/sh
#
#  build-kernel.sh -- link the NT kernel, NTOSKRNL.EXE, and the HAL of the
#  DECstation 5000/200, HAL.DLL, for the R3000 on the MIPS NT machine
#  (vm.sh) the way makefile.def links them, from the libraries build-lib.sh
#  and build-crt.sh made:
#
#	the kernel		NTOS\INIT\UP (NTTEST=ntoskrnl): init.lib and
#				ntoskrnl.res, with the libraries of the other
#				kernel components (cache config dbgk ex FsRtl
#				io kd ke lpc mm ntosrtl ob ps raw se vdm),
#				lsakrnlp.lib and libcntpr.lib
#	the HAL			NTOS\NTHALS\HAL3MAX (TARGETTYPE=HAL): its
#				objects and hal.res
#
#  As makefile.def and the makefile.inc files do, the export lists come
#  first (ntoskrnl.src + mipsdef.src and hal.src through the preprocessor),
#  then the import libraries and export files (lib -def), then the images:
#  the kernel imports from hal.lib and the HAL from ntoskrnl.lib.  The
#  thunks of the import libraries are made R3000 code (r3kimplib.pl) before
#  the images are linked.  The images are checked with r3kcheck.pl.
#  Output: obj/ntoskrnl.exe, obj/hal.dll and their maps (make-disk.sh puts
#  them in \winnt\system32).  Logs: T:\NTOS\3MAX\out\ntoskrnl\*.log.

set -e
. "$(dirname "$0")/vm.sh"
out=$SHARE/out/ntoskrnl
KLIBS="cache config dbgk ex FsRtl init io kd ke lpc mm ntosrtl ob ps raw se vdm"

for l in init/ntoskrnl.res lsakrnlp/lsakrnlp.lib libcntpr/libcntpr.lib hal3max/hal.res; do
	[ -f "$SHARE/out/$l" ] || { echo "build-kernel: no out/$l (build-lib.sh, build-crt.sh)" >&2; exit 1; }
done
for l in $KLIBS hal3max; do
	[ -f "$SHARE/out/$l/$l.lib" ] || { echo "build-kernel: no out/$l/$l.lib (build-lib.sh)" >&2; exit 1; }
done

# The export lists and the HAL's sources (hal.src includes ..\hal.src).
ntcopy PRIVATE/NTOS/INIT PRIVATE/NTOS/NTHALS/HAL3MAX
ntcopytop PRIVATE/NTOS/NTHALS

# ntoskrnl.cmd: the export lists, the import libraries and export files.
{
	cat <<-'EOF'
	@echo off
	rem ntoskrnl.cmd -- made by build-kernel.sh: the import libraries of NTOSKRNL.EXE and HAL.DLL
	set TOP=T:\NTOS\3MAX
	set NT=%TOP%\NT
	set PATH=%TOP%\C900\BIN;C:\MSDEV\BIN;C:\WINNT\system32
	set INCLUDE=
	set O=%TOP%\out\ntoskrnl
	set L=%TOP%\out
	cd /d %O%
	copy %NT%\PRIVATE\NTOS\INIT\ntoskrnl.src+%NT%\PRIVATE\NTOS\INIT\mipsdef.src ntoskrnl.pp > nul
	cl -nologo -EP -DMIPS -D_MIPS_ -D_NTSYSTEM_ -Tcntoskrnl.pp > ntoskrnl.def 2> def.log
	cd /d %NT%\PRIVATE\NTOS\NTHALS\HAL3MAX
	cl -nologo -EP -DMIPS=1 -DDECSTATION -DKN02_AA -Tchal.src > %O%\hal.def 2>> %O%\def.log
	cd /d %O%
	C:\MSDEV\BIN\LIB.EXE -nologo -machine:mips -def:hal.def -out:hal3max.lib %L%\hal3max\*.obj > lib.log 2>&1
	copy hal3max.lib hal.lib > nul
	C:\MSDEV\BIN\LIB.EXE -nologo -machine:mips -def:ntoskrnl.def -out:ntoskrnl.lib >> lib.log 2>&1
	EOF
} | sed 's/$/\r/' > "$SHARE/build/ntoskrnl.cmd"

# ntoslink.cmd: the images, with the linker options of makefile.def:
# LINKER_FLAGS (with -debug:notmapped and the COFF symbols the kernel
# debugger reads), then the NTTEST rule of the kernel (MIPSMK.INC
# NTTEST_LINK_OPTIONS, GPSIZE=32 of INIT\UP\SOURCES) and the HAL rule
# (HALBASE).  libcntpr's memorym.obj defines memcpy, memmove and memset
# again, which ntosrtl's xxmvmem.obj has: NT's link32 kept the first
# definition, link 3.10 does it with -force:multiple (as for OSLOADER.EXE).
# (The variable of the command line is LNK: LINK.EXE takes the variable LINK
# as more options.)
klibs=
for l in $KLIBS; do klibs="$klibs %L%\\$l\\$l.lib"; done
{
	cat <<-'EOF'
	@echo off
	rem ntoslink.cmd -- made by build-kernel.sh: link NTOSKRNL.EXE and HAL.DLL for the R3000
	set TOP=T:\NTOS\3MAX
	set O=%TOP%\out\ntoskrnl
	set L=%TOP%\out
	set LNK=C:\MSDEV\BIN\LINK.EXE -nologo -MERGE:.CRT=.data -MERGE:_PAGE=PAGE -MERGE:_TEXT=.text -SECTION:INIT,d -OPT:REF -PDB:NONE -RELEASE -INCREMENTAL:NO -NODEFAULTLIB -IGNORE:4037,4065 -debug:notmapped -debugtype:coff -machine:mips -subsystem:native -miscrdata -force:multiple
	cd /d %O%
	EOF
	printf '%s\n' "%LNK% -out:ntoskrnl.exe -map:ntoskrnl.map -gpsize:32 -base:0x10000 -entry:KiSystemStartup %L%\\init\\ntoskrnl.res$klibs hal.lib ntoskrnl.exp %L%\\lsakrnlp\\lsakrnlp.lib %L%\\libcntpr\\libcntpr.lib > link.log 2>&1"
	printf '%s\n' "%LNK% -out:hal3max.dll -map:hal3max.map -base:0x80400000 -dll -entry:HalInitSystem hal3max.exp %L%\\hal3max\\*.obj %L%\\hal3max\\hal.res ntoskrnl.lib %L%\\libcntpr\\libcntpr.lib > linkhal.log 2>&1"
	printf '%s\n' 'copy hal3max.dll hal.dll > nul'
} | sed 's/$/\r/' > "$SHARE/build/ntoslink.cmd"

# The old outputs are removed from this side (see build-arcfw.sh).
mkdir -p "$out"
rm -f "$out/"*
vmrun 'T:\NTOS\3MAX\build\ntoskrnl.cmd'

# The import libraries, with R3000 thunks, go back to the share for the
# links (Docker gets local copies, see build-lib.sh).  The share's client
# can see the files the machine made a few seconds late.
i=0
while { [ ! -f "$out/hal.lib" ] || [ ! -f "$out/ntoskrnl.lib" ]; } && [ $i -lt 20 ]; do
	sleep 1; i=$((i + 1))
done
rm -rf "$here/obj/implib"
mkdir -p "$here/obj/implib"
cp "$out/hal.lib" "$out/ntoskrnl.lib" "$here/obj/implib/"
docker run --rm -v "$here":/work/3MAX -w /work/3MAX/obj/implib --entrypoint sh 3max-build \
	-c 'perl /work/3MAX/tools/r3kimplib.pl hal.lib hal3k.lib &&
	    perl /work/3MAX/tools/r3kimplib.pl ntoskrnl.lib ntos3k.lib'
cp "$here/obj/implib/hal3k.lib" "$out/hal.lib"
cp "$here/obj/implib/ntos3k.lib" "$out/ntoskrnl.lib"
vmrun 'T:\NTOS\3MAX\build\ntoslink.cmd'

i=0
while [ ! -f "$out/linkhal.log" ] && [ $i -lt 20 ]; do sleep 1; i=$((i + 1)); done
for f in def lib link linkhal; do
	[ ! -f "$out/$f.log" ] || tr -d '\r' < "$out/$f.log" | grep -v '^$' |
		grep -v 'LNK4006\|LNK4087\|LNK4088' | sed "s/^/$f: /" || true
done
[ -f "$out/ntoskrnl.exe" ] || { echo "build-kernel: no ntoskrnl.exe"; exit 1; }
[ -f "$out/hal.dll" ] || { echo "build-kernel: no hal.dll"; exit 1; }

mkdir -p "$here/obj"
cp "$out/ntoskrnl.exe" "$out/ntoskrnl.map" "$here/obj/"
cp "$out/hal.dll" "$here/obj/hal.dll"
cp "$out/hal3max.map" "$here/obj/hal.map"
docker run --rm -v "$here":/work/3MAX -w /work/3MAX --entrypoint sh 3max-build \
	-c 'perl tools/r3kcheck.pl obj/ntoskrnl.exe obj/hal.dll' > "$here/obj/kernel-r3k.txt" 2>&1 || true
grep -E '\.(exe|dll):' "$here/obj/kernel-r3k.txt" || cat "$here/obj/kernel-r3k.txt"
ls -l "$here/obj/ntoskrnl.exe" "$here/obj/hal.dll"
