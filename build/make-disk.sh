#!/bin/sh
#
#  make-disk.sh -- make or update the system disk of the DECstation,
#  obj/3max-disk.img (DISK=...), the disk run.sh boots ("boot 5/rz0"):
#
#	block 0			the DEC boot block (tools/mkdecboot.pl)
#	blocks 1..2015		the ARC firmware, obj/arcfw.bin (build-arcfw.sh)
#	blocks 2016..2047	the firmware's non-volatile configuration
#	partition 1, at 1 MB	the system partition, FAT16, with the OS loader
#				in \os\nt\osloader.exe (build-osloader.sh) and
#				the HAL beside it, \os\nt\hal.dll (the OS
#				loader takes the HAL from its own directory),
#				and the system in \winnt: the kernel in
#				system32 (build-kernel.sh), the drivers in
#				system32\drivers and the other images (the
#				DLLs, ntdll.dll first) in system32
#				(build-image.sh), the hives (system32\config:
#				system, the one the OS loader loads, software
#				and default, make-hives.sh) and, from the NT
#				tree, the NLS tables (system32: c_1252.nls,
#				c_437.nls and l_intl.nls, which the OS loader
#				loads, and those of the Win32 base) and the
#				fonts in \winnt\system (vgaoem.fon, the OEM
#				font, is also the font of the HAL: the OS
#				loader looks for it there)
#
#  A new disk (none yet, or FORCE=1) is DISK_MB (default 256) MB, with the
#  system partition formatted and the configuration not set yet (the
#  firmware asks for setup).  An existing disk keeps its configuration and
#  partitions: only the firmware and the files are written.
#
#  The system starts over at the first boot of the developer's first run:
#  the new hives replace every file of system32\config (sam and security
#  too, which lsass makes again), with \winnt\registry.ini (the overlay of
#  PUBLIC\OAK\BIN\REGISTRY.INI, which winlogon reads at the first boot),
#  its program groups in \winnt\idw and an empty \tmp; the winlogon.cmd of
#  an earlier first run goes.  KEEP=1 keeps the system where it is: only the
#  programs are written, the hives and the files of the first run are not.

set -e
here=$(cd "$(dirname "$0")/.." && pwd)	# 3MAX
nt35=$(cd "$here/.." && pwd)
disk=${DISK:-obj/3max-disk.img}

# (REGISTRY.INI is an edit script over the NT tree, vm.sh)
perl "$here/tools/ntdiff.pl" apply "$nt35" "$here/nt/PUBLIC/OAK/BIN"
DISK_MB=${DISK_MB:-256}

[ -f "$here/obj/arcfw.bin" ] && [ -f "$here/obj/arcfw.addr" ] ||
	{ echo "make-disk: no obj/arcfw.bin (build-arcfw.sh)" >&2; exit 1; }
for h in SYSTEM SOFTWARE DEFAULT; do
	[ -f "$here/obj/hives/$h" ] ||
		{ echo "make-disk: no obj/hives/$h (make-hives.sh)" >&2; exit 1; }
done
[ "$FORCE" = 1 ] && rm -f "$here/$disk"

docker run --rm -v "$here":/work/3MAX -v "$nt35/PRIVATE":/work/PRIVATE:ro \
	-v "$nt35/PUBLIC":/work/PUBLIC:ro -w /work/3MAX -e disk="$disk" -e mb="$DISK_MB" \
	-e keep="$KEEP" --entrypoint sh 3max-build -c '
set -e
new=0
[ -f "$disk" ] || new=1
perl tools/mkdecboot.pl obj/arcfw.bin $(cat obj/arcfw.addr) "$disk" $mb
export MTOOLS_SKIP_CHECK=1
p="$disk@@1048576"
if [ $new = 1 ]; then
	# Partition 1: from block 2048 to the end of the disk.
	mkfs.fat -F 16 -n NTSYSTEM -h 2048 --offset 2048 "$disk" $(( (mb * 2048 - 2048) / 2 )) > /dev/null
fi
mmd -D s -i "$p" ::/os ::/os/nt 2>/dev/null || true
if [ -f obj/osloader.exe ]; then
	mcopy -o -i "$p" obj/osloader.exe ::/os/nt/osloader.exe
