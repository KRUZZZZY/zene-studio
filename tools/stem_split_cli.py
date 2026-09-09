#!/usr/bin/env python3
# stem_split_cli.py - offline 4-stem separation (reference implementation, G1)
#
# Copyright (c) 2026 LMMS Developers
#
# This file is part of LMMS - https://lmms.io
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public
# License as published by the Free Software Foundation; either
# version 2 of the License, or (at your option) any later version.
#
# Runs an ONNX 4-stem model (HTDemucs layout) over a whole file in overlapping
# segments and writes one file per stem. This is the model contract that the
# C++ backends (src/core/ExternalProcessStemSeparator.cpp and the optional
# src/core/OnnxRuntimeStemSeparator.cpp) implement as well.
#
# Model contract (fixed):
#   input   float32 [1, 2, T]      (T = native segment or --segment)
#   outputs float32 [1, 2, T] x 4  named drums, bass, other, vocals
#
# I/O defaults to headerless interleaved float32 (f32) so the C++ side needs no
# audio decoder; --in-format wav / --out-format wav add PCM16 + float32 RIFF.
#
# Protocol: one JSON object per line on stdout. Progress is
# {"type":"progress","chunk":i,"total":n,"fraction":f}; the parent may create
# --cancel-file at any time, which stops the run between segments and still
# writes the partial stems.
#
# Exit codes: 0 success, 2 error, 3 cancelled.
#
# REAL-TIME IS FORBIDDEN for this model (7.8 s lookahead); this tool is always
# run as an offline/background job by StemJobManager.

import argparse
import json
import os
import struct
import sys
import time

STEM_NAMES = ("drums", "bass", "other", "vocals")


def emit(obj):
    print(json.dumps(obj), flush=True)


def fail(message, code=2):
    emit({"type": "error", "error": str(message)})
    sys.exit(code)


# ---------------------------------------------------------------- audio I/O

def read_f32(path):
    import numpy as np
    data = np.fromfile(path, dtype="<f4")
    if data.size == 0 or data.size % 2 != 0:
        raise ValueError(f"{path}: not interleaved stereo float32 "
                         f"({data.size} floats)")
    return np.ascontiguousarray(data.reshape(-1, 2).T, dtype=np.float32)


def write_f32(path, data):
    data.T.astype("<f4").tofile(path)


def read_wav(path):
    """Minimal RIFF reader: PCM16 / PCM24 / float32."""
    import numpy as np
    with open(path, "rb") as fh:
        header = fh.read(12)
        if len(header) != 12 or header[0:4] != b"RIFF" or header[8:12] != b"WAVE":
            raise ValueError(f"{path}: not a RIFF/WAVE file")
        fmt = None
        payload = None
        while True:
            chunk_header = fh.read(8)
            if len(chunk_header) < 8:
                break
            chunk_id, chunk_size = struct.unpack("<4sI", chunk_header)
            chunk_data = fh.read(chunk_size + (chunk_size & 1))
            if chunk_id == b"fmt ":
                fmt = chunk_data
            elif chunk_id == b"data":
                payload = chunk_data[:chunk_size]
        if fmt is None or payload is None:
            raise ValueError(f"{path}: missing fmt/data chunk")
        audio_format, channels, rate, _, _, bits = struct.unpack("<HHIIHH", fmt[:16])
        if audio_format == 0xFFFE and len(fmt) >= 26:  # WAVE_FORMAT_EXTENSIBLE
            audio_format = struct.unpack("<H", fmt[24:26])[0]
        if channels != 2:
            raise ValueError(f"{path}: expected 2 channels, got {channels}")
        if audio_format == 3:      # IEEE float
            samples = np.frombuffer(payload, dtype="<f4")
        elif audio_format == 1 and bits == 16:
            samples = np.frombuffer(payload, dtype="<i2").astype(np.float32) / 32768.0
        elif audio_format == 1 and bits == 24:
            raw = np.frombuffer(payload, dtype=np.uint8).reshape(-1, 3)
            as32 = (raw[:, 0].astype(np.int32)
                    | (raw[:, 1].astype(np.int32) << 8)
                    | (raw[:, 2].astype(np.int32) << 16))
            as32 = np.where(as32 >= 0x800000, as32 - 0x1000000, as32)
            samples = as32.astype(np.float32) / 8388608.0
        else:
            raise ValueError(f"{path}: unsupported WAV format {audio_format}/{bits} bit")
        if samples.size % 2 != 0:
            raise ValueError(f"{path}: odd sample count")
        return np.ascontiguousarray(samples.reshape(-1, 2).T.astype(np.float32)), rate


