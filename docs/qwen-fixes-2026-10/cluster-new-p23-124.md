Verdict: FALSE-POSITIVE

new-p23-124: FALSE-POSITIVE

Finding: "setValue dereferences editedClip without a null check" (`src/gui/editors/ItemListAutomationSource.cpp:101`). A list edit with no clip open cannot reach the dereference. `setValue` refuses first, because `rowCount()` is 0 whenever the clip is null, so every row index fails the bounds check.

Evidence:

- The bounds guard runs before the call at line 101: [ItemListAutomationSource.cpp:90](src/gui/editors/ItemListAutomationSource.cpp:90) is `if (row < 0 || row >= rowCount()) { return false; }`.
- `rowCount()` returns 0 when there is no clip: [ItemListAutomationSource.cpp:72](src/gui/editors/ItemListAutomationSource.cpp:72) is `clip() != nullptr ? ... : 0`, and `clip()` (line 130) is null when `m_editor` is null or `m_editor->currentClip()` is null. So for a null clip, `row >= rowCount()` holds for every row and `setValue` returns false before `tickOf()` (line 91) or `movePoint` (line 101) runs. The guard also covers the existing `tickOf()`, which dereferences `clip()` without its own check.
- `editedClip()` and `currentClip()` are the same pointer: [AutomationEditor.h:94](include/AutomationEditor.h:94) returns `m_clip`, and [AutomationEditor.h:78](include/AutomationEditor.h:78) returns `m_clip`. The two calls cannot disagree about whether a clip is present.
- A clip that is deleted while open clears the editor's clip. The window connects the clip's `destroyed()` signal to `clearCurrentClip()` at [AutomationEditor.cpp:2246](src/gui/editors/AutomationEditor.cpp:2246), and `clearCurrentClip()` sets `m_clip = nullptr` at [AutomationEditor.cpp:2306-2309](src/gui/editors/AutomationEditor.cpp:2306). So "no clip being edited" is a reachable state, and it is the state the line-90 guard handles.
- `movePoint` is also null-safe for its own clip argument: [AutomationPointEdit.cpp:47-49](src/gui/editors/AutomationPointEdit.cpp:47) (`controlOf`) returns nullptr for a null clip, and `movePoint` tests the model before using the clip.
- Nothing can clear the clip between line 90 and line 101. The only callers are the GUI-thread list panel ([ItemListPanel.cpp:218](src/gui/ItemListPanel.cpp:218) and [ItemListPanel.cpp:416](src/gui/ItemListPanel.cpp:416)). The one call inside `movePoint` that leaves the editor, `automation.move_point` through `moveByCommand`, is handled in [ControlCommandsAutomationTools.cpp:149-165](src/core/ControlCommandsAutomationTools.cpp:149), which never touches `AutomationEditor`. The grep for `AutomationEditor|setCurrentClip|clearCurrentClip|setKeyPoint` in `ControlCommandsAutomationTools.cpp` and `ControlCommandsAutomationEdit.cpp` returned nothing.

Non-crash gap, not fixed and out of scope for a crash fix: when the clip is null, `setValue` returns false at line 90 without calling `refuse()`. The status line then reads `Not done: ` followed by `lastError()`. `lastError()` (`ItemList.h:113`) is only cleared by a successful `run()` ([ItemListPanel.cpp:147](src/gui/ItemListPanel.cpp:147)), so the reason shown can be a stale one from an earlier refusal. Adding `refuse(tr(...))` on line 90 would change user-visible text, and it needs a GUI-level test that the current test suite does not have. This should go to the owner as a separate item rather than be bundled into this verdict.

No regression test was added. Under procedure step 2 a not-real finding skips to step 6, and no product file was changed, so there is no RED or GREEN run and no step 4b patch.

