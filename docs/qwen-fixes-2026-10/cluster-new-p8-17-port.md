# Cluster new-p8-17-port

**Verdict: FIXED (port, round 2 - QA rework addressed).** The fix accepted on `bugs/qwen-new-p8-17`
(commits `c255adea4`, `b87e4d793`) is re-applied by hand on the newest line (`bugs/qwen-new-p8-17-port`,
based on `4eb8d6e8a`). Nothing was merged, cherry-picked or rebased. The bug is not already fixed on this
line: the unpatched code called `project.new` before any check of the cache template.

Finding: "A failed built-in document creation can leave the old session replaced" (`newFromRecipe`,
historical line 157).

## What round 1 got wrong, and what changed

Round 1 (commit `8401616e8`) fixed the reported failure (the session content is preserved) but the QA
rework asked for more: on a **late** template-save failure the rollback reloaded a plain file snapshot,
and a project load clears the journal (`Song::clearProject -> clearJournal`), so the restored session came
back with an **empty undo/redo history**. The late-save regression only checked content and metadata, not
the history, so it did not catch it.

Round 2 makes the rollback go through the existing undo-snapshot machinery, which is exactly the "restore
in place, keep the history" primitive (WF-033, `docs/UNDO-HISTORY-PERSISTENCE.md`):

- `src/core/ControlCommandsDocuments.cpp`
  - `recipeTemplateWritable()` (line 159): unchanged - probes the template destination before `project.new`
    can discard the session.
  - `restoreSession()` (line 172): now calls `Song::restoreUndoSnapshot(snapshot, previousFile,
    skippedSections)`. That restores the whole document **in place** and detaches/reattaches the journal,
    so the undo/redo steps and the append-only `<z:provenance>` log survive; the file name and the modified
    flag are put back afterwards (`noteProjectSaved` + `setModified()`).
  - `newFromRecipe()` (line 195): preflight, then `captureUndoSnapshot()` the current session, **detach the
    undo/redo stacks** (`ProjectJournal::detachHistory()`, which `project.new`'s `clearJournal` would
    otherwise drop), then `project.new`. On any post-mutation failure (new session, template save, template
    open) it restores the snapshot and `reattachHistory()`s the detached steps. On success the detached
    history is discarded, because a template's session starts with an empty history.
- `src/core/Song.cpp`: **reverted to the base commit** (`4eb8d6e8a`). Round 1 added a
  `loadProjectSnapshotWithoutRecent()` / `RecentProjectSuppression` hack for the file-snapshot path; the
  undo-snapshot restore already suppresses the recent-project add through `m_restoringSnapshot`
  (`Song::loadProject`, line 1972), so the workaround - and its `tests/upstream-modifications.txt` entry -
  are gone. This removes the only inherited-file change of the port.
- `tests/src/core/ControlSuccessPathsTest.cpp`: `failedBuiltInDocumentSaveKeepsCurrentSession` now makes a
  **known undoable change** first (`track.add`), then induces the template-save failure, and asserts after
  the failure that the project content **and** its history survive: `canUndo()` is true, `control.undo`
  takes the change back, `control.redo` brings it back. The check was added to the existing slot, as the
  rework asked.
- `tests/upstream-modifications.txt`: the stale `src/core/Song.cpp` entry from round 1 is removed (the file
  ends with a newline).
- `tests/fork-sources.txt`: unchanged (round 1 already registered the test file).

No new control commands; no `ControlCommandsSnapshot` change; no header changed.

## Evidence

