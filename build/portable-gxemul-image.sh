#!/usr/bin/env bash
# Ready-to-run gxemul package obj/ntmax-gxemul.tar.gz: a disk with NT installed (IMG), the
# 5000/200 with its frame buffer in a window and DEC's ROMs (build/portable/ntmax.json, rom/ of
# tools/mkrom.py), the ROM's settings (nvram.bin: boot 5/rz0/osloader, English), run.sh and
# run.cmd (build/portable) and gxemul (the HEAD of ../gxemul) for macOS (arm64 and x86_64 in
# one binary, with the SDL2 of the official framework), Linux x86_64 and aarch64 (AlmaLinux 9,
# its SDL2 bundled) and Windows x86_64 (mingw-w64, the official SDL2 DLL), the last three built
# in Docker. The binaries stay in obj/portable/bin; FORCE=1 rebuilds them.
#
# Usage:  IMG=disk.img [FORCE=1] build/portable-gxemul-image.sh [TARGET...]
#
# TARGET is a binary (macos, linux-x86_64, linux-aarch64, windows-x86_64), built if it is not
# there yet, or pack; none = all the binaries at once, then pack. IMG (for pack) is a disk that
# has done its first run on a 5000/200 and was shut down from NT: a copy of a disk that has the
# firmware's settings for the 5000/200, make-disk.sh on it, the three boots, then Shutdown.
set -euo pipefail
cd "$(dirname "$0")/.."
here=$PWD
G=$(cd ../gxemul && pwd)
P=$here/obj/portable
SDL=2.32.10
SDL_MINGW_SHA256=83a5d74012311edc3c0d40ea6faecbe57ad692aa033fa5dc273cc937e3938ff2
V=1.0.0~git$(git -C "$G" rev-parse --short HEAD)
mkdir -p "$P/dl" "$P/bin" "$P/log" "$P/src"

fetch() {	# fetch FILE: an SDL2 release file into obj/portable/dl
	[[ -s $P/dl/$1 ]] && return
	echo ">> fetching $1"
	curl -fL# -o "$P/dl/$1.part" "https://github.com/libsdl-org/SDL/releases/download/release-$SDL/$1"
	mv "$P/dl/$1.part" "$P/dl/$1"
}

src() {		# src TARGET: the HEAD of ../gxemul in obj/portable/src/TARGET
	rm -rf "$P/src/$1" && mkdir -p "$P/src/$1"
	git -C "$G" archive HEAD | tar -x -C "$P/src/$1"
}

macos() {
	local o=$P/bin/macos s=$P/src/macos f=$P/dl/SDL2.framework m
	fetch SDL2-$SDL.dmg
	if [[ ! -d $f ]]; then
		m=$(mktemp -d)
		hdiutil attach -quiet -nobrowse -readonly -mountpoint "$m" "$P/dl/SDL2-$SDL.dmg"
		ditto "$m/SDL2.framework" "$f"
		hdiutil detach -quiet "$m"
	fi
	codesign --verify --strict "$f"
	src macos
	printf '#!/bin/sh\ncase $1 in\n--cflags) echo "-F%s -I%s/Headers" ;;\n--libs) echo "-F%s -framework SDL2 -framework Cocoa" ;;\nesac\n' \
		"$P/dl" "$f" "$P/dl" >"$s/sdl2-config"
	chmod +x "$s/sdl2-config"
	(cd "$s" && CC="clang -arch arm64 -arch x86_64 -mmacosx-version-min=11.0" \
		SDL2_CONFIG=$s/sdl2-config GXEMUL_VERSION=$V ./configure && make -j8)
	grep -q '^#define WITH_SDL' "$s/config.h"
	# The SDL2 of the framework as a plain library beside gxemul (no symlinks in the package):
	rm -rf "$o" && mkdir -p "$o"
	cp "$s/gxemul" "$o/"
	cp "$f/Versions/A/SDL2" "$o/libSDL2.dylib"
	install_name_tool -change @rpath/SDL2.framework/Versions/A/SDL2 @executable_path/libSDL2.dylib "$o/gxemul"
	codesign -f -s - "$o/gxemul"
	m=$(lipo -archs "$o/gxemul")
	[[ $m == *arm64* && $m == *x86_64* ]]
	{ "$o/gxemul" -h 2>&1 || :; } | grep -q "GXemul $V"
	{ arch -x86_64 "$o/gxemul" -h 2>&1 || :; } | grep -q "GXemul $V"
}

