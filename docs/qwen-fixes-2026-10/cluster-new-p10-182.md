FALSE-POSITIVE: cluster new-p10-182 (BUG 14). On this branch a negative note repeat count cannot reach the division by zero at `src/tracks/InstrumentTrackNotes.cpp:284`; both the project-load path and the command path clamp or refuse it.

# Cluster new-p10-182 — negative repeat count divides by zero

## Finding (as reported)

`src/tracks/InstrumentTrackNotes.cpp:203` — "if project data supplies `repeats == -1`, the computed hit count is zero and the next line divides `frames` by zero."

Line 203 is now a comment inside `InstrumentTrack::noteIsAlreadySounding` (`src/tracks/InstrumentTrackNotes.cpp:199-216`). The arithmetic the finding describes is at lines 283-284.

## Evidence

1. **The division and its divisor.** `src/tracks/InstrumentTrackNotes.cpp:283-284`:
   `hits = frames > 0 ? min(repeats + 1, frames) : 1` and `each = frames / hits`.
   A zero divisor needs `hits == 0`, which with `frames > 0` needs `repeats + 1 <= 0`, i.e. `repeats <= -1`.

2. **Project load clamps the value.** `src/core/NoteConditions.cpp:62-65`:
   `number("rep", MaxRepeats)` returns `std::clamp(attribute.toInt(), 0, MaxRepeats)` with `MaxRepeats = 16` (`include/NoteConditions.h:48`).
   A saved `rep="-1"` loads as 0. Blame shows lines 62-65 come from `c414ab31d4`, the commit that created the file. The file's later commits (`a5b2cb3700`, `bd1efa81a5`) do not touch this clamp. So no version of this file has loaded a negative repeat count.

3. **The command path refuses it.** `src/core/ControlCommandsNoteEdit.cpp:82` gives `repeats` the closed range `0..MaxRepeats`. `checkFieldValue` (`:130-133`) refuses out-of-range values, and `checkEditValues` (`:138-141`) runs before any write. `conditionsAfter` (`:296-306`) only applies values that passed that check.

4. **The lane UI is bounded.** `src/gui/noteeditor/NoteLanes.cpp:63` gives the Repeats lane the range 0..16, and its writes go through `noteEdit` (`:123`), i.e. the same command path as item 3.

5. **No other writer.** A search of `src/`, `include/` and `tests/` for writes to `repeats` finds only: the field default (`include/NoteConditions.h:52`), the load clamp (`src/core/NoteConditions.cpp:65`), and `take("repeats", ...)` in `conditionsAfter` (`src/core/ControlCommandsNoteEdit.cpp:303`). The other `setConditions` callers copy the existing conditions and change only `lyric` (`src/core/ControlCommandsNotePlayback.cpp:160`), `group` (`:77`) or `articulation` (`src/core/ArticulationRemap.cpp:111`).

6. **The hit count is at least one for every value the code can hold.** For `repeats` in `0..16` and `frames >= 1`, `min(repeats + 1, frames) >= 1`. `frames == 0` gives `hits = 1` explicitly (`:283`). Line 284 therefore never divides by zero for any value that load, the command path or the UI can produce.

## Limit of this verdict

The guard at line 283 relies on the load clamp and the command validation, not on itself. A future in-process caller that calls `Note::setConditions` with a negative `repeats` would still reach line 284. No production caller does this today. I did not add a defensive `std::max(1, ...)` there: the finding is not reproducible, and the procedure says a not-real finding gets no product change. That hardening is a one-line option if the owner wants it, as a separate change.

## Regression test

None added. The verdict is not a fix, so there is no RED/GREEN pair (step 4b does not apply). I did not run a runtime reproduction of a `rep="-1"` load. The verdict rests on the code citations above, which were read directly.

## Commands and results

| Command | Result |
| --- | --- |
| `rg -n "repeats" src/ include/` and `rg -n "\brepeats\b\s*=[^=]\|\.repeats\b" src include tests` | read-only searches, used for the writer list in item 5 |
| `git blame -L 62,66 -- src/core/NoteConditions.cpp`; `git log --follow -- src/core/NoteConditions.cpp`; `git log -S'number("rep", MaxRepeats)'` | clamp introduced in `c414ab31d4`; file has 3 commits |
| `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` (logs: `…/logs/feature-check-1.log`) | **EXIT=1**. The script failed before running any gate: `PermissionError` creating `zene-qwen-new-p10-182-feature-check` next to the worktree, outside the write-jail. |
| `python3 $TMPDIR/new-p10-182-gate/feature_check.py <worktree>` (logs: `…/logs/feature-check-2.log`) | **EXIT=0**, `RESULT: PASS`. This is a copy of the same script. The only changes are `ROOT` pointing at the real `qwen/` folder (so baselines and `FIX-BASE` are read from it) and the log directory redirected into the LOG FOLDER. `diff` shows exactly those two lines. |

The table of gate results from the passing run:

| Gate | Result |
| --- | --- |
| fork-sources | PASS (exit 0) |
| all-sources-reproduce | PASS (exit 0) |
| no-upstream-regression | PASS (exit 0) |
| check-namespace | PASS (exit 0) |
| no-tautology | PASS (exit 0) |
| doc-figures | PASS (exit 0) |
| complexity | PASS (no new item in changed files) |
| file-length | PASS (no new item in changed files) |

Affected test family: **0 tests run**. No product file changed, so no build or ctest was run.

Files committed: `docs/qwen-fixes-2026-10/cluster-new-p10-182.md` only.
