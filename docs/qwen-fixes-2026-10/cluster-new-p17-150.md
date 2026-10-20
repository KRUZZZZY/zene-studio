# Cluster new-p17-150: unsaved session lists working-directory files as revisions

**Verdict: FIXED**

`listProjectRevisions` and `findRevision` built project-file artefact paths from `projectPath`. With `projectPath` empty, `projectRevisionPath("", n)` became `.rev<n>` and the backup became `.bak`, so files in the working directory could appear as revisions of an unsaved session. `resolveSlot` now refuses those project-derived rotation, backup and git ids when there is no project path. Session-level autosaves remain independently resolvable from `recoveryFile`, including an autosave with no project identity. A deterministic test puts `.bak` and `.rev0` in the working directory plus an unowned autosave and its backup, and checks that only the two session recovery files are listed.

**Scope note.** The finding describes a user-visible path (`revisions.list` before a save). That path is already refused one layer up: `targetProject` returns "no 'project' given and the session has no project file yet" when the session has no file (`src/core/ControlCommandsRevisions.cpp:86-91`), and the list handler returns that error before calling the helper (`src/core/ControlCommandsRevisions.cpp:213-216`). The defect is therefore latent through the control socket. It is real in the library contract: `listProjectRevisions` and `findRevision` are exported (`include/RevisionTimeline.h`) and return working-directory files for an empty path, which the regression test demonstrates. No GUI code calls them (`rg` finds callers only in `src/core/ControlCommandsRevisions.cpp`, `src/core/RevisionTimeline*.cpp` and the test).

## Evidence

- `src/core/RevisionTimeline.cpp:104-124`: `needsProjectPath` and `resolveSlot` refuse rotation, backup and git ids when `projectPath` is empty. Autosave ids still resolve from the separate `recoveryFile` path.
- `src/core/RevisionTimeline.cpp:246-281` (`findRevision`): calls `resolveSlot`; `rev0`, `backup` and `git:<sha>` return empty for an empty path, while `autosave` and `autosave_prev` retain their session-level behavior.
- `src/core/RevisionTimeline.cpp:284-324` (`listProjectRevisions`): the artefact loop now omits project-derived paths for an empty path; its git branch already refused an empty path (`:297`).
- Callers of the shared resolver, all checked:
  - `restoreTimelineRevision` refuses an empty path before `resolveSlot` (`src/core/RevisionTimeline.cpp:400-419`).
  - `readRevisionBytes` (`:376`) is reached from `revisions.compare` and `revisions.restore` only after `target.isEmpty()` is refused (`src/core/ControlCommandsRevisions.cpp:259`, `:331`). The `live` id in `readRevisionBytes` does not use `resolveSlot`, so compare against the live file is unchanged.
  - The production `revisions.list` handler reaches `revisionTimelineState` (`:341`) only after its `:213` guard.
- `tests/src/core/RevisionTimelineTest.cpp:899-930`: case `anUnsavedSessionListsNoArtefactsFromTheWorkingDirectory` writes `.bak` and `.rev0` into a temp directory, switches the working directory there with a restoring guard, and provides an unowned autosave plus its backup. It expects only `autosave` and `autosave_prev` in the list; the working-directory `rev0` and `backup` remain invalid.
- Unchanged behaviour: any non-empty `projectPath` takes the same code as before, so saved projects, their `.rev` and `.bak` files and the autosave identity check load and list exactly as they did.
- The `revisions.list` control command already refuses an unsaved session before reaching this helper, so this API guard adds no socket-visible behavior. It preserves the existing rule that a recovery with no project identity remains visible as a session artefact.
- No new commands (the canary `ControlCommandsSnapshot` is untouched), no audio-thread code, no undo change. `src/core/RevisionTimeline.cpp` and `tests/src/core/RevisionTimelineTest.cpp` are already registered as fork-new sources (`tests/fork-sources.txt:1795`, `:2435`), so no registration change was needed.

## Evidence table

The following table records the builder's submitted implementation before QA review. Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p17-150.r1/logs/`. All ctest runs were from `build/tests`. The `env MAKEFLAGS= CCACHE_TEMPDIR=/tmp/zene-p17-150-ccache` prefix was needed in that session because the inherited make jobserver FIFO and the ccache temp directory under `/run/user` were not writable. The first build attempt failed on those, not on code (`build-red-1.log`, EXIT=2), and it is not a result.

