FIXED

# cluster new-p17-140: a tap to a deleted channel silently records nothing

new-p17-140: FIXED

## Decision: REAL

- `recordertaps::Tap::channelId` is the mixer channel's persistent id (`include/RecorderTaps.h:48`). `channelAt()` pushes audio only for a tap whose `channelId == channel.id()` (`src/core/RecorderTaps.cpp`, `channelAt`).
- `MultiTrackRecorder` skips the input path for every route where `audioTapped(route)` is true (`src/core/audio/MultiTrackRecorder.cpp:125`).
- Before this fix, `Mixer::deleteChannel` (`src/core/Mixer.cpp:1385`) cleared VCA groups, track sends, mixer sends, sidechains and solo, but never touched `recordertaps`. A route that tapped the deleted channel therefore stayed "tapped" with no channel left to push into it. It recorded silence, and nothing reported an error.
- `mixer.remove_channel` cannot be undone (`src/core/ControlCommandsMixer.cpp`, transaction `reversible:false`, "no command restores a deleted mixer channel"). New channels get fresh ids from `ProjectIds::allocate()` (`src/core/Mixer.cpp:415`). So the id never comes back, and the tap is dangling for good.
- Another path leads to the same bug: the undo and redo lambdas of `record.route_source` (`src/core/ControlCommandsRecordTools.cpp:254`) call `recordertaps::set(route, before/tap)`. They could put a route back onto a channel that had since been deleted.
- RED proof: `test-8.log` (the unfixed tree) shows `undoingOntoADeletedChannelTapsTheInput` reading `recordertaps::get(0).source` = 2 (Channel) after the deletion and undo. `aTapOnADeletedChannelGoesBackToTheInput` fails because the deletion reports and resets nothing.

## Fix (at the cause)

- `include/RecorderTaps.h` / `src/core/RecorderTaps.cpp`: new `recordertaps::channelDeleted(int channelId)` (UI thread). It resets every *pending* tap on that channel to the default `Tap{}` (Input) and returns how many it reset. `beginPeriod()` publishes the change at the next period boundary and keeps `tappedCount` right, as it does for any `set()`. `set()` now stores `Tap{}` for a `Source::Channel` tap whose channel no longer exists, which closes the undo/redo path. With no mixer (`Engine::mixer() == nullptr`), `set()` keeps the old behaviour.
- `src/core/Mixer.cpp` `Mixer::deleteChannel`: it calls `recordertaps::channelDeleted(ch->id())` before the channel is erased, and when any route was reset it issues a `qWarning`. This one place covers every deletion path: the GUI, `mixer.remove_channel`, `mixer.add_channel`'s undo, and `Mixer::clear()`.
- `src/core/ControlCommandsMixer.cpp` `mixer.remove_channel`: the reply reports the reset routes as `record_routes_reset: [route,...]`, so the user sees the missing source. The key is present only when routes were reset, so other replies are unchanged. It is declared in the command's result schema as an optional array. The command description, args schema and command id set are unchanged. `tests/control-commands-snapshot.py` compares command ids only, so the ControlCommandsSnapshot canary does not move.
- `tests/upstream-modifications.txt`: one line for `src/core/Mixer.cpp` naming this cluster.

Realtime: `channelDeleted` and `set` run on the UI thread. `deleteChannel` runs between `requestChangeInModel()` and `doneChangeInModel()`. Nothing is added to the audio path: `beginPeriod`, `channelAt`, `master` and `audioTapped` are unchanged. Only the UI thread writes the pending table, so the two new writers cannot race the existing one. Local TSan does not run on this box; this paragraph is reasoning about the design, not a TSan result.
Undo: `mixer.remove_channel` stays irreversible, as before. A `record.route_source` undo whose target channel is gone now lands on the input instead of a dead tap. That is still one honest undo step.
Saved data: taps are not persisted, and no load or save code changed.

## Regression tests (`tests/src/core/ControlRecordingRoutesTest.cpp`, ctest `ControlRecordingRoutesTest`)

- `aTapOnADeletedChannelGoesBackToTheInput`:
  1. Add channels A and B, and point route 0 at B with `post_fader` / `left`. After `masterMix`, `audioTapped(0)` is true.
  2. Removing the unrelated channel A leaves the tap on B and adds no `record_routes_reset` to the reply.
  3. Removing B returns `record_routes_reset: [0]`. Afterwards `tapped(0)` is false, `get(0).source` is Input, and `record.route_source` reports `input`.
  4. After the next `masterMix`, `audioTapped(0)` is false, so the input path runs again.
- `undoingOntoADeletedChannelTapsTheInput`: route 0 goes to channel C, then to master. Channel C is deleted through `Mixer::deleteChannel`, because `mixer.remove_channel` is irreversible and would block `control.undo`. After that, `control.undo` leaves route 0 on Input (it was Channel before the fix).
- Deterministic: the test drives the periods itself with the audio device stopped, following the existing `aTapChangeDoesNotFeedOneRouteFromTwoSourcesInAPeriod`. There are no sleeps and no timing.

## Commands and EXIT codes (logs in `~/.config/kads/zene/state/runs/new-p17-140.r1/logs/`)

