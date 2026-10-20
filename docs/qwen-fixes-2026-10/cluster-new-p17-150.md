# Cluster new-p17-150: unsaved session lists working-directory files as revisions

**Verdict: FIXED**

`listProjectRevisions` and `findRevision` built every project-file artefact path from `projectPath`. With `projectPath` empty, `projectRevisionPath("", n)` became `.rev<n>`, the backup became `.bak`, and both resolved against the working directory. A `.bak` or `.rev0` sitting in the working directory was therefore listed as a revision of an unsaved session. `resolveSlot`, the one shared resolver for the rotation, backup, autosave and git families, now refuses every id when there is no project path, so no caller can reach the working directory this way. A deterministic test puts `.bak` and `.rev0` in the working directory and expects an empty timeline.

**Scope note.** The finding describes a user-visible path (`revisions.list` before a save). That path is already refused one layer up: `targetProject` returns "no 'project' given and the session has no project file yet" when the session has no file (`src/core/ControlCommandsRevisions.cpp:86-91`), and the list handler returns that error before calling the helper (`src/core/ControlCommandsRevisions.cpp:213-216`). The defect is therefore latent through the control socket. It is real in the library contract: `listProjectRevisions` and `findRevision` are exported (`include/RevisionTimeline.h`) and return working-directory files for an empty path, which the regression test demonstrates. No GUI code calls them (`rg` finds callers only in `src/core/ControlCommandsRevisions.cpp`, `src/core/RevisionTimeline*.cpp` and the test).

## Evidence

- `src/core/RevisionTimeline.cpp:106-118`: `resolveSlot` returns false with "names no artefact: the session has no project file yet" when `projectPath` is empty. This sits before the rotation, backup, autosave and git branches, so every id family is covered.
- `src/core/RevisionTimeline.cpp:240-256` (`findRevision`): calls `resolveSlot` and returns an invalid entry on failure, so `rev0`, `backup`, `autosave` and `git:<sha>` all return empty for an empty path.
- `src/core/RevisionTimeline.cpp:278-296` (`listProjectRevisions`): unchanged. Its artefact loop now yields nothing for an empty path; its git branch already refused an empty path (`:291`).
- Callers of the shared resolver, all checked:
  - `restoreTimelineRevision` refuses an empty path before `resolveSlot` (`src/core/RevisionTimeline.cpp:398-400`).
  - `readRevisionBytes` (`:370`) is reached from `revisions.compare` and `revisions.restore` only after `target.isEmpty()` is refused (`src/core/ControlCommandsRevisions.cpp:259`, `:331`). The `live` id in `readRevisionBytes` does not use `resolveSlot`, so compare against the live file is unchanged.
  - `revisionTimelineState` is reached only from `revisions.list`, after the `:213` guard.
- `tests/src/core/RevisionTimelineTest.cpp:900-925`: new case `anUnsavedSessionListsNoArtefactsFromTheWorkingDirectory`. It writes `.bak` and `.rev0` into a temp directory, switches the working directory there with a restoring guard, and expects `listProjectRevisions("")` and `revisionTimelineState("")` to report 0 and `findRevision("", "rev0"/"backup")` to be invalid.
- Unchanged behaviour: any non-empty `projectPath` takes the same code as before, so saved projects, their `.rev` and `.bak` files and the autosave identity check load and list exactly as they did.
- Design choice: for an empty path the autosave (`recover.mmp`) is also excluded, not only the rotation and backup files. An unsaved session has no project for its autosave to belong to, and the command path already refuses such sessions, so no socket-visible behaviour changes.
- No new commands (the canary `ControlCommandsSnapshot` is untouched), no audio-thread code, no undo change. `src/core/RevisionTimeline.cpp` and `tests/src/core/RevisionTimelineTest.cpp` are already registered as fork-new sources (`tests/fork-sources.txt:1795`, `:2435`), so no registration change was needed.

## Evidence table

Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p17-150.r1/logs/`. All ctest runs were from `build/tests`. The `env MAKEFLAGS= CCACHE_TEMPDIR=/tmp/zene-p17-150-ccache` prefix is needed on this machine because the inherited make jobserver FIFO and the ccache temp directory under `/run/user` are not writable from this session. The first build attempt failed on those, not on code (`build-red-1.log`, EXIT=2), and it is not a result.

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

## Files committed

- `src/core/RevisionTimeline.cpp` (the guard in `resolveSlot`)
- `tests/src/core/RevisionTimelineTest.cpp` (the regression case)
- `docs/qwen-fixes-2026-10/cluster-new-p17-150.md` (this report)

## Self-check

Answered from the evidence above; the completion check's own output is in the section below it, if it was run.

- Regression fails without the fix and passes with it: yes. RED exit 8 (Actual 2, Expected 0); reverted-patch exit 8; fixed exit 0 (24/24).
- Deterministic: yes. No timing, sleeps or races; the working directory is switched and restored by a guard in the same thread.
- Fix at the cause and complete: yes. The guard is in the shared resolver used by every id family and by `findRevision`, `readRevisionBytes` and `restoreTimelineRevision`; the callers are listed above.
- No collateral damage: non-empty `projectPath` takes the same code; the only changed behaviour is the empty-path case, which no socket command reaches.
- Realtime, undo, commands: not on any audio path; `restoreTimelineRevision`'s undo step is unchanged; no command added.
- Registration: both touched files are already fork-new (`tests/fork-sources.txt:1795`, `:2435`); no new file.
- Gates: the table passes (RESULT PASS); the two ratchet scripts exit 1 on their own, as they do on this train, and the table shows no new item in a changed file. The table was run through `logs/run_gates.py` because the stock runner could not create its log folder outside the write-jail.
