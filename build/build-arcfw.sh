#!/bin/sh
#
#  build-arcfw.sh -- build the DECstation ARC firmware (arcfw/, with the HAL
#  modules of hal/ it links, as the Jazz firmware links the HAL's jxtime) with
#  C900 /QMR3000 on the MIPS NT machine (vm.sh), link it at FW_BASE_ADDRESS
#  with the boot library and its runtime (build-lib.sh PRIVATE/NTOS/BOOT/LIB
#  and PRIVATE/NTOS/RTL/BOOT: the file systems, as the Jazz firmware links
#  the loader's) and libcntpr.lib (build-crt.sh), check every object with
#  r3kcheck.pl, and lay it out for the boot block.  The firmware runs on
#  the 5000/200 (KN02), the 5000/1xx (KN02-BA) and the 5000/240 (KN03): it
#  is compiled without a system module define, and dxsystem.c, which
#  describes a module from decdef.h, once for each module (SYSTEMS):
#
#	obj/arcfw.exe		the linked firmware (and arcfw.map)
#	obj/arcfw.bin		the image as it sits in memory from 0x80030000
#	obj/arcfw.addr		its load and start addresses (hex)
#
#  make-disk.sh puts it on the system disk.
#
#  Logs: T:\NTOS\3MAX\out\arcfw\*.log.

set -e
. "$(dirname "$0")/vm.sh"
cmd=$SHARE/build/arcfw.cmd

# The firmware sources: C and assembler in arcfw/, the HAL modules in hal/.
FW_C="dxboot dxdisk dxinit dxnvram dxserial fwentry fwio fwload fwprint fwtime fwusa
	jxboot jxconfig jxenvir jxmemory jxreboot jxsysid jxvendor jzboot jzcommon
	jzconfig jzenvir jzgetpar jzmake jzsetup jzstubs jztime jzusa monitor stubs"
FW_S="d3start d3cache fwtrap xxirql"
HAL_C="d3time"

# The system modules: the object of dxsystem.c and the define of each.
SYSTEMS="dxkn02:KN02_AA dxkn02ba:KN02_BA dxkn03:KN03_AA"
SYSTEM_OBJ=$(for m in $SYSTEMS; do printf '%s ' "${m%%:*}"; done)

ntcopy PRIVATE/NTOS/FW/MIPS PRIVATE/NTOS/FW/DUOBASE/MIPS PRIVATE/NTOS/BOOT/INC PRIVATE/NTOS/BOOT/LIB \
	PRIVATE/NTOS/FASTFAT PRIVATE/NTOS/INC PRIVATE/INC \
	PUBLIC/SDK/INC PUBLIC/OAK/INC
mkdir -p "$SHARE/build" "$SHARE/arcfw" "$SHARE/hal"
rsync -a --inplace --delete --exclude='*.ntdiff' "$here/arcfw/" "$SHARE/arcfw/"
rsync -a --inplace --delete --exclude='*.ntdiff' "$here/hal/" "$SHARE/hal/"

