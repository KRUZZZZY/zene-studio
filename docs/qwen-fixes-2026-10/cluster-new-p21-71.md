FALSE-POSITIVE

# Cluster new-p21-71: skippedColumns accesses model.slot for tracks outside the current grid

Finding (Luna patrol, historical line 146): `skippedColumns` may call `model.slot(track, scene)` with a track outside
the current grid after the grid shrinks and a sub-scene is launched. Verdict on branch `bugs/qwen-new-p21-71`
(base `7641e9493d`, fix base `050/m0-train-18`): no defect. The out-of-grid column is never passed to `slot()`, and
the grid is never read out of range even if it were.

## Evidence (file:line on this branch)

1. **The call is short-circuited.** `src/core/ControlCommandsSessionSubScenes.cpp:146`:
   `if (track >= launchable || model.slot(track, subScene.scene).isEmpty())`. `||` evaluates `model.slot` only when
   `track < launchable`.
2. **`launchable` never exceeds the grid.** `src/core/ControlCommandsSessionSubScenes.cpp:164` passes
   `launchableColumnCount(*model, song)`, which is `launchableColumns` at `src/core/ControlCommandsSessionLaunch.cpp:108-112`:
   `model.trackCount() < songTracks ? model.trackCount() : songTracks`. So `track < launchable` implies
   `track < model.trackCount()`.
3. **Negative columns cannot be stored.** Create refuses a column outside `[0, trackCount)`
   (`src/core/ControlCommandsSessionSubScenes.cpp:~62-78`, `columnsOf`). Restore refuses an unsigned-out-of-range
   column (`src/core/SessionModel.cpp:193`), and `SubScene::normalised` drops negatives (`src/core/SessionModel.cpp:167`).
4. **`slot()` is bounds-checked anyway.** `src/core/SessionModel.cpp:253-263` returns a shared empty slot for any
   `track < 0 || track >= m_trackCount` (and the same for scene). The mutable overload at `:266-277` writes to a
   scratch slot. An out-of-range read therefore cannot crash or corrupt the grid.
5. **The intended contract is "skip and say so".** `include/SessionModel.h:265-268` (SubScene doc): a sub-scene "may
   name ... (after the grid shrank) a row or column outside the grid: a launch skips what is not there and says so."
   `skippedColumns` reports those columns in `skipped`, which is the designed behaviour. `launchCells` applies the same
   guard at `src/core/ControlCommandsSessionLaunch.cpp:318`.
6. **Runtime check of the exact trigger.** A temporary probe (not committed; reverted with `git checkout --`) created a
   sub-scene on scene 1 with tracks `{0, 2}`, shrank the grid to 1 track with `session.set_grid`, then ran
   `session.subscene_launch`. Result: `shrink ok true`, `launch ok true`, `launched` = track 0, `skipped` = `[2]`,
   no crash and no out-of-range access.

## Commands and exit codes

