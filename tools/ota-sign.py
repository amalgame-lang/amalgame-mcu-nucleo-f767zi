#!/usr/bin/env python3
"""Sign a slot image: fill the 512-byte header (magic, size, slot, load address, version, SHA-512 of the
body) and its ed25519 signature (openssl pkeyutl, RFC 8032) over the first 192 bytes.
Usage: ota-sign.py <in.bin> <out.bin> <A|B> <version> <key.pem>   (the input's first 512 bytes are the placeholder)

The signing key may be ENCRYPTED (recommended: it is what makes a firmware acceptable to every board's
bootloader). Point OTA_PASS_FILE at a file holding its passphrase — see tools/ota-unlock.sh, which pulls it
out of KeePass into a tmpfs file for the session. Without it, openssl simply asks on the terminal."""
import sys, struct, hashlib, subprocess, tempfile, os
inp, out, slot, version, key = sys.argv[1:6]
data = bytearray(open(inp, 'rb').read()); assert len(data) > 512, "image trop courte"
s = 0 if slot.upper() == 'A' else 1; base = 0x08040000 if s == 0 else 0x080C0000
body = bytes(data[512:]); h = hashlib.sha512(body).digest()
hdr = struct.pack('<IIIIII', 0x5746434D, 1, len(body), s, base, 0) + version.encode()[:39].ljust(40, b'\0') + b'\0' * 64 + h
assert len(hdr) == 192
with tempfile.TemporaryDirectory() as d:
    p = os.path.join(d, 'h'); open(p, 'wb').write(hdr); q = os.path.join(d, 's')
    cmd = ['openssl', 'pkeyutl', '-sign', '-inkey', key, '-rawin', '-in', p, '-out', q]
    passf = os.environ.get('OTA_PASS_FILE')
    if passf:
        if not os.path.exists(passf): sys.exit(f"OTA_PASS_FILE={passf} introuvable (tools/ota-unlock.sh ?)")
        cmd += ['-passin', 'file:' + passf]
    try: subprocess.run(cmd, check=True, stderr=subprocess.PIPE)
    except subprocess.CalledProcessError as e:
        msg = (e.stderr or b'').decode(errors='replace').strip()
        sys.exit(f"signature impossible avec {key}\n  openssl: {msg}\n"
                 + ("  (clé chiffrée : eval \"$(tools/ota-unlock.sh)\" une fois par session)" if not passf else "  (passphrase incorrecte ?)"))
    sig = open(q, 'rb').read(); assert len(sig) == 64
data[0:512] = (hdr + sig).ljust(512, b'\0')
open(out, 'wb').write(data)
print(f"{out}: slot {slot.upper()} @0x{base:08x} corps {len(body)} o version {version} sha512 {h.hex()[:16]}…")
