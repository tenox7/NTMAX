Windows NT 3.5 for the DECstation 5000 (NTMAX): a disk image for real machines

ntmax-disk.img is a 256 MB SCSI disk with NT 3.5 installed and never started. In gxemul it runs on the
three models below, and on the 5000/200 also with that machine's own ROM; it has not run on a real
machine yet: this image is for testing that.

Machines: DECstation 5000/200 (KN02), 5000/120, /125, /133 (KN02-BA), 5000/240 (KN03), all R3000.
Not the 5000/150 or 5000/260 (R4000, R4400), the Personal DECstations or the DECstation 2100/3100.

You need:
- A PMAG-BA color frame buffer, in any TURBOchannel slot, with its monitor, the LK201 keyboard and the
  mouse. NT has no driver for the other frame buffers yet.
- A terminal (or a computer with a terminal program) on the serial console port (line 3) at 9600 baud,
  8 bits, no parity, 1 stop bit: the firmware's menus are there, not on the screen.
- A SCSI disk of 256 MB or more at SCSI ID 0 (rz0), or a SCSI emulator (SCSI2SD, BlueSCSI, ZuluSCSI)
  set to SCSI ID 0. Writing the image erases the disk.
- Memory: gxemul runs NT with 32 MB (5000/1xx, 5000/240) and 64 MB (5000/200); less is not tried.

1. Write the image to the disk with a raw writer, e.g. dd if=ntmax-disk.img of=/dev/DISK bs=1M on
   Linux. On a ZuluSCSI or BlueSCSI, copy it to the SD card as HD00_512.hda (the disk of SCSI ID 0,
   512-byte blocks); on a SCSI2SD, write it to the device of SCSI ID 0 (see its manual).
2. At the ROM's >> prompt, tell the ROM once what to boot, then boot:
     5000/200:              setenv boot 5/rz0/osloader     then  boot
     5000/1xx or 5000/240:  setenv boot 3/rz0/osloader     then  boot
   The ROM keeps this setting, also with the power off (unless the battery of its clock is dead), so
   from then on boot alone starts NT. The firmware starts on the serial terminal. When NT restarts,
   the ROM boots this again. If the ROM asks for the keyboard language, answer it (4 is English).
3. The firmware's setup, once (it keeps its settings on the disk). Enter takes what it offers:
   - "NVRAM is out of date or not initialized": press a key, choose Run setup.
   - Initialize system > Set default configuration: SCSI Host ID 7, then a key.
   - Set default environment: Scsi Hard Disk, SCSI ID 0, partition 1, then a key.
   - Return to main menu > Manage startup > Add a boot selection: Enter at every question
     (\os\nt\osloader.exe, Yes, \winnt, Windows NT, debugger No).
   - Return to main menu > Exit: the machine resets, and NT starts.
4. NT's first run, three boots; NT restarts by itself between them:
   - Boot 1 reads NT's first settings and restarts.
   - Boot 2: a box says that the domain list cannot be made: OK. NT restarts.
   - Boot 3 and later: that box, then "The system could not log you on": OK them until the Welcome
     box comes, then log on as Administrator, with no password.
   If the ROM stops at >> instead, type boot 5/rz0/osloader (or 3/rz0/osloader).
5. Later: at >>, type boot. With setenv haltaction b, the ROM also boots NT by itself at power-on.
   boot 5/rz0 (or 3/rz0) shows the firmware's menu instead, for its setup or another boot selection.
6. The keyboard: F11 is Esc (the LK201 has no Esc key), Compose is Alt, Ctrl+Alt+Del works.
   Set the date once in NT (Control Panel, Date/Time). Without a SCSI CD-ROM drive, a box at each
   start says that a service or driver failed (the CD-ROM drivers): OK.
7. On another model, run Set default configuration again, on that model.

Please report the model, the memory, the slot of the frame buffer, the disk or emulator, and how far
it got: what the serial terminal shows, and a photo of the screen if NT stops on a blue screen.

NTMAX: https://github.com/tenox7/NTMAX
