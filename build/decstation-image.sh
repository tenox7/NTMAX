#!/usr/bin/env bash
# Disk image for real DECstations, obj/ntmax-decstation.tar.gz: ntmax-decstation/ with a new disk,
# obj/ntmax-decstation.img (make-disk.sh: no firmware settings, NT never started, so the firmware's
# setup and NT's first run happen on the tester's machine, and one image fits every model), as
# ntmax-disk.img, and build/portable/README-DECSTATION.txt as README.txt.
#
# Usage:  build/decstation-image.sh
set -euo pipefail
cd "$(dirname "$0")/.."
img=obj/ntmax-decstation.img
s=obj/portable/stage-decstation

FORCE=1 DISK=$img sh build/make-disk.sh >obj/portable/log/make-disk-decstation.log 2>&1 ||
	{ echo "make-disk failed, see obj/portable/log/make-disk-decstation.log" >&2; exit 1; }

# Never started: no firmware settings (blocks 2016-2047), only the hives make-hives.sh made, the files
# of boot 1 in place and none of the later boots.
python3 -I - "$img" <<'EOF'
import sys
with open(sys.argv[1], 'rb') as f:
    f.seek(2016 * 512)
    nv = f.read(32 * 512)
sys.exit('%s: the firmware settings are not empty' % sys.argv[1] if nv.count(0) != len(nv) else 0)
EOF
cfg=$(mdir -b -i "$img@@1M" ::/winnt/system32/config | sed 's|.*/||' | tr 'A-Z' 'a-z' | sort | tr '\n' ' ')
[[ $cfg == "default software system " ]] || { echo "$img: config holds $cfg" >&2; exit 1; }
mdir -b -i "$img@@1M" ::/winnt/registry.ini >/dev/null
! mdir -b -i "$img@@1M" ::/winnt/winlogon.cmd >/dev/null 2>&1 || { echo "$img: winlogon.cmd" >&2; exit 1; }
! mdir -b -i "$img@@1M" ::/pagefile.sys >/dev/null 2>&1 || { echo "$img: pagefile.sys" >&2; exit 1; }

rm -rf "$s" && mkdir -p "$s/ntmax-decstation"
cp "$img" "$s/ntmax-decstation/ntmax-disk.img"
cp build/portable/README-DECSTATION.txt "$s/ntmax-decstation/README.txt"
COPYFILE_DISABLE=1 tar --no-mac-metadata --no-xattrs --uid 0 --gid 0 --uname root --gname root \
	-C "$s" -cf - ntmax-decstation | gzip -9 >obj/ntmax-decstation.tar.gz
echo ">> built obj/ntmax-decstation.tar.gz ($(du -h obj/ntmax-decstation.tar.gz | cut -f1))"
