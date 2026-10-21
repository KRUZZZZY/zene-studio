# Cluster new-p11-38-port-port: transaction records for note commands (re-port)

Verdict: **FIXED** — the six handlers the finding names returned no `__transaction`
payload on this branch; they now return an accurate one (before-state, inverse,
reversibility, mechanism). Proven RED/GREEN below by reverting and restoring the
product diff.

This is the **re-port** of the accepted fix `bugs/qwen-new-p11-38-port`
(commits `ac32c012d`, `e1e9e8e9d`, `8982a258a`, `76710095c`) onto the newest line.
That branch could not go on the train because of a merge conflict in
`src/core/ControlCommandsNotePlayback.cpp` — the newest line has since rewritten
`articulationsSet` (it now remaps the notes with the map in one lock, via
`applyArticulationEdit`, and takes an optional `from` list). The fix was therefore
re-applied **by hand** to the newest line: no merge, no cherry-pick, no rebase.

## What the finding was

> `src/core/ControlCommandsNoteHarmony.cpp:116` (`clip.quantize`), `:146` (`scale.define`);
> `src/core/ControlCommandsNoteMoves.cpp:132` (`note.split_by_pitch`);
> `src/core/ControlCommandsNotePlayback.cpp:176` (`track.articulations_set`), `:207`
> (`transport.groove`), `:222` (`transport.midi_options`) — "these handlers create undo
> actions but return no `__transaction` record describing the inverse and mechanism."

The finding's line numbers are the **source** branch's (`bugs/qwen-new-p11-38`) pre-fix
state. On this branch they map onto the same handlers, at the current lines below.

## Evidence that the bug was REAL here (not already fixed)

Before the change, none of the six handlers inserted a `__transaction` key:

```
$ rg -n "__transaction" src/core/ControlCommandsNoteHarmony.cpp src/core/ControlCommandsNoteMoves.cpp src/core/ControlCommandsNotePlayback.cpp
(no match — exit 1)
```

The registry (`src/core/ControlRegistry.cpp:505-529`) records a *fallback* record for
such a call: `inverse` stays empty and `mechanism` says "this command recorded no
inverse or snapshot". So the edit was undoable in the application, but the command
result carried no reversibility row for the client — exactly what the finding says.

## File and line evidence (current tree, after the fix)

| Command | Handler | The `__transaction` payload |
| --- | --- | --- |
| `clip.quantize` | `src/core/ControlCommandsNoteHarmony.cpp:118` `quantizeClips` | `:147-149` — before `{clips:[{clip,start}]}` (captured by `captureQuantizeBefore`, `:108`), inverse `control.undo`, reversible `true`, mechanism "Track journal checkpoints: control.undo restores each clip's original start position" |
| `scale.define` | `src/core/ControlCommandsNoteHarmony.cpp:153` `defineScale` | `:176-178` — before `{scales:<the previous custom scale map>}`, inverse `control.undo`, reversible `true`, mechanism "Action checkpoint: control.undo writes the previous custom scales back to the config file" |
| `note.split_by_pitch` | `src/core/ControlCommandsNoteMoves.cpp:112` `splitByPitch` | `:149-151` — before `{clip,track,keys}`, inverse `control.undo`, reversible `true`, mechanism "Action checkpoint: control.undo removes the per-key tracks created by this split" |
| `track.articulations_set` | `src/core/ControlCommandsNotePlayback.cpp:187` `articulationsSet` | `:223-225` — before `{track,articulations:<the previous map>}` (captured at `:203`, **before** the edit), inverse `control.undo`, reversible `true`, mechanism "Action checkpoint: control.undo restores this track's previous articulation map" |
| `transport.groove` | `src/core/ControlCommandsNotePlayback.cpp:244` `groove` | `:265-266` — before `{swing,grid}`, inverse `control.undo`, reversible `true`, mechanism "Action checkpoint: control.undo restores the previous swing and grid" |
| `transport.midi_options` | `src/core/ControlCommandsNotePlayback.cpp:270` `midiOptions` | `:287-288` — before `{chase,offset_ms}`, inverse `control.undo`, reversible `true`, mechanism "Action checkpoint: control.undo restores the previous chase and MIDI offset preferences"; inserted **only when the call changed something** (`:286`), so a no-op report stays a no-op record |

