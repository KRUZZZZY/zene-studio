FIXED

new-p9-454: FIXED

new-p9-453: FIXED

Round 6 answers Luna's REWORK on `new-p9-454` (review
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/qa/reviews/new-p9-454.r5.md`).
The round-2, round-3, round-4 and round-5 fixes are kept; the blocking issue
of this review - a teardown note-off that the bounded event queue refused -
is fixed at its cause.

## The finding that started the cluster

`m_playingNotes` was a `QVector` whose `append()` could grow on the audio path.
That part was fixed in round 2/3 (`370df7573`, `d73caeaca`): the player now
keeps its notes in preallocated slots and hands the synth over to the render
thread through a fixed-capacity queue, so nothing on the audio thread
allocates. The SoundFont lifetime (rounds 4 and 5) is fixed as well. The
round-5 review accepted those parts and sent the cluster back for the teardown
note-off, which is what this round fixes.

## The blocking issue of the round-5 review, and where it is fixed

**A teardown note-off could be lost, and a started note stayed sounding.**

When a note handle is destroyed before its queued note-off was processed, the
player has to stop the note it started. The player does that with a *standalone*
note-off event that names the pitch (`Sf2NoteEvent::standalone`), so it stays
valid while the handle is being destroyed. That event travels through the
bounded note event queue - and `MidiHandoffQueue::push()` **refuses** an event
when the queue is full (`include/MidiHandoffQueue.h:92-96` and `:131-149`).

The old code ignored that refusal
(`plugins/Sf2Player/Sf2Player.cpp:1445` before this change (the line the review quoted)):

```cpp
		event.standalone = true;
		m_noteEvents.push( event );     // the result was thrown away
