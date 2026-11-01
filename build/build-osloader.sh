#!/bin/sh
#
#  build-osloader.sh -- link the genuine OS loader, OSLOADER.EXE, for the
#  R3000 on the MIPS NT machine (vm.sh) the way BOOT\BLDR\MIPSLDR.RSP links
#  it, from the libraries build-lib.sh and build-crt.sh made:
#
#	PRIVATE/NTOS/BOOT/BLDR	bldr.lib and osloader.res
#	PRIVATE/NTOS/BOOT/LIB	boot.lib
#	PRIVATE/NTOS/KE/UP	ke.lib (KeFindConfigurationEntry, KiFindFirstSet*)
#	PRIVATE/NTOS/RTL/BOOT	bldrrtl.lib
#	PRIVATE/NTOS/CONFIG/BOOT bconfig.lib
#	build-crt.sh		libcntpr.lib
#
#  and check the image with r3kcheck.pl.  Output: obj/osloader.exe and
#  obj/osloader.map (make-disk.sh puts the loader in \os\nt on the system
#  partition).  Log: T:\NTOS\3MAX\out\osloader\link.log.

set -e
. "$(dirname "$0")/vm.sh"
out=$SHARE/out/osloader
cmd=$SHARE/build/osloader.cmd

for l in bldr/bldr.lib bldr/osloader.res boot/boot.lib ke/ke.lib bldrrtl/bldrrtl.lib \
	libcntpr/libcntpr.lib bconfig/bconfig.lib; do
	[ -f "$SHARE/out/$l" ] || { echo "build-osloader: no out/$l (build-lib.sh, build-crt.sh)" >&2; exit 1; }
done

# libcntpr's memorym.obj (memcmp) defines memcpy, memmove and memset again,
# which bldrrtl's xxmvmem.obj has: NT's link32 kept the first definition,
# link 3.10 does it with -force:multiple (as NT's PowerPC builds link).
{
	cat <<-'EOF'
	@echo off
	rem osloader.cmd -- made by build-osloader.sh: link OSLOADER.EXE for the R3000
	set TOP=T:\NTOS\3MAX
	set O=%TOP%\out\osloader
	set L=%TOP%\out
	cd /d %O%
	C:\MSDEV\BIN\LINK.EXE -nologo -machine:mips -debug:notmapped -debugtype:coff -rom -fixed -map:osloader.map -align:0x200 -base:0x80600000,0x80660000 -entry:BlOsLoader -nodefaultlib -force:multiple -out:osloader.exe %L%\bldr\bldr.lib %L%\bldr\osloader.res %L%\boot\boot.lib %L%\ke\ke.lib %L%\bldrrtl\bldrrtl.lib %L%\libcntpr\libcntpr.lib %L%\bconfig\bconfig.lib > link.log 2>&1
	EOF
} | sed 's/$/\r/' > "$cmd"

# The old outputs are removed from this side (see build-arcfw.sh).
mkdir -p "$out"
rm -f "$out/"*
vmrun 'T:\NTOS\3MAX\build\osloader.cmd'
tr -d '\r' < "$out/link.log" | grep -v 'LNK4006\|LNK4088' || true
[ -f "$out/osloader.exe" ] || { echo "build-osloader: no osloader.exe"; exit 1; }

mkdir -p "$here/obj"
cp "$out/osloader.exe" "$out/osloader.map" "$here/obj/"
docker run --rm -v "$here":/work/3MAX -w /work/3MAX --entrypoint sh 3max-build \
	-c 'perl tools/r3kcheck.pl obj/osloader.exe' > "$here/obj/osloader-r3k.txt" 2>&1 || true
grep -E '\.exe:' "$here/obj/osloader-r3k.txt" || cat "$here/obj/osloader-r3k.txt"
ls -l "$here/obj/osloader.exe"
