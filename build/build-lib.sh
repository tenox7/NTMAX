#!/bin/sh
#
#  build-lib.sh -- build a library of the NT tree for the R3000 the way
#  build.exe builds it, with C900 /QMR3000 on the MIPS NT machine (vm.sh).
#
#	build-lib.sh DIR
#	ONLY="SOURCE..." build-lib.sh DIR
#	OUT=DIRECTORY build-lib.sh DIR
#
#  With OUT, the objects and the library go to T:\NTOS\3MAX\out\OUT (OUT=
#  crtst/stdio) in place of out\TARGETNAME: a component built again with
#  other macros (CRT32 for each CRTLIBTYPE, NTENV, as BUILDCRT builds the
#  copies of the tree CRTLIB merges).
#
#  With ONLY, only the sources named (mutexs.s, dispcf.c: the file names of
#  SOURCES entries) are compiled again, and the library is made of their new
#  objects and the objects of the other sources from the last build, as
#  build.exe compiles only the sources that changed.
#  DIR is the directory of the SOURCES file, from the top of the tree
#  (PRIVATE/NTOS/RTL/BOOT).  nmake reads SOURCES, MIPS\SOURCES and
#  ..\MIPS\SOURCES on the machine, as makefile.def does, for TARGETNAME,
#  SOURCES, MIPS_SOURCES (with MIPS_R3000 defined: the 3MAX/nt versions of
#  the MIPS SOURCES files pick the R3000 files with it),
#  INCLUDES, C_DEFINES, NET_C_DEFINES, USE_CRTDLL, USE_LIBCMT, GPSIZE,
#  NTTARGETFILE0, TARGETPATH and NTTARGETFILES.  There is no library for
#  TARGETTYPE UMAPPL_NOLIB or an empty SOURCES (makefile.def makes no TARGET
#  then): only the sources and the UMAPPL programs compile.
#  The NTTARGETFILE0 targets are made first with nmake in DIR (the message
#  files of mc, the export list or the .res of a DLL): build/mkinc.mak, the
#  inference rules of makefile.def, then makefile.inc, with the macros of
#  makefile.def that the rules use (NTTARGETFILE0, CDEFINES, INCPATH0,
#  BASEDIR, _NTROOT: the top of the tree on the machine) and the obj\mips
#  directory that build.exe makes; a source that the tree does not have is
#  made the same way (the system service stubs of gensrv).  Each source is
#  compiled in DIR (a C source with build/r3000.h included first: no
#  sqrt.d), a .rc source with rc into a .res; the objects, the .res files, a
#  .log for each source and TARGETNAME.lib go to
#  T:\NTOS\3MAX\out\TARGETNAME.  The object of each program of UMAPPL
#  (makefile.def: obj\mips\PROGRAM.obj, from PROGRAM.c or a source of
#  another suffix, by the inference rules) is compiled too, unless SOURCES
#  has it, into the um directory there: it is not in the library.  Every
#  object is checked with tools/r3kcheck.pl.  A library of TARGETPATH obj
#  also goes to DIR\obj\mips, where build.exe puts it, and a library that
#  NTTARGETFILES names (obj\*\NAME.lib) is made there after it by the rule
#  of makefile.inc, of the libraries of obj\mips directories (LSA\CRYPT\DLL:
#  sys003.lib, of its crypt.lib and the engine.lib of ..\ENGINE), and goes
#  to T:\NTOS\3MAX\out\NAME as NAME.lib.

