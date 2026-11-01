#!/bin/sh
#
#  build-image.sh -- build the images of a directory of the NT tree for the
#  R3000 the way makefile.def builds them:
#
#	build-image.sh DIR
#
#  The image of the TARGETTYPE: DRIVER, MINIPORT and EXPORT_DRIVER make a
#  driver (obj/drivers/TARGETNAME.sys), DYNLINK a DLL
#  (obj/system32/TARGETNAME.dll, or the extension TARGETEXT names), PROGRAM
#  a program (obj/system32/TARGETNAME.exe); LIBRARY and UMAPPL_NOLIB make
#  none.  Then each program of UMAPPL (obj/system32/PROGRAM.exe, or the
#  extension of UMAPPLEXT).  build-lib.sh compiles DIR: the objects and .res
#  files go to T:\NTOS\3MAX\out\TARGETNAME, the objects of the UMAPPL
#  programs to its um directory.  The library of the objects that it makes
#  is removed unless TARGETTYPE is LIBRARY: an image has none, and the
#  programs of a LIBRARY link with it.  Then, on the MIPS NT machine (vm.sh),
#  an image with exports (EXPORT_DRIVER, DYNLINK) first gets its import
#  library and export file from its DLLDEF (lib -def, without the objects,
#  as LIBRARY_OBJS of MIPSMK.INC is empty, and with the DLLLIBOBJECTS added
#  to the import library; a DLLDEF that neither the tree nor the
#  NTTARGETFILE0 rules of build-lib.sh made is made from TARGETNAME.src with
#  the preprocessor), and the thunks of the import library are made R3000
#  code (r3kimplib.pl).  The images are linked with the linker options of
#  makefile.def (LINKER_FLAGS, -debug:MINIMAL and the debug type of
#  NTDEBUGTYPE) and the rule of their kind, with the inputs in the order of
#  that rule:
#
#	a driver	-align:0x20, -base:0x10000 and -entry:DriverEntry,
#			then TARGETLIB (the import libraries of the kernel and
#			the HAL of build-kernel.sh; none for a miniport) and
#			TARGETLIBS, an export driver also with its export file
#			and LINKLIBS
#	a DLL		-dll, the .prf order of NTPROFILEINPUT, the base of
#			coffbase.txt (or DLLBASE), the subsystem of UMTYPE and
#			DLLENTRY, then its export file, its objects, LINKLIBS,
#			TARGETLIB (NTDLL's import library, and CRTDLL's with
#			USE_CRTDLL; none for NTDLL) and TARGETLIBS
#	a program	the subsystem, the entry and the libraries of UMTYPE
#			(nt: NtProcessStartup, ntdll.lib and nt.lib; ntss the
#			same and smdll.lib; console: mainCRTStartup, or
#			WinMainCRTStartup with UMENTRY winmain, the C library
#			of makefile.def (libc.lib, libcmt.lib for USE_LIBCMT,
#			crtdll.lib for USE_CRTDLL), advapi32.lib and
#			kernel32.lib; windows the same and gdi32.lib and
#			user32.lib), the base of coffbase.txt for COFFBASE (or
#			for usermode, or UMBASE), -gpsize for GPSIZE and
#			LNKFLAGS; a PROGRAM then UMRES, its objects, CRTLIBS,
#			UMLIBS and the libraries of UMTYPE, TARGETLIBS and
#			LINKLIBS; a program of UMAPPL with the .prf order of
#			EXEPROFILEINPUT, then UMRES, its object, UMOBJS, UMLIBS
#			and the libraries of UMTYPE, CRTLIBS and LINKLIBS
#
#  An export list with exports marked PRIVATE (user32.def, gdi32.def) also
#  gives the private import library NAMEp.lib, of all the exports, as the
#  makefile.inc of USER\CLIENT and GDI\CLIENT makes it (the export list
#  through the preprocessor with PRIVATE empty); winsrv and the two DLLs
#  link with it.  With IMPLIB=1 the script makes the import libraries only,
#  without compiling or linking: user32 and gdi32 import from each other, as
#  their import libraries in the PUBLIC\SDK\LIB of the tree let them.
#
#  A library X.lib is the import library of the image X when build-image.sh
#  made one, else the library X of build-lib.sh or build-crt.sh, or the
#  kernel's (build-kernel.sh); a library of a directory of the tree
#  (DIR\obj\*\X.lib) is the one that build-lib.sh put in the obj\mips of
#  that directory, as two directories can make a library of one name (the
#  client and the server of WINREG); an object or a .res file (DIR\obj\*\NAME.obj,
#  of this directory or another) is the one build-lib.sh made for its
#  directory.  The images are checked with r3kcheck.pl and marked as R3000
#  images (r3kmark.pl): the linker writes R4000, and an R3000 kernel maps
#  only R3000 images into a process.  An image, its map, and the import
#  library and export file of an image with exports stay in
#  T:\NTOS\3MAX\out\img\IMAGE (the images that link with them find them
#  there).  make-disk.sh puts the drivers in \winnt\system32\drivers and the
#  other images in \winnt\system32.