linux() {	# linux ARCH PLATFORM
	local o=$P/bin/linux-$1
	src linux-$1
	rm -rf "$o" && mkdir -p "$o/lib"
	docker run --rm --platform "$2" -v "$P/src/linux-$1:/src" -v "$o:/out" -e V="$V" almalinux:9 bash -euxc '
		dnf -y -q install dnf-plugins-core
		dnf config-manager --set-enabled crb
		dnf -y -q install gcc make SDL2-devel zlib-devel diffutils findutils which
		cd /src && GXEMUL_VERSION=$V ./configure && make -j8
		grep -q "^#define WITH_SDL" config.h
		cp gxemul /out/
		cp -L "$(ldd gxemul | sed -n "s/.*libSDL2-2.0.so.0 => \([^ ]*\).*/\1/p")" /out/lib/
		ldd gxemul
		./gxemul -h 2>&1 | grep "GXemul $V"'
}

windows() {
	local o=$P/bin/windows-x86_64
	fetch SDL2-devel-$SDL-mingw.tar.gz
	echo "$SDL_MINGW_SHA256  $P/dl/SDL2-devel-$SDL-mingw.tar.gz" | shasum -a 256 -c -
	src windows
	rm -rf "$o" && mkdir -p "$o"
	docker run --rm --platform linux/amd64 -v "$P/src/windows:/src" -v "$o:/out" -v "$P/dl:/dl:ro" \
		-e V="$V" -e SDL="$SDL" ubuntu:24.04 bash -euxc '
		export DEBIAN_FRONTEND=noninteractive WINEDEBUG=-all WINEPREFIX=/tmp/wine
		apt-get update -qq
		apt-get install -y -qq gcc make gcc-mingw-w64-x86-64 binutils-mingw-w64-x86-64 wine wine64 >/dev/null
		tar -xzf /dl/SDL2-devel-$SDL-mingw.tar.gz -C /tmp
		sdl=/tmp/SDL2-$SDL/x86_64-w64-mingw32
		cd /src && CC=x86_64-w64-mingw32-gcc CC_FOR_BUILD=gcc CROSS_RUN=wine \
			SDL2_CONFIG=$sdl/bin/sdl2-config GXEMUL_VERSION=$V ./configure
		grep -q "^#define WITH_SDL" config.h
		make -j8
		# Only Windows own DLLs (msvcrt: the C library of this mingw-w64) and SDL2.dll:
		if x86_64-w64-mingw32-objdump -p gxemul.exe | sed -n "s/^.*DLL Name: //p" |
			grep -viE "^(ole32|shell32|kernel32|user32|ws2_32|iphlpapi|comdlg32|msvcrt|sdl2|api-ms-win-crt-[a-z0-9-]+)\.dll$"; then
			exit 1
		fi
		cp gxemul.exe $sdl/bin/SDL2.dll /out/
		cp /tmp/SDL2-$SDL/LICENSE.txt /out/LICENSE-SDL2.txt
		cd /out && wine ./gxemul.exe -h 2>&1 | grep "GXemul $V"'
}

