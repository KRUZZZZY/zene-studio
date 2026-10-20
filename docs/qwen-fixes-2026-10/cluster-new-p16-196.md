FIXED - rt_alloc_probe refuses malformed, zero, negative, trailing-garbage and out-of-range block sizes before the model loads (cluster new-p16-196, BUG 11)

## Decision

REAL. Luna's finding stands on the current tree.

- `plugins/NeuralAmp/tests/rt_alloc_probe.cpp:91` (before the fix) was `const int block = (argc > 2) ? std::atoi(argv[2]) : 512;`.
- `atoi("abc")` returns 0. The probe then sized both buffers to zero, ran `process()` on 0 frames, and printed `RESULT: NO ALLOCATION IN PROCESS PATH` with exit 0. That is a false real-time-safety pass.
- `atoi("-1")` returns -1. `static_cast<std::size_t>(block)` turns it into a huge vector size, and the probe aborts with `std::length_error`.
- Both reproduced on the unfixed binary (see C4 below).

## Fix (at the cause)

`plugins/NeuralAmp/tests/rt_alloc_probe.cpp`:

- `parseBlockSize` (lines 87-102) parses the whole argument with `strtol`. It rejects empty input, trailing characters, `ERANGE`, and values outside `int`, then requires `1 <= value <= NamModel::kMaxBlock` (8192). This is the same contract as the sibling `nam_profile.cpp` `parseBlockSize` (BUG new-p9-323).
- `main` (lines 111-116) validates the argument before `NamModel::loadFromFile`, prints `blockSize must be in [1, 8192]` and returns 2. Nothing is measured on a rejected argument.
- The usage comment (lines 23-25) now states the accepted range and the exit code 2.

Callers and siblings checked:

- The only caller of the block argument is `main`.
- `plugins/NeuralAmp/tests/nam_harness.cpp:448,479` already range-checks `--block` (`blockSize < 1 || > kMaxBlock` is a usage error). Unchanged.
- `plugins/NeuralAmp/tests/nam_profile.cpp` already uses the same checked parser (BUG new-p9-323). Unchanged.
- `tests/src/core/TwoTrackAlsaCaptureProbe.cpp:79` also uses `atoi` (for a seconds argument). It belongs to another subsystem, is outside this cluster, and is not changed here.

## Regression test

`tests/src/plugins/NeuralAmpAllocProbeArgumentsTest.cpp` (new), registered in `tests/CMakeLists.txt` under `if(TARGET rt_alloc_probe)`, mirroring `NeuralAmpProfileArgumentsTest`. It runs the probe as a subprocess against a minimal in-test model and checks the exit code, the message, and that no `RESULT:` line appears on a rejected argument.

| test slot | arguments | expects |
| --- | --- | --- |
| rejectsMalformedBlockSize | `abc` | exit 2, no RESULT |
| rejectsNegativeBlockSize | `-1` | exit 2, no RESULT |
| rejectsZeroBlockSize | `0` | exit 2, no RESULT |
| rejectsTrailingGarbageBlockSize | `512x` | exit 2, no RESULT |
| rejectsOutOfRangeBlockSize | `8193`, `4294967297` (2^32+1 would truncate to 1 in an `int`) | exit 2, no RESULT |
| acceptsBlockSizeBounds | `1`, `8192` | exit 0, NO ALLOCATION |
| acceptsDefaultBlockSize | (none) | exit 0, NO ALLOCATION |

No timing, sleeps, or races. Each case is a deterministic subprocess with fixed arguments.

## Evidence

Direct probe output on the minimal model (`logs/minimal.nam`, the same JSON the test writes):

| argument | before fix | after fix |
| --- | --- | --- |
| `abc` | EXIT 0, `block=0 ... (0 frames)`, `RESULT: NO ALLOCATION` (false pass) | EXIT 2 |
| `-1` | EXIT 134, `terminate called ... std::length_error` | EXIT 2 |
| `0` | EXIT 0, `block=0 ... (0 frames)`, `RESULT: NO ALLOCATION` (false pass) | EXIT 2 |
| `512x` / `8193` / `4294967297` | not run directly (covered by the test) | EXIT 2 |
| `1` / `512` / `8192` | EXIT 0 | EXIT 0 |

Log files are in `logs/`: `probe-red-*.log`, `probe-green-*.log`, `probe-green-summary.log`.

### Commands and exit codes (run from the worktree unless stated)

