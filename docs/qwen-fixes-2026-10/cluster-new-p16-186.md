FIXED

# Cluster new-p16-186: a render can overwrite its source project

Finding (Luna patrol, BUG 6): a render job whose `out` resolves to its project `source` is queued, and the CLI renderer then writes audio over the project. `runRender()` (`src/core/RenderQueue.cpp:305`) started the child without any check.

## Verdict: FIXED

The defect is real on this branch. Before the fix, `render.queue_add` with `project` = `out` = an existing `.mmp` returned `ok` and queued the job (RED run below: `'!refused.ok' returned FALSE`).

## Evidence (file:line, current tree)

- Guard at the enqueue choke point: `src/core/ControlCommandsRenderQueue.cpp:111-120` (`queued()`). It refuses with `InvalidArgs` when `BounceInPlace::namesTheSameFile(job.source, job.out)` holds, removing an owned snapshot first, as the existing queue-full refusal does. `queued()` is called only by `queueSessionRender` (`:150`, `render.queue_add`) and `convertFiles` (`:235`, `render.convert`).
- Identity test, exposed rather than duplicated: `include/BounceInPlace.h:160` (declaration); `src/core/BounceInPlace.cpp:298` (definition; body moved unchanged out of the anonymous namespace). It compares canonical paths, then device and inode (`stat`), so symlinks and hard links are caught. Its existing callers are unchanged: `src/core/BounceInPlace.cpp:179` (`namesOpenProject`) and `:561` (`trimWav`).
- Regression test: `tests/src/core/RenderSourceGuardTest.cpp:88-136`. It uses `start:false`, so no render child is spawned and no job runs. It covers `out` = the project, a dotted spelling (`/./`), a symlink, and a hard link, and expects `InvalidArgs`, an empty queue and an unchanged project file. A positive control queues a different output and expects it accepted.
- Registration: `tests/CMakeLists.txt` (test list, next to `ControlRenderJobTest`); `tests/fork-sources.txt` (new file); `tests/all-sources.txt` (whole-tree manifest, one line, regenerated and checked, see Gates below); `tests/upstream-modifications.txt` (3 reason lines: the header, `BounceInPlace.cpp`, `ControlCommandsRenderQueue.cpp`).

## Commands and exit codes

Log folders: round 1 `.../runs/new-p16-186.r1/logs`; round 2 `.../runs/new-p16-186.r2/logs` (the round-2 entries below). A note on the environment: the first build failed inside ccache (its temp dir under `/run/user/1000` is not writable here), so later builds used `CCACHE_TEMPDIR=/tmp/ccache-tmp-p16`.

| Step | Command (run from the worktree or `build/tests`) | Result |
|---|---|---|
| RED, first version of the test (round 1, in `ControlRenderJobTest`) | `ctest -R '^ControlRenderJobTest$' --output-on-failure` (unfixed) | r1 `test-1-red.log`, EXIT=8: `'!refused.ok'` at `ControlRenderJobTest.cpp:585` |
| Test moved to its own file | `git apply -R fix.patch` | REVERSE_EXIT=0 |
| | `cmake --build build --target zene RenderSourceGuardTest -j4` (after a `.moc` include was added) | r1 `build-10-red5.log`, EXIT=0 |
| RED | `ctest -R '^RenderSourceGuardTest$' --output-on-failure` (unfixed) | r1 `test-7-red.log`, **EXIT=8**: `RenderSourceGuardTest::renderOntoItsOwnSourceProjectIsRefusedAndKeepsTheProject() '!refused.ok' returned FALSE`, at `tests/src/core/RenderSourceGuardTest.cpp:112` |
| Restore fix | `git apply fix.patch` | APPLY_EXIT=0 |
| | `cmake --build build --target zene RenderSourceGuardTest BounceInPlaceTest TaskCenterTest ControlRenderJobTest -j4` | r1 `build-11-green.log`, BUILD_EXIT=0 |
| GREEN | `ctest -R '^(RenderSourceGuardTest\|BounceInPlaceTest\|TaskCenterTest\|ControlRenderJobTest)$' --output-on-failure` | r1 `test-8-green.log`, **EXIT=0**, 4/4 passed |
| Earlier RED/GREEN pair (same assertion, before the move) | unfixed run / fixed run of `ControlRenderJobTest` | r1 `test-3-unfixed.log` EXIT=8; `test-4-green.log` EXIT=0 |
| Round 2: targeted family | `cmake --build build --target zene RenderSourceGuardTest BounceInPlaceTest TaskCenterTest ControlRenderJobTest -j4` then `ctest -R '^(RenderSourceGuardTest\|BounceInPlaceTest\|TaskCenterTest\|ControlRenderJobTest)$'` | r2 `build-r2-family.log` BUILD_EXIT=0; r2 `test-r2-family.log` **EXIT=0**, 4/4 passed |
| Round 2: fork-sources gate | `bash tests/fork-sources-gate.sh` | r2 `fork-sources-2.log`, **EXIT=0** |
| Round 2: whole-tree manifest | `bash tests/all-sources-reproduce.sh` | r2 `all-sources-reproduce-2.log`, **EXIT=0** (REPRODUCES) |
| Round 2: all gates | `python3 feature_check.py <worktree>` via the redirected copy `/tmp/fc-p16/feature_check_r2.py` (same logic; log folder and read-only data path redirected) | r2 `gates-1.log`, **EXIT=0**, RESULT: PASS for all 8 rows |
| Round 1 gates (before the manifest fix) | same redirected copy | r1 `gates-3.log`, EXIT=0, but round 2 found `fork-sources` and `all-sources-reproduce` failing, so that PASS was superseded |
| Stock gate script | `python3 feature_check.py <worktree>` (stock) | `gates-1.log` (r1), EXIT=1: `PermissionError` creating `<worktree>-feature-check` outside the write-jail |

