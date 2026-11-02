#!/bin/sh
#
#  build-midl.sh -- build MIDL, the RPC interface compiler of the NT tree
#  (RPC\MIDL20), as a program of the MIPS NT machine (vm.sh) with its MSDEV
#  compiler and C library, as build-tools.sh builds the other tools; the
#  pass 0 of the tree (build-midl0.sh) runs it on the machine with the MIPS
#  C preprocessor, as the MIPS build machines of NT ran it.
#
#  First the parser tools of RPC\MIDLNEW that MIDL20\MAKEFIL0 makes the IDL
#  and ACF grammars with (midlyacc, midlpg, midleb), then the grammars as
#  MAKEFIL0 makes them (midlyacc with the parser of FRONT\YYPARS.C, the #line
#  directives taken out, midlpg; midleb writes the error recovery tables of
#  INCLUDE\IDLEREC.H and ACFEREC.H), then the libraries of MIDL20 (ANALYSIS,
#  CODEGEN, EXPR, SUPPORT) and the program of FRONT, each directory with the
#  SOURCES, INCLUDES and C_DEFINES of its SOURCES file (FRONT: MIPS_BUILD,
#  the defaults of a MIPS machine) and the defines that makefile.def gives a
#  MIPS build (MIPSMK.INC STD_DEFINES, the R4000 of the machine: without
#  _MIPS_, CODEGEN\FRMTSTR.HXX makes UNALIGNED nothing and MIDL takes
#  misaligned data faults).  The parser tools fail by exit(): completing is
#  success.
#
#	T:\NTOS\3MAX\host\midl.exe	(and midlyacc.exe, midlpg.exe, midleb.exe;
#					host\midl\midl.map)
#
#  The objects and a .log for each step go to T:\NTOS\3MAX\host\midl.

set -e
. "$(dirname "$0")/vm.sh"
M=PRIVATE/RPC
ntcopy $M/MIDLNEW/YACC $M/MIDLNEW/PG $M/MIDLNEW/EREC $M/MIDL20 PUBLIC/SDK/INC	# (ntverp.h, common.ver: midl.rc)
mkdir -p "$SHARE/host/midl" "$SHARE/build"
rm -rf "${SHARE:?}/host/midl/"*
rm -f "$SHARE/host/midl.exe" "$SHARE/host/midlyacc.exe" "$SHARE/host/midlpg.exe" "$SHARE/host/midleb.exe"

# srcs DIR [SUFFIX...]: the sources of the SOURCES file of DIR (not the .rc)
srcs() {
	ntsources "$1" SOURCES | sed -n 's/^SOURCES=//p' | tr ' \t' '\n\n' | grep -v '^$' | grep -iv '\.rc$'
}
var() {
	ntsources "$1" "$2" | sed -n "s/^$2=//p" | sed 's/^ *//; s/ *$//'
}
# incs DIR: the -I options of INCLUDES of DIR
incs() {
	for i in $(var "$1" INCLUDES | tr ';' ' '); do printf ' -I%s' "$i"; done
}

