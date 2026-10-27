Verdict: FALSE-POSITIVE

new-p21-75: FALSE-POSITIVE (`handleSet` cannot reach `Engine::getSong()->setModified()` with a null song)

Evidence: the finding's line 130, `Engine::getSong()->setModified();` in `handleSet` at [ControlCommandsUndoHistory.cpp:130](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-75/src/core/ControlCommandsUndoHistory.cpp:130), is unguarded. The scenario the finding describes, a journal present with no song, cannot be reached through any path that runs this handler:

1. Every dispatch through `ControlRegistry::invoke()` checks `cmd->requiresEngine && !isEngineReady()` before the handler runs and refuses with `Busy` ([ControlRegistry.cpp:283-289](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-75/src/core/ControlRegistry.cpp:283)). `isEngineReady()` returns `s_ready && Engine::getSong() != nullptr && Engine::mixer() != nullptr` ([ControlRegistry.cpp:205-207](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-75/src/core/ControlRegistry.cpp:205)).
2. `requiresEngine` defaults to `true` ([ControlRegistry.h:104](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-75/include/ControlRegistry.h:104)). `registerSet` in `ControlCommandsUndoHistory.cpp` (lines 154-170) never sets it. The audit (`logs/002-source-audit.log`) finds no `requiresEngine` in that file, so `project.set_undo_history` is gated.
3. The song pointer is assigned once, in `Engine::create()`, after the journal ([Engine.cpp:70](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-75/src/core/Engine.cpp:70) journal, [Engine.cpp:72](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-75/src/core/Engine.cpp:72) song). It is deleted in teardown after the journal ([Engine.cpp:122-123](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-75/src/core/Engine.cpp:122)). There is no setter: `s_song` is a private static ([Engine.h:146](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-75/include/Engine.h:146)). So a null song means the engine is not created yet or is already gone. In both states `isEngineReady()` is false and `invoke()` refuses before the handler.
4. The journal-null check at line 129 is consistent with the same lifecycle. The journal exists alone only in the create window (Engine.cpp:70 to 72), and there `isEngineReady()` is still false.
5. The only other callers of the handler in the tree do not reach this command without the gate. `handlerOf` (ControlCommandsRecordTools.cpp:66-69) runs only punch ids. The NoteAcross and SessionPerform direct calls (ControlCommandsNoteAcross.cpp:130, ControlCommandsSessionPerform.cpp:267) target other commands. The GUI caller `UndoHistoryPanel.cpp:254` goes through `invoke()`. `rg set_undo_history src/` finds no other dispatch.

Not changed: the unguarded call is the same pattern as the other `Engine::getSong()->setModified()` call sites in `src/core` (121 by `rg -o`; `ControlCommandsController.cpp:373` and `:440` are wrapped in a null check). The unguarded ones sit behind the same `requiresEngine` gate. Adding a null check here would be defensive only and would change no reachable behaviour. A regression test cannot be written that fails without a fix: the handler is in an anonymous namespace, the only public entry refuses first, and `Engine` has no seam to clear the song. So there is no RED test, no product patch to reverse for step 4b, and no GREEN run. The procedure forbids a fix without a RED test, so none was made.

Residual, unverified: I did not trace whether a command already past `isEngineReady()` on the caller thread can run its handler on the UI thread during shutdown after `Engine::destroy()` starts. That is outside this finding's scenario.

