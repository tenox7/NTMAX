#!/bin/sh
#
#  build-tools.sh -- build the tools of the NT tree that the 3MAX build runs
#  on the MIPS NT machine (vm.sh), as programs of that machine (its MSDEV
#  compiler and C library):
#
#	gensrv		the system service stubs and table of KE
#			(KE\UP\makefile.inc)
#	hextract	the header extractor and the message compiler of the
#	mc		header pass (build-gen.sh: NTOS\makefil0); mc writes its
#			strings with %ws, which only the C library of NT has:
#			it is built as makefile.def builds a console program,
#			with C900, the NT C headers and libc.lib of the tree
#	hsplit		the header splitter and the Unicode header filter of
#	wcshdr		the header pass of the Win32 headers (build-gen.sh:
#			WINDOWS\makefil0)
#	listmung	the list munger of the pass 0 of USER (USER\makefil0,
#			build-midl0.sh): the client/server API indices and
#			dispatch tables from the .lst lists and .tpl templates
#	mapmsg		the message file of the message definitions of a
#			header (NET\MESSAGES\makefile.inc: netmsg.mc from
#			lmerr.h, lmsvc.h, alertmsg.h ...)
#	solidpp		the id preprocessor of Solitaire
#			(SHELL\GAMES\SOL\makefile.inc: solid.h, sol.s and the
#			.msg files from the .txt lists), from the SOLIDPP.C
#			beside that makefile.inc
#	rcpp		the preprocessor of the resource compiler, which the
#			same makefile.inc runs on sol.rc; built as mc is
#	uixport		the export list of a NET UI DLL from the symbols of its
#			libraries (NET\UI\COMMON\SRC\DLL3\makefile.inc), which
#			that makefile.inc reads from "link32 -dump -decorate
#			-symbols": link32.cmd runs the dump of the linker of the
#			machine (MSDEV), which shows the decorated names unasked
#	binplace	the placer of the files that a makefile.inc puts in the
#			binaries tree (SDKTOOLS\PERFMON\makefile.inc:
#			perfc009.dat), from the BINPLACE.C of IMAGEHLP, with an
#			import library of the IMAGEHLP.DEF beside it (the machine's
#			imagehlp.dll has its two functions); build-lib.sh gives it
#			T:\NTOS\3MAX\out\binaries as the tree
#
#  The programs go to T:\NTOS\3MAX\host, which build-lib.sh puts in the PATH.

set -e
. "$(dirname "$0")/vm.sh"
ntcopy PRIVATE/SDKTOOLS/GENSRV PRIVATE/SDKTOOLS/HEXTRACT PRIVATE/SDKTOOLS/HSPLIT PRIVATE/SDKTOOLS/WCSHDR \
	PRIVATE/SDKTOOLS/LISTMUNG PRIVATE/SDKTOOLS/MAPMSG PRIVATE/SDKTOOLS/VCTOOLS/MC PUBLIC/SDK/INC \
	PUBLIC/SDK/LIB/MIPS PRIVATE/WINDOWS/SHELL/GAMES/SOL PRIVATE/SDKTOOLS/RCPP PRIVATE/SDKTOOLS/UIXPORT \
	PRIVATE/SDKTOOLS/IMAGEHLP
