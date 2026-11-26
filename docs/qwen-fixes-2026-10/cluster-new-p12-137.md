FIXED

# Cluster new-p12-137: a newly enabled channel filter can suppress a held note's note-off

Finding (Luna patrol, BUG 4, confidence low): `src/core/MidiInputTransform.cpp:119` passed an
unmatched note-off only when it passed the current channel filter, and a note-on played before
the transform began tracking held keys has no entry in `m_heldAs` - so the release of a note
that is sounding is dropped and the note hangs.

## Verdict: REAL

The defect is at the base commit `3436a808b` of this branch and reproduces on it; both bullet
paths below are code I read in this session.

- The live-input path really does skip the transform while it is the identity:
  `src/tracks/InstrumentTrackNotes.cpp:154-161` - `handleInputTransform()` returns `false`
  before `apply()` when `!m_inputTransform.active()`, so the event continues down the raw
  path and nothing is recorded in the held table.
- `src/core/MidiInputTransform.cpp:112-127`, `follow()` (after the fix; the branch the
  finding quotes as line 119 was `if (as == NotHeld) { return passesChannel(*event); }` at
  line 122 before the fix): a note-off/pressure whose entry is `NotHeld` - "not held through
  the transform", `include/MidiInputTransform.h:99-100` - was returned only when
  `passesChannel()` held. `passesChannel()` reads the channel filter *now*
  (`src/core/MidiInputTransform.cpp:78-82`), so a filter set after the key went down dropped
  the release of a note that is sounding.
- It contradicts the class contract it belongs to: `include/MidiInputTransform.h:46-47`
  promises "A note-off takes the key its note-on was given, so a change while a key is held
  never leaves a note sounding". The key range and the transpose are already never applied to
  an untracked release; only the channel filter was, which is the asymmetry that hangs notes.
- The neighbouring "dropped" case is deliberate and stays: a note-on the transform itself
  refused stores `Dropped` (`src/core/MidiInputTransform.cpp:92-110`) and its note-off is
  dropped with it, because that note never sounded.

Reproduction on the base (both new slots failed, `test-1.log`):

```
FAIL!  : MidiInputTransformTest::anUntrackedNoteOffOutlivesANewChannelFilter() 'transform.apply(&off)' returned FALSE. (the release of a sounding note is passed, whatever the filter is now)
FAIL!  : MidiInputTransformTest::aNoteHeldAcrossAChannelFilterIsStillReleased() 'track->playingNote(60) == nullptr' returned FALSE. (the release of the held note reached it after the filter came in)
```

The second line is the hang itself: after the release, the live note the track had started was
still in `InstrumentTrack::playingNote(60)` (`include/InstrumentTrack.h:126-129`, the
`m_notes[key]` readback).

## Fix

`src/core/MidiInputTransform.cpp:114-122`: an untracked release is passed whatever the channel
filter says now - `return ends || passesChannel(*event);`. `ends` is true for a note-off and
for a note-on of velocity 0 (`src/core/MidiInputTransform.cpp:67-75`), the two ways a key is
released. The filter is a gate on starting a note, not on releasing one.

The change is one expression plus its comment; no header, no new member, no new setting, no
new control command (the ControlCommandsSnapshot canary is untouched), no undo step, and the
held table keeps its size and meaning.

Siblings and callers checked, and why they are consistent:

- `follow()` has one caller, `apply()` (`src/core/MidiInputTransform.cpp:67-75`), which routes
  note-offs and velocity-0 note-ons through it with `ends = true` and key pressure with
  `ends = false`. Pressure is not a release and cannot hold a note down, so it keeps the
  channel gate - behavior outside the bug is unchanged, and the new test pins that.
- `Dropped` entries are still dropped (`src/core/MidiInputTransform.cpp:124`); only the
  `NotHeld` branch moved.
- A tracked release still takes the key its note-on was given, whatever the filter is now
  (`src/core/MidiInputTransform.cpp:123-126`, existing slot
  `aNoteOffFollowsItsNoteOnAcrossAChange`).
- `apply()` has one product caller, `InstrumentTrack::handleInputTransform()`. The event it
  passes re-enters `processInEvent()` inside the `InputTransformScope`, so the release still
  reaches the take capture (`src/tracks/InstrumentTrack.cpp:498-500`) and the monitor gate,
  exactly as a tracked release already did. No new class of event skips capture.
