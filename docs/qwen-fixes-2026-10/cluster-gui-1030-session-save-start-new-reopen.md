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

A first version of the test went into `tests/src/core/ProjectContainerSaveTest.cpp`. Once it was added, the file-length gate reported that file as "new file over 500 lines" (592 code lines, `gate-file-length.log`). A separate new file would have avoided that, but the lane cannot reconfigure the build tree to register it (see the blocked reconfigure above), so the test moved into `ProjectOpenIntegrityTest.cpp`. That file is configured and the file-length gate does not list it. `tests/CMakeLists.txt`, `tests/fork-sources.txt` and `tests/all-sources.txt` were restored to HEAD and are not part of this change.

## Files committed for this verdict

- `tests/src/core/ProjectOpenIntegrityTest.cpp` (coverage test; no product code changed)
- `docs/qwen-fixes-2026-10/cluster-gui-1030-session-save-start-new-reopen.md` (this report)

Environment notes: `CCACHE_DISABLE=1` was needed for every build because ccache could not create its temp files inside the sandbox. This changed no source or result.

## Self-check

The completion check (`submit_check.py`, log `submit-check-1.log`) reported two FAILs before this section existed: the missing Self-check section, and the gate wrapper. Each item is answered below.

- **Regression test fails with the product change reverted and passes with it (exit codes).** Not applicable, and not met. There is no product change, so there is nothing to revert, and the test passes on the code as it stands (`ctest -R '^ProjectOpenIntegrityTest$'` EXIT 0, 9 passed). This is a coverage check for a not-real finding, not a regression proof.
- **Deterministic (no wall-clock timing, sleeps or races).** The test has no clock, sleep or thread. It uses a fresh temporary directory, a fixed tempo (133), fixed note keys (60, 64, 67), a fixed velocity and length, and `Engine::getSong()` state read back after each step. The only environment dependence seen is the `Plugin not found` warnings (built-in plugins are not present in this build tree); those lines do not feed the comparison, and the comparison matched in this environment. I did not test it on a tree with plugins present.
- **Fix at the cause, complete (every caller and sibling checked).** No product fix. The sibling paths were checked: `saveProject`, `saveProjectAs`, `saveProjectAsWithFormat`, `createNewProject` and `openProjectFile` in `src/gui/MainWindow.cpp` (`:960-1155`) and the command handlers `project.save`, `project.new` and `project.open` (`src/core/ControlCommandsProjectFiles.cpp:647`) reach the same loader. The test drives the command-level path for both formats. It does not drive the file dialog or the menu wiring; that remains unverified in this run.
- **No collateral damage (old projects load as before).** No product file changed. The test file changes only add a helper set and a `ControlRegistry::setReady` call in `initTestCase`/`cleanupTestCase`. All 9 tests in `ProjectOpenIntegrityTest` pass, including the 8 that already existed. No saved data format was touched.
- **FALSE-POSITIVE evidence (file:line or contract).** The save, new and reopen code is at `src/gui/MainWindow.cpp:960-982` and `:1041-1168`, with the loader reached through `ControlCommandsProjectFiles.cpp:647`. The patrol's own record shows the save step was skipped, not failed (`terra-pilot/session/runs/m0-train-24/final.md`, `save-reopen` verdict `UNSURE`, no Zene error logged).

**Gate wrapper (`feature_check.py`) FAIL.** The wrapper cannot run from this lane: it creates `<worktree>-feature-check/` as a sibling of the worktree, which is outside the write area, so `mkdir` fails with `PermissionError` (`feature-check-2.log`, exit 1). I did not work around that. I reran each gate command it runs directly, from the worktree, on the committed final tree (`gate3-*.log`):

| Gate | Command | EXIT |
| --- | --- | --- |
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 |
| check-namespace | `tests/scripted/check-namespace` | 0 |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check` | 1 (7 tree-wide items; none names a file this change touches) |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check` | 1 (26 tree-wide items; none names `ProjectOpenIntegrityTest.cpp`) |

The two ratchets are red on the train on purpose. By the wrapper's rule (fail only on a new item in a file this change touches), they pass for this change: `grep` over `gate3-complexity.log` and `gate3-file-length.log` finds 0 items naming my file. The wrapper's base-listing comparison itself was not computed, because its cache is outside the write area.
