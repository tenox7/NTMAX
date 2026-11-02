# NTMAX — Windows NT 3.5 on the DECstation 5000/200

A port of Windows NT 3.5 (build 782) to the DECstation 5000/200 (3MAX: the
KN02 system board, a MIPS R3000, TURBOchannel), run in the gxemul emulator.
The NT components are built from the NT 3.5 source tree for the R3000. The
parts that this machine needs and the tree does not have are written for the
port: the ARC firmware, the R3000 assembler of the kernel, the HAL, the
frame buffer miniport and the drivers of the serial lines, the keyboard and
the mouse.

## Status (2026-11-01)

- NT boots to its graphical shell on the color frame buffer (PMAG-BA,
  1024 by 864, 8 bits a pixel).
- The genuine developer first run (three boots: `registry.ini`, then
  `winlogon.cmd` in a cmd console, then the logon) ends at the logon dialog
  and Program Manager.
- The LK201 keyboard and the VSXXX mouse work, Ctrl+Alt+Del too.
- SCSI CD-ROM drives (CDFS: drive D:) and tape drives (DAT, DLT) work, and
  DZ lines 2 and 3 are COM1 and COM2.
- The programs of Program Manager's groups run: File Manager, Control
  Panel, PIF Editor and the command prompt; Notepad, Paintbrush,
  Calculator, Cardfile, Clock, Terminal, Character Map, Media Player (AVI
  video too), CD Player and Volume Control; the three games; Disk
  Administrator, Performance Monitor and Backup. Help and the command-line
  tools of `PRIVATE\UTILS` run too (not built yet: format, mode and more).
- The 5000/200 has no sound hardware: Volume Control finds no device.
- Not yet: networking; Event Viewer, User Manager and Registry Editor (their
  NET UI DLLs do not build yet); ClipBook (it needs the NetDDE services),
  Sound Recorder (OLE 2), Setup (the MIPS INF files), Mail and Schedule+.
- Not part of the port: the Spooler (printing, Print Manager) and RAS. The
  hive has no Spooler service.

## DECstation models

| Model | System board, processor | NT on it |
|---|---|---|
| DECstation and DECsystem 5000/200 | KN02 (3MAX), R3000 | Works in gxemul; not yet tried on a real machine |
| DECstation 5000/120, 5000/125, 5000/133 | KN02-BA (3MIN), R3000 | Written, not built or tested yet |
| DECstation and DECsystem 5000/240, DECsystem 5900 | KN03 (3MAX+), R3000A | Written, not built or tested yet |
| DECstation 5000/150 | KN04, R4000 | Not supported: the port is built for the R3000 |
| DECstation and DECsystem 5000/260, DECsystem 5900/260 | KN05, R4400 | Not supported: the port is built for the R3000 |
| Personal DECstation 5000/20, /25, /33, /50 (MAXINE) | KN02-CA, R3000 (/50: R4000) | Not supported: no drivers for its built-in frame buffer and its ACCESS.bus keyboard and mouse |
| DECstation 2100 and 3100, DECsystem 3100 | KN01 (PMAX), R2000 | Not supported: no TURBOchannel, no drivers for its SCSI and frame buffer |
| DECstation 5100, DECsystem 5400, 5500 and 5800 | KN230, KN210, KN220, KN5800 | Not supported: other buses and devices |

- The three supported boards need a PMAG-BA color frame buffer: it is the
  only display that NT has a driver for here (not yet: the PMAGB-JA (TX)
  and the other frame buffers).
- One firmware runs on all three boards: it asks the ROM which board it is
  on. On an R4000 or R4400 it prints a message and goes back to the ROM's
  prompt.
