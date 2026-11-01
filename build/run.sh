#!/bin/sh
#
#  run.sh -- run a 3MAX program in gxemul as a DECstation 5000/200 (3MAX, KN02):
#
#	build/run.sh [disk [IMAGE]]	boot IMAGE (obj/3max-disk.img, make-disk.sh)
#					from its DEC boot block, as the PROM's
#					"boot 5/rz0" does
#	build/run.sh prom [IMAGE]	run the 5000/200's own ROM (the images in
#					rom/, made by tools/mkrom.py) with IMAGE
#					as disk rz0, at its ">>" prompt
#	build/run.sh screen [IMAGE]	the same with the color frame buffer
#					(rom/pmag-ba.bin, slot 0): the ROM's
#					console is its screen, in a window, with
#					the keyboard there; the serial line is on
#					telnet localhost 4000
#	build/run.sh elf [FILE]		load an ELF file (obj/hello.elf) with
#					gxemul's PROM emulation
#
#  The serial console (DZ line 3) is this terminal.  T=SECONDS runs without
#  input and stops after that many seconds (tests).  GXEMUL_OPTS adds gxemul
#  options: -X = the framebuffer console in a window, -V = start in gxemul's
#  debugger, -i = trace every instruction.
#
#  The ROM needs the option ROM of the SCSI controller (rom/pmaz-aa.bin, slot
#  5) to boot from the disk; without it, "boot 5/rz0" comes back to ">>".
#  The ROM keeps its settings (setenv) in obj/prom/nvram.bin.  Its console
#  is the screen when it has one (console=*); "setenv console s" makes it
#  the serial line.

set -e
here=$(cd "$(dirname "$0")/.." && pwd)	# 3MAX
gxemul=${GXEMUL:-$here/../gxemul/gxemul}
machine="-e 3max"

case ${1:-disk} in
disk)	args="-d ${2:-$here/obj/3max-disk.img}" ;;
elf)	args="${2:-$here/obj/hello.elf}" ;;
prom|screen)
	rom=$here/rom
	[ -f "$rom/kn02-aa.bin" ] ||
		{ echo "run.sh: no rom/kn02-aa.bin (tools/mkrom.py)" >&2; exit 1; }
	disk=${2:-$here/obj/3max-disk.img}
	disk=$(cd "$(dirname "$disk")" && pwd)/$(basename "$disk")
	slots= display=false
	if [ $1 = screen ]; then
		[ -f "$rom/pmag-ba.bin" ] ||
			{ echo "run.sh: no rom/pmag-ba.bin (tools/mkrom.py)" >&2; exit 1; }
		slots="\"0\": \"$rom/pmag-ba.bin\"" display=true
	fi
	[ -f "$rom/pmaz-aa.bin" ] && slots="${slots:+$slots, }\"5\": \"$rom/pmaz-aa.bin\""
	[ -f "$rom/pmad-aa.bin" ] && slots="${slots:+$slots, }\"6\": \"$rom/pmad-aa.bin\""
	# skip_failing_tests: the frame buffer's ROM tests the Bt459's
	# signature registers, which gxemul does not compute.
	mkdir -p "$here/obj/prom"
	cat > "$here/obj/prom/$1.json" <<-EOF
	{
	  "gxemul": 1,
	  "name": "DECstation 5000/200 (ROM)",
	  "machine": { "type": "decstation", "model": "3max", "memory_mb": 64 },
	  "boot": { "prom_emulation": false, "rom": "$rom/kn02-aa.bin", "nvram": "nvram.bin",
	            "skip_failing_tests": true },
	  "drives": [ { "file": "$disk", "kind": "disk", "id": 0 } ],
	  "display": { "enabled": $display },
	  "slots": { $slots },
	  "network": { "mac": "08:00:2b:00:00:01" }
	}
	EOF
	# -q: no gxemul messages about the ROM's probes of empty addresses.
	machine=-q
	args=$here/obj/prom/$1.json ;;
*)	echo "usage: run.sh [disk [IMAGE] | prom [IMAGE] | screen [IMAGE] | elf [FILE]]" >&2; exit 1 ;;
esac

if [ -z "$T" ]; then
	exec "$gxemul" $machine $GXEMUL_OPTS $args
fi

"$gxemul" $machine $GXEMUL_OPTS $args < /dev/null &
pid=$!
i=0
while kill -0 $pid 2>/dev/null && [ $i -lt "$T" ]; do
	sleep 1
	i=$((i + 1))
done
kill $pid 2>/dev/null && echo "run.sh: stopped after $T s" || true
wait $pid 2>/dev/null || true