Contract rows for the same six commands: `src/core/ControlReversibilityTableDevices.cpp:133`
(`note.split_by_pitch`), `:135` (`clip.quantize`), `:136` (`scale.define`), `:144`
(`track.articulations_set`), `:147` (`transport.groove`), `:243` (`transport.midi_options`)
— every one `TrueInverse`, and the registry stamps that class onto the record
(`src/core/ControlRegistry.cpp:543-571`), so a handler cannot claim more than the table
allows.

### How the newest line differs, and how the port was adapted

`src/core/ControlCommandsNotePlayback.cpp` was rewritten on the newer line
(`git diff ded355440 HEAD -- src/core/ControlCommandsNotePlayback.cpp`):

* `articulationsSet` no longer calls the old `writeArticulations(id, entries)` helper.
  It now takes an optional `from` list, plans the note remap
  (`planArticulationRemap`) and applies map **and** notes in ONE lock
  (`applyArticulationEdit`, `include/ArticulationRemap.h:65`), announcing the touched
  clips only after the lock is released.
  The port therefore captures the before-map with `track->articulations().toJson()`
  **before** `applyArticulationEdit` runs (`ControlCommandsNotePlayback.cpp:203`) and
  keeps the newest line's `notes_remapped` field in the reply.
* `midiOptions`' description grew (chase also covers a note under the playhead when
  its track or clip is unmuted). The description and the args schema are **unchanged**
  by this port, so the command snapshot does not move.

The other two files are byte-identical to the source branch's pre-fix state, so their
hunks carry over unchanged.

## Regression test

`tests/src/core/NoteCommandTransactionsTest.cpp` (234 lines) drives all six commands
through the real registry (`ControlRegistry::instance()->invoke`) and reads the record
back from `control.transactions` — the same stream a client reads. Per command it
asserts `inverse.op == "control.undo"`, `reversible == true`, the exact before-state
snapshot and the exact mechanism text.

Registration: `tests/CMakeLists.txt:782` (source), `:1710` (the plugin-module list, so
the test binary gets `ENABLE_EXPORTS` and `LMMS_TEST_PLUGIN_DIR`), `:1747` (build the
`tripleoscillator` module beside it), `tests/fork-sources.txt:2374` and
`tests/all-sources.txt:2658` (the two scope manifests).

## Evidence

