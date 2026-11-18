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

## Self-check

Answers to the submit_check ASK items. Log references are to the LOG FOLDER named above.

- **RED with the fix reverted, GREEN with it applied.** RED: `git apply -R logs/fix.patch`, rebuild (EXIT 0), `ctest -R '^ClipWarpPersistenceTest$'` EXIT 8, `FAIL! ... anOversizedWarpMapLoadsItsPrefixAndReportsTheLoss() 'Engine::getSong()->hasErrors()' returned FALSE`, ClipWarpPersistenceTest.cpp:330 (`test-red-1.log` and `test-red-2-proof.log`). GREEN: `git apply logs/fix.patch`, rebuild (EXIT 0), ctest EXIT 0 (`test-green-1.log`, `test-green-2-proof.log`). The first RED run was against the unmodified `Clip.cpp` and failed for the same reason.
- **Deterministic.** The test has no timers, sleeps or threads. Its input is a `<warp>` built by `warpWithMarkers` (ClipWarpPersistenceTest.cpp:129) with fixed `src`/`pos` values, and the clip is built with the file's existing `makeTone`, which is a pure function of the rate. Each test calls `Engine::getSong()->clearErrors()` first, so it does not depend on errors left by an earlier test in the same binary.
- **Fix at the cause; callers and siblings checked.** `loadWarp` is the only reader of the `<warp>` child (`rg -n '"warp"' src include`: Clip.cpp:560 is the writer, Clip.cpp:769 the only reader). Its only caller is `Clip::restoreState` (Clip.cpp:204), so the copy-payload, undo and checkpoint paths all reach the same check. The non-increasing rejection in the same function (Clip.cpp:807-808) uses the same `collectError` channel, so the two warnings are consistent. `ProjectMarkers::load` (src/core/ProjectMarkers.cpp:189-213) has the same cap but already logs its truncation with `qWarning`; it is a different structure and is out of scope. `SampleClip::setWarpMarkers` (include/SampleClip.h:166) refuses oversized sets on the authoring path and is unchanged.
- **No collateral damage.** The first `MaxMarkers` markers still load, so the audio is unchanged. The new code only reads the count and calls `collectError`; the load result is the same. No cap is added or lowered, so no caller that legitimately uses 128 markers is affected. A file with exactly 128 markers loads whole and reports nothing (`aWarpMapAtTheLimitLoadsWholeWithoutAReport`, ClipWarpPersistenceTest.cpp:336; passes). Old files without a `<warp>` child, and files with fewer markers, take no new path. The existing family ran 8 of 8 passing (`test-family-1.log`).
- **Verdict.** FIXED, not ALREADY-FIXED or FALSE-POSITIVE, so no contract citation is needed beyond the evidence above.

**Gate row (blocked by the sandbox, not the code).** `feature_check.py` fails before running any gate because it creates `<worktree>/../<name>-feature-check`, which is outside the write-jail (`gate-feature-check-1.log`, PermissionError). `submit_check.py` calls the same script, so its gate row stays FAIL in this session. I did not edit the shared script. Substitute evidence, all with the worktree as cwd: the six simple gates each EXIT 0 (`gate-fork-sources.log`, `gate-all-sources.log`, `gate-no-upstream.log`, `gate-check-namespace.log`, `gate-no-tautology.log`, `gate-doc-figures.log`). The two ratchets EXIT 1 by design (`gate-complexity.log`, `gate-file-length.log`), and the only check the script applies to them is whether an item names a changed file. None names `Clip.cpp`, `loadWarp` or `ClipWarpPersistenceTest`. The script's own baseline comparison was not run, so the gate row needs a run from a location the jail allows.