set -e
. "$(dirname "$0")/vm.sh"
[ $# -eq 1 ] || { echo "usage: build-lib.sh DIR" >&2; exit 1; }
DIR=${1%/}
[ -f "$nt35/$DIR/SOURCES" ] || [ -f "$here/nt/$DIR/SOURCES" ] || { echo "build-lib: no $DIR/SOURCES" >&2; exit 1; }
WDIR=$(printf "%s\n" "$DIR" | tr / '\\')
mkdir -p "$SHARE/build"
cp "$here/build/r3000.h" "$SHARE/build/"
sed 's/$/\r/' "$here/build/mkinc.mak" > "$SHARE/build/mkinc.mak"

# Read the SOURCES file with nmake.  The MIPS directories go to the share
# first: their SOURCES files are read here, before the directories of the
# sources are copied.
ntcopy "$DIR"
# The files of the parent directory: the sources include them as ..\NAME
# (the drivesup.c and hal.src of a HAL).
d=$(ntpath ..)
[ -z "$d" ] || ntcopytop "$d"
for i in mips ../mips; do
	d=$(ntpath "$i")
	[ -z "$d" ] || ntcopy "$d"
done
vars=$(ntsources "$DIR" TARGETNAME TARGETTYPE SOURCES MIPS_SOURCES INCLUDES C_DEFINES NET_C_DEFINES USE_CRTDLL USE_LIBCMT \
	NTTARGETFILE0 GPSIZE UMAPPL TARGETPATH NTTARGETFILES)
var() { printf '%s\n' "$vars" | sed -n "s/^$1=//p" | sed 's/^ *//; s/ *$//'; }
NAME=$(var TARGETNAME)
[ -n "$NAME" ] || { echo "build-lib: $DIR: no TARGETNAME"; printf '%s\n' "$vars"; exit 1; }
# The output directory: out\NAME, or out\OUT (OUT=crtst/stdio, a component
# built again with other macros: CRT32 with CRTLIBTYPE ST)
ON=${OUT:-$NAME}
OW=$(printf "%s\n" "$ON" | tr / '\\')
OF=$(printf "%s\n" "$ON" | tr / _)
SRCS="$(var SOURCES) $(var MIPS_SOURCES)"
# The library of the objects of SOURCES (makefile.def's TARGET): none for
# UMAPPL_NOLIB or without sources (only the UMAPPL programs compile, and the
# objects of SOURCES are the program's UMOBJS or UMLIBS).
MKLIB=1
[ "$(var TARGETTYPE)" != UMAPPL_NOLIB ] && [ -n "$(printf '%s' "$SRCS" | tr -d ' ')" ] || MKLIB=
GPSIZE=$(var GPSIZE); GPSIZE=${GPSIZE:-0}	# (makefile.def: 0 unless SOURCES sets it)
# The defines of the component, ENV_DEFINES of MIPSMK.INC: LIBC_DEFINES of
# makefile.def, then C_DEFINES and NET_C_DEFINES (-DUNICODE, in most of NET).
if [ -n "$(var USE_CRTDLL)" ]; then CDEF="-D_DLL=1 -D_MT=1"
elif [ -n "$(var USE_LIBCMT)" ]; then CDEF="-D_MT=1"
else CDEF=; fi
CDEF="$CDEF $(var C_DEFINES) $(var NET_C_DEFINES)"

# ntfile S: true if the tree (or 3MAX/nt) has the source S (relative to DIR)
ntfile() {
	f=$(printf "%s\n" "$1" | tr '\\' /)
	d=$(ntpath "$(dirname "$f")")
	[ -n "$d" ] && ls "$nt35/$d" "$here/nt/$d" 2>/dev/null | grep -qix "$(basename "$f")"
}

# ntsrc S: the file that makefile.def compiles for the SOURCES entry S.  For
# an entry the tree does not have, its inference rules take the first file
# of that name in ..\, ..\mips\, the directory and mips\ (CONFIG\MIPS\SOURCES
# lists ..\init.c for ..\mips\init.c), then a file of that name with the
# other suffixes of the rules, in the order of .SUFFIXES (FP32NT\TRAN\SOURCES
# lists mips\fsqrt.c for mips\fsqrt.s); else S, which makefile.inc makes.
ntsrc() {
	ntfile "$1" && { printf '%s\n' "$1"; return; }
	b=$(basename "$(printf "%s\n" "$1" | tr '\\' /)")
	for p in '..\' '..\mips\' '' 'mips\'; do
		ntfile "$p$b" && { printf '%s\n' "$p$b"; return; }
	done
	d=$(dirname "$(printf "%s\n" "$1" | tr '\\' /)" | tr / '\\')\\
	[ "$d" != '.\' ] || d=
	for x in cxx cpp c f s asm; do
		for p in "$d" '..\' '..\mips\' '' 'mips\'; do
			ntfile "$p${b%.*}.$x" && { printf '%s\n' "$p${b%.*}.$x"; return; }
		done
	done
	printf '%s\n' "$1"
}

# The sources of the UMAPPL programs that SOURCES does not compile.
UMSRCS=
for u in $(var UMAPPL | tr '*' ' '); do
	for s in $SRCS; do
		b=$(basename "$(printf "%s\n" "$s" | tr '\\' /)")
		case $b in *.[rR][cC]) continue ;; esac
		[ "$(printf "%s\n" "${b%.*}" | tr A-Z a-z)" != "$(printf "%s\n" "$u" | tr A-Z a-z)" ] || continue 2
	done
	UMSRCS="$UMSRCS $(ntsrc "$u.c")"
done

# Copy the include directories and the directories of the sources.
INC='-Imips\ -I.'	# (makefile.def: -Imips\ -I. first, for C and the assembler)
incs=
for i in $(var INCLUDES | tr ';' ' '); do
	d=$(ntpath "$i")
	[ -n "$d" ] || { printf "%s\n" "build-lib: $DIR: no include directory $i"; continue; }
	ntcopy "$d"; incs="$incs $d"
	INC="$INC -I%NT%\\$(printf "%s\n" "$d" | tr / '\\')"
done
dirs=
for s in $SRCS $UMSRCS; do
	d=$(ntpath "$(dirname "$(ntsrc "$s" | tr '\\' /)")")
	case " $dirs " in *" $d "*) ;; *) dirs="$dirs $d"; ntcopy "$d" ;; esac