def write_wav(path, data, rate):
    """float32 RIFF, stereo."""
    interleaved = data.T.astype("<f4").tobytes()
    fmt = struct.pack("<HHIIHH", 3, 2, rate, rate * 8, 8, 32)
    chunks = b"fmt " + struct.pack("<I", len(fmt)) + fmt
    chunks += b"data" + struct.pack("<I", len(interleaved)) + interleaved
    with open(path, "wb") as fh:
        fh.write(b"RIFF" + struct.pack("<I", 4 + len(chunks)) + b"WAVE" + chunks)


def read_audio(path, fmt, rate):
    if fmt == "f32":
        if rate is None:
            raise ValueError("--in-rate is required for --in-format f32")
        return read_f32(path), rate
    data, wav_rate = read_wav(path)
    return data, wav_rate


def write_audio(path, data, fmt, rate):
    if fmt == "f32":
        write_f32(path, data)
    else:
        write_wav(path, data, rate)


# ------------------------------------------------------------------- model

def load_session(model_path):
    import onnxruntime as ort
    providers = ort.get_available_providers()
    session = ort.InferenceSession(model_path, providers=["CPUExecutionProvider"]
                                   if "CPUExecutionProvider" in providers else providers)
    inputs = session.get_inputs()
    outputs = session.get_outputs()
    if len(inputs) != 1:
        raise ValueError(f"model has {len(inputs)} inputs, expected 1")
    if len(outputs) < len(STEM_NAMES):
        raise ValueError(f"model has {len(outputs)} outputs, expected >= 4")
    shape = inputs[0].shape
    if len(shape) != 3 or shape[0] not in (1, "1") or shape[1] not in (2, "2"):
        raise ValueError(f"model input must be [1, 2, T], got {shape}")
    static_segment = shape[2] if isinstance(shape[2], int) and shape[2] > 0 else None
    return session, static_segment


def resolve_output_indices(session):
    names = [o.name for o in session.get_outputs()]
    indices = []
    for position, stem in enumerate(STEM_NAMES):
        if stem in names:
            indices.append(names.index(stem))
        else:
            indices.append(position)
    return indices


# --------------------------------------------------------------- separation

def separate(session, mix, segment, progress_cb, cancel_cb, chunk_delay_ms=0.0):
    """Overlap-add separation. Returns (stems [4,2,n] or partial, chunks_done,
    cancelled)."""
    import numpy as np

    frames = mix.shape[1]
    hop = segment // 2
    pad_left = segment // 2
    padded = frames + pad_left + segment
    chunk_count = (padded + hop - 1) // hop

    window = (0.5 - 0.5 * np.cos(2.0 * np.pi * (np.arange(segment) + 0.5) / segment)
              ).astype(np.float32)

    acc = np.zeros((len(STEM_NAMES), 2, frames), dtype=np.float64)
    window_sum = np.zeros(frames, dtype=np.float64)

    input_name = session.get_inputs()[0].name
    out_indices = resolve_output_indices(session)

    chunks_done = 0
    cancelled = False
    for chunk in range(chunk_count):
        if cancel_cb():
            cancelled = True
            break
        start = chunk * hop
        src0 = start - pad_left
        src1 = src0 + segment
        lo = max(0, src0)
        hi = min(frames, src1)
        block = np.zeros((1, 2, segment), dtype=np.float32)
        if hi > lo:
            block[0, :, lo - src0:hi - src0] = mix[:, lo:hi]
        outputs = session.run(None, {input_name: block})
        if hi > lo:
            a = lo - src0
            b = a + (hi - lo)
            dst = slice(lo, hi)
            for s, out_index in enumerate(out_indices):
                acc[s, :, dst] += outputs[out_index][0][:, a:b].astype(np.float64) * window[a:b]
            window_sum[dst] += window[a:b]
        chunks_done = chunk + 1
        progress_cb(chunks_done / chunk_count)
        if chunk_delay_ms > 0:
            time.sleep(chunk_delay_ms / 1000.0)

    denom = np.maximum(window_sum, 1e-8)
    stems = (acc / denom).astype(np.float32)
    return stems, chunks_done, cancelled


