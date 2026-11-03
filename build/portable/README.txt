Windows NT 3.5 for the DECstation 5000/200 (NTMAX) on gxemul

Run ./run.sh (macOS 11+, Apple silicon or Intel; Linux x86_64 or aarch64 with glibc 2.34+ and a desktop) or
run.cmd (Windows 10+ x64). gxemul runs a DECstation 5000/200 with 64 MB, the PMAG-BA color frame buffer and
DEC's own ROMs (rom/). NT is installed on ntmax-disk.img and its first run is done.

- The ROM starts in the window and shows its >> prompt: type boot and Enter. The ROM keeps its settings
  in nvram.bin (the memory of the machine's clock): boot=5/rz0/osloader, so boot starts NT, and English
  for the keyboard. When NT restarts, the ROM starts it again.
- At each start of NT a box says that the domain list cannot be made, then "The system could not log you
  on": OK them (Enter) until the Welcome box comes, then log on as Administrator, with no password.
- NT is in the window: click in it to give NT the mouse, Ctrl+Alt (on a Mac Ctrl+Option) releases it.
  F10 opens gxemul's menus when NT does not have the mouse.
- The keyboard is an LK201, which has no Esc key: F11 is Esc (the Esc key types Ctrl+[).
- The task manager (NT Performance) is the Task List: Ctrl+F11 (Ctrl+Esc) or a double-click on the
  desktop opens it.
- To stop: in Program Manager, File > Shutdown, then close the window. Closing the window stops the
  machine at once, as pulling its plug would.
- At >>, boot 5/rz0 starts the ARC firmware's menu instead, on the serial line 3: telnet localhost 4002.
- The CD-ROM drive (SCSI ID 4, D: in NT) starts empty: gxemul's Media menu loads an ISO 9660 image.
- Keep a copy of ntmax-disk.img and nvram.bin to start over. Without nvram.bin, the ROM asks for the
  keyboard's language at each start (4 is English) until setenv console 1, and needs
  setenv boot 5/rz0/osloader again. Extra gxemul options are passed through.
- This disk is set up for the 5000/200 only (the firmware keeps its settings on the disk).

rom/ has DEC's ROMs of the 5000/200: kn02-aa.bin (KN02-AA V5.3s, the system), pmag-ba.bin (the frame
buffer, slot 0), pmaz-aa.bin (SCSI, slot 5) and pmad-aa.bin (Ethernet, slot 6). ntmax.json is the machine
for gxemul.

NTMAX: https://github.com/tenox7/NTMAX
gxemul @V@: branch ntmax-local of https://github.com/tenox7/gxemul (license: bin/LICENSE-gxemul.txt).
SDL2 2.32.10: https://github.com/libsdl-org/SDL (zlib license: bin/LICENSE-SDL2.txt).
