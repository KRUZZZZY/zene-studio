# Cluster new-p10-271-port

**Verdict: FIXED**

The finding was real against the upstream base of this branch and is fixed at the cause, end to end.

- Finding: `plugins/NeuralAmp/tests/nam_harness.cpp` added the generated block count
  (`opt.blocks + opt.warmupBlocks`) as a narrow `int` before widening it, so the sum could
  overflow and turn into a huge (or negative) allocation request.
- Upstream base (`ded355440`, the state the finding was written against) still has that
  narrow addition; the regression below fails against it with
  `std::length_error: cannot create std::vector larger than max_size()`.
- The first half of the fix (wide addition + a 4,194,304-sample limit) is already on this
  branch in commit `68ad344a0` (its own evidence table is in that commit's version of this
  file). This round completes the family: the counts are 64-bit end to end, the count
  options are parsed without throwing, and the planned sample count is bounded before any
  multiplication or allocation. The evidence table below is this round's own evidence; the
  reverse-patch proof is run against the whole chain, not just this round's diff.

## What changed

`plugins/NeuralAmp/tests/nam_harness.cpp`

| Location | Change |
|---|---|
| `nam_harness.cpp:56-57` | `Options::blocks` and `Options::warmupBlocks` are `int64_t`, so the count never lives in a 32-bit type. |
| `nam_harness.cpp:65-74` | `parseInt64Option()` — `strtoll` + `ERANGE` + full-consumption check; no exception can escape `main`. |
| `nam_harness.cpp:78-89` | `parseIntOption()` — the same, range-checked against `int` (used for the frame count). |
| `nam_harness.cpp:95-104` | `planGeneratedSamples()` — bounds both counts *before* adding them, so the sum (`<= 2 * limit`) and the product (checked against `limit / blockSize` before multiplying) cannot overflow; a run over the limit is refused before any buffer exists. |
| `nam_harness.cpp:480-503` | `--block`, `--blocks` and `--warmup` are parsed with the checked parsers; a malformed or out-of-range value is a usage error (exit 2), not an abort. |
| `nam_harness.cpp:546-552` | The generated-run plan replaces the inline `int` addition; the rejection message and exit code are unchanged. |
| `nam_harness.cpp:684-685, 711, 721, 728, 750-751, 816-817` | Every consumer of the widened counts was made consistent: `int64_t` loop counters, `%lld` formats (rendered text unchanged), explicit `size_t` cast for `blockUs.reserve()`, and the sample offsets are now wide products of two 64-bit operands. |

`tests/neuralamp-harness-block-count.py` (registered ctest `NeuralAmpHarnessBlockCount`,
`tests/CMakeLists.txt:5706-5716`, already listed in `tests/fork-sources.txt:2148`)

The regression now drives the whole family through the real harness binary: the reported
case (`--blocks 2147483647`), both counts at `INT_MAX` (the sum alone overflows a 32-bit
addition), a count that no longer fits the narrow type (`--blocks 2147483648`), counts that
are not numbers (`--blocks not-a-number`, `--blocks 99999999999999999999`), the same for
`--warmup` and `--block`, and the pre-existing range messages (`--blocks 0`,
`--warmup -1`). Every case must be refused with exit code 2 and a usage message, and the
test explicitly rejects an abort (`terminate called`).

`tests/upstream-modifications.txt:2270` — the ledger reason for the inherited harness file
now states what this round changed.

## Evidence