nvram() {	# nvram FILE: the ROM's settings, the clock's 50 bytes of RAM (registers 14-63)
	{
		printf '\000\003\030\000\000\000\000\000'	# 14-21, as the ROM sets them (16: more=24)
		printf '5/rz0/osloader'				# 22-35: boot
		head -c 26 /dev/zero				# 36-61
		printf '1'					# 62: console=1, English (the ROM does not ask)
		# 63: the year for NT (2000 + it): gxemul takes it from the file, not from the clock
		printf "\\$(printf %03o $(($(date +%Y) - 2000)))"
	} >"$1"
	[[ $(wc -c <"$1") -eq 50 ]]
}

pack() {
	[[ -n ${IMG:-} && -f $IMG ]] || { echo "IMG= a disk with NT installed (see the top of $0)" >&2; exit 1; }
	local b s=$P/stage r=$P/stage/ntmax-gxemul
	for b in macos linux-x86_64 linux-aarch64 windows-x86_64; do
		[[ -d $P/bin/$b ]] || { echo "no obj/portable/bin/$b: build it first" >&2; exit 1; }
	done
	for b in kn02-aa pmag-ba pmaz-aa pmad-aa; do
		[[ -s rom/$b.bin ]] || { echo "no rom/$b.bin (tools/mkrom.py)" >&2; exit 1; }
	done
	echo ">> packing obj/ntmax-gxemul.tar.gz"
	rm -rf "$s" && mkdir -p "$r/rom"
	cp "$IMG" "$r/ntmax-disk.img"
	cp rom/kn02-aa.bin rom/pmag-ba.bin rom/pmaz-aa.bin rom/pmad-aa.bin "$r/rom/"
	cp build/portable/ntmax.json "$r/"
	nvram "$r/nvram.bin"
	cp build/portable/run.sh "$r/" && chmod +x "$r/run.sh"
	sed 's/$/\r/' build/portable/run.cmd >"$r/run.cmd"
	sed "s/@V@/$V/g" build/portable/README.txt >"$r/README.txt"
	cp -R "$P/bin" "$r/bin"
	mv "$r/bin/windows-x86_64/LICENSE-SDL2.txt" "$r/bin/LICENSE-SDL2.txt"
	cp "$G/LICENSE" "$r/bin/LICENSE-gxemul.txt"
	COPYFILE_DISABLE=1 tar --no-mac-metadata --no-xattrs --uid 0 --gid 0 --uname root --gname root \
		-C "$s" -cf - ntmax-gxemul | gzip -9 >obj/ntmax-gxemul.tar.gz
	echo ">> built obj/ntmax-gxemul.tar.gz ($(du -h obj/ntmax-gxemul.tar.gz | cut -f1))"
}

build() {	# build TARGET: the binary, if it is not there yet (or FORCE=1), log in obj/portable/log
	local rc
	if [[ -d $P/bin/$1 && -z ${FORCE:-} ]]; then return 0; fi
	echo ">> building $1 (obj/portable/log/$1.log)"
	# (not in an || list: bash ignores set -e there, in the functions too)
	set +e
	(
		set -e
		case $1 in
		macos) macos ;;
		linux-x86_64) linux x86_64 linux/amd64 ;;
		linux-aarch64) linux aarch64 linux/arm64 ;;
		windows-x86_64) windows ;;
		esac
	) >"$P/log/$1.log" 2>&1
	rc=$?
	set -e
	if [[ $rc != 0 ]]; then
		rm -rf "$P/bin/$1"
		echo "!! $1 failed, see obj/portable/log/$1.log" >&2
	fi
	return $rc
}

[[ $# -gt 0 ]] || set -- macos linux-x86_64 linux-aarch64 windows-x86_64 pack
pids=() fail=0
for t; do
	case $t in
	pack) for p in ${pids[@]+"${pids[@]}"}; do wait "$p" || fail=1; done; pids=()
		[[ $fail = 0 ]] && pack ;;
	macos|linux-x86_64|linux-aarch64|windows-x86_64) build "$t" & pids+=($!) ;;
	*) echo "unknown target $t" >&2; exit 1 ;;
	esac
done
for p in ${pids[@]+"${pids[@]}"}; do wait "$p" || fail=1; done
exit $fail
