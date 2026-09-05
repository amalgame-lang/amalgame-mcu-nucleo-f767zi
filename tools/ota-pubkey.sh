#!/usr/bin/env bash
# Generate vendor/boot/ota_pubkey.h from the ed25519 signing key (PEM). The private key never leaves ~/secrets.
# Usage: tools/ota-pubkey.sh ~/secrets/musicall-ota-ed25519.pem
set -euo pipefail
KEY="${1:?clé privée ed25519 (PEM)}"; HERE="$(cd "$(dirname "$0")/.." && pwd)"
HEX=$(openssl pkey -in "$KEY" -pubout -outform DER | tail -c 32 | od -An -v -tx1 | tr -d ' \n')
[ ${#HEX} -eq 64 ] || { echo "clé publique inattendue"; exit 1; }
{ echo "/* ed25519 public key of the OTA signing key (tools/ota-pubkey.sh) — generated, do not edit */"; echo "#include <stdint.h>"; printf 'static const uint8_t ota_pubkey[32] = {'; echo "$HEX" | sed 's/\(..\)/0x\1,/g'; echo '};'; } > "$HERE/vendor/boot/ota_pubkey.h"
echo "→ vendor/boot/ota_pubkey.h ($(echo $HEX | cut -c1-16)…)"
