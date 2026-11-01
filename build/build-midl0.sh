#!/bin/sh
#
#  build-midl0.sh -- the MIDL part of the pass 0 of the NT build for MIPS:
#  run the MAKEFIL0 of each directory named (default: the RPC interfaces of
#  the Win32 core) on the MIPS NT machine (vm.sh) with nmake, as build.exe
#  runs it, with the macros of makefile.plt for MIPS: MIDL is the MIDL of
#  the tree (build-midl.sh) and MIDL_CPP the MIPS cl (C900), the C
#  preprocessor of a MIPS build machine, with MIDL_FLAGS -DMIPS -D_MIPS_
#  -D_WCHAR_T_DEFINED.  INCLUDE is empty, as PUBLIC\TOOLS\NTENV.CMD sets it.
#
#	build-midl0.sh [DIR...]		(DIR from the top of the tree:
#					PRIVATE/LSA)
#
#  The stubs and headers land in the directories of the tree on the share
#  (the client and server directories of the component, PRIVATE\INC), where
#  build-lib.sh compiles them; the script lists the files each MAKEFIL0
#  made.  A log for each directory goes to T:\NTOS\3MAX\out\midl0.  nmake
#  runs under dbgrun -a (build-dbgrun.sh): a fault of midl or cl is reported
#  in the log, without an error box on the machine's screen; the run stops at
#  the first directory that fails.

set -e
. "$(dirname "$0")/vm.sh"
[ -f "$SHARE/host/midl.exe" ] || { echo "build-midl0: no T:\\NTOS\\3MAX\\host\\midl.exe (build-midl.sh)" >&2; exit 1; }
[ -f "$SHARE/host/dbgrun.exe" ] || { echo "build-midl0: no T:\\NTOS\\3MAX\\host\\dbgrun.exe (build-dbgrun.sh)" >&2; exit 1; }
[ $# -gt 0 ] || set -- PRIVATE/WINDOWS/SCREG/WINREG PRIVATE/WINDOWS/SCREG/SC PRIVATE/LSA PRIVATE/NEWSAM \
	PRIVATE/EVENTLOG PRIVATE/RPC/RUNTIME/RTIFS PRIVATE/WINDOWS/SPOOLER/SPOOLSS

ntcopy PUBLIC/SDK/INC PUBLIC/OAK/INC PRIVATE/INC
ntcopytop PUBLIC/OAK/BIN	# (makefile.plt)
mkdir -p "$SHARE/out/midl0" "$SHARE/build"
for d in "$@"; do
	ntcopy "$d"
	# (RTIFS writes the stubs of the runtime in its sibling directories)
	case $d in
	*/RPC/RUNTIME/RTIFS) ntcopy "${d%/RTIFS}/MTRT" "${d%/RTIFS}/RPCEPMPR" ;;
	esac
	# The directories that the files of these include by a path with ..
	# (SPOOLSS\import.h: <..\inc\splapip.h>, from the -I. of MIDL_FLAGS),
	# then the ones that the files of those include, as build-lib.sh
	# copies them.
	scan=$d; seen=" $d "
	while [ -n "$scan" ]; do
		next=
		for s in $scan; do
			for f in $(LC_ALL=C command grep -ahE '^[[:space:]]*#[[:space:]]*include[[:space:]]*["<][^">]*\.\.' \
			    "$nt35/$s"/* "$here/nt/$s"/* 2>/dev/null | LC_ALL=C sed 's/^[^"<]*["<]\([^">]*\)[">].*/\1/' | tr '\\' / | sort -u); do
				i=$(ntpath "$(dirname "$f")" "$s")
				[ -n "$i" ] || continue
				case $seen in *" $i "*) continue ;; esac
				seen="$seen$i "; next="$next $i"
				ntcopy "$i"
			done
		done
		scan=$next
	done
done

top() { case $1 in */RPC/RUNTIME/RTIFS) echo "${1%/RTIFS}" ;; *) echo "$1" ;; esac; }
for d in "$@"; do
	find "$SHARE/NT/$(top "$d")" -type f | sort > "$SHARE/out/midl0/$(basename "$d").before"
done
{
	cat <<-'EOF'
	@echo off
	set TOP=T:\NTOS\3MAX
	set NT=%TOP%\NT
	set PATH=%TOP%\host;%TOP%\C900\BIN;C:\MSDEV\BIN;C:\WINNT\system32
	set INCLUDE=
	set LIB=
	EOF
	for d in "$@"; do
		w=$(printf "%s\n" "$d" | tr / '\\')
		# (failed.txt names the directory until its nmake succeeds)
		printf '%s\n' "cd /d %NT%\\$w" "echo $w> %TOP%\\out\\midl0\\failed.txt" \
			"%TOP%\\host\\dbgrun.exe -a C:\\MSDEV\\BIN\\NMAKE.EXE -nologo -f makefil0 MIPS=1 386=0 ALPHA=0 PPC=0 NTMAKEENV=%NT%\\PUBLIC\\OAK\\BIN BASEDIR=%NT% > %TOP%\\out\\midl0\\$(basename "$d").log 2>&1" \
			"if errorlevel 1 goto end" "del %TOP%\\out\\midl0\\failed.txt"
	done
	printf '%s\n' ':end'
} | sed 's/$/\r/' > "$SHARE/build/midl0.cmd"
rm -f "$SHARE/out/midl0/failed.txt" "$SHARE/out/midl0/"*.log
# (all its output to a log: the machine stops a command that writes to an
# rsh connection that is gone)
vmrun 'T:\NTOS\3MAX\build\midl0.cmd > T:\NTOS\3MAX\out\midl0\midl0.txt 2>&1'

# What each MAKEFIL0 made, and what MIDL and nmake said (not its banner)
for d in "$@"; do
	n=$(basename "$d")
	[ -f "$SHARE/out/midl0/$n.log" ] || { rm -f "$SHARE/out/midl0/$n.before"; continue; }
	echo "--- $d"
	tr -d '\r' < "$SHARE/out/midl0/$n.log" | grep -v '^$' |
		grep -v '^Processing\|^Microsoft\|^Copyright\|^ *[0-9]* file(s) copied\|^IF EXIST\|^	*copy \|^	*del \|^	*midl ' || true
	find "$SHARE/NT/$(top "$d")" -type f | sort | comm -13 "$SHARE/out/midl0/$n.before" - | sed "s|^$SHARE/NT/|   |"
	rm -f "$SHARE/out/midl0/$n.before"
done
[ ! -f "$SHARE/out/midl0/failed.txt" ] || { echo "build-midl0: stopped, failed in $(tr -d '\r' < "$SHARE/out/midl0/failed.txt")"; exit 1; }
