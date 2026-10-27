FALSE-POSITIVE

# Cluster new-p10-59: `range.set` accepts duplicate track ids

Finding: `src/core/ControlCommandsRange.cpp:137-149` (`setRange`) appends every resolved track id without a duplicate check. The claim is that a range which keeps duplicates makes range edits process the same track more than once, so a ripple delete on `tracks: ["trk-1", "trk-1"]` would delete and shift the track twice.

The handler does keep duplicates, but the range model never turns them into a second pass over the same track.

## Evidence (file:line, branch `bugs/qwen-new-p10-59` at base `5c1c6715d9`)

1. **The handler keeps duplicates.** `src/core/ControlCommandsRange.cpp:145` (`ids.append(track->id())`, no dedup) and `:147` pass the list to `ArrangerRange::set`.
2. **The stored raw list is not the track set.** `include/ArrangementEdits.h:226` (`m_trackIds`) and `src/core/ArrangementEdits.cpp:536` store it unchanged.
3. **`ArrangerRange::tracks()` dedups by construction.** `src/core/ArrangementEdits.cpp:522-529` walks `Engine::getSong()->tracks()` once and appends each song track at most once. The id list is only a membership test (`:527`), so duplicate ids cannot produce a second entry for one track.
4. **Every per-track edit takes its tracks from `tracks()`.**
   - `beginEdit`: `src/core/ControlCommandsRange.cpp:113` (`*tracks = range.tracks();`).
   - `deleteRange`: the ripple covered set at `:251` and the per-track loop at `:258`.
   - `cropToRange`: the loop at `:269`.
   - `rippledOthers`: `src/core/ControlCommandsRange.cpp:225-233`. "Others" are song tracks not in `covered` (`:231`), where `covered` is `range.tracks()` (`:251`).
   Each track is deleted and shifted once.
5. **The reply is deduped too.** `rangeJson` (`src/core/ControlCommandsRange.cpp:71-78`) lists `range.tracks()`, so the response has no duplicate ids.
6. **The raw `trackIds()` accessor (`include/ArrangementEdits.h:214`) has no per-track consumer.**
   - `duplicateRange` re-sets the same raw list (`src/core/ControlCommandsRange.cpp:357`).
   - The render-bounce cache stores it (`src/core/ControlCommandsRenderBounce.cpp:388`) and compares it raw-to-raw (`:132`). Both sides keep duplicates, so the comparison stays consistent.

The only observable effect is that `ArrangerRange::trackIds()` can hold a duplicate id. None of the failure modes in the finding (double delete, double shift, double edit) can happen.

## Decision

FALSE-POSITIVE. No product change is made, so there is no defect to regress. A test that pins dedup would pass on the unchanged code and would not prove a fix, so none is added. The finding's `reported_times: 2` does not change the code evidence above.

## Commands run and exit codes

Gate run (the brief's `feature_check.py` could not run as written; see the first row):

| # | Command (run from the worktree) | EXIT | Log |
|---|---|---|---|
| 1 | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 1 | `logs/gate-feature-1.log` |
| 2 | `bash tests/fork-sources-gate.sh` | 0 | `logs/gate-fork-sources-2.log` |
| 3 | `bash tests/all-sources-reproduce.sh` | 0 | `logs/gate-all-sources-reproduce-2.log` |
| 4 | `bash tests/no-upstream-regression-gate.sh` | 0 | `logs/gate-no-upstream-regression-2.log` |
| 5 | `tests/scripted/check-namespace` | 0 | `logs/gate-check-namespace-2.log` |
| 6 | `bash tests/no-tautology-gate.sh` | 0 | `logs/gate-no-tautology-2.log` |
| 7 | `python3 tests/doc-figures-gate.py` | 0 | `logs/gate-doc-figures-2.log` |
| 8 | `bash tests/complexity-gate.sh --check` | 1 | `logs/gate-complexity-2.log` |
| 9 | `bash tests/file-length-gate.sh --check` | 1 | `logs/gate-file-length-2.log` |

- Row 1: `feature_check.py` stops with `PermissionError` at `logs.mkdir` for `<worktree>/../zene-qwen-new-p10-59-feature-check`. That path is outside this lane's write-jail, and the script writes its per-gate logs there. The script is shared, so I did not change it. Rows 2-9 run the same commands the script lists.
- Rows 8-9 are red on the train by design. Feature-check only fails on items in files the branch changes. The branch changes no product file, and the only new file is this report. `rg "ControlCommandsRange|ArrangementEdits"` over `gate-complexity-2.log` and `gate-file-length-2.log` finds nothing, so no reported item is in a changed file. This is my reading of the script's rule, not a run of its table.
- Doc-figures (row 7) was run before this report was written; it is re-run after the report is added (see the Self-check section).

## Files committed

- `docs/qwen-fixes-2026-10/cluster-new-p10-59.md` (this report).

## Self-check

Filled in after `submit_check.py` ran (see below).
