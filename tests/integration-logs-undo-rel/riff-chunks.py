#!/usr/bin/env python3
"""Per-chunk sha256 of a RIFF/WAVE file, so a render can be compared by payload.

The file hash alone also covers the header; the `data` payload is the audio.

    python3 riff-chunks.py <file.wav> [...]
"""
import hashlib
import struct
import sys


def main() -> int:
    for path in sys.argv[1:]:
        with open(path, "rb") as handle:
            raw = handle.read()
        print(f"{path}")
        print(f"  file sha256   {hashlib.sha256(raw).hexdigest()}  ({len(raw)} bytes)")
        if raw[:4] != b"RIFF" or raw[8:12] != b"WAVE":
            print("  NOT a RIFF/WAVE file")
            continue
        off = 12
        while off + 8 <= len(raw):
            cid = raw[off:off + 4]
            size = struct.unpack("<I", raw[off + 4:off + 8])[0]
            payload = raw[off + 8:off + 8 + size]
            extra = ""
            if cid == b"fmt" and size >= 16:
                fmt, ch, rate, brate, align, bits = struct.unpack("<HHIIHH", payload[:16])
                extra = (f"\n      fmt={fmt} channels={ch} sampleRate={rate} byteRate={brate}"
                         f" blockAlign={align} bitsPerSample={bits}")
            print(f"  chunk {cid.decode('latin1'):4s} size={size:>9d} "
                  f"payload sha256={hashlib.sha256(payload).hexdigest()}{extra}")
            off += 8 + size
            if size % 2:
                off += 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
