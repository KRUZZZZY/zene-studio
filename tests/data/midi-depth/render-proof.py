#!/usr/bin/env python3
"""Render the MIDI-depth fixtures headlessly and check the claims that matter.

    python3 tests/data/midi-depth/render-proof.py render  <build-dir> <out-dir>
    python3 tests/data/midi-depth/render-proof.py compare <pre-dir> <post-dir>

`render` renders every fixture twice (a determinism repeat) and writes
<out-dir>/manifest.tsv. `compare` checks the claims below and exits non-zero if
any fails, so a comparator that always says "equal" cannot pass.

Two harness decisions, both forced by measurement rather than taste:

  * Renders are pinned to one CPU with `taskset -c 0`. LMMS renders play
    handles on `QThread::idealThreadCount() - 1` worker threads
    (src/core/AudioEngine.cpp:85) and unpinned renders of the *same* untouched
    project differ from each other by ~1 ulp on 1.6% of samples
    (max|delta| 2.4e-07). Pinning removes that run-to-run jitter so the
    comparison measures the change under test and not the scheduler. With
    pinning, repeated renders are bit-identical.

  * The audio identity compared is the `data` chunk, not the whole file. Even
    pinned, the whole-file SHA-256 differs run to run because LMMS writes a
    `PEAK` chunk whose "peak position" field is a float computed off the audio
    threads; it differs by one ulp while every sample is identical. The
    harness therefore checks the `data` chunk sample-for-sample, and the
    whole-file hashes are printed too so the difference is not hidden.
"""

import hashlib
import os
import pathlib
import shutil
import struct
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parents[2]

FIXTURES = [
    "baseline.mmp",
    "prob-all-one.mmp",
    "prob-seed1.mmp",
    "prob-seed2.mmp",
    "prob-seed1-vol.mmp",
    "veljit-seed1.mmp",
    "veljit-seed2.mmp",
]

# (fixture, left source, right source, expected relation, what it proves)
# sources: "pre" = pre-change build, "post" = post-change build, "post-r" = the
# post-change build's repeat render.
CLAIMS = [
    ("baseline.mmp", "pre", "post", "equal",
     "behaviour preservation: a project with no probability values renders sample-identical"),
    ("prob-all-one.mmp", "pre", "post", "equal",
     "an explicit probability of 1 (the documented default) is inert"),
    ("prob-seed1.mmp", "pre", "post", "differ",
     "note probability (prob=0.5) changes what is played, so it is not ignored"),
    ("veljit-seed1.mmp", "pre", "post", "differ",
     "velocity jitter (veljit=0.5) changes what is played, so it is not ignored"),
    ("prob-seed1.mmp", "post-r", "post", "equal",
     "repeatability: the same seed renders sample-identical on a second pass"),
    ("veljit-seed1.mmp", "post-r", "post", "equal",
     "repeatability: the same seed renders sample-identical for velocity jitter"),
    ("prob-seed1.mmp", "post", "prob-seed2.mmp", "differ",
     "a different seed re-rolls the take (probability)"),
    ("veljit-seed1.mmp", "post", "veljit-seed2.mmp", "differ",
     "a different seed re-rolls the take (velocity jitter)"),
    ("prob-seed1.mmp", "post", "prob-seed1-vol.mmp", "differ",
     "sensitivity control: one note moved by a single velocity step is detected"),
]


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def read_wav(path):
    """Minimal RIFF reader. LMMS writes IEEE float (fmt tag 3), which the
    stdlib `wave` module refuses outright."""
    data = path.read_bytes()
    if data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise ValueError(f"{path}: not a RIFF/WAVE file")
    pos = 12
    fmt = None
    payload = None
    while pos + 8 <= len(data):
        cid = data[pos:pos + 4]
        size = struct.unpack_from("<I", data, pos + 4)[0]
        body = data[pos + 8:pos + 8 + size]
        if cid == b"fmt ":
            fmt = struct.unpack_from("<HHIIHH", body, 0)
        elif cid == b"data":
            payload = body[: len(body) - (len(body) % 4)]
        pos += 8 + size + (size % 2)
    if fmt is None or payload is None:
        raise ValueError(f"{path}: missing fmt or data chunk")
    if fmt[0] != 3:
        raise ValueError(f"{path}: expected IEEE float samples, got fmt tag {fmt[0]}")
    return payload


def payload_stats(payload):
    samples = struct.unpack(f"<{len(payload) // 4}f", payload)
    peak = max((abs(s) for s in samples), default=0.0)
    rms = (sum(s * s for s in samples) / len(samples)) ** 0.5 if samples else 0.0
    return samples, peak, rms


def max_abs_delta(a, b):
    if len(a) != len(b):
        return None
    return max((abs(x - y) for x, y in zip(a, b)), default=0.0)


