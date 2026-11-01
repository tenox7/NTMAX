#!/bin/sh
#
#  build-gxemul.sh -- build gxemul (nt35/gxemul, rpajarola/gxemul) in place.
#
#  gxemul is the emulator, like QEMU for RASPI, so it is built on the Mac and
#  not in Docker: its framebuffer window (-X) needs SDL2 on the host
#  (brew install sdl2-compat; configure finds it under /opt/homebrew).
#  configure makes the Makefiles from the Makefile.skel files, so it runs
#  again when configure or one of those is newer than the Makefile (after a
#  pull that added a source file).

set -e
cd "$(dirname "$0")/../../gxemul"
if [ ! -f Makefile ] || [ configure -nt Makefile ] ||
    [ -n "$(find . -name Makefile.skel -newer Makefile)" ]; then
	./configure
fi
make -j"$(sysctl -n hw.ncpu 2>/dev/null || nproc)"
ls -l gxemul
