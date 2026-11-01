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
- Not yet: networking, most applications.
- Not part of the port: the Spooler (printing) and RAS. The hive has no
  Spooler service.

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

The files that are new (the DZ, LK201 and VSXXX drivers, the firmware's
`decfw.h`, `dxboot.c`, `dxnvram.c` and `d3start.s`, the scripts and tools)
are plain files.

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
           type); mkrom.py (rom/ from dumps of the ROM chips); dbgrun.c,
           vmkill.c (programs for the MIPS NT machine); drive.py, vtscreen.py
           (type at the serial console, show its screen)
  ctest/   tests of the compiler in R3000 mode
  hello/   the first boot test program
  rom/     the 5000/200's own ROMs, made by tools/mkrom.py (DEC's: not here)
  obj/     the build's outputs
```

## What is needed

- **The NT 3.5 source tree** (build 782): its `PRIVATE` and `PUBLIC`
  folders beside this folder (`../PRIVATE`, `../PUBLIC`).
- **A MIPS machine with Windows NT 4.0** (an emulated one works) that runs an
  rsh server, with Visual C++ 4.0 for MIPS in `C:\MSDEV` (its LINK, LIB,
  NMAKE, and its compiler for the build tools) and this Mac's
  `/Volumes/NTOS` as its `T:\NTOS` (`build/vm.sh`: `VM_HOST`, `VM_PORT`,
  `SHARE`, `RSH`). The compiler of NT itself is the tree's own,
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
them.

```sh
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
| dzport, lk201, kbdclass, vsxxx, mouclass | `PRIVATE/NTOS/DD/DZPORT`, `LK201`, `KBDCLASS`, `VSXXX`, `MOUCLASS` |
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
| ini, ech, adduser, shutdown, sleep | `PRIVATE/SDKTOOLS/INI`, `ECH`, `ADDUSER`, `SHUTDOWN`, `SLEEP` |

Every object and image goes through `tools/r3kcheck.pl`: the compiler in
R3000 mode still lets some MIPS II code through.

## Run

```sh
GXEMUL_OPTS=-X build/run.sh       # boot obj/3max-disk.img, the screen in a window
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
with no password.
`make-disk.sh` sets the disk up for a new first run; `KEEP=1 make-disk.sh`
writes only the programs.

At the ROM's `>>` prompt, `boot 5/rz0` starts the firmware;
`boot 5/rz0/osloader` starts that boot selection at once. With gxemul's own
PROM, `GXEMUL_OPTS="-X -j osloader"` does the same.

The keyboard is an LK201, which has no Esc key: the Esc key of the Mac
is the LK201's F11, which is Esc on an LK201.

## Limits of gxemul

- gxemul does not model the R3000's load-delay slots, and it runs some MIPS II
  instructions that an R3000 does not have. A program that runs in gxemul is
  not proven correct for the R3000: `r3kcheck.pl` checks every object.
- gxemul ends at a restart (the PROM's reboot call halts it).