All logs are in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-38-port-port.r1/logs`.

| # | Check | Exact command (cwd) | Exit | Result |
| --- | --- | --- | ---: | --- |
| 1 | Configure the new test target | `cmake --build build --target zene` (worktree) | 0 | `build-1.log` — CMake re-ran, the new test target appeared |
| 2 | Build the regression test | `cmake --build build --target NoteCommandTransactionsTest` (worktree) | 0 | `build-3.log` |
| 3 | **RED**, before the fix | `ctest -R '^NoteCommandTransactionsTest$' --output-on-failure` (`build/tests`) | **8** | `test-2.log` — 2 test functions FAILED: `inverse.op` was `""` instead of `"control.undo"` (`NoteCommandTransactionsTest.cpp:81`) and the before-state was empty |
| 4 | Build with the fix | `cmake --build build --target zene NoteCommandTransactionsTest` (worktree) | 0 | `build-5.log` |
| 5 | **GREEN**, with the fix | `ctest -R '^NoteCommandTransactionsTest$' --output-on-failure` (`build/tests`) | 0 | `test-3.log` — 1/1 passed |
| 6 | Build the affected family | `cmake --build build --target NoteToolsTest NotePlaybackTest ArticulationPlaybackTest UnmuteChaseTest ReversibilityContractTest ReversibilityUndoTest UndoHonestyTest` (worktree) | 0 | `build-6.log` |
| 7 | Affected-family run | `ctest -R '^(NoteCommandTransactionsTest\|NoteToolsTest\|NotePlaybackTest\|ArticulationPlaybackTest\|UnmuteChaseTest\|ReversibilityContractTest\|ReversibilityUndoTest\|UndoHonestyTest)$' --output-on-failure` (`build/tests`) | 0 | `test-5.log` — **8/8 passed** (tests #175, #176, #252, #253, #254, #255, #256, #286) |
| 8 | The GUI articulation editor | `ctest -R '^ArticulationMapEditorTest$' --output-on-failure` (`build/tests`) | 0 | `test-8.log` — 1/1 passed |
| 8b | Wider control-surface family (committed state) | `ctest -R '^(NoteCommandTransactionsTest\|NoteToolsTest\|NotePlaybackTest\|ArticulationPlaybackTest\|UnmuteChaseTest\|ReversibilityContractTest\|ReversibilityUndoTest\|UndoHonestyTest\|ArticulationMapEditorTest\|ControlRegistryTest\|ControlEditCommandsTest\|ControlVerbInverseTest\|ControlNoteScaleVerbsTest\|ControlGrooveCommandsTest\|RecordToolsTest)$' --output-on-failure` (`build/tests`) | 0 | `test-9.log` — **15/15 passed** |
| 9 | Product patch | `git diff -- src/core/ControlCommandsNoteHarmony.cpp src/core/ControlCommandsNoteMoves.cpp src/core/ControlCommandsNotePlayback.cpp > …/logs/fix.patch` (worktree) | 0 | `fix.patch` (179 lines) |
| 10 | Revert the fix | `git apply -R …/logs/fix.patch` (worktree) | 0 | `apply-reverse-1.log` |
| 11 | Rebuild without the fix | `cmake --build build --target zene NoteCommandTransactionsTest` (worktree) | 0 | `build-8.log` |
| 12 | **RED**, fix reverted | `ctest -R '^NoteCommandTransactionsTest$' --output-on-failure` (`build/tests`) | **8** | `test-6.log` — 2 test functions FAILED on the empty `inverse.op` |
| 13 | Restore the fix | `git apply …/logs/fix.patch` (worktree) | 0 | `apply-forward-1.log` |
| 14 | Rebuild with the fix | `cmake --build build --target zene NoteCommandTransactionsTest` (worktree) | 0 | `build-9.log` |
| 15 | **GREEN**, fix restored | `ctest -R '^NoteCommandTransactionsTest$' --output-on-failure` (`build/tests`) | 0 | `test-7.log` — 1/1 passed |
| 16 | Gates (all eight) | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 0 | `gate-feature-check-1.log` — **8/8 PASS** (fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity, file-length) |
| 17 | Complexity ratchet (raw) | `bash tests/complexity-gate.sh --check` (worktree) | 1 | `gate-complexity-1.log` — pre-existing train drift (6 functions not in the baseline, none of them in a file this branch changed) |
| 18 | File-length ratchet (raw) | `bash tests/file-length-gate.sh --check` (worktree) | 1 | `gate-filelength-1.log` — pre-existing train drift (10 files not in the baseline, none of them in a file this branch changed) |
| 19 | Whole-tree manifest | `bash tests/all-sources-reproduce.sh` (worktree) | 0 | `REPRODUCES: the entry list in all-sources.txt is the recipe's own output.` |

Gates 17 and 18 are red **on the train on purpose** (their baseline predates the newer
line). Gate 16 is the gate that counts: it compares each ratchet's regression list
against the same gate on the branch's base commit, and reports **"no new item in your
files"** for both.

## Files committed

