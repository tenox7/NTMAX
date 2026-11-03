#!/bin/sh
#
#  build-all.sh -- build the whole system from the NT tree: the build
#  scripts in turn, in the order that their inputs need:
#
#	build-all.sh [-n] [FIRST]
#
#	1  the tools of the MIPS NT machine (dbgrun, the tree's tools, MIDL),
#	   the header pass and the MIDL pass 0 (the RPC interfaces of the
#	   Win32 core, the tables of USER, NET)
#	2  the C run-times: libcntpr.lib, libc.lib, libcmt.lib and the
#	   libraries of crtdll
#	3  the firmware, the OS loader, the kernel and the HAL
#	4  the drivers
#	5  the libraries that the user-mode images link
#	6  the user-mode images; where two of them import from each other,
#	   the import library of one comes first (IMPLIB=1 build-image.sh:
#	   advapi32 for rpcrt4, gdi32 for user32, winspool for gdi32,
#	   netapi32 for NetRap, lsasrv for samsrv, fteng32 for ftui32)
#	7  the programs of apps/: the task manager, the games (obj/games),
#	   wsock32, which TenoxVNC and Internet Explorer import, and the
#	   programs of obj/apps
#	8  the tools of PRIVATE\SDKTOOLS that build for the R3000
#	   (obj/sdktools)
#	9  the hives and the system disk: make-disk.sh reads KEEP, DISK and
#	   FORCE as usual; without KEEP=1 the system on the disk starts over
#	   at the first run
#
#  The output of step N goes to obj/build-all/N.log, its result lines to
#  the terminal.  The run stops at the first step that fails, and
#  build-all.sh FIRST goes on from step FIRST.  -n lists the steps.  The
#  Docker image 3max-build is made from build/Dockerfile if it is missing.
#  Not built here: gxemul (build-gxemul.sh), and the NT programs that
#  make-hives.sh takes from the ARM port (RCPP, REGINI, HIVEDMP).

set -e
here=$(cd "$(dirname "$0")/.." && pwd)	# 3MAX
unset IMPLIB
LIST=
[ "$1" != -n ] || { LIST=1; shift; }
FIRST=${1:-1}
log=$here/obj/build-all
n=0

# step [VAR=VALUE...] SCRIPT [ARG...]: the next step, a script of build/
# with the variables VAR (DEST=games build-image.sh DIR); "implib DIR" is
# IMPLIB=1 build-image.sh DIR
step() {
	n=$((n + 1))
	[ -z "$LIST" ] || { echo "$n $*"; return 0; }
	[ "$n" -ge "$FIRST" ] || return 0
	echo "build-all: $n ($(date +%H:%M)) $*"
	f=$log/$n.log
	e=
	while :; do
		case $1 in *=*) e="$e $1"; shift ;; *) break ;; esac
	done
	case $1 in
	implib) shift; set -- env $e IMPLIB=1 sh "$here/build/build-image.sh" "$@" ;;
	*) s=$1; shift; set -- env $e sh "$here/build/$s" "$@" ;;
	esac
	# (rsh reads its standard input: not the rest of this script's; a
	# library some of whose sources failed is a failed step, as an image is)
	if "$@" < /dev/null > "$f" 2>&1 && ! grep -q '^build-lib: [1-9][0-9]* failed' "$f"; then
		grep -E '^(build-[a-z0-9]+|make-[a-z]+): |instructions: ' "$f" | sed 's/^/	/' || true
	else
		tail -30 "$f"
		echo "build-all: step $n failed (obj/build-all/$n.log); go on with: build/build-all.sh $n"
		exit 1
	fi
}

# images DIR...: build-image.sh of each PRIVATE/DIR
images() {
	for d; do step build-image.sh "PRIVATE/$d"; done
}