set -e
. "$(dirname "$0")/vm.sh"
[ $# -eq 1 ] || { echo "usage: build-image.sh DIR" >&2; exit 1; }
DIR=${1%/}
WDIR=$(printf "%s\n" "$DIR" | tr / '\\')

IMPLIB=${IMPLIB:-0}
if [ "$IMPLIB" = 1 ]; then
	# the files that build-lib.sh copies for the SOURCES file and the
	# export list (of this directory, the one above or the MIPS ones)
	ntcopy "$DIR"
	ntcopytop "$(dirname "$DIR")"
	for d in "$DIR/MIPS" "$(dirname "$DIR")/MIPS"; do
		[ ! -d "$nt35/$d" ] && [ ! -d "$here/nt/$d" ] || ntcopy "$d"
	done
	mkdir -p "$SHARE/build"
else
	# a source that did not compile stops the build here, as nmake stops
	# at its error: no image is linked without it
	r=
	"$here/build/build-lib.sh" "$DIR" > "$here/obj/build-lib.$$.log" 2>&1 || r=$?
	cat "$here/obj/build-lib.$$.log"
	if [ -n "$r" ] || grep -q '^build-lib: [1-9][0-9]* failed' "$here/obj/build-lib.$$.log"; then
		rm -f "$here/obj/build-lib.$$.log"
		echo "build-image: $DIR: build-lib.sh failed, no image" >&2; exit 1
	fi
	rm -f "$here/obj/build-lib.$$.log"
fi
ntcopytop PUBLIC/SDK/LIB	# (coffbase.txt)

vars=$(ntsources "$DIR" TARGETNAME TARGETTYPE TARGETEXT TARGETLIBS LINKLIBS DLLDEF DLLENTRY DLLBASE \
	MIPS_DLLLIBOBJECTS UMTYPE USE_CRTDLL USE_LIBCMT NTPROFILEINPUT DRIVER_ALIGNMENT NTDEBUGTYPE GPSIZE \
	UMAPPL UMAPPLEXT UMLIBS UMRES UMOBJS UMENTRY UMBASE COFFBASE LNKFLAGS CRTLIBS EXEPROFILEINPUT)
var() { printf '%s\n' "$vars" | sed -n "s/^$1=//p" | sed 's/^ *//; s/ *$//'; }
NAME=$(var TARGETNAME)
TYPE=$(var TARGETTYPE)
case $TYPE in
DRIVER|MINIPORT|EXPORT_DRIVER) EXT=sys ;;
DYNLINK) EXT=$(var TARGETEXT); EXT=${EXT:-dll} ;;
PROGRAM) EXT=exe ;;
LIBRARY|UMAPPL_NOLIB) EXT= ;;
*)	echo "build-image: $DIR: TARGETTYPE $TYPE is not an image this script links" >&2; exit 1 ;;
esac
UMAPPL=$(var UMAPPL | tr '*' ' ')
[ -n "$EXT" ] || [ -n "$UMAPPL" ] || { echo "build-image: $DIR: a $TYPE without UMAPPL has no image" >&2; exit 1; }
[ "$IMPLIB" != 1 ] || [ $TYPE = DYNLINK ] || [ $TYPE = EXPORT_DRIVER ] ||
	{ echo "build-image: $DIR: a $TYPE has no import library" >&2; exit 1; }
