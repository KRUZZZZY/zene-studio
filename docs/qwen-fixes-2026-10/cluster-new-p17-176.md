**Verdict: FIXED**

# Cluster new-p17-176: the dialog keeps its download label during inference

The stem-split progress dialog set its label to "Downloading the stem model, then separating (offline job)..." only while `fetching_model` was true, and nothing set it back when the fetch ended. A first-use split therefore showed the download text for the whole separation. Round 2 (Luna REWORK) found that the round-1 regression called a helper and never ran the poll, so it still passed with the original poll condition restored. The regression now drives the real `pollJob` through a scripted status source, and the fix is back in `pollJob` where the bug was.

## Evidence (file:line, current tree)

- `src/gui/StemSplitController.cpp:331-337` - `pollJob`'s dialog update: the download label while `fetching_model` is true (`:331-334`), and the separation label otherwise (`:335-338`, the `else` added by this fix).
- `src/gui/StemSplitController.cpp:312-314` - the status read goes through the seam: `s_statusReader` when set, else `stem.job_status` through the registry.
- `src/gui/StemSplitController.cpp:57` - `s_statusReader`, empty by default (the registry path is unchanged in production).
- `src/gui/StemSplitController.cpp:297-300` - `setStatusReaderForTest`, the setter the test uses.
- `include/StemSplitController.h:85-86` - the `StatusReader` type and the setter declaration; `:48` forward-declares `ControlResult`.
- `tests/src/core/StemSplitPipelineTest.cpp:605-687` - `pollRestoresPhaseLabelWhenFetchingEnds()`: a real `splitClipToStems` creates the dialog and job; three scripted statuses (fetching at 25%, separating at 50%, cancelled) are delivered to the controller's own `pollJob` through `QMetaObject::invokeMethod`. The label assertion that fails without the fix is at `:678`.
- `:630-642` - a `Cleanup` guard that restores the registry status path and cancels the submitted job on every exit path.
- Callers and siblings: `setLabelText` is called only in `pollJob` (`:333`, `:337`) and once in `startJob` (`:272`, the initial text, which equals the restored separation text). The completed, failed and cancelled branches close or reset the dialog, so no label is visible afterwards. No other file reads `fetching_model` for the GUI (`src/core/ControlStemSupport.cpp:475` is the backend producer; the command schema is in `src/core/ControlCommandsStems.cpp`).
- No audio path, no undo step, no save or load, no command, no new control command.

## Why the seam is at the status read

The real fetch is hard-wired in `StemSurface` (`src/core/ControlStemSupport.cpp:132-147`), and `ControlRegistry::registerCommand` is fatal on a duplicate id with no unregister (`src/core/ControlRegistry.cpp:151-159`). So a test cannot replace `stem.job_status` in the registry. The seam is the one place `pollJob` reads a job's status, so the test runs everything after that read (the progress value, the label, and the state dispatch) on the real code, with no timer and no event loop. The test never spins the event loop, so the 150 ms poll timer cannot interleave.

## Commands and exit codes (round 2)

Environment note: every build sets `CCACHE_TEMPDIR=/tmp/ccache-tmp-p17176`, because the default ccache temp dir (`/run/user/1000/ccache-tmp`) is not writable in this session. Logs are in `runs/new-p17-176.r2/logs/`.

| # | Command (worktree / `build/tests`) | Log | EXIT | Result |
|---|---|---|---|---|
| R1 | `env CCACHE_TEMPDIR=... cmake --build build --target zene StemSplitPipelineTest -j4` - stage 1: the ORIGINAL `if (fetching_model)` poll condition, the seam, the new test, no `else` | build-r2-1-stage1.log | 0 | builds |
| R2 | `ctest -R '^StemSplitPipelineTest$' -V --output-on-failure` - stage 1 | test-r2-red-1-stage1.log | 8 | `FAIL! : pollRestoresPhaseLabelWhenFetchingEnds() '!progress->labelText().contains("Downloading")' returned FALSE`, `Loc: StemSplitPipelineTest.cpp(678)`; 14 passed, 1 failed |
| R3 | `env CCACHE_TEMPDIR=... cmake --build build --target zene StemSplitPipelineTest -j4` - stage 1 + fix (`else` branch) | build-r2-2-fix.log | 0 | builds |
| R4 | `ctest -R '^StemSplitPipelineTest$' -V --output-on-failure` - **GREEN with fix** | test-r2-green-1.log | 0 | `Totals: 15 passed, 0 failed, 0 skipped` |
| R5 | `git diff -- src/gui/StemSplitController.cpp include/StemSplitController.h > logs/fix-r2.patch` | - | 0 | 15-line patch: the `else` branch only (the staged stage 1 is the index) |
| R6 | `git apply -R logs/fix-r2.patch` - **fix reverted: the original `if (fetching_model)` poll behaviour** | - | 0 | - |
| R7 | `env CCACHE_TEMPDIR=... cmake --build build --target zene StemSplitPipelineTest -j4` | build-r2-3-revert.log | 0 | builds |
| R8 | `ctest -R '^StemSplitPipelineTest$' -V --output-on-failure` - **RED with the original poll behaviour** | test-r2-red-2-revert.log | **8** | same `FAIL!` at `StemSplitPipelineTest.cpp(678)`; 14 passed, 1 failed |
| R9 | `git apply logs/fix-r2.patch` - fix restored | - | 0 | - |
| R10 | `env CCACHE_TEMPDIR=... cmake --build build --target zene StemSplitPipelineTest -j4` | build-r2-4-reapply.log | 0 | builds |
| R11 | `ctest -R '^StemSplitPipelineTest$' -V --output-on-failure` - **GREEN restored** | test-r2-green-2-reapply.log | 0 | `Totals: 15 passed, 0 failed, 0 skipped` |
| R12 | branch-base feature check: `python3 feature_check_local_r2.py <worktree>` (the shipped `feature_check.py` with its log folder redirected; see below) | gate-r2-1.log | 0 | `RESULT: PASS`, 8 of 8 rows PASS |

