# NTMAX — Windows NT 3.5 on the DECstation 5000/200

Based on a true story: in 1992 Alexandre Bronstein showed Windows NT running
on a DECstation 5000/200, in his talk
[Windows NT: what is it technologically/strategically?](https://youtu.be/bMJIhN4xCro?t=4948)
(Computer History Museum, on YouTube; the demo starts at 1:22:28).

A port of Windows NT 3.5 (build 782) to the DECstation 5000 (MIPS R3000,
TURBOchannel), built from the NT 3.5 source tree. Written for the port: the
ARC firmware, the R3000 kernel code, the HAL, and the drivers of the frame
buffer, the serial lines, the keyboard and the mouse. It runs in gxemul and
on real machines.

## Status (2026-11-24)

- NT boots to Program Manager on the PMAG-BA color frame buffer (1024 by
  864, 8 bits). The first run takes three boots; from then on Administrator
  (password `password`) logs on by itself.
- The LK201 keyboard, the VSXXX mouse, SCSI disks, CD-ROM and tape drives,
  and COM1 and COM2 (DZ lines 2 and 3) work.
- Network on the Ethernet of the system board (the PMAD-AA of the 5000/200,
  the LANCE of the 5000/1xx and the 5000/240): TCP/IP with DHCP, NetBIOS and
  file sharing (`net share`, `net use`).
- The programs of Program Manager's groups run, and those of `apps/` (as in
  [pint](https://github.com/tenox7/pint)): a task manager, more games,
  ntfetch, a VNC viewer and Internet Explorer 1.5. The SDK tools are in
  `\SDKTOOLS`.
- Not yet: ClipBook, Sound Recorder, Setup and the Network control panel,
  Mail and Schedule+.
- Not part of the port: printing (the Spooler) and RAS.

## DECstation models

| Model | System board, processor | NT on it |
|---|---|---|
| DECstation and DECsystem 5000/200 | KN02 (3MAX), R3000 | Works in gxemul; not yet tried on a real machine |
| DECstation 5000/120, 5000/125, 5000/133 | KN02-BA (3MIN), R3000 | Works in gxemul; not yet tried on a real machine |
| DECstation and DECsystem 5000/240, DECsystem 5900 | KN03 (3MAX+), R3000A | Works in gxemul; a real 5000/240 runs it to the desktop, with the network |
| DECstation 5000/150 | KN04, R4000 | Not supported: the port is built for the R3000 |
| DECstation and DECsystem 5000/260, DECsystem 5900/260 | KN05, R4400 | Not supported: the port is built for the R3000 |
| Personal DECstation 5000/20, /25, /33, /50 (MAXINE) | KN02-CA, R3000 (/50: R4000) | Not supported: no drivers for its frame buffer, keyboard and mouse |
| DECstation 2100 and 3100, DECsystem 3100 | KN01 (PMAX), R2000 | Not supported: no TURBOchannel |
| DECstation 5100, DECsystem 5400, 5500 and 5800 | KN230, KN210, KN220, KN5800 | Not supported: other buses and devices |

The supported models need a PMAG-BA color frame buffer, the only display NT
has a driver for here.

## No Microsoft code

This repository holds no NT source. A file of the port that is an NT 3.5
file with changes is kept as an edit script, `FILE.ntdiff`, over the NT 3.5
tree, and the build writes the file from it (`tools/ntdiff.pl`). After a
change to such a file: `perl tools/ntdiff.pl update .. nt arcfw crt hal build`.

## Layout

```
arcfw/   the ARC firmware
nt/      the port's files, at their paths in the NT tree
hal/     the clock routines the firmware links
crt/     setjmp and longjmp for the firmware
apps/    programs that are not part of NT, each with its license
build/   the build scripts
tools/   ntdiff.pl, r3kcheck.pl (code an R3000 cannot run) and more
rom/     DEC's ROMs of the 5000/200
```

## Build

What it needs:

- The NT 3.5 source tree (build 782) in `../PRIVATE` and `../PUBLIC`.
- A MIPS machine with NT 4.0 (an emulated one works), an rsh server and
  Visual C++ 4.0 for MIPS. Its settings are in `build/vm.sh`.
- Docker (`docker build -t 3max-build build`), and pint's build tools
  (`../RASPI/obj/host`) for the registry hives.
- gxemul with the port's changes, in `../gxemul`: the `ntmax-local` branch
  of [tenox7/gxemul](https://github.com/tenox7/gxemul).

```sh
build/build-all.sh                       # everything, to obj/3max-disk.img (-n lists the steps)
build/build-gxemul.sh                    # gxemul (needs SDL2)
build/decstation-image.sh                # obj/ntmax-decstation.tar.gz: a disk for real machines
IMG=DISK build/portable-gxemul-image.sh  # obj/ntmax-gxemul.tar.gz: gxemul for macOS, Linux, Windows + DISK
```

## Run

```sh
GXEMUL_OPTS=-X build/run.sh   # boot obj/3max-disk.img, the screen in a window
build/run.sh screen           # DEC's ROM: at its ">>" prompt, type boot 5/rz0
MODEL=3min build/run.sh       # a 5000/1xx (MODEL=3maxplus: a 5000/240)
```

- The firmware's menu is on the serial console: this terminal, or with
  `-X` gxemul's telnet port.
- A new disk asks for the firmware's setup once: Initialize system, Set
  default configuration, Set default environment, Add a boot selection.
- With gxemul's own PROM, each restart ends gxemul: start it again.
- The LK201 has no Esc key: F11 is Esc.