done
# The directories that the files of these include by a path with ..
# (csrdll.h: "..\..\csr\inc\csr.h", from the directory of csrdll.h, where the
# compiler looks first), then the ones that the files of those include.
scan="$incs $dirs"; seen=" $scan "
while [ -n "$scan" ]; do
	next=
	for d in $scan; do
		for f in $(LC_ALL=C command grep -ahE '^[[:space:]]*#[[:space:]]*include[[:space:]]*"[^"]*\.\.' \
		    "$nt35/$d"/* "$here/nt/$d"/* 2>/dev/null | LC_ALL=C sed 's/^[^"]*"\([^"]*\)".*/\1/' | tr '\\' / | sort -u); do
			i=$(ntpath "$(dirname "$f")" "$d")
			[ -n "$i" ] || continue
			case $seen in *" $i "*) continue ;; esac
			seen="$seen$i "; next="$next $i"
			ntcopy "$i"
		done
	done
	scan=$next
done
ntcopy PUBLIC/SDK/INC PUBLIC/OAK/INC

# The libraries of NTTARGETFILES (obj\mips\NAME.lib)
LIBTARGETS=
for t in $(var NTTARGETFILES | sed 's/\*/mips/g'); do
	case $t in [oO][bB][jJ]\\mips\\*.[lL][iI][bB]) LIBTARGETS="$LIBTARGETS $t" ;; esac
done

# only B: true if the source file B is to be compiled (all without ONLY)
only() {
	[ -n "$ONLY" ] || return 0
	for _o in $ONLY; do
		[ "$(printf "%s\n" "$_o" | tr A-Z a-z)" != "$(printf "%s\n" "$1" | tr A-Z a-z)" ] || return 0
	done
	return 1
}