| # | Command (cwd = worktree unless noted) | Log (LOG FOLDER) | EXIT |
|---|---|---|---|
| build-1 | `cmake --build build --target SessionSubSceneTest -j4` | build-1.log | 2 (ccache could not write its temp file in `~/.local/share/zene-bunny-fix/dsh/...`, outside the write-jail; not a code error) |
| build-2 | `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/zene-tmp/ccache-tmp-71 cmake --build build --target SessionSubSceneTest -j4` | build-2.log | 0 |
| test-1 | `cd build/tests && ctest -R "SessionSubSceneTest\|SessionSubSceneGridTest" --output-on-failure` | test-1.log | 8 (GridTest not yet built: "Not Run"; SessionSubSceneTest Passed) |
| build-3 | `CCACHE_TEMPDIR=... cmake --build build --target SessionSubSceneGridTest -j4` | build-3.log | 0 |
| test-2 | `cd build/tests && ctest -R SessionSubScene --output-on-failure` | test-2.log | 0 (2/2 passed) |
| build-4 | probe build (temporary test method, see item 6) | build-4-probe.log | 0 |
| test-3 | `cd build/tests && ctest -R "^SessionSubSceneTest$" --output-on-failure` (probe present) | test-3-probe.log | 0 (1/1 passed; probe printed `skipped [2]`) |
| run-4 | `./SessionSubSceneTest zzProbeShrinkThenLaunch` (probe present) | (stdout, quoted in item 6) | 0 (Totals: 3 passed, 0 failed) |
| revert | `git checkout -- tests/src/core/SessionSubSceneTest.cpp`; `git status --short` | none | 0; status clean |
| build-5 | `CCACHE_TEMPDIR=... cmake --build build --target SessionSubSceneTest SessionSubSceneGridTest -j4` (clean source) | build-5-clean.log | 0 |
| test-4 | `cd build/tests && ctest -R SessionSubScene --output-on-failure` (clean source) | test-4-clean.log | 0 (2/2 passed) |
| gate-fork-sources | `bash tests/fork-sources-gate.sh` | gate-fork-sources.log | 0 |
| gate-all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | gate-all-sources-reproduce.log | 0 |
| gate-no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | gate-no-upstream-regression.log | 0 |
| gate-check-namespace | `tests/scripted/check-namespace` | gate-check-namespace.log | 0 |
| gate-no-tautology | `bash tests/no-tautology-gate.sh` | gate-no-tautology.log | 0 |
| gate-doc-figures | `python3 tests/doc-figures-gate.py` | gate-doc-figures.log | 0 |
| gate-complexity | `bash tests/complexity-gate.sh --check` | gate-complexity.log | 1 (7 existing REGRESSION lines on the train; see below) |
| gate-file-length | `bash tests/file-length-gate.sh --check` | gate-file-length.log | 1 (22 existing REGRESSION lines on the train; see below) |
| feature_check | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | (none) | 1, before any gate ran: `PermissionError` creating `<worktree>-feature-check/` in the program folder, outside the write-jail. Replaced by the per-gate runs above, using the same commands from the script. |

Ratchet reading (the rule `feature_check.py` applies): complexity and file-length fail only for items new in a file the
branch changed. `git diff --name-only 7641e9493d..HEAD` lists **0 files** and `git status --short` is empty, so no item
can be new in a changed file. Both ratchets are therefore PASS under the script's rule, though their raw exit is 1.

## RED / GREEN

Not applicable: there is no product change, so there is no fix to prove. The probe in item 6 is the runtime check, and
it is not part of the commit. Per procedure step 2, the regression-test steps (4, 4b) were skipped for a not-real finding.

## Files committed

- `docs/qwen-fixes-2026-10/cluster-new-p21-71.md` (this report). No product or test file changed.

## Self-check

`submit_check.py` (log: LOG FOLDER `submit-1.log`, EXIT=1) reported:
- `commits on the branch`: FAIL before this commit; this commit is the one that closes it.
- `working tree clean`: FAIL before this commit; clean after it.
- `gates (feature_check.py)`: FAIL. Cause: the script writes its logs to `<worktree>-feature-check/` in the program folder,
  outside the write-jail, and fails with `PermissionError` before any gate runs. This is an environment limit, not a
  gate result. The six gates and two ratchets were run individually with the script's own commands (table above): six
  exit 0; the two ratchets exit 1 with no item in a changed file (0 changed files), so the script's rule reads PASS.
- Regression test RED/GREEN: not applicable. The verdict is FALSE-POSITIVE and no product code changed, so there is
  no fix to prove. The runtime probe (item 6) shows the trigger path is safe on the current code.
- Deterministic: no test added or changed; nothing timing-based is in this commit.
- Fix at the cause, callers and siblings: no code change. `skippedColumns` has one caller
  (`ControlCommandsSessionSubScenes.cpp:172`, from `launchSubScene`); its sibling guard at
  `ControlCommandsSessionLaunch.cpp:318` uses the same `track >= launchable` bound.
- Collateral damage: none; no code, data or project format changed.
- Verdict evidence: `ControlCommandsSessionSubScenes.cpp:146`, `ControlCommandsSessionLaunch.cpp:108-112`,
  `SessionModel.cpp:253-263`, `SessionModel.cpp:167,193`, `include/SessionModel.h:265-268` (see Evidence above).