mkdir -p "$SHARE/host/rcpp" "$SHARE/build"
{
	cat <<-'EOF'
	@echo off
	set TOP=T:\NTOS\3MAX
	set NT=%TOP%\NT
	set PATH=%MSDevDir%\BIN;%SystemRoot%\system32
	set INCLUDE=%MSDevDir%\INCLUDE;%NT%\PUBLIC\SDK\INC
	set LIB=%MSDevDir%\LIB
	cd /d %TOP%\host
	cl -nologo -O2 -D_OS2_20_=0 -DNO_EXT_KEYS -Dnear= -Dfar= -Dregister= -DOS2 -DNPSZ=LPSTR -DCOMMAND=1 -DNODEB=1 -Dpascal= %NT%\PRIVATE\SDKTOOLS\GENSRV\gensrv.c -Fegensrv.exe > gensrv.log 2>&1
	cl -nologo -O2 %NT%\PRIVATE\SDKTOOLS\HEXTRACT\hextract.c -Fehextract.exe > hextract.log 2>&1
	cl -nologo -O2 %NT%\PRIVATE\SDKTOOLS\HSPLIT\hsplit.c -Fehsplit.exe > hsplit.log 2>&1
	cl -nologo -O2 -D_OS2_20_=0 -Dnear= -Dfar= -Dpascal= %NT%\PRIVATE\SDKTOOLS\WCSHDR\wcshdr.c -Fewcshdr.exe > wcshdr.log 2>&1
	cl -nologo -O2 -DMIPS=1 -D_MIPS_=1 -D_CRTAPI1=__cdecl %NT%\PRIVATE\SDKTOOLS\LISTMUNG\listmung.c -Felistmung.exe > listmung.log 2>&1
	cl -nologo -O2 -D_CRTAPI1=__cdecl %NT%\PRIVATE\SDKTOOLS\MAPMSG\mapmsg.c -Femapmsg.exe > mapmsg.log 2>&1
	cl -nologo -O2 %NT%\PRIVATE\WINDOWS\SHELL\GAMES\SOL\solidpp.c -Fesolidpp.exe > solidpp.log 2>&1
	cl -nologo -O2 %NT%\PRIVATE\SDKTOOLS\UIXPORT\uixport.c -Feuixport.exe > uixport.log 2>&1
	lib -nologo -machine:mips -def:%NT%\PRIVATE\SDKTOOLS\IMAGEHLP\imagehlp.def -out:imagehlp.lib > binplace.log 2>&1
	cl -nologo -O2 -DMIPS=1 %NT%\PRIVATE\SDKTOOLS\IMAGEHLP\binplace.c imagehlp.lib -Febinplace.exe >> binplace.log 2>&1
	set PATH=%TOP%\C900\BIN;%MSDevDir%\BIN;%SystemRoot%\system32
	set INCLUDE=%NT%\PUBLIC\SDK\INC\CRT;%NT%\PUBLIC\SDK\INC
	set LIB=%NT%\PUBLIC\SDK\LIB\MIPS
	cd /d %NT%\PRIVATE\SDKTOOLS\VCTOOLS\MC
	rc -r -fo%TOP%\host\mc.res mc.rc > %TOP%\host\mc.log 2>&1
	for %%f in (mc mclex mcparse mcout mcutil) do cl -nologo -c -Ox -I. -DMIPS=1 -D_MIPS_=1 -DWIN32=100 -DCONDITION_HANDLING=1 -DNT_UP=1 -DDBG=0 -DDEVL=1 -Fo%TOP%\host\%%f.obj %%f.c >> %TOP%\host\mc.log 2>&1
	cd /d %TOP%\host
	link -nologo -machine:mips -subsystem:console -entry:mainCRTStartup -out:mc.exe mc.obj mclex.obj mcparse.obj mcout.obj mcutil.obj mc.res libc.lib kernel32.lib user32.lib >> mc.log 2>&1
	cd /d %NT%\PRIVATE\SDKTOOLS\RCPP
	rc -r -fo%TOP%\host\rcpp\rcpp.res rcpp.rc > %TOP%\host\rcpp.log 2>&1
	for %%f in (charmap error getflags getmsg globals ltoa rcpp p0expr p0gettok p0io p0keys p0macros p0prepro p1sup rcpputil scanner tokens rcunicod) do cl -nologo -c -Ox -I. -I%NT%\PUBLIC\SDK\INC\OS2 -DMIPS=1 -D_MIPS_=1 -DWIN32=100 -DCONDITION_HANDLING=1 -DNT_UP=1 -DDBG=0 -DDEVL=1 -Fo%TOP%\host\rcpp\%%f.obj %%f.c >> %TOP%\host\rcpp.log 2>&1
	cd /d %TOP%\host
	link -nologo -machine:mips -subsystem:console -entry:mainCRTStartup -out:rcpp.exe rcpp\*.obj rcpp\rcpp.res libc.lib kernel32.lib >> rcpp.log 2>&1
	EOF
} | sed 's/$/\r/' > "$SHARE/build/tools.cmd"
# link32 -dump: the dump of the machine's linker (-decorate is not one of its
# options)
printf '%s\r\n' '@echo off' 'set A=' ':next' 'if "%1"=="" goto run' \
	'if /i not "%1"=="-decorate" set A=%A% %1' 'shift' 'goto next' ':run' \
	'%MSDevDir%\BIN\LINK.EXE %A%' > "$SHARE/host/link32.cmd"
tools="gensrv hextract hsplit wcshdr listmung mapmsg solidpp uixport binplace mc rcpp"
for t in $tools; do rm -f "$SHARE/host/$t.exe"; done
vmrun 'T:\NTOS\3MAX\build\tools.cmd'
for t in $tools; do
	# (the messages, not a source such as error.c, which cl prints first)
	tr -d '\r' < "$SHARE/host/$t.log" | grep -iE '(error|warning)[ :]' || true
done
# (the share shows a new file here a few seconds after the machine wrote it)
for f in $tools; do
	for i in 1 2 3 4 5 6 7 8 9 10; do [ ! -f "$SHARE/host/$f.exe" ] || break; sleep 1; done
	[ -f "$SHARE/host/$f.exe" ] || { echo "build-tools: no $f.exe"; exit 1; }
done
echo "build-tools: $(for t in $tools; do printf '%s.exe ' $t; done)"
