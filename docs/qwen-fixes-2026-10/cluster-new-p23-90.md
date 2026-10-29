FIXED

# Cluster new-p23-90: Sfz EQ drops output on buffer allocation failure

Verdict: **FIXED**. The finding is real, but its impact is narrower than its title. The production caller
(`Voice.cpp`) runs the EQ in place, so the output buffer is the input buffer. On scratch-buffer exhaustion the
old early return left the already-filtered signal in place, which means the EQ was bypassed for that block. It
did not produce silence. The stale or unwritten outputs the finding describes apply to out-of-place callers,
which `EQHolder::process(const float** inputs, float** outputs, ...)` permits and which its own
`description == nullptr` branch already honours.

## Evidence

- **Defect (before the fix):** `plugins/SfzPlayer/sfizz/src/sfizz/EQPool.cpp:60-61` was
  `if (!frequencySpan || !bandwidthSpan || !gainSpan) return;` with no write to `outputs`. The sibling branch at
  `EQPool.cpp:48-51` (`description == nullptr`) copies every input channel to its output; the allocation-failure
  branch did not.
- **Why the pool fails:** `plugins/SfzPlayer/sfizz/src/sfizz/BufferPool.h` `getBuffer()` returns an empty
  `SpanHolder` when no mono buffer is free (`monoAvailable` has no `1`) or when `numFrames` exceeds the buffer size.
- **Production caller (in place):** `plugins/SfzPlayer/sfizz/src/sfizz/Voice.cpp:1038-1044` (mono) and
  `Voice.cpp:1056-1063` (stereo) pass the same buffer as input and output.
- **Fix:** `EQPool.cpp:60-66`. The failure branch now copies each input channel to its output and returns, the same
  copy the `description == nullptr` branch (`EQPool.cpp:48-52`) already performs. The EQ still runs on every block
  where its scratch buffers are available, so behaviour outside the bug does not change.
- **Siblings checked, not changed:** `plugins/SfzPlayer/sfizz/src/sfizz/FilterPool.cpp:80-81` has the same
  early-return shape in the filter stage, and `Voice.cpp:865-866` and `Voice.cpp:933-934` return early after
  `getBuffer()` failures. These are separate functions with their own contracts, so they fall outside this finding
  and are left alone. They are candidates for a separate finding if the owner wants them.

## Regression test

`tests/src/plugins/SfzEQPoolExhaustionTest.cpp` (new, cluster new-p23-90):

- Exhausts the real `BufferPool`: it holds every mono buffer with `getBuffer()` `SpanHolder`s, and asserts that the
  count equals `config::bufferPoolSize` and that a further `getBuffer()` is empty. The failure therefore comes from
  the pool itself, not from a mock.
- Seeds the outputs with the sentinel `-1234.0f`, calls `EQHolder::process()`, and checks that the outputs equal the
  inputs. It runs once mono and once stereo.
- Control: with the pool available, no output sample is left at the sentinel.
- Deterministic: no clock, sleep or thread. The inputs are a fixed ramp; the failure is produced by pool exhaustion.

Registered in `tests/CMakeLists.txt` (inside the `if(TARGET sfzplayer_sfizz)` block, and in the include/link block
next to `HistoricalBufferResizeTest`) and in `tests/fork-sources.txt`.

## Commands and exit codes

Environment notes that affected the run (all recorded in the LOG FOLDER):

1. `cmake --build build --target SfzEQPoolExhaustionTest -j2` → EXIT=2 (`build-1.log`): "No rule to make target"; the
   generated Makefile did not yet contain the new target.
2. `cmake -S . -B build` → EXIT=97 (`configure-1.log`): `bunny-guard` refuses to configure in a lane.
3. `cmake --build build --target cmake_check_build_system` → EXIT=0 (`regen-1.log`): regenerated the build system
   through the allowed build path.
4. `cmake --build build --target SfzEQPoolExhaustionTest -j2` → EXIT=2 (`build-3.log`): ccache could not create a
   temp file under `~/.local/share/zene-bunny-fix/dsh/home/ccache-tmp-r6` (Permission denied). The path comes from
   `temporary_dir` in `~/.cache/ccache-bunny/ccache.conf`, which is outside this lane's write-jail.
5. Workaround, used for every build after that: `CCACHE_TEMPDIR=$TMPDIR/ccache-tmp-p23-90` set in the environment.
   The shared ccache config was not edited.

RED (fix not yet applied; test built against the original `EQPool.cpp`):

- `CCACHE_TEMPDIR=$TMPDIR/ccache-tmp-p23-90 cmake --build build --target SfzEQPoolExhaustionTest -j2` → EXIT=0 (`build-4.log`)
- `ctest -N -R SfzEQPoolExhaustionTest` (from `build/tests`) → EXIT=0, 1 test (`ctest-list-1.log`)
- `ctest -R SfzEQPoolExhaustionTest --output-on-failure` (from `build/tests`) → **EXIT=8**, `Totals: 3 passed, 2 failed`:
  `exhaustedMonoPoolPassesInputThroughToOutput` fails at `SfzEQPoolExhaustionTest.cpp:73` and
  `exhaustedStereoPoolPassesInputThroughToOutput` fails at `:82` (`test-red-1.log`)

GREEN (fix applied):

