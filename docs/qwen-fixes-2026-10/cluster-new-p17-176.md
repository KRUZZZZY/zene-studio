**Verdict: FIXED**

# Cluster new-p17-176: the dialog keeps its download label during inference

The stem-split progress dialog set its label to "Downloading the stem model, then separating (offline job)..." only while `fetching_model` was true, and nothing set it back when the fetch ended. A first-use split therefore showed the download text for the whole separation. The label is now set from the job phase on every poll: the download text while fetching, "Separating stems (offline job)..." otherwise.

## Evidence (file:line, current tree)

- `src/gui/StemSplitController.cpp:293-305` - new `applyPhaseLabel(dialog, fetchingModel)`: download text when fetching, separation text otherwise.
- `src/gui/StemSplitController.cpp:332` - `pollJob` calls it on every poll (the old `if (fetching) setLabelText(...)` block is gone).
- `src/gui/StemSplitController.cpp:268` - the initial dialog label, `"Separating stems (offline job)..."`, is the same text the helper now restores.
- `include/StemSplitController.h:84` - declaration, public static, as the test seam (no new control command, no timer in the test).
- `tests/src/core/StemSplitPipelineTest.cpp:604-615` - `dialogLabelFollowsJobPhase()`: label after `(dialog, true)` contains "Downloading the stem model"; after `(dialog, false)` it no longer contains "Downloading" and contains "Separating stems".
- Callers and siblings: `setLabelText` is called only from `applyPhaseLabel` (the initial label is set in `startJob`, line 268). The completed, failed and cancelled paths close the dialog, so no label is shown after them. No other file reads `fetching_model` for the GUI (`rg fetching_model` shows the backend producer in `src/core/ControlStemSupport.cpp:475` and the command schema only).
- No audio path, no undo, no save/load, no command file is touched.

## Method

The regression test cannot gate the real model fetch: `StemSurface` in `src/core/ControlStemSupport.cpp:132-147` hard-wires `StemModelStore::fetchDefaultModelIfMissing`, and `ControlRegistry::registerCommand` is fatal on a duplicate id with no unregister (`src/core/ControlRegistry.cpp:151-159`). So the test drives the label helper that `pollJob` calls, with a stack `QProgressDialog` and no timers, sleeps or threads.

Commit staging: the helper, its declaration and the test were staged as a behaviour-preserving first stage (helper still had the bug), the RED run was taken, then only the one-line-plus-else fix was left unstaged, so `git diff` produced exactly the fix for the revert step (4b).

## Commands and exit codes

Environment note: the default ccache temp dir (`/run/user/1000/ccache-tmp`) is not writable in this session, so every build below sets `CCACHE_TEMPDIR=/tmp/ccache-tmp-p17176`. No code change came from this.

| # | Command (run from the worktree / `build/tests`) | Log | EXIT |
|---|---|---|---|
| 1 | `cmake --build build --target StemSplitPipelineTest -j4` | build-1.log | 2 (ccache temp dir permission; no compile) |
| 2 | `env CCACHE_TEMPDIR=/tmp/ccache-tmp-p17176 cmake --build build --target StemSplitPipelineTest -j4` | build-2.log | 0 |
| 3 | `ctest -R '^StemSplitPipelineTest$' --output-on-failure` (baseline, before any edit) | test-base.log | 0 (1/1 passed, 0 skipped) |
| 4 | `env CCACHE_TEMPDIR=... cmake --build build --target zene StemSplitPipelineTest -j4` (stage 1, bug kept) | build-3.log | 0 |
| 5 | `ctest -R '^StemSplitPipelineTest$' --output-on-failure` - **RED** (stage 1, no fix) | test-red-1.log | **8** |
| 6 | `git diff -- src/gui/StemSplitController.cpp include/StemSplitController.h > .../logs/fix.patch` | - | 0 (15-line patch) |
| 7 | `git apply -R .../logs/fix.patch` (revert fix only) | - | 0 |
| 8 | `env CCACHE_TEMPDIR=... cmake --build build --target zene StemSplitPipelineTest -j4` | build-4-revert.log | 0 |
| 9 | `ctest -R '^StemSplitPipelineTest$' --output-on-failure` - **RED, fix reverted (step 4b)** | test-red-2.log | **8** |
| 10 | `git apply .../logs/fix.patch` (restore fix) | - | 0 |
| 11 | `env CCACHE_TEMPDIR=... cmake --build build --target zene StemSplitPipelineTest -j4` | build-5-fix.log | 0 |
| 12 | `ctest -R '^StemSplitPipelineTest$' --output-on-failure` - **GREEN** | test-green-1.log | 0 |
| 13 | `ctest -R '^StemSplitPipelineTest$' -V --output-on-failure` - GREEN, counted | test-green-2.log | 0 |
| 14 | `python3 feature_check.py <worktree>` as shipped | gate-1.log | 1 (see below) |
| 15 | `python3 /tmp/p17176-gate/feature_check_local.py <worktree>` - gate copy, log folder redirected | gate-2.log | **0** (RESULT: PASS) |

