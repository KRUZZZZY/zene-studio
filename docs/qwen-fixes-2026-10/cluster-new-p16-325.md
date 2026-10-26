Verdict: FIXED

new-p16-325: Excess warp markers are silently discarded

Evidence: `Clip::loadWarp` (src/core/Clip.cpp:767) read at most `WarpMarkers::MaxMarkers` (128, include/WarpMarkers.h:249) `<marker>` children. The loop condition at Clip.cpp:774 stopped the read and nothing looked at the rest, so a `<warp>` with 129 or more markers loaded its first 128 with no report. The next save writes only what was loaded (`saveWarp`, Clip.cpp:588-594 writes `m_warp.all()`), so the remaining markers were lost for good.

Fix (src/core/Clip.cpp:780-788): the first 128 markers still load, so the audio is what it was. When the `<warp>` holds more than `MaxMarkers`, the load now calls `Engine::getSong()->collectError(...)`, the same channel the non-increasing warp rejection uses (Clip.cpp:807-808). `Song::hasErrors()` is then checked after project load (src/core/Song.cpp:2288-2302), which shows the error report to the user (or prints it to stderr in agent mode). The message names the markers found, the number kept, and the number left out.

Decision: report rather than reject. Rejecting the oversized map would change what loads for every file that loaded before, which is a behaviour change outside the bug. The finding allows either; reporting keeps the load result unchanged.

Scope checked: `loadWarp` is the only reader of the `<warp>` child (`rg -n '"warp"' src include`: Clip.cpp:560 is the writer, Clip.cpp:769 the only reader). It runs from `Clip::restoreState` for every payload (Clip.cpp:204), so the copy and undo paths get the same check. Checkpoint restores never reach the limit because a checkpoint captures a map that was already within `MaxMarkers`.

Regression test: tests/src/core/ClipWarpPersistenceTest.cpp:313 `anOversizedWarpMapLoadsItsPrefixAndReportsTheLoss` (129 markers: asserts 128 loaded, `hasErrors()`, and the count in `errorSummary()`), and :336 `aWarpMapAtTheLimitLoadsWholeWithoutAReport` (exactly 128: all loaded, no report). The helper is `warpWithMarkers` (:129). No timing, sleeps or races.

Registration: no new files. `src/core/Clip.cpp` is inherited, so a reasoned line was appended to tests/upstream-modifications.txt (last line). The test file was already in tests/fork-sources.txt.

## Evidence table