- `MODEL=3min` or `MODEL=3maxplus` makes `build/run.sh` run a 5000/1xx or
  a 5000/240 in gxemul, with gxemul's PROM emulation (`rom/` has only the
  5000/200's ROM). gxemul's 5000/240, and a fix to its SCCs, are on the
  `ntmax-local` branch of [tenox7/gxemul](https://github.com/tenox7/gxemul).

## No Microsoft code

This repository holds none of the NT source. A file of the port that is an
NT 3.5 file with changes is kept as an edit script, `FILE.ntdiff`, over the
NT 3.5 tree (`tools/ntdiff.pl`). Its first line names the NT files it copies
from. A line `=N LINE COUNT` copies COUNT lines of the N-th of them, from
line LINE. A line `+TEXT` is a line of the port.

`build/vm.sh`, `make-hives.sh` and `make-disk.sh` write the files from their
scripts before they use them (`ntdiff.pl apply`: only a file that is missing
or older than its script). `.gitignore` lists the files that scripts make.
After a change to such a file, rewrite its script (from the top of this
repository):

```sh
perl tools/ntdiff.pl update .. nt arcfw crt hal build   # every script
perl tools/ntdiff.pl make .. FILE NTFILE...              # a new one (NTFILE from the top of the tree)
```

The files that are new (the DZ, LK201 and VSXXX drivers, DZSERIAL's
`DZLINE.C` and its one-line files that include those of `DD\SERIAL`, the
firmware's `decfw.h`, `dxboot.c`, `dxnvram.c` and `d3start.s`, the scripts
and tools) are plain files.

## Layout

```
NTMAX/
  arcfw/   the ARC firmware: the Jazz firmware files of FW\MIPS with DECSTATION
           parts (edit scripts), and its own files (the REX PROM entry, the DZ
           serial lines, the disk through the PROM's boot device, the NVRAM
           image on the disk)
  hal/     the clock routines (DS1287) that the firmware links
  crt/     R3000 versions of setjmp and longjmp for the firmware's C library
  nt/      the files of the port at their paths in the NT tree:
             PRIVATE\NTOS\KE\MIPS        X3*.S, the R3000 trap, context switch
                                         and start code
             PRIVATE\NTOS\MM\MIPS        mir3000.h, the R3000 page tables
             PRIVATE\NTOS\NTHALS\HAL3MAX the DECstation HAL
             PRIVATE\NTOS\VIDEO\PMAGBA   the PMAG-BA miniport
             PRIVATE\NTOS\MINIPORT\NCR53C94  the PMAZ-AA SCSI controller
             PRIVATE\NTOS\DD\DZPORT      the DZ serial line driver
             PRIVATE\NTOS\DD\DZSERIAL    COM1 and COM2 (DZ lines 2 and 3): the
                                         serial driver of DD\SERIAL over DZPORT
             PRIVATE\NTOS\DD\LK201       the keyboard port of KBDCLASS
             PRIVATE\NTOS\DD\VSXXX       the pointer port of MOUCLASS
             PRIVATE\LSA\CRYPT\ENGINE    the LSA's encryption engine
             PUBLIC\OAK\BIN              SYSTEM.INI (the DECstation hive) and
                                         REGISTRY.INI (the first run's answers)
           and the R3000 versions of other MIPS files (no MIPS II code: no
           ldc1/sdc1, ll/sc, branch-likely)
  build/   the build scripts (below), Dockerfile (image 3max-build)
  tools/   ntdiff.pl; mkdecboot.pl (the DEC boot block); pe2bin.pl (a PE image
           laid out flat); r3kcheck.pl (code an R3000 cannot run, load-delay
           mistakes); r3kimplib.pl, r3kmark.pl (R3000 import thunks, image
           type); makeinc.pl (the makefiles a MAKEFILE includes, copied to
           the build machine with it); mkrom.py (rom/ from dumps of the ROM
           chips); dbgrun.c, vmkill.c (programs for the MIPS NT machine);
           drive.py, vtscreen.py (type at the serial console, show its screen)
  ctest/   tests of the compiler in R3000 mode
  hello/   the first boot test program
  rom/     the 5000/200's own ROMs, made by tools/mkrom.py (DEC's: not here)
  obj/     the build's outputs
```

## What is needed

- **The NT 3.5 source tree** (build 782): its `PRIVATE` and `PUBLIC`
  folders beside this folder (`../PRIVATE`, `../PUBLIC`).
- **A MIPS machine with Windows NT 4.0** (an emulated one works) that runs an
  rsh server, with Visual C++ 4.0 for MIPS (its LINK, LIB, NMAKE, and its
  compiler for the build tools; the build finds it in `%MSDevDir%`) and this
  Mac's `/Volumes/NTOS` as its `T:\NTOS` (`build/vm.sh`: `VM_HOST`,
  `VM_PORT`, `SHARE`, `RSH`). The compiler of NT itself is the tree's own,
  `PRIVATE\SDKTOOLS\C900`, with `/QMR3000`.