| # | command | EXIT | log |
| --- | --- | --- | --- |
| C1 | `cmake --build build --target rt_alloc_probe NeuralAmpAllocProbeArgumentsTest -j4 > build-1-red.log 2>&1` | 2 | build-1-red.log (ccache could not write `/run/user/1000/ccache-tmp`: permission denied in the sandbox) |
| C2 | same as C1 with `CCACHE_TEMPDIR=/tmp/zene-ccache-tmp-p16-196` | 0 | build-2-red.log |
| C3 (RED) | `cd build/tests && ctest -R NeuralAmpAllocProbeArgumentsTest --output-on-failure` | 8 | test-1-red.log: `0% tests passed, 1 tests failed out of 1`; `Totals: 4 passed, 5 failed`; `Actual (result.exitCode): 0`, `Actual (result.status): CrashExit` |
| C4 | `build/plugins/rt_alloc_probe logs/minimal.nam {abc,-1,0,512}` on the unfixed binary | 0 / 134 / 0 / 0 | probe-red-*.log |
| C5 | `cmake --build build --target rt_alloc_probe NeuralAmpAllocProbeArgumentsTest -j4` after the fix | 0 | build-3-green.log |
| C6 | `cmake --build build --target NeuralAmpProfileArgumentsTest -j4` (the sibling was not built in this tree; its earlier ctest showed `Not Run`) | 0 | build-4-sibling.log |
| C7 (GREEN) | `cd build/tests && ctest -R "NeuralAmpAllocProbeArgumentsTest\|NeuralAmpProfileArgumentsTest"` | 0 | test-3-green-family.log: `100% tests passed out of 2` |
| C8 | 4b: `git diff -- plugins/NeuralAmp/tests/rt_alloc_probe.cpp > logs/fix.patch` | 0 | - |
| C9 | 4b: `git apply -R logs/fix.patch` | 0 | - |
| C10 | 4b: rebuild (C5 command) | 0 | build-5-revert.log |
| C11 (RED, proof) | 4b: `ctest -R NeuralAmpAllocProbeArgumentsTest` with the fix reverted | 8 | test-4-revert-RED.log: `Totals: 4 passed, 5 failed` |
| C12 | 4b: `git apply logs/fix.patch` | 0 | - |
| C13 | 4b: rebuild (C5 command) | 0 | build-6-reapply.log |
| C14 (GREEN, proof) | 4b: `ctest -R NeuralAmpAllocProbeArgumentsTest` with the fix reapplied | 0 | test-5-reapply-GREEN.log: `100% tests passed out of 1` |
| C15 | `python3 feature_check.py <worktree>` as written | 1 | not saved (traceback: `PermissionError` creating `<worktree>-feature-check`, a sibling of the worktree outside the write-jail) |
| C16 | `python3 /tmp/fc-p16-196/feature_check_logs.py <worktree>` (copy of feature_check.py; only ROOT and the log dir differ, the checks are unchanged) | 0 | gates-1.log (before the manifest registrations) |
| C17 | `python3 /tmp/fc-p16-196/submit_check_copy.py <worktree> new-p16-196` | 1 | submit-1.log: gates FAIL on `fork-sources`, `all-sources-reproduce`, `no-upstream-regression` |
| C18 | fix for C17: register `plugins/NeuralAmp/tests/rt_alloc_probe.cpp` in `tests/fork-sources.txt` (fork-new, added after the gate base) and re-derive `tests/all-sources.txt` (C19) | - | commit 9f21b8d0f |
| C19 | regenerate: `{ git ls-files '*.c' ... ; git ls-files tests/control-detect-commands.py tests/control_detect_fixtures.py ; } \| LC_ALL=C sort -u` into a temp file; `diff` against the current entries shows only `+tests/src/plugins/NeuralAmpAllocProbeArgumentsTest.cpp` (diff exit 1, as expected); spliced under the unchanged 70-line header; the manifest's "Verify it" check prints `REPRODUCES` | 1 then REPRODUCES | - |
| C20 | `python3 /tmp/fc-p16-196/feature_check_logs.py <worktree>` on commit 9f21b8d0f | 0 | gates-3.log (final, 8/8 PASS) |
| C21 | `python3 /tmp/fc-p16-196/submit_check_copy.py <worktree> new-p16-196` on 9f21b8d0f | 1 | submit-2.log: only `working tree clean` FAIL (uncommitted report at that point); all gates PASS |

Notes on C15-C21: the gate writes its logs to a sibling of the worktree, which the write-jail forbids. The copy in C16 has its log folder moved into the LOG folder and its ROOT pointed at the qwen directory (where `FIX-BASE` and the cached base listings are). `submit_check.py` is run from a `/tmp` copy for the same reason: its gate step calls `feature_check.py` from its own directory. Its checks are unchanged.

