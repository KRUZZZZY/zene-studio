FIXED

# Cluster new-p10-49: clip.quantize can move a clip past the song limit or overflow tick_t

Finding (Luna patrol): `src/core/ControlCommandsNoteHarmony.cpp` quantizes clip starts with
`(start + grid / 2) / grid * grid` in `tick_t` (int32) and never checks the result against `MaxSongLength`.

## Evidence (verified by reading and by running the test)

- The rounding was in `tick_t`: the original `snapped` lambda at `src/core/ControlCommandsNoteHarmony.cpp:130`
  (HEAD `5c1c6715d9`), and the original move at `:143`. `tick_t` is `std::int32_t` (`include/LmmsTypes.h:35`).
- `MaxSongLength = 9999 * DefaultTicksPerBar` = 1,919,808 (`include/Song.h:81`). Nothing on the snap path checks it.
- The grid is schema-bounded to 1..49152 (`ControlCommandsNoteHarmony.cpp:210` at the fixed tree), so the half-grid
  addition can only overflow for a start within 24576 of `INT_MAX`.
- Reachability:
  - Past the limit, through commands: `clip.move` accepts `position` up to `MaxSongLength`
    (`src/core/ControlCommandsClip.cpp:184`), and `Clip::movePosition` only clamps below zero (`src/core/Clip.cpp:282`).
    A clip at 1919807 with grid 1000 snaps to 1920000, which is past the limit. This is the case the test reproduces.
  - Overflow: needs a start within grid/2 of `INT_MAX`. No command can place a clip there (`clip.move` is bounded),
    so it is reached only from a hand-edited or corrupt project file (`src/tracks/MidiClip.cpp:518-520` loads `pos`
    with `toInt` and guards only `>= 0`).
    It is real but not command-reachable.
- Sibling, already correct: `note.repeat` refuses a copy past the limit with `InvalidArgs` and "past the song limit"
  (`src/core/ControlCommandsNoteTools.cpp:280-284`). The fix follows that contract.
- Other snap sites (`src/core/NoteTransform.cpp:270,298`, `src/core/ScoreExport.cpp:44`,
  `src/core/ControlCommandsGrooveEdit.cpp:306`) work on note ticks, which are bounded near 1.9e6 and cannot overflow
  there. They are not changed. Not verified by a test.

## Decision

- Refuse the whole batch, `InvalidArgs`, "past the song limit of 1919808", before any clip is captured or moved.
  Chosen over clamping: a clamp would move a clip to a position the caller did not ask for and make the reply lie
  about "moved". Refusing matches `note.repeat`, and a refused batch takes no undo step.
- The check runs only when a clip would actually move (`target != start`). A clip that the snap leaves in place
  is not checked, so an already-invalid clip that is not moved is not reported.
- The snap is computed in `qint64` and narrowed to `tick_t` only after the limit check, so the narrowing is safe.
- A snap that lands exactly on `MaxSongLength` is still allowed (test 3 below).
- Behaviour change for a clip already past the limit that the snap moves but leaves past the limit: it is now refused.
  The clip was already invalid, and quantize should not write a new invalid position. No command, reply field or
  snapshot changes (no new control command; the canary is untouched).

## Change

- `src/core/ControlCommandsNoteHarmony.cpp`: `qint64` snap, limit check before capture/move, explicit `tick_t`
  narrowing at the move, `#include "Song.h"` for `MaxSongLength`.
- `tests/src/core/NoteCommandTransactionsTest.cpp`: three slots, `quantizingPastTheSongLimitIsRefusedAndMovesNothing`,
  `quantizingNearTickMaxIsRefusedNotWrapped`, `quantizingOntoTheSongLimitIsAllowed`; plus `#include <limits>`.
  The file already tests `clip.quantize` (its undo and transaction record), and it is registered already.
  The first attempt put the test in `tests/src/core/NoteToolsTest.cpp`; that pushed the file to 511 code lines
  (HEAD 489) and the file-length gate failed (`gate-1` in the run folder). It was moved and the file restored to HEAD.

## Commands and exit codes