Log folder: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-17-port.r1/logs`. Builds use
`CCACHE_TEMPDIR=/tmp/zene-ccache-tmp-p8-17` (ccache cannot write under `/run/user/1000` in this sandbox).
**No `HOME` override was needed this round**: `ControlSuccessPathsTest` ran and passed in the normal
environment (`r2-05`).

The RED/GREEN proof reverses **only the product delta** (not the test), as the procedure requires. Because
round 1's partial fix was already committed, the product delta is the change from `HEAD` (`8401616e8`) to
this tree; reversing it lands on round 1's file-snapshot fix, whose rollback drops the history - which is
exactly what the extended test must catch.

| Step | Exact command (from worktree, or `build/tests` for ctest) | Exit | Log | Result |
|---|---|---:|---|---|
| Build fix | `CCACHE_TEMPDIR=/tmp/zene-ccache-tmp-p8-17 cmake --build build --target zene ControlSuccessPathsTest -j4` | 0 | `r2-04-build-fix.log` | |
| **GREEN** | `ctest -R '^ControlSuccessPathsTest$' --output-on-failure` | **0** | `r2-05-test-green.log` | 1/1 passed |
| Proof: make product patch | `git diff HEAD -- src/core/ControlCommandsDocuments.cpp src/core/Song.cpp > <logs>/r2-red.patch` | 0 | `r2-red.patch` | |
| Proof: reverse product delta | `git apply -R <logs>/r2-red.patch` | 0 | | workspace product files == `HEAD` (round 1's fix) |
| Proof: rebuild reversed | `CCACHE_TEMPDIR=/tmp/zene-ccache-tmp-p8-17 cmake --build build --target zene ControlSuccessPathsTest -j4` | 0 | `r2-08-build-red.log` | |
| **Proof RED** (product delta reversed) | `ctest -R '^ControlSuccessPathsTest$' --output-on-failure` | **8** | `r2-09-test-red.log` | `FAIL!: failedBuiltInDocumentSaveKeepsCurrentSession() 'undoSurvived' returned FALSE. (a save-time failure dropped the undo history of the restored session)`. Totals: 16 passed, 1 failed |
| Proof: re-apply product delta | `git apply <logs>/r2-red.patch` | 0 | | |
| Proof: rebuild fixed | `CCACHE_TEMPDIR=/tmp/zene-ccache-tmp-p8-17 cmake --build build --target zene ControlSuccessPathsTest -j4` | 0 | `r2-10-build-green.log` | |
| **GREEN** (verbose) | `ctest -R '^ControlSuccessPathsTest$' -V` | **0** | `r2-14-test-green-verbose.log` | `Totals: 17 passed, 0 failed, 0 skipped, 0 blacklisted` |
| Complexity gate | `bash tests/complexity-gate.sh --check` | **1** | `r2-12-gate-complexity.log` | pre-existing; see below |
| File-length gate | `bash tests/file-length-gate.sh --check` | **1** | `r2-13-gate-file-length.log` | pre-existing; see below |

**Affected ctest family:** `ControlSuccessPathsTest`, 1 registered test, **17 Qt slots** in each run
(0 would be an error). Counts: RED 16 passed / 1 failed; GREEN 17 passed / 0 failed.

**Related targets:** `ArrangingCatchUpTest` and `LibraryMenusTest` (the other two tests that mention
`project.new_document`) were built and run. `LibraryMenusTest` passes. `ArrangingCatchUpTest` fails only in
`queuedInstrumentHandlesAreRemovedBeforeTheirTrackCanDie`, where `track->loadInstrument("tripleoscillator")`
falls back to `dummyinstrument` because the plug-in is not found in this ctest environment
(`r2-16-test-related.log` lines 35-39). That is a plug-in-directory/environment failure on a path this
change does not touch, not a rollback regression. `LMMS_PLUGIN_DIR` was not set, as the rules require.

## Ratchet gates: failing, identical to the base

- `bash tests/complexity-gate.sh --check` exits **1**; the `REGRESSION:` lines are byte-identical to round
  1's run on the base (`diff` of the `REGRESSION` lines against `gate-1-complexity.log`: **EXIT 0**). None
  names `ControlCommandsDocuments.cpp`, `Song.cpp`, `newFromRecipe`, `restoreSession` or
  `ControlSuccessPathsTest.cpp`.
- `bash tests/file-length-gate.sh --check` exits **1**; the `REGRESSION:` lines are likewise identical to
  round 1's base run (`diff` against `gate-2-file-length.log`: **EXIT 0**). `ControlSuccessPathsTest.cpp`
  is not listed.
- Neither gate limit nor baseline was touched, as required. The failures are the same stale-baseline
  entries (e.g. `tests/src/gui/ArrangerTimelineTest.cpp` 689 lines, `XyPadPairTest.cpp dotCentreAlong`
  CCN 16) that were already red on the untouched base; fixing them is out of scope for this bug.

## Not verified

- The rest of the `ctest` suite was not run; only the family that covers the change
  (`ControlSuccessPathsTest`) plus the two related targets above.

## Files committed

- `docs/qwen-fixes-2026-10/cluster-new-p8-17-port.md`
- `src/core/ControlCommandsDocuments.cpp`
- `src/core/Song.cpp` (reverted to the base commit - the branch no longer modifies this inherited file)
- `tests/src/core/ControlSuccessPathsTest.cpp`
- `tests/upstream-modifications.txt` (round 1's `Song.cpp` entry removed)