[ $TYPE = LIBRARY ] || [ "$IMPLIB" = 1 ] || rm -f "$SHARE/out/$NAME/$NAME.lib"

# lib L: the library L (a name, or a path of SOURCES) as the machine finds it.
# A library of a directory of the tree (DIR\obj\*\NAME.lib) is the one in the
# obj\mips of that directory, where build-lib.sh puts a library of
# TARGETPATH obj: the client and the server of WINREG both make winreg.lib.
lib() {
	p=$(printf "%s\n" "$1" | tr '\\' /)
	n=$(basename "$p" .lib | tr A-Z a-z)
	case $p in
	*/[oO][bB][jJ]/\*/*|[oO][bB][jJ]/\*/*)
		d=$(ntpath "$(dirname "$(dirname "$(dirname "$p")")")")
		f=$(ls "$SHARE/NT/$d/obj/mips" 2>/dev/null | grep -ix "$(basename "$p")" | head -1)
		[ -z "$d" ] || [ -z "$f" ] ||
			{ printf '%s\n' "%NT%\\$(printf "%s\n" "$d" | tr / '\\')\\obj\\mips\\$f"; return; } ;;
	esac
	for d in "img/$n" "$n" ntoskrnl; do
		[ ! -f "$SHARE/out/$d/$n.lib" ] || { printf '%s\n' "%L%\\$(printf "%s\n" "$d" | tr / '\\')\\$n.lib"; return; }
	done
	echo "build-image: $DIR: no library $n (build-image.sh, build-lib.sh, build-kernel.sh or build-crt.sh)" >&2; exit 1
}

