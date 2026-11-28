FIXED

# Cluster 3244 - A full ALSA output queue can lose note-offs silently

Branch `bugs/qwen-3244`, base `3436a808b6`. Logs: `/home/kruzzzzy/.config/kads/zene/state/runs/3244.r1/logs/`.

## Verdict: REAL

`src/core/midi/MidiAlsaSeq.cpp:374` (before this change) read

```cpp
if( m_outQueue.pushAndRequestWake( event, time, port, dueNs ) && m_pipe[1] >= 0 )
```

so a refused push had exactly one consequence: no wake byte was written. The event itself was gone.
`include/MidiOutQueue.h:47-52` (before this change) is where it went:

```cpp
if (write - read >= Capacity)
{
    m_overflow.fetch_add(1, std::memory_order_relaxed);
    return false;
}
```

That counter is a diagnostic, not a recovery: `include/MidiAlsaSeq.h:142` exposes it as
`outputQueueOverflowCount()` and nothing acts on it. Nothing downstream resends a note-off either -
`MidiAlsaSeq::processQueuedOutEvent()` sends only what the queue popped, and `MidiPort`
(`include/MidiPort.h`) keeps no per-note state and no retry: its only output bookkeeping is
`outputEventsOffered()`/`outputEventsWritten()`, counters that cannot resend anything. So once a burst
filled the queue between a note-on and its release, the receiving device held that note until it was
reset. The queue's counting made the loss *visible*, not recoverable - which is what the earlier
SKIPPED-SCOPE attempt rested on, and why it did not settle the finding.

## The fix (2 product files)

- `include/MidiOutQueue.h`: the ring now has `Capacity` (1024) ordinary slots plus `NoteOffReserve`
  (1024) slots that **only a note-off may take** (`Slots` = 2048, still a power of two, so the audio
  thread keeps a mask instead of a division). `push()` refuses an ordinary event past `Capacity` exactly
  as before and accepts a note-off up to `Slots`. It is note-off *priority* - the finding's own first
  suggestion - and because it is the **same ring**, production order is untouched: a note-off can neither
  overtake an event produced after it nor be overtaken by one produced before it. (A second reserve queue
  would have to merge two orders; an all-notes-off panic would cut notes on ports that lost nothing.
  Neither is needed.) `overflowCount()` still counts every refusal; it now only reaches 1 when even the
  reserve is full, which needs 1024 pending note-offs - a stall of seconds at any real event rate, not a
  burst.
- `src/core/midi/MidiAlsaSeq.cpp`: `enqueueOutEvent()` handles the answer explicitly - accepted: wake
  the sequencer thread as before; refused: the queue has counted it, and the comment says what that
  means and why the audio thread can do nothing more (it must not wait for room or call ALSA).

Realtime: the only audio-thread additions are one enum compare (`event.type() == MidiNoteOff`), one
compare against `Slots`, and the same slot store. No allocation, free, lock, wait, log, I/O or signal.
The probe in `AudioEnqueue_IsAllocationFreeAndLockFree` still measures 0 allocations; a note-off that
takes the reserve is a plain array assignment (`push()` allocates nothing on either path). Memory: the
two preallocated rings a client owns go from 1024 to 2048 `MidiOutQueue::Command` slots each, allocated
with the client, never on the audio thread.

Every caller and sibling checked: `MidiOutQueue` has one product user - `MidiAlsaSeq::m_outQueue`
(`m_nonAudioQueue` is the same type but carries no note-offs, so it never touches the reserve) - plus
the tests. `MidiAlsaRaw` and `MidiJack` use `MpscQueue`, not this type, so no other contract moved. No
command, no undo step, no save format, no GUI file.

## Test matrix