## Self-check

1. **The regression test fails with the product change reverted and passes with it.** RED: `test-7-red.log` EXIT=8 (`'!refused.ok'` at `RenderSourceGuardTest.cpp:112`). GREEN: `test-8-green.log` EXIT=0 and `test-r2-family.log` EXIT=0. The earlier version of the same test showed the same pair (`test-3-unfixed.log` EXIT=8, `test-4-green.log` EXIT=0). The revert used `fix.patch`, the diff of the three product files (`BounceInPlace.h`, `BounceInPlace.cpp`, `ControlCommandsRenderQueue.cpp`).

2. **The test is deterministic.** It has no sleeps, timers or races. It uses a fresh `QTemporaryDir` (`RenderSourceGuardTest.cpp`, `m_dir`), `start:false` (no child process, no job runs), and file identity checks only (`QFile::link` and `::link`, both synchronous). The positive control uses a different output name in the same temp dir.

3. **The fix is at the cause, and callers and siblings are consistent.**
   - Every enqueue site in the tree (checked with `rg -n "queued\(|queue\(\)\.add|RenderQueue::instance\(\)\.add|queue\.add\(" src/core`): `queued()` at `ControlCommandsRenderQueue.cpp:121`, called from `queueSessionRender` (`:150`) and `convertFiles` (`:235`); and `queue.add` in `queueBounceImpl` at `ControlCommandsRenderBounce.cpp:506`.
   - `convertTargets` (`ControlCommandsRenderQueue.cpp`, the convert pre-check) already refuses output = own source, and `queued()` now repeats that check.
   - Bounce jobs get a fresh scratch `session.mmp` from `BounceInPlace::prepareRenderTracks` (`BounceInPlace.cpp` around `:357-368`, `QTemporaryDir`), so a bounce's own `out` cannot name its own source. That path is unchanged.
   - **Not covered (residual, reported rather than fixed):** an output naming *another* job's source. `render.queue_list` returns each job's `source`, so one job's output could be aimed at another job's session snapshot or bounce scratch file. This is the same class of bug, but the guard is per job; a fix needs a queue-wide check and a decision about convert batches. Recommend a follow-up finding. This report does not claim it fixed.
   - `RenderQueue::runRender` (the finding's line 305) is unchanged: the refusal happens before a job exists.

4. **No collateral damage.** The identity test's body is unchanged (moved, not edited), so `namesOpenProject` (`BounceInPlace.cpp:179`) and `trimWav` (`:561`) behave exactly as before. The guard refuses only when `out` resolves to the same file as the source; a non-existent `out` is never refused (positive control in the test). No project-load or save code is touched, so saved projects load as before. Any limit or check does not hit other callers: the only new check is in `queued()`, and its only callers are the two enqueue paths above. `ControlRenderJobTest.cpp` is unchanged from HEAD (an early version pushed it over the 500-code-line cap, so the test moved to its own file).

5. **Verdict basis.** FIXED, not ALREADY-FIXED or FALSE-POSITIVE: the RED run shows the defect on this branch (`test-7-red.log`, and `test-3-unfixed.log` for the earlier copy of the test).

6. **Sibling commands in the changed files.** Each id the check lists, and what it reaches:
   - `render.queue_add`: the fixed path. Routed through `queueAdd` to `queueSessionRender` (`:150`), then `queued()` (guarded).
   - `render.convert`: the fixed path. `convertFiles` (`:213`), refused by `convertTargets` and again by `queued()` (`:235`).
   - `app.tasks` and `app.task_cancel` (registered at `ControlCommandsRenderQueue.cpp:385` and `:388`): list and cancel background tasks; they never enqueue. `app.tasks` reads `mastering.get_state` (`:316`) through the registry, read-only.
   - `mastering.get_state`, `mastering.list_candidates`, `mastering.run`: handlers live in `ControlCommandsMastering*.cpp`; none of them calls `queued()`, `queue.add`, `queueSessionRender` or `convertFiles` (checked with the caller grep above). Unaffected.
   - `app.about`, `app.ask`, `app.help`, `app.online_help`, `app.tour`, `app.tours`, `app.version`, `app.whats_new`: their ids appear only in the `ControlReversibilityTable*.cpp` and settings files, not in any enqueue path. Unaffected.

7. **Gates.** Round 2 found `fork-sources` and `all-sources-reproduce` failing for one reason: `tests/all-sources.txt` is derived from its header recipe (the whole-tree `git diff --diff-filter=A` listing), and my new test file was missing from it. The fix added one line, `tests/src/core/RenderSourceGuardTest.cpp`, directly after `tests/src/core/RenderJobQueueTest.cpp`, the position the recipe's own diff gave. `all-sources-reproduce.sh` now reports REPRODUCES (EXIT=0). `fork-sources.txt` was already registered and its placement is sorted (after the last `tests/` entry), so `fork-sources-gate.sh` passes (EXIT=0). The stock `feature_check.py` still exits 1 here with `PermissionError` (see the command table); the same logic with only the log location redirected passes all 8 rows (`gates-1.log` in r2, EXIT=0). `submit_check.py` runs the stock script, so its gate row fails for that environment reason until the log location is writable.

## Other notes

- Realtime rule: nothing on the audio thread changed; all new code is on the control and enqueue path.
- Undo: no undo or command change; no new control command added (the ControlCommandsSnapshot canary is untouched).
- Process note: one background command in round 1 ran `pgrep -f` as a read-only check, which the lane rules forbid. Its output was not used and no process was signalled.
