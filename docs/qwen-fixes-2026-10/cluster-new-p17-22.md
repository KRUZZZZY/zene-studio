Verdict: FIXED

new-p17-22: FIXED (real defect, reachable; the finding's literal trigger does not occur)

## Decision

The finding says `range.delete` with ripple `All` shifts every track when the range covers no track. That part is real:
the case is reachable, and it is not the `all: false` + empty `tracks` case the finding names.

- The finding's trigger does not occur. `setRange` passes `all` straight through, but `ArrangerRange::set` turns an empty
  track list into all-tracks: `src/core/ArrangementEdits.cpp:537` (`m_allTracks = allTracks || trackIds.isEmpty();`). So
  `range.set {all:false, tracks:[]}` covers every track and `rippledOthers` returns nothing. (Read from the code; not run.)
- The reachable trigger: a range keeps the ids of the tracks it was set on. If one of them is then removed
  (`track.remove`), `ArrangerRange::tracks()` (`src/core/ArrangementEdits.cpp:522-529`) matches no track, so the covered
  set is empty. `rippledOthers(range.tracks())` (`src/core/ControlCommandsRange.cpp:253`) then returns every remaining
  song track as an "other", and `rippleOthers` shifts all of them left by the range length (`:237-244`) although
  `deleteRange` removes nothing (`removed: 0`).
- Demonstrated by the regression test below: after `track.remove` on the range's only track, `range.delete` with
  ripple `all` moved the other track's clip from 3 bars (576 ticks) to 2 bars (384 ticks) with `removed` = 0.

## Fix

`src/core/ControlCommandsRange.cpp:228-231`, in `rippledOthers`:

```cpp
if (arrangement::modes().ripple != arrangement::Ripple::All) { return others; }
// No covered track (its track was removed since range.set): nothing is cut, so no gap to close.
if (covered.isEmpty()) { return others; }
```

Chosen over refusing the delete: with no covered track the delete already removes nothing, and every other range edit
(split, crop, copy, insert) already succeeds on an empty cover. Refusing would change those callers' behaviour. Only the
ripple is suppressed. The lock check and the undo checkpoint (`beginEdit`) are unchanged. `rippleOthers` has no other
caller; `rippledOthers` is used only by `deleteRange` (`src/core/ControlCommandsRange.cpp:253`).

Siblings checked: `arrangement::deleteRange` (`src/core/ArrangementEdits.cpp:152`) ripples only the track it is given
and is never called with an empty cover; `ArrangerRange::tracks()` is the only producer of the cover. No command was
added; the ControlCommandsSnapshot canary is untouched.

## Regression test

`tests/src/core/RangeEditsTest.cpp`: `rippleAllOverANoTrackRangeShiftsNothing()` (after `deleteWithAndWithoutRipple`),
plus a `cleanup()` slot that restores the arranger modes (`m_modes`, saved in `init()`), so the ripple mode cannot leak
into other cases. Deterministic: no timing, no sleeps.

Test matrix: normal ripple (`deleteWithAndWithoutRipple`, existing, still passes); empty-cover ripple all (new);
ripple off/track (existing); undo of delete (existing `control.undo` cases); realtime — not applicable (command-thread
edit only); build target `RangeEditsTest`, plus `ArrangementModesTest` for the ripple-mode contract.

## Evidence (every command run from the worktree unless noted; logs in the run folder `logs/`)

| # | Step | Command | EXIT | Result |
|---|---|---|---|---|
| 1 | RED build (env issue) | `cmake --build build --target RangeEditsTest -j4 > logs/build-1.log 2>&1` | 2 | ccache temp dir not writable (`ccache: error: failed to create temporary file ... Permission denied`); not a code error |
| 2 | RED build | `CCACHE_TEMPDIR=$TMPDIR/ccache-tmp-p17 cmake --build build --target RangeEditsTest -j4 > logs/build-2.log 2>&1` | 0 | builds |
| 3 | RED run (test change only) | `cd build/tests && ctest -R RangeEditsTest --output-on-failure > logs/test-red-1.log 2>&1` | 8 | `14 passed, 1 failed`: `FAIL! RangeEditsTest::rippleAllOverANoTrackRangeShiftsNothing() Compared lists differ at index 0. Actual (spans(other)): (384, 576) Expected: (576, 768)` |
| 4 | GREEN build | `CCACHE_TEMPDIR=$TMPDIR/ccache-tmp-p17 cmake --build build --target RangeEditsTest zene -j4 > logs/build-3.log 2>&1` | 0 | builds |
| 5 | GREEN run | `cd build/tests && ctest -R RangeEditsTest --output-on-failure > logs/test-green-1.log 2>&1` | 0 | 100% passed |
| 6 | Proof: save product diff | `git diff -- src/core/ControlCommandsRange.cpp > logs/fix.patch` | 0 | 2-line patch |
| 7 | Proof: revert fix | `git apply -R logs/fix.patch` | 0 | |
| 8 | Proof: rebuild without fix | `CCACHE_TEMPDIR=... cmake --build build --target RangeEditsTest zene -j4 > logs/build-4b-revert.log 2>&1` | 0 | |
| 9 | Proof: run without fix | `cd build/tests && ctest -R RangeEditsTest -V > logs/test-4b-revert.log 2>&1` | 8 | `14 passed, 1 failed`: `FAIL! ... rippleAllOverANoTrackRangeShiftsNothing()` (same assertion as step 3) |
| 10 | Proof: re-apply fix | `git apply logs/fix.patch` | 0 | |
| 11 | Proof: rebuild with fix | `CCACHE_TEMPDIR=... cmake --build build --target RangeEditsTest zene -j4 > logs/build-4b-fix.log 2>&1` | 0 | |
| 12 | Proof: run with fix | `cd build/tests && ctest -R RangeEditsTest -V > logs/test-4b-fix.log 2>&1` | 0 | `15 passed, 0 failed` |
| 13 | Family build | `CCACHE_TEMPDIR=... cmake --build build --target ArrangementModesTest -j4 > logs/build-5.log 2>&1` | 0 | |
| 14 | Family run | `cd build/tests && ctest -R "RangeEditsTest\|ArrangementModesTest" -V > logs/test-family-1.log 2>&1` | 0 | RangeEditsTest 15 passed, 0 failed; ArrangementModesTest 18 passed, 0 failed; 2/2 tests ran |

Affected-family test count: 2 tests (`RangeEditsTest`, `ArrangementModesTest`), 33 cases executed, 0 failed.

## Gates

`feature_check.py` itself could not run here: it writes its per-gate logs to
`<worktree parent>/<worktree name>-feature-check/` (`feature_check.py:71`, `logs.mkdir`), which is outside this
worktree's write jail. Run as-is it exits 1 with `PermissionError` (`logs/gates-1.log`). I did not create that folder
or the shared `zene-cachyos` baseline checkout it would use. Instead I ran each of its gate commands from the worktree,
with the same arguments:

| Gate | Command | EXIT | Log |
|---|---|---|---|
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 | logs/gate-1.log |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 | logs/gate-2.log |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 | logs/gate-3.log |
| check-namespace | `tests/scripted/check-namespace` | 0 | logs/gate-4.log |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 | logs/gate-5.log |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 | logs/gate-6.log |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check` | 1 | logs/gate-7.log: 67 grandfathered functions over CCN 10, none in `ControlCommandsRange.cpp` or `RangeEditsTest.cpp` |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check` | 1 | logs/gate-8.log: 23 REGRESSION lines, none in the two changed files |

The two ratchets exit 1 because of the train's existing red items. By the script's rule (`feature_check.py`
`mine = ... item_file(l) in changed`), they count as PASS when no new item is in a changed file; that holds here. The
script's own `RESULT` line was therefore not produced; the per-gate rows above are the equivalent evidence.

Note: a bare `bash -c`-style `$c` loop failed in zsh (no word splitting). Those 127 exits (`logs/gate-*.log` before
the re-run) are not gate results; they were overwritten by the explicit re-run above.

## Files committed

- `src/core/ControlCommandsRange.cpp`
- `tests/src/core/RangeEditsTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p17-22.md`

No new file, so no registration change. `RangeEditsTest.cpp` is already in `tests/fork-sources.txt`.

## Self-check

- **Regression test fails with the product change reverted and passes with it:** yes. Reverted: step 9, `ctest` EXIT=8,
  `14 passed, 1 failed`, `rippleAllOverANoTrackRangeShiftsNothing` (logs/test-4b-revert.log). Re-applied: step 12,
  EXIT=0, `15 passed, 0 failed` (logs/test-4b-fix.log). Step 3 (RED, test-only change) and step 5 (GREEN) match.
- **Deterministic:** yes. The test uses fixed tick arithmetic (`Bar` = 192 ticks), sets the ripple mode explicitly and
  removes the range's track with the existing `track.remove` command. No timing, sleeps, threads or seams needed.
- **Fix at the cause and complete:** `rippledOthers` is the only producer of the "others" set and is called only from
  `deleteRange` (`src/core/ControlCommandsRange.cpp:253`); `rippleOthers` only from `deleteRange`
  (`src/core/ControlCommandsRange.cpp:256`). The cover comes only from `ArrangerRange::tracks()`
  (`src/core/ArrangementEdits.cpp:522-529`). The other range edits (split, crop, copy, paste, insert) already do nothing
  on an empty cover and are unchanged. The clip-level ripples (`rippleTracks`, `src/core/ArrangementEdits.cpp:217-224`)
  always start from a real clip's track, so they never see an empty cover.
- **No collateral damage:** the only behaviour change is an empty cover under ripple `All`, which previously shifted
  every track with nothing cut. No save format, no command, no limit or check is added; a valid range keeps its
  covered set and ripple exactly as before. Existing `RangeEditsTest` and `ArrangementModesTest` cases all pass
  (steps 12 and 14).
- **Verdict basis:** the verdict is FIXED, not ALREADY-FIXED or FALSE-POSITIVE, so no "already fixed" evidence is
  claimed. The finding's literal trigger (`all: false` with no tracks) is shown above not to occur (`ArrangementEdits.cpp:537`).
- **Realtime:** not applicable. The change is on the command thread and touches no audio path.
- **Sibling commands (`arrangement.*`):** none is in a file this commit changes. Those commands are registered in
  the GUI and arrangement command files (for example `tests/src/gui/ArrangerTimelineTest.cpp` and
  `src/gui/PanelShortcuts.cpp`), not in `src/core/ControlCommandsRange.cpp`. `rippledOthers` and `rippleOthers` sit in
  that file's anonymous namespace (`src/core/ControlCommandsRange.cpp:49` on), so no other translation unit can call
  them. `arrangement.modes` only sets the ripple mode this fix reads; its behaviour is unchanged (ArrangementModesTest,
  18/18, step 14).
- **Gates via `submit_check.py`:** still FAIL, for an environment reason. That check runs `feature_check.py`, which
  creates `<worktree parent>/<name>-feature-check/` outside the write jail and exits 1 with `PermissionError`. The
  eight gate commands it runs were run individually from the worktree (table above): six exit 0, and the two
  ratchets' only REGRESSION lines are in files this commit does not change. The gate itself is not satisfied by
  this report; a run from outside the jail is needed to get its single table.