```

and then released the note's slot right after. The slot release moves the
slot's generation on, so the queued *slot* note-off event (if there was one) is
dropped by the generation check in the render stage
(`plugins/Sf2Player/Sf2Player.cpp:1351-1358`), and the standalone note-off was
never queued. The result: the voice the note started kept sounding and the
player's running-note count (`m_notesRunning`, raised in `noteOn()` at
`plugins/Sf2Player/Sf2Player.cpp:1103` and lowered in `noteOff()` at `:1110`
and in the standalone branch at `:1385`) was never lowered. The note stayed
stuck for ever.

### The fix: a bounded, allocation-free, lock-free fallback

The refusal is now handled, not ignored. The player keeps a **per-pitch
counter of teardown note-offs the queue refused** and the render stage applies
it:

- `plugins/Sf2Player/Sf2Player.h:386` -
  `std::array<std::atomic<int>, 128> m_pendingTeardownNotes{}` - one counter
  per MIDI pitch, fixed size, part of the instrument, never resized.
- `plugins/Sf2Player/Sf2Player.h:389` -
  `std::atomic<bool> m_pendingTeardownDirty{false}` - the flag that tells the
  render stage that a counter has something in it.
- `plugins/Sf2Player/Sf2Player.cpp:1510-1522` (`deleteNotePluginData()`) - when
  `m_noteEvents.push()` refuses the standalone note-off, the pitch's counter is
  incremented and the flag is set (the counter first, the release store of the
  flag second, so a render stage that sees the flag also sees the count).
- `plugins/Sf2Player/Sf2Player.cpp:1426-1447`
  (`applyPendingTeardownNoteOffs()`) - the render stage takes the counts
  (`exchange(0)`), lowers `m_notesRunning` for the pitch by that count and
  sends `fluid_synth_noteoff()` when the last note of the pitch is gone. That
  is exactly what the standalone event does when the queue *does* take it
  (`plugins/Sf2Player/Sf2Player.cpp:1383-1390`), so the two paths cannot
  disagree.
- `plugins/Sf2Player/Sf2Player.cpp:1291`, `:1326` and `:1407` - the render
  stage applies the pending teardowns at the end of the period, after the
  note events of that period: a note-on of the same pitch in the same period
  is not cut by the teardown of the older note.

Why this is safe:

- **Exactly one note-off per started note.** Either the queue takes the
  standalone event (and the event path lowers the count) or it refuses it (and
  the counter path lowers it). The two paths are never used for the same note.
- **The note slot stays safe.** The fallback needs nothing from the slot but
  the pitch, which is copied into the counter's index, so the slot can be
  returned to the pool right after the teardown, exactly as before. The
  generation check keeps dropping the stale slot events.
- **Nothing new allocates, locks, waits or logs.** The counters are a fixed
  array of atomics inside the instrument: one `fetch_add` and one `store` on
  the teardown path, one `exchange` per pitch and one `fluid_synth_noteoff()`
  on the render path. No container is grown, no mutex is taken, nothing is
  printed.
- **The note always stops, eventually.** If the render stage is not running,
  the counter keeps the note-off until it is (the flag and the counters are
  not lost by a busy render stage: the flag is cleared before the counters are
  read, and a teardown that arrives in between sets the flag again, so the
  next period takes it).

### The siblings of the changed call were checked

`m_noteEvents.push()` is called in three places, and only one of them could
lose a note-off:

- `publishNoteEvent()` for the note-on (`plugins/Sf2Player/Sf2Player.cpp:912`)
  - a refused note-on releases the slot and the note tries again in a later
  period (`plugins/Sf2Player/Sf2Player.cpp:997-1006`): the note is delayed,
  never dropped.
- `publishNoteEvent()` for the slot note-off
  (`plugins/Sf2Player/Sf2Player.cpp:1010-1018`) - a refused note-off leaves
  `noteOffSent` unset, so the note-off is published again in a later period
  while the note lives, and if the note is destroyed first, the teardown of
  this round takes it over. No hole.
- the standalone teardown note-off (`plugins/Sf2Player/Sf2Player.cpp:1510`) -
  the hole of this review, now closed by the counter.

## The regression (tests/src/plugins/Sf2PlayerRealtimeTest.cpp)

New in this round: `aStartedNoteIsStoppedWhenTheEventQueueIsFull`. It

1. loads the tree's own SoundFont and plays a note through the product's own
   plugin module, so the note really reaches the synth;
2. checks that the player counts the note as running (`runningNotes()` reads
   the player's own counter through the `runningNotesForTest` seam);
3. **fills the player's bounded note event queue** (`fillNoteEventQueue()`
   through the `fillNoteEventQueueForTest` seam), so the next event the player
   publishes is refused;
4. destroys the started note - the player's own teardown
   (`deleteNotePluginData()`), which is what the instrument track calls when
   the note handle is destroyed;
5. renders one more period and checks that the player no longer counts the
   note: the teardown note-off that the queue refused was applied anyway.

The two seams are the player's own public slots, like the `currentSynthAddress`
seam the earlier rounds added. The filler events name a note slot that is
never claimed (`m_fillerSlot`, `plugins/Sf2Player/Sf2Player.h:393`), so the
render stage drops them (a free slot's generation never matches an event):
the queue fills up without changing what the synth plays.

The fixture the test shares with the other cases of the binary moved into its
own header, `tests/src/plugins/Sf2PlayerFixture.h`, because the file-length
ratchet measures code lines per file and the test file went over the limit
with the new case in it. The fixture is the same code as before, moved - see
the file-length gate below.

The cases of the earlier rounds are kept: `notePathAllocatesNothing`,
`disabledTuningKeepsEqualTemperament`, `newSynthStartsWithTheRequestedEffectState`,
`everySynthHasItsOwnSettings`, `moreNotesThanTheOldPoolGetSlots`,
`renderSurvivesAPendingReload`, `renderPathAppliesTheState`,
`playsWithARealSoundFont`, `reloadKeepsTheSoundFontWhenTheFileIsGone`,
`everySynthHasItsOwnSoundFont` and `theTuningOffsetMatchesFluidSynthsKeyTuning`
(13 cases in the binary, 14 with the new one).

## Evidence

All logs are new numbered files in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-454.r6/logs`.

### The regression test before the fix (RED)

The first RED run was with the test as it was written first (the note handle
destroyed through `NotePlayHandleManager::release`), which never reached the
player's teardown at all - the plugin is loaded as a module and the track
never gets its instrument pointer, so the track's `deleteNotePluginData()`
never reaches the player. The test was corrected to call the player's own
teardown, and the RED proof below is the reversal of the product change with
the final test:

- `cmake --build build --target sf2player Sf2PlayerRealtimeTest` EXIT=0
  (`build-1.log`).
- `cd build/tests && ctest -R '^Sf2PlayerRealtimeTest$' --output-on-failure`
  EXIT=8 (`test-red-1.log`): `FAIL! :
  Sf2PlayerRealtimeTest::aStartedNoteIsStoppedWhenTheEventQueueIsFull()
  Compared values are not the same / Actual (fixture.runningNotes()): 1 /
  Expected (0) : 0` - the started note stayed counted, i.e. stuck.

