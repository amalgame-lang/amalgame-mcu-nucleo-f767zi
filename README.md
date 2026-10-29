# amalgame-mcu-nucleo-f767zi

`Amalgame.Mcu` HAL board support for the **ST Nucleo-F767ZI** (STM32F767ZI,
Cortex-M7F), wrapping **libopencm3**. See the amc MCU proposal
(`docs/proposals/amc-embedded.md` §9) for the layering: the `Amalgame.Mcu`
facade (declarations) ships with amc; this package supplies the real `Mcu_*`
bodies + named pin constants for one board.

> ⚠️ **Status: scaffold — not yet built or flashed.** It is written against
> the libopencm3 API and the F767ZI pin map (UM1974) but was authored without
> the board or an `arm-none-eabi` + libopencm3 setup at hand. Validate the
> clock/pin setup and the (currently busy-wait) `Mcu.DelayMs` before trusting
> timing. Contributions/fixes from a real bring-up welcome.

## What's here

| File | Role |
|---|---|
| `runtime/Amalgame_Mcu_Board.h` | Real `Mcu_*` over libopencm3; defines `AMC_HAVE_MCU_BOARD` to suppress amc's virtual/semihosting default. Pin = `port*16+bit`; `Board_LedGreen` = PB0, etc. |
| `boards/startup.c` | Minimal Cortex-M7 reset → `amc_main()`. (Use libopencm3's startup for full IRQ vectors.) |
| `boards/stm32f767zi.ld` | Flash @0x08000000 (2 MB), SRAM @0x20000000 (512 KB). |
| `amalgame.toml` | Package manifest: `[stdlib]` (Mcu/Board, libopencm3) + `[target]` (cortex-m7, flash via st-flash). |

## Build + flash (on a machine with the toolchain)

```bash
# prerequisites: arm-none-eabi-gcc, libopencm3 (built for stm32f7), st-flash
amc --target=cortex-m7 blink.am -o blink     # transpile to freestanding C
arm-none-eabi-gcc -mcpu=cortex-m7 -mthumb -mfloat-abi=hard -mfpu=fpv5-d16 \
  -Os -ffreestanding -nostartfiles -DSTM32F7 \
  -I<amc>/runtime/embedded -Iruntime -I<libopencm3>/include \
  -T boards/stm32f767zi.ld boards/startup.c blink.c \
  -L<libopencm3>/lib -lopencm3_stm32f7 -o blink.elf
arm-none-eabi-objcopy -O binary blink.elf blink.bin
st-flash write blink.bin 0x08000000          # on-board ST-LINK/V2-1, LD1 should blink
```

The same `examples/mcu/blink.am` from the amc repo drives this board — the
`led = 13` there is the virtual-board pin; for real hardware use
`Board_LedBuiltin` (PB0) once the opaque-typed `Pin` / `Board` constants land
(follow-up). Until full `amc build --target=cortex-m7 --board=…` driver
integration lands, link with the explicit `arm-none-eabi-gcc` line above.

## Toolchain (one-shot install)

```bash
tools/mcu-setup.sh        # in the amc repo: installs arm-none-eabi-gcc, openocd,
                          # gdb-multiarch, qemu, and builds libopencm3 (stm32/f7)
export AMC_EMBED_INC=~/libopencm3/include
export AMC_EMBED_LIB=~/libopencm3/lib
```
`amc build --target=…` / `amc dap --target=…` preflight-check each tool and print
the exact install command if one is missing.

## VSCode — flash + debug like a real MCU IDE

This folder ships `.vscode/tasks.json` + `.vscode/launch.json`. With the Amalgame
extension installed:

- **Flash**: `Ctrl-Shift-B` (default build task → `amc build --target … --board … --flash`)
  — builds and writes the firmware to the board over the on-board ST-LINK.
- **Debug (F5)**: runs the `Debug on MCU (F767ZI)` config → the extension launches
  `amc dap --target=cortex-m7 --openocd=board/st_nucleo_f7.cfg`, which connects via
  OpenOCD, flashes, resets, and stops at entry. Set breakpoints **directly in your
  `.am` file**, step, inspect Amalgame locals — on the real chip.

The debug config keys (`target`, `openocd`) are forwarded by the extension to
`amc dap`. Build first happens automatically via the `preLaunchTask`.

## Follow-ups

- Wire board discovery into `amc build --target=cortex-m7` so the link step is
  one command (today the driver only auto-resolves the bundled QEMU M3 board).
- Opaque typed `Pin` + `Board.<pin>` constants (the proposal's §9.1 design;
  slice 6 uses plain `int`).
- SysTick-based `Mcu.Millis()` / accurate `Mcu.DelayMs()`; PLL clock setup.

## Option `MC_AUDIO_CKIN=1` (2026-09-13)
Horloge SAI1 prise sur un oscillateur externe 12,288 MHz relié à PC9 (I2S_CKIN, AF5) au lieu du PLLSAI entier
(`boards/clock.c`, `vendor/audio/audio_sai.c` : SAI1SEL = 10, MCKDIV = 0 → MCLK 12,288 MHz exact, LRCK 48 000 Hz).
Réservé à une carte qui porte cet oscillateur. Depuis le 2026-10-26, `clock.c` vérifie au démarrage que PC9 bat
(`board_audio_ckin`) et reste sinon sur le PLLSAI. **Ne pas compiler avec ce drapeau pour une Nucleo-F767ZI** (rien
sur PC9). Sans le drapeau, rien ne change (PLLSAI, 47 991 Hz).

## Points d'accroche pour une autre carte (2026-10-29)
Le paquet vise la Nucleo-F767ZI. Une carte dérivée garde ses particularités (brochage, pilotes) dans son propre
dépôt et les branche par un en-tête passé à la compilation : `-I<dossier> -DBOARD_EXT_HEADER='"ma_carte.h"'`.
`vendor/lwip-port/ethernetif.c` et `vendor/audio/audio_sai.c` l'incluent ; il peut définir :

| Macro | Rôle | Défaut (Nucleo-F767ZI) |
|---|---|---|
| `ETH_RMII_TXB_PINS` | broches RMII d'émission (TX_EN, TXD0, TXD1) prises sur le port B, AF11 | `GPIO13` (TXD1) |
| `ETH_RMII_TXG_PINS` | les mêmes sur le port G (`0` = aucune) | `GPIO11 \| GPIO13` (TX_EN, TXD0) |
| `ETH_PHY_NRST_PORT`, `ETH_PHY_NRST_PIN`, `ETH_PHY_NRST_RCC` | broche du MCU reliée au nRST du PHY : 30 ms de reset matériel avant le MDIO | absentes (nRST sur NRST) |
| `AUD_BOARD_EXT` | nom d'un fragment C inclus dans `audio_sai.c` (ci-dessous) | absent |

Le fragment `AUD_BOARD_EXT` n'est pas une unité de compilation : `audio_sai.c` l'inclut à trois endroits, avec
`AUD_EXT_STAGE` = 1 (il définit `aud_sorties()`, appelée à la fin de `aud_spk_start()` et par l'application avant un
redémarrage logiciel), 2 (déclarations avant `aud_mic_take_raw2()`, dont au besoin `AUD_MIC_FRAME_HOOK(l, r)`, appelée
sur chaque trame capturée) puis 3 (pilotes propres à la carte, que l'application appelle par des symboles faibles). Il
partage donc les statiques du fichier. Sans `BOARD_EXT_HEADER`, l'image Nucleo est inchangée.

## Valeurs fournies par l'application (2026-10-29)
Ce paquet ne contient ni clé, ni racine privée, ni nom de produit : l'application qui l'utilise fournit les siens.

| Où | Point d'accroche | Défaut du paquet |
|---|---|---|
| Bootloader (`boards/bootloader/build.sh`) | `OTA_PUBKEY_H=<en-tête>` : clé publique ed25519 que doit porter toute image OTA. **Obligatoire** : sans elle, le script s'arrête avec un message (et `main.c` refuse de compiler). En-tête produit par `tools/ota-pubkey.sh <clé privée .pem> <sortie.h>`. | aucun |
| Racines TLS (`vendor/mbedtls-port/build-mbedtls.sh`) | `MCU_TLS_ROOTS_C=<fichier .c>` : magasin de confiance compilé à la place de `mbedtls_roots.c` (mêmes symboles `mcu_tls_roots_pem` / `mcu_tls_roots_pem_len`). Construire alors SA propre archive (argument `out.a`), jamais celle par défaut. | ISRG Root X1, X2, YE |
| Autotest TLS (`tls_selftest.c`) | `MCU_TLS_CFLAGS="-DMCU_TLS_SELFTEST_HOST=… -DMCU_TLS_SELFTEST_PERS=…"` (passé à `build-mbedtls.sh`) | `example.com`, `mcu-tls` |
| Nom réseau (`vendor/lwip-port/net_mcu.c`) | `-DNET_HOSTNAME_PREFIX='"monappareil-"'` : nom DHCP / mDNS = préfixe + 8 chiffres hexa de l'UID | `mcu-` |
| Signature (`tools/ota-sign.py`) | `OTA_PASS_FILE` : fichier qui contient la passphrase d'une clé chiffrée | aucun |