Commands run (log folder: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-124.r1/logs`):

| Evidence | Command | Result |
| --- | --- | --- |
| Source evidence (rg for the guard, rowCount, the editedClip/currentClip accessors, controlOf, destroyed connection, setValue callers) | `rg -n ...` block, output to `evidence-1.log` (wrapped in a `{ ...; } > evidence-1.log 2>&1` group) | EXIT=0; each rg line EXIT=0 |
| Gate: fork-sources | `bash tests/fork-sources-gate.sh > gates-direct-1-fork-sources.log 2>&1` | EXIT=0 |
| Gate: all-sources-reproduce | `bash tests/all-sources-reproduce.sh > gates-direct-1-all-sources-reproduce.log 2>&1` | EXIT=0 |
| Gate: no-upstream-regression | `bash tests/no-upstream-regression-gate.sh > gates-direct-1-no-upstream-regression.log 2>&1` | EXIT=0 |
| Gate: check-namespace | `tests/scripted/check-namespace > gates-direct-1-check-namespace.log 2>&1` | EXIT=0 |
| Gate: no-tautology | `bash tests/no-tautology-gate.sh > gates-direct-1-no-tautology.log 2>&1` | EXIT=0 |
| Gate: doc-figures | `python3 tests/doc-figures-gate.py > gates-direct-1-doc-figures.log 2>&1` | EXIT=0 |
| Ratchet: complexity | `bash tests/complexity-gate.sh --check > gates-direct-1-complexity.log 2>&1` | EXIT=1; 7 REGRESSION lines, none in `ItemListAutomationSource.cpp` or any file this branch changes (the only change is this report) |
| Ratchet: file-length | `bash tests/file-length-gate.sh --check > gates-direct-1-file-length.log 2>&1` | EXIT=1; 19 REGRESSION lines, none in a file this branch changes |
| Feature check (wrapper) | `python3 .../feature_check.py <worktree> > gates-feature-check-1.log 2>&1` | EXIT=1, but the failure is in the wrapper, not in a gate: `PermissionError` creating `<worktree>-feature-check/`, a sibling path outside the write-jail. The gates were run individually above, which is the same set of commands the wrapper runs. |

The ratchet rows are red on the train by design. With only this report changed, neither ratchet can show a new item in a changed file, so they meet the wrapper's "no new item in your files" rule.

Files committed for this verdict: `docs/qwen-fixes-2026-10/cluster-new-p23-124.md`. No product file changed.

## Self-check

- **Gates (feature_check.py).** The stock wrapper cannot run in this write-jail: it creates `<worktree>-feature-check/` as a sibling of the worktree and gets `PermissionError` (`gates-feature-check-1.log`, EXIT=1). Its own logic was then run from a copy with only two paths changed, `ROOT` (read-only use of the existing `gate-baselines` cache and `FIX-BASE`) and the log folder (into the LOG FOLDER). Command: `python3 -I <LOG>/feature_check_logredirect.py <worktree>` → `gates-feature-check-2.log`, EXIT=0, table: all eight rows PASS, `RESULT: PASS`. The base for the ratchet comparison is `cf3618e61` (merge-base with `050/m0-train-17`), and its cached listing was read, not recomputed.
- **Regression test FAILS with the change reverted, PASSES with it:** not applicable. There is no product change, so there is nothing to revert (step 4b skipped under step 2). No RED or GREEN run exists for this verdict.
- **Deterministic test:** not applicable, no test was added.
- **Fix at the cause, every caller/sibling checked:** not applicable, no fix. The callers that matter for the verdict are the two `setValue` call sites (`ItemListPanel.cpp:218`, `:416`), both on the GUI thread, and `refuse()` is not reached on the null path.
- **No collateral damage:** not applicable, no product or saved-data change.
- **Evidence for FALSE-POSITIVE:** `ItemListAutomationSource.cpp:90` (row guard) with `:72` (`rowCount()` is 0 for a null clip) and `:130` (`clip()` null-check); `AutomationEditor.h:78` and `:94` (`currentClip()` and `editedClip()` are the same `m_clip`); `AutomationEditor.cpp:2246` and `:2306-2309` (clip destroyed → null); `AutomationPointEdit.cpp:47-49` (`controlOf` is null-safe); `ControlCommandsAutomationTools.cpp:149-165` (the command path does not touch the editor).
- **Working tree:** clean after the verdict commit; `AGENTS.md` is not present at this worktree root (`wc -l AGENTS.md`, EXIT=1), so the brief's condensed rules were used.