| Evidence | Command | Result |
| --- | --- | --- |
| Source audit of every gate and direct-handler site | `rg -n "Engine::getSong\(\)|s_song\s*=|requiresEngine|isEngineReady|->handler\(|handlerOf\(" src/core/ControlRegistry.cpp src/core/Engine.cpp src/core/ControlCommandsUndoHistory.cpp src/core/ControlCommandsRecordTools.cpp src/core/ControlCommandsNoteAcross.cpp src/core/ControlCommandsSessionPerform.cpp include/Engine.h include/ControlRegistry.h > .../logs/002-source-audit.log 2>&1; echo EXIT=$?` | EXIT=0 (`logs/002-source-audit.log`). `logs/001-source-audit.log` is a mis-pathed first attempt (EXIT=2, nonexistent `src/core/ControlRecordTools.cpp`), superseded by 002. |
| Song-pointer and setter check | `rg -n "setSong|clearSong|s_song" include/Engine.h src/core/Engine.cpp > logs/004-engine-song-audit.log 2>&1; echo EXIT=$?` | EXIT=0. Only the declaration (Engine.cpp:48, Engine.h:146), creation (Engine.cpp:72), teardown (Engine.cpp:123) and uses; no setter |
| Count of unguarded-pattern call sites | `rg -o "Engine::getSong\(\)->setModified" src/core \| wc -l` | 121 occurrences (not logged to a file; reported as a count only) |
| Feature gate wrapper | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > logs/003-feature-check.log 2>&1; echo EXIT=$?` | EXIT=1. It stops before any check with `PermissionError` creating `<worktree-parent>/zene-qwen-new-p21-75-feature-check`, a directory outside the write-jail. No check result comes from the wrapper. |
| Gate fork-sources | `bash tests/fork-sources-gate.sh > logs/11-gate-fork-sources.log 2>&1; echo EXIT=$?` (cwd = worktree) | EXIT=0 |
| Gate all-sources-reproduce | `bash tests/all-sources-reproduce.sh > logs/12-gate-all-sources-reproduce.log 2>&1; echo EXIT=$?` | EXIT=0 |
| Gate no-upstream-regression | `bash tests/no-upstream-regression-gate.sh > logs/13-gate-no-upstream-regression.log 2>&1; echo EXIT=$?` | EXIT=0 |
| Gate check-namespace | `tests/scripted/check-namespace > logs/14-gate-check-namespace.log 2>&1; echo EXIT=$?` | EXIT=0 |
| Gate no-tautology | `bash tests/no-tautology-gate.sh > logs/15-gate-no-tautology.log 2>&1; echo EXIT=$?` | EXIT=0 |
| Gate doc-figures | `python3 tests/doc-figures-gate.py > logs/16-gate-doc-figures.log 2>&1; echo EXIT=$?` | EXIT=0 |
| Ratchet complexity | `bash tests/complexity-gate.sh --check > logs/17-gate-complexity.log 2>&1; echo EXIT=$?` | EXIT=1; 7 `REGRESSION` lines, all in other files (for example `WasmSandbox.cpp`, `DawProjectReadTracks.cpp`, `ControlCommandsMixerStrip.cpp`). None in any file this lane changed. |
| Ratchet file-length | `bash tests/file-length-gate.sh --check > logs/18-gate-file-length.log 2>&1; echo EXIT=$?` | EXIT=1; 22 `REGRESSION` lines, none in files this lane changed |
| Product tests | none run | 0 tests: no product code changed, so no test was run or is claimed |
| Step 4b (revert/re-apply) | not applicable | no product patch exists |

The ratchets are red on the train on purpose. By the gate's own rule (fail only on items in files the change touches), this lane's change touches no code, so both would pass. I did not run the wrapper's ratchet comparison: it writes its baseline under the gate's own directory, which is outside the write-jail. The six plain gates above all exited 0.

Files committed for this verdict: `docs/qwen-fixes-2026-10/cluster-new-p21-75.md`. No product file changed.

## Self-check

Completion check: `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> new-p21-75 > logs/019-submit-check.log 2>&1; echo EXIT=$?` gave EXIT=1 with two FAIL rows, answered here.

- **Regression test fails with the change reverted, passes with it** (ASK 1): not applicable. No product change exists to revert, and no test was added. The claim rests on the code paths at ControlRegistry.cpp:205-207 and 283-289 and Engine.cpp:70-72 and 122-123 (evidence above), not on a test result.
- **Deterministic test** (ASK 2): not applicable. No test was added. A test of the handler with a null song cannot be built: the handler sits in an anonymous namespace, the only public entry (`invoke`) refuses first, and `Engine` has no seam to clear `s_song` (Engine.h:146, no setter).
- **Fix at the cause and complete** (ASK 3): not applicable (no fix). The callers checked are the rows in the evidence list: the `invoke` gate, `handlerOf`, the NoteAcross and SessionPerform direct calls, and `UndoHistoryPanel.cpp:254`. None reaches this handler without the gate.
- **No collateral damage** (ASK 4): nothing changed, so no saved project, limit or caller changes behaviour.
- **Verdict code evidence** (ASK 5): ControlRegistry.cpp:205-207 (`isEngineReady`), ControlRegistry.cpp:283-289 (`requiresEngine` refusal before the handler), ControlRegistry.h:104 (default `true`), Engine.cpp:70-72 and 122-123 (song created after and destroyed before the journal), Engine.h:146 (private static, no setter).
- **FAIL "gates (feature_check.py)"** (not fixable in this lane): the wrapper stops at `logs.mkdir` with `PermissionError`, because it writes `<worktree-parent>/<name>-feature-check`, which is outside the write-jail. The wrapper therefore produced no table. The six plain gates and two ratchets were run directly, with the exit codes in the evidence table. I did not patch the wrapper or fake its table. This needs the owner to run `feature_check.py` from an unrestricted location, or to allow the wrapper a log folder inside the run folder.
- **FAIL "report has a Self-check section"**: resolved by this section.
