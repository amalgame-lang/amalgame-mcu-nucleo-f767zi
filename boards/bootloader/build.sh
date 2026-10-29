#!/usr/bin/env bash
# Build the bootloader → build/bootloader.{elf,bin}. Flash: st-flash --serial <SN> write build/bootloader.bin 0x08000000
# Usage: OTA_PUBKEY_H=<header> boards/bootloader/build.sh   — the header holds the public key that every OTA image must be
# signed with (tools/ota-pubkey.sh <key.pem> <header>); it belongs to the application, so it is REQUIRED here: no key,
# no bootloader (a bootloader with a wrong key refuses every image).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"; LIB="${LIBOPENCM3:-$HOME/libopencm3}"
OTA_PUBKEY_H="${OTA_PUBKEY_H:-}"
if [ -z "$OTA_PUBKEY_H" ] || [ ! -f "$OTA_PUBKEY_H" ]; then
  echo "bootloader: OTA_PUBKEY_H=<en-tête de la clé publique OTA> requis (introuvable : '${OTA_PUBKEY_H}')." >&2
  echo "  Le produire : tools/ota-pubkey.sh <clé privée ed25519 .pem> <sortie.h>, puis OTA_PUBKEY_H=<sortie.h> $0" >&2
  exit 1
fi
OTA_PUBKEY_H="$(cd "$(dirname "$OTA_PUBKEY_H")" && pwd)/$(basename "$OTA_PUBKEY_H")"
O="$(mktemp -d)"; mkdir -p "$ROOT/build"
CC=arm-none-eabi-gcc
CFLAGS=(-mcpu=cortex-m7 -mthumb -mfloat-abi=hard -mfpu=fpv5-d16 -Os -g -ffreestanding -nostartfiles -ffunction-sections -fdata-sections
  -DSTM32F7 -DBOARD_NO_ICACHE=0 -DAMC_BOARD_HEADER='"Amalgame_Mcu_Board.h"' -Werror=implicit-function-declaration
  -DOTA_PUBKEY_HEADER="\"$OTA_PUBKEY_H\""
  -I"$ROOT/runtime" -I"$ROOT/vendor/boot" -I"$ROOT/vendor/monocypher" -I"$LIB/include")
for c in "$ROOT/boards/bootloader/main.c" "$ROOT/boards/startup.c" "$ROOT/vendor/boot/bootctl.c" "$ROOT/vendor/boot/fault.c" "$ROOT/vendor/monocypher/monocypher.c" "$ROOT/vendor/monocypher/monocypher-ed25519.c"; do
  $CC "${CFLAGS[@]}" -c "$c" -o "$O/$(basename "$c").o"; done
$CC "${CFLAGS[@]}" -Wl,--gc-sections -T "$ROOT/boards/bootloader/bootloader.ld" "$O"/*.o -L"$LIB/lib" -lopencm3_stm32f7 -o "$ROOT/build/bootloader.elf"
arm-none-eabi-objcopy -O binary "$ROOT/build/bootloader.elf" "$ROOT/build/bootloader.bin"; rm -rf "$O"
arm-none-eabi-size "$ROOT/build/bootloader.elf" | tail -1