### The whole-fix reversal (the brief's command in step 4b)

- `git diff -- plugins/Sf2Player/Sf2Player.cpp plugins/Sf2Player/Sf2Player.h > logs/fix.patch`
  EXIT=0 (84 lines);
- `git apply -R logs/fix.patch` EXIT=0;
  `cmake --build build --target sf2player Sf2PlayerRealtimeTest` EXIT=0
  (`build-revert-2.log`);
  `cd build/tests && ctest -R '^Sf2PlayerRealtimeTest$' --output-on-failure`
  EXIT=8 (`test-revert-2.log`): the same failure -
  `Actual (fixture.runningNotes()): 1`, `Expected (0) : 0`,
  `Totals: 13 passed, 1 failed`.
- `git apply logs/fix.patch` EXIT=0;
  `cmake --build build --target sf2player Sf2PlayerRealtimeTest` EXIT=0
  (`build-green-4.log`);
  `cd build/tests && ctest -R '^Sf2PlayerRealtimeTest$' --output-on-failure`
  EXIT=0 (`test-green-5.log`): `100% tests passed out of 1`.

### Affected family, sweep and gates

- `cd build/tests && ctest -R '^(Sf2PlayerRealtimeTest|MidiHandoffQueueTest|CarlaMidiHandoffTest|RemotePluginAudioPortsTest|RemotePluginClientE2ETest|RtSafetySweep|RtSafetySelfTest)$' --output-on-failure`
  EXIT=0, **7/7 tests passed** (`test-family-3.log`).
- `cd build/tests && ctest -R '^PluginPortsMigrationTest$' --output-on-failure`
  EXIT=0, 1/1 test passed (`test-ppm-2.log`). (The first run of this test
  failed with "SWH Decimator test fixture was not loaded" - the LADSPA fixture
  was not built in this build tree; after
  `cmake --build build --target decimator_1202` EXIT=0 (`build-decimator.log`)
  the test passes. The failure is unrelated to this change.)
- `python3 tests/rt-safety-sweep.py --check` EXIT=0 - "PASS: every hit on a
  declared audio-thread path is allowlisted with a reason and at its allowed
  count" (`rt-safety-sweep-1.log`). The new render-path function is declared
  in `tests/rt-safety-scope.txt`.
- `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>`
  EXIT=0 - fork-sources PASS, all-sources-reproduce PASS,
  no-upstream-regression PASS, check-namespace PASS, no-tautology PASS,
  doc-figures PASS, complexity PASS ("no new item in your files"), file-length
  PASS ("no new item in your files"), RESULT PASS (`feature-check-2.log`).
  The first run of this gate in this round was red
  (`feature-check-1.log`, EXIT=1) with exactly one item: "file-length NEW: new
  file over 500 lines: tests/src/plugins/Sf2PlayerRealtimeTest.cpp (549)". The
  fixture was therefore moved into `tests/src/plugins/Sf2PlayerFixture.h` (the
  test file is now 449 code lines, the header 126), the new header was
  registered in `tests/fork-sources.txt` and `tests/all-sources.txt` was
  re-derived with the recipe its own header publishes. No gate limit or
  baseline was changed.
- Build: `cmake --build build --target zene` EXIT=0 (`build-zene-2.log`),
  `cmake --build build --target sf2player Sf2PlayerRealtimeTest` EXIT=0
  (`build-4.log`), `cmake --build build --target MidiHandoffQueueTest
  CarlaMidiHandoffTest RemotePluginAudioPortsTest RemotePluginClientE2ETest`
  EXIT=0 (`build-family.log`).

### A note on the flaky RemotePluginClientE2ETest

`RemotePluginClientE2ETest` is flaky in this environment and says nothing about
this change: it passed in the focused run above (11.72 s) and in the earlier
runs of this round, and the round-5 report records the same flake with the
same two cases (a large-message chunking case and a wall-clock case). This
branch does not touch that path.

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with
  it (exit codes).** With the fix reverted (`git apply -R logs/fix.patch`):
  EXIT=8, `Actual (fixture.runningNotes()): 1`, `Expected (0) : 0`
  (`test-revert-2.log`). With the fix restored (`git apply logs/fix.patch`):
  EXIT=0 (`test-green-5.log`). The RED run before the fix is
  `test-red-1.log` (EXIT=8, the same failure).
