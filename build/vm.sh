#!/bin/sh
#
#  vm.sh -- the MIPS NT build machine, sourced by the build scripts.
#
#  The NT code of 3MAX is compiled with C900 (/QMR3000) on an NT 4.0 MIPS
#  machine (~/VM/NTMIPS) that runs rshd.  Its T:\NTOS is this Mac's
#  /Volumes/NTOS: the scripts copy the sources there, run a .cmd file on the
#  machine over rsh, and read the objects and logs back from the share.
#
#	VM_HOST, VM_PORT	the rshd of the machine (127.0.0.1, 8514)
#	SHARE			/Volumes/NTOS/3MAX = T:\NTOS\3MAX
#	NTENV			macros of the build environment that SOURCES
#				files read, for nmake (CRTLIBTYPE=NT for the C
#				run-time of FP32NT, as BUILDCRT.CMD sets it)
#
#  vmrun CMDLINE		runs a command line on the machine (under dbgrun)
#  ntcopy DIR...		copies directories of the NT tree (PRIVATE/...,
#				PUBLIC/...) to the share, where the .cmd files
#				find them as %NT%\DIR; the files of 3MAX/nt/DIR
#				(the R3000 and DECstation versions of NT files,
#				and new files) go over them (a directory of
#				new files only, such as a new HAL, is in 3MAX/nt only)
#  ntcopytop DIR...		the same for the files of the directories only, not
#				their subdirectories
#  ntpath P [D]			the directory P of a path in the tree (relative
#				to D, by default DIR) from the top of the tree
#  ntsources DIR VAR...		prints VAR=VALUE for each VAR as nmake reads the
#				SOURCES file of DIR (copied with ntcopy) on the
#				machine, with MIPS\SOURCES and ..\MIPS\SOURCES as
#				makefile.def reads them (MIPS_R3000 defined: the
#				3MAX/nt versions of the MIPS SOURCES files pick the
#				R3000 files with it)

here=$(cd "$(dirname "$0")/.." && pwd)	# 3MAX
nt35=$(cd "$here/.." && pwd)
VM_HOST=${VM_HOST:-127.0.0.1}
VM_PORT=${VM_PORT:-8514}
SHARE=${SHARE:-/Volumes/NTOS/3MAX}
RSH=${RSH:-$HOME/go/bin/rsh}

[ -d "$SHARE" ] || { echo "vm.sh: $SHARE is not mounted" >&2; exit 1; }

# The files of 3MAX that are NT files with R3000 and DECstation changes are
# kept as edit scripts (FILE.ntdiff) over the NT tree: write the ones that
# are missing or older than their scripts (tools/ntdiff.pl).
ntdiff() {
	perl "$here/tools/ntdiff.pl" apply "$nt35" "$here/nt" "$here/arcfw" \
		"$here/crt" "$here/hal" "$here/build"
}
ntdiff || exit 1

# The compiler of the NT code, the tree's C900, is T:\NTOS\3MAX\C900 for
# the .cmd files.
[ -f "$SHARE/C900/BIN/CL.EXE" ] ||
	rsync -a --inplace --exclude=README.TXT "$nt35/PRIVATE/SDKTOOLS/C900/" "$SHARE/C900/"

# (under dbgrun -a, once build-dbgrun.sh has built it: a program of the
# command line that faults is reported and ends the command, without an
# error box on the machine's screen)
vmrun() {
	if [ -f "$SHARE/host/dbgrun.exe" ]; then
		"$RSH" -p "$VM_PORT" "$VM_HOST" "T:\\NTOS\\3MAX\\host\\dbgrun.exe -a cmd /c \"$1\""
	else
		"$RSH" -p "$VM_PORT" "$VM_HOST" "$1"
	fi
}

ntcopy() {
	for d in "$@"; do
		mkdir -p "$SHARE/NT/$d"
		[ ! -d "$nt35/$d" ] ||
			rsync -a --inplace "$nt35/$d/" "$SHARE/NT/$d/"
		[ ! -d "$here/nt/$d" ] ||
			rsync -a --inplace --exclude='*.ntdiff' "$here/nt/$d/" "$SHARE/NT/$d/"
	done
}

ntcopytop() {
	for d in "$@"; do
		mkdir -p "$SHARE/NT/$d"
		[ ! -d "$nt35/$d" ] ||
			rsync -a --inplace --exclude='*/' "$nt35/$d/" "$SHARE/NT/$d/"
		[ ! -d "$here/nt/$d" ] ||
			rsync -a --inplace --exclude='*/' --exclude='*.ntdiff' "$here/nt/$d/" "$SHARE/NT/$d/"
	done
}

ntsources() {
	_dir=$(printf "%s\n" "$1" | tr / '\\'); shift
	{
		printf '%s\n' '!INCLUDE sources' '!IF EXIST(mips\sources)' '!INCLUDE mips\sources' '!ENDIF' \
			'!IF EXIST(..\mips\sources)' '!INCLUDE ..\mips\sources' '!ENDIF' 'dump:'
		# (in an inline file: an echo of the SOURCES of GDI\GRE is longer
		# than a command line of nmake, U1095)
		printf '\t@type <<\n'
		for _v in "$@"; do
			printf '%s=$(%s)\n' $_v $_v
		done
		printf '<<\n'
	} | sed 's/$/\r/' > "$SHARE/build/dump$$.mak"
	vmrun "cd /d T:\\NTOS\\3MAX\\NT\\$_dir & C:\\MSDEV\\BIN\\NMAKE.EXE -nologo -f T:\\NTOS\\3MAX\\build\\dump$$.mak MIPS=1 MIPS_R3000=1 ALPHA=0 PPC=0 386=0 i386=0 TARGET_DIRECTORY=mips BASEDIR=T:\\NTOS\\3MAX\\NT $NTENV" | tr -d '\r'
	rm -f "$SHARE/build/dump$$.mak"
}

# ntpath P [D]: the directory P (relative to D, by default DIR, or \nt\...,
# or $(BASEDIR)\..., as nmake gives the BASEDIR of ntsources) from the top of
# the tree, spelled as the tree (or 3MAX/nt) spells it
ntpath() {
	case $1 in
	[\\/][nN][tT][\\/]*) p=$(printf "%s\n" "${1#?nt?}" | tr '\\' /) ;;
	T:?NTOS?3MAX?NT?*) p=$(printf "%s\n" "${1#T:?NTOS?3MAX?NT?}" | tr '\\' /) ;;
	*) p=${2:-$DIR}/$(printf "%s\n" "$1" | tr '\\' /) ;;
	esac
	# fold the .. and . components, then find the spelling the tree has
	p=$(printf "%s\n" "$p" | awk -F/ '{ n = 0; for (i = 1; i <= NF; i++) {
		if ($i == "..") n--; else if ($i != "." && $i != "") w[++n] = $i }
		for (i = 1; i <= n; i++) printf "%s%s", (i > 1 ? "/" : ""), w[i] }')
	r=
	for c in $(printf "%s\n" "$p" | tr / ' '); do
		# (|| :, as set -e ends the list at the first ls that fails)
		m=$( { ls "$nt35/$r" || :; ls "$here/nt/$r" || :; } 2>/dev/null | grep -ix "$c" | head -1)
		[ -n "$m" ] || return 0
		r=${r:+$r/}$m
	done
	echo "$r"
}