def do_render(build_dir, out_dir):
    lmms = pathlib.Path(build_dir).resolve() / "lmms"
    if not lmms.exists():
        sys.exit(f"no lmms binary at {lmms}")
    out_dir = pathlib.Path(out_dir).resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    pin = shutil.which("taskset")
    if pin:
        print(f"pinning renders to one CPU with {pin}")
    else:
        print("WARNING: no taskset; renders will differ by ~1 ulp run to run")

    rows = []
    for fixture in FIXTURES:
        project = HERE / fixture
        stem = fixture[:-4]
        for suffix in ("", ".repeat"):
            wav = out_dir / f"{stem}{suffix}.wav"
            if wav.exists():
                wav.unlink()
            cmd = []
            if pin:
                cmd += [pin, "-c", "0"]
            cmd += [str(lmms), "render", str(project), "-f", "wav", "-o", str(wav), "-a"]
            proc = subprocess.run(cmd, capture_output=True, text=True, timeout=900,
                                  env={**os.environ, "QT_QPA_PLATFORM": "offscreen"},
                                  cwd=str(REPO))
            if proc.returncode != 0 or not wav.exists():
                sys.exit(f"render failed for {fixture}{suffix}: exit {proc.returncode}\n"
                         f"{proc.stdout[-2000:]}\n{proc.stderr[-2000:]}")
            payload = read_wav(wav)
            samples, peak, rms = payload_stats(payload)
            rows.append({
                "fixture": f"{stem}{suffix}",
                "wav": wav.name,
                "whole_sha256": sha256(wav.read_bytes()),
                "audio_sha256": sha256(payload),
                "peak": peak,
                "rms": rms,
                "frames": len(samples),
            })
            print(f"rendered {wav.name}: audio={rows[-1]['audio_sha256'][:16]} "
                  f"frames={len(samples)} peak={peak:.6f} rms={rms:.6f}")

    (out_dir / "manifest.tsv").write_text(
        "fixture\taudio_sha256\twhole_sha256\tpeak\trms\tframes\n"
        + "".join(f"{r['fixture']}\t{r['audio_sha256']}\t{r['whole_sha256']}\t"
                  f"{r['peak']:.8f}\t{r['rms']:.8f}\t{r['frames']}\n" for r in rows))
    print(f"manifest: {out_dir / 'manifest.tsv'}")


def load_manifest(d):
    path = pathlib.Path(d).resolve() / "manifest.tsv"
    out = {}
    for line in path.read_text().splitlines()[1:]:
        fixture, audio, whole, peak, rms, frames = line.split("\t")
        out[fixture] = {"audio_sha256": audio, "whole_sha256": whole,
                        "peak": float(peak), "rms": float(rms), "frames": int(frames)}
    return out


def resolve(source, stem, pre, post, pre_dir, post_dir):
    """A claim names a source: "pre" (pre-change build), "post" (post-change
    build), "post-r" (post-change build's repeat render), or another fixture
    name (also from the post-change build). Returns (directory, wav stem,
    manifest record) for each."""
    if source == "pre":
        return pre_dir, stem, pre[stem]
    if source == "post":
        return post_dir, stem, post[stem]
    if source == "post-r":
        return post_dir, stem + ".repeat", post[stem + ".repeat"]
    other = source[:-4] if source.endswith(".mmp") else source
    if other not in post:
        sys.exit(f"claim references a render that was not made: {other}")
    return post_dir, other, post[other]


def do_compare(pre_dir, post_dir):
    pre = load_manifest(pre_dir)
    post = load_manifest(post_dir)

    failures = 0
    print(f"{'claim':<58} {'verdict':<7} {'max|delta|':>12}  audio sha256")
    print("-" * 118)
    for fixture, left, right, relation, why in CLAIMS:
        stem = fixture[:-4]
        dir_a, name_a, a = resolve(left, stem, pre, post, pre_dir, post_dir)
        dir_b, name_b, b = resolve(right, stem, pre, post, pre_dir, post_dir)
        same_audio = a["audio_sha256"] == b["audio_sha256"]

        delta = "frames differ"
        if a["frames"] == b["frames"]:
            sa, _, _ = payload_stats(read_wav(pathlib.Path(dir_a) / f"{name_a}.wav"))
            sb, _, _ = payload_stats(read_wav(pathlib.Path(dir_b) / f"{name_b}.wav"))
            d = max_abs_delta(sa, sb)
            delta = "frames differ" if d is None else f"{d:.9f}"
            # every sample bit-identical is the strict form of "equal"
            if relation == "equal" and d is not None:
                same_audio = same_audio and d == 0.0

        ok = same_audio if relation == "equal" else not same_audio
        failures += 0 if ok else 1
        print(f"{fixture + ' [' + left + ' vs ' + right + ']':<58} "
              f"{'PASS' if ok else 'FAIL':<7} {delta:>12}  "
              f"{'same' if same_audio else 'different'}  # {why}")

    print()
    print("whole-file sha256 (differs run to run only in the PEAK metadata float):")
    for name in sorted(post):
        print(f"  {name:<24} {post[name]['whole_sha256'][:24]}  "
              f"(pre: {pre[name]['whole_sha256'][:24] if name in pre else 'n/a'})")

    print()
    silent = [n for n, s in post.items() if s["peak"] <= 0.0]
    pre_silent = [n for n, s in pre.items() if s["peak"] <= 0.0]
    if silent or pre_silent:
        print(f"FAIL: silent renders — post {silent}, pre {pre_silent}")
        failures += 1
    else:
        print(f"liveness: every render is non-silent — post-change peaks "
              f"{min(s['peak'] for s in post.values()):.4f}..{max(s['peak'] for s in post.values()):.4f}, "
              f"pre-change peaks {min(s['peak'] for s in pre.values()):.4f}.."
              f"{max(s['peak'] for s in pre.values()):.4f}")

    print()
    if failures:
        print(f"RESULT: FAIL — {failures} claim(s) failed")
        return 1
    print("RESULT: PASS — every claim held")
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 4 or sys.argv[1] not in ("render", "compare"):
        sys.exit(__doc__)
    if sys.argv[1] == "render":
        do_render(sys.argv[2], sys.argv[3])
    else:
        sys.exit(do_compare(sys.argv[2], sys.argv[3]))
