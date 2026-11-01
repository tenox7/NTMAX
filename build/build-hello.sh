#!/bin/sh
#
#  build-hello.sh -- build the hello-world test program and its boot disk in
#  Docker (image 3max-build, made from build/Dockerfile if missing):
#
#	obj/hello.elf		for gxemul's PROM emulation (run.sh hello)
#	obj/hello.bin		the raw program
#	obj/hello-disk.img	a 64 MB disk with a DEC boot block (run.sh disk)

set -e
here=$(cd "$(dirname "$0")/.." && pwd)	# 3MAX
nt35=$(cd "$here/.." && pwd)
image=3max-build

if ! docker image inspect $image >/dev/null 2>&1; then
	docker build -t $image "$here/build"
fi

docker run --rm -v "$nt35":/work -w /work/3MAX --entrypoint sh $image -c '
set -e
mkdir -p obj
mipsel-linux-gnu-as -march=r3000 -mabi=32 -non_shared -EL \
	-o obj/hello.o hello/hello.S
mipsel-linux-gnu-ld -EL -z max-page-size=4096 --no-warn-rwx-segments -T hello/hello.ld -o obj/hello.elf obj/hello.o
mipsel-linux-gnu-objcopy -O binary obj/hello.elf obj/hello.bin
mipsel-linux-gnu-objdump -d obj/hello.elf > obj/hello.lst
entry=$(mipsel-linux-gnu-readelf -h obj/hello.elf | sed -n "s/.*Entry point address: *0x//p")
perl tools/mkdecboot.pl obj/hello.bin 80030000 $entry obj/hello-disk.img
'
ls -l "$here/obj"
