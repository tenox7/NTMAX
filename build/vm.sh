#!/bin/sh
#
#  vm.sh -- the MIPS NT build machine, sourced by the build scripts.
#
#  The NT code of 3MAX is compiled with C900 (/QMR3000) on an NT 4.0 MIPS
#  machine (~/VM/NTMIPS) that runs rshd.  Its T:\NTOS is this Mac's
#  /Volumes/NTOS: the scripts copy the sources there, run a .cmd file on the
#  machine over rsh, and read the objects and logs back from the share.  Any
#  NT 4.0 MIPS machine with Visual C++ 4.0 for MIPS and the share as T: will
#  do: the .cmd files find its Visual C++ and its system directory in its
#  environment, %MSDevDir% and %SystemRoot%.
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
#				(a directory is copied again only when a file of
#				it in the tree changed since its last copy, or the
#				share has another copy: the stamp of the copy,
#				obj/ntcopy/all-DIR or top-DIR, holds a token that
#				the share's copy holds too, DIR/.ntcopy-all or
#				.ntcopy-top; rsync looks at each file on the
#				share, 11 s for PUBLIC\SDK\INC and OAK\INC)
#  ntpath P [D]			the directory P of a path in the tree (relative
#				to D, by default DIR) from the top of the tree
#  ntsources DIR VAR...		prints VAR=VALUE for each VAR as nmake reads the
#				SOURCES file of DIR (copied with ntcopy) on the
#				machine, with MIPS\SOURCES and ..\MIPS\SOURCES as
#				makefile.def reads them (MIPS_R3000 defined: the
#				3MAX/nt versions of the MIPS SOURCES files pick the
#				R3000 files with it), after the MAKEFILE of DIR,
#				which includes makefile.def: the macros that it
#				and the files it includes set first are the ones
#				the SOURCES file starts from (NET\UI: C_DEFINES,
#				-DUNICODE, of uiglobal.mk)

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
#
# The answer of the command, its output and dbgrun's, goes to a file of the
# share, build\vmrun-ID, between a line vmrun-start and a line vmrun-end,
# and is printed from there: the rshd of the machine (ntrshd) ends a session
# a quarter of a second after the command and loses what its pipe still
# held then (the last lines, now and then), and when it runs several
# sessions at once one of them now and then goes on after its command, or
# never starts it.  vmrun waits for rsh; once the file has vmrun-end it
# gives rsh 10 s more, then stops it; a command that has not started after
# 5 minutes (no vmrun-start) runs again, up to 3 times.  (The share can show
# a file the machine wrote some seconds late: 30 s for the end of the file
# after rsh.)
vmrun() {
	_id=$$-$(od -An -N4 -tu4 /dev/urandom | tr -d ' ')
	_f=$SHARE/build/vmrun-$_id
	_w="T:\\NTOS\\3MAX\\build\\vmrun-$_id"
	if [ -f "$SHARE/host/dbgrun.exe" ]; then
		_c="T:\\NTOS\\3MAX\\host\\dbgrun.exe -a cmd /c \"$1\""
	else
		_c="cmd /c \"$1\""
	fi
	_i=0
	while :; do
		rm -f "$_f"
		# (in parentheses: ntrshd hands a command line that starts with
		# echo or perl to a Perl of its own)
		"$RSH" -p "$VM_PORT" "$VM_HOST" "(echo vmrun-start)> $_w & $_c >> $_w 2>&1 & echo vmrun-end>> $_w" \
			< /dev/null > "$_f.rsh" 2>&1 &
		_p=$!; _t=$(date +%s); _e=
		while kill -0 $_p 2> /dev/null; do
			if [ -z "$_e" ]; then
				grep -q '^vmrun-end' "$_f" 2> /dev/null && _e=$(date +%s)
			elif [ $(($(date +%s) - _e)) -ge 10 ]; then
				kill $_p 2> /dev/null; break
			fi
			if [ $(($(date +%s) - _t)) -ge 300 ] && ! grep -q '^vmrun-start' "$_f" 2> /dev/null; then
				kill $_p 2> /dev/null; break
			fi
			sleep 1
		done
		wait $_p 2> /dev/null || :
		_n=0
		while ! grep -q '^vmrun-end' "$_f" 2> /dev/null && [ $_n -lt 30 ]; do
			sleep 1; _n=$((_n + 1))
		done
		grep -q '^vmrun-start' "$_f" 2> /dev/null || [ $_i -ge 3 ] || {
			_i=$((_i + 1))
			printf 'vmrun: the command did not start, again (%s): %s\n' $_i "$1" >&2
			sed 's/^/vmrun: /' "$_f.rsh" >&2
			sleep 5
			continue
		}
		break
	done
	# (the rsh error of a command that did not start)
	grep -q '^vmrun-start' "$_f" 2> /dev/null || cat "$_f.rsh" >&2
	rm -f "$_f.rsh"
	if [ ! -f "$_f" ]; then
		printf 'vmrun: no answer from the machine: %s\n' "$1" >&2
		return 1
	fi
	grep -v '^vmrun-start\|^vmrun-end' "$_f" || :
	grep -q '^vmrun-end' "$_f" ||
		printf 'vmrun: the answer of the machine stops before the end: %s\n' "$1" >&2
	rm -f "$_f"
}