| Path | What happens | Where |
|---|---|---|
| normal | ordinary pushes/pops, order over 100000 events, one wake per drain | existing `MidiOutQueueTest` cases, still pass |
| failure (the finding) | queue full (1024 note-ons queued), note-off pushed: **accepted**, no overflow counted, delivered once after the 1024 | `FullQueue_StillTakesTheNoteOffOfASoundingNote` |
| exhaustion | the whole ring filled with note-offs: one more note-off is refused and counted, an ordinary event is still refused at `Capacity` | `NoteOffRoom_IsBoundedAndReservedForNoteOffs` |
| the finding's site | `MidiAlsaSeq::enqueueOutEvent()` is a two-line branch whose only input is `pushAndRequestWake()`; the queue case above is the decision it now reports, and the wake path keeps its existing test (`processOutEventRequestsTheSequencerWake`, PASS). A separate client-level case was dropped: it needs ~12 code lines in the same file, which put that file over the 500-code-line gate (509, `gate-4-file-length.log`), and a new test file cannot become a target here (`bunny-guard: configuring is the parent's job`) | `test-14-functions-final.log` |
| reconfiguration | none: the change has no configuration, no port-table entry and no persistent state | - |
| audio thread | no allocation (probe), no lock (existing `processOutEventDoesNotTakeTheSequencerMutex` still passes) | existing + new cases |
| platform | `include/MidiOutQueue.h` is header-only `std::array`/`std::atomic`; the new cases are outside `#ifdef LMMS_HAVE_ALSA`, so they build and run on the non-ALSA backends too | build of `MidiOutQueueTest` |

Determinism: no clock, no sleep, no thread in the new cases - a `MidiOutQueue` is filled and drained on
the test thread, and the assertions are about what the queue took and what it hands back.

## Commands (unpiped exit codes, copied from the logs)

Definitive red/green, run on the tree this report is committed with (`red-green-definitive.log`):

| # | Command | Exit | Log |
|---|---|---|---|
| 1 | `git diff -- include/MidiOutQueue.h src/core/midi/MidiAlsaSeq.cpp > logs/fix.patch` | 0 | `red-green-definitive.log` |
| 2 | `git apply -R logs/fix.patch` (product change removed, tests kept) | 0 | `red-green-definitive.log` |
| 3 | `cmake --build build --target MidiOutQueueTest -j4` | 0 | `build-13-red-definitive.log` |
| 4 | `cd build/tests && ctest -R '^MidiOutQueueTest$' --output-on-failure` | **8** | `test-15-red-definitive.log` |
| 5 | `git apply logs/fix.patch` (restored) | 0 | `red-green-definitive.log` |
| 6 | `cmake --build build --target MidiOutQueueTest -j4` | 0 | `build-14-green-definitive.log` |
| 7 | `ctest -R '^MidiOutQueueTest$' --output-on-failure` | **0** | `test-16-green-definitive.log` |

Red run, exactly the bug's symptom:

```
FAIL!  : MidiOutQueueTest::FullQueue_StillTakesTheNoteOffOfASoundingNote() 'queue.push(noteOff, time, nullptr)'
         returned FALSE. (the queue refused a note-off while full: the receiver keeps that note held)
FAIL!  : MidiOutQueueTest::NoteOffRoom_IsBoundedAndReservedForNoteOffs() 'accepted > MidiOutQueue::Capacity'
         returned FALSE. (no room is reserved for note-offs)
Totals: 11 passed, 2 failed, 1 skipped, 0 blacklisted, 553ms
```

Green: `Totals: 13 passed, 0 failed, 1 skipped` (`test-14-functions-final.log`, exit 0).

Neighbours and gates:

| Command | Exit | Log |
|---|---|---|
| `cmake --build build --target zene MidiOutQueueTest MidiAlsaSeqShutdownTest MidiOutTimingTest MidiClockCaptureOutputTest RetroMidiCaptureCommandsTest -j4` | 0 | `build-11-green-final2.log` |
| `ctest -R '^(MidiOutQueueTest\|MidiAlsaSeqShutdownTest\|MidiOutTimingTest\|MidiClockCaptureOutputTest\|RetroMidiCaptureCommandsTest)$' --output-on-failure` | 0 (5/5) | `test-11-green-final2.log` |
| `python3 .../qwen/feature_check.py <worktree>` | 0 | `gate-6-final.log` |

