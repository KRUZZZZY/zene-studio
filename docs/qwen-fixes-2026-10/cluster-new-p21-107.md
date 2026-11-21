FIXED

Cluster new-p21-107 (Luna patrol BUG 2): the stem WAV writer computed the byte rate from an unchecked `int` sample rate before the cast to `quint32`. The fix is verified by a directly linked test binary; ctest registration of the new test is pending a parent reconfigure of the build tree (see "Open items").

## Verdict: REAL, fixed

- The arithmetic was `static_cast<quint32>(sampleRate * kWavChannels * (kWavBits / 8))` (historic line 104; `kWavChannels`, `kWavBits` are `int`, so the product is `int`). Above 268435455 the product overflows (undefined behaviour); a negative rate was written silently as a wrong header, and a rate whose byte rate exceeds 32 bits wrapped silently. The function is `LMMS_EXPORT` (`include/ControlStemSupport.h:201`), so any `int` reaches it.
- Current callers pass only sane rates: `src/core/ControlStemSupport.cpp:248` sets `StemModelSampleRate` (44100), used at `:374`; `src/gui/StemSplitController.cpp:241` passes a clip's `sample_rate_t` (uint32, `include/LmmsTypes.h:42`) converted to `int`. So the defect is in the writer's contract, not reachable with realistic inputs today. It is fixed at the writer, because that is where the value is validated.
- Behaviour change, deliberate: a zero rate from the GUI path (an in-memory clip whose source has rate 0; `tests/src/tracks/SampleClipWindowTest.cpp:588` builds such a buffer) now fails at write with a typed error instead of writing a WAV with a 0 Hz header. Clips with a stored file never reach the writer (`StemSplitController.cpp` `resolveSourceFile`).

## The fix

`src/core/ControlStemWav.cpp:77-91, 117`:
- `sampleRate <= 0`, or `sampleRate * 8` above `UINT32_MAX`, is refused before any file is opened (typed error, nothing written, an existing stem kept).
- The byte rate is computed in `quint64` (`byteRate`, line 88) and written as `static_cast<quint32>(byteRate)` (line 117).
- `dataBytes` uses the same `bytesPerFrame` (line 91); the value is unchanged.
- Accepted range: 1 to 536870911 (largest rate whose byte rate is 2^32 - 8). Callers stay inside it.

## Regression test

`tests/src/core/ControlStemWavTest.cpp` (new, registered in `tests/CMakeLists.txt:1455` and `tests/fork-sources.txt:2392`):
- `aValidRateWritesTheReferenceHeader` - 44100 Hz header, field by field (PASS before and after).
- `aZeroOrNegativeRateIsRefusedBeforeAnyWrite` - rates 0, -44100, INT_MIN are refused, and a pre-existing file is unchanged (FAIL before).
- `aByteRatePastThirtyTwoBitsIsRefusedBeforeAnyWrite` - rates 536870912 and INT_MAX are refused and no file is created (FAIL before).
- `theLargestRepresentableRateIsWritten` - 536870911 is accepted with byte rate 4294967288 (PASS before and after; guards against over-refusal).

Determinism: no clock, sleep or thread; inputs are fixed values in a `QTemporaryDir`.

## Evidence (exit codes as observed)

Logs: `~/.local/share/zene-bunny-fix/qwen/runs/new-p21-107.r1/logs/`

