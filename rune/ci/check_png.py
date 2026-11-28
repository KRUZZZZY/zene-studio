#!/usr/bin/env python3
"""Sanity check for a headless --snap render (stdlib only): PNG signature, IHDR size, minimum file size.

A blank or failed render is a few KB; the real 1366x768 piano scene is about 195 KB, so 50 KB is the floor.
  python ci/check_png.py ci-out/snap.png 1366 768 [--min-bytes 50000]
Prints a JSON object; exit 0 if ok.
"""
import json, pathlib, struct, sys


def main(argv):
    path, width, height = argv[1], int(argv[2]), int(argv[3])
    min_bytes = int(argv[argv.index('--min-bytes') + 1]) if '--min-bytes' in argv else 50000
    r = {'file': path, 'ok': False, 'bytes': None, 'width': None, 'height': None, 'reason': None}
    p = pathlib.Path(path)
    if not p.is_file():
        r['reason'] = 'file missing'
    else:
        data = p.read_bytes()
        r['bytes'] = len(data)
        if data[:8] != b'\x89PNG\r\n\x1a\n' or data[12:16] != b'IHDR':
            r['reason'] = 'not a PNG'
        else:
            r['width'], r['height'] = struct.unpack('>II', data[16:24])
            if (r['width'], r['height']) != (width, height):
                r['reason'] = f'size {r["width"]}x{r["height"]}, wanted {width}x{height}'
            elif len(data) < min_bytes:
                r['reason'] = f'only {len(data)} bytes (< {min_bytes}): looks blank'
            else:
                r['ok'] = True
    print(json.dumps(r))
    return 0 if r['ok'] else 1


if __name__ == '__main__':
    sys.exit(main(sys.argv))
