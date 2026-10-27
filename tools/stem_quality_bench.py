#!/usr/bin/env python3
# stem_quality_bench.py - real-model SDR and time per stem-separation quality
#
# Copyright (c) 2026 Zene Studio contributors
#
# This file is part of LMMS - https://lmms.io
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public
# License as published by the Free Software Foundation; either
# version 2 of the License, or (at your option) any later version.
#
# Q050-062.3: measures every quality setting of tools/stem_split_cli.py
# (fast / standard / high, the segment overlap of Q050-062.1) on a fixed
# reference set whose source stems are known, and writes one JSON record with
# the per-setting SDR, the elapsed time, and the identity of the model, the
# data and the machine. doc/STEM-SPLIT.md "Quality per setting" holds the
# measured figures and the exact commands.
#
#   fetch     pull the pinned reference set: four MUSDB18-HQ *test* tracks
#             (Zenodo record 3338373), read out of the 22 GB zip with HTTP
#             range requests so only those tracks' stems are downloaded. Each
#             excerpt is a fixed 30 s window; the mix is written as the exact
#             sum of its four reference stems, so mix and references are
#             sample-aligned by construction. Every written file's sha256 is
#             checked against the pins below (a mismatch is an error).
#   run       separate every excerpt at every quality with the CLI the GUI
#             drives, time it, and score the stems against the references.
#   selftest  the hermetic check registered in ctest (StemQualityBench): a
#             synthetic reference set and the committed stub model, with SDR
#             values known in closed form. It never touches the network.
#
# SDR is the global signal-to-distortion ratio of the Music Demixing
# challenges (the "nSDR" Demucs reports): per stem and excerpt,
#   10 * log10((sum ref^2 + eps) / (sum (ref - est)^2 + eps)),  eps = 1e-7,
# over both channels of the whole excerpt. Higher is better. It is NOT
# BSSEval v4 (museval), whose figures are not comparable with these.
#
# The reference audio is MUSDB18-HQ (CC BY-NC-SA-style terms, research use);
# it is downloaded to a scratch directory and never committed.

import argparse
import hashlib
import io
import json
import os
import platform
import subprocess
import sys
import time
import urllib.request
import zipfile

STEM_NAMES = ("drums", "bass", "other", "vocals")
QUALITIES = ("fast", "standard", "high")
SAMPLE_RATE = 44100
SDR_EPS = 1e-7

MUSDB_URL = "https://zenodo.org/records/3338373/files/musdb18hq.zip?download=1"
MUSDB_ZIP_BYTES = 22656664047
EXCERPT_START_S = 60
EXCERPT_SECONDS = 30

# The fixed reference set: test-split tracks (HTDemucs was not trained on
# them), each read from EXCERPT_START_S for EXCERPT_SECONDS. The sha256 pins
# are of the excerpt files `fetch` writes (interleaved float32 stereo); they
# were recorded on the first fetch and every later fetch must reproduce them.
REFERENCE_TRACKS = (
    "Al James - Schoolboy Facination",
    "Angels In Amplifiers - I'm Alright",
    "Hollow Ground - Ill Fate",
    "Tom McKenzie - Directions",
)
REFERENCE_SHA256 = {}


def emit(obj):
    print(json.dumps(obj), flush=True)


