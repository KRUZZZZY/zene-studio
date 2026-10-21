FIXED

# cluster new-p14-279: Finite cents can load as an infinite scale ratio

Finding (Luna patrol, BUG 5): the project-file validator accepted any finite cents value, but the interval holds its ratio as a 32-bit float computed as `exp2(cents / 1200)`. 200000 cents is a ratio of about 1e50 (infinite in float); -200000 cents is about 1e-50 (zero in float). Both loaded silently.

## Verdict and decision

REAL. Reproduced on the unfixed code before any change (RED below): `Scale::loadSettings` on a `<scale>` holding `cents="200000"` loaded two intervals, and the first one's ratio was not finite; `cents="-200000"` loaded a zero ratio.

## Evidence (file:line, on branch bugs/qwen-new-p14-279)

- `src/core/Scale.cpp:37-56` `hasValidIntervalData`: the cents branch used to return `validCents && std::isfinite(cents)`; it now returns `validCents && Interval::centsGiveUsableRatio(cents)` (line 47). This is the project-file validator the finding names; `Scale::loadSettings` calls it at line 177 before any interval is built.
- `src/core/Scale.cpp:61-65` new `Interval::centsGiveUsableRatio`: `ratio = std::exp2(cents / 1200.f)`, usable when `std::isfinite(ratio) && ratio > 0.f`. It computes exactly what the constructor (line 72) and `Interval::loadSettings` (line 123) compute, so the check and the stored ratio cannot disagree.
- `include/Scale.h:45-48` the static declaration, with the reason in the comment.
- `src/gui/MicrotunerConfig.cpp:336-339` `validateScaleForm`, the cents branch of the Microtuner scale dialog. Before the fix it only checked parseability, and `applyScale()` then built `Interval(firstSection.toFloat())` and set it on the Song. Now an out-of-range cents line is refused with a message before `applyScale()` can build it.
- Sibling checked and deliberately not changed: the `.scl` loader. `src/core/SessionTuningScala.cpp:90` still builds `Interval(cents)` with no range check, but an infinite ratio never reaches the session table: `SessionTuning::buildTable` (`src/core/SessionTuning.cpp:227`) calls `tableFrequenciesAreRenderable` (`:150-165`, the finite-frequency check at `:156`) and refuses. Evidence: the `.scl` test I first wrote (a `200000.0` line through `mts.load_scale`) PASSED on the unfixed code (`test-red-1.log`, `SessionTuningTest` passed), so it is not a regression for this fix and I removed it.
- Sibling checked: `Interval::loadSettings` cents path (`src/core/Scale.cpp:123`) still computes the ratio without a check. Its only caller is `Scale::loadSettings` (`src/core/Scale.cpp:180`), which validates first (line 177), so no unvalidated value reaches it (`grep restoreState` finds no other caller in `src/`).

## Regression test

