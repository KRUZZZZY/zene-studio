FIXED - rt_alloc_probe refuses malformed, zero, negative, trailing-garbage and out-of-range block sizes before the model loads (cluster new-p16-196, BUG 11)

## Decision

REAL. Luna's finding stands on the current tree.

- `plugins/NeuralAmp/tests/rt_alloc_probe.cpp:91` (before the fix) was `const int block = (argc > 2) ? std::atoi(argv[2]) : 512;`.
- `atoi("abc")` returns 0. The probe then sized both buffers to zero, ran `process()` on 0 frames, and printed `RESULT: NO ALLOCATION IN PROCESS PATH` with exit 0. That is a false real-time-safety pass.
- `atoi("-1")` returns -1. `static_cast<std::size_t>(block)` turns it into a huge vector size, and the probe aborts with `std::length_error`.
- Both reproduced on the unfixed binary (see RED below).

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

Direct probe output on the minimal model (`logs/minimal.nam`, written by the test's same JSON):

| argument | before fix | after fix |
| --- | --- | --- |
| `abc` | EXIT 0, `block=0 ... (0 frames)`, `RESULT: NO ALLOCATION` (false pass) | EXIT 2 |
| `-1` | EXIT 134, `terminate called ... std::length_error` | EXIT 2 |
| `0` | EXIT 0, `block=0 ... (0 frames)`, `RESULT: NO ALLOCATION` (false pass) | EXIT 2 |
| `512x` / `8193` / `4294967297` | not run directly (covered by test) | EXIT 2 |
| `1` / `512` / `8192` | EXIT 0 | EXIT 0 |

Log files are in `logs/`: `probe-red-*.log`, `probe-green-*.log`, `probe-green-summary.log`.

### Commands and exit codes (run from the worktree unless stated)

| # | command | EXIT | log |
| --- | --- | --- | --- |
| C1 | `cmake --build build --target rt_alloc_probe NeuralAmpAllocProbeArgumentsTest -j4 > build-1-red.log 2>&1` | 2 | build-1-red.log (ccache could not write `/run/user/1000/ccache-tmp`, a permission error in the sandbox) |
| C2 | same as C1 with `CCACHE_TEMPDIR=/tmp/zene-ccache-tmp-p16-196` | 0 | build-2-red.log |
| C3 (RED) | `cd build/tests && ctest -R NeuralAmpAllocProbeArgumentsTest --output-on-failure` | 8 | test-1-red.log: `0% tests passed, 1 tests failed out of 1`; `Totals: 4 passed, 5 failed`; `Actual (result.exitCode): 0`, `Actual (result.status): CrashExit` |
| C4 | `build/plugins/rt_alloc_probe logs/minimal.nam {abc,-1,0,512}` on the unfixed binary | 0 / 134 / 0 / 0 | probe-red-*.log |
| C5 | `cmake --build build --target rt_alloc_probe NeuralAmpAllocProbeArgumentsTest -j4` after the fix | 0 | build-3-green.log |
| C6 | `cmake --build build --target NeuralAmpProfileArgumentsTest -j4` (sibling was not built in this tree; its earlier ctest showed `Not Run`) | 0 | build-4-sibling.log |
| C7 (GREEN) | `cd build/tests && ctest -R "NeuralAmpAllocProbeArgumentsTest\|NeuralAmpProfileArgumentsTest"` | 0 | test-3-green-family.log: `100% tests passed out of 2` |
| C8 | 4b: `git diff -- plugins/NeuralAmp/tests/rt_alloc_probe.cpp > logs/fix.patch` | 0 | - |
| C9 | 4b: `git apply -R logs/fix.patch` | 0 | - |
| C10 | 4b: rebuild (C5 command) | 0 | build-5-revert.log |
| C11 (RED, proof) | 4b: `ctest -R NeuralAmpAllocProbeArgumentsTest` with the fix reverted | 8 | test-4-revert-RED.log: `Totals: 4 passed, 5 failed` |
| C12 | 4b: `git apply logs/fix.patch` | 0 | - |
| C13 | 4b: rebuild (C5 command) | 0 | build-6-reapply.log |
| C14 (GREEN, proof) | 4b: `ctest -R NeuralAmpAllocProbeArgumentsTest` with the fix reapplied | 0 | test-5-reapply-GREEN.log: `100% tests passed out of 1` |
| C15 | `python3 feature_check.py <worktree>` as written | 1 | not saved (traceback: `PermissionError` creating `<worktree>-feature-check`, a sibling of the worktree outside the write-jail) |
| C16 | `python3 /tmp/fc-p16-196/feature_check_logs.py <worktree>` (copy of feature_check.py; only ROOT and the log dir differ, the checks are unchanged) | 0 | gates-1.log (before manifest registration) |
| C17 | same as C16 after registering the test and the CMake change | 0 | gates-2.log (final) |

Note on C15-C17: the gate writes its logs to a sibling of the worktree, which the write-jail forbids. The copy in C16 has its log folder moved into the LOG folder and its ROOT pointed at the qwen directory (where `FIX-BASE` and the cached base listings are). The checks themselves are unchanged.

### Affected family

- `NeuralAmpAllocProbeArgumentsTest` (1 test, GREEN exit 0; RED exit 8).
- `NeuralAmpProfileArgumentsTest` (1 test, exit 0, unchanged code).
- Total executed in the family: 2. Zero tests executed would have been an error; this was not the case.

### Gates (C17, final)

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

### Files committed

- `plugins/NeuralAmp/tests/rt_alloc_probe.cpp` (fix)
- `tests/CMakeLists.txt` (registration)
- `tests/src/plugins/NeuralAmpAllocProbeArgumentsTest.cpp` (new regression test)
- `tests/fork-sources.txt` (new test file registered as fork-new)
- `tests/upstream-modifications.txt` (`tests/CMakeLists.txt` is an inherited file; reason recorded)
- `docs/qwen-fixes-2026-10/cluster-new-p16-196.md` (this report)

Not registered: `plugins/NeuralAmp/tests/rt_alloc_probe.cpp` itself is fork-new, but no other plugin test tool is in `fork-sources.txt` either, so it follows that convention.

## Follow-up (not changed here)

`docs/specs/NEURAL-AMP.md:415` shows `build-rel/plugins/rt_alloc_probe ... --blocks 512`. The probe has no `--blocks` option. The old code read that as block 0 and reported a false pass. The new code exits 2 with the range message. The doc should be changed to `... 512` in a separate docs change.

## Self-check

- **Each new test FAILS with the product change reverted and PASSES restored (exit codes).** Yes: C11 exit 8 (5 failed) with the fix reverted; C14 exit 0 with it reapplied. Reverted hunk: the only product file is `plugins/NeuralAmp/tests/rt_alloc_probe.cpp`, reversed with `git apply -R`.
- **Every acceptance criterion is proven by a test I ran.** The criterion is that malformed and out-of-range block sizes are refused and valid sizes still measure. Refusal is proven by `rejectsMalformedBlockSize`, `rejectsNegativeBlockSize`, `rejectsZeroBlockSize`, `rejectsTrailingGarbageBlockSize`, `rejectsOutOfRangeBlockSize` (C14 exit 0). Valid sizes are proven by `acceptsBlockSizeBounds` and `acceptsDefaultBlockSize` (C14 exit 0).
- **Undo / reversibility.** Not applicable. No command, no undo step and no project data changed. The probe is a standalone CLI measurement tool.
- **Nothing on the audio thread allocates, frees, locks, waits, does I/O or emits a signal.** The change is in `main()` before the measured window, and it runs before any `process()` call. No product audio path is touched.
- **Old projects still load and render exactly as before.** Not applicable: no model, project or save format changed. Valid arguments (default, `1`, `512`, `8192`) give exit 0 and `NO ALLOCATION` both before and after (C14 and probe-green logs).
- **Portability.** The probe is already Linux-only (`if(UNIX AND NOT APPLE)` in `plugins/NeuralAmp/tests/CMakeLists.txt`). The new test is registered only under `if(TARGET rt_alloc_probe)`. The code uses only `<cerrno>`, `<cstdlib>`, `<limits>`.
- **The test is deterministic.** It runs a subprocess with fixed arguments and checks its exit code and output. No timing or sleeps.
- **The fix is at the cause and complete.** See "Callers and siblings checked" above.
- **No collateral damage.** The new bound is `[1, NamModel::kMaxBlock]`, the same bound the model enforces for its buffers and the same as `nam_profile`. No legitimate caller is in the tree except the documented example, which is covered in Follow-up.
- **Verdict is evidenced.** REAL, with the file:line and the RED output (C3, C4) above.
