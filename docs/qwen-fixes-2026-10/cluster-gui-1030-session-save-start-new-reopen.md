FALSE-POSITIVE

gui-1030-session-save-start-new-reopen: FALSE-POSITIVE. The finding records a test step that was never run, not a Zene failure. The save, new and reopen workflow is implemented on this branch, and the command-level round trip (save, `project.new`, `project.open`) keeps tracks, notes and clips. No product file was changed. One regression-style check was added as coverage; it passes on the unmodified code, so it is not a regression proof.

## Why it is not a product defect

- **The tester never saved.** Luna's own record says the save-and-reopen step was skipped because the test instruction forbade creating files other than screenshots: `save-reopen` verdict `UNSURE`, `log_errors` empty, and "Save/reopen was not tested" (`/home/kruzzzzy/.local/share/zene-bunny-fix/terra-pilot/session/runs/m0-train-24/final.md`). The finding's own `repro` field says "Not reproduced". No Zene error was logged.
- **The save path exists and is wired to the owner's expected use** (`src/gui/MainWindow.cpp`):
  - Ctrl+S (`saveProject`, `:1041-1056`): an unnamed project goes to `saveProjectAs` (`:1061`), a named one to `guiSaveProject` (`:1047`).
  - Save As (`saveProjectAsWithFormat`, `:1077-1155`): the legacy blob goes through `guiSaveProjectAs(fname)` (`:1141`); a v2 container goes through `saveThroughCommand` → `project.save` (`:1160-1168`).
  - Start new (`createNewProject`, `:960-967`): `mayChangeProject(true)`, then `project.new`.
  - Reopen (`openProjectFile`, `:972-982`): `control::loadProjectWithCheckpoint` (`:976`). The command `project.open` reaches the same loader at `src/core/ControlCommandsProjectFiles.cpp:647`, so the File menu, recent list and socket share one reader.
- **The round trip is covered by a test that passes** (see below): a session with a tracked instrument, a clip and three notes is saved in both formats, `project.new` runs, and `project.open` reloads it. Tracks, tempo, mixer, clip names and note content all compare equal to the state before the save.

## What is not verified here

- The file dialog itself (Ctrl+S on an unnamed project opening Save As, and the File menu reopen) is not driven by any automated test in this run. The save and load logic under it is covered; the dialog wiring is not.
- No GUI run was done in this lane.

## Regression-style check added (coverage only)

`tests/src/core/ProjectOpenIntegrityTest.cpp`: new slot `saveStartNewAndReopenKeepsTracksNotesAndClips` (`:546`), plus helpers `run`, `stateOf`, `contentOf`, `buildSession` in the anonymous namespace (`:186-247`). `initTestCase` and `cleanupTestCase` now also call `ControlRegistry::setReady(true/false)`, which the new test needs.

- Non-vacuity: the test asserts more than three content items exist before the save (`contentOf().size() > 3`), so an empty comparison cannot pass.
- The new project must hold none of the saved track names and no clips or notes.
- After the reopen, tracks, tempo, mixer and content must equal the pre-save state.
- Runs in both `legacy` and `v2`.

RED was not achieved, and this is expected: there is no defect to fail on. Procedure step 4b (reverse the product diff and rerun) does not apply because there is no product diff. A deliberate product mutation to prove sensitivity was not run.

## Commands and exit codes

| Step | Command (run from the worktree) | EXIT |
| --- | --- | --- |
| Build, first attempt | `cmake --build build --target ProjectContainerSaveTest -j4 > .../logs/build-1.log 2>&1` | 2 (ccache temp-file permission denied; no compile error) |
| Build, ccache off | `CCACHE_DISABLE=1 cmake --build build --target ProjectContainerSaveTest -j4 > .../build-2.log 2>&1` | 2 (two compile errors in a draft test, since reverted) |
| Build after fixing those | `... build-3.log` | 0 |
| Draft test run (superseded) | `cd build/tests && ctest -R '^ProjectContainerSaveTest$' --output-on-failure > .../test-1.log 2>&1` | 8 (my first assertion was wrong: `project.new` leaves a default project with tracks; corrected) |
| Draft test run after correction | `ctest -R '^ProjectContainerSaveTest$' -V > .../test-4-verbose.log` | 0 (23 passed, 0 failed) |
| Move to an existing file | `cmake --build build --target ProjectSaveNewReopenTest` | 2: "No rule to make target"; `cmake build` for a reconfigure was blocked by the lane guard ("configuring is the parent's job"), so the new file was dropped and the test moved |
| Build target, final | `CCACHE_DISABLE=1 cmake --build build --target ProjectOpenIntegrityTest -j4 > .../build-8.log 2>&1` | 0 |
| Family run, final | `cd build/tests && ctest -R '^ProjectOpenIntegrityTest$' -V > .../test-5-verbose.log 2>&1` | 0 (`Totals: 9 passed, 0 failed`; new test `PASS`) |
| Gates (individually) | `bash tests/fork-sources-gate.sh`, `bash tests/all-sources-reproduce.sh`, `bash tests/no-upstream-regression-gate.sh`, `tests/scripted/check-namespace`, `bash tests/no-tautology-gate.sh`, `python3 tests/doc-figures-gate.py` | 0, 0, 0, 0, 0, 0 (logs `gate2-*.log`) |
| Ratchet gates (individually) | `bash tests/complexity-gate.sh --check`; `bash tests/file-length-gate.sh --check` | 1 and 1 (both red on the train; see below) |
| Gate wrapper | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | not usable from this lane: exits 1 with `PermissionError` creating `<worktree>-feature-check/`, a sibling directory outside the write area. I ran each gate command directly instead, with logs in the LOG FOLDER. |

The two ratchet gates list 7 (complexity) and 26 (file-length) tree-wide items. None names `tests/src/core/ProjectOpenIntegrityTest.cpp`, the only file this change touches, so no item is new in a file I changed. The base-listing comparison that `feature_check.py` performs was not computed; the cache sits outside my write area. The result above rests on the absence of any item in my file, not on that comparison.

## Why the test moved

A first version of the test went into `tests/src/core/ProjectContainerSaveTest.cpp`. That file is already over the 500-code-line limit, so the file-length ratchet flagged it. A new file would have been cleaner, but the lane cannot reconfigure the build tree to register it, so the test moved into `ProjectOpenIntegrityTest.cpp`, which is configured and well under the limit. `tests/CMakeLists.txt`, `tests/fork-sources.txt` and `tests/all-sources.txt` were restored to HEAD and are not part of this change.

## Files committed for this verdict

- `tests/src/core/ProjectOpenIntegrityTest.cpp` (coverage test; no product code changed)
- `docs/qwen-fixes-2026-10/cluster-gui-1030-session-save-start-new-reopen.md` (this report)

Environment notes: `CCACHE_DISABLE=1` was needed for every build because ccache could not create its temp files inside the sandbox. This changed no source or result.
