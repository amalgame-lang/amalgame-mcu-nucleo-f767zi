#!/usr/bin/env bash
# Build F767ZI firmware: amc → freestanding C → cross-compile against libopencm3.
#
# Prereqs:
#   - arm-none-eabi-gcc
#   - libopencm3 built for stm32f7:
#       git clone https://github.com/libopencm3/libopencm3
#       cd libopencm3 && make TARGETS=stm32/f7
#     then point LIBOPENCM3 at that checkout.
#   - amc (AMC env var, or auto-detected sibling ../Amalgame/amc)
#
# Usage:  LIBOPENCM3=/path/to/libopencm3 ./build.sh [entry.am]
set -euo pipefail
cd "$(dirname "$0")"

: "${LIBOPENCM3:?set LIBOPENCM3 to your libopencm3 checkout (with lib/libopencm3_stm32f7.a built)}"
AMC="${AMC:-$(cd ../Amalgame 2>/dev/null && pwd)/amc}"
[ -x "$AMC" ] || { echo "amc not found (set AMC=/path/to/amc)"; exit 1; }
AMC_ROOT="$(cd "$(dirname "$AMC")" && pwd)"
EMB="$AMC_ROOT/runtime/embedded"
[ -d "$EMB" ] || { echo "embedded runtime not found at $EMB"; exit 1; }
ENTRY="${1:-blink.am}"

echo ">> amc --target=cortex-m7 $ENTRY  (transpile to freestanding C)"
"$AMC" --target=cortex-m7 "$ENTRY" -o firmware

echo ">> arm-none-eabi-gcc + libopencm3"
arm-none-eabi-gcc -mcpu=cortex-m7 -mthumb -mfloat-abi=hard -mfpu=fpv5-d16 \
  -Os -g -ffreestanding -nostartfiles -DSTM32F7 \
  -I"$EMB" -Iruntime -I"$LIBOPENCM3/include" \
  -T boards/stm32f767zi.ld boards/startup.c firmware.c \
  -L"$LIBOPENCM3/lib" -lopencm3_stm32f7 \
  -o firmware.elf

arm-none-eabi-objcopy -O binary firmware.elf firmware.bin
arm-none-eabi-objcopy -O ihex   firmware.elf firmware.hex
arm-none-eabi-size firmware.elf
echo
echo "OK: firmware.elf / .bin / .hex"
echo "Flash (board plugged via USB, on-board ST-LINK):  st-flash write firmware.bin 0x08000000"
