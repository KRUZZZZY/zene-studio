IMPLEMENTED

# Q050-062.3: measure real-model quality and time (stage 3/4 of Q050-062)

Branch `feat/050-Q050-062.3-oc`, cut from `050/m0-train-20` (`c6ee4fc7b`). Feature record:
`docs/050-features/Q050-062.3.md`. Engine doc: `doc/STEM-SPLIT.md` "End-to-end §5". Raw logs:
`~/.local/share/zene-bunny-fix/qwen/runs/Q050-062.3.r1/logs/`.

Each quality setting from stage 1 (`fast` / `standard` / `high`, the 25 / 50 / 75 % segment overlap) was
measured on the pinned real HTDemucs fp16 model. The reference set is four hash-pinned 30 s MUSDB18-HQ
*test* excerpts with aligned source stems. Each excerpt and setting ran twice, interleaved, and both passes
gave identical SDRs:

| quality | passes / 30 s | mean SDR | vs standard | time vs standard |
| --- | --- | --- | --- | --- |
| `fast` | 8 | 7.613 dB | -0.111 dB | 0.64x |
| `standard` | 11 | 7.723 dB | 0 | 1.00x |
| `high` | 22 | 7.768 dB | +0.045 dB | 2.01x |

New: `tools/stem_quality_bench.py` (`fetch` / `run`) and the hermetic ctest `StemQualityBench`
(`tests/stem-quality-bench.py`, a black-box check of `run`). The run's JSON record is `docs/050-features/Q050-062.3-bench.md`, unedited in one fenced block.
The docs, the manual and a stale header comment are updated. No command, engine, GUI or project-format
change.

## Acceptance

| Criterion | Command | What I saw | Exit |
| --- | --- | --- | --- |
| Pinned real model, fixed reference set with aligned source stems, not the stub | `/usr/bin/python3 tools/stem_quality_bench.py run --refset $R/refset --model …/Q050-131.r3/logs/htdemucs_fp16weights.onnx --work $R/work --out $R/logs/07-quality-bench.json --repeats 2` (log `07-run.log`) | 24 runs; recorded `model_sha256` = pinned `d05c269d…db70a` (also `sha256sum` before the run); segment 343,980 (the real model's static input); hops 257985/171990/85995; real-model SDRs of 5.6 to 9.5 dB per excerpt | 0 |
| (fixed set) | `tools/stem_quality_bench.py fetch --out $R/refset` (`02-fetch.log`), then again `--out $R/refset-refetch` with strict pins (`20-refetch.log`) | The second fetch passed the pin check on all 20 files, and `cmp` reports all 20 files identical to the first fetch | 0, 0 |
| Per-setting SDR and elapsed time with model/data identity and reproducible conditions | the JSON record (`docs/050-features/Q050-062.3-bench.md`, verified equal to `07-quality-bench.json` with `json.loads`) + `doc/STEM-SPLIT.md` §5 | SDR per stem/excerpt/setting; `elapsed_s`, RTF and process wall for each run; loadavg before/after; model, CLI and 20 data-file sha256s; Python/onnxruntime/numpy versions, CPU, kernel; exact commands. The cgroup quota (`400000 100000`, captured in `19-bench-cgroup.log`) is in the doc; the tool records it from now on. Identical SDRs on both passes | 0 |
| Tool correctness (hermetic) | `ctest -R '^StemQualityBench$'` | closed-form SDRs (13.9794 dB, 0 dB, exact ≥ 60 dB), chunk counts fast<standard<high, identity fields present | 0 |

Revert proof for the final `StemQualityBench` (`41`-`45-*.log`): tool removed → exit 8 (`FAIL: run exited
2`); SDR `10→20 log10` → exit 8 (`SDR 27.9588 dB, want 13.9794`); only two qualities run → exit 8
(`qualities measured ['fast', 'standard']`); record without the model hash → exit 8 (`run conditions lack
model_sha256`). Restored → exit 0, and the file is byte-identical to the original (`cmp`). The earlier
in-tool selftest had the same proof (`13`-`16`, `31`-`35`).

Final ctest (`22-ctest-final.log`, after rebuilding `zene` and the stem test targets for the `StemTypes.h`
comment; build `21-build.log` exit 0):
`ctest -R '^(StemQualityBench|StemQualityTest|StemJobManagerTest|StemSplitPipelineTest|StemSplitMenuTest|ControlStemCommands|DocFiguresGate|StemAvailabilityDocsGate)$'`
→ 8/8 passed, exit 0. After the test moved to `tests/stem-quality-bench.py` (the build regenerated, `29-build.log`
exit 0): `StemQualityBench|DocFiguresGate|StemAvailabilityDocsGate` → 3/3, exit 0 (`39-ctest.log`).

Gates (`40-feature-check.log`): fork-sources, all-sources-reproduce, no-upstream-regression,
check-namespace, no-tautology, doc-figures PASS; complexity and file-length: no new item. Tools scope:
`complexity-gate.sh --scope tools --check` exit 0, `file-length-gate.sh --scope tools --check` exit 0.

Notes on the environment:
- `feature_check.py` and `submit_check.py` write logs to `<worktree>/../…-feature-check`, which this lane's
  write-jail refuses (`PermissionError`). I ran copies under `/tmp/q0623/fc/` that change only the log
  directory, which is now under the run folder.
- The first `zene` build failed in ccache: its temp dir `…/dsh/home/ccache-tmp-r6` was not writable (`03-build.log`).
  With `CCACHE_TEMPDIR=/tmp/ccache-tmp-q0623` it built, exit 0 (`05-build.log`).

## Known limits

- The timing figures are contended: the run was inside the fix loop's cgroup (4-CPU quota shared with
  other lanes). The ratios against `standard` and the pass counts are the comparable figures, and §4 holds
  the absolute speed. The doc says this.
