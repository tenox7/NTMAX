#!/bin/sh
#
#  build-gen.sh -- the header pass of the NT tree for the 3MAX build, run on
#  the MIPS NT machine (vm.sh) before the libraries, as the NT build runs it:
#
#	NTOS\makefil0	inc\bugcodes.h, init\bugcodes.rc and init\msg00001.bin
#			(mc), inc\ntddk.h, nthal.h, miniport.h, ntifs.h,
#			updriver.h, ndis.h and windbgkd.h (hextract, from the
#			headers with the R3000 versions of 3MAX/nt over them),
#			and with DD\makefil0 inc\ntiologc.h and the message
#			files of the drivers (made again each time: DD\makefil0
#			cleansrc first, as nmake does not see a new mc.exe and
#			takes a header an older mc wrote for up to date)
#	WINDOWS\makefil0
#			the headers of the Win32 API: the private ones of
#			WINDOWS\inc (winbasep.h, winuserp.h, wingdip.h,
#			winnlsp.h, windefp.h, conapi.h ...: hsplit and wcshdr
#			from the .w files) and the public ones of the SDK
#			(the tree has those: build-lib.sh copies them again)
#	ntstatus	NTOS\DLL\ntstatus.rc and msg00001.bin, the messages of
#			the NT status codes in NTDLL.DLL (mc, NLS\ntstatus.mc,
#			as the ntstatus rule of PRIVATE\makefil0 makes them)
#	netevent, msaudite, msobjs
#			the other message files of PRIVATE\makefil0's rules:
#			EVENTLOG\NETEVENT\netevent.rc (NLS\netevent.mc, the
#			messages of the system's events), NTOS\SEAUDIT\
#			MSAUDITE\msaudite.rc and MSOBJS\msobjs.rc (the
#			security audit messages), each with its msg00001.bin
#	MEDIA\AVI\makefil0
#			the headers of the AVI libraries in AVI\INC (msvideo.h,
#			drawdib.h, compman.h ...: copies of those of their
#			directories), the English resources of VIDEO and
#			MCIAVI32 and DRAWDIB\lockbm.c, the targets of its rule
#			all but the capture header (AVICAP: not built) and the
#			codecs it places (avifile.h and aviiface.h, of AVIFILE,
#			are included by COMPMAN and MCIAVI32)
#	generr		RTL\error.h, the NT status to DOS error table that
#			error.c includes, built (C900, crtdll.lib of the tree)
#			and run as RTL\USER\makefile.inc does
#
#  The files are made in the share's copy of the tree (T:\NTOS\3MAX\NT),
#  where the makefiles put them.  build-tools.sh makes hextract, hsplit,
#  wcshdr and mc.

set -e
. "$(dirname "$0")/vm.sh"
for t in hextract hsplit wcshdr mc; do
	[ -f "$SHARE/host/$t.exe" ] || { echo "build-gen: no $t.exe (build-tools.sh)"; exit 1; }
done
ntcopy PUBLIC/OAK/BIN PUBLIC/SDK/INC PRIVATE/NTOS/INC PRIVATE/NTOS/NLS PRIVATE/NTOS/INIT \
	PRIVATE/NTOS/DD PRIVATE/NTOS/RTL PRIVATE/NLS PRIVATE/NTOS/DLL \
	PUBLIC/OAK/INC PRIVATE/WINDOWS/INC PRIVATE/WINDOWS/NLSMSG \
	PRIVATE/EVENTLOG/NETEVENT PRIVATE/NTOS/SEAUDIT/MSAUDITE PRIVATE/NTOS/SEAUDIT/MSOBJS \
	PRIVATE/WINDOWS/MEDIA/AVI