| Step | Exact command | EXIT | Result |
| --- | --- | --- | --- |
| Build, RED (original code + new test) | `env MAKEFLAGS= CCACHE_TEMPDIR=/tmp/zene-p17-150-ccache cmake --build build --target RevisionTimelineTest -j2 > build-red-2.log 2>&1` | 0 | target built |
| **RED** | `ctest -R '^RevisionTimelineTest$' --output-on-failure > test-red-1.log 2>&1` | **8** | `anUnsavedSessionListsNoArtefactsFromTheWorkingDirectory()` FAIL: Actual (entries.size()) 2, Expected 0, `tests/src/core/RevisionTimelineTest.cpp(919)`. 23 other cases pass. |
| Build with fix | `env MAKEFLAGS= CCACHE_TEMPDIR=... cmake --build build --target RevisionTimelineTest -j2 > build-green-1.log 2>&1` | 0 | target built |
| **GREEN** | `ctest -R '^RevisionTimelineTest$' --output-on-failure > test-green-1.log 2>&1` | **0** | 100% passed |
| GREEN, per-case | `ctest -R '^RevisionTimelineTest$' -V > test-green-2.log 2>&1` | 0 | `PASS: ...anUnsavedSessionListsNoArtefactsFromTheWorkingDirectory()`; Totals 24 passed, 0 failed |
| 4b: save fix as patch | `git diff -- src/core/RevisionTimeline.cpp > logs/fix.patch` | 0 | 10 insertions |
| 4b: revert fix | `git apply -R logs/fix.patch` | 0 | diff stat empty afterwards |
| 4b: rebuild reverted | `env MAKEFLAGS= ... cmake --build build --target RevisionTimelineTest -j2 > build-4b-revert.log 2>&1` | 0 | |
| **4b: test, reverted** | `ctest -R '^RevisionTimelineTest$' -V > test-4b-revert.log 2>&1` | **8** | same FAIL: Actual 2, Expected 0, line 919; Totals 23 passed, 1 failed |
| 4b: reapply fix | `git apply logs/fix.patch` | 0 | `1 file changed, 10 insertions(+)` |
| 4b: rebuild with fix | `env MAKEFLAGS= ... cmake --build build --target RevisionTimelineTest -j2 > build-4b-reapply.log 2>&1` | 0 | |
| **4b: test, fixed again** | `ctest -R '^RevisionTimelineTest$' -V > test-4b-reapply.log 2>&1` | **0** | PASS for the new case; Totals 24 passed, 0 failed |
| Affected family | `ctest -R "Revision|Timeline|Revisions"` | 8 | Only `RevisionTimelineTest` (24/24 cases pass in the green runs above) belongs to the revision family. `TimelineTest` and `ArrangerTimelineTest` match the regex but were never built in this tree, so ctest reports them "Not Run" (`test-family-1.log`). That is what makes the exit 8, not a failure of the revision code. `ScriptCommandBridgeTest` only checks sandbox paths for `revisions.compare` and does not reach the resolver; not run. |
| Gate table, as written | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 1 | Stopped before running any gate: its log folder `<worktree>/../zene-qwen-new-p17-150-feature-check` is outside the write-jail, and `mkdir` failed with `PermissionError` (EACCES). Nothing was changed in the gate. |
| Gate table, same gate logic | `python3 -I logs/run_gates.py <worktree> > gates-run-1.log 2>&1` | 0 | Loads the unmodified `feature_check.py` functions and writes its per-gate logs to `logs/gates/`. Table: fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures PASS (exit 0 each); complexity and file-length PASS in the table; RESULT: PASS |
| Complexity gate (ratchet, script exit) | `bash tests/complexity-gate.sh --check` (run by the table) | 1 | Red on the train, as the gate's own description says. No item in a changed file is new against base `2da2ed356`. `resolveSlot` is not listed. |
| File-length gate (ratchet, script exit) | `bash tests/file-length-gate.sh --check` (run by the table) | 1 | `tests/src/core/RevisionTimelineTest.cpp` is listed at 621 lines (`logs/gates/file-length.log:37`). It was already over 500 lines on the base and the gate's key (name, not count) matches the base listing, so it is not new. |

## QA correction and final verification

The submitted guard also hid session-level autosaves when `projectPath` was empty. That conflicted with `RevisionTimeline.cpp:197-204`, which keeps an autosave with no project identity visible rather than hiding a possible unsaved session. The final code narrows the guard to rotation, backup and git IDs; the regression also checks that the independent autosave and its backup remain listed.