- Four 30 s excerpts, MDX global SDR (not BSSEval v4), no significance test. `high` was below `standard`
  on one excerpt (-0.012 dB).
- Only the external-process (CLI) backend was run on the real model. The in-process ONNX backend shares
  the hop formula but has no real-model measurement.
- MUSDB18-HQ is research-licensed. No audio is committed, only hashes.
- Stage 4 (source/result association) is untouched.

## Self-check

- **Each new test fails with the change reverted, then passes restored.** `StemQualityBench` exited 8 in
  four separate runs: tool absent, SDR formula mutated, one quality dropped, model hash dropped. Restored, it
  exited 0 (logs `41`-`45`). The unit's other "test" is the real-model render comparison itself: 24 runs, exit 0, and
  identical results on both passes.
- **Every acceptance criterion is proven by a test I ran.** Criterion 1: the `run` command (exit 0) with the
  model hash recorded, plus the two `fetch` runs (exit 0, 0) with 20/20 files byte-identical. Criterion 2:
  the same `run` record and the doc §5. Tool correctness: `StemQualityBench` exit 0. See the table above.
- **One undo step per edit; honest reversibility.** No project edit and no command is added or changed:
  nothing in `src/core/ControlCommands*`, `ControlReversibilityTable*` or `ControlRegistry*`. The snapshot
  is unchanged, and `DocFiguresGate` exit 0.
- **Nothing on the audio thread.** The only product-tree C++ change is a comment in `StemTypes.h`. The
  benchmark is an offline Python tool that runs as its own process. No locks or races are introduced; TSan does
  not run on this box, and nothing here is concurrent.
- **Old projects load and render as before.** No project format, load or render path changed.
- **Portability.** No C++ code changed (one comment). The Python tool guards `os.sched_getaffinity` with
  `hasattr`, reads `/proc` and `/sys/fs/cgroup` in `try/except OSError`, and `os.getloadavg` the same way.
  The ctest uses only the stub model and the CLI that `ControlStemCommands` already runs on every platform,
  and it returns 77 (skip) where numpy/onnxruntime are missing. Its scratch files go in a `tempfile` directory.
- **GUI and agent path (DoD 2).** This stage adds no user action. The quality choice already has its GUI
  entry (stage 2, the **Split to stems** submenu) and its agent argument (stage 1, `stem.job_start
  quality`), both through the registered command. This stage measures those settings and documents the
  result in the manual for both audiences. `submit_check` will WARN that no GUI/command file changed;
  that is expected for a measurement unit.
