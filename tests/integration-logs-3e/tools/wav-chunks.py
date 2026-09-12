#!/usr/bin/env python3
"""Print a WAV's chunk table, the sha256 of each chunk's payload, and the tag text.

usage: wav-chunks.py file.wav [file.wav ...]

Written for merge train 3E so the render question ("did the audio move or did a
metadata tag move?") is answered from the file itself: the `data` chunk's sha256
covers the samples only, the `LIST`/`INFO` chunk carries ISFT, and the file
sha256 covers everything.  Fields are labelled from the actual offsets - a
previous train's throwaway parser printed them in the wrong order.
"""
import hashlib
import struct
import sys


def chunks(b):
    pos, out = 12, []
    while pos + 8 <= len(b):
        cid = b[pos:pos + 4]
        size = struct.unpack("<I", b[pos + 4:pos + 8])[0]
        payload = b[pos + 8:pos + 8 + size]
        out.append((cid, size, payload))
        pos += 8 + size + (size & 1)
    return out


def info_tags(payload):
    tags = []
    if payload[:4] != b"INFO":
        return tags
    pos = 4
    while pos + 8 <= len(payload):
        tid = payload[pos:pos + 4]
        tsz = struct.unpack("<I", payload[pos + 4:pos + 8])[0]
        val = payload[pos + 8:pos + 8 + tsz].split(b"\x00")[0]
        tags.append((tid.decode("ascii", "replace"), val.decode("utf-8", "replace")))
        pos += 8 + tsz + (tsz & 1)
    return tags


for path in sys.argv[1:]:
    b = open(path, "rb").read()
    print(f"\n{path}")
    print(f"  file sha256   {hashlib.sha256(b).hexdigest()}  ({len(b)} bytes)")
    for cid, size, payload in chunks(b):
        name = cid.decode("ascii", "replace")
        print(f"  chunk {name:4s}   size={size:9d}  payload sha256={hashlib.sha256(payload).hexdigest()}")
        if name == "fmt " and size >= 16:
            fmt, ch, rate, byterate, align, bits = struct.unpack("<HHIIHH", payload[:16])
            print(f"      fmt={fmt} channels={ch} sampleRate={rate} byteRate={byterate} "
                  f"blockAlign={align} bitsPerSample={bits}")
        if name == "LIST":
            for t, v in info_tags(payload):
                print(f"      INFO {t} = {v!r}")