# obj O: the object or .res file O of SOURCES (DIR\obj\*\NAME.obj, of this
# directory or another) in the output directory of its directory's library
obj() {
	o=$(printf "%s\n" "$1" | tr '\\' / | sed 's,/\*/,/mips/,')
	# the objects of the C library that the CRT puts beside the libraries
	# of the SDK (setargv.obj, binmode.obj): build-crt32.sh ST leaves them
	# with libc.lib
	case $(printf "%s\n" "$o" | tr A-Z a-z) in
	*public/sdk/lib/mips/*.obj)
		f=$(basename "$o" | tr A-Z a-z)
		[ -f "$SHARE/out/libc/$f" ] ||
			{ echo "build-image: $DIR: no object $1 (build-crt32.sh ST)" >&2; exit 1; }
		printf '%s\n' "%L%\\libc\\$f"; return ;;
	esac
	d=$(dirname "$(dirname "$(dirname "$o")")")	# (the directory above obj/mips)
	d=$(printf "%s/%s\n" "$DIR" "$d" | awk -F/ '{ n = 0; for (i = 1; i <= NF; i++) {
		if ($i == "..") n--; else if ($i != "." && $i != "") w[++n] = $i }
		for (i = 1; i <= n; i++) printf "%s%s", (i > 1 ? "/" : ""), w[i] }')
	if [ "$d" = "$DIR" ]; then
		t=$NAME
	else
		t=$(ntsources "$d" TARGETNAME | sed -n 's/^TARGETNAME=//p' | sed 's/ *$//')
	fi
	f=$(basename "$o" | tr A-Z a-z)
	[ -n "$t" ] && [ -f "$SHARE/out/$t/$f" ] ||
		{ echo "build-image: $DIR: no object $1 (build-lib.sh $d)" >&2; exit 1; }
	printf '%s\n' "%L%\\$t\\$f"
}

# files F...: the libraries (lib), objects and .res files (obj) F
files() {
	for f in "$@"; do
		case $f in
		*.[lL][iI][bB]) x=$(lib "$f") ;;
		*) x=$(obj "$f") ;;
		esac
		printf ' %s' "$x"
	done
}

case $(var NTDEBUGTYPE) in
windbg)	dbg=cv ;;
both)	dbg=both ;;
*)	dbg=coff ;;
esac

# The subsystem, the entry and the libraries of UMTYPE.
GPSIZE=$(var GPSIZE); GPSIZE=${GPSIZE:-0}
gp=
[ "$GPSIZE" = 0 ] || gp=-gpsize:$GPSIZE
libc=libc
[ -z "$(var USE_LIBCMT)" ] || libc=libcmt
[ -z "$(var USE_CRTDLL)" ] || libc=crtdll
[ "$GPSIZE" = 0 ] || libc="small $libc"
case $(var UMTYPE) in
''|nt)	sub=native; umentry=-entry:NtProcessStartup; umlibs="ntdll nt" ;;
ntss)	sub=native; umentry=-entry:NtProcessStartup; umlibs="ntdll nt smdll" ;;
console|windows)
	sub=$(var UMTYPE)
	umentry=-entry:mainCRTStartup
	[ "$(var UMENTRY)" != winmain ] || umentry=-entry:WinMainCRTStartup
	umlibs="$libc advapi32 kernel32"
	[ $sub = console ] || umlibs="$umlibs gdi32 user32" ;;
*)	echo "build-image: $DIR: UMTYPE $(var UMTYPE) is not one this script links" >&2; exit 1 ;;
esac
umbase=$(var UMBASE)
if [ -z "$umbase" ]; then
	cb=$(var COFFBASE)
	umbase="@%NT%\\PUBLIC\\SDK\\LIB\\coffbase.txt,${cb:-usermode}"
fi

# cmdhead IMAGE: the start of a .cmd file of the image IMAGE
cmdhead() {
	cat <<-'EOF'
	@echo off
	set TOP=T:\NTOS\3MAX
	set NT=%TOP%\NT
	set PATH=%TOP%\C900\BIN;C:\MSDEV\BIN;C:\WINNT\system32
	set INCLUDE=
	set L=%TOP%\out
	EOF
	printf '%s\n' "set O=%L%\\img\\$1" "cd /d %NT%\\$WDIR"
}

# linkimg FILE DEST OPTIONS INPUTS: link the image FILE (IMAGE.EXT) in
# T:\NTOS\3MAX\out\img\IMAGE, then check it and mark it R3000 in obj/DEST.
# NT's link32 kept the first definition of a symbol that an object and a
# library both define, link 3.10 does it with -force:multiple (as for the
# kernel, build-kernel.sh): libcntpr's memorym.obj defines memcpy, memmove
# and memset again, which rtl's xxmvmem.obj has (not shown); kernel32's
# muldiv.obj defines RtlEnlargedUnsignedDivide, and the member of
# ntdll.lib that lcompat.c imports it with has its thunk of that name.
linkimg() {
	img=${1%.*}
	io=$SHARE/out/img/$img
	force=' -force:multiple'
	# The options and the inputs go in a response file, one to a line, as
	# makefile.def links with @<<: the command line of an image of many
	# libraries (netapi32) is longer than cmd takes ("The input line is too
	# long").  The variables of cmdhead are put in by their values.
	printf '%s\n' -nologo -MERGE:.CRT=.data -MERGE:_PAGE=PAGE -MERGE:_TEXT=.text -SECTION:INIT,d -OPT:REF -PDB:NONE \
		-RELEASE -INCREMENTAL:NO -NODEFAULTLIB -IGNORE:4037,4065$force -debug:MINIMAL -debugtype:$dbg -machine:mips \
		$3 -out:%O%\\$1 -map:%O%\\$img.map $4 |
		sed -e "s/%O%/%L%\\\\img\\\\$img/g" -e 's/%L%/%TOP%\\out/g' -e 's/%NT%/%TOP%\\NT/g' \
		    -e 's/%TOP%/T:\\NTOS\\3MAX/g' -e 's/$/\r/' > "$SHARE/build/$img-link.rsp"
	{
		cmdhead "$img"
		printf '%s\n' "rem $img-link.cmd -- made by build-image.sh: link $1 for the R3000" \
			"C:\\MSDEV\\BIN\\LINK.EXE @%TOP%\\build\\$img-link.rsp > %O%\\link.log 2>&1"
	} | sed 's/$/\r/' > "$SHARE/build/$img-link.cmd"
	vmrun "T:\\NTOS\\3MAX\\build\\$img-link.cmd"
	n=0
	while [ ! -f "$io/link.log" ] && [ $n -lt 20 ]; do sleep 1; n=$((n + 1)); done
	tr -d '\r' < "$io/link.log" | grep -v '^$' | grep -v 'LNK4087\|LNK4088' |
		grep -v 'LNK4006: mem\(cpy\|move\|set\) already defined' | sed "s/^/link $1: /" || true
	[ -f "$io/$1" ] || { echo "build-image: no $1"; exit 1; }
	mkdir -p "$here/obj/$2"
	cp "$io/$1" "$io/$img.map" "$here/obj/$2/"
	docker run --rm -v "$here":/work/3MAX -w /work/3MAX --entrypoint sh 3max-build \
		-c "perl tools/r3kcheck.pl obj/$2/$1 && perl tools/r3kmark.pl obj/$2/$1" \
		> "$here/obj/$2/$img-r3k.txt" 2>&1 || true
	grep -F "$1:" "$here/obj/$2/$img-r3k.txt" || cat "$here/obj/$2/$img-r3k.txt"
	ls -l "$here/obj/$2/$1"
}

# The image of TARGETTYPE.
if [ -n "$EXT" ]; then
	out=$SHARE/out/img/$NAME
	mkdir -p "$out"
	if [ "$IMPLIB" = 1 ]; then
		rm -f "$out/$NAME.lib" "$out/$NAME.exp" "$out/def.log" "$out/lib.log"
	else
		rm -f "$out/"*
	fi

	# TARGETLIB of makefile.def, then the libraries in the order of the rule.
	[ "$IMPLIB" != 1 ] || TYPE0=$TYPE TYPE=IMPLIB
	case $TYPE in
	DYNLINK)
		tl=
		if [ "$NAME" != ntdll ]; then
			[ -z "$(var USE_CRTDLL)" ] || tl=crtdll
			tl="$tl ntdll"
		fi
		list="$(var LINKLIBS) $tl $(var TARGETLIBS)" ;;
	EXPORT_DRIVER)
		list="ntoskrnl hal $(var TARGETLIBS) $(var LINKLIBS)" ;;
	DRIVER)	list="ntoskrnl hal $(var TARGETLIBS)" ;;
	MINIPORT) list="$(var TARGETLIBS)" ;;
	PROGRAM) list="$(var CRTLIBS) $(var UMLIBS) $umlibs $(var TARGETLIBS) $(var LINKLIBS)" ;;
	IMPLIB)	list=; TYPE=$TYPE0 ;;
	esac
	libs=
	for l in $list; do
		case $l in
		*.*)	libs="$libs$(files "$l")" ;;
		*)	libs="$libs $(lib "$l")" ;;	# (a library of the rule, by its name)
		esac
	done
	res=
	for r in "$SHARE/out/$NAME/"*.res; do
		[ "$IMPLIB" != 1 ] && [ -f "$r" ] && res="$res %L%\\$NAME\\$(basename "$r")"
	done
	order=
	if [ -n "$(var NTPROFILEINPUT)" ]; then
		prf=$(ls "$SHARE/NT/$DIR" | grep -ix "$NAME.prf" | head -1)
		[ -z "$prf" ] || order="-order:@$prf"
	fi

	# An image with exports: its import library and export file.
	exp=
	if [ $TYPE = EXPORT_DRIVER ] || [ $TYPE = DYNLINK ]; then
		# (makefile.def: DLLDEF=$(@B).def, the target's base name)
		def=$(printf "%s\n" "$(var DLLDEF)" | sed 's/\*/mips/')
		def=${def:-$NAME.def}
		extra=
		for o in $(var MIPS_DLLLIBOBJECTS); do
			extra="$extra $(obj "$o")"
		done
		# A DLLDEF that the tree does not have is made by the preprocessor
		# from the .src (or .def) of its name in the directory above or
		# this one, as the inference rules of makefile.def take it
		# (kernel32: ..\kernel32.src).
		src=
		if [ ! -f "$SHARE/NT/$DIR/$(printf "%s\n" "$def" | tr '\\' /)" ]; then
			b=$(basename "$(printf "%s\n" "$def" | tr '\\' /)" .def)
			for s in "../$b.src" "$b.src" "../$b.def" "$b.def"; do
				m=$(ls "$SHARE/NT/$DIR/$(dirname "$s")" 2>/dev/null | grep -ix "$(basename "$s")" | head -1)
				[ -z "$m" ] || { src=$(printf "%s\n" "$(dirname "$s")/$m" | sed 's,^\./,,' | tr / '\\'); break; }
			done
			[ -n "$src" ] || { echo "build-image: $DIR: no export list $b.src or $b.def" >&2; exit 1; }
		fi
		pdef=$(printf "%s\n" "${src:-$def}" | tr '\\' /)
		priv=
		! LC_ALL=C command grep -aqw PRIVATE "$SHARE/NT/$DIR/$pdef" 2>/dev/null || priv=${NAME}p
		{
			cmdhead "$NAME"
			printf '%s\n' "rem $NAME.cmd -- made by build-image.sh: the import library of $NAME.$EXT"
			if [ -n "$src" ]; then
				printf '%s\n' "cl -nologo -EP -DMIPS=1 -D_MIPS_ -DR3000 -Tc$src > %O%\\$b.def 2> %O%\\def.log"
				def="%O%\\$b.def"
			fi
			if [ -z "$extra" ]; then
				printf '%s\n' "C:\\MSDEV\\BIN\\LIB.EXE -nologo -machine:mips -def:$def -out:%O%\\$NAME.lib > %O%\\lib.log 2>&1"
			else
				# makefile.def: the DLLLIBOBJECTS go in the import library
				printf '%s\n' "C:\\MSDEV\\BIN\\LIB.EXE -nologo -machine:mips -def:$def -out:%O%\\tmp.lib > %O%\\lib.log 2>&1" \
					"ren %O%\\tmp.exp $NAME.exp" \
					"C:\\MSDEV\\BIN\\LIB.EXE -nologo -machine:mips -out:%O%\\$NAME.lib %O%\\tmp.lib$extra >> %O%\\lib.log 2>&1" \
					"del %O%\\tmp.lib"
			fi
			if [ -n "$priv" ]; then
				# the private import library: all the exports
				printf '%s\n' "cl -nologo -EP -DPRIVATE= -DMIPS=1 -D_MIPS_ -DR3000 -Tc$def > %O%\\$priv.def 2> %O%\\defp.log" \
					"C:\\MSDEV\\BIN\\LIB.EXE -nologo -machine:mips -def:%O%\\$priv.def -out:%O%\\$priv.lib > %O%\\libp.log 2>&1"
			fi
		} | sed 's/$/\r/' > "$SHARE/build/$NAME.cmd"
		vmrun "T:\\NTOS\\3MAX\\build\\$NAME.cmd"
		i=0
		while [ ! -f "$out/$NAME.lib" ] && [ $i -lt 20 ]; do sleep 1; i=$((i + 1)); done
		for f in def lib; do
			[ ! -f "$out/$f.log" ] || tr -d '\r' < "$out/$f.log" | grep -v '^$' | sed "s/^/$f: /" || true
		done
		[ -f "$out/$NAME.lib" ] || { echo "build-image: no $NAME.lib"; exit 1; }
		# (Docker gets a copy, see build-lib.sh)
		rm -rf "$here/obj/implib/$NAME"; mkdir -p "$here/obj/implib/$NAME"
		cp "$out/$NAME.lib" "$here/obj/implib/$NAME/"
		docker run --rm -v "$here":/work/3MAX -w "/work/3MAX/obj/implib/$NAME" --entrypoint sh 3max-build \
			-c "perl /work/3MAX/tools/r3kimplib.pl $NAME.lib r3k.lib"
		cp "$here/obj/implib/$NAME/r3k.lib" "$out/$NAME.lib"
		exp="%O%\\$NAME.exp"
		if [ -n "$priv" ]; then
			i=0
			while [ ! -f "$out/$priv.lib" ] && [ $i -lt 20 ]; do sleep 1; i=$((i + 1)); done
			for f in defp libp; do
				[ ! -f "$out/$f.log" ] || tr -d '\r' < "$out/$f.log" | grep -v '^$' | sed "s/^/$f: /" || true
			done
			[ -f "$out/$priv.lib" ] || { echo "build-image: no $priv.lib"; exit 1; }
			# (as an image of that name: lib finds it in img/NAMEp)
			cp "$out/$priv.lib" "$here/obj/implib/$NAME/"
			docker run --rm -v "$here":/work/3MAX -w "/work/3MAX/obj/implib/$NAME" --entrypoint sh 3max-build \
				-c "perl /work/3MAX/tools/r3kimplib.pl $priv.lib r3kp.lib"
			mkdir -p "$SHARE/out/img/$priv"
			cp "$here/obj/implib/$NAME/r3kp.lib" "$SHARE/out/img/$priv/$priv.lib"
			rm -f "$out/$priv.lib"
		fi
		[ "$IMPLIB" != 1 ] || { echo "build-image: $NAME.lib${priv:+ and $priv.lib} (the import libraries)"; exit 0; }
	fi

	case $TYPE in
	DYNLINK)
		case $(var UMTYPE) in windows) dsub=windows ;; console) dsub=console ;; *) dsub=native ;; esac
		base=$(var DLLBASE); base=${base:-"@%NT%\\PUBLIC\\SDK\\LIB\\coffbase.txt,$NAME"}
		entry=$(var DLLENTRY); entry=${entry:+-entry:$entry}; entry=${entry:--noentry}
		linkimg "$NAME.$EXT" system32 "-dll $order -base:$base -subsystem:$dsub $entry" "$exp %L%\\$NAME\\*.obj$res$libs" ;;
	PROGRAM)
		umres=$(files $(var UMRES))
		linkimg "$NAME.$EXT" system32 "-subsystem:$sub -base:$umbase $gp $umentry $(var LNKFLAGS)" \
			"$umres %L%\\$NAME\\*.obj$res$libs" ;;
	*)
		align=$(var DRIVER_ALIGNMENT)
		linkimg "$NAME.$EXT" drivers "$order -align:${align:-0x20} -subsystem:native -debug:notmapped -base:0x10000 -entry:DriverEntry" \
			"$exp %L%\\$NAME\\*.obj$res$libs" ;;
	esac