Gate table (`gate-6-final.log`, 5 changed files, exit 0): fork-sources PASS, all-sources-reproduce PASS,
no-upstream-regression PASS, check-namespace PASS, no-tautology PASS, doc-figures PASS, complexity PASS
("no new item in your files"), file-length PASS ("no new item in your files").

Wider `ctest -R Midi` sweep (`test-5-midi-sweep.log`, exit 8): the 11 targets that are built in this
worktree passed; the other 40 were **Not Run** because their binaries are not built here, which is what
the non-zero exit is. It is not a regression claim either way.

## Re-verification (second session, tree at 9035c5c95e)

The chain was re-run on the committed tree. The fix is in the commit, so `git diff` is empty and the patch
has to come from the fix commit's parent (`3436a808b6..HEAD`). A first attempt also exposed a stale build
object: `make` believed `MidiOutQueueTest` up to date and the suite reported the pre-fix symptom
(`test-18-verify-family.log`, exit 8) although the header on disk was fixed. Deleting the test object and
binary before each build made every red/green result a genuine recompile (`build-19-force-recompile.log`,
exit 0, is that recompile passing). Commands and exit codes (`red-green-definitive-3.log`):

| # | Command | Exit | Log |
|---|---|---|---|
| 1 | `git diff 3436a808b6 HEAD -- include/MidiOutQueue.h src/core/midi/MidiAlsaSeq.cpp > logs/fix.patch` | 0 | `red-green-definitive-3.log` |
| 2 | `git apply -R logs/fix.patch` | 0 | `red-green-definitive-3.log` |
| 3 | `rm -f` the test object and binary, then `cmake --build build --target MidiOutQueueTest -j4` | 0 | `build-23-red-definitive.log` |
| 4 | `cd build/tests && ctest -R '^MidiOutQueueTest$' --output-on-failure` | **8** | `test-23-red-definitive.log` |
| 5 | `git apply logs/fix.patch` | 0 | `red-green-definitive-3.log` |
| 6 | `rm -f` the test object and binary, then `cmake --build build --target MidiOutQueueTest -j4` | 0 | `build-24-green-definitive.log` |
| 7 | `ctest -R '^MidiOutQueueTest$' --output-on-failure` | **0** | `test-24-green-definitive.log` |
| 8 | `cmake --build build --target zene MidiAlsaSeqShutdownTest MidiOutTimingTest MidiClockCaptureOutputTest RetroMidiCaptureCommandsTest -j4` | 0 | `build-25-family-after.log` |
| 9 | `ctest -R '^(MidiOutQueueTest\|MidiAlsaSeqShutdownTest\|MidiOutTimingTest\|MidiClockCaptureOutputTest\|RetroMidiCaptureCommandsTest)$' --output-on-failure` | **0** (5/5) | `test-25-family-after.log` |

The red run is the same two assertions on the same two lines (`test-23-red-definitive.log`); the green run
prints `100% tests passed out of 1` and the family `100% tests passed out of 5`. Gate re-run
(`gate-8-session2.log`, exit 0): all eight rows PASS. `submit_check.py`
(`submit-check-3-session2.log`, exit 0): `RESULT: PASS - submit`.

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it:** steps 2-4 give exit
  **8** with both new cases failing on the finding's exact symptom; steps 5-7 give exit **0**
  (`red-green-definitive.log`, `test-15-red-definitive.log`, `test-16-green-definitive.log`), and again in
  the second session with the patch taken from the fix commit (`red-green-definitive-3.log`:
  `test-23-red-definitive.log` exit **8**, `test-24-green-definitive.log` exit **0**).