if [ -z "$LIST" ]; then
	docker image inspect 3max-build > /dev/null 2>&1 || docker build -t 3max-build "$here/build"
	mkdir -p "$log"
	[ "$FIRST" != 1 ] || rm -f "$log"/*.log
fi

# 1. the tools of the build machine, the header pass, the MIDL pass 0
step build-dbgrun.sh
step build-tools.sh
step build-gen.sh
step build-midl.sh
step build-midl0.sh
step build-midl0.sh PRIVATE/WINDOWS/USER
step build-midl0.sh PRIVATE/NET PRIVATE/NET/SVCDLLS/SRVSVC PRIVATE/NET/SVCDLLS/WKSSVC \
	PRIVATE/NET/SVCDLLS/BROWSER PRIVATE/NET/SVCDLLS/REPL PRIVATE/NET/SVCDLLS/MSGSVC \
	PRIVATE/NET/SVCDLLS/AT PRIVATE/NET/SVCDLLS/LOGONSRV PRIVATE/NET/SVCDLLS/RPL

# 2. the C run-times
step build-crt.sh
step build-crt32.sh ST
step build-crt32.sh MT
step build-crt32.sh DLL

# 3. the firmware (the boot library), the OS loader, the kernel and the HALs
# (of the 5000/200, the 5000/1xx and the 5000/240)
for d in RTL/BOOT BOOT/LIB BOOT/BLDR CONFIG/BOOT; do
	step build-lib.sh PRIVATE/NTOS/$d
done
step build-arcfw.sh
for d in KE MM EX IO CONFIG SE PS RTL FSRTL CACHE OB RAW KD INIT LPC VDM DBGK; do
	step build-lib.sh PRIVATE/NTOS/$d/UP
done
step build-lib.sh PRIVATE/LSA/CLIENT/KERNEL
step build-osloader.sh
for d in HAL3MAX HAL3MIN HAL3MAXP; do
	step build-lib.sh PRIVATE/NTOS/NTHALS/$d
done
step build-kernel.sh

# 4. the drivers (scsidisk and scsicdrm link the class library and, as
# ncr53c9x does, scsiport's import library; pmagba videoprt's; the tape
# drivers the library of SCSITAPE\CLASS)
step build-lib.sh PRIVATE/NTOS/DD/CLASS
images NTOS/DD/SCSIPORT NTOS/DD/SCSIDISK NTOS/MINIPORT/NCR53C94 NTOS/FASTFAT NTOS/NPFS \
	NTOS/MAILSLOT NTOS/VIDEO/PORT NTOS/VIDEO/PMAGBA NTOS/DD/DZPORT NTOS/DD/SCCPORT \
	NTOS/DD/DZSERIAL \
	NTOS/DD/LK201 NTOS/DD/KBDCLASS NTOS/DD/VSXXX NTOS/DD/MOUCLASS NTOS/DD/SCSICDRM NTOS/CDFS
step build-lib.sh PRIVATE/NTOS/DD/SCSITAPE/CLASS
images NTOS/DD/SCSITAPE/4MMDAT NTOS/DD/SCSITAPE/DLTTAPE

# 5. the libraries of the user-mode images (the client libraries of SC and
# WINREG have the names of the server ones: advapi32 takes them from their
# own directories; NETDDE\INCS makes the message header of the toolkit)
for d in NTOS/RTL/USER URTL SM/CLIENT WINDOWS/BASE/RTL WINDOWS/WINNLS WINDOWS/WINCON/CLIENT \
	RPC/NDRLIB RPC/NDRMEM RPC/NDR20 RPCUTIL LSA/COMMON LSA/UCLIENT LSA/CRYPT/ENGINE \
	LSA/CRYPT/DLL EVENTLOG/ELFCLNT WINDOWS/SCREG/SC/LIB WINDOWS/SCREG/SC/CLIENT \
	WINDOWS/SCREG/WINREG/CLIENT WINDOWS/SCREG/WINREG/LOCAL WINDOWS/SCREG/WINREG/PERFLIB \
	WINDOWS/SCREG/WINREG/LIB WINDOWS/GDI/MATH WINDOWS/PRSINF WINDOWS/WINCON/SERVER/DAYTONA \
	WINDOWS/GDI/FONDRV/TT/SCALER WINDOWS/GDI/FONDRV/TT/TTFD WINDOWS/GDI/FONDRV/BMFD \
	WINDOWS/GDI/FONDRV/VTFD WINDOWS/GDI/HALFTONE/HT WINDOWS/GDI/GRE WINDOWS/USER/SERVER \
	NET/NETLIB NET/SVCDLLS/SRVSVC/CLIENT NET/SVCDLLS/SRVSVC/LIB NET/SVCDLLS/WKSSVC/CLIENT \
	NET/SVCDLLS/BROWSER/CLIENT NET/SVCDLLS/BROWSER/COMMON NET/SVCDLLS/LOGONSRV/CLIENT \
	NET/SVCDLLS/REPL/COMMON NET/SVCDLLS/REPL/CLIENT NET/RPCXLATE/RXCOMMON NET/RPCXLATE/RXAPI \
	NET/NETBIOS NET/SVCCTRL/CLIENT NET/SVCDLLS/MSGSVC/CLIENT NET/SVCDLLS/AT/CLIENT \
	NET/SVCDLLS/RPL/CLIENT NET/ACCESS LSA/CLIENT/USER WINDOWS/SCREG/WINREG/SERVER NLSECUTL \
	WINDOWS/SCREG/SC/SVCSLIB WINDOWS/SHELL/USERPRI WINDOWS/PORT1632 SDKTOOLS/ZTOOLS/SRC \
	WINDOWS/DIAMOND/MSZIP/MDI.NT WINDOWS/DIAMOND/CHUCK/FDI.NT WINDOWS/SHELL/LZ/LIBS \
	WINDOWS/NETDDE/INCS WINDOWS/NETDDE/SRC/TOOLKIT WINDOWS/MEDIA/MMCNTRLS \
	WINDOWS/MEDIA/AVI/DRAWDIB WINDOWS/MEDIA/AVI/COMPMAN WINDOWS/MEDIA/AVI/MCIWND; do
	step build-lib.sh PRIVATE/$d
done

# 6. the user-mode images
images NTOS/DLL/DAYTONA WINDOWS/BASE/CLIENT/DAYTONA
step build-crtdll.sh
images CSR/SERVER WINDOWS/BASE/SERVER SM/SERVER
step implib PRIVATE/WINDOWS/BASE/ADVAPI
images RPC/RUNTIME/MTRT WINDOWS/BASE/ADVAPI
step implib PRIVATE/WINDOWS/GDI/CLIENT
images WINDOWS/USER/CLIENT
step implib PRIVATE/WINDOWS/SPOOLER/SPOOLSS/CLIENT
images WINDOWS/GDI/CLIENT WINDOWS/SPOOLER/SPOOLSS/CLIENT WINDOWS/WINSRV/DAYTONA \
	WINDOWS/GDI/DISPLAYS/FRAMEBUF WINDOWS/USER/KBDLYOUT/US RPC/RUNTIME/TRANS/WIN32/CLNTNP \
	RPC/RUNTIME/TRANS/WIN32/SVRNP NEWSAM/CLIENT
step implib PRIVATE/NET/API
images NET/RAP NET/API
step implib PRIVATE/LSA/SERVER
images NEWSAM/SERVER LSA/SERVER LSA/MSV1_0 LSA/MSPRIVS WINDOWS/SCREG/SC/SERVER/DAYTONA \
	EVENTLOG/SERVER EVENTLOG/NETEVENT NTOS/DD/NLSMSG NET/MESSAGES WINDOWS/USER/WINLOGON/DAYTONA \
	WINDOWS/SHELL/LIBRARY WINDOWS/MPR WINDOWS/USER/USERINIT WINDOWS/CMD SDKTOOLS/INI \
	SDKTOOLS/ECH SDKTOOLS/ADDUSER SDKTOOLS/SHUTDOWN SDKTOOLS/SLEEP WINDOWS/SHELL/LZ/LZEXPAND \
	WINDOWS/SHELL/VERSION WINDOWS/SHELL/COMDLG WINDOWS/SHELL/CONTROL/T1INSTAL \
	WINDOWS/SHELL/CONTROL/MAIN WINDOWS/SHELL/CONTROL/CPANEL WINDOWS/SHELL/PROGMAN \
	WINDOWS/SETUP/SETUPDLL WINDOWS/SHELL/MSCTLS WINDOWS/SHELL/ACCESORY/NOTEPAD \
	WINDOWS/SHELL/ACCESORY/CALC WINDOWS/SHELL/ACCESORY/CLOCK WINDOWS/SHELL/GAMES/SOL \
	WINDOWS/EP/WINMINE WINDOWS/SHELL/WINFILE WINDOWS/EP/CARDDLL WINDOWS/EP/FREECELL \
	WINDOWS/SHELL/ACCESORY/SPECHARS WINDOWS/OLE/CLIENT WINDOWS/OLE/SERVER \
	WINDOWS/SHELL/ACCESORY/CARDFILE WINDOWS/SHELL/ACCESORY/PAINTBRS \
	WINDOWS/SHELL/ACCESORY/TERMINAL WINDOWS/WINHELP/MVAPI
step implib PRIVATE/WINDOWS/WINHELP/FTENGINE
images WINDOWS/WINHELP/FTUI WINDOWS/WINHELP/FTENGINE WINDOWS/WINHELP/WINHELP SDKTOOLS/PERFMON \
	MVDM/VDMUTILS/PIFEDIT
# (the codecs of MSACM are .acm files; msacm32.drv, the wave mapper, comes
# after msacm32.dll, the import library of which has the same name)
images WINDOWS/MEDIA/WINMM WINDOWS/MEDIA/MMDRV WINDOWS/MEDIA/MCIWAVE WINDOWS/MEDIA/MCISEQ \
	WINDOWS/MEDIA/MCICDA WINDOWS/MEDIA/SYNTH WINDOWS/MEDIA/MSACM/MSACM \
	WINDOWS/MEDIA/MSACM/IMAADPCM WINDOWS/MEDIA/MSACM/MSADPCM WINDOWS/MEDIA/MSACM/GSM610 \
	WINDOWS/MEDIA/MSACM/MSACMMAP WINDOWS/MEDIA/MCIOLE WINDOWS/MEDIA/SNDVOL \
	WINDOWS/MEDIA/CDPLAYER WINDOWS/MEDIA/AVI/VIDEO WINDOWS/MEDIA/AVI/MSVIDC \
	WINDOWS/MEDIA/AVI/MSRLE WINDOWS/MEDIA/AVI/MCIAVI32 WINDOWS/MEDIA/MPLAYER2
# (the utilities: the file system DLLs that fmifs, Disk Administrator,
# Backup and the command-line tools link; DISKREG and WINTOOLS\LIB are
# libraries of windisk, ntbackup and winmsd; Windows NT Diagnostics also
# imports its 3D controls, the winmsd.dll of WINTOOLS\CTL3D\DLL)
images UTILS/ULIB/SRC UTILS/UREG/SRC UTILS/IFSUTIL/SRC UTILS/UFAT/SRC UTILS/UHPFS/SRC \
	UTILS/UNTFS/SRC UTILS/CUFAT/SRC UTILS/CUHPFS UTILS/FMIFS/SRC
step build-lib.sh PRIVATE/NTOS/DISKREG
images UTILS/FDISK
step build-lib.sh PRIVATE/SDKTOOLS/WINTOOLS/LIB
images SDKTOOLS/WINTOOLS/CLB UTILS/NTBACKUP/SRC UTILS/ATTRIB UTILS/CHCP UTILS/CHKDSK UTILS/COMP \
	UTILS/DOSKEY UTILS/FC UTILS/FIND UTILS/FINDSTR UTILS/HELP UTILS/LABEL UTILS/FORMAT \
	UTILS/MODE UTILS/MORE UTILS/RECOVER UTILS/REPLACE UTILS/RESTORE/SRC UTILS/SORT \
	UTILS/SUBST UTILS/TREE UTILS/XCOPY
images SDKTOOLS/WINTOOLS/CTL3D/DLL SDKTOOLS/WINTOOLS/WINMSD
# (the NET UI programs, Event Viewer and User Manager: the libraries of the
# NET UI toolkit, each PCH directory before its library, then its DLLs;
# acledit, ntlanman and mprui; the libraries of the admin tools; regedt32
# imports ntlanman)
for d in STRING/PCH STRING/STRING COLLECT/PCH COLLECT/COLLECT MISC/FMX MISC/BUFFER MISC/MISC \
	MISC/TRACE MISC/WINTIME MISC/SYSTEM MISC/FSENUM MISC/HEAP MISC/BASE MISC/ERRMAP BLT/PCH \
	BLT/BLT BLT/BLTCC APPLIB/PCH APPLIB/APPLIB MNET32/PCH MNET32/MNET32 MNET32/NETSLOW \
	LMOBJ/PCH LMOBJ/LMOENUM LMOBJ/LMOBJ LMOBJ/LOG PROFILE/PROFILE REG/REG MISC/BIN APPLIB/BIN \
	LMOBJ/BIN; do
	step build-lib.sh PRIVATE/NET/UI/COMMON/SRC/$d
done
images NET/UI/COMMON/SRC/DLL3/DLL0 NET/UI/COMMON/SRC/DLL3/DLL1 NET/UI/COMMON/SRC/DLL3/DLL2
for d in ACLEDIT/ACLEDIT ACLEDIT/XLATE SHELL/SHELL SHELL/UTIL SHELL/ENUM SHELLUI/SHARE \
	SHELLUI/FILE SHELLUI/MISC SHELLUI/XLATE MPR/XLATE; do
	step build-lib.sh PRIVATE/NET/UI/$d
done
images NET/UI/ACLEDIT/BIN NET/UI/SHELL/BIN NET/UI/SHELLUI/BIN NET/UI/MPR/MPR
for d in COMMON/SRC/ADMINAPP/SETFOCUS COMMON/SRC/ADMINAPP/ADMINAPP COMMON/SRC/ADMINAPP/COLWIDTH \
	COMMON/SRC/ADMINAPP/BIN EVENTVWR/XLATE USER/XLATE; do
	step build-lib.sh PRIVATE/NET/UI/ADMIN/$d
done
images NET/UI/ADMIN/EVENTVWR/EVENTVWR NET/UI/ADMIN/USER/USER NET/UI/ADMIN/USER/MINIUSER \
	UTILS/REGEDIT/SRC

# 7. the programs of apps/ (APPS/DIR): the task manager, in system32; the
# games, in \GAMES; wsock32 (the libraries of SOCKETS, then the DLL); the
# libraries of Internet Explorer, then the programs of \APPS
step build-image.sh APPS/taskman
for d in 2048 breakout bubbles lander missile ntrigue riscoban riscque snake sokoban tankz wintown; do
	step DEST=games build-image.sh APPS/games/$d
done
for d in LIBUEMUL SOCKREG SOCKUTIL; do
	step build-lib.sh PRIVATE/NET/SOCKETS/$d
done
images NET/SOCKETS/WINSOCK
for d in shared win32 bearssl basic; do
	step build-lib.sh APPS/ie/$d
done
for d in ie/make tenoxvnc/win32 ntfetch; do
	step DEST=apps build-image.sh APPS/$d
done

# 8. the tools of PRIVATE\SDKTOOLS that build for the R3000, in \SDKTOOLS:
# the libraries and DLLs that other tools link (RLTOOLS\COMMON in
# out\rlcommon, as the common.lib of SHELL\ACCESORY\COMMON is in out\common),
# then the tools; the program of RANDOM is a test program of UMTEST
sdk() {
	for d; do step DEST=sdktools build-image.sh "PRIVATE/SDKTOOLS/$d"; done
}
for d in RESTOOLS/UNICODE TLIBS/NB3LIB/SRC WZMAIL/DH.NEW/LIB WINDIFF/CLIENT VCTOOLS/PDB/DBI; do
	step build-lib.sh PRIVATE/SDKTOOLS/$d
done
step OUT=rlcommon build-lib.sh PRIVATE/SDKTOOLS/RESTOOLS/RLTOOLS/COMMON
sdk IMAGEHLP SECEDIT/DLL TOPDESK/DLL RESMON VCTOOLS/RCDLL GUTILS SPY/DLL FASTIMER PSAPI WAP/FERNEL \
	WAP/BASE WAP/ADVAPI WAP/CRTDLL ALIAS CAT CHECKREL CHMODE CMDEVENT CRASHDMP CP DELNODE DIFF \
	DISKMON DU ECHOTIME EXP FCOM GETDBG HEAD INFILTER LOC LS MUNGE MV PARCOMP PERFMTR PSTAT \
	PROFILE RATHOLE RESCAN REGINI RM STARTPGM STRINGS SUCK SYNC TAIL TC TIMER TOUCH TRANS UNDEL \
	UPD UPDRN VDMPERF WALK WC WHERE WPERF DSKBENCH YNC BUILD RESTOOLS/RLTOOLS/RLMAN \
	RESTOOLS/RLTOOLS/RLQUIKED RESTOOLS/RLTOOLS/RLADMIN RESTOOLS/RLTOOLS/RLEDIT \
	RESTOOLS/RESONEXE RESTOOLS/TCT GENSRV HE LIST UNITS MAPMSG QGREP PPR RCPP HD UIXPORT VADUMP \
	HEXTRACT HSPLIT WCSHDR PVIEW TEE WS ZWAPI WINOBJ DDESPY REGTOGRP REMOTE CNTRTEXT/LODCTR \
	CNTRTEXT/UNLODCTR CNTRTEXT/CTRLIST LISTMUNG TZ TOPDESK/APP DISKPERF BIN2C NETJOIN MTTF/MAIN \
	MTTF/SETUP MTTF/VWR DUMPGRP TLIST MAKEANI COMPACT REGDIFF CACLS BUILDNUM AUTOWRAP \
	VCTOOLS/CVDUMP VCTOOLS/MC VCTOOLS/RC VCTOOLS/CONTENTS/HDXCVT VCTOOLS/PMAKE/SRC PCH PDC \
	NTSLM MASM DISKEDIT DLGEDIT DOCTOR DOSDEV DSKIMAGE FONTEDIT IMAGEDIT MIP2COFF OBJDIR \
	READLINE SETLINK SOLIDPP SLMNEW/SLM SLMNEW/UTIL SLMNEW/SLMED/DAYTONA SLMNEW/FIXDIFF SPY/APP \
	SECLIST SIMBAD TAKEOWN RCDUMP VI NEWINVTP WZMAIL/MAIL1.0 WAP/DUMP WAP/CONVERT \
	WINDIFF/WINDIFF WINDIFF/SERVER ZOOMIN COMPDIR PVIEWER ROSHARE A2COFF CLEARMEM EDITREG XEROX \
	TILE CNTFS DETAB DIS32 DISKGEOM FILEVER FLEX GETBIN LOGOFF MODSEL MPTEST MRGSCOMP \
	MHLSRV/CLIENT MHLSRV/SERVER NEWER NTFSDUMP OWNTREE RSHELL/NTCLIENT RSHELL/SERVER SFS TIMTP \
	LZTEST VMTESTS XMAS YACC ZD DSKDUMP KERNPROF SECEDIT/APP
step UMTEST=1 DEST=sdktools build-image.sh PRIVATE/SDKTOOLS/RANDOM

# 9. the hives and the system disk
step make-hives.sh
step make-disk.sh

[ -n "$LIST" ] || echo "build-all: done, $n steps ($(date +%H:%M))"