{
	cat <<-'EOF'
	@echo off
	rem arcfw.cmd -- made by build-arcfw.sh: compile the DECstation ARC
	rem firmware for the R3000 and link it in T:\NTOS\3MAX\out\arcfw
	set TOP=T:\NTOS\3MAX
	set NT=%TOP%\NT
	set O=%TOP%\out\arcfw
	set PATH=%TOP%\C900\BIN;%SystemRoot%\system32
	set INCLUDE=
	set DEFS=-DMIPS=1 -D_MIPS_=1 -DMIPSEL -DNO_EXT_KEYS -DCONDITION_HANDLING=1 -DWIN32_LEAN_AND_MEAN=1 -DNT_UP=1 -DNT_INST=0 -DWIN32=100 -D_NT1X_=100 -DR3000 -D_M_MRX000=3000 /QMR3000 -DDBG=0 -DDEVL=1 -D_NTSYSTEM_ -DDECSTATION
	set CFLAGS=-nologo -c /Zel /Zp8 /Gy /W3 -Ox -Gt0 -D__stdcall= -D__cdecl= -D_LANGUAGE_C -DLANGUAGE_C
	set AFLAGS=-nologo -c /Zel /Zp8 /Gy /W3 -Gt0 -Ge -O -D_LANGUAGE_ASSEMBLY
	set INC=-I. -I%TOP%\arcfw -I%TOP%\hal -I%NT%\PRIVATE\NTOS\FW\MIPS -I%NT%\PRIVATE\NTOS\BOOT\INC -I%NT%\PRIVATE\NTOS\BOOT\LIB -I%NT%\PRIVATE\NTOS\FW\DUOBASE\MIPS -I%NT%\PRIVATE\NTOS\FASTFAT -I%NT%\PRIVATE\NTOS\INC -I%NT%\PRIVATE\INC -I%NT%\GEN -I%NT%\PUBLIC\SDK\INC -I%NT%\PUBLIC\SDK\INC\CRT -I%NT%\PUBLIC\OAK\INC
	if not exist %O% mkdir %O%
	del /q %O%\*.* > nul 2>&1
	EOF
	printf '%s\n' 'cd /d %TOP%\arcfw'
	for f in $FW_C; do
		printf '%s\n' "cl %CFLAGS% %INC% %DEFS% -Fo%O%\\$f.obj $f.c > %O%\\$f.log 2>&1"
	done
	for m in $SYSTEMS; do
		printf '%s\n' "cl %CFLAGS% %INC% %DEFS% -D${m#*:} -Fo%O%\\${m%%:*}.obj dxsystem.c > %O%\\${m%%:*}.log 2>&1"
	done
	for f in $FW_S; do
		printf '%s\n' "cl %AFLAGS% %INC% %DEFS% -Fo%O%\\$f.obj $f.s > %O%\\$f.log 2>&1"
	done
	# HAL modules linked into the firmware call the kernel routines directly.
	printf '%s\n' 'cd /d %TOP%\hal'
	for f in $HAL_C; do
		printf '%s\n' "cl %CFLAGS% %INC% %DEFS% -D_BLDR_ -Fo%O%\\$f.obj $f.c > %O%\\$f.log 2>&1"
	done
	# libcntpr's memorym.obj (memcmp) defines memcpy, memmove and memset
	# again, which bldrrtl's xxmvmem.obj has: NT's link32 kept the first
	# definition, link 3.10 does it with -force:multiple (as NT's PowerPC
	# builds link).
	printf '%s\n' 'cd /d %O%'
	printf '%s' '%MSDevDir%\BIN\LINK.EXE -nologo -machine:mips -fixed -align:0x200 -base:0x80030000 -force:multiple'
	printf '%s' ' -entry:FwStart -subsystem:native -nodefaultlib -debug:none -map:arcfw.map -out:arcfw.exe'
	for f in $FW_S $FW_C $SYSTEM_OBJ $HAL_C; do printf ' %s.obj' $f; done
	printf '%s' ' %TOP%\out\boot\boot.lib %TOP%\out\bldrrtl\bldrrtl.lib'
	printf '%s\n' ' %TOP%\out\libcntpr\libcntpr.lib > link.log 2>&1'
} | sed 's/$/\r/' > "$cmd"

# The old outputs are removed from this side, not by del on the MIPS
# machine: the SMB client renames a file someone still has open (to
# .smbdelete*, removed when it is closed), so its name is free.
mkdir -p "$SHARE/out/arcfw"
rm -f "$SHARE/out/arcfw/"*
vmrun 'T:\NTOS\3MAX\build\arcfw.cmd'

# Report the sources that did not compile, the link, and check the objects.
fail=0
for f in $FW_C $SYSTEM_OBJ $FW_S $HAL_C; do
	if [ ! -f "$SHARE/out/arcfw/$f.obj" ]; then
		echo "--- $f failed:"; tr -d '\r' < "$SHARE/out/arcfw/$f.log" | grep -v '^$' | head -30
		fail=$((fail + 1))
	else
		tr -d '\r' < "$SHARE/out/arcfw/$f.log" | grep -i 'warning\|error' | sed "s/^/$f: /" | head -10
	fi
done
[ $fail -eq 0 ] || { echo "build-arcfw: $fail failed"; exit 1; }
echo "--- link:"; tr -d '\r' < "$SHARE/out/arcfw/link.log"
[ -f "$SHARE/out/arcfw/arcfw.exe" ] || { echo "build-arcfw: no arcfw.exe"; exit 1; }

# Docker gets copies of the objects: the files of the share it reads stay
# open in the SMB client for a while, and the next build cannot replace them.
mkdir -p "$here/obj"
rm -rf "$here/obj/arcfw.o"; mkdir "$here/obj/arcfw.o"
cp "$SHARE/out/arcfw/"*.obj "$here/obj/arcfw.o/"
cp "$SHARE/out/arcfw/arcfw.exe" "$SHARE/out/arcfw/arcfw.map" "$here/obj/"
docker run --rm -v "$here":/work/3MAX -w /work/3MAX --entrypoint sh 3max-build -c '
set -e
perl tools/r3kcheck.pl obj/arcfw.o/*.obj obj/arcfw.exe > obj/arcfw-r3k.txt 2>&1 || true
set -- $(perl tools/pe2bin.pl obj/arcfw.exe obj/arcfw.bin)
echo $1 $2 > obj/arcfw.addr
mipsel-linux-gnu-objdump -b binary -m mips:3000 -EL --adjust-vma=0x$1 -D obj/arcfw.bin > obj/arcfw.lst
'
grep -E '\.(obj|exe):' "$here/obj/arcfw-r3k.txt" | grep -vE ': R3000, [0-9]+ instructions: clean$' || true
ls -l "$here/obj/arcfw.bin"