Working directory is the repository root unless noted. Log folder:
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-271-port.r5/logs`.

| Phase | Exact command | Exit |
|---|---|---:|
| Baseline build (before this round) | `cmake --build build --target nam_harness -j 8 > .../logs/build-1.log 2>&1` | 0 |
| Baseline family run | `cd build/tests && ctest -R "NeuralAmpHarnessBlockCount\|NeuralAmpWavRunSampleRateWarning\|NamLoaderTest" --output-on-failure > .../logs/test-1.log 2>&1` (10/10 passed) | 0 |
| Baseline gates | `python3 .../feature_check.py <worktree> > .../logs/feature-check-1.log 2>&1` | 0 |
| Build after the fix | `cmake --build build --target nam_harness -j 8 > .../logs/build-2.log 2>&1` | 0 |
| GREEN after the fix (extended regression) | `python3 tests/neuralamp-harness-block-count.py build/plugins/nam_harness > .../logs/test-3.log 2>&1` | 0 |
| **RED A** — this round's product patch reversed | `git diff -- plugins/NeuralAmp/tests/nam_harness.cpp > .../logs/fix.patch && git apply -R .../logs/fix.patch` | 0 |
| RED A rebuild | `cmake --build build --target nam_harness -j 8 > .../logs/build-red-a.log 2>&1` | 0 |
| **RED A test** | `python3 tests/neuralamp-harness-block-count.py build/plugins/nam_harness > .../logs/test-red-a.log 2>&1` -> `FAIL: args=--blocks 2147483648 exit=-6 (expected 2): terminate called after throwing an instance of 'std::out_of_range'` | **1** |
| Restore this round's patch | `git apply .../logs/fix.patch` | 0 |
| Rebuild | `cmake --build build --target nam_harness -j 8 > .../logs/build-green-a.log 2>&1` | 0 |
| **GREEN A** | `python3 tests/neuralamp-harness-block-count.py build/plugins/nam_harness > .../logs/test-green-a.log 2>&1` -> `PASS: oversized generated run rejected with a valid NAM model` | **0** |
| **RED B** — whole chain reversed to the upstream base | `git diff ded355440 -- plugins/NeuralAmp/tests/nam_harness.cpp > .../logs/fix-full.patch && git apply -R .../logs/fix-full.patch` | 0 |
| RED B rebuild | `cmake --build build --target nam_harness -j 8 > .../logs/build-red-b.log 2>&1` | 0 |
| **RED B test** (the finding's own symptom) | `python3 tests/neuralamp-harness-block-count.py build/plugins/nam_harness > .../logs/test-red-b.log 2>&1` -> `FAIL: args=--blocks 2147483647 exit=-6 (expected 2): terminate called after throwing an instance of 'std::length_error' what(): cannot create std::vector larger than max_size()` | **1** |
| Restore the whole chain | `git apply .../logs/fix-full.patch` | 0 |
| Rebuild | `cmake --build build --target nam_harness -j 8 > .../logs/build-green-b.log 2>&1` | 0 |
| **GREEN B** | `python3 tests/neuralamp-harness-block-count.py build/plugins/nam_harness > .../logs/test-green-b.log 2>&1` | **0** |
| Product build | `cmake --build build --target zene -j 8 > .../logs/build-zene.log 2>&1` | 0 |
| Build the family test executables | `cmake --build build --target NeuralAmpModelLoadTest NeuralAmpWavInputTest NamModelMonoOutputTest NeuralAmpProfileArgumentsTest NamModelPrewarmTest NamespaceRegistryTest TrackKeyNamesLoadTest -j 8 > .../logs/build-tests.log 2>&1` | 0 |
| Family run, first attempt (test binaries not built yet -> 7 "Not Run") | `cd build/tests && ctest -R "NeuralAmp\|NamLoader\|Nam" --output-on-failure > .../logs/test-family.log 2>&1` | 8 |
| **Family run after the test binaries were built (18/18 passed)** | `cd build/tests && ctest -R "NeuralAmp\|NamLoader\|Nam" --output-on-failure > .../logs/test-family-2.log 2>&1` | **0** |
| Gates | `python3 .../feature_check.py <worktree> > .../logs/feature-check-2.log 2>&1` -> `RESULT: PASS` | 0 |
| Ratchet gate 4 | `bash tests/complexity-gate.sh --check > .../logs/gate-complexity.log 2>&1` (6 pre-existing regressions, none in my files) | 1 |
| Ratchet gate 7 | `bash tests/file-length-gate.sh --check > .../logs/gate-file-length.log 2>&1` (7 pre-existing regressions, none in my files) | 1 |
| Ratchet gate 9 | `bash tests/fork-sources-gate.sh > .../logs/gate-fork-sources.log 2>&1` | 0 |

Affected-family test count: **18/18** (`ctest -R "NeuralAmp|NamLoader|Nam"`, `.../logs/test-family-2.log`),
of which `NeuralAmpHarnessBlockCount` is the new regression and
`NeuralAmpWavRunSampleRateWarning`, `NeuralAmpCompareRateTest`, `NeuralAmpWavInputTest`,
`NeuralAmpModelLoadTest`, `NeuralAmpProfileArgumentsTest`, `NamModelMonoOutputTest`,
`NamModelPrewarmTest` and the eight `NamLoaderTest_*` cases are the rest.

Gates 4 and 7 are red on this train branch before this change as well (the same six and
seven entries, all in `src/core/...`, `tests/src/...` files that this change does not
touch); `grep 'nam_harness\|neuralamp-harness'` over both gate logs finds nothing, and
`feature_check.py` reports `complexity: no new item in your files` and
`file-length: no new item in your files`.

## Files in this commit

- `plugins/NeuralAmp/tests/nam_harness.cpp` (product fix)
- `tests/neuralamp-harness-block-count.py` (regression)
- `tests/upstream-modifications.txt` (ledger reason for the inherited harness file)
- `docs/qwen-fixes-2026-10/cluster-new-p10-271-port.md` (this report)

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it (exit codes).**
  Proof A (this round's product patch): `git diff -- plugins/NeuralAmp/tests/nam_harness.cpp > .../logs/fix.patch`
  (EXIT=0), `git apply -R .../logs/fix.patch` (EXIT=0), rebuild (EXIT=0), test -> **EXIT=1** with
  `FAIL: args=--blocks 2147483648 exit=-6 (expected 2): terminate called after throwing an instance of
  'std::out_of_range'` (`.../logs/test-red-a.log`); `git apply .../logs/fix.patch` (EXIT=0), rebuild
  (EXIT=0), test -> **EXIT=0** (`.../logs/test-green-a.log`).
  Proof B (the whole chain, i.e. the harness reverted to the upstream base `ded355440`): reverse-apply
  (EXIT=0), rebuild (EXIT=0), test -> **EXIT=1** with the finding's own symptom
  `std::length_error: cannot create std::vector larger than max_size()` (`.../logs/test-red-b.log`);
  restore (EXIT=0), rebuild (EXIT=0), test -> **EXIT=0** (`.../logs/test-green-b.log`).
  The regression is not a tautology: it drives the real `nam_harness` binary and asserts on its exit
  code and stderr, and it fails on both the pre-fix and the partially-fixed code.

- **The test is deterministic: no wall-clock timing, sleeps or races.** The seam is the harness's own
  command-line contract: the test writes a fixed minimal WaveNet `.nam` fixture into a temp directory
  and runs the harness with fixed `--block/--blocks/--warmup` values, then asserts the process exit
  code (2) and the stderr message. No sleep, no clock, no thread, no random input. The fixture model
  and every count are literal constants in `tests/neuralamp-harness-block-count.py:19-95`.

- **The fix is at the cause and complete: every caller and sibling was checked and stays consistent.**
  Cause: the narrow sum of the two block counts. The counts are now `int64_t` end to end
  (`nam_harness.cpp:56-57`), the sum is bounded before it is taken and the product is bounded before it
  is taken (`nam_harness.cpp:95-104`), and the count options are parsed without an exception escaping
  `main` (`nam_harness.cpp:65-89`, `nam_harness.cpp:480-503`). Callers of the widened values: the
  warmup loop (`nam_harness.cpp:711`), the timed loop (`nam_harness.cpp:728`), `blockUs.reserve()`
  (`nam_harness.cpp:721`), the measured-region offsets (`nam_harness.cpp:750-751`), the two report
  lines (`nam_harness.cpp:684-685`, `nam_harness.cpp:816-817`) and the CPU maths
  (`nam_harness.cpp:806-811`) - all of them wide, with the rendered output text unchanged. The other
  path into the same arithmetic, the `--wav-run` mode, never allocates from a block count and is
  untouched. Sibling tool in the same directory, `plugins/NeuralAmp/tests/nam_profile.cpp:115-132`,
  already parses its counts with a range-checked `strtol`; the harness now follows the same rule
  (checked parse, no throwing `std::stoi`). The frame count stays `int` on purpose: it is bounded by
  `NamModel::kMaxBlock` (8192) and is handed to `NamModel::process(..., int)`; it is parsed through
  the same checked parser, so it cannot truncate.

- **No collateral damage.** Only the headless test harness changed - no product code, no project
  format, no saved data, no plugin state. The new limit applies to the generated-signal mode only and
  rejects exactly what it did before (a run above 4,194,304 samples); the accepted range is
  unchanged (`--blocks 8192 --block 512` = exactly the limit is still accepted, `--blocks 0` and
  `--warmup -1` still produce their old messages). The only behaviour that changes is on input that
  used to abort the process: a count that is not an integer or does not fit the type is now a usage
  error with exit code 2, which is what the harness documents (`nam_harness.cpp:11`, "Exit code 0 = all
  checks passed, 1 = a check failed, 2 = usage/load error"). The other callers of the harness
  (`plugins/NeuralAmp/tests/verify-reference.sh:39` with `--blocks 2000 --warmup 20 --block 512`
  = 1,034,240 samples, and `plugins/NeuralAmp/tests/compare_rate_test.py:14`) stay well inside the
  limit and are unchanged. Out of scope, and left alone on purpose: `--sample-rate` still parses with
  `std::stod` (`nam_harness.cpp:504`), so a non-numeric rate still aborts; that option takes part in no
  count arithmetic, so it is not this finding.

- **If the verdict is ALREADY-FIXED or FALSE-POSITIVE: the code that shows it.** Not applicable - the
  verdict is FIXED. For the record, the narrow addition the finding quotes existed at the upstream base
  of this branch (`ded355440`, `plugins/NeuralAmp/tests/nam_harness.cpp:610`:
  `totalSamples = static_cast<int64_t>(opt.blockSize) * (opt.blocks + opt.warmupBlocks);`), and proof B
  above shows the regression failing against exactly that state.
