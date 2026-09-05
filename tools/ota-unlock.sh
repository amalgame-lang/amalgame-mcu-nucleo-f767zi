#!/usr/bin/env bash
# ota-unlock.sh — déverrouille la clé de signature OTA pour la session en cours, sans jamais la laisser
# en clair sur le disque. Sort la passphrase de KeePass dans un fichier de $XDG_RUNTIME_DIR (tmpfs, 600,
# effacé à la déconnexion) et imprime la ligne d'export à évaluer :
#
#   eval "$(tools/ota-unlock.sh)"        puis flash-full.sh autant de fois qu'on veut
#   tools/ota-unlock.sh --lock           efface le fichier tout de suite
#
# Un mot de passe maître par session de banc, au lieu d'une invite openssl à chaque compilation.
set -euo pipefail
KDBX="${KDBX:-$HOME/secrets/musicall.kdbx}"
ENTRY="${OTA_KP_ENTRY:-PKI/OTA signature ed25519}"
RUN="${XDG_RUNTIME_DIR:-/tmp}"; PASS="$RUN/musicall-ota.pass"
if [ "${1:-}" = "--lock" ]; then rm -f "$PASS"; echo "unset OTA_PASS_FILE"; exit 0; fi
if [ -s "$PASS" ]; then echo "export OTA_PASS_FILE=$PASS"; exit 0; fi   # déjà déverrouillée
command -v keepassxc-cli >/dev/null || { echo "echo 'keepassxc-cli absent' >&2" ; exit 1; }
[ -f "$KDBX" ] || { echo "echo '$KDBX introuvable' >&2"; exit 1; }
# l'invite part sur stderr : stdout ne porte QUE la ligne d'export, pour rester évaluable
# le terminal quand il y en a un (stdout doit rester évaluable), sinon l'entrée standard
if { exec 3</dev/tty; } 2>/dev/null
then printf 'Mot de passe MAÎTRE de %s : ' "$(basename "$KDBX")" >&2; read -rs M <&3; exec 3<&-
else read -rs M
fi
echo >&2
umask 077
printf '%s\n' "$M" | keepassxc-cli show -q -s -a Password "$KDBX" "$ENTRY" 2>/dev/null | tr -d '\n' > "$PASS" || true
unset M
[ -s "$PASS" ] || { rm -f "$PASS"; echo "echo 'passphrase introuvable dans « $ENTRY »' >&2"; exit 1; }
chmod 600 "$PASS"
echo "export OTA_PASS_FILE=$PASS"