- **Deterministic, no wall clock, sleeps or races:** the new cases fill and drain one `MidiOutQueue` on
  the test thread; there is no thread, no clock, no device and no sleep. The seam is none - the queue is
  a plain object, and the real transport path into it stays covered by the existing
  `processOutEventRequestsTheSequencerWake` (wake) and `offAudioOutputNeverSharesTheAudioThreadsQueue`
  (producer/consumer) cases, both PASS.
- **At the cause and complete:** checked every caller and sibling. `push()` callers:
  `MidiAlsaSeq::enqueueOutEvent()` (audio thread - the finding's line, now explicit) and
  `MidiAlsaSeq::processNonAudioOutEvent()` (off-audio queue, no note-offs, unchanged); `pop()`'s
  consumers are `MidiAlsaSeq::drainOutQueue()`, its `run()` loop and the testing drain, all unchanged.
  The `Slots` mask is used by both `push()` and `pop()` so a slot cannot be written and read under
  different indices, and a `static_assert` keeps the ring a power of two. The residual case is stated at
  the constant: with 1024 note-offs already waiting, one more is refused and counted, exactly as every
  event was before - so the fix narrows the loss to a stall no real event rate reaches, and never hides
  it.
- **No collateral damage:** ordinary events keep the old `Capacity` limit (asserted by the new exhaustion
  case, and by the unchanged `FullQueue_CountsOverflowWithoutDroppingSilently` and
  `FullQueue_RequestsNoWake`); no save format, project file, command or undo path is touched, so old
  projects load and render exactly as before. Nothing persistent was added and nothing new is parsed.
- **Undo / commands:** no command id, no reversibility class, no undo entry, no command file changed (a
  bug fix must not add a control command), so `ControlCommandsSnapshot` and `ReversibilityContractTest`
  are untouched by this change.
- **Realtime:** the audio-thread additions are one enum compare and one compare against a constant; the
  ring grew, which is memory allocated with the client, not work on the audio thread. The existing
  allocation probe and the sequencer-mutex case still pass. TSan does not run on this box; reasoned
  instead: `m_outQueue` still has one producer (the audio thread) and one consumer (the sequencer
  thread), `m_write`/`m_read` keep their release/acquire pair, and the reserve changes only the bound
  compared against `write - read`, never the publication order or who writes which index.
- **Registration:** `include/MidiOutQueue.h` and `tests/src/core/MidiOutQueueTest.cpp` are fork sources
  already listed in `tests/fork-sources.txt`; `src/core/midi/MidiAlsaSeq.cpp` is an inherited file
  already listed in `tests/upstream-modifications.txt`, and its reason now also records Q3244. No new
  file was added, both list files still end with a newline, and the fork-sources /
  all-sources-reproduce gates PASS.
- **Portability:** the changed header uses no Qt API at all, no `windows.h` names, no `std::filesystem`;
  the test uses only QtTest and `MidiOutQueue`. Not compiled here for Qt 5.15 / MSVC / macOS - no such
  toolchain on this host; unverified.
- **Verdict evidence (the ALREADY-FIXED / FALSE-POSITIVE question does not apply - the verdict is
  FIXED):** the finding's code is quoted at the top with its pre-change line numbers
  (`src/core/midi/MidiAlsaSeq.cpp:374`, `include/MidiOutQueue.h:47-52`), and the fix is the two
  product hunks named under "The fix", proved by the exit codes above.
- **Not verified:** bytes on a real device. `/dev/snd/seq` does not exist in this environment, so
  `MidiOutQueueTest::queuedEventReachesAnAlsaSequencerClientPromptly` skips
  (`ALSA sequencer is unavailable`), and nothing here can watch an instrument hold or release a note.
  The claim is about what the client hands its consumer, which is what the test measures.

## Other findings (not fixed here)

- `src/core/midi/MidiAlsaRaw.cpp:272` and `src/core/midi/MidiJack.cpp:242` have the same shape - a
  refused `MpscQueue::push()` is counted and the message dropped - so a burst can lose a note-off on the
  raw-ALSA and JACK clients too. Different queue type and different producer/consumer model, and not
  this finding's file; worth its own cluster.
