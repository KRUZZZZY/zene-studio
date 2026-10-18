# Cluster new-p9-315 (port to 050/m0-train-5)

**Verdict: FIXED** for the generated-signal path (`--sample-rate`). The WAV path was **ALREADY-FIXED** on this line; no change was needed there.

This is a port of `bugs/qwen-new-p9-315` (`c46f6f91a`, "fix: cluster new-p9-315 NAM harness rates") onto the newest line. The port was written by hand against the current tree; nothing was merged, cherry-picked or rebased.

## Evidence

| Path | Status on this line | file:line |
|---|---|---|
| `--sample-rate` (generated signal) accepts `-48000`, `0`, `nan`, `inf`, `-inf` | Real on the base; fixed here | `plugins/NeuralAmp/tests/nam_harness.cpp:459-463` (new check, before block-size validation and before any model metric) |
| WAV input rate `0` | Already rejected: `readWav` returns false when `rate == 0` | `plugins/NeuralAmp/tests/nam_harness.cpp:252`; caller prints "could not read WAV" and returns 2 at `nam_harness.cpp:511-516` |

The original commit also added a `std::isfinite(parsedRate)` test to the WAV path. A WAV rate is a `uint32_t`, so it can never be NaN or negative, and `rate == 0` already covers the only reachable invalid value. I did not add a redundant check.

Red-run output for the base tree shows the harness accepting the invalid rates and exiting 1 (not the required 2 with a `--sample-rate` message). Examples from `test-1.log`: `--sample-rate -48000` printed `WARNING: ... harness running at -48000 Hz` and ran the sanity checks; `--sample-rate nan` printed `output finite: NO (1 non-finite of 1)`.

Regression: `plugins/NeuralAmp/tests/nam_harness_rate_test.py` (new, from the original commit). It drives the real harness with `-48000`, `0`, `nan`, `inf`, `-inf` and requires exit 2 plus `--sample-rate` in the output. It also checks that a zero-rate WAV exits 2, prints `could not read WAV`, and writes no output file. The WAV-message assertion is new in this port, so exit 2 from a model-load failure cannot pass the WAV case by accident.

Registration: `tests/CMakeLists.txt` (block at the end, `NeuralAmpHarnessRateValidation`); `tests/fork-sources.txt` (new file); `tests/upstream-modifications.txt` (two lines: `nam_harness.cpp`, `CMakeLists.txt`).

## Commands and exit codes

Build environment note: the first build failed with `ccache: error: failed to create temporary file for /run/user/1000/ccache-tmp/...: Permission denied` (build-1.log, EXIT=2). All later builds set `CCACHE_TEMPDIR=/tmp/ccache-tmp-qwen-315` and did not change any source. Run from the worktree root unless noted; `ctest` runs from `build/tests`.

| Step | Command | EXIT | Result | Log |
|---|---|---:|---|---|
| Build, env fault | `cmake --build build --target nam_harness` | 2 | ccache temp-dir permission error | build-1.log |
| Build, pre-fix (RED build) | `CCACHE_TEMPDIR=/tmp/ccache-tmp-qwen-315 cmake --build build --target nam_harness` | 0 | built; ctest then found the new test | build-2.log |
| **RED** | `ctest -R NeuralAmpHarnessRateValidation --output-on-failure` | **8** | 0% passed, 1/1 failed; five generated-rate cases exit 1 | test-1.log |
| Reverse-patch | `git diff -- plugins/NeuralAmp/tests/nam_harness.cpp > fix.patch && git apply -R fix.patch` | 0 / 0 | fix removed | — |
| Rebuild, reverted | `CCACHE_TEMPDIR=... cmake --build build --target nam_harness` | 0 | built | build-4.log |
| Reverted test | `ctest -R NeuralAmpHarnessRateValidation --output-on-failure` | **8** | 0% passed, 1/1 failed | test-3.log |
| Re-apply | `git apply fix.patch` | 0 | fix restored | — |
| Rebuild, fixed | `CCACHE_TEMPDIR=... cmake --build build --target nam_harness` | 0 | built | build-5.log |
| **GREEN** | `ctest -R NeuralAmpHarnessRateValidation --output-on-failure` | **0** | 100% passed, 1/1 executed | test-2.log (after fix, before reverse-patch), test-4.log (after re-apply) |
| Family, first run | `ctest -R "NeuralAmp\|NAM\|Nam" --output-on-failure` | 8 | 7 of 21 "Not Run": executables not built in this tree | test-5.log |
| Build the 7 missing targets | `CCACHE_TEMPDIR=... cmake --build build --target NamespaceRegistryTest TrackKeyNamesLoadTest NeuralAmpModelLoadTest NeuralAmpWavInputTest NamModelMonoOutputTest NeuralAmpProfileArgumentsTest NamModelPrewarmTest -j2` | 0 | built, 0 error lines | build-6.log |
| **Family** | `ctest -R "NeuralAmp\|NAM\|Nam" --output-on-failure` | **0** | **21/21 executed and passed** | test-6.log |
| Gate: complexity (fixed tree) | `bash tests/complexity-gate.sh --check` | **1** | 6 "REGRESSION: new function over target" lines (XyPadPairTest, DawProjectReadTracks, ControlCommandsMixerTools, ControlCommandsMixerStrip, ProjectRevisions, ControlCommandsShortcuts) | gate-complexity-1.log |
| Gate: file length (fixed tree) | `bash tests/file-length-gate.sh --check` | **1** | 14 "REGRESSION: new file over 500 lines" lines (SmfInterchangeReader.cpp, TakeLane.cpp and 12 test files) | gate-filelength-1.log |
| Gate base check | `git diff > full-tracked.patch && git apply -R full-tracked.patch`, run both gates, `git apply full-tracked.patch` | 1 / 1 | same REGRESSION line counts as the fixed tree | gate-complexity-base.log, gate-filelength-base.log |

## Gate status: failing, pre-existing, not caused by this change

Both ratchet gates exit 1 on this branch. With my tracked changes reversed, they fail with the same REGRESSION lines and the same counts. None of the listed functions or files is touched by this change, and `nam_harness.cpp`'s `main` is not in either list.

I did not "fix" the listed files. They are unrelated to NAM, and changing gate baselines is forbidden. The gate failures are on the base tree and need their own owner. They do not block this port's correctness, but the commit should not be described as gate-clean.

## Files committed

- `plugins/NeuralAmp/tests/nam_harness.cpp` (+5, main: `--sample-rate` validation)
- `plugins/NeuralAmp/tests/nam_harness_rate_test.py` (new, regression)
- `tests/CMakeLists.txt` (+9, registers `NeuralAmpHarnessRateValidation`)
- `tests/fork-sources.txt` (+2, registers the new test file)
- `tests/upstream-modifications.txt` (+2, manifest entries for inherited files)
- `docs/qwen-fixes-2026-10/cluster-new-p9-315-port.md` (this report)

## Not checked

- `--sample-rate abc` is parsed by `std::stod`, which throws `std::invalid_argument` on non-numeric input. The throw is not caught in `main` (unverified by run; not in this finding's scope, so not changed).
- The finding's claim that the bogus rate passes the `<5%` and `<1%` gates was not reproduced. What I observed is that the harness exits 1 and prints the metrics with the invalid rate.
- The line-numbered "Where to start" region (loader test configs, `nam_harness.cpp:295-355`) was read but not changed.