The restored `if (fetching_model)` in R6 is the original poll line, byte-for-byte the condition at the base commit (`2da2ed356`, `src/gui/StemSplitController.cpp:317` there). R8 is the reviewer's exact reproduction: restore the old poll behaviour and the regression fails.

Round-1 history, for the record: the round-1 test called a helper with the bug, and passed with the poll condition restored (the REWORK reason). That helper and its declaration are removed in this round.

## Evidence table

| Item | Result |
|---|---|
| RED command / exit | R2 and R8 (`ctest -R '^StemSplitPipelineTest$' -V`): EXIT 8, `FAIL!` at `StemSplitPipelineTest.cpp(678)` |
| GREEN command / exit | R4 and R11: EXIT 0 |
| Affected family | `StemSplitPipelineTest`: 15 cases executed in GREEN (14 existing + 1 new), 0 failed, 0 skipped |
| Ratchet gates (complexity, file-length) | PASS, "no new item in your files" (`gate-r2-1.log`) |
| Simple gates (fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures) | PASS, exit 0 each (`gate-r2-1.log`) |
| Branch-base feature check | `feature_check_local_r2.py`: EXIT 0, RESULT: PASS (base `2da2ed356`, 4 changed files) |
| Files committed | `include/StemSplitController.h`, `src/gui/StemSplitController.cpp`, `tests/src/core/StemSplitPipelineTest.cpp`, `docs/qwen-fixes-2026-10/cluster-new-p17-176.md` |
| New files / registration | none new; `tests/fork-sources.txt` and `tests/upstream-modifications.txt` not touched (gates `fork-sources` and `no-upstream-regression` PASS) |

The shipped `feature_check.py` exits 1 in this sandbox (run in round 1, `gate-1.log`): it writes its logs to `<worktree>/../<name>-feature-check/`, outside the write-jail (`PermissionError`). I did not write there. The copy used for R12 is the same script with two paths changed: `ROOT` points at the existing qwen directory, so the base-listing cache `gate-baselines/2da2ed356...json` is read, and `logs` points at `runs/new-p17-176.r2/logs/feature-check-2/`. No gate logic was altered.

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it.** R8 (EXIT 8, original poll condition restored via `git apply -R`) and R11 (EXIT 0, fix restored). The failing assertion is the one the reviewer's reproduction hits: the label after the second poll still reads "Downloading the stem model".
- **The test runs the real poll path.** The test calls the controller's private `pollJob` through `QMetaObject::invokeMethod` (`tests/src/core/StemSplitPipelineTest.cpp:672, 676, 682`), not a helper. The only substitution is the status read, through the seam at `src/gui/StemSplitController.cpp:312-314`.
- **The test is deterministic.** No wall-clock timing, sleeps or threads. The statuses are scripted in order, the poll runs synchronously with no event loop, and the first poll asserts the progress value 25 and the download label before the second poll is sent. The real job is cancelled in the guard.
- **The fix is at the cause and complete.** The cause was the poll setting the label in one branch only. `pollJob` now sets the separation label when `fetching_model` is false. The initial label in `startJob` (`src/gui/StemSplitController.cpp:272`) already uses the same separation text, and the terminal states close the dialog.
- **No collateral damage.** GUI-only. `s_statusReader` is empty by default, so production reads `stem.job_status` exactly as before. No save or load, no limit, no check added.
- **Realtime, undo and commands.** Nothing on the audio thread. The poll runs on the GUI thread. No undo step. No new control command, so no `ControlCommandsSnapshot` change.
- **Registration and gates.** No new files. Gates PASS (R12). The shipped script is blocked by the write-jail, reported above.
- **Sibling commands.** No command file changed, so no sibling command list applies.
- **Portability.** `QProgressDialog::setLabelText`, `labelText`, `value`, `isVisible` exist in Qt 5.15 and Qt 6. `QMetaObject::invokeMethod` with a string name is in both. No new Qt API.
- **Verdict-specific item (ALREADY-FIXED / FALSE-POSITIVE evidence).** Not applicable: the verdict is FIXED.