cmd=$SHARE/build/$OF.cmd
{
	printf '%s\n' '@echo off' "set GPSIZE=$GPSIZE"
	cat <<-'EOF'
	set TOP=T:\NTOS\3MAX
	set NT=%TOP%\NT
	set PATH=%TOP%\C900\BIN;C:\MSDEV\BIN;%TOP%\host;C:\WINNT\system32
	set INCLUDE=
	set CPU=/QMR3000
	set DEFS=-DMIPS=1 -D_MIPS_=1 -DMIPSEL -DNO_EXT_KEYS -DCONDITION_HANDLING=1 -DWIN32_LEAN_AND_MEAN=1 -DNT_UP=1 -DNT_INST=0 -DWIN32=100 -D_NT1X_=100 -DR3000 -D_M_MRX000=3000 -DDBG=0 -DDEVL=1
	set CFLAGS=-nologo -c /Zel /Zp8 /Gy /W3 -Ox -Gt%GPSIZE% -D__stdcall= -D__cdecl= -D_LANGUAGE_C -DLANGUAGE_C -FI%TOP%\build\r3000.h
	set AFLAGS=-nologo -c /Zel /Zp8 /Gy /W3 -Gt%GPSIZE% -Ge -O -D_LANGUAGE_ASSEMBLY
	set MKINC=C:\MSDEV\BIN\NMAKE.EXE -nologo -f %TOP%\build\mkinc.mak MIPS=1 MIPS_R3000=1 ALPHA=0 PPC=0 386=0 i386=0 TARGET_DIRECTORY=mips ASM_SUFFIX=s ASM_INCLUDE_SUFFIX=h TARGET_BRACES=-B TARGET_CPP=cl "TARGET_DEFINES=-DMIPS -D_MIPS_" BASEDIR=%NT% _NTROOT=%NT%
	EOF
	printf '%s\n' "set INC=$INC -I%NT%\\PUBLIC\\OAK\\INC -I%NT%\\PUBLIC\\SDK\\INC -I%NT%\\PUBLIC\\SDK\\INC\\CRT"
	# (MIPSMK.INC: CDEFINES, and MIPS_CDEFINES the same; makefile.def:
	# INCPATH0, the include path)
	printf '%s\n' "set MKINC=%MKINC% $NTENV \"CDEFINES=%DEFS% $CDEF\" \"MIPS_CDEFINES=%DEFS% $CDEF\" \"INCPATH0=%INC%\""
	printf '%s\n' "rem $OF.cmd -- made by build-lib.sh: compile $WDIR for the R3000"
	printf '%s\n' "set O=%TOP%\\out\\$OW"
	printf '%s\n' "cd /d %NT%\\$WDIR" 'if not exist obj\mips\nul mkdir obj\mips'
	# (MIPSMK.INC: NTTARGETFILE0 with the * made mips, a macro that
	# makefile.inc can name)
	t0=$(var NTTARGETFILE0 | sed 's/\*/mips/g')
	[ -z "$t0" ] || printf '%s\n' "%MKINC% \"NTTARGETFILE0=$t0\" $t0 > %O%\\nttarget0.log 2>&1"
	for s in $SRCS; do
		b=$(basename "$(printf "%s\n" "$s" | tr '\\' /)"); o=$(printf "%s\n" "${b%.*}" | tr A-Z a-z)
		only "$b" || continue
		s=$(ntsrc "$s")
		# a source the tree does not have is made by a rule of makefile.inc
		ntfile "$s" || printf '%s\n' "%MKINC% $s > %O%\\$o.gen.log 2>&1"
		case $s in
		*.[sS])	printf '%s\n' "cl %AFLAGS% %INC% %CPU% %DEFS% $CDEF -Fo%O%\\$o.obj $s > %O%\\$o.log 2>&1" ;;
		*.[rR][cC]) printf '%s\n' "rc -r %INC% %DEFS% $CDEF -fo%O%\\$o.res $s > %O%\\$o.rc.log 2>&1" ;;
		*)	printf '%s\n' "cl %CFLAGS% %INC% %CPU% %DEFS% $CDEF -Fo%O%\\$o.obj $s > %O%\\$o.log 2>&1" ;;
		esac
	done
	for s in $UMSRCS; do
		b=$(basename "$(printf "%s\n" "$s" | tr '\\' /)"); o=$(printf "%s\n" "${b%.*}" | tr A-Z a-z)
		only "$b" || continue
		case $s in
		*.[sS])	printf '%s\n' "cl %AFLAGS% %INC% %CPU% %DEFS% $CDEF -Fo%O%\\um\\$o.obj $s > %O%\\um\\$o.log 2>&1" ;;
		*)	printf '%s\n' "cl %CFLAGS% %INC% %CPU% %DEFS% $CDEF -Fo%O%\\um\\$o.obj $s > %O%\\um\\$o.log 2>&1" ;;
		esac
	done
	if [ -n "$MKLIB" ]; then
		printf '%s\n' 'cd /d %O%' "C:\\MSDEV\\BIN\\LIB.EXE -nologo -machine:mips -out:$NAME.lib *.obj > lib.log 2>&1"
		[ "$(var TARGETPATH)" != obj ] || printf '%s\n' "copy $NAME.lib %NT%\\$WDIR\\obj\\mips > nul"
	fi
	for t in $LIBTARGETS; do
		n=$(basename "$(printf "%s\n" "$t" | tr '\\' /)" .lib)
		printf '%s\n' "cd /d %NT%\\$WDIR" "%MKINC% $t > %TOP%\\out\\$n\\$n.log 2>&1" \
			"copy $t %TOP%\\out\\$n\\$n.lib > nul"
	done
} | sed 's/$/\r/' > "$cmd"

