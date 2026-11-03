#!/bin/sh
cd "$(dirname "$0")" || exit 1
case "$(uname -s)-$(uname -m)" in
Darwin-*) g=bin/macos/gxemul; xattr -cr bin/macos 2>/dev/null ;;
Linux-x86_64) g=bin/linux-x86_64/gxemul; export LD_LIBRARY_PATH=bin/linux-x86_64/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH} ;;
Linux-aarch64) g=bin/linux-aarch64/gxemul; export LD_LIBRARY_PATH=bin/linux-aarch64/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH} ;;
*) echo "No gxemul binary for $(uname -sm)" >&2; exit 1 ;;
esac
exec "$g" -e 3max -X -j osloader -d ntmax-disk.img -d c4: "$@"