Run folder: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-49.r1/logs` (abbreviated below as LOGS).
Every build used `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/zene-tmp`: the default ccache temp directory is outside the
write-jail (the first build attempt, `build-1-red.log`, exited 2 on `ccache: ... Permission denied`).

| Step | Command (run from the worktree / `build/tests`) | Exit | Log |
|---|---|---|---|
| RED, test first, on the pre-fix product | `cmake --build build --target NoteToolsTest -j4` (first test location; superseded) | 0 | `build-2-red.log` |
| RED (first location) | `ctest -R NoteToolsTest --output-on-failure` | 8 | `test-1-red.log`: `quantizingPastTheSongLimit...` errorKind 0, expected 3 |
| GREEN (first location) | build + `ctest -R NoteToolsTest` | 0 | `build-3-green.log`, `test-2-green.log` |
| 4b revert | `git diff -- src/core/ControlCommandsNoteHarmony.cpp > LOGS/fix.patch && git apply -R LOGS/fix.patch` | 0 | (fix.patch) |
| 4b RED (first location) | build + `ctest -R NoteToolsTest` | 8 | `test-3-revert-red.log`: same assertion |
| 4b reapply | `git apply LOGS/fix.patch` | 0 | |
| 4b GREEN (first location) | build + `ctest -R NoteToolsTest -V` | 0 | `test-4-reapply-green.log`: 28/28 |
| Move to final location | `git checkout -- tests/src/core/NoteToolsTest.cpp` (restore to HEAD) | 0 | |
| RED, final (fix reverted) | `git apply -R LOGS/fix.patch`; `cmake --build build --target NoteCommandTransactionsTest -j4`; `ctest -R NoteCommandTransactionsTest -V` | 0; 0; **8** | `build-7-red-final.log`, `test-6-red-final.log` |
| RED, final detail | | | `quantizingPastTheSongLimitIsRefusedAndMovesNothing`: errorKind 0, expected 3 (line 165). `quantizingNearTickMaxIsRefusedNotWrapped`: errorKind 0, expected 3 (line 178). `quantizingOntoTheSongLimitIsAllowed`: pass (boundary already allowed). Totals 5 passed, 2 failed |
| GREEN, final (fix reapplied) | `git apply LOGS/fix.patch`; build; `ctest -R NoteCommandTransactionsTest -V` | 0; 0; **0** | `build-8-green-final.log`, `test-7-green-final.log`: 7/7 pass |
| Family: NoteToolsTest (HEAD version) | build; `ctest -R NoteToolsTest -V` | 0; 0 | `build-9-notetools.log`, `test-8-notetools.log`: 27/27 |
| Family list (regex `Harmony\|Quantiz\|NoteCommand\|NoteTools\|ControlEdit`) | `ctest -R ... ` | 8 | `test-9-family.log`: WarpQuantizeTest and ControlEditCommandsTest "Not Run" (not built in this targeted tree) |
| Family: build the two missing | build `ControlEditCommandsTest WarpQuantizeTest` | 0 | `build-10-family-extra.log` |
| Family: WarpQuantizeTest and ControlEditCommandsTest | `ctest -R "ControlEditCommandsTest\|WarpQuantizeTest" -V` | 8 (overall) | `test-10-family-extra.log`: WarpQuantizeTest 9/9 passed; ControlEditCommandsTest 19 passed, 1 failed: `trackSetArmManyReportsOnlyTheTracksItChanged`, "take file /home/kruzzzzy/Documents/Zene Studio/zene-take-trk40.wav could not be opened for writing" |
| ControlEdit failure on HEAD behaviour | revert fix, build `ControlEditCommandsTest`, run; reapply fix | 0; 0; 8; 0 | `test-11-ce-head.log`: the same failure with the fix reverted |

The `ControlEditCommandsTest` failure is environmental and not caused by this change. The take file lives under
`~/Documents/Zene Studio`, outside the write-jail, and it fails the same way with the fix reverted.

Gates:

| Gate run | Command | Exit | Result |
|---|---|---|---|
| `feature_check.py` as specified | `python3 .../feature_check.py <worktree>` | 1 | Cannot run in this jail: `PermissionError` creating `<worktree>-feature-check` next to the worktree (`gate-1-feature-check.log`). The gate's own log folder is outside the write-jail. |
| Same gate sequence, logs in the run folder | `python3 -I LOGS/gate-driver.py <worktree> LOGS/gates-N` (imports `feature_check.py` unchanged) | 1 | `gates-1`: file-length `NEW: new file over 500 lines: tests/src/core/NoteToolsTest.cpp (511)`. Fixed by moving the test. |
| Same, final | | 0 | `gates-2`: all six simple gates PASS, complexity and file-length "no new item in your files", `RESULT: PASS` |

## Self-check

- **RED/GREEN with the change reverted and restored:** the final test fails with the fix reverted (`test-6-red-final.log`, ctest exit 8,
  two slots errorKind 0 where 3 is expected) and passes with it (`test-7-green-final.log`, exit 0, 7/7). The boundary slot passes
  both ways, as intended: it guards that a snap onto the limit is still allowed.
- **Deterministic:** no clocks, sleeps or threads. Positions are set directly on the model with `Clip::movePosition` (a model seam used
  across these tests), and the grid is a fixed argument. Tick extremes come from `std::numeric_limits<tick_t>`, and the limit from
  `MaxSongLength`.
- **Caller and sibling check:** `quantizeClips` is the only handler for `clip.quantize` (`ControlCommandsNoteHarmony.cpp`); its
  `movePosition` call is the only write. The sibling `note.repeat` refusal (`ControlCommandsNoteTools.cpp:280-284`) uses the same
  contract. The other snap sites work on note ticks and are unchanged (see Evidence). Not verified by a test.
- **No collateral damage:** the refusal fires only when a moving clip would land past `MaxSongLength` (`ControlCommandsNoteHarmony.cpp:141`).
  Legal positions, and a snap that lands on the limit, behave as before. Project load is untouched (only the command's arithmetic changed).
  The reply schema and command set are unchanged, so the snapshot canary is not affected. A clip already past the limit that the snap
  leaves in place is not touched.
- **Realtime and undo:** the change is a control command, not an audio-thread path (no allocation or locking added on the
  audio path; the only new work is local integer arithmetic and one `QStringLiteral` error string on the refused branch). The refusal returns before `captureQuantizeBefore`
  and `addUndoStep`, so a refused batch leaves no undo step. The success path's undo record is unchanged.
- **Registration:** no new files. `src/core/ControlCommandsNoteHarmony.cpp` is already in `tests/fork-sources.txt`, and the test file is
  already registered. No upstream-modifications entry is needed (`no-upstream-regression` PASS).
- **Verdict basis:** the bug reproduces through the command path (clip at MaxSongLength-1, grid 1000, accepted with errorKind 0 before
  the fix). The overflow half is real but reachable only from a corrupt project position; the report says so.