| Log | Command | EXIT |
|---|---|---|
| build-1.log | `cmake --build build --target zene ControlRecordingRoutesTest -j4` (tests added, no fix) | 0 |
| test-2.log | `cd build/tests && ctest -R ControlRecordingRoutesTest --output-on-failure` (RED) | 8 |
| build-5.log | `cmake --build build --target zene ControlRecordingRoutesTest RecordToolsTest -j4` (fix) | 0 |
| test-5.log | `ctest -R ControlRecordingRoutesTest --output-on-failure` (GREEN) | 0 |
| — | `git diff -- include/RecorderTaps.h src/core/RecorderTaps.cpp src/core/Mixer.cpp src/core/ControlCommandsMixer.cpp > logs/fix.patch && git apply -R logs/fix.patch` | 0 |
| build-7.log | `cmake --build build --target zene ControlRecordingRoutesTest RecordToolsTest -j4` (fix reverted) | 0 |
| test-8.log | `ctest -R ControlRecordingRoutesTest --output-on-failure` (fix reverted) | 8 |
| test-9.log | `ctest -R RecordTools --output-on-failure` (fix reverted) | 8 (`theRemainingRecordTime` only; see below) |
| — | `git apply logs/fix.patch` | 0 |
| build-8.log | `cmake --build build --target zene ControlRecordingRoutesTest RecordToolsTest -j4` (fix reapplied) | 0 |
| test-10.log | `ctest -R ControlRecordingRoutesTest --output-on-failure` (fix reapplied): `100% tests passed out of 1` | 0 |
| test-11.log | `ctest -R RecordTools --output-on-failure` (fix reapplied) | 8 (`theRemainingRecordTime` only; see below) |

The test-8 failures with the fix reverted:
- `FAIL! : ControlRecordingRoutesTest::aTapOnADeletedChannelGoesBackToTheInput()`: Actual `QJsonArray()`, Expected `QJsonArray([0])`.
- `FAIL! : ControlRecordingRoutesTest::undoingOntoADeletedChannelTapsTheInput()`: Actual `recordertaps::get(0).source` 2, Expected `recordertaps::Source::Input` 0.

`RecordToolsTest::theRemainingRecordTime` fails identically with and without the fix (test-9 and test-11): `'left.ok' returned FALSE. (cannot measure the volume holding '/home/kruzzzzy/Documents/Zene Studio')` at `RecordToolsTestParts1.h:296`. That directory does not exist on this machine. The failure is environmental and unrelated to this change. The other 26 RecordTools functions pass in both runs.

## Gates

- `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` → EXIT=1 (gates-1.log). The run crashed before printing its table: the session guard refused its baseline step, `git -C ~/.local/share/zene-bunny-fix/qwen/gate-baselines/tree checkout -q --detach 9913d36d5...` ("not allowed in a Bunny lane"), so `CalledProcessError` followed. I did not work around the refusal.
- I ran each gate the wrapper runs directly from the worktree:

| Log | Gate | EXIT |
|---|---|---|
| gate-1.log | `bash tests/fork-sources-gate.sh` | 0 |
| gate-2.log | `bash tests/all-sources-reproduce.sh` | 0 |
| gate-3.log | `bash tests/no-upstream-regression-gate.sh` | 0 |
| gate-4.log | `tests/scripted/check-namespace` | 0 |
| gate-5.log | `bash tests/no-tautology-gate.sh` | 0 |
| gate-6.log | `python3 tests/doc-figures-gate.py` | 0 |
| gate-7.log | `bash tests/complexity-gate.sh --check` | 2: "lizard is not installed" (environment; not installed because nothing may be fetched) |
| gate-8.log | `bash tests/file-length-gate.sh --check` | 0 (no REGRESSION lines) |

- Complexity, worked out by hand because lizard is missing (unverified by tooling):
  - `recordRoutesTapping` is 4.
  - `channelExists` is 4.
  - `channelDeleted` is 4.
  - `set` is 5.
  - The `mixer.remove_channel` handler lambda goes from 3 to 4.
  - `tests/complexity-baseline.tsv` has no entry for `ControlCommandsMixer.cpp` or `RecorderTaps.cpp`, so no grandfathered function rises.
  - `Mixer.cpp` is upstream and outside the gate's fork-sources scope.

## Self-check

- Verdict with evidence: REAL. The file:line evidence is above, and RED was shown in test-2.log and test-8.log.
- Regression test fails without the fix and passes with it: test-8.log EXIT 8, test-10.log EXIT 0.
- Every caller and sibling checked:
  - All deletion paths go through `Mixer::deleteChannel`: `mixer.remove_channel`, the `mixer.add_channel` undo, `Mixer::clear`, and the GUI.
  - The `record.route_source` undo/redo `set()` path is covered by `set()`'s existence check.
  - `tapJson` already rendered a missing channel as `""`; with the fix that case no longer arises from a deletion.
- No collateral damage: taps aren't saved, the reply gains a key only when routes were reset, and no limit was added.
- Realtime: nothing on the audio thread changed.
- Undo: `mixer.remove_channel` stays irreversible as before. No new control command was added.
- Registration: Mixer.cpp has an `upstream-modifications.txt` line ending in a newline. The other changed files were already in `fork-sources.txt`. No new files.
- Not verified: lizard CCN, the feature_check wrapper table (blocked as described above), and TSan (not available locally).

Delegation:
- Haiku (1st start): read-only search for `MixerChannel::id()` persistence and the `deleteChannel` callers and their undo.
- Sonnet: wrote the two regression tests to my design, ran RED, then implemented my fix design and ran GREEN (build-1..6, test-1..7).
- Haiku (2nd start): ran the 4b reverse/reapply proof and the RecordTools family runs (build-7/8, test-8..11).
- I did the diagnosis, the design, the diff review, the gate runs, the report and the commit.

## Other findings

- `RecordToolsTest::theRemainingRecordTime` depends on `~/Documents/Zene Studio` existing, and fails on a machine without that directory (test-9.log, unfixed tree).
- `feature_check.py`'s baseline step runs `git checkout` in a tree outside the worktree, and the lane's session guard refuses that. As a result the wrapper cannot run in this lane.
- `tests/complexity-gate.sh` needs `lizard`, which is not installed on this box.