ntcopy() {
	for d in "$@"; do
		ntcurrent all "$d" && continue
		ntstart
		mkdir -p "$SHARE/NT/$d"
		[ ! -d "$nt35/$d" ] ||
			rsync -a --inplace "$nt35/$d/" "$SHARE/NT/$d/"
		[ ! -d "$here/nt/$d" ] ||
			rsync -a --inplace --exclude='*.ntdiff' "$here/nt/$d/" "$SHARE/NT/$d/"
		ntstamp all "$d"
	done
}

ntcopytop() {
	for d in "$@"; do
		ntcurrent top "$d" && continue
		ntstart
		mkdir -p "$SHARE/NT/$d"
		[ ! -d "$nt35/$d" ] ||
			rsync -a --inplace --exclude='*/' "$nt35/$d/" "$SHARE/NT/$d/"
		[ ! -d "$here/nt/$d" ] ||
			rsync -a --inplace --exclude='*/' --exclude='*.ntdiff' "$here/nt/$d/" "$SHARE/NT/$d/"
		ntstamp top "$d"
	done
}

# ntcurrent all|top DIR: true if the share's copy of DIR is current (see
# ntcopy); ntstart before a copy, ntstamp all|top DIR after it: the stamp
# has the time the copy started
ntcurrent() {
	_s=$here/obj/ntcopy/$1-$(printf '%s\n' "$2" | tr / _)
	[ -f "$_s" ] && cmp -s "$_s" "$SHARE/NT/$2/.ntcopy-$1" 2>/dev/null || return 1
	if [ "$1" = top ]; then _x='-maxdepth 1'; else _x=; fi
	[ -z "$(find "$nt35/$2" "$here/nt/$2" $_x \( -newer "$_s" -o -cnewer "$_s" \) -print 2>/dev/null | head -1)" ]
}
ntstart() {
	mkdir -p "$here/obj/ntcopy"
	_t=$here/obj/ntcopy/.start.$$
	: > "$_t"
}
ntstamp() {
	_s=$here/obj/ntcopy/$1-$(printf '%s\n' "$2" | tr / _)
	echo "$(date +%s).$$.$(od -An -N4 -tu4 /dev/urandom | tr -d ' ')" > "$_s.$$"
	touch -r "$_t" "$_s.$$"
	rm -f "$_t"
	cp "$_s.$$" "$SHARE/NT/$2/.ntcopy-$1" && mv "$_s.$$" "$_s"
}

ntsources() {
	_dir=$(printf "%s\n" "$1" | tr / '\\'); shift
	# The makefile.def that the MAKEFILE includes, $(NTMAKEENV)\makefile.def:
	# the SOURCES files and a target, dump, that prints the macros.
	mkdir -p "$SHARE/build/dump$$"
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
	} | sed 's/$/\r/' > "$SHARE/build/dump$$/makefile.def"
	_m="MIPS=1 MIPS_R3000=1 ALPHA=0 PPC=0 386=0 i386=0 TARGET_DIRECTORY=mips BASEDIR=T:\\NTOS\\3MAX\\NT $NTENV"
	_o=$(vmrun "cd /d T:\\NTOS\\3MAX\\NT\\$_dir & %MSDevDir%\\BIN\\NMAKE.EXE -nologo -f makefile NTMAKEENV=T:\\NTOS\\3MAX\\build\\dump$$ $_m dump" | tr -d '\r')
	# (a directory without a MAKEFILE that includes makefile.def: its
	# SOURCES files alone; a makefile it includes that is not on the share
	# is an error to see)
	if ! printf '%s\n' "$_o" | grep -q "^$1="; then
		printf '%s\n' "$_o" | grep 'U1052' | grep -iv "'makefile'" |
			while IFS= read -r _l; do printf 'ntsources: %s: %s\n' "$_dir" "$_l"; done >&2 || :
		_o=$(vmrun "cd /d T:\\NTOS\\3MAX\\NT\\$_dir & %MSDevDir%\\BIN\\NMAKE.EXE -nologo -f T:\\NTOS\\3MAX\\build\\dump$$\\makefile.def $_m dump" | tr -d '\r')
	fi
	printf '%s\n' "$_o"
	rm -rf "$SHARE/build/dump$$"
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
