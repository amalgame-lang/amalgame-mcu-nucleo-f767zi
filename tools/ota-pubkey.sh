#!/usr/bin/env bash
# Generate the bootloader's public-key header from the ed25519 signing key (PEM). The header belongs to the
# application (boards/bootloader/build.sh takes it through OTA_PUBKEY_H); the private key stays where it is.
# Usage: tools/ota-pubkey.sh <private key .pem> <out.h>
set -euo pipefail
KEY="${1:?clé privée ed25519 (PEM)}"; OUTH="${2:?en-tête à produire (ex. ota_pubkey.h dans le dépôt de l'application)}"
HEX=$(openssl pkey -in "$KEY" -pubout -outform DER | tail -c 32 | od -An -v -tx1 | tr -d ' \n')
[ ${#HEX} -eq 64 ] || { echo "clé publique inattendue"; exit 1; }
{ echo "/* ed25519 public key of the OTA signing key (tools/ota-pubkey.sh) — generated, do not edit */"; echo "#include <stdint.h>"; printf 'static const uint8_t ota_pubkey[32] = {'; echo "$HEX" | sed 's/\(..\)/0x\1,/g'; echo '};'; } > "$OUTH"
echo "→ $OUTH ($(echo $HEX | cut -c1-16)…)"