The RED failure in runs 5 and 9 is the same assertion, verbatim from the log:

`FAIL!  : StemSplitPipelineTest::dialogLabelFollowsJobPhase() '!dialog.labelText().contains(QStringLiteral("Downloading"))' returned FALSE. (Downloading the stem model, then separating (offline job)...)`
`Loc: [../../tests/src/core/StemSplitPipelineTest.cpp(611)]`

Run 14 failed only because the shipped script writes its logs to `<worktree>/../<name>-feature-check/`, which is the parent directory of the worktree and outside the write-jail (`PermissionError: ... zene-qwen-new-p17-176-feature-check`). I did not write there. The copy in run 15 is the same script with two paths changed (`ROOT` pointed at the existing qwen directory so the existing base-listing cache `gate-baselines/2da2ed356...json` is read; `logs` pointed at `runs/new-p17-176.r1/logs/feature-check-1/`). No gate logic was altered.

## Evidence table

| Item | Result |
|---|---|
| RED command / exit | run 5 (`test-red-1.log`) and run 9 (`test-red-2.log`): EXIT 8 |
| GREEN command / exit | run 12 (`test-green-1.log`) and run 13 (`test-green-2.log`): EXIT 0 |
| Affected family | `StemSplitPipelineTest`: 15 cases executed in GREEN (14 existing + 1 new), 0 failed, 0 skipped (`Totals: 15 passed, 0 failed, 0 skipped`). Baseline run 3 executed 1 ctest entry; the RED run reported `14 passed, 1 failed`. |
| Ratchet gates (complexity, file-length) | PASS, "no new item in your files" (`gate-2.log`) |
| Simple gates (fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures) | PASS, exit 0 each (`gate-2.log`) |
| Gate script exit | feature_check (copy): EXIT 0. As shipped: EXIT 1 (log-folder permission, not a gate result) |
| Files committed | `include/StemSplitController.h`, `src/gui/StemSplitController.cpp`, `tests/src/core/StemSplitPipelineTest.cpp`, `docs/qwen-fixes-2026-10/cluster-new-p17-176.md` |
| New files / registration | none new; `tests/fork-sources.txt` and `tests/upstream-modifications.txt` not touched (gates `fork-sources` and `no-upstream-regression` PASS) |

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it.** Run 9 (EXIT 8, fix reverted via `git apply -R fix.patch`) and run 12 (EXIT 0, fix re-applied). The stage-1 state (run 5) shows the same failure with the buggy helper.
- **The test is deterministic.** It calls `gui::StemSplitController::applyPhaseLabel` directly on a stack `QProgressDialog` and asserts the label text. No timers, sleeps, threads or model downloads. The seam is the public static `applyPhaseLabel` (`include/StemSplitController.h:84`).
- **The fix is at the cause and complete.** The cause is that the label was only set in one branch of the poll. The helper sets the label in both branches on every poll. The only other label-setting call (the initial label in `startJob`, `src/gui/StemSplitController.cpp:268`) already uses the same separation text.
- **No collateral damage.** GUI-only change: no save/load, no project data, no limit or check added, no change to the backend, to `fetching_model` production (`src/core/ControlStemSupport.cpp:475`), or to the command schema.
- **Realtime, undo and commands.** Nothing on the audio thread; the helper runs on the GUI thread from `pollJob`. No undo step is involved. No new control command (no `ControlCommandsSnapshot` change).
- **Registration and gates.** No new files. Gates PASS (run 15, RESULT: PASS). The shipped gate script is blocked by the write-jail (run 14, EXIT 1), reported above.
- **Sibling commands.** No command file changed, so no sibling command list applies.
- **Portability.** `QProgressDialog::setLabelText` and `labelText` are present in Qt 5.15 and Qt 6. No new Qt API.