fi
if [ -f obj/ntoskrnl.exe ] && [ -f obj/hal.dll ]; then
	mmd -D s -i "$p" ::/winnt ::/winnt/system ::/winnt/system32 \
		::/winnt/system32/config ::/winnt/system32/drivers 2>/dev/null || true
	mcopy -o -i "$p" obj/ntoskrnl.exe ::/winnt/system32/ntoskrnl.exe
	for f in obj/drivers/*.sys; do
		[ ! -f "$f" ] || mcopy -o -i "$p" "$f" ::/winnt/system32/drivers/$(basename "$f")
	done
	for f in obj/system32/*.dll obj/system32/*.drv obj/system32/*.exe obj/system32/*.cpl; do
		[ ! -f "$f" ] || mcopy -o -i "$p" "$f" ::/winnt/system32/$(basename "$f")
	done
	mcopy -o -i "$p" obj/hal.dll ::/os/nt/hal.dll
	mdel -i "$p" ::/winnt/system32/hal.dll 2>/dev/null || true
	if [ "$keep" != 1 ]; then
		# the new hives; the other files of config (the alternate
		# copies and logs of the hives, sam, security, the hives of the
		# users) belong to the old ones and go, the system makes them
		# again
		mdel -i "$p" "::/winnt/system32/config/*" 2>/dev/null || true
		for h in system software default; do
			mcopy -o -i "$p" obj/hives/$(echo $h | tr a-z A-Z) ::/winnt/system32/config/$h
		done
		# the first run: registry.ini, its program groups, \tmp
		mcopy -o -i "$p" nt/PUBLIC/OAK/BIN/REGISTRY.INI ::/winnt/registry.ini
		mmd -D s -i "$p" ::/winnt/idw ::/tmp 2>/dev/null || true
		for f in MAIN ACCESORI GAMES ADMIN; do
			mcopy -o -i "$p" /work/PRIVATE/WINDOWS/SHELL/PROGMAN/$f.GRP \
				::/winnt/idw/$(echo $f | tr A-Z a-z).grp
		done
		mdel -i "$p" ::/winnt/winlogon.cmd 2>/dev/null || true
	fi
	# the NLS tables (the OS loader loads c_1252, c_437 and l_intl with
	# the kernel)
	for f in C_1252 C_437 CTYPE L_INTL LOCALE SORTKEY SORTTBLS UNICODE; do
		mcopy -o -i "$p" /work/PRIVATE/WINDOWS/WINNLS/DATA/$f.NLS \
			::/winnt/system32/$(echo $f | tr A-Z a-z).nls
	done
	# the fonts Setup copies to \winnt\system (TXTSETUP.SIF directory 5):
	# the VGA resolution set the registry names (vgaoem.fon is also the
	# font of the HAL, which the OS loader loads), the TrueType fonts with
	# Lucida Console, the DOS window fonts
	F=/work/PRIVATE/WINDOWS/GDI/FONTS
	for f in VGASYS VGAFIX VGAOEM VGA850 VGA860 VGA861 VGA863 VGA865 SSERIFE SERIFE COURE SMALLE SMALLF \
	    SYMBOLE SYMBOLF ROMAN SCRIPT MODERN APP850 DOSAPP CGA40850 CGA40WOA CGA80850 CGA80WOA EGA40850 \
	    EGA40WOA EGA80850 EGA80WOA 8514FIX 8514OEM 8514SYS; do
		mcopy -o -i "$p" $F/$f.FON ::/winnt/system/$(echo $f | tr A-Z a-z).fon
	done
	for f in ARIAL ARIALBD ARIALBI ARIALI COUR COURBD COURBI COURI TIMES TIMESBD TIMESBI TIMESI SYMBOL \
	    WINGDING LUCON; do
		g=$(echo $f | tr A-Z a-z)
		mcopy -o -i "$p" $F/$f.TTF ::/winnt/system/$g.ttf
		mcopy -o -i "$p" $F/$f.FOT ::/winnt/system/$g.fot
	done
	mcopy -o -i "$p" $F/L_10646.TTF ::/winnt/system/l_10646.ttf
	mdel -i "$p" ::/winnt/system32/vgaoem.fon 2>/dev/null || true
	mdir -/ -i "$p" ::/winnt | grep -v "^ *$"
fi
mdir -/ -i "$p" ::/ | grep -v "^ *$"
'
