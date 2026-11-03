Windows NT 3.5 for the DECstation 5000/200 (NTMAX) on gxemul

Run ./run.sh (macOS 11+, Apple silicon or Intel; Linux x86_64 or aarch64 with glibc 2.34+ and a desktop) or
run.cmd (Windows 10+ x64). gxemul runs a DECstation 5000/200 with 64 MB and the PMAG-BA color frame buffer.
NT is installed on ntmax-disk.img and its first run is done.

- At each start a box says that the domain list cannot be made, then "The system could not log you on":
  OK them (Enter) until the Welcome box comes, then log on as Administrator, with no password.
- NT is in the window: click in it to give NT the mouse, Ctrl+Alt (on a Mac Ctrl+Option) releases it.
  F10 opens gxemul's menus when NT does not have the mouse.
- The keyboard is an LK201, which has no Esc key: F11 is Esc (the Esc key types Ctrl+[).
- To stop: in Program Manager, File > Shutdown, then close the window. Closing the window stops the
  machine at once, as pulling its plug would. A restart of NT ends gxemul too: start it again.
- The CD-ROM drive (SCSI ID 4, D: in NT) starts empty: gxemul's Media menu loads an ISO 9660 image.
- Keep a copy of ntmax-disk.img to start over. Extra gxemul options are passed through.
- This disk is set up for the 5000/200 only (the firmware keeps its settings on the disk).

NTMAX: https://github.com/tenox7/NTMAX
gxemul @V@: branch ntmax-local of https://github.com/tenox7/gxemul (license: bin/LICENSE-gxemul.txt).
SDL2 2.32.10: https://github.com/libsdl-org/SDL (zlib license: bin/LICENSE-SDL2.txt).
