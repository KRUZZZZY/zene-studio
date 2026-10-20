# Cluster new-p10-271-port

**Verdict: FIXED**

The finding was real against the upstream base of this branch and is fixed at the cause, end to end.

- Finding: `plugins/NeuralAmp/tests/nam_harness.cpp` added the generated block count
  (`opt.blocks + opt.warmupBlocks`) as a narrow `int` before widening it, so the sum could
  overflow and turn into a huge (or negative) allocation request.
- Upstream base (`ded355440`, the state the finding was written against) still has that
  narrow addition at `plugins/NeuralAmp/tests/nam_harness.cpp:610`
  (`totalSamples = static_cast<int64_t>(opt.blockSize) * (opt.blocks + opt.warmupBlocks);`).
  The regression below fails against exactly that state, with the finding's own symptom
  (`std::length_error: cannot create std::vector larger than max_size()`).
- The first half of the fix (wide addition + a 4,194,304-sample limit) is already on this
  branch in commit `68ad344a0`; commit `4b4c05418` completed the family (64-bit counts, a
  non-throwing count parser, a bounded sample plan). This round fixes the **proof**, not the
  behaviour: see "Why the regression was rebuilt" below.

## What changed

`plugins/NeuralAmp/tests/nam_harness.cpp` (product, unchanged since `4b4c05418`)

| Location | Change |
|---|---|
| `nam_harness.cpp:56-57` | `Options::blocks` and `Options::warmupBlocks` are `int64_t`, so the count never lives in a 32-bit type. |
| `nam_harness.cpp:65-74` | `parseInt64Option()` — `strtoll` + `ERANGE` + full-consumption check; no exception can escape `main`. |
| `nam_harness.cpp:78-89` | `parseIntOption()` — the same, range-checked against `int` (used for the frame count). |
| `nam_harness.cpp:95-104` | `planGeneratedSamples()` — bounds both counts *before* adding them, so the sum (`<= 2 * limit`) and the product (checked against `limit / blockSize` before multiplying) cannot overflow; a run over the limit is refused before any buffer exists. |
| `nam_harness.cpp:480-503` | `--block`, `--blocks` and `--warmup` are parsed with the checked parsers; a malformed or out-of-range value is a usage error (exit 2), not an abort. |
| `nam_harness.cpp:546-552` | The generated-run plan replaces the inline `int` addition; the rejection message and exit code are unchanged. |
| `nam_harness.cpp:684-685, 711, 721, 728, 750-751, 816-817` | Every consumer of the widened counts was made consistent: `int64_t` loop counters, `%lld` formats (rendered text unchanged), explicit `size_t` cast for `blockUs.reserve()`, and the sample offsets are now wide products of two 64-bit operands. |

`tests/src/plugins/NeuralAmpHarnessBlockCount.cpp` (new regression, ctest
`NeuralAmpHarnessBlockCount`)

The test drives the real harness binary through `QProcess` and asserts, for each case, that
the harness exits normally with code 2 and reports the expected message: the reported case
(`--blocks 2147483647`), both counts at `INT_MAX` (the sum alone overflows a 32-bit
addition), a count past the narrow type (`--blocks 2147483648`), counts that are not numbers
(`--blocks not-a-number`, `--blocks 99999999999999999999`, `--warmup many`), the same for
`--block`, and the pre-existing range messages (`--blocks 0`, `--warmup -1`,
`--block 99999`). A final slot runs a small legitimate generated run to prove the harness
still works.

`tests/CMakeLists.txt:5706-5726` — the ctest is now the C++ test target, and the target
**depends on `nam_harness`**:

```cmake
add_dependencies(NeuralAmpHarnessBlockCount nam_harness)
```

`tests/fork-sources.txt`, `tests/all-sources.txt`, `tests/upstream-modifications.txt` — the
new test source is registered in both scope manifests (and in the `fork-sources.txt` recipe
pathspec, so the manifest stays reproducible), and the ledger reason for the inherited harness
file names the new regression.

`tests/neuralamp-harness-block-count.py` — deleted. It was the Python driver of the same
cases; it is superseded by the C++ test (same ctest name, strictly more cases).

## Why the regression was rebuilt (the round-5 review finding)

The automated negative control reverted the product file, rebuilt and ran the test, and the
test **passed** - so it proved nothing. The reason is in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-271-port.r5/negctl/`:

- `build-reverted.log` shows that the reverted build compiled **`zene` only**. The harness is
  a separate `EXCLUDE_FROM_ALL` target (`plugins/NeuralAmp/tests/CMakeLists.txt:38`), so
  `nam_harness` was **not** rebuilt and the test ran against the stale, already-fixed binary.
- The Python driver could not express the missing dependency: it is a `ctest` command, not a
  build target, so nothing made the harness rebuild with it.

The fix is structural: the regression is now a **build target of its own**
(`NeuralAmpHarnessBlockCount`, source `tests/src/plugins/NeuralAmpHarnessBlockCount.cpp`)
that `add_dependencies(... nam_harness)`. Building the test therefore always rebuilds the
harness it exercises, and reverting the product file changes the binary under test. The
proof below runs the automated control's exact commands and the test now fails without the
product change and passes with it.

## Evidence

Working directory is the repository root unless noted. Log folder:
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-271-port.r5/logs`.

