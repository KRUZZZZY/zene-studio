FALSE-POSITIVE

# Cluster new-p10-68 - "Malformed sub-scene coordinates can reach grid access"

Finding: `src/core/ControlCommandsSessionSubScenes.cpp:146,159` (claim: a loaded sub-scene with `scene: -1` or
`tracks: [-1]` reaches `model.slot()` with negative coordinates and "may crash or read invalid state"). Confidence in the
finding was low. Checked against branch `bugs/qwen-new-p10-68` (fix base `050/m0-train-21`).

## Why it is not real

1. **Negative coordinates cannot be loaded.** The only readers of a sub-scene row are the two load paths, and both
   use `SubScene::restoreState`:
   - `src/core/SessionModel.cpp:184-200`: the scene is checked as `static_cast<unsigned>(row) >= MaxScenes`
     (`:188`) and each column as `static_cast<unsigned>(column) >= MaxTracks` (`:193`). `-1` becomes `UINT_MAX` and is
     refused. `MaxTracks = 256`, `MaxScenes = 512` (`src/core/SessionModelPrivate.h:156-157`).
   - `src/core/SessionSubScenes.cpp:68` (`readRows`): a row whose `restoreState` fails makes the section
     `false`, and the section is then kept verbatim and not read (`src/core/SessionSubScenes.cpp:128-130`).
   - `src/core/SessionModel.cpp:126-130` (`restoreSubScenes`, the legacy `<subscenes>` child): a refused row is skipped.
   So `scene="-1"` and `tracks="-1"` in a project file are never put into `m_subScenes`.

2. **No command can create a negative coordinate.** `subscene_create` limits `scene` to `integerProperty(0, 511)`
   (`src/core/ControlCommandsSessionSubScenes.cpp:190`), checks `scene < 0 || scene >= sceneCount()` (`:87`), and
   refuses `track < 0` in `columnsOf` (`:70`). `subscene_rename` and `subscene_delete` do not change coordinates.
   `m_subScenes` has no other writer: `setSubScenes` is called from `SessionSubScenes.cpp:133`,
   `SessionModel.cpp:461` (restore), and `ControlCommandsSessionSubScenes.cpp:102,119` (create and delete). The grid
   resize paths do not touch `m_subScenes` (`rg m_subScenes src/core/SessionModel.cpp`: only `:309`, `:340`, `:405`,
   `:461`, `:582`, all clear, save, restore or move).

3. **Even a negative value would not crash or read an invalid slot.** `SessionModel::slot` is bounds-checked on both
   overloads: const at `src/core/SessionModel.cpp:253-263` returns a static empty slot when
   `track < 0 || track >= m_trackCount || scene < 0 || scene >= m_sceneCount`; non-const at `:266-276` returns a
   scratch slot. So `skippedColumns` at `:146` gets an empty slot and reports the column as skipped. In `launchCells`
   (`src/core/ControlCommandsSessionLaunch.cpp:315-316`) the slot read happens before the `track < 0` check, but the
   read is bounds-safe and the column is then skipped, so no launch and no invalid access. The upper bound is already
   checked: `launchSubScene` refuses `scene >= sceneCount()` (`ControlCommandsSessionSubScenes.cpp:159`), and columns
   at or past `launchable` are skipped (`:146`, `ControlCommandsSessionLaunch.cpp:316`).

4. **Related hardening already landed.** Commit `8242786e0f` (`new-p12-238-port`) made every session attribute reader
   strict through `intAttribute()` (`src/core/SessionModelPrivate.h`); commit `0260985dac` (`new-p16-222`) kept the
   sub-scene section intact when it holds text. A non-numeric sub-scene row is covered by
   `tests/src/core/SessionSubSceneTest.cpp:276-300` (`aNonNumericSubSceneRowIsRefusedNotSceneZero`).

## What is not covered (no change made)

- No existing test loads a sub-scene with a **negative** `scene` or `tracks` (the test at `SessionSubSceneTest.cpp:276`
  covers text only). The rejection in item 1 is by code, not by a test on this branch.
- `launchSubScene` has no explicit `scene < 0` check; a negative scene would be caught only by `slot()`'s bounds check
  and would report success with zero clips. This cannot be reached from a project file or a command, so it is not
  changed here. A one-line `scene < 0` guard would be a defensive option for a future writer; this is not a
  behaviour change for any reachable input.

## Commands run

| Command | Result |
| --- | --- |
| `git status --short` / `git log --oneline -1` (worktree) | clean; `37eaf07557` |
| `rg -n` on `src/core/ControlCommandsSessionSubScenes.cpp`, `src/core/SessionModel.cpp`, `src/core/SessionModelPrivate.h`, `src/core/SessionSubScenes.cpp`, `src/core/ControlCommandsSessionLaunch.cpp`, `tests/src/core/SessionSubSceneTest.cpp` | the line numbers above |
| `Read` of the same files | source read directly |

No product code was changed, so no RED or GREEN test run applies. No regression test was added: a test would pass on
the current tree and would not prove a bug.

Gates:

| Command | EXIT | Result |
| --- | --- | --- |
| `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 1 | Not a gate result: it stopped at `logs.mkdir` with `PermissionError` on `<worktree>-feature-check`, a folder outside the write-jail. Log: `logs/gate-1.log`. |
| `python3 -I logs/gate-runner/feature_check.py <worktree>` | 0 | RESULT: PASS. Copy of the same script with only two lines changed: the log folder (now `logs/gate-out`) and the baseline cache (read-only, the existing `gate-baselines/37eaf07557…json`). All eight checks PASS, including complexity and file-length with no new item. Log: `logs/gate-2.log`. |

The completion check (`submit_check.py`) runs after this commit; its result is reported in the session summary.

## Self-check

- *The regression test FAILS with your product change reverted and PASSES with it.* **N/A**: no product change. A
  pinning test would pass on the current tree, so it is not added as regression proof.
- *The test is deterministic.* **N/A** (no test added).
- *The fix is at the cause and complete.* **N/A** (not real). The cause the finding names is already handled by the
  `restoreState` unsigned compare and the `slot()` bounds check (items 1 and 3).
- *No collateral damage.* **Unchanged**: no product or test file edited, so project loading and saving are unchanged.
- *If your verdict is ALREADY-FIXED or FALSE-POSITIVE: the code (file:line) or the intended contract that shows it.*
  **Yes**: `SessionModel.cpp:184-200`, `SessionModelPrivate.h:156-157`, `SessionSubScenes.cpp:68,128-130`,
  `ControlCommandsSessionSubScenes.cpp:70,87,190`, `SessionModel.cpp:253-276`, `ControlCommandsSessionLaunch.cpp:315-316`.
- *Each new test FAILS with your product change reverted (exit codes).* **N/A**.
- *Every acceptance criterion is proven by a test you ran yourself.* **N/A** (no acceptance criteria beyond the verdict).
- *Each edit is ONE undo step, reversibility class.* **N/A** (no command or edit).
- *Nothing new runs on the audio thread.* **Yes**: no code added.
- *Old projects still load and render as before.* **Yes**: no code or data change.
- *Sibling commands in the changed command files.* **N/A** (no command file changed).