- `src/core/ControlCommandsNoteHarmony.cpp`
- `src/core/ControlCommandsNoteMoves.cpp`
- `src/core/ControlCommandsNotePlayback.cpp`
- `tests/src/core/NoteCommandTransactionsTest.cpp` (new)
- `tests/CMakeLists.txt`
- `tests/fork-sources.txt`
- `tests/all-sources.txt`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p11-38-port-port.md` (this report)

## Self-check

**1. The regression test FAILS with the product change reverted and PASSES with it
(exit codes).**
RED: `ctest -R '^NoteCommandTransactionsTest$' --output-on-failure` → **exit 8**
(`test-2.log`, before the fix; and again `test-6.log` after
`git apply -R …/logs/fix.patch`). The failing assertion is
`NoteCommandTransactionsTest.cpp:81` — `inverse.op` was `""` instead of
`"control.undo"` — plus the empty before-state at `:162`/`:203`.
GREEN: the same command → **exit 0** (`test-3.log` with the fix; `test-7.log` after
`git apply …/logs/fix.patch`). Steps 9–15 of the evidence table are the mechanical
proof.

**2. The test is deterministic: no wall-clock timing, sleeps or races.**
The test reads only the registry's own records and the engine's own settings, all on
the thread that wrote them: the tick positions are the constants `200` and the clip's
own start, the grid is `TimePos::ticksPerBar()`, the swing/grid/chase/offset are
`MidiPlayback::instance()`'s current values (read back on the same thread), and the
custom scales are read back with `scale.custom`. No clock, no `QTest::qWait`, no
`sleep`, no thread, no file I/O, no randomness. The one global the test touches — the
user's config (`scales/custom`, `midi/chase`, `midi/playbackoffset`) — is snapshotted in
`initTestCase` and **restored** in `cleanupTestCase` before `Engine::destroy()`
persists it (the `qa-fix` follow-up of the source branch, carried over).

**3. The fix is at the cause and complete: every caller and sibling of what you changed
was checked and stays consistent.**
The cause is a handler that changes the project and returns a result with no
`__transaction` payload; the fix is at that return statement in all six handlers.
Siblings in the three touched files, checked one by one:

| Command | File | State |
| --- | --- | --- |
| `note.follow_chords` | `ControlCommandsNoteHarmony.cpp` | already records (through `finish()`, `ControlCommandsNoteShared.h:78`) |
| `clip.quantize` | `ControlCommandsNoteHarmony.cpp` | **fixed here** |
| `scale.define` | `ControlCommandsNoteHarmony.cpp` | **fixed here** |
| `scale.custom` | `ControlCommandsNoteHarmony.cpp` | read-only, no undo step — correctly no record |
| `note.move_to_track` | `ControlCommandsNoteMoves.cpp` | already records (through `finish()`) |
| `note.split_by_pitch` | `ControlCommandsNoteMoves.cpp` | **fixed here** |
| `scale.detect` | `ControlCommandsNoteMoves.cpp` | read-only — correctly no record |
| `transport.panic` | `ControlCommandsNoteMoves.cpp` | read-only ("Nothing in the project changes") — correctly no record |
| `note.lyrics`, `note.group`, `note.ungroup` | `ControlCommandsNotePlayback.cpp` | already record (through `finish()`) |
| `track.articulations_set` | `ControlCommandsNotePlayback.cpp` | **fixed here** |
| `track.articulations` | `ControlCommandsNotePlayback.cpp` | read-only — correctly no record |
| `transport.groove` | `ControlCommandsNotePlayback.cpp` | **fixed here** |
| `transport.midi_options` | `ControlCommandsNotePlayback.cpp` | **fixed here** |

Every other `addUndoStep` in the three files now sits next to a `__transaction` record
(grep: `ControlCommandsNoteHarmony.cpp:139`/`:174`,
`ControlCommandsNoteMoves.cpp:72`/`:140`,
`ControlCommandsNotePlayback.cpp:219`/`:261`/`:281`). The wider flow the payload
flows into is `ControlRegistry::runHandler`
(`src/core/ControlRegistry.cpp:505-538`), which reads `__transaction` and stamps the
contract class onto it — unchanged by this port. `scale.set`, the sibling of
`scale.define` in the scale family, already records through `contextTransaction`
(`src/core/ControlCommandsScale.cpp:243`).

**4. No collateral damage: old projects and saved data still load as before, and any
limit or check you added does not hit other callers that legitimately exceed it.**
No save, load or project format was touched. The articulation map is still written by
the same code path as before (only the *read* of the previous map was added, and it
reads the very JSON the map already serialises). `clip.quantize` keeps its exact undo
behaviour: the set of tracks put into the undo step is unchanged (`captureQuantizeBefore`
appends a track only when it is not already in the list — the old condition was
`snapped != start && !tracks.contains(ref.track)`, the new one is `snapped != start`
plus the same `!contains` test inside the helper), the `moved` counter is unchanged, and
the early return for a no-op quantize still returns without a record. No new limit or
check was added anywhere; the payload sizes are bounded by what the command already
accepted (`MaxTransactionBytes = 262144`,
`include/ControlReversibility.h:442`), and the registry evicts the oldest record if a
record ever crosses it (`src/core/ControlTransactions.cpp:60-71`), so an unusually long
input degrades the history, never the command.

**5. If your verdict is ALREADY-FIXED or FALSE-POSITIVE: the code that shows it.**
Not applicable — the verdict is FIXED. The evidence that it was NOT already fixed here
is the empty grep for `__transaction` in the three files before the change (recorded
above) and the RED run in `test-2.log`.

**6. Sibling commands in the command files you changed — confirm each is consistent.**
The sibling set the completion check computes for this diff is
`scale.custom`, `scale.define`, `scale.detect`, `scale.from_detected_key`,
`scale.get_state`, `scale.list`, `scale.root_set`, `scale.set`, `scale.snap_notes`,
`transport.audition_start`, `transport.audition_stop`, `transport.get_state`,
`transport.groove`, `transport.low_latency`, `transport.metronome_options`,
`transport.midi_options`, `transport.panic`, `transport.play`, `transport.punch_clear`,
`transport.punch_get_state`, `transport.punch_here`, `transport.punch_set`,
`transport.record`, `transport.record_options`, `transport.seek`,
`transport.set_metronome`, `transport.set_tempo`, `transport.stop`,
`transport.tempo_map_add`, `transport.tempo_map_clear`, `transport.tempo_map_from_clip`,
`transport.tempo_map_get`, `transport.tempo_map_remove`, `transport.tempo_map_set_active`,
`transport.time_format`.
Answer: of those, the two this branch changed (`transport.groove`,
`transport.midi_options`) and their scale siblings `scale.define` are fixed here;
`scale.custom`, `scale.detect` and `transport.panic` are read-only and correctly
record nothing; `scale.set` already records (`ControlCommandsScale.cpp:243`); the
remaining `transport.*` commands live in other files that this branch did not touch
(`ControlCommandsClock.cpp`, `ControlCommandsRecording.cpp`, `ControlCommandsPunch.cpp`,
`ControlCommandsMasteringRun.cpp`), each with its own contract row in
`src/core/ControlReversibilityTable*.cpp` — none of them is affected by this change.

**7. Realtime, undo and commands.**
Nothing on the audio thread changed: every edit is in a UI-thread command handler.
The articulation map is still written under the track lock exactly as before
(`applyArticulationEdit`, `include/ArticulationRemap.h:65`); the only new read of that
map (`toJson()`, `ControlCommandsNotePlayback.cpp:203`) happens on the UI thread
**before** the locked write, so it cannot race with it. No allocation, lock, wait, I/O
or logging was added to any per-period path. Undo stays exactly one honest step per
command — no new undo step was added, and the payload only *describes* the step that
was already there. **No new control command was added** (the six commands' ids,
descriptions and args schemas are untouched), so the
`tools/mcp-zene-control/zene_control/commands_snapshot.json` canary does not move:
the snapshot records `id`, `description`, `args_schema`, `requires`, `mutating`,
`group` and `result_schema` (a static `{"type": "object"}`), and none of them changed.

**8. Registration and gates.**
The new test source is registered in `tests/CMakeLists.txt` (three places),
`tests/fork-sources.txt` and `tests/all-sources.txt`; the inherited-file change to
`tests/all-sources.txt` is declared in `tests/upstream-modifications.txt`. The
whole-tree manifest still reproduces from its own recipe
(`bash tests/all-sources-reproduce.sh` → `REPRODUCES`). All eight gates pass
(`feature_check.py` → exit 0, `gate-feature-check-1.log`).