- **Docker**: the image `3max-build` (`docker build -t 3max-build build`)
  for the disk image, and for the registry hives the NT programs RCPP,
  REGINI and HIVEDMP that the NT 3.5 ARM port builds
  ([pint](https://github.com/tenox7/pint), in `../RASPI`: `obj/host`), with
  its image `tenox7/msvc-arm32`.
- **gxemul** with the changes the port needs (`build/build-gxemul.sh`
  builds `../gxemul`; it needs SDL2 for its window: `brew install
  sdl2-compat`).

## Build

Each script builds one thing; libraries come before the images that link
them. `build-all.sh` runs them all in that order, from the tools to the
system disk: `-n` lists its steps, and after a failure `build-all.sh N`
goes on from step N.

```sh
build/build-all.sh [-n] [N]       # all of the below but gxemul (KEEP=1 as make-disk.sh)
build/build-gxemul.sh             # gxemul
build/build-dbgrun.sh             # dbgrun.exe on the MIPS machine
build/build-tools.sh              # the tree's tools the build runs (gensrv, mc, ...)
build/build-gen.sh                # the header pass (ntddk.h, the message files)
build/build-midl.sh               # MIDL, then
build/build-midl0.sh [DIR...]     # the RPC stubs (pass 0)
build/build-crt.sh                # libcntpr.lib, the kernel's C library
build/build-crt32.sh ST|MT|DLL    # the Win32 C libraries
build/build-crtdll.sh             # crtdll.dll
build/build-lib.sh DIR            # one library of the tree, e.g. PRIVATE/NTOS/KE/UP
build/build-arcfw.sh              # the firmware, obj/arcfw.bin
build/build-osloader.sh           # obj/osloader.exe
build/build-kernel.sh             # obj/ntoskrnl.exe and obj/hal.dll
build/build-image.sh DIR          # the driver, DLL or programs of DIR
build/make-hives.sh               # obj/hives: SYSTEM, SOFTWARE, DEFAULT
build/make-disk.sh                # obj/3max-disk.img (KEEP=1 keeps NT's state)
```

The kernel links the libraries of `PRIVATE/NTOS` (KE, MM, EX, IO, ...) and
`PRIVATE/NTOS/NTHALS/HAL3MAX`. The images on the disk, by the directory
that `build-image.sh` builds them from:

| Images | Directory |
| --- | --- |
| scsiport, scsidisk, fastfat, npfs, msfs | `PRIVATE/NTOS/DD/SCSIPORT`, `DD/SCSIDISK`, `FASTFAT`, `NPFS`, `MAILSLOT` |
| ncr53c9x | `PRIVATE/NTOS/MINIPORT/NCR53C94` |
| videoprt, pmagba | `PRIVATE/NTOS/VIDEO/PORT`, `VIDEO/PMAGBA` |
| dzport, dzserial, lk201, kbdclass, vsxxx, mouclass | `PRIVATE/NTOS/DD/DZPORT`, `DZSERIAL`, `LK201`, `KBDCLASS`, `VSXXX`, `MOUCLASS` |
| scsicdrm, cdfs | `PRIVATE/NTOS/DD/SCSICDRM`, `PRIVATE/NTOS/CDFS` |
| 4mmdat, dlttape (with the tape class library, `DD/SCSITAPE/CLASS`) | `PRIVATE/NTOS/DD/SCSITAPE/4MMDAT`, `SCSITAPE/DLTTAPE` |
| ntdll, smss, csrss + csrsrv | `PRIVATE/NTOS/DLL/DAYTONA`, `PRIVATE/SM/SERVER`, `PRIVATE/CSR/SERVER` |
| kernel32, basesrv, advapi32 | `PRIVATE/WINDOWS/BASE/CLIENT/DAYTONA`, `BASE/SERVER`, `BASE/ADVAPI` |
| user32, gdi32, winsrv, framebuf, kbdus | `PRIVATE/WINDOWS/USER/CLIENT`, `GDI/CLIENT`, `WINSRV/DAYTONA`, `GDI/DISPLAYS/FRAMEBUF`, `USER/KBDLYOUT/US` |
| rpcrt4, rpcltc1, rpclts1 | `PRIVATE/RPC/RUNTIME/MTRT`, `TRANS/WIN32/CLNTNP`, `TRANS/WIN32/SVRNP` |
| lsass + lsasrv, msv1_0, msprivs, samsrv, samlib | `PRIVATE/LSA/SERVER`, `LSA/MSV1_0`, `LSA/MSPRIVS`, `PRIVATE/NEWSAM/SERVER`, `NEWSAM/CLIENT` |
| netapi32, NetRap, netmsg | `PRIVATE/NET/API`, `NET/RAP`, `NET/MESSAGES` |
| services, eventlog, netevent, iologmsg | `PRIVATE/WINDOWS/SCREG/SC/SERVER/DAYTONA`, `PRIVATE/EVENTLOG/SERVER`, `EVENTLOG/NETEVENT`, `PRIVATE/NTOS/DD/NLSMSG` |
| winlogon, userinit | `PRIVATE/WINDOWS/USER/WINLOGON/DAYTONA`, `USER/USERINIT` |
| progman, cmd, control + main.cpl, t1instal | `PRIVATE/WINDOWS/SHELL/PROGMAN`, `PRIVATE/WINDOWS/CMD`, `SHELL/CONTROL/CPANEL`, `CONTROL/MAIN`, `CONTROL/T1INSTAL` |
| shell32, comdlg32, mpr, lz32, version, winspool.drv | `PRIVATE/WINDOWS/SHELL/LIBRARY`, `SHELL/COMDLG`, `PRIVATE/WINDOWS/MPR`, `SHELL/LZ/LZEXPAND`, `SHELL/VERSION`, `SPOOLER/SPOOLSS/CLIENT` |
| setupdll | `PRIVATE/WINDOWS/SETUP/SETUPDLL` |
| comctl32, winfile | `PRIVATE/WINDOWS/SHELL/MSCTLS`, `SHELL/WINFILE` |
| notepad, calc, clock, sol, winmine | `PRIVATE/WINDOWS/SHELL/ACCESORY/NOTEPAD`, `ACCESORY/CALC`, `ACCESORY/CLOCK`, `SHELL/GAMES/SOL`, `PRIVATE/WINDOWS/EP/WINMINE` |
| ini, ech, adduser, shutdown, sleep | `PRIVATE/SDKTOOLS/INI`, `ECH`, `ADDUSER`, `SHUTDOWN`, `SLEEP` |
| cards, freecell, charmap, cardfile, pbrush, terminal | `PRIVATE/WINDOWS/EP/CARDDLL`, `EP/FREECELL`, `SHELL/ACCESORY/SPECHARS`, `ACCESORY/CARDFILE`, `ACCESORY/PAINTBRS`, `ACCESORY/TERMINAL` |
| olecli32, olesvr32 | `PRIVATE/WINDOWS/OLE/CLIENT`, `OLE/SERVER` |
| winhlp32, ftui32, fteng32, mvapi32 | `PRIVATE/WINDOWS/WINHELP/WINHELP`, `WINHELP/FTUI`, `WINHELP/FTENGINE`, `WINHELP/MVAPI` |
| perfmon, pifedit | `PRIVATE/SDKTOOLS/PERFMON`, `PRIVATE/MVDM/VDMUTILS/PIFEDIT` |
| winmm, mmdrv, mciwave, mciseq, mcicda, synth, mciole32 | `PRIVATE/WINDOWS/MEDIA/WINMM`, `MEDIA/MMDRV`, `MCIWAVE`, `MCISEQ`, `MCICDA`, `SYNTH`, `MCIOLE` |
| msacm32.dll, msacm32.drv, imaadp32.acm, msadp32.acm, msgsm32.acm | `PRIVATE/WINDOWS/MEDIA/MSACM/MSACM`, `MSACM/MSACMMAP`, `IMAADPCM`, `MSADPCM`, `GSM610` |
| sndvol32, cdplayer, mplay32 | `PRIVATE/WINDOWS/MEDIA/SNDVOL`, `MEDIA/CDPLAYER`, `MEDIA/MPLAYER2` |
| msvfw32, msvidc32, msrle32, mciavi32 | `PRIVATE/WINDOWS/MEDIA/AVI/VIDEO`, `AVI/MSVIDC`, `AVI/MSRLE`, `AVI/MCIAVI32` |
| ulib, ureg, ifsutil, ufat, uhpfs, untfs, cufat, cuhpfs, fmifs | `PRIVATE/UTILS/ULIB/SRC`, `UREG/SRC`, `IFSUTIL/SRC`, `UFAT/SRC`, `UHPFS/SRC`, `UNTFS/SRC`, `CUFAT/SRC`, `CUHPFS`, `FMIFS/SRC` |
| windisk, ntbackup, clb | `PRIVATE/UTILS/FDISK`, `UTILS/NTBACKUP/SRC`, `PRIVATE/SDKTOOLS/WINTOOLS/CLB` |
| attrib, chcp, chkdsk, comp, doskey, fc, find, findstr, help, label, format, mode, more, recover, replace, restore, sort, subst, tree, xcopy | `PRIVATE/UTILS/`NAME (`RESTORE/SRC`) |
| netui0, netui1, netui2, acledit, ntlanman, ntlanui, mprui | `PRIVATE/NET/UI/COMMON/SRC/DLL3/DLL0`, `DLL3/DLL1`, `DLL3/DLL2`, `NET/UI/ACLEDIT/BIN`, `UI/SHELL/BIN`, `UI/SHELLUI/BIN`, `UI/MPR/MPR` |
| eventvwr, usrmgr, musrmgr, regedt32 | `PRIVATE/NET/UI/ADMIN/EVENTVWR/EVENTVWR`, `ADMIN/USER/USER`, `ADMIN/USER/MINIUSER`, `PRIVATE/UTILS/REGEDIT/SRC` |

Every object and image goes through `tools/r3kcheck.pl`: the compiler in
R3000 mode still lets some MIPS II code through.

## Run

```sh
GXEMUL_OPTS=-X build/run.sh       # boot obj/3max-disk.img, the screen in a window
CD=cd.iso GXEMUL_OPTS=-X build/run.sh  # the same with cd.iso in a CD-ROM drive (rz4: D:)
T=10 build/run.sh                 # no input, stop after 10 seconds (tests)
build/run.sh prom                 # the 5000/200's own ROM, at its ">>" prompt
build/run.sh screen               # the same, with the frame buffer's ROM
```

The serial console (DZ line 3) is the terminal; with `-X` the firmware's
console is on the telnet port of the "DC7085 (main console)".

A new disk has no firmware settings: in the firmware's setup run
"Initialize system", "Set default configuration" (with `-X`: the frame
buffer must be in slot 0), "Set default environment" (SCSI hard disk 0,
partition 1) and, in "Manage startup", "Add a boot selection" (the defaults
give `\os\nt\osloader.exe`). The firmware keeps them on the disk, blocks
2016-2047.

Then the developer first run: boot 1 reads `\winnt\registry.ini` and
restarts (a restart ends gxemul: start it again), boot 2 logs on and runs
`winlogon.cmd` (a box says that the domain list cannot be made: OK, or it
goes after two minutes), boot 3 shows the logon: log on as Administrator,
with no password. The boots after that show the domain list box and then
"The system could not log you on" (the automatic logon that `winlogon.cmd`
set up): OK both, and log on.
`make-disk.sh` sets the disk up for a new first run; `KEEP=1 make-disk.sh`
writes only the programs.

At the ROM's `>>` prompt, `boot 5/rz0` starts the firmware;
`boot 5/rz0/osloader` starts that boot selection at once. With gxemul's own
PROM, `GXEMUL_OPTS="-X -j osloader"` does the same.

The keyboard is an LK201, which has no Esc key: in NT, F11 is Esc, as on
an LK201 (the Esc key of the Mac types Ctrl+[).

The 5000/200's ROM sets the clock's year to 72 when it is not 72 or 73, so
the HAL and the firmware keep the year in byte 63 of the clock's RAM, as
Linux does (gxemul sets it from the Mac's clock). On a real machine, set
the date once in NT.

## Limits of gxemul

- gxemul does not model the R3000's load-delay slots, and it runs some MIPS II
  instructions that an R3000 does not have. A program that runs in gxemul is
  not proven correct for the R3000: `r3kcheck.pl` checks every object.
- gxemul ends at a restart (the PROM's reboot call halts it).
- CD Player stops at "Scanning for CDROM drives": gxemul's CD-ROM drive
  answers READ SUB-CHANNEL with other data, NT's SCSI miniport then resets
  the NCR 53C94, and gxemul's reset of the chip leaves its interrupt
  asserted.
- With the frame buffer, gxemul writes what NT sends on COM1 and COM2 to
  its main console, and has no input for them.
- gxemul's tape drive reads SIMH tape images (`.tap`) but cannot write
  them: Backup cannot back up to tape.
