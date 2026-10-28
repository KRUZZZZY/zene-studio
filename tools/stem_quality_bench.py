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
#             excerpt is a fixed 30 s window; the mix is written as the sum
#             of its four reference stems (float64, stored as float32), so mix
#             and references are sample-aligned by construction. Every written file's sha256 is
#             checked against the pins below (a mismatch is an error).
#   run       separate every excerpt at every quality with the CLI the GUI
#             drives, time it, and score the stems against the references.
#
# tests/stem-quality-bench.py (ctest StemQualityBench) checks `run`
# hermetically: a synthetic reference set and the committed stub model, with
# SDR values known in closed form. It never touches the network.
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
import tempfile
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
# were recorded on the first fetch (2026-10-27) and every later fetch must
# reproduce them - an unpinned or mismatching file is an error.
REFERENCE_TRACKS = (
    "Al James - Schoolboy Facination",
    "Angels In Amplifiers - I'm Alright",
    "Hollow Ground - Ill Fate",
    "Tom McKenzie - Directions",
)
REFERENCE_SHA256 = {
    "Al_James___Schoolboy_Facination/bass.f32":
        "d111384f980898878eadac69cbcd97ba8659f1666ee00ebe9b171ff0d169cd3f",
    "Al_James___Schoolboy_Facination/drums.f32":
        "27055c5cf02b8f33743e2958914043d3411b00c75cd45cb4e99cf2afd620423f",
    "Al_James___Schoolboy_Facination/mix.f32":
        "f7daa07a0f951c435b433779aeac48c72f2c1cdb343280d27e8feed85532f300",
    "Al_James___Schoolboy_Facination/other.f32":
        "4829d632591374c7d18d1581f2bf20cc0158a53f4c69313bcc0624338e1715d0",
    "Al_James___Schoolboy_Facination/vocals.f32":
        "8ea61ee4e71cc030a09394dc6efb2266979b1f0715ee76a59bcd9b62193bd294",
    "Angels_In_Amplifiers___I_m_Alright/bass.f32":
        "aca985db4ccb469d00b6399aba6613f470f72abc346f61618abe76ff1f476abe",
    "Angels_In_Amplifiers___I_m_Alright/drums.f32":
        "cd357be9d1ea91180af1a53c737491ddf7e13104771c16b8c32b223f14617c3c",
    "Angels_In_Amplifiers___I_m_Alright/mix.f32":
        "cd454ba032210a66dc0ede867eaea0db1cf802acfc13ff39ace58fb2b4c16b6c",
    "Angels_In_Amplifiers___I_m_Alright/other.f32":
        "b12027c875e1d06cf3d9a7b9c3d5492010284b4e25dc793834c4e53c859f5c43",
    "Angels_In_Amplifiers___I_m_Alright/vocals.f32":
        "c1b60f43481cfb5b817c0b66f6f49eb796625b50946407056ed7919068732813",
    "Hollow_Ground___Ill_Fate/bass.f32":
        "473fb7cd3adfccc250276240a4b6a4659edcc04d9d7c0dae9a53d31fd0c33d25",
    "Hollow_Ground___Ill_Fate/drums.f32":
        "d58d6dfdd573a61289a2df44443ddb0bc047a9dc1d402ec47457de7652cb3f18",
    "Hollow_Ground___Ill_Fate/mix.f32":
        "976211e259c93374259256a39527fd1cfcf9e2a63cf8929b1f9e85af9dcc96d0",
    "Hollow_Ground___Ill_Fate/other.f32":
        "17a17edd98d92927d361d0b05bbe2ab5527861cd68e59645afac4821a76eb523",
    "Hollow_Ground___Ill_Fate/vocals.f32":
        "d55f57d6ffacb67518440412c9de712bf418ac6f6ec2d8fcd405837689cdd7ca",
    "Tom_McKenzie___Directions/bass.f32":
        "3fc3e0632a79ccbcbe51609cc3fcdc17167de3ba15f2084b3686741006d68b28",
    "Tom_McKenzie___Directions/drums.f32":
        "a830e4c8b26cb937d6e4b3edc303548ebb84a4dc5ede03e383a184ef2b44c6db",
    "Tom_McKenzie___Directions/mix.f32":
        "47ddb2d711ae2ddb4922e1bd66f9fa2f5e158313e197b5e6bd63fcd529a2c9c2",
    "Tom_McKenzie___Directions/other.f32":
        "572cb44e652d529f4814d1a9cf799bc1a9f533b2fa05949d3abfdaf81c4ed590",
    "Tom_McKenzie___Directions/vocals.f32":
        "1d6efb1d19d92d51af3e4f4221c419180d7be663f43a80add9f0050641c67597",
}


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
    return os.path.join(tempfile.gettempdir(), f"stem-quality-bench-{os.getpid()}.wav")


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
        if pinned != hashes[key]:
            raise ValueError(f"{key}: sha256 {hashes[key]} != pinned {pinned}")
    return hashes


def command_fetch(args):
    archive = zipfile.ZipFile(io.BufferedReader(HttpRangeFile(args.url, MUSDB_ZIP_BYTES),
                                                buffer_size=4 << 20))
    manifest = {"source": args.url, "zip_bytes": MUSDB_ZIP_BYTES,
                "excerpt_start_s": EXCERPT_START_S, "excerpt_s": EXCERPT_SECONDS,
                "mix": "sum of the four reference stems (float64, stored as float32)",
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
    getloadavg = getattr(os, "getloadavg", None)
    if getloadavg is None:
        return None
    try:
        return [round(value, 2) for value in getloadavg()]
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
        "affinity_cpus": len(os.sched_getaffinity(0)) if hasattr(os, "sched_getaffinity") else None,
        "cgroup_cpu_max": _cgroup_cpu_max(),
    }


def _cgroup_cpu_max():
    """The cgroup v2 CPU quota of this process ("quota period", or "max"), when
    there is one: a quota throttles the run however many CPUs are visible."""
    try:
        with open("/proc/self/cgroup") as fh:
            path = fh.read().strip().split("::", 1)[1]
        with open(f"/sys/fs/cgroup{path}/cpu.max") as fh:
            return fh.read().strip()
    except (OSError, IndexError):
        return None


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


def _mean(values):
    values = list(values)
    return round(sum(values) / len(values), 4)


def _quality_summary(mine):
    per_stem = {stem: _mean(r["sdr"][stem] for r in mine) for stem in STEM_NAMES}
    return {
        "runs": len(mine),
        "sdr_mean": _mean(per_stem.values()),
        "sdr_per_stem": per_stem,
        "elapsed_s_total": round(sum(r["elapsed_s"] for r in mine), 3),
        "process_wall_s_total": round(sum(r["process_wall_s"] for r in mine), 3),
        "audio_s_total": round(sum(r["audio_s"] for r in mine), 3),
        "chunks_total": sum(r["chunks"] for r in mine),
    }


def summarise(rows):
    """Per quality: mean SDR per stem and overall, total elapsed and wall."""
    by_quality = {quality: [row for row in rows if row["quality"] == quality] for quality in QUALITIES}
    return {quality: _quality_summary(mine) for quality, mine in by_quality.items() if mine}


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
    run.add_argument("--cli", default=os.path.join(here, "stem_split_cli.py"))
    args = parser.parse_args()
    return {"fetch": command_fetch, "run": command_run}[args.command](args)


if __name__ == "__main__":
    sys.exit(main())
