IMPLEMENTED

# Cluster Q050-131 — Benchmark real-model stem separation speed

Docs-only unit. `doc/STEM-SPLIT.md` gained "End-to-end §4. Real-model benchmark
(Q050-131, 2026-10-17)" (doc/STEM-SPLIT.md:149): the pinned real model
(commit `d54ed9eb60e258ea82131c6ee14578628816456a` of StemSplitio/htdemucs-onnx,
sha256 verified before the run) on a deterministic 30.0 s stereo 44.1 kHz reference
mix (sha256 recorded, generator command printed), driven by the exact CLI the
GUI's external-process backend spawns. Three runs recorded wall time and
real-time factor; hardware, model revision, clip duration and exact commands all
recorded in the file. Stub-model figures stay in sections 1–3, explicitly labelled
as stub-model only (doc/STEM-SPLIT.md:146, doc/STEM-SPLIT.md:151). The "Not
verified" §1 was reworded (throughput is now measured; quality still is not) and
§5 got the new longest-audio entry. No product code, no new commands, no format
changes.

## Acceptance evidence (exact commands and exit codes)

Model fetch + pinned-checksum verification (exit 0; LOGS =
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/Q050-131.r1/logs`):
```
$ curl -L --max-time 900 -o $LOGS/htdemucs_fp16weights.onnx \
    "https://huggingface.co/StemSplitio/htdemucs-onnx/resolve/d54ed9eb60e258ea82131c6ee14578628816456a/htdemucs_fp16weights.onnx"
$ sha256sum htdemucs_fp16weights.onnx
d05c269d0178d2a72ad484b10b11dd370193fc923201c3b27a99f848745db70a  htdemucs_fp16weights.onnx
EXIT=0   # size 165612636 matches the pinned spec's sizeBytes
```

Reference mix (deterministic, PCG64 seed 20261017):
```
$ python3 -c '<see generator in doc/STEM-SPLIT.md §4>'
1323000 30.0
EXIT=0
$ sha256sum ref-mix-30s.f32
548aab6c6fc07424d9ed752a084eb18a9bde7a37a2d004d11be8b9c6eb47d2ad
```

Benchmark (workdir = worktree; model = <logs>/htdemucs_fp16weights.onnx;
input = /tmp/opencode/ref-mix-30s.f32; run log in <logs>/):
```
$ /usr/bin/python3 tools/stem_split_cli.py --model htdemucs_fp16weights.onnx \
    --input ref-mix-30s.f32 --in-format f32 --in-rate 44100 \
    --out-dir stems-out-N --out-format f32 --progress-json
run 1: EXIT=0 … "elapsed_s": 55.835771, "audio_s": 30.0, "rtf": 1.861192   (2-bench-run-1.jsonl)
run 2: EXIT=0 … "elapsed_s": 64.734849, "audio_s": 30.0, "rtf": 2.157828   (4-bench-run-2.jsonl; process wall 73.695 s)
run 3: EXIT=0 … "elapsed_s": 59.831495, "audio_s": 30.0, "rtf": 1.994383   (6-bench-run-3.jsonl; process wall 69.183 s)
```
All three saw `{"type": "done", ...}` with 11 chunks and wrote four
10,584,000-byte finite stems (sanity printed below).

Output sanity (exit 0):
```
$ /usr/bin/python3 -c '...np.isfinite(all four).all() == True...'
drums frames 1323000 finite True rms 0.2214
bass  frames 1323000 finite True rms 0.2305
other frames 1323000 finite True rms 0.2566
vocals frames 1323000 finite True rms 0.0054
```

Gate exits (all seen on this tree, 2026-10-17):
```
bash tests/complexity-gate.sh --check     EXIT=1  (red on train 5 by design; no file of this unit newly listed)
bash tests/file-length-gate.sh --check    EXIT=1  (same; no file of this unit newly listed)
bash tests/fork-sources-gate.sh           EXIT=0
bash tests/all-sources-reproduce.sh       EXIT=0  (REPRODUCES)
bash tests/no-upstream-regression-gate.sh EXIT=0
bash tests/scripted/check-namespace       EXIT=0  (0 errors)
bash tests/no-tautology-gate.sh           EXIT=0
```

No build, no ctest run: nothing but documentation changed, so there was no target
to build or test to drive; the acceptance proved out through the recorded commands
above.

Contended-box condition, recorded per run:
```
1: 15.09 14.94 11.16 → 45.02 23.18 14.23
2: 39.71 22.77 14.19 → 56.69 31.01 17.68
3: 17.74 24.06 16.97 → 47.15 31.49 20.06
```
