#!/usr/bin/env python3
"""Sign a slot image: fill the 512-byte header (magic, size, slot, load address, version, SHA-512 of the
body) and its ed25519 signature (openssl pkeyutl, RFC 8032) over the first 192 bytes.
Usage: ota-sign.py <in.bin> <out.bin> <A|B> <version> <key.pem>   (the input's first 512 bytes are the placeholder)"""
import sys, struct, hashlib, subprocess, tempfile, os
inp, out, slot, version, key = sys.argv[1:6]
data = bytearray(open(inp, 'rb').read()); assert len(data) > 512, "image trop courte"
s = 0 if slot.upper() == 'A' else 1; base = 0x08040000 if s == 0 else 0x080C0000
body = bytes(data[512:]); h = hashlib.sha512(body).digest()
hdr = struct.pack('<IIIIII', 0x5746434D, 1, len(body), s, base, 0) + version.encode()[:39].ljust(40, b'\0') + b'\0' * 64 + h
assert len(hdr) == 192
with tempfile.TemporaryDirectory() as d:
    p = os.path.join(d, 'h'); open(p, 'wb').write(hdr); q = os.path.join(d, 's')
    subprocess.run(['openssl', 'pkeyutl', '-sign', '-inkey', key, '-rawin', '-in', p, '-out', q], check=True)
    sig = open(q, 'rb').read(); assert len(sig) == 64
data[0:512] = (hdr + sig).ljust(512, b'\0')
open(out, 'wb').write(data)
print(f"{out}: slot {slot.upper()} @0x{base:08x} corps {len(body)} o version {version} sha512 {h.hex()[:16]}…")
