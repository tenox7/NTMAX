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
#				the HALs beside it, \os\nt\hal.dll of the
#				5000/200, hal3min.dll of the 5000/1xx and
#				hal3maxp.dll of the 5000/240 (the OS loader
#				takes the HAL from its own directory, the
#				firmware names it),
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
#				loader looks for it there), the help files
#				of the programs and the texts of the
#				performance counters (system32);
#				the tools of the SDK in \SDKTOOLS
#				(obj/sdktools: build-image.sh DEST=sdktools),
#				the games of apps/games in \GAMES and the
#				other programs of apps in \APPS
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
#  its program groups in \winnt\idw (the games of \GAMES in the Games
#  group, TenoxVNC, Internet Explorer and ntfetch of \APPS in Main:
#  tools/grp.pl) and an empty \tmp; the winlogon.cmd of an earlier first
#  run goes.  KEEP=1 keeps the system where it is: only the programs are
#  written, the hives and the files of the first run are not.

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
	for f in obj/system32/*.dll obj/system32/*.drv obj/system32/*.acm obj/system32/*.exe \
	    obj/system32/*.com obj/system32/*.cpl; do
		[ ! -f "$f" ] || mcopy -o -i "$p" "$f" ::/winnt/system32/$(basename "$f")
	done
	# the tools of the SDK, the games and the other programs of apps, each
	# folder with its DLLs
	for d in SDKTOOLS GAMES APPS; do
		l=obj/$(echo $d | tr A-Z a-z)
		ls $l 2>/dev/null | grep -q -i "\.\(exe\|dll\|com\)$" || continue
		mmd -D s -i "$p" ::/$d 2>/dev/null || true
		for f in $l/*.exe $l/*.dll $l/*.com; do
			[ ! -f "$f" ] || mcopy -o -i "$p" "$f" ::/$d/$(basename "$f")
		done
	done
	# the commands of SLM, copies of execslm.exe, which runs slm.exe with
	# its name (SLMNEW\SLM\MAKEFILE.INC), and the templates of the scanners
	# that flex writes (flex -Sc:\sdktools\skeleton.flx) and of the parsers
	# of yacc (on the INCLUDE path)
	if [ -f obj/sdktools/execslm.exe ]; then
		for n in addfile addproj catsrc defect delfile delproj enlist in log out scomp ssync status; do
			mcopy -o -i "$p" obj/sdktools/execslm.exe ::/SDKTOOLS/$n.exe
		done
	fi
	[ ! -f obj/sdktools/flex.exe ] ||
		mcopy -o -i "$p" /work/PRIVATE/SDKTOOLS/FLEX/SKELETON.FLX ::/SDKTOOLS/skeleton.flx
	[ ! -f obj/sdktools/yacc.exe ] ||
		mcopy -o -i "$p" /work/PRIVATE/SDKTOOLS/YACC/YYPARS.C ::/SDKTOOLS/yypars.c
	mcopy -o -i "$p" obj/hal.dll ::/os/nt/hal.dll
	for f in obj/hal3min.dll obj/hal3maxp.dll; do
		[ ! -f "$f" ] || mcopy -o -i "$p" "$f" ::/os/nt/$(basename "$f")
	done
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
			cp /work/PRIVATE/WINDOWS/SHELL/PROGMAN/$f.GRP /tmp/$f.GRP
		done
		# the games of \GAMES in the Games group, TenoxVNC, Internet
		# Explorer and ntfetch of \APPS in Main, each run from the system drive
		# (HOMEDRIVE, which winlogon sets to it for a user without a
		# home directory)
		grp() {
			g=$1 d=$2 c=$3; shift 3
			i=$IFS; IFS="|"; set -- $*; IFS=$i
			n=
			for x; do
				[ -f obj/$d/${x#*=}.exe ] && n="$n|$x"
			done
			i=$IFS; IFS="|"; set -- ${n#|}; IFS=$i
			[ $# = 0 ] || perl tools/grp.pl /tmp/$g.GRP obj/$d "%HOMEDRIVE%\\$(echo $d | tr a-z A-Z)" $c "$@"
		}
		grp GAMES games 5 "2048=2048|Breakout=breakout|Bubble Breaker=bubbles|Moon Lander=lander" \
			"Missile Command=misl|Snake=ntsnake|Tankz=tankz|NT Rigue=ntrigue|RISCque=riscque" \
			"Sokoban=sokoban|RISCoban=riscoban|WiNTown=wintown"
		grp MAIN apps 6 "TenoxVNC=tenoxvnc|Internet Explorer=iexplore|ntfetch=ntfetch"
		for f in MAIN ACCESORI GAMES ADMIN; do
			mcopy -o -i "$p" /tmp/$f.GRP ::/winnt/idw/$(echo $f | tr A-Z a-z).grp
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
	# the help files of the programs, where Setup puts them, and the names
	# and explanations of the performance counters (Perflib)
	W=/work/PRIVATE/WINDOWS
	for h in WINHELP/HLP/WINHELP SHELL/PROGMAN/PROGMAN SHELL/WINFILE/WINFILE SHELL/CONTROL/CONTROL \
	    SHELL/ACCESORY/NOTEPAD/NOTEPAD SHELL/ACCESORY/CALC/CALC SHELL/ACCESORY/SPECHARS/CHARMAP \
	    SHELL/GAMES/SOL/SOL EP/WINMINE/HELP/WINMINE EP/FREECELL/FREECELL SHELL/ACCESORY/CARDFILE/CARDFILE \
	    SHELL/ACCESORY/PAINTBRS/PBRUSH SHELL/ACCESORY/TERMINAL/TERMINAL MEDIA/MPLAYER2/HELP/MPLAYER; do
		mcopy -o -i "$p" $W/$h.HLP ::/winnt/system32/$(basename $h | tr A-Z a-z).hlp
	done
	mcopy -o -i "$p" /work/PRIVATE/UTILS/FDISK/DISKMAN.HLP ::/winnt/system32/windisk.hlp
	mcopy -o -i "$p" /work/PRIVATE/UTILS/REGEDIT/SRC/REGEDIT.HLP ::/winnt/system32/regedt32.hlp
	mcopy -o -i "$p" /work/PRIVATE/MVDM/BIN86/PIFEDIT.HLP ::/winnt/system32/pifedit.hlp
	# (the command list of help.exe, a text file: MSG_HELP_HELP_FILE_NAME)
	mcopy -o -i "$p" /work/PRIVATE/UTILS/HELP/DOSHELP.HLP ::/winnt/system32/doshelp.hlp
	for f in PERFC009 PERFH009; do
		mcopy -o -i "$p" /work/PRIVATE/SDKTOOLS/CNTRTEXT/PERFINI/$f.DAT \
			::/winnt/system32/$(echo $f | tr A-Z a-z).dat
	done
	mdir -/ -i "$p" ::/winnt | grep -v "^ *$"
fi
mdir -/ -i "$p" ::/ | grep -v "^ *$"
'