Log folder: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-325.r1/logs`. All commands run from the worktree root unless noted.

| Step | Command | EXIT |
| --- | --- | --- |
| Build attempt 1 (environment) | `cmake --build build --target ClipWarpPersistenceTest -j4 > build-1.log 2>&1` | 2 (ccache could not write its temp file under the shared dsh path: "Permission denied"; no compile error, see build-1.log) |
| Build, RED (before fix) | `CCACHE_TEMPDIR=$TMPDIR cmake --build build --target ClipWarpPersistenceTest -j4 > build-2.log 2>&1` | 0 |
| RED run | `cd build/tests && ctest -R '^ClipWarpPersistenceTest$' --output-on-failure > test-red-1.log 2>&1` | 8 |
| Build after fix | `CCACHE_TEMPDIR=$TMPDIR cmake --build build --target ClipWarpPersistenceTest -j4 > build-3.log 2>&1` | 0 |
| GREEN run | `cd build/tests && ctest -R '^ClipWarpPersistenceTest$' --output-on-failure > test-green-1.log 2>&1` | 0 (100% passed, 1 test) |
| Mutation: save fix diff | `git diff -- src/core/Clip.cpp > logs/fix.patch` | 0 |
| Mutation: revert fix | `git apply -R logs/fix.patch` | 0 |
| Mutation: rebuild | `CCACHE_TEMPDIR=$TMPDIR cmake --build build --target ClipWarpPersistenceTest -j4 > build-4-revert.log 2>&1` | 0 |
| Mutation: RED with fix reverted | `cd build/tests && ctest -R '^ClipWarpPersistenceTest$' --output-on-failure > test-red-2-proof.log 2>&1` | 8 (`FAIL! : ...anOversizedWarpMapLoadsItsPrefixAndReportsTheLoss() 'Engine::getSong()->hasErrors()' returned FALSE`, ClipWarpPersistenceTest.cpp:330) |
| Mutation: re-apply fix | `git apply logs/fix.patch` | 0 |
| Mutation: rebuild | `CCACHE_TEMPDIR=$TMPDIR cmake --build build --target ClipWarpPersistenceTest -j4 > build-5-reapply.log 2>&1` | 0 |
| GREEN with fix re-applied | `cd build/tests && ctest -R '^ClipWarpPersistenceTest$' --output-on-failure > test-green-2-proof.log 2>&1` | 0 (100% passed, 1 test) |
| Family build | `CCACHE_TEMPDIR=$TMPDIR cmake --build build --target ControlWarpCommandsTest WarpMarkersTest WarpQuantizeTest LinkedClipGroupEditsTest ClipImportFileTest SampleClipStretchTest ClipSerialisationTest -j4 > build-6-family.log 2>&1` | 0 |
| Family run | `cd build/tests && ctest -R '^(ClipWarpPersistenceTest\|ControlWarpCommandsTest\|WarpMarkersTest\|WarpQuantizeTest\|LinkedClipGroupEditsTest\|ClipImportFileTest\|SampleClipStretchTest\|ClipSerialisationTest)$' --output-on-failure > test-family-1.log 2>&1` | 0 (8 of 8 passed) |
| Gate (script) | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > gate-feature-check-1.log 2>&1` | 1 (see note below; the script could not create its log folder) |
| Gate: fork-sources | `bash tests/fork-sources-gate.sh > gate-fork-sources.log 2>&1` | 0 |
| Gate: all-sources-reproduce | `bash tests/all-sources-reproduce.sh > gate-all-sources.log 2>&1` | 0 |
| Gate: no-upstream-regression | `bash tests/no-upstream-regression-gate.sh > gate-no-upstream.log 2>&1` | 0 |
| Gate: check-namespace | `tests/scripted/check-namespace > gate-check-namespace.log 2>&1` | 0 |
| Gate: no-tautology | `bash tests/no-tautology-gate.sh > gate-no-tautology.log 2>&1` | 0 |
| Gate: doc-figures | `python3 tests/doc-figures-gate.py > gate-doc-figures.log 2>&1` | 0 |
| Gate: complexity (ratchet) | `bash tests/complexity-gate.sh --check > gate-complexity.log 2>&1` | 1 (red on the train; no REGRESSION line names Clip.cpp, loadWarp or ClipWarpPersistenceTest) |
| Gate: file-length (ratchet) | `bash tests/file-length-gate.sh --check > gate-file-length.log 2>&1` | 1 (red on the train; no REGRESSION line names the changed files) |

Affected-family test count: 8 executed, 8 passed (ClipWarpPersistenceTest, ControlWarpCommandsTest, WarpMarkersTest, WarpQuantizeTest, LinkedClipGroupEditsTest, ClipImportFileTest, SampleClipStretchTest, ClipSerialisationTest).

Note on the gate script: `feature_check.py` writes its logs to `<worktree>/../<name>-feature-check/`, which is outside the write-jail. It failed on `mkdir` with `PermissionError` before running any gate (gate-feature-check-1.log). I did not change the shared script. Instead I ran each gate command the script runs, with the same working directory, and logged each one. The two ratchets are exit 1, as the brief allows, because the script only fails on items in changed files, so the check was whether any ratchet item named a changed file. None did. The script's own baseline comparison was not run.

Note on ccache: the first build failed with ccache "Permission denied" writing to the shared `dsh` temp path. Setting `CCACHE_TEMPDIR=$TMPDIR` fixed it. The compile itself never failed.

Files committed for this verdict: `src/core/Clip.cpp`, `tests/src/core/ClipWarpPersistenceTest.cpp`, `tests/upstream-modifications.txt`, `docs/qwen-fixes-2026-10/cluster-new-p16-325.md`.