cp "$nt35/PRIVATE/NTOS/MAKEFIL0" "$SHARE/NT/PRIVATE/NTOS/"	# (a file: ntcopy copies directories)
cp "$nt35/PRIVATE/WINDOWS/MAKEFIL0" "$SHARE/NT/PRIVATE/WINDOWS/"
mkdir -p "$SHARE/NT/PRIVATE/WINDOWS/MEDIA/WINMM"	# (media\winmm\mmsystem.h)
out=$SHARE/out/gen
mkdir -p "$out" "$SHARE/build"
rm -f "$out/"* "$SHARE/NT/PRIVATE/NTOS/RTL/error.h"
{
	cat <<-'EOF'
	@echo off
	set TOP=T:\NTOS\3MAX
	set NT=%TOP%\NT
	set PATH=%TOP%\host;%MSDevDir%\BIN;%SystemRoot%\system32
	set INCLUDE=
	set LIB=%MSDevDir%\LIB
	set NTMAKEENV=%NT%\PUBLIC\OAK\BIN
	set _NTDRIVE=T:
	set _NTROOT=\NTOS\3MAX\NT
	set MIPS=1
	set O=%TOP%\out\gen
	cd /d %NT%\PRIVATE\NTOS\DD
	nmake -nologo -f makefil0 cleansrc > %O%\ddclean.log 2>&1
	cd /d %NT%\PRIVATE\NTOS
	nmake -nologo -f makefil0 > %O%\makefil0.log 2>&1
	cd /d %NT%\PRIVATE\WINDOWS
	nmake -nologo -f makefil0 > %O%\winmakefil0.log 2>&1
	cd /d %NT%\PRIVATE\WINDOWS\MEDIA\AVI
	nmake -nologo -f makefil0 inc\msvideo.h inc\ivideo32.h inc\msviddrv.h inc\mciavi.h inc\profile.h inc\drawdib.h inc\compman.h inc\compddk.h inc\icm.rc inc\dith775.h inc\aviiface.h inc\avifile.h inc\mciwnd.h inc\preview.h mciavi32\mciavi.rc mciavi32\mciavi.rcv mciavi32\cnfgdlg.dlg video\msvideo.rc video\msvideo.rcv drawdib\lockbm.c > %O%\avimakefil0.log 2>&1
	cd /d %NT%\PRIVATE
	mc -v -r ntos\dll\ -h %NT%\PUBLIC\SDK\INC\ -x %NT%\PUBLIC\SDK\INC\ nls\ntstatus.mc > %O%\ntstatus.log 2>&1
	mc -v -r eventlog\netevent\ -h %NT%\PUBLIC\SDK\INC\ -x %NT%\PUBLIC\SDK\INC\ nls\netevent.mc > %O%\netevent.log 2>&1
	mc -v -r ntos\seaudit\msaudite\ -h %NT%\PUBLIC\SDK\INC\ ntos\seaudit\msaudite\msaudite.mc > %O%\msaudite.log 2>&1
	mc -v -r ntos\seaudit\msobjs\ -h %NT%\PUBLIC\SDK\INC\ ntos\seaudit\msobjs\msobjs.mc > %O%\msobjs.log 2>&1
	set PATH=%TOP%\C900\BIN;%MSDevDir%\BIN;%SystemRoot%\system32
	set INCLUDE=%NT%\PRIVATE\NTOS\RTL;%NT%\PRIVATE\NTOS\INC;%NT%\PRIVATE\INC;%NT%\PUBLIC\SDK\INC\CRT;%NT%\PUBLIC\SDK\INC
	set LIB=%NT%\PUBLIC\SDK\LIB\MIPS
	cd /d %O%
	cl -nologo -c -Ox -DMIPS=1 -D_MIPS_=1 -DR3000 -DNT_UP=1 -DDBG=0 -DDEVL=1 -DCONDITION_HANDLING=1 -DNT_INST=0 -DWIN32=100 -D_NT1X_=100 -D_DLL=1 -D_MT=1 -D_NTSYSTEM_ -Fogenerr.obj %NT%\PRIVATE\NTOS\RTL\generr.c > generr.log 2>&1
	link -nologo -machine:mips -subsystem:console -entry:mainCRTStartup -out:generr.exe generr.obj crtdll.lib kernel32.lib >> generr.log 2>&1
	if exist generr.exe generr.exe %NT%\PRIVATE\NTOS\RTL\error.h >> generr.log 2>&1
	EOF
} | sed 's/$/\r/' > "$SHARE/build/gen.cmd"
vmrun 'T:\NTOS\3MAX\build\gen.cmd'
tr -d '\r' < "$out/makefil0.log" | grep -i 'error\|warning\|stop' || true
tr -d '\r' < "$out/winmakefil0.log" | grep -i 'error\|warning\|stop' || true
tr -d '\r' < "$out/avimakefil0.log" | grep -i 'error\|warning\|stop' || true
for l in ntstatus netevent msaudite msobjs; do
	tr -d '\r' < "$out/$l.log" | grep -i 'error\|warning' || true
done
tr -d '\r' < "$out/generr.log" | grep -i 'error\|warning' | grep -v 'error\.h' || true

# (the share shows a new file here a few seconds after the machine wrote it)
miss=
for f in PRIVATE/NTOS/INC/bugcodes.h PRIVATE/NTOS/INIT/bugcodes.rc PRIVATE/NTOS/INIT/msg00001.bin \
	PRIVATE/NTOS/INC/ntddk.h PRIVATE/NTOS/INC/nthal.h PRIVATE/NTOS/INC/miniport.h \
	PRIVATE/NTOS/INC/ntifs.h PRIVATE/NTOS/INC/updriver.h PRIVATE/NTOS/INC/ndis.h \
	PRIVATE/NTOS/INC/ntiologc.h PRIVATE/NTOS/DLL/ntstatus.rc PRIVATE/NTOS/DLL/msg00001.bin \
	PRIVATE/EVENTLOG/NETEVENT/netevent.rc PRIVATE/EVENTLOG/NETEVENT/msg00001.bin \
	PRIVATE/NTOS/SEAUDIT/MSAUDITE/msaudite.rc PRIVATE/NTOS/SEAUDIT/MSAUDITE/msg00001.bin \
	PRIVATE/NTOS/SEAUDIT/MSOBJS/msobjs.rc PRIVATE/NTOS/SEAUDIT/MSOBJS/msg00001.bin \
	PRIVATE/NTOS/RTL/error.h PRIVATE/WINDOWS/INC/winbasep.h PRIVATE/WINDOWS/INC/winuserp.h \
	PRIVATE/WINDOWS/INC/wingdip.h PRIVATE/WINDOWS/INC/winnlsp.h PRIVATE/WINDOWS/INC/windefp.h \
	PRIVATE/WINDOWS/INC/conapi.h PRIVATE/WINDOWS/MEDIA/AVI/INC/msvideo.h \
	PRIVATE/WINDOWS/MEDIA/AVI/DRAWDIB/lockbm.c; do
	for i in 1 2 3 4 5 6 7 8 9 10; do [ ! -f "$SHARE/NT/$f" ] || break; sleep 1; done
	[ -f "$SHARE/NT/$f" ] || miss="$miss $f"
done
[ -z "$miss" ] || { echo "build-gen: not made:$miss"; exit 1; }
printf '%s\n' 'build-gen: NTOS\makefil0 and WINDOWS\makefil0 headers, the message files (ntstatus, netevent,' \
	'msaudite, msobjs), the AVI headers, RTL\error.h'
