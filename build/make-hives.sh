#!/bin/sh
#
#  make-hives.sh -- build the registry hives of the DECstation the way
#  PUBLIC\OAK\BIN\MKHIVES.CMD builds the hives of the NT tree (MKHIVES
#  GENERAL): for each hive RCPP preprocesses its .INI with the options of the
#  hive and REGINI -h loads it into a new hive file.  The .INI files are those
#  of nt/PUBLIC/OAK/BIN where 3MAX has one (SYSTEM.INI: the SYSTEM.INI of the
#  tree with the DECstation variant, -D_DECSTATION_, its drivers in place of
#  the PC and Jazz ones), else those of the tree:
#
#	obj/hives/SYSTEM	SYSTEM.INI, root System
#	obj/hives/SOFTWARE	SOFTWARE.INI, root Software
#	obj/hives/DEFAULT	DEFAULT.INI, root .Default
#
#  make-disk.sh puts them in \winnt\system32\config (system, software,
#  default).  For each hive NAME there are also obj/hives/NAME.DMP, the hive
#  as HIVEDMP shows it, and NAME.REG, what REGINI wrote while it loaded it.
#
#  RCPP, REGINI and HIVEDMP are the NT programs the ARM port builds for its
#  own hives (RASPI/obj/host: RASPI/tools/make-tools.sh and
#  RASPI/tools/hive/make-hives.sh), run on the 32-bit ARM Wine of its build
#  image, tenox7/msvc-arm32 (RASPI/tools/host.sh).  A hive file has the same
#  form on every processor.

set -e
here=$(cd "$(dirname "$0")/.." && pwd)	# 3MAX
nt35=$(cd "$here/.." && pwd)

# (the .INI files of nt/PUBLIC/OAK/BIN are edit scripts over the NT tree, vm.sh)
perl "$here/tools/ntdiff.pl" apply "$nt35" "$here/nt/PUBLIC/OAK/BIN"

for t in RCPP REGINI HIVEDMP; do
	[ -f "$nt35/RASPI/obj/host/$t.EXE" ] ||
		{ echo "make-hives: no RASPI/obj/host/$t.EXE (RASPI/tools/hive/make-hives.sh)" >&2; exit 1; }
done
mkdir -p "$here/obj/hives"

docker run --rm -v "$here":/work/3MAX -v "$nt35/RASPI":/work/RASPI:ro \
	-v "$nt35/PRIVATE":/work/PRIVATE:ro -v "$nt35/PUBLIC":/work/PUBLIC:ro \
	-w /work/3MAX/obj/hives --entrypoint bash tenox7/msvc-arm32 -c '
set -e
host() { bash /work/RASPI/tools/host.sh "$@"; }
export NTNLS="Z:\\work\\PRIVATE\\WINDOWS\\WINNLS\\DATA"
for h in "SYSTEM System" "SOFTWARE Software" "DEFAULT .Default"; do
	set -- $h
	ini=/work/3MAX/nt/PUBLIC/OAK/BIN/$1.INI
	[ -f $ini ] || ini=/work/PUBLIC/OAK/BIN/$1.INI
	rm -f $1 $1.PP $1.REG $1.DMP
	host rcpp -R -P -I /work/PUBLIC/OAK/BIN -f $ini -D_GENERAL_PURPOSE_ -D_DECSTATION_ -g $1.PP > /dev/null
	host regini -h $1 $2 $1.PP | tr -d "\r" > $1.REG 2>&1
	host hivedmp -f $1 | tr -d "\r" > $1.DMP 2>&1
	echo "obj/hives/$1: $(stat -c %s $1) bytes, $(grep -c "" $1.DMP) dump lines, $(grep -c "^REGINI" $1.REG) REGINI errors"
done
'
