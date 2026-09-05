#!/usr/bin/env bash
# Compile the vendored mbedTLS (library/*.c) + port for cortex-m7 into a static archive.
# Usage: vendor/mbedtls-port/build-mbedtls.sh [out.a]   (default: build/libmbedtls-f7.a)
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"; ROOT="$(cd "$HERE/../.." && pwd)"
LINKPROOF=0; for a in "$@"; do [ "$a" = "--linkproof" ] && LINKPROOF=1; done
OUT="$ROOT/build/libmbedtls-f7.a"; [ $# -ge 1 ] && [ "$1" != "--linkproof" ] && OUT="$1"
LIB="${LIBOPENCM3:-$HOME/libopencm3}"
OBJ="$(mktemp -d)"; mkdir -p "$(dirname "$OUT")"
CC=arm-none-eabi-gcc
CFLAGS=(-mcpu=cortex-m7 -mthumb -mfloat-abi=hard -mfpu=fpv5-d16 -Os -g -ffreestanding -ffunction-sections -fdata-sections
  -DSTM32F7 -DMBEDTLS_CONFIG_FILE='"mcu_mbedtls_config.h"' -Werror=implicit-function-declaration
  -I"$HERE" -I"$ROOT/vendor/time" -I"$ROOT/vendor/mbedtls/include" -I"$ROOT/vendor/mbedtls/library" -I"$LIB/include")
n=0
for c in "$ROOT"/vendor/mbedtls/library/*.c "$HERE"/mbedtls_port.c "$HERE"/mbedtls_roots.c "$HERE"/tls_selftest.c; do
  $CC "${CFLAGS[@]}" -c "$c" -o "$OBJ/$(basename "$c" .c).o"; n=$((n+1))
done
arm-none-eabi-ar rcs "$OUT" "$OBJ"/*.o; rm -rf "$OBJ"
echo ">> $OUT ($n files)"; arm-none-eabi-size -t "$OUT" | tail -1

# ── optional link proof: --linkproof → build/mbedtls-linkproof.elf (sizes only, not flashed) ──
if [ $LINKPROOF = 1 ]; then
  ELF="$ROOT/build/mbedtls-linkproof.elf"; O="$(mktemp -d)"
  for c in "$HERE/linkproof.c" "$ROOT/vendor/time/wallclock.c" "$ROOT/boards/startup.c" "$ROOT/boards/clock.c"; do $CC "${CFLAGS[@]}" -DAMC_BOARD_HEADER='"Amalgame_Mcu_Board.h"' -I"$ROOT/runtime" -c "$c" -o "$O/$(basename "$c").o"; done
  $CC -mcpu=cortex-m7 -mthumb -mfloat-abi=hard -mfpu=fpv5-d16 -Os -ffreestanding -nostartfiles -Wl,--gc-sections \
    -T "$ROOT/boards/stm32f767zi.ld" "$O"/*.o "$OUT" -L"$LIB/lib" -lopencm3_stm32f7 -o "$ELF"
  rm -rf "$O"; echo ">> $ELF"; arm-none-eabi-size "$ELF" | tail -1
fi
