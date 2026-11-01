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
#	   netapi32 for NetRap, lsasrv for samsrv)
#	7  the hives and the system disk: make-disk.sh reads KEEP, DISK and
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

# step SCRIPT [ARG...]: the next step, a script of build/; "implib DIR" is
# IMPLIB=1 build-image.sh DIR
step() {
	n=$((n + 1))
	[ -z "$LIST" ] || { echo "$n $*"; return 0; }
	[ "$n" -ge "$FIRST" ] || return 0
	echo "build-all: $n ($(date +%H:%M)) $*"
	f=$log/$n.log
	case $1 in
	implib) shift; set -- env IMPLIB=1 sh "$here/build/build-image.sh" "$@" ;;
	*) s=$1; shift; set -- sh "$here/build/$s" "$@" ;;
	esac
	# (rsh reads its standard input: not the rest of this script's)
	if "$@" < /dev/null > "$f" 2>&1; then
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

# 3. the firmware (the boot library), the OS loader, the kernel and the HAL
for d in RTL/BOOT BOOT/LIB BOOT/BLDR CONFIG/BOOT; do
	step build-lib.sh PRIVATE/NTOS/$d
done
step build-arcfw.sh
for d in KE MM EX IO CONFIG SE PS RTL FSRTL CACHE OB RAW KD INIT LPC VDM DBGK; do
	step build-lib.sh PRIVATE/NTOS/$d/UP
done
step build-lib.sh PRIVATE/LSA/CLIENT/KERNEL
step build-osloader.sh
step build-lib.sh PRIVATE/NTOS/NTHALS/HAL3MAX
step build-kernel.sh

# 4. the drivers (scsidisk links the class library and, as ncr53c9x does,
# scsiport's import library; pmagba videoprt's)
step build-lib.sh PRIVATE/NTOS/DD/CLASS
images NTOS/DD/SCSIPORT NTOS/DD/SCSIDISK NTOS/MINIPORT/NCR53C94 NTOS/FASTFAT NTOS/NPFS \
	NTOS/MAILSLOT NTOS/VIDEO/PORT NTOS/VIDEO/PMAGBA NTOS/DD/DZPORT NTOS/DD/LK201 \
	NTOS/DD/KBDCLASS NTOS/DD/VSXXX NTOS/DD/MOUCLASS

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
	WINDOWS/NETDDE/INCS WINDOWS/NETDDE/SRC/TOOLKIT; do
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
	WINDOWS/EP/WINMINE WINDOWS/SHELL/WINFILE

# 7. the hives and the system disk
step make-hives.sh
step make-disk.sh

[ -n "$LIST" ] || echo "build-all: done, $n steps ($(date +%H:%M))"