def sha256_of(path):
    digest = hashlib.sha256()
    with open(path, "rb") as fh:
        for block in iter(lambda: fh.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


# ------------------------------------------------------------------ scoring

def sdr(reference, estimate):
    """Global SDR in dB of one stem; both arrays [channels, frames]."""
    import numpy as np
    ref = reference.astype(np.float64)
    err = ref - estimate.astype(np.float64)
    return float(10.0 * np.log10((np.sum(ref * ref) + SDR_EPS)
                                 / (np.sum(err * err) + SDR_EPS)))


def read_f32(path):
    import numpy as np
    data = np.fromfile(path, dtype="<f4")
    return np.ascontiguousarray(data.reshape(-1, 2).T)


def write_f32(path, data):
    data.T.astype("<f4").tofile(path)


def score_excerpt(ref_dir, est_dir):
    """{stem: SDR} for one separated excerpt."""
    scores = {}
    for stem in STEM_NAMES:
        scores[stem] = round(sdr(read_f32(os.path.join(ref_dir, stem + ".f32")),
                                 read_f32(os.path.join(est_dir, stem + ".f32"))), 4)
    return scores


# ------------------------------------------------------- reference fetching

class HttpRangeFile(io.RawIOBase):
    """A read-only seekable view of a remote file, one Range request per read."""

    def __init__(self, url, size):
        super().__init__()
        self.url = url
        self.size = size
        self.pos = 0

    def readable(self):
        return True

    def seekable(self):
        return True

    def tell(self):
        return self.pos

    def seek(self, offset, whence=io.SEEK_SET):
        base = {io.SEEK_SET: 0, io.SEEK_CUR: self.pos, io.SEEK_END: self.size}[whence]
        self.pos = base + offset
        return self.pos

    def readinto(self, buffer):
        if self.pos >= self.size:
            return 0
        last = min(self.size, self.pos + len(buffer)) - 1
        request = urllib.request.Request(self.url, headers={"Range": f"bytes={self.pos}-{last}"})
        for attempt in range(5):
            try:
                with urllib.request.urlopen(request, timeout=120) as response:
                    data = response.read()
                break
            except OSError:
                if attempt == 4:
                    raise
                time.sleep(2.0 * (attempt + 1))
        buffer[:len(data)] = data
        self.pos += len(data)
        return len(data)


def read_wav_member(archive, name):
    """[2, frames] float32 of one PCM16 stereo WAV inside the zip (CRC-checked
    by zipfile as it is read)."""
    import numpy as np
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from stem_split_cli import read_wav
    with archive.open(name) as member, open(_scratch_wav(), "wb") as out:
        for block in iter(lambda: member.read(1 << 20), b""):
            out.write(block)
    data, rate = read_wav(_scratch_wav())
    os.remove(_scratch_wav())
    if rate != SAMPLE_RATE:
        raise ValueError(f"{name}: {rate} Hz, expected {SAMPLE_RATE}")
    return np.ascontiguousarray(data)


def _scratch_wav():
    return os.path.join(os.environ.get("TMPDIR", "/tmp"), f"stem-quality-bench-{os.getpid()}.wav")


def track_slug(track):
    return "".join(c if c.isalnum() else "_" for c in track).strip("_")


def write_excerpt(archive, track, out_dir):
    """Write <out_dir>/<slug>/{drums,bass,other,vocals,mix}.f32 for one track."""
    import numpy as np
    first = EXCERPT_START_S * SAMPLE_RATE
    last = first + EXCERPT_SECONDS * SAMPLE_RATE
    track_dir = os.path.join(out_dir, track_slug(track))
    os.makedirs(track_dir, exist_ok=True)
    mix = None
    for stem in STEM_NAMES:
        excerpt = read_wav_member(archive, f"test/{track}/{stem}.wav")[:, first:last]
        if excerpt.shape[1] != last - first:
            raise ValueError(f"{track}/{stem}: shorter than the excerpt window")
        write_f32(os.path.join(track_dir, stem + ".f32"), excerpt)
        mix = excerpt.astype(np.float64) if mix is None else mix + excerpt
    write_f32(os.path.join(track_dir, "mix.f32"), mix.astype(np.float32))
    return track_dir


def check_pins(track_dir, track):
    """{file: sha256}; raises when a pinned hash does not match."""
    hashes = {}
    for name in STEM_NAMES + ("mix",):
        key = f"{track_slug(track)}/{name}.f32"
        hashes[key] = sha256_of(os.path.join(track_dir, name + ".f32"))
        pinned = REFERENCE_SHA256.get(key)
        if pinned is not None and pinned != hashes[key]:
            raise ValueError(f"{key}: sha256 {hashes[key]} != pinned {pinned}")
    return hashes


def command_fetch(args):
    archive = zipfile.ZipFile(io.BufferedReader(HttpRangeFile(args.url, MUSDB_ZIP_BYTES),
                                                buffer_size=4 << 20))
    manifest = {"source": args.url, "zip_bytes": MUSDB_ZIP_BYTES,
                "excerpt_start_s": EXCERPT_START_S, "excerpt_s": EXCERPT_SECONDS,
                "mix": "exact float sum of the four reference stems",
                "tracks": list(REFERENCE_TRACKS), "sha256": {}}
    for track in REFERENCE_TRACKS:
        track_dir = write_excerpt(archive, track, args.out)
        manifest["sha256"].update(check_pins(track_dir, track))
        emit({"type": "fetched", "track": track, "dir": track_dir})
    with open(os.path.join(args.out, "manifest.json"), "w") as fh:
        json.dump(manifest, fh, indent=1, sort_keys=True)
    emit({"type": "done", "manifest": os.path.join(args.out, "manifest.json")})
    return 0


# ------------------------------------------------------------------ running

def run_cli(cli, model, mix_path, out_dir, quality, segment):
    """Run one separation; returns (done-line dict, process wall seconds)."""
    command = [sys.executable, cli, "--model", model, "--input", mix_path,
               "--in-format", "f32", "--in-rate", str(SAMPLE_RATE),
               "--out-dir", out_dir, "--out-format", "f32", "--quality", quality,
               "--progress-json"]
    if segment:
        command += ["--segment", str(segment)]
    started = time.perf_counter()
    result = subprocess.run(command, capture_output=True, text=True, check=False)
    wall = time.perf_counter() - started
    lines = [json.loads(line) for line in result.stdout.splitlines() if line.startswith("{")]
    done = [line for line in lines if line.get("type") == "done"]
    if result.returncode != 0 or not done:
        raise RuntimeError(f"CLI exit {result.returncode}: {result.stdout[-400:]} {result.stderr[-400:]}")
    info = [line for line in lines if line.get("type") == "info"][0]
    return dict(done[0], hop=info["hop"], segment=info["segment"]), wall


def load_average():
    try:
        return [round(value, 2) for value in os.getloadavg()]
    except OSError:
        return None


def run_conditions(cli, model):
    import numpy
    import onnxruntime
    return {
        "python": platform.python_version(),
        "numpy": numpy.__version__,
        "onnxruntime": onnxruntime.__version__,
        "provider": "CPUExecutionProvider",
        "machine": platform.machine(),
        "processor": _cpu_model(),
        "logical_cpus": os.cpu_count(),
        "kernel": platform.release(),
        "cli": os.path.relpath(cli),
        "cli_sha256": sha256_of(cli),
        "model": os.path.basename(model),
        "model_sha256": sha256_of(model),
        "model_bytes": os.path.getsize(model),
        "omp_num_threads": os.environ.get("OMP_NUM_THREADS"),
    }


def _cpu_model():
    try:
        with open("/proc/cpuinfo") as fh:
            for line in fh:
                if line.startswith("model name"):
                    return line.split(":", 1)[1].strip()
    except OSError:
        pass
    return platform.processor()


def reference_dirs(refset):
    return sorted(os.path.join(refset, name) for name in os.listdir(refset)
                  if os.path.isfile(os.path.join(refset, name, "mix.f32")))


def measure_one(args, track_dir, quality, repeat):
    out_dir = os.path.join(args.work, os.path.basename(track_dir), f"{quality}-{repeat}")
    load_before = load_average()
    done, wall = run_cli(args.cli, args.model, os.path.join(track_dir, "mix.f32"),
                         out_dir, quality, args.segment)
    scores = score_excerpt(track_dir, out_dir)
    row = {"excerpt": os.path.basename(track_dir), "quality": quality, "repeat": repeat,
           "hop": done["hop"], "chunks": done["chunks"], "elapsed_s": done["elapsed_s"],
           "process_wall_s": round(wall, 3), "audio_s": done["audio_s"], "rtf": done["rtf"],
           "sdr": scores, "sdr_mean": round(sum(scores.values()) / len(scores), 4),
           "loadavg_before": load_before, "loadavg_after": load_average()}
    emit(dict(row, type="measured"))
    return row


def summarise(rows):
    """Per quality: mean SDR per stem and overall, total elapsed and wall."""
    summary = {}
    for quality in QUALITIES:
        mine = [row for row in rows if row["quality"] == quality]
        if not mine:
            continue
        per_stem = {stem: round(sum(r["sdr"][stem] for r in mine) / len(mine), 4)
                    for stem in STEM_NAMES}
        summary[quality] = {
            "runs": len(mine),
            "sdr_mean": round(sum(per_stem.values()) / len(per_stem), 4),
            "sdr_per_stem": per_stem,
            "elapsed_s_total": round(sum(r["elapsed_s"] for r in mine), 3),
            "process_wall_s_total": round(sum(r["process_wall_s"] for r in mine), 3),
            "audio_s_total": round(sum(r["audio_s"] for r in mine), 3),
            "chunks_total": sum(r["chunks"] for r in mine),
        }
    return summary


def command_run(args):
    excerpts = reference_dirs(args.refset)
    if not excerpts:
        emit({"type": "error", "error": f"no excerpts under {args.refset}"})
        return 2
    record = {"started": time.strftime("%Y-%m-%dT%H:%M:%S%z"),
              "conditions": run_conditions(args.cli, args.model),
              "data": {os.path.basename(d): {n: sha256_of(os.path.join(d, n + ".f32"))
                                             for n in STEM_NAMES + ("mix",)} for d in excerpts},
              "sdr_definition": "10*log10((sum ref^2 + 1e-7) / (sum (ref-est)^2 + 1e-7)), "
                                "per stem over the whole excerpt, both channels",
              "rows": []}
    # Interleave the settings per excerpt so slow drift in machine load hits
    # every setting alike instead of one whole block.
    for repeat in range(1, args.repeats + 1):
        for track_dir in excerpts:
            for quality in args.qualities:
                record["rows"].append(measure_one(args, track_dir, quality, repeat))
    record["summary"] = summarise(record["rows"])
    record["finished"] = time.strftime("%Y-%m-%dT%H:%M:%S%z")
    with open(args.out, "w") as fh:
        json.dump(record, fh, indent=1, sort_keys=True)
    emit({"type": "done", "summary": record["summary"], "out": args.out})
    return 0


# ----------------------------------------------------------------- selftest

def make_synthetic_refset(refset):
    """One excerpt whose references are fixed multiples of the mix, chosen so
    the stub model's estimates (0.4/0.3/0.2/0.1 x mix, tests/data/
    stub-4stem-linear.onnx) score known SDRs: ref = g x mix, est = s x mix
    gives SDR = 20 log10(g / |g - s|) for any mix."""
    import numpy as np
    gains = {"drums": 0.5, "bass": 0.3, "other": 0.1, "vocals": 0.1}
    stub = {"drums": 0.4, "bass": 0.3, "other": 0.2, "vocals": 0.1}
    track_dir = os.path.join(refset, "synthetic")
    os.makedirs(track_dir, exist_ok=True)
    rng = np.random.default_rng(20261027)
    mix = (0.3 * rng.standard_normal((2, 3 * SAMPLE_RATE))).astype(np.float32)
    write_f32(os.path.join(track_dir, "mix.f32"), mix)
    for stem, gain in gains.items():
        write_f32(os.path.join(track_dir, stem + ".f32"), (gain * mix).astype(np.float32))
    expected = {}
    for stem in STEM_NAMES:
        miss = abs(gains[stem] - stub[stem])
        expected[stem] = None if miss == 0 else 20.0 * np.log10(gains[stem] / miss)
    return expected


def selftest_failures(record, expected):
    failures = []
    rows = record["rows"]
    if sorted({row["quality"] for row in rows}) != sorted(QUALITIES):
        failures.append(f"qualities measured {[r['quality'] for r in rows]}")
    chunks = {row["quality"]: row["chunks"] for row in rows}
    if not chunks.get("fast", 0) < chunks.get("standard", 0) < chunks.get("high", 0):
        failures.append(f"chunk counts do not rise with quality: {chunks}")
    for row in rows:
        for stem, want in expected.items():
            got = row["sdr"][stem]
            if want is None and got < 60.0:
                failures.append(f"{row['quality']}/{stem}: exact estimate scored {got} dB, want >= 60")
            if want is not None and abs(got - want) > 0.01:
                failures.append(f"{row['quality']}/{stem}: SDR {got} dB, want {want:.4f}")
        if not row["elapsed_s"] > 0 or row["hop"] < 1:
            failures.append(f"{row['quality']}: elapsed {row['elapsed_s']} hop {row['hop']}")
    conditions = record["conditions"]
    for key in ("model_sha256", "cli_sha256", "onnxruntime", "python"):
        if not conditions.get(key):
            failures.append(f"run conditions lack {key}")
    if set(record["summary"]) != set(QUALITIES):
        failures.append(f"summary covers {sorted(record['summary'])}")
    return failures


def command_selftest(args):
    import tempfile
    try:
        import numpy  # noqa: F401
        import onnxruntime  # noqa: F401
    except ImportError as exc:
        print(f"SKIP: {exc}")
        return 77
    with tempfile.TemporaryDirectory() as scratch:
        expected = make_synthetic_refset(os.path.join(scratch, "refset"))
        out = os.path.join(scratch, "result.json")
        run_args = argparse.Namespace(refset=os.path.join(scratch, "refset"), cli=args.cli,
                                      model=args.model, work=os.path.join(scratch, "work"),
                                      out=out, qualities=list(QUALITIES), repeats=1,
                                      segment=16384)
        if command_run(run_args) != 0:
            print("FAIL: run did not complete")
            return 1
        with open(out) as fh:
            failures = selftest_failures(json.load(fh), expected)
    for failure in failures:
        print("FAIL:", failure)
    print("PASS" if not failures else f"{len(failures)} failure(s)")
    return 0 if not failures else 1


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    parser = argparse.ArgumentParser(description="Stem-separation SDR and time per quality")
    sub = parser.add_subparsers(dest="command", required=True)
    fetch = sub.add_parser("fetch", help="download the pinned reference excerpts")
    fetch.add_argument("--out", required=True)
    fetch.add_argument("--url", default=MUSDB_URL)
    run = sub.add_parser("run", help="measure every quality on a reference set")
    run.add_argument("--refset", required=True)
    run.add_argument("--model", required=True)
    run.add_argument("--work", required=True, help="scratch directory for separated stems")
    run.add_argument("--out", required=True, help="JSON record to write")
    run.add_argument("--qualities", nargs="+", choices=QUALITIES, default=list(QUALITIES))
    run.add_argument("--repeats", type=int, default=1)
    run.add_argument("--segment", type=int, default=None)
    selftest = sub.add_parser("selftest", help="hermetic check with the stub model")
    selftest.add_argument("--model", required=True)
    for each in (run, selftest):
        each.add_argument("--cli", default=os.path.join(here, "stem_split_cli.py"))
    args = parser.parse_args()
    return {"fetch": command_fetch, "run": command_run, "selftest": command_selftest}[args.command](args)


if __name__ == "__main__":
    sys.exit(main())
