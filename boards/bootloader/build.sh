#!/usr/bin/env bash
# Build the bootloader → build/bootloader.{elf,bin}. Flash: st-flash --serial <SN> write build/bootloader.bin 0x08000000
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"; LIB="${LIBOPENCM3:-$HOME/libopencm3}"; O="$(mktemp -d)"; mkdir -p "$ROOT/build"
CC=arm-none-eabi-gcc
CFLAGS=(-mcpu=cortex-m7 -mthumb -mfloat-abi=hard -mfpu=fpv5-d16 -Os -g -ffreestanding -nostartfiles -ffunction-sections -fdata-sections
  -DSTM32F7 -DBOARD_NO_ICACHE=0 -DAMC_BOARD_HEADER='"Amalgame_Mcu_Board.h"' -Werror=implicit-function-declaration
  -I"$ROOT/runtime" -I"$ROOT/vendor/boot" -I"$ROOT/vendor/monocypher" -I"$LIB/include")
for c in "$ROOT/boards/bootloader/main.c" "$ROOT/boards/startup.c" "$ROOT/vendor/boot/bootctl.c" "$ROOT/vendor/monocypher/monocypher.c" "$ROOT/vendor/monocypher/monocypher-ed25519.c"; do
  $CC "${CFLAGS[@]}" -c "$c" -o "$O/$(basename "$c").o"; done
$CC "${CFLAGS[@]}" -Wl,--gc-sections -T "$ROOT/boards/bootloader/bootloader.ld" "$O"/*.o -L"$LIB/lib" -lopencm3_stm32f7 -o "$ROOT/build/bootloader.elf"
arm-none-eabi-objcopy -O binary "$ROOT/build/bootloader.elf" "$ROOT/build/bootloader.bin"; rm -rf "$O"
arm-none-eabi-size "$ROOT/build/bootloader.elf" | tail -1
