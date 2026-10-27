ALREADY-FIXED

Cluster new-p16-134: "NaN surround panner settings can produce NaN audio" (`src/core/MixerSurround.cpp:418` in the finding; that line is now the `matrix()` index clamp, which cannot produce NaN, so the parser is the real subject).

## Evidence

- The parsing helper the finding describes now rejects non-finite values. `src/core/MixerSurround.cpp:284-291`: `attribute()` returns the fallback unless `ok && std::isfinite(value)`, with the comment naming NaN and overflow to infinity. This came in with commit `3a8724211` (cluster new-p10-257), which is an ancestor of this branch: `git merge-base --is-ancestor 3a8724211 HEAD` exits 0.
- Every panner attribute on the project-load path goes through that helper. `ChannelSurround::load()` at `src/core/MixerSurround.cpp:513-528` reads `x` (523), `y` (524), `lfe` (525) and `spread` (526) with `surround::attribute()`, so `spread="nan"` loads the default `spread` (0), not NaN.
- `panGains()` (`src/core/MixerSurround.cpp:400-414`) therefore receives finite values from the project-load path. Its own inputs are still clamped with `std::clamp`, which passes NaN through, so the guard is the only protection; the regression below exercises exactly that.
- Other entry points into the panner were checked as siblings. The control command `readPan()` (`src/core/ControlCommandsMixerSurround.cpp:174-191`) rejects a value unless `inRange()` (`:168-170`) holds, and `value >= low && value <= high` is false for NaN and infinity. The downmix reader `loadMixer()` (`src/core/MixerSurround.cpp:445-458`) already uses the same helper for `centre`, `surround` and `lfe`, and its regression is `ProjectStateTest::nonFiniteDownmixSettingsAreIgnored` (`tests/src/core/ProjectStateTest.cpp:279`).
- Before this change the panner attributes had no regression. That is what the new test covers.

## Change

Only the test is new: `tests/src/core/ProjectStateTest.cpp:301-329`, `nonFinitePannerAttributesFallBackToTheDefaults()`, next to the downmix NaN case. It builds a `<surround format="5.1" pan="1" x="nan" y="inf" lfe="-inf" spread="nan"/>` element, calls `ChannelSurround::load()` on a standalone `ChannelSurround` with `init(64)` (no audio engine, no clock), and checks that the panner is enabled, that x, y, lfe and spread equal the `PanPosition{}` defaults, and that every gain of every one of the `FormatCount` formats is finite. No production code was changed.

Placement: the first version of this test (commit `ff1ba3a9b`) lived in `tests/src/core/SurroundTest.cpp`. That file was 499 code lines at its base commit `639e1d23e`, counted with the gate's own rule (the awk in `tests/file-length-gate.sh` `code_lines`, applied to `git show 639e1d23e:tests/src/core/SurroundTest.cpp`). The test added about 27 code lines, which took the file to 526, over the 500 limit; the gate then reported `file-length: NEW: new file over 500 lines: tests/src/core/SurroundTest.cpp (526)`. This change moves the test to `ProjectStateTest.cpp` (376 code lines by the same count, under the limit) and restores `SurroundTest.cpp` to its base content (499 code lines). The gate baseline and limit were not changed.

