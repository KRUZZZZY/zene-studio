# Cluster new-p21-4: frame-exact split dereferences a possibly null right clip

Verdict: **FALSE-POSITIVE**

The finding says `arrangement::splitAtFrame` can return `nullptr` after `canSplitAtFrame` succeeded. On this branch that cannot happen. `splitAtFrame` returns `nullptr` only when its own `canSplitAtFrame` check fails, and the caller has already run that same check on the same clip and frame. The one other source of a null `right`, `clone()`, is `new SampleClip(*this)`, which throws on allocation failure instead of returning null.

## Evidence

- The caller checks first. `src/core/ControlCommandsClipFrames.cpp:127-129` runs `frameEditable` and then `canSplitAtFrame(*clip, frame, &why)` and refuses on failure. `frame` is computed once at `:128`.
- Between that check and the split, nothing changes the clip. `:131` is `ref.track->addJournalCheckPoint()`, which only records undo state (`src/core/JournallingObject.cpp:70-74` journals and does not move the clip).
- `splitAtFrame` gates on the same predicate. `src/core/SampleFrameEdits.cpp:198-200`: `if (!canSplitAtFrame(*clip, frame, why)) { return nullptr; }`. `canSplitAtFrame` (`:154-157`) is `frameEditable(clip, why) && splitFits(clip, frame, why)`. Both are pure reads of the clip (`splitFits` at `:110-127` reads the sample window, `tickOfFrame` at `:48-53`, and the clip's start and end ticks), so the same inputs give the same answer.
- `clone()` cannot return null. `include/SampleClip.h:259-262` is `SampleClip* clone() override { return new SampleClip(*this); }`. `src/core/SampleFrameEdits.cpp:208` then does `dynamic_cast<SampleClip*>` on that result, which always succeeds for a `SampleClip`.
- No global allocator override turns allocation failure into null: `rg -n "operator new|nothrow" src include` finds no `operator new` replacement, so `new` throws `std::bad_alloc`.
- `splitAtFrame` has two callers. `src/core/ControlCommandsClipFrames.cpp:132` (the command, which the check above guards) and `src/core/SampleFrameEdits.cpp:251`, which tests `!= nullptr` before using the result. Neither caller needs a change.

So the failure scenario in the finding ("`splitAtFrame` returns nullptr despite `canSplitAtFrame` succeeding") has no reachable path. A null check at `:133` could never be exercised, so no test could fail without it. I did not add one. Adding a defensive check, or a test seam to force a null, would be a guess at a behaviour change, and the procedure rules that out for a finding that is not real. If `splitAtFrame` ever gains a null-returning path that is not covered by `canSplitAtFrame`, the guard belongs at `:132-133` and the test should drive that path through a seam.

No product file was changed. No RED/GREEN regression was written, because the finding's failure is unreachable and step 2 sends a not-real finding straight to the report.

## Commands run (worktree `zene-qwen-new-p21-4`, logs in `runs/new-p21-4.r1/logs`)

| Step | Exact command | Exit | Log / result |
| --- | --- | ---: | --- |
| Feature gate table | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 1 | `gate-1.log`. Crashed before any gate ran: `PermissionError` creating `<worktree>-feature-check` next to the worktree, which the write-jail refuses. No table was produced. |
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 | `gate-2-fork-sources.log` |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 | `gate-2-all-sources.log` |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 | `gate-2-no-upstream.log` |
| check-namespace | `tests/scripted/check-namespace` | 0 | `gate-2-check-namespace.log` |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 | `gate-2-no-tautology.log` |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 | `gate-2-doc-figures.log` |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check` | 1 | `gate-2-complexity.log`. Six `REGRESSION:` lines, none in a file this branch changes. Red on the train as expected. |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check` | 1 | `gate-2-file-length.log`. 23 `REGRESSION:` lines, none in a file this branch changes. Red on the train as expected. |

`feature_check.py` only fails a ratchet on an item in a file the branch changed. This branch changes only `docs/qwen-fixes-2026-10/cluster-new-p21-4.md`, and no `REGRESSION:` line names it, so both ratchets pass under that rule. The feature table itself could not be produced, so this is a manual equivalent, not the table.

No build or ctest was run. Nothing was built, since no product code changed.

## Self-check

`submit_check.py` was run before this report was committed (`runs/new-p21-4.r1/logs/submit-1.log`, exit 1). Its result:

- PASS: report present; tests added or changed (none: claim-only closure).
- FAIL: commits on the branch, and working tree clean. Both clear once this report is committed.
- FAIL: gates (`feature_check.py`). It crashes with `PermissionError` creating `<worktree>-feature-check` outside the worktree, which the write-jail forbids. The eight gate scripts it runs were run individually (table above): six exit 0; complexity and file-length exit 1 with regression lines in no file this branch changes.

The four "?" items, answered:

- RED/GREEN with the product change reverted: not applicable. There is no product change, and the finding is not reachable, so no test can fail on the current code.
- Determinism: not applicable. No test was added.
- Fix at the cause and every caller: not applicable as a fix. The two callers of `splitAtFrame` were checked (`src/core/ControlCommandsClipFrames.cpp:132`, `src/core/SampleFrameEdits.cpp:251`) and both are consistent with the current contract.
- Collateral damage: no code changed, so project loading and saved data are unaffected.
- FALSE-POSITIVE evidence: `src/core/ControlCommandsClipFrames.cpp:127-129` (check before split), `src/core/SampleFrameEdits.cpp:198-200` and `:154-157` (`splitAtFrame` returns null only when `canSplitAtFrame` fails), `include/SampleClip.h:259-262` (`clone()` is `new`, which throws rather than returning null), and `src/core/JournallingObject.cpp:70-74` (the checkpoint does not change the clip).

Files committed:

- `docs/qwen-fixes-2026-10/cluster-new-p21-4.md`
