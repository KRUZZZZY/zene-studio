#!/usr/bin/env python3
"""Verify the placeholder rasters: dimensions vs the pre-change snapshot, container
structure, placeholder metadata, and that none is byte-identical to upstream."""
import struct, subprocess, os, sys, zlib
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))  # .../docs/evidence/brand-placeholders -> repo root
os.chdir(ROOT)
fail = 0
def bad(msg):
    global fail; fail = 1; print("  FAIL " + msg)

# --- 1. dimensions must equal the pre-change snapshot -----------------------
snap = {}
for line in open(os.path.join(HERE, 'pre-change-dims.txt')):
    parts = line.split()
    if len(parts) >= 4:
        wh = parts[0]; snap[parts[-1]] = tuple(int(v) for v in wh.split('x'))
print(f"[1] dimension check against pre-change-dims.txt ({len(snap)} files)")
for f, want in sorted(snap.items()):
    d = open(f,'rb').read()
    if d[:8] != b'\x89PNG\r\n\x1a\n': bad(f + " is not a PNG"); continue
    got = struct.unpack('>II', d[16:24])
    if got != want: bad(f" {f}: {got} != {want}")
print(f"    checked {len(snap)} PNGs, {0 if not fail else 'see above'}")

# --- 2. every PNG carries the placeholder note ------------------------------
print("[2] placeholder metadata (tEXt Description) in every PNG")
n = 0
for f in sorted(snap):
    d = open(f,'rb').read(); i = 8; found = None
    while i < len(d):
        ln = struct.unpack('>I', d[i:i+4])[0]; typ = d[i+4:i+8]
        if typ == b'tEXt': found = d[i+8:i+8+ln].decode('latin-1')
        i += 12 + ln
    n += 1
    if not found or 'placeholder' not in found: bad(f"{f}: tEXt='{found}'")
print(f"    {n} PNGs scanned")

# --- 3. ICO structure -------------------------------------------------------
print("[3] ICO container structure")
for f in ('cmake/nsis/icon.ico','cmake/nsis/project.ico'):
    d = open(f,'rb').read()
    res, typ, cnt = struct.unpack('<HHH', d[:6])
    sizes = []
    for i in range(cnt):
        w,h,_,_,_,bc,sz,off = struct.unpack('<BBBBHHII', d[6+i*16:22+i*16])
        sizes.append((w or 256, bc, sz))
    print(f"    {f}: type={typ} entries={cnt} (size,bpp,bytes)={sizes}")
    if typ != 1 or cnt != 8: bad(f + " wrong container")
    if sizes != [(16,32,1128),(24,32,2440),(32,32,4264),(48,32,9640),(64,32,16936),(96,32,38056),(128,32,67624),(256,32,270376)]:
        bad(f + " entry byte counts differ from upstream's")
    else:
        print("    (byte counts identical to upstream's own icon.ico entries)")

# --- 4. ICNS structure ------------------------------------------------------
print("[4] ICNS container structure")
for f in ('cmake/apple/icon.icns','cmake/apple/project.icns'):
    d = open(f,'rb').read()
    if d[:4] != b'icns': bad(f + " bad magic"); continue
    total = struct.unpack('>I', d[4:8])[0]
    if total != len(d): bad(f" {f}: declared {total} != actual {len(d)}")
    i = 8; chunks = []
    while i < len(d):
        t = d[i:i+4].decode('latin1'); ln = struct.unpack('>I', d[i+4:i+8])[0]
        body = d[i+8:i+ln]
        ok = body[:8] == b'\x89PNG\r\n\x1a\n'
        wh = struct.unpack('>II', body[16:24]) if ok else None
        chunks.append((t, ln, wh))
        if ln < 8 or i + ln > len(d): bad(f + " bad chunk length")
        i += ln
    print(f"    {f}: total={total} chunks={chunks}")
    if i != len(d): bad(f + " chunk walk did not land on EOF")

# --- 5. nothing may still be byte-identical to upstream ---------------------
def _default_pre() -> str:
    # The pre-change revision: the branch this placeholder work was based on.  HEAD~1 is
    # only right while the artwork commit is still the tip.
    if os.environ.get('PRE_CHANGE_REV'):
        return os.environ['PRE_CHANGE_REV']
    for cand in ('post-alpha/rename-complete', 'HEAD~2'):
        if subprocess.run(f'git rev-parse --verify --quiet {cand}^{{commit}}', shell=True,
                          capture_output=True).returncode == 0:
            return cand
    return 'HEAD'


PRE = _default_pre()
print(f"[5] byte-identity against origin/master (same path and pre-rename path); 'unchanged' compared to {PRE}")
ren = {}
out = subprocess.run(['git','diff','--name-status','-M','origin/master..HEAD'],
                     capture_output=True, text=True).stdout
for line in out.splitlines():
    if line.startswith('R'):
        p = line.split('\t'); ren[p[2]] = p[1]
targets = sorted(snap) + ['cmake/nsis/icon.ico','cmake/nsis/project.ico',
                          'cmake/apple/icon.icns','cmake/apple/project.icns']
still = 0
for f in targets:
    for up in filter(None, [f, ren.get(f)]):
        r = subprocess.run(f'git show origin/master:{up} 2>/dev/null | cmp -s - {f}',
                           shell=True)
        if r.returncode == 0:
            bad(f" {f} is STILL byte-identical to upstream {up}"); still += 1
    # also: is the current file identical to the PRE-CHANGE copy?  PRE defaults to
    # HEAD~1 because this branch's tip commit is the placeholder change itself; set
    # PRE_CHANGE_REV to re-run the check against a different base.
    r = subprocess.run(f'git show {PRE}:{f} 2>/dev/null | cmp -s - {f}', shell=True)
    if r.returncode == 0: bad(f" {f} unchanged from {PRE}")
print(f"    {len(targets)} files compared; still-upstream={still}")

print()
print("RESULT:", "FAIL" if fail else "PASS")
sys.exit(1 if fail else 0)