fi

# The programs of UMAPPL: the object of each is the one build-lib.sh
# compiled into its um directory, or the object of SOURCES of its name.
ext=$(var UMAPPLEXT); ext=${ext:-.exe}
for u in $UMAPPL; do
	u=$(printf "%s\n" "$u" | tr A-Z a-z)
	if [ "$u" != "$NAME" ] || [ -z "$EXT" ]; then
		mkdir -p "$SHARE/out/img/$u"; rm -f "$SHARE/out/img/$u/"*
	fi
	po="%L%\\$NAME\\$u.obj"
	[ ! -f "$SHARE/out/$NAME/um/$u.obj" ] || po="%L%\\$NAME\\um\\$u.obj"
	order=
	[ -z "$(var EXEPROFILEINPUT)" ] || order="-order:@$u.prf"
	ulibs=
	for l in $umlibs; do ulibs="$ulibs $(lib "$l")"; done
	# (each list by itself: a failing lookup ends the script)
	umres=$(files $(var UMRES)); umobjs=$(files $(var UMOBJS)); uml=$(files $(var UMLIBS))
	crt=$(files $(var CRTLIBS)); ll=$(files $(var LINKLIBS))
	linkimg "$u$ext" system32 "$order $gp -base:$umbase -subsystem:$sub $umentry $(var LNKFLAGS)" \
		"$umres $po$umobjs$uml$ulibs$crt$ll"
done
