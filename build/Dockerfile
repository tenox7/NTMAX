# 3max-build: host tools for the DECstation port.
#
# GNU binutils for little-endian MIPS (assembler, linker, objcopy, objdump) for the
# test programs and to look at objects; no C compiler.  The NT code itself is compiled
# with the genuine NT 3.5 MIPS compiler (PRIVATE/SDKTOOLS/C900) on a MIPS NT machine.
#
#   docker build -t 3max-build 3MAX/build

FROM debian:trixie-slim

RUN apt-get update \
 && apt-get install -y --no-install-recommends \
        binutils-mipsel-linux-gnu \
        perl \
        mtools \
        dosfstools \
        make \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /work/3MAX