| Phase | Exact command | Exit |
|---|---|---:|
| Build the new test target (first build) | `cmake --build build --target NeuralAmpHarnessBlockCount -j 8 > .../logs/build-6.log 2>&1` | 0 |
| GREEN with the fix | `cd build/tests && ctest -R "^NeuralAmpHarnessBlockCount$" --output-on-failure --timeout 600 > .../logs/test-6.log 2>&1` (1/1 passed) | 0 |
| **Product patch for the control** | `git diff ded355440 HEAD -- plugins/NeuralAmp/tests/nam_harness.cpp > .../logs/product.patch` | 0 |
| **Revert the product change** | `git apply -R .../logs/product.patch` | 0 |
| **Reverted build (the control's build step: `zene` + the test target)** | `cmake --build build --target zene NeuralAmpHarnessBlockCount -- -j4 > .../logs/build-red-d.log 2>&1` (rebuilds `nam_harness` too) | 0 |
| **RED without the product change** | `cd build/tests && ctest -R "^NeuralAmpHarnessBlockCount$" --output-on-failure --timeout 600 > .../logs/test-red-d.log 2>&1` -> `FAIL! ... rejectsTheMaximumBlockCount() ... exit=6 status=crashed stderr=terminate called after throwing an instance of 'std::length_error' what(): cannot create std::vector larger than max_size()` (6 of 10 slots failed) | **8** |
| Restore the product change | `git apply .../logs/product.patch` | 0 |
| Restored build (same command as the reverted build) | `cmake --build build --target zene NeuralAmpHarnessBlockCount -- -j4 > .../logs/build-green-d.log 2>&1` | 0 |
| **GREEN with the product change** | `cd build/tests && ctest -R "^NeuralAmpHarnessBlockCount$" --output-on-failure --timeout 600 > .../logs/test-green-d.log 2>&1` (1/1 passed) | **0** |
| Build the family test executables | `cmake --build build --target NeuralAmpModelLoadTest NeuralAmpWavInputTest NamModelMonoOutputTest NeuralAmpProfileArgumentsTest NamModelPrewarmTest NamespaceRegistryTest TrackKeyNamesLoadTest -j 8 > .../logs/build-tests-2.log 2>&1` | 0 |
| **Family run (18/18 passed)** | `cd build/tests && ctest -R "NeuralAmp\|NamLoader\|Nam" --output-on-failure > .../logs/test-family-4.log 2>&1` | **0** |
| Gates | `python3 .../feature_check.py . > .../logs/feature-check-4.log 2>&1` -> `RESULT: PASS` | 0 |
| Ratchet gate 4 | `bash tests/complexity-gate.sh --check > .../logs/gate-complexity-2.log 2>&1` (6 pre-existing regressions, none in my files; identical list before this change) | 1 |
| Ratchet gate 7 | `bash tests/file-length-gate.sh --check > .../logs/gate-file-length-2.log 2>&1` (14 pre-existing regressions, none in my files; identical list before this change) | 1 |
| Ratchet gate 9 | `bash tests/fork-sources-gate.sh > .../logs/gate-fork-sources-2.log 2>&1` | 0 |

Affected-family test count: **18/18** (`ctest -R "NeuralAmp|NamLoader|Nam"`, `.../logs/test-family-4.log`),
of which `NeuralAmpHarnessBlockCount` is the regression and `NeuralAmpWavRunSampleRateWarning`,
`NeuralAmpCompareRateTest`, `NeuralAmpWavInputTest`, `NeuralAmpModelLoadTest`,
`NeuralAmpProfileArgumentsTest`, `NamModelMonoOutputTest`, `NamModelPrewarmTest` and the eight
`NamLoaderTest_*` cases are the rest.

Gates 4 and 7 are red on this train branch before this change as well; `grep
NeuralAmpHarnessBlockCount` over both gate logs finds nothing, and `feature_check.py` reports
`complexity: no new item in your files` and `file-length: no new item in your files`.

## Files in this commit

- `tests/src/plugins/NeuralAmpHarnessBlockCount.cpp` (new regression)
- `tests/CMakeLists.txt` (register the C++ test target and its `nam_harness` dependency)
- `tests/neuralamp-harness-block-count.py` (deleted: superseded by the C++ test)
- `tests/fork-sources.txt`, `tests/all-sources.txt` (scope manifests for the new test source)
- `tests/upstream-modifications.txt` (ledger reason names the new regression)
- `docs/qwen-fixes-2026-10/cluster-new-p10-271-port.md` (this report)

The product change itself is in `plugins/NeuralAmp/tests/nam_harness.cpp`, committed in
`4b4c05418` (this branch's previous commit) and unchanged here.

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it (exit codes).**
  The control's own commands: `git diff ded355440 HEAD -- plugins/NeuralAmp/tests/nam_harness.cpp >
  .../logs/product.patch` (EXIT=0), `git apply -R .../logs/product.patch` (EXIT=0),
  `cmake --build build --target zene NeuralAmpHarnessBlockCount -- -j4` (EXIT=0; the test
  target's `nam_harness` dependency rebuilds the reverted harness),
  `ctest -R "^NeuralAmpHarnessBlockCount$" --output-on-failure --timeout 600` -> **EXIT=8**,
  6 of 10 slots failed, e.g. `rejectsTheMaximumBlockCount()` with
  `exit=6 status=crashed stderr=terminate called after throwing an instance of
  'std::length_error' what(): cannot create std::vector larger than max_size()`
  (`.../logs/test-red-d.log`). Then `git apply .../logs/product.patch` (EXIT=0), the same build
  (EXIT=0), the same ctest -> **EXIT=0**, 1/1 passed (`.../logs/test-green-d.log`). The earlier
  round's proof (`.../logs/test-red-a.log`, `.../logs/test-green-a.log`) used a Python driver
  that could not make the harness rebuild; that is the gap this round closes, and the RED run
  above is a runtime failure of the rebuilt binary, not a build failure and not a stale one.

- **The test is deterministic: no wall-clock timing, sleeps or races.** The seam is the harness's
  own command-line contract: the test writes a fixed minimal WaveNet `.nam` fixture into a
  `QTemporaryDir` and runs the harness with literal `--block/--blocks/--warmup` values, then
  asserts the process exit status, the exit code and the stderr text. No clock, no sleep, no
  thread, no random input. Every literal is in
  `tests/src/plugins/NeuralAmpHarnessBlockCount.cpp:81-196`.

- **The fix is at the cause and complete: every caller and sibling was checked and stays consistent.**
  Cause: the narrow sum of the two block counts. The counts are `int64_t` end to end
  (`nam_harness.cpp:56-57`), the sum is bounded before it is taken and the product is bounded
  before it is taken (`nam_harness.cpp:95-104`), and the count options are parsed without an
  exception escaping `main` (`nam_harness.cpp:65-89`, `nam_harness.cpp:480-503`). Callers of the
  widened values: the warmup loop (`nam_harness.cpp:711`), the timed loop
  (`nam_harness.cpp:728`), `blockUs.reserve()` (`nam_harness.cpp:721`), the measured-region
  offsets (`nam_harness.cpp:750-751`), the two report lines (`nam_harness.cpp:684-685`,
  `nam_harness.cpp:816-817`) and the CPU maths (`nam_harness.cpp:806-811`) - all wide, with the
  rendered output text unchanged. The other path into the same arithmetic, the `--wav-run` mode,
  never allocates from a block count and is untouched. Sibling tool in the same directory,
  `plugins/NeuralAmp/tests/nam_profile.cpp:115-132`, already parses its counts with a
  range-checked `strtol`; the harness now follows the same rule (checked parse, no throwing
  `std::stoi`). The frame count stays `int` on purpose: it is bounded by `NamModel::kMaxBlock`
  (8192) and is handed to `NamModel::process(..., int)`; it is parsed through the same checked
  parser, so it cannot truncate.

- **No collateral damage.** Only the headless test harness and its regression changed - no
  product code, no project format, no saved data, no plugin state. The new limit applies to the
  generated-signal mode only and rejects exactly what it did before (a run above 4,194,304
  samples); the accepted range is unchanged (`--blocks 8192 --block 512` = exactly the limit is
  still accepted, `--blocks 0` and `--warmup -1` still produce their old messages, and the test
  asserts those messages). The only behaviour that changes is on input that used to abort the
  process: a count that is not an integer or does not fit the type is now a usage error with
  exit code 2, which is what the harness documents (`nam_harness.cpp:11`, "Exit code 0 = all
  checks passed, 1 = a check failed, 2 = usage/load error"). The other callers of the harness
  (`plugins/NeuralAmp/tests/verify-reference.sh:39` with `--blocks 2000 --warmup 20 --block 512`
  = 1,034,240 samples, and `plugins/NeuralAmp/tests/compare_rate_test.py:14`) stay well inside
  the limit and are unchanged. The Python driver was deleted, not weakened: the C++ test
  covers every case it covered (the reported case, the oversized run, the small run) plus the
  malformed and out-of-range counts, and it runs the same binary. Out of scope, and left alone
  on purpose: `--sample-rate` still parses with `std::stod` (`nam_harness.cpp:504`), so a
  non-numeric rate still aborts; that option takes part in no count arithmetic, so it is not
  this finding.

- **If the verdict is ALREADY-FIXED or FALSE-POSITIVE: the code that shows it.** Not applicable -
  the verdict is FIXED. For the record, the narrow addition the finding quotes existed at the
  upstream base of this branch (`ded355440`, `plugins/NeuralAmp/tests/nam_harness.cpp:610`),
  and the RED run above shows the regression failing against exactly that state.