## Commands and exit codes

Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-134.r2/logs/`.

| Step | Command | EXIT |
| --- | --- | --- |
| Build zene, ProjectStateTest, SurroundTest with the fix present | `CCACHE_TEMPDIR=$TMPDIR cmake --build build --target zene ProjectStateTest SurroundTest -j4 > .../build-1-fix-present.log 2>&1` | 0 |
| New test with the fix present | `cd build/tests && QT_QPA_PLATFORM=offscreen ./ProjectStateTest nonFinitePannerAttributesFallBackToTheDefaults > .../test-1-fix-present.log 2>&1` | 0 (3 passed, 0 failed) |
| Mutation: remove the finiteness check. `git diff -- src/core/MixerSurround.cpp > .../fix.patch` taken first; the edit changes `ok && std::isfinite(value) ? ...` to `ok ? ...` | `git diff -- src/core/MixerSurround.cpp > .../fix.patch` | 0 |
| Rebuild with the mutation | `CCACHE_TEMPDIR=$TMPDIR cmake --build build --target zene ProjectStateTest -j4 > .../build-2-mutant.log 2>&1` | 0 |
| RED: new test with the guard removed | `cd build/tests && QT_QPA_PLATFORM=offscreen ./ProjectStateTest nonFinitePannerAttributesFallBackToTheDefaults > .../test-2-red-mutant.log 2>&1` | 1 (`FAIL! ... Compared floats are not the same` at `ProjectStateTest.cpp:321`, the `x` comparison; 2 passed, 1 failed) |
| Restore: `git apply -R .../fix.patch` (reverts the mutation) | `git apply -R .../fix.patch` | 0; `git diff --stat -- src/core/MixerSurround.cpp` empty |
| Rebuild after restore | `CCACHE_TEMPDIR=$TMPDIR cmake --build build --target zene ProjectStateTest -j4 > .../build-3-restored.log 2>&1` | 0 |
| GREEN: new test after restore | `cd build/tests && QT_QPA_PLATFORM=offscreen ./ProjectStateTest nonFinitePannerAttributesFallBackToTheDefaults > .../test-3-green-restored.log 2>&1` | 0 (3 passed, 0 failed) |
| Family build | `CCACHE_TEMPDIR=$TMPDIR cmake --build build --target zene SurroundPannerTest ProjectStateTest ExportWavWriteErrorTest SurroundTest -j4 > .../build-4-family.log 2>&1` | 0 |
| Family ctest (4 tests executed) | `ctest -R '^(SurroundTest\|SurroundPannerTest\|ProjectStateTest\|ExportWavWriteErrorTest)$' --output-on-failure > .../test-4-family.log 2>&1` (from `build/tests`) | 0 (4/4 passed: ExportWavWriteErrorTest, SurroundTest, ProjectStateTest, SurroundPannerTest) |
| Gates | `python3 .../qwen/feature_check.py <worktree>` as written exits 1 at `mkdir` of `<worktree>-feature-check` in the parent directory (permission denied, outside the write-jail). Run with an identical copy whose only changes are the log folder (`.../logs/gates`), the baseline cache path and the ROOT path, all pointing at the same directories: `python3 -I .../logs/gates-tool/feature_check_local.py <worktree> > .../gates-2.log 2>&1` | 0 (all 8 rows PASS; `RESULT: PASS`) |
| Completion check, run before this commit | `python3 .../qwen/submit_check.py <worktree> new-p16-134 > .../submit-1.log 2>&1` | 1 (only the gate step fails, for the same log-folder reason) |

The round-1 gate run (`.../new-p16-134.r1/logs/gates-1.log`) reported all 8 rows PASS, but the review's post-commit run flagged `file-length` on `SurroundTest.cpp` (526 code lines). That is the item fixed here; the round-2 run above is the first one to pass with the test out of that file.

The regression was run against the mutated tree and then restored, which is the proof step 4b asks for. There is no separate product fix diff to reverse, because the fix landed earlier (`3a8724211`).

Test count: 4 tests executed in the family (`SurroundTest`, `SurroundPannerTest`, `ProjectStateTest`, `ExportWavWriteErrorTest`), 0 failures.

## Self-check

- **Regression fails with the product change reverted, passes with it.** Yes. With the guard removed, `test-2-red-mutant.log` shows the failure at `ProjectStateTest.cpp:321` (EXIT 1). With the guard restored, `test-3-green-restored.log` (EXIT 0) and `test-4-family.log` (EXIT 0) pass.
- **Deterministic.** No wall-clock time, sleeps, threads or audio engine. The input is a fixed `QDomElement`, the object is a standalone `ChannelSurround` with `init(64)`, and the assertions are exact comparisons with the `PanPosition{}` defaults plus `std::isfinite`.
- **Fix at the cause and complete.** The cause is the parser accepting non-finite floats, and `attribute()` (`src/core/MixerSurround.cpp:284-291`) is the one parser for the panner attributes. The sibling entry points listed in Evidence were checked; `readPan()` already rejects non-finite values through `inRange()`. The pre-existing NaN-to-default behaviour for `centre`, `surround`, `lfe` and the snapshot readers is covered by commit `3a8724211`'s tests and is unchanged.
- **No collateral damage.** `attribute()` returns the same value for every finite input as before, so valid projects load identically. The fallback is the same default a missing attribute already gets. No new limit or check was added that could reject a legitimate value. No test was weakened.
- **Evidence for ALREADY-FIXED.** `src/core/MixerSurround.cpp:284-291` (the guard), `:513-528` (the panner load path), `:400-414` (`panGains`), and `git merge-base --is-ancestor 3a8724211 HEAD` exits 0.

## Notes

- The completion check and gate runs above are environment blockers, not failures of the change. `feature_check.py` and `submit_check.py` write their logs into the parent of the worktree, which the write-jail forbids. The gate checks themselves all pass when run from the log-folder copy.
- The ccache first build failed on a temp directory outside the jail; `CCACHE_TEMPDIR=$TMPDIR` was used for every build after that. Compiler flags and sources are unchanged.

Files committed for this verdict: `tests/src/core/ProjectStateTest.cpp` (the regression test), `tests/src/core/SurroundTest.cpp` (restored to its base content, removing the earlier test from `ff1ba3a9b`), and `docs/qwen-fixes-2026-10/cluster-new-p16-134.md` (this report). No product file changed.