# The objects of an earlier build go first (from here: the share's server
# can keep a file open for a while that the machine cannot then delete);
# with ONLY, those of the sources named and the library.
mkdir -p "$SHARE/out/$ON"
if [ -n "$ONLY" ]; then
	for b in $ONLY; do
		o=$(printf "%s\n" "${b%.*}" | tr A-Z a-z)
		rm -f "$SHARE/out/$ON/$o.obj" "$SHARE/out/$ON/$o.res" "$SHARE/out/$ON/$o.log" \
			"$SHARE/out/$ON/$o.rc.log" "$SHARE/out/$ON/$o.gen.log"
	done
	rm -f "$SHARE/out/$ON/$NAME.lib" "$SHARE/out/$ON/lib.log"
else
	rm -rf "$SHARE/out/$ON/um"; rm -f "$SHARE/out/$ON/"*
fi
for t in $LIBTARGETS; do
	n=$(basename "$(printf "%s\n" "$t" | tr '\\' /)" .lib)
	mkdir -p "$SHARE/out/$n"; rm -f "$SHARE/out/$n/"*
done
[ -z "$UMSRCS" ] || mkdir -p "$SHARE/out/$ON/um"
echo "build-lib: $DIR -> ${MKLIB:+$NAME.lib, }$(printf "%s\n" $SRCS | grep -c .) sources${UMSRCS:+, programs:$UMSRCS}"
vmrun "T:\\NTOS\\3MAX\\build\\$OF.cmd"

# Report the sources that did not compile, then check the objects.  The
# share's client can see the files the machine made a few seconds late (it
# keeps what it found missing for a while): wait for the library first.
out=$SHARE/out/$ON
i=0
if [ -n "$MKLIB" ]; then
	while [ ! -f "$out/$NAME.lib" ] && [ $i -lt 20 ]; do sleep 1; i=$((i + 1)); done
else
	# (no library: wait for the objects of the programs instead)
	for s in $UMSRCS; do
		b=$(basename "$(printf "%s\n" "$s" | tr '\\' /)"); o=$(printf "%s\n" "${b%.*}" | tr A-Z a-z)
		while [ ! -f "$out/um/$o.obj" ] && [ $i -lt 20 ]; do sleep 1; i=$((i + 1)); done
	done