- Realtime: `follow()` runs on the MIDI input thread; the change adds one integer compare. No
  allocation, no free, no lock, no wait, no logging, no Qt call, no new atomic.
- No new range or limit, so no other caller can be wrongly rejected; old projects and saved
  data are unaffected (`toJson`/`save`/`load` are untouched).

## Regression tests

Both in the existing test file `tests/src/core/MidiInputTransformTest.cpp` (already
registered in `tests/CMakeLists.txt:1022` and `tests/fork-sources.txt:2885`; no registration
change needed):

- `anUntrackedNoteOffOutlivesANewChannelFilter()` (lines 152-175): transform level. A release
  arrives with no held entry while the channel filter excludes its channel - it must pass, at
  the key that arrived; a velocity-0 note-on releases the same way; a note-on the filter
  dropped still drops its release; key pressure on an excluded channel is still filtered out.
- `aNoteHeldAcrossAChannelFilterIsStillReleased()` (lines 441-459): through the live path. The
  note-on goes through `processInEvent()` while the transform is the identity (so the path
  skips `apply()` and nothing is tracked), the filter is then set with
  `track.input_transform_set {track, channel: 2}` while the key is held, and the note-off must
  clear `playingNote(60)`.

Both are deterministic: no sleeps, no wall-clock, no threads - the events are injected
directly and the readback is synchronous.

Test matrix:

| path | case | covered by |
|---|---|---|
| normal | a release whose note-on was transformed takes the mapped key across a change | `aNoteOffFollowsItsNoteOnAcrossAChange` (existing) |
| failure | untracked release after a filter excludes its channel | the two new slots |
| sibling | note-on the transform dropped still drops its release (`Dropped`) | new unit slot, existing `theChannelFilterAndTheScaleSnap` |
| sibling | untracked key pressure keeps the channel gate | new unit slot |
| reconfiguration | `track.input_transform_set` while the key is held | new live-path slot |
| exhaustion / limits | none added: same table size, no new range, no new field | - |
| audio thread | one compare, no allocation/lock/log; no header change | `follow()` only |

## Commands and exit codes

All run in this worktree, output in
`/home/kruzzzzy/.config/kads/zene/state/runs/new-p12-137.r1/logs`. Each command ends with
`echo ..._EXIT=$? >> log`, so the exit code recorded below is the command's own; the
background wrapper's exit code is not used.

RED - tests added, product code untouched:

```
cmake --build build --target MidiInputTransformTest -j4        build-1.log  BUILD_EXIT=0
cd build/tests && ctest -R MidiInputTransformTest --output-on-failure
                                                                test-1.log   TEST_EXIT=8
```

GREEN - after the fix in `src/core/MidiInputTransform.cpp`:

```
cmake --build build --target zene MidiInputTransformTest -j4    build-2.log  BUILD_EXIT=0
cd build/tests && ctest -R MidiInputTransformTest --output-on-failure
                                                                test-2.log   TEST_EXIT=0
cmake --build build --target MidiInputGateTest -j4              build-3.log  BUILD_EXIT=0
cd build/tests && ctest -R MidiInput --output-on-failure
                                                                test-3.log   TEST_EXIT=0
```

`ctest -R MidiInput` is the family of the changed code: `MidiInputGateTest`,
`MidiInputTransformTest`, `MidiInputSupportDoc` - 3/3 passed, "100% tests passed out of 3".

4b - the test catches the bug (`fix.patch` is the product diff only):

```
git diff -- src/core/MidiInputTransform.cpp > logs/fix.patch
                                                step4b-revert.log  GIT_DIFF_EXIT=0
git apply -R logs/fix.patch                     step4b-revert.log  APPLY_REVERSE_EXIT=0
cmake --build build --target zene MidiInputTransformTest -j4
                                                build-4.log  BUILD_EXIT=0
cd build/tests && ctest -R MidiInputTransformTest --output-on-failure
                                                test-4.log   TEST_EXIT=8   (2 FAIL, the same two slots)
git apply logs/fix.patch                        step4b-reapply.log REAPPLY_EXIT=0
cmake --build build --target zene MidiInputTransformTest -j4
                                                build-5.log  BUILD_EXIT=0
cd build/tests && ctest -R MidiInputTransformTest --output-on-failure
                                                test-5.log   TEST_EXIT=0
```

Gates:

```
python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>
                                                gates-1.log  GATES_EXIT=0
```