- **The test is deterministic: no wall-clock timing, no sleeps, no races.** It
  uses no timing at all: the queue is filled through the player's own
  `fillNoteEventQueueForTest` seam (the loop stops when the bounded queue
  refuses the event), the note is destroyed through the player's own
  `deleteNotePluginData()`, and the running-note count is read through the
  player's own `runningNotesForTest` seam. Every step runs in the test thread
  with `Qt::DirectConnection`; nothing waits for wall-clock time and nothing
  races.
- **The fix is at the cause and complete: every caller and sibling was
  checked.** The cause is the ignored `push()` refusal in
  `deleteNotePluginData()`; the fix handles the refusal where it happens. All
  three `m_noteEvents.push()` call sites were checked (the note-on, the slot
  note-off and the standalone teardown - see "The siblings of the changed call
  were checked" above), and both note-off paths lower the same counter in the
  same way, so the two cannot disagree. The wider type the value flows into -
  `m_notesRunning`, one counter per pitch - is lowered by exactly the number
  of teardowns that were refused.
- **No collateral damage.** The saved attributes (`src`, `bank`, `patch`, the
  effect values, the gain) are untouched, so old projects load as before. No
  new limit or check was added that could refuse another caller: the fallback
  is used only where the queue already refused the event, and the counters
  cannot overflow in practice (a pitch can have at most the pool's 512 notes
  running at once). The teardown of a note whose queue push *succeeds* behaves
  exactly as before. The two new public slots are test seams: they read and
  fill the player's own state and change nothing else.
- **Nothing new runs on the audio thread that allocates, frees, locks, waits or
  logs.** The teardown path (UI thread) adds one `fetch_add` and one `store`
  on fixed-size atomics; the render path adds one `exchange` on a flag, one
  `exchange` per pitch and the `fluid_synth_noteoff()` that the standalone
  event already sent. No allocation, no mutex, no logging. The RT-safety
  sweep over the declared audio paths passes (EXIT=0), and the new function is
  declared in the scope file. Undo stays one honest step (nothing about undo
  or the command surface changed), and no new control command was added.
- **The ALREADY-FIXED / FALSE-POSITIVE question does not apply to this
  round.** The verdict is FIXED, and the code that shows the bug was there
  before this round is the `m_noteEvents.push( event )` whose result was
  thrown away in `deleteNotePluginData()`
  (`plugins/Sf2Player/Sf2Player.cpp:1445` as the round-5 review quoted it),
  followed by `releasePluginDataSlot( slot )` on the next lines. The RED run
  (`test-red-1.log`) and the reversal (`test-revert-2.log`) both show the
  note staying counted with that code in place.
- **Verdict.** The round-5 blocking finding (a teardown note-off lost when the
  bounded event queue was full) is REAL and is FIXED: the refusal is now
  handled by a bounded, allocation-free, lock-free fallback that guarantees
  the started note is stopped, and the regression that proves it fails without
  the fix. The verdict for this cluster is therefore FIXED, not ALREADY-FIXED
  and not FALSE-POSITIVE: the code in this branch before this round ignored
  the result of `m_noteEvents.push()` (`plugins/Sf2Player/Sf2Player.cpp:1445`
  before this change) and released the slot right after, which is exactly what
  the RED run above shows. The original finding of the cluster (a `QVector`
  that could allocate on the audio path) was fixed in the earlier rounds of
  this branch and stays fixed.

Files committed: `plugins/Sf2Player/Sf2Player.cpp`,
`plugins/Sf2Player/Sf2Player.h`, `tests/src/plugins/Sf2PlayerRealtimeTest.cpp`,
`tests/src/plugins/Sf2PlayerFixture.h`, `tests/fork-sources.txt`,
`tests/all-sources.txt`, `tests/rt-safety-scope.txt`,
`tests/upstream-modifications.txt`,
`docs/qwen-fixes-2026-10/cluster-new-p9-454.md`.

## Earlier rounds (kept for the record)

The round-5 state of this report (the SoundFont lifetime fix, its regression
`everySynthHasItsOwnSoundFont` and its evidence) is in the git history of
this file: commit `688357e5d` ("docs(qwen): record the round-5 evidence for
new-p9-454"). The findings it fixed were: the `QVector` note tracking that
could allocate on the audio path (rounds 2/3, commits `370df7573`,
`d73caeaca`), the FluidSynth settings object shared between two synths
(round 3, `3c57b7e54`, `4510382de`) and the `fluid_sfont_t` shared between the
holder and the candidate synths (round 5, `4510382de`).