fi
fail=0
# (the messages: "warning C4101", "fatal error U1083", "Error:"; not a name
# such as ..\error.c, which cl prints first)
[ -z "$(var NTTARGETFILE0)" ] || tr -d '\r' < "$out/nttarget0.log" | grep -iE '(error|warning)[ :]' || true
for s in $SRCS; do
	b=$(basename "$(printf "%s\n" "$s" | tr '\\' /)"); o=$(printf "%s\n" "${b%.*}" | tr A-Z a-z)
	# (a .rc source has a log of its own: it can have the name of a C source)
	case $s in *.[rR][cC]) x=res l=$o.rc ;; *) x=obj l=$o ;; esac
	if [ ! -f "$out/$o.$x" ]; then
		printf "%s\n" "--- $s failed:"
		[ ! -f "$out/$o.gen.log" ] || tr -d '\r' < "$out/$o.gen.log" | grep -v '^$' | head -10
		tr -d '\r' < "$out/$l.log" | grep -v '^$' | head -20
		fail=$((fail + 1))
	else
		tr -d '\r' < "$out/$l.log" | grep -iE '(warning|error)[ :]' | sed "s/^/$o: /" | head -10
	fi
done
for s in $UMSRCS; do
	b=$(basename "$(printf "%s\n" "$s" | tr '\\' /)"); o=$(printf "%s\n" "${b%.*}" | tr A-Z a-z)
	if [ ! -f "$out/um/$o.obj" ]; then
		printf "%s\n" "--- $s failed:"
		tr -d '\r' < "$out/um/$o.log" | grep -v '^$' | head -20
		fail=$((fail + 1))
	else
		tr -d '\r' < "$out/um/$o.log" | grep -iE '(warning|error)[ :]' | sed "s/^/$o: /" | head -10
	fi
done
if [ -n "$MKLIB" ]; then
	tr -d '\r' < "$out/lib.log"
	[ -f "$out/$NAME.lib" ] || { echo "build-lib: no $NAME.lib ($fail failed)"; exit 1; }
fi
# Docker gets copies of the objects: the files of the share it reads stay
# open in the SMB client for a while, and the next build cannot replace them.
mkdir -p "$here/obj"
rm -rf "$here/obj/$OF.o"; mkdir "$here/obj/$OF.o"
! ls "$out/"*.obj > /dev/null 2>&1 || cp "$out/"*.obj "$here/obj/$OF.o/"
! ls "$out/um/"*.obj > /dev/null 2>&1 || cp "$out/um/"*.obj "$here/obj/$OF.o/"
docker run --rm -v "$here":/work/3MAX -w "/work/3MAX/obj/$OF.o" --entrypoint sh 3max-build \
	-c 'perl /work/3MAX/tools/r3kcheck.pl *.obj' > "$here/obj/$OF.o/r3kcheck.txt" 2>&1 || true
grep -E '^[a-z0-9_]+\.obj:' "$here/obj/$OF.o/r3kcheck.txt" | grep -vE ': R3000, [0-9]+ instructions: clean$' || true
if [ -n "$MKLIB" ]; then
	echo "build-lib: $fail failed, $NAME.lib: $(ls -l "$out/$NAME.lib" | awk '{print $5}') bytes"
else
	echo "build-lib: $fail failed, no library ($(var TARGETTYPE))"
fi
for t in $LIBTARGETS; do
	n=$(basename "$(printf "%s\n" "$t" | tr '\\' /)" .lib)
	i=0
	while [ ! -f "$SHARE/out/$n/$n.lib" ] && [ $i -lt 20 ]; do sleep 1; i=$((i + 1)); done
	[ -f "$SHARE/out/$n/$n.lib" ] || { tr -d '\r' < "$SHARE/out/$n/$n.log"; echo "build-lib: no $n.lib"; exit 1; }
	echo "build-lib: $n.lib (NTTARGETFILES): $(ls -l "$SHARE/out/$n/$n.lib" | awk '{print $5}') bytes"
done