{
	cat <<-'EOF'
	@echo off
	set TOP=T:\NTOS\3MAX
	set NT=%TOP%\NT
	set PATH=%MSDevDir%\BIN;%SystemRoot%\system32
	set INCLUDE=%MSDevDir%\INCLUDE
	set LIB=%MSDevDir%\LIB
	set H=%TOP%\host\midl
	set CF=-nologo -c -O2 -DMIPS=1 -D_MIPS_=1 -DMIPSEL -DNO_EXT_KEYS -DCONDITION_HANDLING=1 -DWIN32_LEAN_AND_MEAN=1 -DNT_UP=1 -DNT_INST=0 -DWIN32=100 -D_NT1X_=100 -DR4000 -D_M_MRX000=4000 -DDBG=0 -DDEVL=1 -DBETA=0
	EOF
	# the parser tools
	for t in YACC:midlyacc PG:midlpg EREC:midleb; do
		d=$M/MIDLNEW/${t%%:*} n=${t#*:}
		w=$(printf "%s\n" "$d" | tr / '\\')
		printf '%s\n' "mkdir %H%\\$n 2> nul" "cd /d %NT%\\$w"
		for s in $(srcs "$d"); do
			printf '%s\n' "cl %CF% $(var "$d" C_DEFINES)$(incs "$d") -Fo%H%\\$n\\ $s >> %H%\\$n.log 2>&1"
		done
		printf '%s\n' "link -nologo -subsystem:console -out:%TOP%\\host\\$n.exe %H%\\$n\\*.obj libc.lib kernel32.lib >> %H%\\$n.log 2>&1"
	done
	# the grammars (MIDL20\MAKEFIL0), in a copy of FRONT (gram: the
	# objects of FRONT go to FRONT, the same name on the share)
	printf '%s\n' "mkdir %H%\\gram 2> nul" "copy %NT%\\$(printf "%s\n" "$M" | tr / '\\')\\MIDL20\\FRONT\\*.* %H%\\gram > nul 2>&1" "cd /d %H%\\gram"
	for g in grammar:xlatidl:IDL:idlerec acfgram:xlatacf:ACF:acferec; do
		n=$(echo "$g" | cut -d: -f1) x=$(echo "$g" | cut -d: -f2)
		t=$(echo "$g" | cut -d: -f3) r=$(echo "$g" | cut -d: -f4)
		printf '%s\n' "%TOP%\\host\\midlyacc -his -t \"YYCONST \" $n.y > $n.yacc.log 2>&1" \
			"findstr /v /r /c:\"^#.*line\" $n.c > $n.tmp" \
			"%TOP%\\host\\midlpg $n.tmp > $n.cxx 2> $n.pg.log" \
			"%TOP%\\host\\midleb - $x.dat $t > $r.h 2> $n.eb.log"
	done
	# the libraries of MIDL20 and the program of FRONT (with the grammars)
	for d in ANALYSIS CODEGEN EXPR SUPPORT FRONT; do
		w=$(printf "%s\n" "$M/MIDL20/$d" | tr / '\\')
		printf '%s\n' "mkdir %H%\\$d 2> nul" "cd /d %NT%\\$w"
		# (the grammar headers of FRONT and the recovery tables of INCLUDE)
		x="-I%H%\\gram"
		for s in $(srcs "$M/MIDL20/$d"); do
			case $d:$s in
			FRONT:grammar.cxx|FRONT:acfgram.cxx) s="%H%\\gram\\$s" ;;
			esac
			printf '%s\n' "cl %CF% -GX $(var "$M/MIDL20/$d" C_DEFINES)$(incs "$M/MIDL20/$d") $x -Fo%H%\\$d\\ $s >> %H%\\$d.log 2>&1"
		done
	done
	printf '%s\n' "cd /d %NT%\\$(printf "%s\n" "$M" | tr / '\\')\\MIDL20\\FRONT" \
		"rc -r -I..\\include -I%NT%\\PUBLIC\\SDK\\INC -fo%H%\\midl.res midl.rc > %H%\\rc.log 2>&1" \
		"link -nologo -subsystem:console -map:%H%\\midl.map -out:%TOP%\\host\\midl.exe %H%\\FRONT\\*.obj %H%\\CODEGEN\\*.obj %H%\\ANALYSIS\\*.obj %H%\\EXPR\\*.obj %H%\\SUPPORT\\*.obj %H%\\midl.res libc.lib kernel32.lib > %H%\\link.log 2>&1"
} | sed 's/$/\r/' > "$SHARE/build/midl.cmd"
# (all its output to a log: the machine stops a command that writes to an
# rsh connection that is gone)
vmrun 'T:\NTOS\3MAX\build\midl.cmd > T:\NTOS\3MAX\host\midl.log 2>&1'

for l in midlyacc midlpg midleb ANALYSIS CODEGEN EXPR SUPPORT FRONT rc link; do
	[ ! -f "$SHARE/host/midl/$l.log" ] ||
		tr -d '\r' < "$SHARE/host/midl/$l.log" | grep -iE '(error|warning)[ :]' | grep -v 'C4[0-9]*:' | sed "s/^/$l: /" | head -20 || true
done
# (the link of midl.exe failed if its log has an error: the share's client
# can show a file for a while that the machine deleted)
! tr -d '\r' < "$SHARE/host/midl/link.log" | grep -iq 'error' || { echo "build-midl: no midl.exe"; exit 1; }
for f in midlyacc midlpg midleb midl; do
	for i in 1 2 3 4 5 6 7 8 9 10; do [ ! -f "$SHARE/host/$f.exe" ] || break; sleep 1; done
	[ -f "$SHARE/host/$f.exe" ] || { echo "build-midl: no $f.exe"; exit 1; }
done
echo "build-midl: midl.exe, midlyacc.exe, midlpg.exe, midleb.exe"