```
feature-check kads-new-p12-137 (base 3436a808b, 2 changed files)
  PASS  fork-sources            exit 0
  PASS  all-sources-reproduce   exit 0
  PASS  no-upstream-regression  exit 0
  PASS  check-namespace         exit 0
  PASS  no-tautology            exit 0
  PASS  doc-figures             exit 0
  PASS  complexity              no new item in your files
  PASS  file-length             no new item in your files
RESULT: PASS
```

## Self-check

Answers to the five ASK items of `submit_check.py`, then the supporting evidence.

1. **The regression test fails with the product change reverted and passes with it.** Reverted:
   `git apply -R fix.patch` (`APPLY_REVERSE_EXIT=0`), rebuild (`BUILD_EXIT=0`), run
   (`TEST_EXIT=8`, both new slots FAIL - `test-4.log`). Restored: `git apply fix.patch`
   (`REAPPLY_EXIT=0`), rebuild (`BUILD_EXIT=0`), run (`TEST_EXIT=0` - `test-5.log`). The 17
   pre-existing slots of the file pass in both runs, so the two new slots are exactly what the
   product change turns from red to green.
2. **The test is deterministic.** No wall clock, no sleep, no thread, no event loop: the seam is
   the transform's own `apply()` (pure, one event in, one answer out) plus
   `InstrumentTrack::processInEvent()` with `Engine::audioEngine()->audioDev()->stopProcessing()`
   already run in `initTestCase`, and the readback is the synchronous
   `InstrumentTrack::playingNote(key)` (`include/InstrumentTrack.h:126-129`). The events are
   injected with fixed keys, channels and velocities; nothing is timed.
3. **The fix is at the cause and complete.** `follow()` has exactly one caller, `apply()`
   (`src/core/MidiInputTransform.cpp:67-75`), and it routes all three release shapes through it
   (note-off, velocity-0 note-on with `ends = true`, key pressure with `ends = false`).
   `apply()` has exactly one product caller, `InstrumentTrack::handleInputTransform()`
   (`src/tracks/InstrumentTrackNotes.cpp:154-171`), whose early return for the identity path is
   the reason the entry is missing - and the reason the fix must not depend on tracking. The
   siblings - `Dropped` releases, tracked releases across a reconfiguration, the key range and
   the transpose - are unchanged and each is pinned by a slot that passes (`test-3.log`).
4. **No collateral damage.** The transform's stored form (`toJson`/`fromJson`/`save`/`load`) and
   the `track.input_transform_set`/`_get` commands are untouched, so old projects load and
   round-trip as before (the file's own `theCommandsUndoAndTheFile` slot passes). No limit or
   range was added, so no other caller can be newly rejected. The `-R MidiInput` family passes
   3/3 (`test-3.log`) and the gates pass (`gates-2.log`).
5. **ALREADY-FIXED / FALSE-POSITIVE**: not claimed - the verdict is FIXED, reproduced on the
   base and proven by the reverse-patch run above.

Supporting evidence:

- **Verdict backed by code and a reproduction**: the mechanism is quoted above with file:line,
  and the defect was observed twice - at the transform (`apply()` returned false for a release)
  and through the live path (`playingNote(60)` still set after the release). `test-1.log`.
- **Realtime**: `follow()` runs on the MIDI input thread and the change is one integer compare;
  no allocation, free, lock, wait, I/O or log was added, and no member, header or command
  changed, so nothing new runs on the audio thread and the command snapshot cannot move.
- **Undo and commands**: no undo step is added or removed and no command is added or changed;
  the fix is one product expression, two test slots and this report.
- **Registration**: no new file; the changed product file is already listed in
  `tests/fork-sources.txt:2133`, and the gate's `fork-sources` check passes.
- **Commit**: the report and the fix are in one commit naming cluster new-p12-137
  (`a4bbaa8de0a6e56c3a15e82dec01a3ad427ec17c`).

## Other findings

- `follow()` still applies the channel filter to untracked *polyphonic key pressure*
  (`src/core/MidiInputTransform.cpp:122`, `ends == false`). That is deliberate here - pressure
  cannot hang a note, and passing it would erode the channel filter for expression events - and
  it is pinned by the new unit slot.
- `InstrumentTrack::m_notes` is indexed by key alone (`src/tracks/InstrumentTrack.cpp:532-583`)
  while the held table is per channel and key. Two sounding notes on the same key from
  different channels of one port therefore already shadow each other. Not investigated further
  and not changed here; it is independent of this fix.
