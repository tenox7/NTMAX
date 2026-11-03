#!/usr/bin/env python3
#
#  mkrom.py -- make the ROM images that gxemul runs as the firmware of the
#  5000/200 itself (build/run.sh prom), from dumps of the ROM chips of a
#  real one:
#
#    python3 -I tools/mkrom.py [-o DIR] DUMPS...
#
#  Each of DUMPS is a dump, a zip file or a folder of dumps (e.g.
#  ~/Downloads/decstation200.zip), DIR the folder for the images (default
#  rom/).  The images are found by what is in them, not by their names:
#
#	kn02-aa.bin	the system ROM: the KN02 has two 16-bit chips, one
#			with the low and one with the high half of each
#			32-bit word, put together here
#	pmaz-aa.bin	the option ROM of slot 5, the SCSI controller
#	pmad-aa.bin	the option ROM of slot 6, the Ethernet interface
#
#  An option ROM is one byte wide, dumped one ROM byte per file byte, which
#  is what gxemul loads.  The ROMs are DEC's; rom/ has those of a
#  5000/200, with the ROM of its PMAG-BA frame buffer (slot 0).

import os, struct, sys, zipfile

args = sys.argv[1:]
here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
out = os.path.join(here, 'rom')
if args[:1] == ['-o']:
    out = args[1]
    args = args[2:]
if not args:
    sys.exit('usage: mkrom.py [-o DIR] DUMPS...')
dumps = ', '.join(args)

files = {}
for arg in args:
    if os.path.isdir(arg):
        for name in sorted(os.listdir(arg)):
            path = os.path.join(arg, name)
            if os.path.isfile(path):
                files[name] = open(path, 'rb').read()
    elif zipfile.is_zipfile(arg):
        with zipfile.ZipFile(arg) as z:
            for name in z.namelist():
                if not name.endswith('/'):
                    files[name] = z.read(name)
    else:
        files[os.path.basename(arg)] = open(arg, 'rb').read()

def system_rom(image):
    # The first instruction, at 0xbfc00000, jumps into the ROM.
    word = struct.unpack('<I', image[:4])[0]
    target = 0xb0000000 | (word & 0x03ffffff) << 2
    return word >> 26 == 2 and target >= 0xbfc00000 and b'KN02-AA' in image

def option_rom(image):
    # The TURBOchannel ROM header: the test pattern and "TCF0".
    return image[0xfc:0x100] == b'\x55\x00\xaa\xff' and image[0x118:0x11c] == b'TCF0'

def put(name, image, what):
    path = os.path.join(out, name)
    with open(path, 'wb') as f:
        f.write(image)
    print('%s: %s, %d KB' % (path, what, len(image) // 1024))

os.makedirs(out, exist_ok=True)
made = set()

for name, image in files.items():
    if option_rom(image):
        module = image[0x110:0x118].decode('latin-1').strip()
        version = image[0x100:0x108].decode('latin-1').strip()
        put(module.lower() + '.bin', image, '%s %s option ROM (%s)' % (module, version, name))
        made.add(module)

def words16(lo, hi):
    return b''.join(lo[i:i + 2] + hi[i:i + 2] for i in range(0, len(lo), 2))

halves = [n for n, b in files.items() if len(b) == 128 * 1024 and n not in made]
for name, image in files.items():
    if system_rom(image):
        pair = image, name
        break
else:
    pair = None
    for lo in halves:
        for hi in halves:
            if lo != hi and system_rom(words16(files[lo], files[hi])):
                pair = words16(files[lo], files[hi]), '%s + %s' % (lo, hi)
if pair:
    image, how = pair
    # The ROM's name as the configuration shows it: "V5.3s   DEC     KN02-AA ".
    at = image.find(b'KN02-AA')
    version = image[at - 16:at - 8].decode('latin-1').strip()
    put('kn02-aa.bin', image, 'KN02-AA %s system ROM (%s)' % (version, how))
    made.add('KN02-AA')

for module, what in (('KN02-AA', 'the system ROM'),
                     ('PMAZ-AA', 'the SCSI option ROM, slot 5: the ROM cannot boot from a disk without it'),
                     ('PMAD-AA', 'the Ethernet option ROM, slot 6')):
    if module not in made:
        print('mkrom: %s is not in %s (%s)' % (module, dumps, what))