| Check | Command | EXIT | Result |
| --- | --- | --- | --- |
| Final build | `cmake --build build --target zene RevisionTimelineTest -j4` | 0 | `zene` and `RevisionTimelineTest` rebuilt |
| Final focused test | `ctest -R '^RevisionTimelineTest$' --output-on-failure` (from `build/tests`) | 0 | 1/1 ctest target passed, 24 Qt cases passed |
| Final feature gate | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p17-150` | 0 | All rows pass; no new complexity or file-length item |
| Reverse only the QA correction | `git apply -R /tmp/new-p17-150-qa-product.patch` | 0 | Restored the builder's broad guard for the check |
| Rebuild with QA correction reversed | `cmake --build build --target zene RevisionTimelineTest -j4` | 0 | targets rebuilt |
| Regression with QA correction reversed | `ctest -R '^RevisionTimelineTest$' --output-on-failure` (from `build/tests`) | 8 | Test fails at `RevisionTimelineTest.cpp:921`: actual 0, expected 2; 23 cases pass |
| Restore QA correction | `git apply /tmp/new-p17-150-qa-product.patch` | 0 | Final product code restored |
| Rebuild and retest restored code | `cmake --build build --target zene RevisionTimelineTest -j4`; `ctest -R '^RevisionTimelineTest$' --output-on-failure` (from `build/tests`) | 0 / 0 | Rebuilt and passed, 24 Qt cases |

## Files committed

- `src/core/RevisionTimeline.cpp` (the guard in `resolveSlot`)
- `tests/src/core/RevisionTimelineTest.cpp` (the regression case)
- `docs/qwen-fixes-2026-10/cluster-new-p17-150.md` (this report)

## Self-check

The original `submit_check.py` run recorded a gate failure because its process could not create the sibling log folder. During final QA, the requested stock `feature_check.py` command ran successfully with EXIT 0; all rows passed, including complexity and file-length, with no new item in a touched file.

Answers to the completion check's five items:

1. **Regression fails with the product change reverted, passes with it.** The original test in the builder's evidence failed before its fix (EXIT 8, actual 2 vs expected 0) and passed with it. The final regression was also run with the QA correction reversed (`git apply -R /tmp/new-p17-150-qa-product.patch`, EXIT 0): rebuild EXIT 0, ctest EXIT 8 at line 921 (actual 0 vs expected 2, 23 pass). Restoring the QA correction (EXIT 0), rebuilding (EXIT 0), and rerunning ctest (EXIT 0) passed all 24 Qt cases.
2. **Deterministic.** No clock, sleep, thread or race. The inputs are fixed files the test writes into its own temp directory (`m_dir`). The working directory is switched to a subfolder of it and restored by a scope guard (`tests/src/core/RevisionTimelineTest.cpp:909-915`). The autosave path is a separate temp file with no identity sidecar, so no real session file is read.
3. **Fix at the cause, and complete.** The empty-path check (`src/core/RevisionTimeline.cpp:104-124`) covers the project-derived rotation, backup and git IDs, while session-level autosave IDs remain resolvable. Its callers, checked: `findRevision` (`:261`), `readRevisionBytes` (`:376`), and `restoreTimelineRevision`, which refuses an empty path first (`:404`) and then calls `resolveSlot` (`:419`). `listProjectRevisions` (`:284`) already refused the git branch for an empty path (`:297`). The command handlers refuse an empty target before any helper call: `ControlCommandsRevisions.cpp:86-91` (`targetProject`), `:213`, `:259`, `:331`. The `live` id in `readRevisionBytes` does not use `resolveSlot` and is unchanged.
4. **No collateral damage.** A non-empty `projectPath` takes the same code as before. With an empty path, only project-derived paths are refused; session autosave behavior, including a recovery with no project identity, remains intact. No saved project or recovery data is changed, and no limit or cap was added.
5. **Not applicable.** The verdict is FIXED, not ALREADY-FIXED or FALSE-POSITIVE. The finding's user-visible route is already refused one layer up (`ControlCommandsRevisions.cpp:86-91`, `:213-216`); the defect is in the library contract, as stated in the scope note.

Also checked: no audio-thread code and no undo change (`restoreTimelineRevision`'s undo step is unchanged), no command added (the canary is untouched), and both touched files are already fork-new sources (`tests/fork-sources.txt:1795`, `:2435`).
