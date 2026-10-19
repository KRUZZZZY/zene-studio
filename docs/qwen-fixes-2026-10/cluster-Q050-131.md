IMPLEMENTED

# Cluster Q050-131 — Benchmark real-model stem separation speed

Docs-only unit. `doc/STEM-SPLIT.md` gained "End-to-end §4. Real-model benchmark
(Q050-131, 2026-10-17/18/19)" (doc/STEM-SPLIT.md:149): the pinned real model
(commit `d54ed9eb60e258ea82131c6ee14578628816456a` of StemSplitio/htdemucs-onnx,
sha256 re-verified before each measurement day) on a deterministic 30.0 s stereo
44.1 kHz reference mix (sha256 recorded, generator command printed), driven by
the exact CLI the GUI's external-process backend spawns. Three runs per day
recorded wall time and real-time factor; hardware, model revision, clip duration
and exact commands all recorded in the file. Stub-model figures stay in sections
1–3, explicitly labelled as stub-model only (doc/STEM-SPLIT.md:146,
doc/STEM-SPLIT.md:151). The "Not verified" §1 was reworded (throughput is now
measured; quality still is not) and §5 got the new longest-audio entry. This
round (2026-10-19) re-ran the benchmark fresh so every figure below was seen in
this session; no product code, no new commands, no format changes.

## Acceptance evidence (exact commands and exit codes)

Acceptance table (this session, 2026-10-19; LOGS =
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/Q050-131.r4/logs`):

| # | criterion | command (as run) | observed result | EXIT |
|---|---|---|---|---|
| 1 | Documented benchmark on the pinned real model and reference audio, wall time and real-time factor recorded | `sha256sum <logs>/htdemucs_fp16weights.onnx` → `/usr/bin/python3 <worktree>/tools/stem_split_cli.py --model <logs>/htdemucs_fp16weights.onnx --input ref-mix-30s.f32 --in-format f32 --in-rate 44100 --out-dir stems-out-N --out-format f32 --progress-json` × 3 (cwd `/tmp/opencode/q050-131-r4`, wall via `date +%s.%N` around each process, loadavg via `uptime` before/after) | sha256 = `d05c269d…745db70a` (matches the pin, 165,612,636 B); runs printed `{"type":"done",...,"elapsed_s":38.86537/37.441109/33.987777,"audio_s":30.0,"rtf":1.295512/1.248037/1.132926}`, process walls 47.253/45.864/42.200 s — recorded in doc/STEM-SPLIT.md §4 with the exact commands (logs: `01-model-sha256.log`, `run-{1,2,3}.jsonl`, `0{4,5,6}-run-N-shell.log`, `run-N-load.txt`) | 0, 0, 0 |
| 2 | Hardware, model revision, clip duration and exact command in STEM-SPLIT.md; stub-model figures labelled separately | `doc/STEM-SPLIT.md` §4 + `sha256sum` of model and ref mix + regenerating the ref mix with the doc's generator | hardware (Intel Core Ultra 7 255HX, 20 cores, 30 GiB, kernel `7.2.9-2-cachyos`, `00-hardware.log`), model revision (pinned commit + sha256, re-verified exit 0), clip duration (30.0 s, 1,323,000 frames), exact commands (ref-mix generator + per-run CLI line) each recorded; §4 opens by stating sections 1–3 are stub-model only and every stub number keeps its "stub-model" labelling; regenerating the ref mix reproduced sha256 `548aab6c…47d2ad` byte-for-byte (`02-refmix.log`) | sha256sum 0, generator 0 |

Reference mix (deterministic, PCG64 seed 20261017; doc generator verbatim):
```
$ /usr/bin/python3 -c '<generator printed in doc/STEM-SPLIT.md §4>'
$ sha256sum ref-mix-30s.f32
548aab6c6fc07424d9ed752a084eb18a9bde7a37a2d004d11be8b9c6eb47d2ad  ref-mix-30s.f32
EXIT=0   # identical to the sha256 recorded in the doc (02-refmix.log)
```

Benchmark runs (2026-10-19, all printed `{"type":"done"}` with 11 chunks):
```
run 1: EXIT=0  "elapsed_s": 38.86537  "audio_s": 30.0  "rtf": 1.295512  wall 47.253 s  (run-1.jsonl, 04-run-1-shell.log)
run 2: EXIT=0  "elapsed_s": 37.441109 "audio_s": 30.0  "rtf": 1.248037  wall 45.864 s  (run-2.jsonl, 05-run-2-shell.log)
run 3: EXIT=0  "elapsed_s": 33.987777 "audio_s": 30.0  "rtf": 1.132926  wall 42.200 s  (run-3.jsonl, 06-run-3-shell.log)
```

Load condition (recorded per run, `uptime` before → after):
```
1: 7.48 7.57 4.66 → 19.28 10.63 5.84
2: 17.06 10.56 5.90 → 21.20 12.45 6.75
3: 18.94 12.25 6.75 → 21.34 13.58 7.42
```

Output sanity (exit 0, `07-output-sanity.log`): run 3's four stems are each
10,584,000 bytes (1,323,000 frames × 2 ch × 4 B), every sample finite, per-stem
RMS drums 0.221 / bass 0.231 / other 0.257 / vocals 0.005 — identical to the
2026-10-17 and 2026-10-18 values, so the pinned pipeline is deterministic.

Gate exits (all seen on this tree, 2026-10-19; logs `08`…`17` in LOGS):
```
bash tests/complexity-gate.sh --check     EXIT=1  (red on train 5 by design; 0 mentions of this unit's files;
                                                   ../zene-cachyos fails identically: EXIT=1, logs 11/12)
bash tests/file-length-gate.sh --check    EXIT=1  (same; no file of this unit newly listed; ../zene-cachyos EXIT=1)
bash tests/fork-sources-gate.sh           EXIT=0  (PASS: 1792 fork-NEW, 1212 inherited, 48 tooling)
bash tests/all-sources-reproduce.sh       EXIT=0  (REPRODUCES)
bash tests/no-upstream-regression-gate.sh EXIT=0  (PASS: 1497 changed path(s) declared)
tests/scripted/check-namespace            EXIT=0  (0 errors; must be run directly, it is a Python script)
bash tests/no-tautology-gate.sh           EXIT=0  (PASS)
python3 tests/doc-figures-gate.py         EXIT=0  (PASS: snapshot measures 828 ids / 76 groups — unchanged)
```

No build, no ctest run: nothing but documentation changed, so there was no target
to build or test to drive; the acceptance proved out through the recorded commands
above.