# --------------------------------------------------------------------- main

def main():
    parser = argparse.ArgumentParser(description="Offline 4-stem separation")
    parser.add_argument("--model", required=True)
    parser.add_argument("--input", required=True)
    parser.add_argument("--in-format", choices=("f32", "wav"), default="f32")
    parser.add_argument("--in-rate", type=int, default=None)
    parser.add_argument("--out-dir", required=True)
    parser.add_argument("--out-format", choices=("f32", "wav"), default="f32")
    parser.add_argument("--segment", type=int, default=None,
                        help="model segment length in frames (required for dynamic-T models)")
    parser.add_argument("--progress-json", action="store_true")
    parser.add_argument("--cancel-file", default=None)
    parser.add_argument("--chunk-delay-ms", type=float, default=0.0,
                        help="test hook: slow down per-segment processing")
    args = parser.parse_args()

    try:
        import numpy  # noqa: F401
        import onnxruntime as ort
    except ImportError as exc:
        fail(f"missing Python module: {exc}")

    try:
        mix, rate = read_audio(args.input, args.in_format, args.in_rate)
    except Exception as exc:  # noqa: BLE001
        fail(f"cannot read input: {exc}")

    try:
        session, static_segment = load_session(args.model)
    except Exception as exc:  # noqa: BLE001
        fail(f"cannot load model: {exc}")

    segment = args.segment or static_segment
    if not segment or segment <= 0:
        fail("model input T is dynamic; pass --segment")

    os.makedirs(args.out_dir, exist_ok=True)
    emit({
        "type": "info",
        "onnxruntime": ort.__version__,
        "providers": ort.get_available_providers(),
        "model": os.path.abspath(args.model),
        "segment": int(segment),
        "sample_rate": int(rate),
        "frames": int(mix.shape[1]),
        "chunks": int((mix.shape[1] + segment // 2 + segment + segment // 2 - 1)
                      // (segment // 2)),
    })

    def cancel_cb():
        return bool(args.cancel_file) and os.path.exists(args.cancel_file)

    started = time.perf_counter()
    try:
        stems, chunks_done, cancelled = separate(
            session, mix, int(segment),
            lambda fraction: emit({"type": "progress",
                                   "fraction": round(float(fraction), 6)}),
            cancel_cb, args.chunk_delay_ms)
    except Exception as exc:  # noqa: BLE001
        fail(f"inference failed: {exc}")

    elapsed = time.perf_counter() - started
    outputs = {}
    for index, stem in enumerate(STEM_NAMES):
        path = os.path.join(args.out_dir, f"{stem}.{args.out_format}")
        write_audio(path, stems[index], args.out_format, rate)
        outputs[stem] = os.path.abspath(path)

    duration = mix.shape[1] / float(rate) if rate else 0.0
    payload = {
        "type": "cancelled" if cancelled else "done",
        "stems": outputs,
        "chunks": chunks_done,
        "elapsed_s": round(elapsed, 6),
        "audio_s": round(duration, 6),
        "rtf": round(elapsed / duration, 6) if duration > 0 else None,
    }
    emit(payload)
    sys.exit(3 if cancelled else 0)


if __name__ == "__main__":
    main()
