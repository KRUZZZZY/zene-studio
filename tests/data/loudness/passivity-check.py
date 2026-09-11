#!/usr/bin/env python3
"""Passivity check for the loudness report: two renders of the same project,
one with the report on and one with it off.

The claim it tests is that measuring a render cannot change it. The comparison
is made three ways, so a cosmetic metadata difference cannot hide an audio
difference (or be mistaken for one):

  1. the "data" chunk - the audio itself - byte for byte;
  2. sample by sample, as floats;
  3. every byte of the file that differs at all, located: anything outside
     libsndfile's "PEAK" metadata chunk would be a real difference. The PEAK
     chunk carries the wall-clock second of the write, which is why two runs
     are not md5-identical files even when the audio is.

Exit status 0 only if the audio is bit-identical and every other difference
lies inside the PEAK chunk.

Usage: python3 passivity-check.py <with-report.wav> <without-report.wav>
"""

import hashlib
import struct
import sys
import time


def chunks(raw):
    """(id, body_offset, size) for every RIFF chunk."""
    out = []
    offset = 12
    while offset + 8 <= len(raw):
        chunk_id = raw[offset:offset + 4]
        size = struct.unpack_from("<I", raw, offset + 4)[0]
        out.append((chunk_id, offset + 8, size))
        offset += 8 + size + (size & 1)
    return out


def find(raw, wanted):
    for chunk_id, body, size in chunks(raw):
        if chunk_id == wanted:
            return body, size
    raise SystemExit(f"no {wanted!r} chunk")


def samples(raw):
    body, size = find(raw, b"data")
    payload = raw[body:body + size]
    return struct.unpack("<%df" % (len(payload) // 4), payload)


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    with_report, without_report = sys.argv[1], sys.argv[2]
    a = open(with_report, "rb").read()
    b = open(without_report, "rb").read()
    if len(a) != len(b):
        print(f"FILE SIZES DIFFER: {len(a)} vs {len(b)}")
        return 1

    payload_a = a[find(a, b"data")[0]:find(a, b"data")[0] + find(a, b"data")[1]]
    payload_b = b[find(b, b"data")[0]:find(b, b"data")[0] + find(b, b"data")[1]]
    frames_a, frames_b = samples(a), samples(b)

    peak_body, peak_size = find(a, b"PEAK")
    differing = [i for i in range(len(a)) if a[i] != b[i]]
    outside_peak = [i for i in differing if not peak_body <= i < peak_body + peak_size]

    print(f"with report    : {with_report} ({len(a)} bytes)")
    print(f"without report : {without_report} ({len(b)} bytes)")
    print(f"1. data chunk (the audio) : sha256 {hashlib.sha256(payload_a).hexdigest()[:32]}  "
          f"{'IDENTICAL' if payload_a == payload_b else 'DIFFERENT'}")
    delta = max((abs(frames_a[i] - frames_b[i]) for i in range(len(frames_a))), default=0.0)
    print(f"2. sample-by-sample       : {len(frames_a)} frames, max |delta| = {delta}  "
          f"{'IDENTICAL' if delta == 0.0 and len(frames_a) == len(frames_b) else 'DIFFERENT'}")
    print(f"3. differing bytes        : {len(differing)} of {len(a)}"
          f"{'' if not differing else f' at {differing[:8]}'}")
    if differing:
        stamps = [struct.unpack_from('<I', a, i)[0] for i in differing[:4]] \
            if not outside_peak else []
        if stamps:
            readable = ", ".join(time.strftime('%Y-%m-%d %H:%M:%S', time.gmtime(s)) for s in stamps)
            print(f"   PEAK chunk body at {peak_body} (size {peak_size}); the differing bytes read "
                  f"as Unix seconds: {readable}")
    print(f"   bytes differing outside the PEAK chunk: {len(outside_peak)}")
    if outside_peak:
        print(f"   first such offset: {outside_peak[:8]}")

    ok = (payload_a == payload_b and delta == 0.0 and len(frames_a) == len(frames_b)
          and not outside_peak)
    print("PASSIVITY: " + ("PASS - measuring the render did not change it" if ok
                           else "FAIL - the report altered the render"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