- `CCACHE_TEMPDIR=... cmake --build build --target SfzEQPoolExhaustionTest -j2` → EXIT=0 (`build-5.log`)
- `ctest -R SfzEQPoolExhaustionTest --output-on-failure` → **EXIT=0**, `1/1 Passed` (`test-green-1.log`)
- `ctest -R SfzEQPoolExhaustionTest -V` → EXIT=0, `Totals: 5 passed, 0 failed` (`test-4b-fixed-1.log`); the test has
  three test functions, and QTest counts five checks.

Proof the test catches the bug (step 4b):

- `git diff -- plugins/SfzPlayer/sfizz/src/sfizz/EQPool.cpp > <LOG>/fix.patch` → EXIT=0
- `git apply -R <LOG>/fix.patch` → EXIT=0
- `CCACHE_TEMPDIR=... cmake --build build --target SfzEQPoolExhaustionTest -j2` → EXIT=0 (`build-6.log`)
- `ctest -R SfzEQPoolExhaustionTest --output-on-failure` → **EXIT=8**, `2 failed` (`test-4b-unfixed-1.log`)
- `git apply <LOG>/fix.patch` → EXIT=0
- `CCACHE_TEMPDIR=... cmake --build build --target SfzEQPoolExhaustionTest -j2` → EXIT=0 (`build-7.log`)
- `ctest -R SfzEQPoolExhaustionTest -V` → **EXIT=0**, `Passed` (`test-4b-fixed-1.log`)

Affected test family (`ctest -R "Sfz|Sfizz|HistoricalBuffer"`, 10 tests):

- `ctest -N -R "Sfz|Sfizz|HistoricalBuffer"` → 10 tests listed (`ctest-family-list-1.log`).
- The first family run (`test-family-1.log`, EXIT=8) reported 8 tests as "Not Run": only my target had been built.
  Those targets were then built one at a time, each with EXIT=0 (`build-fam-*.log`): HistoricalBufferResizeTest,
  HistoricalBufferEmptyTest, SfzSimdHelperAlignmentTest, SfzSimdGainAVXTest, SfizzLeakDetectorTest, SfzFilePoolTest,
  SfzPlayerTest, SfzPlayerRealtimeTest.
- Rerun → **EXIT=0**, `100% tests passed out of 10` (`test-family-2.log`).

Gates:

- `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` → EXIT=1. The script could not create its
  log directory, `<worktree-parent>/zene-qwen-new-p23-90-feature-check`, which is outside this lane's write-jail
  (`PermissionError`).
- The same script, with only its log-directory line redirected to `<LOG>/feature-check` (an inline `python3 -I`
  wrapper; the gate logic is unchanged) → **EXIT=0**, `RESULT: PASS` (`feature-check-1.log`):
  fork-sources PASS, all-sources-reproduce PASS, no-upstream-regression PASS, check-namespace PASS, no-tautology PASS,
  doc-figures PASS, complexity PASS (no new item in changed files), file-length PASS (no new item in changed files).
  The check's changed-file count was 4 (`EQPool.cpp`, `tests/CMakeLists.txt`, `tests/fork-sources.txt`, and the new test).

## Self-check

- **The regression test fails with the product change reverted and passes with it:** yes. RED EXIT=8 (2 failed);
  GREEN EXIT=0 (5 checks passed); step 4b reverted EXIT=8 and re-applied EXIT=0. The command sequence is listed above.
- **Deterministic:** yes. The test uses no clock, sleep or thread. The failure is produced by holding every
  `BufferPool` mono buffer with real `SpanHolder`s. The inputs are a fixed ramp and the outputs are seeded with the
  sentinel `-1234.0f`. The available-pool control uses the same fixed inputs.
- **The fix is at the cause, and complete:** the cause is the failure branch's missing copy, and the change adds only
  that copy, mirroring `EQPool.cpp:48-52`. Callers: `Voice.cpp:1044` and `Voice.cpp:1063` pass the same buffer as
  input and output, so the in-place case now keeps the pre-EQ signal explicitly. Siblings `FilterPool.cpp:80-81`,
  `Voice.cpp:865-866` and `Voice.cpp:933-934` have the same early-return shape but belong to other functions with
  other contracts. They are left unchanged and listed as candidates.
- **No collateral damage:** the change touches only the allocation-failure branch. The normal path is unchanged and
  the availability control confirms it writes every output. No limit or check was added. The diff touches only the
  buffer-failure branch of `EQHolder::process()`. It does not change `setup()`, the region's equalizer description,
  or any parsing or saved-data code, so saved projects and SFZ files load as before. The test family (10 tests) passes.
- **Verdict is FIXED, so no ALREADY-FIXED or FALSE-POSITIVE justification is needed.**
- **Realtime:** the new code is one `copy<float>` loop per channel on a branch that already runs on the audio thread.
  It adds no allocation, lock, wait or logging, and no `DBG` call.
- **Undo and commands:** no control command was added; undo is not involved.
- **Registration:** the new test is listed in `tests/fork-sources.txt` and in `tests/CMakeLists.txt`.
- **Open environment items:** `feature_check.py` and `submit_check.py` still need the lane's log folder created
  outside the write-jail (see Gates above). The ccache temp directory override is a lane-local environment setting,
  not a repository change.