`tests/src/core/ScaleIntervalOverflowTest.cpp` (existing file, the overflow family):
- `anOverflowingStoredCentsIntervalIsRefusedNotLoadedAsInfinite` (200000 cents + 3/2): every loaded ratio must be finite and positive; exactly the 3/2 loads.
- `anUnderflowingStoredCentsIntervalIsRefusedNotLoadedAsZero` (-200000 cents + 3/2): same, for zero.
- `aLargeStoredCentsIntervalWhoseRatioFitsStillLoads` (150000 cents, ratio about 4e37, finite): must still load. Passes before and after the fix; it guards against a too-wide limit.
- helper `requireUsableRatios` (deterministic: no timing, no randomness, inputs are the file's own text).

## Commands and exit codes

Build tool note: the first build attempt failed with `ccache: error: failed to create temporary file for /run/user/1000/ccache-tmp/...: Permission denied` (EXIT=2, `build-red-1.log`). Not a code result; every later build used `CCACHE_TEMPDIR=/tmp/ccache-tmp-p14-279`.

| Step | Command (run from the worktree / `build/tests` as noted) | EXIT | Log |
|---|---|---|---|
| RED build | `CCACHE_TEMPDIR=/tmp/ccache-tmp-p14-279 cmake --build build --target ScaleIntervalOverflowTest SessionTuningTest -j4` | 0 | build-red-2.log |
| RED test | `ctest -R '^(ScaleIntervalOverflowTest\|SessionTuningTest)$' --output-on-failure` (in `build/tests`) | 8 | test-red-1.log |
| GREEN build | `CCACHE_TEMPDIR=... cmake --build build --target zene ScaleIntervalOverflowTest SessionTuningTest ControlNoteScaleVerbsTest -j4` | 0 | build-green-1.log |
| GREEN test | `ctest -R '^(ScaleIntervalOverflowTest\|SessionTuningTest\|ControlNoteScaleVerbsTest)$' --output-on-failure` (in `build/tests`) | 0 | test-green-1.log |
| Proof: save fix | `git diff -- include/Scale.h src/core/Scale.cpp src/gui/MicrotunerConfig.cpp > fix.patch` | 0 | (logs dir) fix.patch |
| Proof: revert fix | `git apply -R fix.patch` | 0 | |
| Proof: build without fix | `CCACHE_TEMPDIR=... cmake --build build --target zene ScaleIntervalOverflowTest -j4` | 0 | build-proof-red-2.log |
| Proof: test without fix | `ctest -R '^ScaleIntervalOverflowTest$' --output-on-failure` (in `build/tests`) | 8 | test-proof-red-2.log |
| Proof: reapply fix | `git apply fix.patch` | 0 | |
| Proof: build with fix | `CCACHE_TEMPDIR=... cmake --build build --target zene ScaleIntervalOverflowTest SessionTuningTest ControlNoteScaleVerbsTest -j4` | 0 | build-proof-green-2.log |
| Proof: test with fix | `ctest -R '^(ScaleIntervalOverflowTest\|SessionTuningTest\|ControlNoteScaleVerbsTest)$' --output-on-failure` (in `build/tests`) | 0 | test-proof-green-2.log |

RED result (test-red-1.log, unfixed code): `ScaleIntervalOverflowTest` FAILED two tests: `anOverflowingStoredCentsIntervalIsRefusedNotLoadedAsInfinite` (`a loaded interval holds a ratio that is not finite and positive`; then `Actual 2, Expected 1`) and `anUnderflowingStoredCentsIntervalIsRefusedNotLoadedAsZero` (same). The third new test passed, as intended.

Proof result (test-proof-red-2.log, reverted fix): the same two tests FAILED, `0% tests passed`. After `git apply fix.patch` (test-proof-green-2.log): `100% tests passed out of 3`.

Test count (test-count-1.log, `ctest -R` family with `-V`): 3 tests executed; QTest totals `ScaleIntervalOverflowTest` 10 passed / 0 failed (includes init and cleanup), `SessionTuningTest` 24 passed / 0 failed, `ControlNoteScaleVerbsTest` 22 passed / 0 failed. Zero tests were not run.

Gates. `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` was run and EXITED 1 before any gate ran (`gates-1.log`): it tries `mkdir` on `<worktree>/../zene-qwen-new-p14-279-feature-check`, which is outside the write-jail (`PermissionError`). It also keeps its ratchet baseline cache under the shared `qwen/` folder. I did not work around it. Instead I ran each gate's own command from the worktree with the same arguments and logged each to the LOG FOLDER (`gate-*-1.log`):

| Gate | Command | EXIT | Note |
|---|---|---|---|
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 | |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 | |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 | `Scale.cpp`, `MicrotunerConfig.cpp` already registered; `include/Scale.h` added this commit |
| check-namespace | `tests/scripted/check-namespace` | 0 | |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 | |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 | `gate-doc-figures-1.log`, run after this report was written |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check` | 1 | 5 REGRESSION lines, all in files this change does not touch (`XyPadPairTest.cpp`, `DawProjectReadTracks.cpp`, `ControlCommandsMixerTools.cpp`, `ControlCommandsMixerStrip.cpp`, `ControlCommandsShortcuts.cpp`). The feature check rule fails only on items in changed files, so this row passes by that rule. |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check` | 1 | 14 REGRESSION lines, none in changed files (`TakeLane.cpp`, several `tests/src/core/*Test.cpp`). Same rule: passes. |

## Self-check

- **Regression fails with the fix reverted and passes with it:** yes. Exit codes above: RED 8 (`test-red-1.log`), proof-revert 8 (`test-proof-red-2.log`), proof-restore 0 (`test-proof-green-2.log`). The `.scl` test I first wrote passed without the fix, so I removed it; it is not claimed as regression proof.
- **Deterministic:** yes. No timing, sleeps, or races. Inputs are the file's own cents text; `requireUsableRatios` is a pure predicate over the loaded intervals.
- **Fix at the cause and complete:** the cause is that `hasValidIntervalData` accepted a cents value whose ratio the interval cannot hold. The same predicate is now the one rule everywhere a user-entered cents value becomes an `Interval`: project load (`Scale.cpp:47`) and the Microtuner dialog (`MicrotunerConfig.cpp:339`). The `.scl` loader is refused downstream by `SessionTuning::buildTable` (`SessionTuning.cpp:227`, `:156`), so I left it unchanged (evidence above). `Interval::loadSettings` is reachable only after validation (`Scale.cpp:177-180`).
- **Gap, stated plainly: the Microtuner dialog change has no runtime test.** `validateScaleForm` reports failures through a modal `QMessageBox::critical`, which blocks under `QT_QPA_PLATFORM=offscreen`, so a test cannot drive it. Coverage for that wiring is: the `zene` target builds (`build-green-1.log`, EXIT 0) and the shared predicate is tested.
- **No collateral damage:** cents values whose ratio is finite and positive are unchanged (`aLargeStoredCentsIntervalWhoseRatioFitsStillLoads`, the `ControlNoteScaleVerbsTest` and `SessionTuningTest` families all pass). Behaviour change, deliberate: a project that already holds an out-of-range cents interval loads without that interval (the same treatment as other malformed intervals) instead of loading an infinite or zero ratio. A reviewer should decide whether that is acceptable for such files; I think it is, since the stored value cannot produce a usable tuning.
- **Realtime:** no audio-thread code is touched. `centsGiveUsableRatio` runs only in project load and dialog validation.
- **Undo and commands:** no command is added or changed; no undo entry is involved. The dialog refuses the scale before it is set on the Song, so no undo step is created for an out-of-range value.
- **Old projects and saved data:** unchanged for every value that was already usable. Saved files are not rewritten.
- **Portability:** only `std::exp2` and `std::isfinite`, already used in this file. No new Qt API.
- **Registration:** `tests/upstream-modifications.txt` gained an `include/Scale.h` line with its reason; `src/core/Scale.cpp` and `src/gui/MicrotunerConfig.cpp` were already registered. `tests/fork-sources.txt` gained this report's path. No new test file. `ScaleIntervalOverflowTest.cpp` was already registered.
- **Gates:** the wrapper script could not run (write-jail, above). All gates were run directly; all required gates pass; the two ratchets exit 1 with regression lines in files this change does not touch.
- **Sibling commands in the command files:** the diff touches no `ControlCommands*` file, so no command sibling applies. No control command was added (the commands snapshot is unaffected).
- **Working tree:** clean after the commit.