### Affected family

- `NeuralAmpAllocProbeArgumentsTest` (1 test, GREEN exit 0; RED exit 8).
- `NeuralAmpProfileArgumentsTest` (1 test, exit 0, unchanged code).
- Total executed in the family: 2. Zero tests executed would have been an error; this was not the case.

### Gates (C20, on commit 9f21b8d0f)

| gate | result |
| --- | --- |
| fork-sources | PASS (exit 0) |
| all-sources-reproduce | PASS (exit 0) |
| no-upstream-regression | PASS (exit 0) |
| check-namespace | PASS (exit 0) |
| no-tautology | PASS (exit 0) |
| doc-figures | PASS (exit 0) |
| complexity (ratchet) | PASS: no new item in your files |
| file-length (ratchet) | PASS: no new item in your files |
| RESULT | PASS, exit 0 |

The ratchet logs also print `FAIL: complexity ratchet regressed (check mode: baseline not written)` and the same for file-length. Those are the train-wide red items the table's "no new item in your files" rule exists for; none of them lists a file this branch touched.

### Files committed

- `c8046be7b`: `plugins/NeuralAmp/tests/rt_alloc_probe.cpp` (fix), `tests/CMakeLists.txt` (registration), `tests/src/plugins/NeuralAmpAllocProbeArgumentsTest.cpp` (new regression test), `tests/upstream-modifications.txt` (`tests/CMakeLists.txt` is an inherited file; reason recorded), `docs/qwen-fixes-2026-10/cluster-new-p16-196.md` (this report, first version)
- `9f21b8d0f`: `tests/fork-sources.txt` (`tests/src/plugins/NeuralAmpAllocProbeArgumentsTest.cpp` and `plugins/NeuralAmp/tests/rt_alloc_probe.cpp` registered as fork-new), `tests/all-sources.txt` (re-derived, one line added)
- Final commit: this report (verdict, Self-check and the C15-C21 rows).

## Follow-up (not changed here)

`docs/specs/NEURAL-AMP.md:415` shows `build-rel/plugins/rt_alloc_probe ... --blocks 512`. The probe has no `--blocks` option. The old code read that as block 0 and reported a false pass. The new code exits 2 with the range message. The doc should be changed to `... 512` in a separate docs change.

## Self-check

Answers to the pre-submit list, each with the evidence above:

- **"The regression test FAILS with your product change reverted and PASSES with it (exit codes)."** Yes. Reverted with `git apply -R` on the saved patch (only product file: `plugins/NeuralAmp/tests/rt_alloc_probe.cpp`): C11 ctest exit 8, 5 failed. Reapplied: C14 ctest exit 0, 1 of 1 passed.
- **"The test is deterministic: no wall-clock timing, sleeps or races (name the seam or injected values it uses)."** It uses no timing or sleep and needs no seam. It runs the probe as a subprocess with fixed argument strings and a fixed in-test model JSON, and checks an exit code and fixed substrings. `QProcess::waitForFinished()` uses its default timeout only as a bound on the run, not as an assertion.
- **"The fix is at the cause and complete: every caller and sibling of what you changed was checked and stays consistent."** The only caller is `main`. Siblings checked: `nam_profile.cpp` (same checked parser, BUG new-p9-323), `nam_harness.cpp:448,479` (already range-checked), and `tests/src/core/TwoTrackAlsaCaptureProbe.cpp:79` (`atoi` in another subsystem, outside this cluster, not changed). The argument is validated before any model load, so no path reaches the measurement with a bad size.
- **"No collateral damage: old projects still load as before, and any limit or check you added does not hit other callers that legitimately exceed it."** No project or save format is involved. The bound is `[1, NamModel::kMaxBlock]`, the same bound the model enforces for its buffers and the same as `nam_profile`. The default (512) and the bounds (`1`, `8192`) still exit 0 with `NO ALLOCATION` (C14 test and probe-green logs). The one caller that passes an invalid value is the docs example in the Follow-up section, which the old code silently measured wrongly.
- **"If your verdict is ALREADY-FIXED or FALSE-POSITIVE: the code (file:line) or the intended contract that shows it."** Not applicable: the verdict is FIXED (REAL). The defect is shown at `rt_alloc_probe.cpp:91` (before the fix) and reproduced in C4.
- **Undo, realtime and command items.** Not applicable. No control command was added or changed, no undo step is involved, and nothing on any audio path was touched: the change is in the CLI `main()` before the measured window.
