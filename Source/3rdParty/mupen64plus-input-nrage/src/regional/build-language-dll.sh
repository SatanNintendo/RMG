#!/bin/bash
set -e

RC_FILE="${1:-}"
LANGID="${2:-}"
OUTDIR="${3:-build-language}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SRC_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

if [ -z "$RC_FILE" ] || [ -z "$LANGID" ]; then
    echo "usage: $0 <file.rc> <LANGID> [outdir]" >&2
    exit 1
fi
if [ "$LANGID" != "1049" ]; then
    echo "error: only Russian LANGID 1049 is supported" >&2
    exit 1
fi
if [ ! -f "$RC_FILE" ]; then
    echo "error: '$RC_FILE' not found" >&2
    exit 1
fi

# MSYS2 MINGW64 names these tools simply windres/gcc. A prefixed Linux
# cross-toolchain is also accepted as a fallback.
if command -v "${WINDRES:-windres}" >/dev/null 2>&1; then
    WINDRES="${WINDRES:-windres}"
    CC="${CC:-gcc}"
elif command -v x86_64-w64-mingw32-windres >/dev/null 2>&1; then
    WINDRES="${WINDRES:-x86_64-w64-mingw32-windres}"
    CC="${CC:-x86_64-w64-mingw32-gcc}"
else
    echo "error: windres not found; install the MINGW64 binutils/toolchain package" >&2
    exit 1
fi
command -v "$CC" >/dev/null 2>&1 || { echo "error: compiler '$CC' not found" >&2; exit 1; }

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

"$WINDRES" \
    -I "$SRC_DIR" \
    -I "$SCRIPT_DIR" \
    -O coff \
    --target=pe-x86-64 \
    -o "$TMP/res.o" \
    "$RC_FILE"

"$CC" -c -I "$SRC_DIR" "$SCRIPT_DIR/satellite-dllmain.c" -o "$TMP/dllmain.o"
mkdir -p "$OUTDIR"
"$CC" -shared -nostdlib -s -Wl,--entry,DllMain \
    -o "$OUTDIR/NRage-Language-${LANGID}.dll" \
    "$TMP/dllmain.o" "$TMP/res.o"

if command -v file >/dev/null 2>&1; then
    file "$OUTDIR/NRage-Language-${LANGID}.dll"
fi
echo "Built: $OUTDIR/NRage-Language-${LANGID}.dll"
