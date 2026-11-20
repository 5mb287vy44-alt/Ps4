#!/usr/bin/env bash
# Builds Neon Snake into an installable PS4 .pkg using the OpenOrbis toolchain.
# Needs: clang, lld, $OO_PS4_TOOLCHAIN (unpacked release), dotnet runtime (for PkgTool.Core).
set -euo pipefail

: "${OO_PS4_TOOLCHAIN:?OO_PS4_TOOLCHAIN is not set}"
TC="$OO_PS4_TOOLCHAIN"
BIN="$TC/bin/linux"
chmod +x "$BIN"/* 2>/dev/null || true

TITLE="Neon Snake"
VERSION="1.00"
TITLE_ID="BREW00777"
CONTENT_ID="IV0000-BREW00777_00-NEONSNAKE0000000"
NAME="neonsnake"

# Libraries linked into the ELF (same set as the OpenOrbis SDL2 sample).
LIBS="-lc -lkernel -lc++ -lSceUserService -lSceVideoOut -lSceAudioOut -lScePad -lSceSysmodule -lSceFreeType -lSDL2"

cd "$(dirname "$0")"
rm -rf build eboot.bin pkg.gp4 sce_sys/param.sfo ./*.pkg
mkdir -p build sce_sys/about sce_module

# --- icon (generated, so the repo needs no binary files) ---
if [ ! -f sce_sys/icon0.png ]; then
python3 - <<'PY'
import zlib, struct
S, C, M = 512, 64, 8
pts = [(1,1),(2,1),(3,1),(4,1),(5,1),(5,2),(5,3),(4,3),(3,3),(2,3),(2,4),(2,5),(3,5),(4,5),(5,5),(6,5)]
n = len(pts)
cells = {}
for i, p in enumerate(pts):
    t = i / (n - 1)
    cells[p] = (int(255 * t), int(255 * (1 - t)), int(255 - 60 * t))
cells[(6, 1)] = (57, 255, 20)
raw = bytearray()
for y in range(S):
    raw.append(0)
    cy, iy = divmod(y, C)
    for x in range(S):
        cx, ix = divmod(x, C)
        px = (8, 8, 22)
        c = cells.get((cx, cy))
        if c:
            if M <= ix < C - M and M <= iy < C - M:
                px = c
            elif M - 6 <= ix < C - M + 6 and M - 6 <= iy < C - M + 6:
                px = tuple(min(255, 8 + v // 4) for v in c)
        if x < 4 or y < 4 or x >= S - 4 or y >= S - 4:
            px = (0, 200, 255)
        raw.extend(px)
def chunk(tag, data):
    body = tag + data
    return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xffffffff)
png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", S, S, 8, 2, 0, 0, 0)) \
      + chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b"")
open("sce_sys/icon0.png", "wb").write(png)
PY
fi

# --- system modules and the "about" file come from the toolchain release ---
find_in_tc() { find "$TC" -name "$1" -print -quit 2>/dev/null; }
for f in libc.prx libSceFios2.prx; do
  src="$(find_in_tc "$f")"
  if [ -n "$src" ]; then cp -f "$src" "sce_module/$f"; else echo "WARNING: $f not found in toolchain"; fi
done
src="$(find_in_tc right.sprx)"
if [ -n "$src" ]; then cp -f "$src" sce_sys/about/right.sprx; else echo "ERROR: right.sprx not found in toolchain"; exit 1; fi

# --- compile ---
CFLAGS="--target=x86_64-pc-freebsd12-elf -fPIC -funwind-tables -O2 -c -isysroot $TC -isystem $TC/include"
OBJS=""
for c in src/*.c; do
  o="build/$(basename "${c%.c}").o"
  echo "CC  $c"
  clang $CFLAGS -o "$o" "$c"
  OBJS="$OBJS $o"
done

# --- link ---
echo "LD  build/$NAME.elf"
# shellcheck disable=SC2086
ld.lld $OBJS -o "build/$NAME.elf" -m elf_x86_64 -pie --script "$TC/link.x" --eh-frame-hdr -L"$TC/lib" $LIBS "$TC/lib/crt1.o"

# --- eboot.bin ---
echo "EBOOT"
"$BIN/create-fself" -in="build/$NAME.elf" -out="build/$NAME.oelf" --eboot "eboot.bin" --paid 0x3800000000000011

# --- param.sfo ---
echo "SFO"
SFO=sce_sys/param.sfo
"$BIN/PkgTool.Core" sfo_new "$SFO"
"$BIN/PkgTool.Core" sfo_setentry "$SFO" APP_TYPE --type Integer --maxsize 4 --value 1
"$BIN/PkgTool.Core" sfo_setentry "$SFO" APP_VER --type Utf8 --maxsize 8 --value "$VERSION"
"$BIN/PkgTool.Core" sfo_setentry "$SFO" ATTRIBUTE --type Integer --maxsize 4 --value 0
"$BIN/PkgTool.Core" sfo_setentry "$SFO" CATEGORY --type Utf8 --maxsize 4 --value 'gd'
"$BIN/PkgTool.Core" sfo_setentry "$SFO" CONTENT_ID --type Utf8 --maxsize 48 --value "$CONTENT_ID"
"$BIN/PkgTool.Core" sfo_setentry "$SFO" DOWNLOAD_DATA_SIZE --type Integer --maxsize 4 --value 0
"$BIN/PkgTool.Core" sfo_setentry "$SFO" SYSTEM_VER --type Integer --maxsize 4 --value 0
"$BIN/PkgTool.Core" sfo_setentry "$SFO" TITLE --type Utf8 --maxsize 128 --value "$TITLE"
"$BIN/PkgTool.Core" sfo_setentry "$SFO" TITLE_ID --type Utf8 --maxsize 12 --value "$TITLE_ID"
"$BIN/PkgTool.Core" sfo_setentry "$SFO" VERSION --type Utf8 --maxsize 8 --value "$VERSION"

# --- gp4 + pkg ---
echo "PKG"
FILES="eboot.bin sce_sys/about/right.sprx sce_sys/param.sfo sce_sys/icon0.png $(ls sce_module/* | tr '\n' ' ')"
"$BIN/create-gp4" -out pkg.gp4 --content-id="$CONTENT_ID" --files "$FILES"
"$BIN/PkgTool.Core" pkg_build pkg.gp4 .

ls -la ./*.pkg
echo "Done: $CONTENT_ID.pkg"