| Step | Command (summary) | EXIT | Log |
| --- | --- | --- | --- |
| Target lookup | `cmake --build build --target ControlStemWavTest -j2` | 2 ("No rule to make target") | build-1-red.log, build-2-red.log |
| Reconfigure attempt | `cmake -S . -B build` | 97 (bunny-guard: configuring is the parent's job) | configure-1.log |
| Moc of test | `manual_build.py moc` (recorded flags of StemQualityTest, cwd build/tests) | 0 | moc-test-red.log |
| Compile test | `manual_build.py compile` (test, recorded flags) | 0 | build-2-test-red.log |
| Compile writer, unfixed | `manual_build.py compile` (ControlStemWav.cpp, its own recorded flags) | 0 | build-3-wav-red.log |
| Link, first try | `manual_build.py link` | 1 (sibling's AUTOMOC object not built) | build-4-link-red.log |
| Link, RED | `manual_build.py link` (sibling AUTOMOC object dropped) | 0 | build-5-link-red.log |
| **RED run** | `ControlStemWavTest.red.bin` from build/tests | **2**: 4 passed, 2 failed (zero/negative rate; past-32-bit rate) | test-1-red.log |
| Compile writer, fixed | `manual_build.py compile` | 0 | build-6-wav-green.log |
| Link, GREEN | `manual_build.py link` | 0 | build-7-link-green.log |
| **GREEN run** | `ControlStemWavTest.green.bin` from build/tests | **0**: 6 passed, 0 failed (4 test functions + init/cleanup) | test-2-green.log |
| Mutation: save diff | `git diff -- src/core/ControlStemWav.cpp > fix.patch` | 0 | (fix.patch) |
| Mutation: revert | `git apply -R fix.patch` | 0 | |
| Compile/link reverted | `manual_build.py compile` / `link` | 0 / 0 | build-8, build-9 |
| **Reverted run** | `ControlStemWavTest.revert.bin` | **2**: 4 passed, 2 failed (same two tests) | test-3-revert.log |
| Reapply | `git apply fix.patch` | 0 | |
| Compile/link reapplied | `manual_build.py compile` / `link` | 0 / 0 | build-10, build-11 |
| **Reapplied run** | `ControlStemWavTest.reapplied.bin` | **0**: 6 passed | test-4-reapplied.log |
| ctest registration | `ctest -N -R ControlStemWavTest` (build/tests) | 0, **Total Tests: 0** | ctest-N-1.log |
| ctest family, registered | `ctest -N -R "ControlStem\|StemQuality\|StemSplit"` | 0, Total Tests: 5 (listed, not run) | ctest-N-2.log |
| feature_check direct | `python3 feature_check.py <worktree>` | 1 (PermissionError: its log folder `<worktree>/../…-feature-check` is outside the write-jail; no gate ran) | gate-feature-1.log |
| Gates, same definitions | `python3 gates/gate_runner.py <worktree> <logs>/gates` (imports feature_check.py, logs redirected) | **0**: fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity, file-length all PASS | gate-feature-2.log, gates/ |
| submit_check before commit | `python3 submit_check.py <worktree> new-p21-107` | 1 (nothing committed; report and self-check were missing) | submit-1.log |

Affected family: the ctest family `ControlStem|StemQuality|StemSplit` has 5 registered tests; **none were run**. Their binaries (the `zene` binary and the stem test objects) are the pre-fix build of this tree, and the build tree cannot be reconfigured or rebuilt from this lane, so a run would measure the old code. Not claimed as passing.

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it.** Yes: test-3-revert.log (exit 2, two FAIL lines at `ControlStemWavTest.cpp:126` and `:145`) vs test-4-reapplied.log (exit 0, 6 passed). The revert was the `git apply -R` of `fix.patch`, which is the single product file.
- **The test is deterministic.** No timing, sleep or thread. The inputs are fixed rates and frame values; files go to a `QTemporaryDir`; the existing-file check writes its own sentinel.
- **The fix is at the cause and complete.** `stemWriteWavFile` is the only WAV writer (`rg` over `src`, `include`, `tests`: callers are `ControlStemSupport.cpp:374` and `StemSplitController.cpp:241`). The validation sits before the `QSaveFile` open, so nothing is written on refusal. The block-align field `kWavChannels * (kWavBits / 8)` (line 116) is `int` but small and constant; left as is.
- **No collateral damage.** The accepted range (1 to 536870911) includes every rate a caller passes: 44100 (both control paths) and every real clip rate. Stem files already on disk are not read or changed. The one changed outcome outside the bug is a zero-rate in-memory GUI clip, which now gets a typed error instead of a 0 Hz header (see Verdict).
- **Verdict is FIXED, not ALREADY-FIXED or FALSE-POSITIVE:** the historic code (`git show HEAD:src/core/ControlStemWav.cpp`, line 104) has the unchecked int product, and the RED run shows the defect.

## Open items

1. **ctest registration (blocked):** `tests/CMakeLists.txt` now lists `ControlStemWavTest`, but the lane's build tree was configured before that, and the lane may not reconfigure (bunny-guard). `ctest -N` shows 0 tests for it. The parent must reconfigure the build tree (`cmake -S . -B build` from the parent) and then run `ctest -R ControlStemWavTest --output-on-failure` from `build/tests`. Expected result: the same 6 passing assertions as test-2-green.log. The family run is also the parent's.
2. **feature_check.py's log folder** lies outside this lane's write-jail, so the script exits 1 before running any gate. The gates were run through the same definitions with logs in the run folder (gate_runner.py, exit 0). The parent should re-run `feature_check.py` from an allowed location to confirm the table.
3. **Direct binaries:** the RED and GREEN binaries were linked by `logs/manual_build.py`, which reuses the recorded compile and link lines of `StemQualityTest` (only the sources and the outputs changed; the dependency-file flags were dropped so nothing under `build/` was written). This is how the regression was proved without touching the build tree.

## Files committed

- `src/core/ControlStemWav.cpp` (fix)
- `tests/src/core/ControlStemWavTest.cpp` (regression test, new)
- `tests/CMakeLists.txt` (registration, LMMS_TESTS)
- `tests/fork-sources.txt` (registration, new test file)
- `docs/qwen-fixes-2026-10/cluster-new-p21-107.md` (this report)
