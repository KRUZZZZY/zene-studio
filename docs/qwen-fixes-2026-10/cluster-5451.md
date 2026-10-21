# Cluster 5451 - the VeSTige VST host (plugins/Vestige/Vestige.cpp)

Verdict: FIXED for new-p14-358, ALREADY-FIXED for 5451.

5451: ALREADY-FIXED
new-p14-358: FIXED

Two findings in one file, one tree, one build. Each got its own decision, its own proof and its
own commit.

| Bug | Verdict | Where | Proof |
|---|---|---|---|
| 5451 - "Vestige locks the plugin mutex on the audio path" | ALREADY-FIXED | `plugins/Vestige/Vestige.cpp:449` (`handleMidiEvent`), `plugins/Vestige/Vestige.cpp:433` (`processLock`) | the rt-safety sweep now measures the VeSTige note and render paths (`tests/rt-safety-scope.txt`); ctest `RtSafetySweep` |
| new-p14-358 - "Turn off all notes" only sends note-offs on MIDI channel 0 | FIXED | `plugins/Vestige/Vestige.cpp:842` (`VestigeInstrumentView::noteOffAll`) | `tests/src/plugins/VestigeMidiHandoffTest.cpp` (`turnOffAllEndsTheNotesOfEveryMidiChannel`) |

Logs: `$L = /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5451.r1/logs`.

---

## new-p14-358 - "Turn off all notes" only ended the notes of MIDI channel 0

### The bug

`VestigeInstrumentView::noteOffAll()` - the "Turn off all notes" button of the instrument view
(`plugins/Vestige/Vestige.cpp:842`, the button is created at `plugins/Vestige/Vestige.cpp:621`) -
queued a note-off for every key of **channel 0 only**:

```cpp
for( int key = 0; key <= MidiMaxKey; ++key )
{
        HandoffMidiEvent off;
        off.type = static_cast<std::uint8_t>( MidiNoteOff );
        off.channel = 0;                       // <- channel 0 only
        ...
}
```

The plug-in can receive events on all sixteen MIDI channels (`handleMidiEvent()` forwards the
channel of every event it is given, `plugins/Vestige/Vestige.cpp:456`), so a note that was
sounding on another channel of a multitimbral VST kept sounding after the button was pressed.

### The fix

`plugins/Vestige/Vestige.cpp:842` - the button now queues an **all-notes-off (MIDI CC 123) for
every one of the sixteen channels** through the same overflow-safe hand-off path
(`pushOrRecord()`), after the note-offs of channel 0 it has always sent:

```cpp
for( int channel = 0; channel < 16; ++channel )
{
        HandoffMidiEvent cut;
        cut.type = static_cast<std::uint8_t>( MidiControlChange );
        cut.channel = static_cast<std::int8_t>( channel );
        cut.param1 = 123; // All Notes Off
        cut.param2 = 0;
        pushOrRecord( m_vi->m_midiHandoff, m_vi->m_midiOverflow, cut );
}
```

Why this message and this path:

- **CC 123 is the repo's own "cut this channel" message.** The overflow path already sends it
  when the hand-off queue refuses an event (`plugins/Vestige/Vestige.cpp:414-418`), and so do the
  other hosts (`include/MidiHandoffQueue.h:566-574`, `src/core/RemotePlugin.cpp`). The plug-in
  side of the contract does not change.
- **The ordering property is kept.** The cut goes through the same queue as the notes it has to
  end, so a note-on that was queued before the button was pressed is still ended by the cut, and
  a note-on queued after it starts a new note - the behaviour the queue was introduced for
  (`plugins/Vestige/Vestige.cpp:844`).
- **Channel 0 keeps what the button always sent** (a note-off per key), so a plug-in that
  honours note-offs but not the controller still ends its channel-0 notes. The added
  all-notes-off is a superset, not a replacement.
- **Bounded.** 128 + 16 = 144 events fit the 256-slot hand-off queue
  (`plugins/Vestige/Vestige.h:126`), so the button does not even depend on the overflow path in
  the normal case; the overflow path stays the safety net (a refusal is recorded as a channel
  cut, which is the same all-notes-off).

### The regression test

`tests/src/plugins/VestigeMidiHandoffTest.cpp` (new, fork-new, registered in
`tests/fork-sources.txt`; the plug-in's own sources are compiled into it, the SlicerT and Vibed
tests' shape, so the code under test is the shipping one).

`turnOffAllEndsTheNotesOfEveryMidiChannel` drives the **shipping button**: it builds the
instrument view, finds the "Turn off all notes" button, clicks it, drains the hand-off queue the
way `VestigeInstrument::processImpl()` drains it (`drainMidi()`, `include/MidiHandoffQueue.h:555`)
and asserts that **every** MIDI channel is covered - either by an all-notes-off or by a note-off
for every key of the channel - and that the note that was sounding on channel 3 is ended *after*
the note-on that started it.

The assertion states the requirement, not the implementation: a future fix that sends per-key
note-offs for every channel would satisfy it too.

### RED / GREEN (step 4b)

```
$ git diff b61e5f965..HEAD -- plugins/Vestige/Vestige.cpp > $L/fix.patch && git apply -R $L/fix.patch
EXIT=0
$ cmake --build build --target VestigeMidiHandoffTest -j8            # $L/build-18-red.log
EXIT=0
$ cd build/tests && ctest -R '^VestigeMidiHandoffTest$' --output-on-failure   # $L/test-19-red.log
EXIT=8
  FAIL!  : VestigeMidiHandoffTest::turnOffAllEndsTheNotesOfEveryMidiChannel() 'covered[channel].covered()'
           returned FALSE. (channel 1 was not cut: a note that is sounding on it keeps sounding
           after "Turn off all notes")
  Totals: 3 passed, 1 failed, 0 skipped, 0 blacklisted, 2039ms

$ git apply $L/fix.patch
EXIT=0
$ cmake --build build --target VestigeMidiHandoffTest vestige -j8    # $L/build-19-green.log
EXIT=0
$ cd build/tests && ctest -R '^VestigeMidiHandoffTest$' --output-on-failure   # $L/test-20-green.log
EXIT=0
  100% tests passed out of 1
```

The failing assertion names the bug exactly: **channel 1** was not cut (channel 0 was, by its
note-offs).

### The rest of the family still passes

```
$ cd build/tests && ctest -R 'RtSafety|MidiHandoffQueue|VestigeMidiHandoff|AudioPluginTest|VstEffectControls|ZynNoteOff' --output-on-failure
EXIT=0        # 7 of 7 tests passed ($L/test-13-family.log)
```

(The two failures seen while assembling this were missing build-tree artifacts - `libeq.so`, the
`gate` module and the `nam_harness` fixture were not built in this tree - and not the change:
after building them, all seven pass.)

---

## 5451 - "Vestige locks the plugin mutex on the audio path"

The finding names two places where the plug-in mutex is taken on the audio path:
`processLock()` ("takes `m_pluginMutex` for each processing call, and waits indefinitely during
export") and `handleMidiEvent()` ("also takes the mutex with a blocking lock").

### `handleMidiEvent()` - ALREADY-FIXED on this branch

On this branch the note path takes **no mutex at all**. `VestigeInstrument::handleMidiEvent()`
(`plugins/Vestige/Vestige.cpp:449-463`) publishes the event into the bounded, lock-free hand-off
queue and returns:

```cpp
bool VestigeInstrument::handleMidiEvent( const MidiEvent& event, const TimePos& time, f_cnt_t offset )
{
        // Realtime: this is called from the audio thread too, so it never takes m_pluginMutex. ...
        HandoffMidiEvent queued;
        ...
        pushOrRecord( m_midiHandoff, m_midiOverflow, queued );

        return true;
}
```

The fix is commit `f3953cc8e` ("fix(instruments): Vestige, Carla and ZynAddSubFx queue MIDI
instead of locking for it (cluster 5451, 5508, 908)"), whose own message admits that
"the note path through each instrument is unproven" - that gap is closed below.

The repo already records this half of the cluster as fixed:
`docs/m0-blocker-fixes-2026-10-08/D-hosting.md` - "5451 remote MIDI under the lock | FIXED |
72c405a50, f3953cc8e".

**The proof that it stays fixed** is the repo's own realtime programme. `tests/rt-safety-scope.txt`
is the declared set of audio-thread paths that the whole-tree sweep measures; the VeSTige note and
render paths were **not** in it (the sibling Carla's were), so nothing measured them. This change
declares them:

```
plugins/Vestige/Vestige.cpp	VestigeInstrument::handleMidiEvent	MIDI may arrive on the audio thread (the note path), the GUI thread or the input thread; this producer only publishes a fixed-size event into the bounded hand-off queue
plugins/Vestige/Vestige.cpp	VestigeInstrument::processImpl	VeSTige's per-period render path: it drains the queued events into the plug-in and calls the plug-in's process
```

With them declared, ctest `RtSafetySweep` measures both bodies: **0 hits** (no allocation, no
lock, no container growth, no synchronous stdio).

RED/GREEN of that proof (the negative control is the pre-fix shape of `handleMidiEvent()`, the
blocking `m_pluginMutex.lock()` that `f3953cc8e` removed):

```
$ git apply $L/negctl-5451.patch        # puts the blocking lock back
EXIT=0
$ cd build/tests && ctest -R '^RtSafetySweep$' --output-on-failure    # $L/test-8-sweep-red.log
EXIT=8
  FAIL: NEW rt-safety hit: plugins/Vestige/Vestige.cpp:VestigeInstrument::handleMidiEvent:lock-call
        [locking] - 1 occurrence(s), first at plugins/Vestige/Vestige.cpp:451: m_pluginMutex.lock();

$ git apply -R $L/negctl-5451.patch     # the tree as it is
EXIT=0
$ cd build/tests && ctest -R '^RtSafetySweep$' --output-on-failure    # $L/test-9-sweep-green.log
EXIT=0
  100% tests passed out of 1
```

The runtime half of the same claim is in the new regression test:
`aNoteIsQueuedForThePlugin` drives `handleMidiEvent()` from the test and asserts that the note and
its note-off reach the plug-in in order through the queue, with no cut in between.

The whole-tree scope measurement is the repository's own realtime programme
(`tests/rt-safety-scope.txt`, `tests/rt-safety-sweep.py`, ctest `RtSafetySweep`): "no allocation,
no locking, no unbounded growth on audio-thread paths". It measures only what the scope file
declares - "a new audio-thread path that nobody declares is measured by nothing here" - which is
why the declaration is part of this fix.

### `processLock()` - the documented host contract, not a VeSTige defect

What is left of the finding is `VestigeInstrument::processLock()`
(`plugins/Vestige/Vestige.cpp:433-436`):

```cpp
auto VestigeInstrument::processLock() -> bool
{
        return m_pluginMutex.tryLock(Engine::getSong()->isExporting() ? -1 : 0);
}
```

- It is the **documented host contract**, not a VeSTige invention: `include/AudioPlugin.h:110-124`
  states that "Some LMMS plugins currently require a lock around their process method to prevent
  threading issues ... plugins may override these lock/unlock methods to ensure their process
  method is protected", and the audio path consumes it without waiting
  (`include/AudioPlugin.h:166-189`: a failed lock simply skips the period).
- The line is **byte-identical in the three sibling hosts**: `plugins/LadspaEffect/LadspaEffect.cpp:253`,
  `plugins/ZynAddSubFx/ZynAddSubFx.cpp:390`, `plugins/VstEffect/VstEffect.cpp:128`. A fix that
  changed it in VeSTige alone would make one host of four behave differently from the other three.
- **Outside the export the audio path never waits**: the live arm is `tryLock(0)`, a
  non-blocking attempt; when the GUI thread holds the mutex the period is skipped, exactly as
  before.
- **The export arm waits on purpose**: `-1` means "wait for the mutex", so that the rendered file
  has no dropped period while the GUI thread is inside the plug-in (plug-in load, project save).
  Replacing it with a non-blocking attempt in VeSTige alone would silently put a hole in the
  exported audio of one host - a product decision about the shared `ProcessMutex` contract, not a
  VeSTige bug, and a change the three siblings would need as well.

So the VeSTige-specific part of the finding - a **blocking** lock on the audio path - is fixed on
this branch, and the remaining part is the shared, intentional contract.

---

## What was changed

| File | Change |
|---|---|
| `plugins/Vestige/Vestige.cpp` | `VestigeInstrumentView::noteOffAll()`: an all-notes-off for every MIDI channel (new-p14-358) |
| `tests/src/plugins/VestigeMidiHandoffTest.cpp` | new: the button and the note path of the shipping plug-in (both clusters) |
| `tests/CMakeLists.txt` | the new test is registered and builds the VeSTige plug-in's own sources |
| `tests/fork-sources.txt` | the new test source and this report are registered as fork-new |
| `tests/all-sources.txt` | the whole-tree source manifest is re-derived: the recipe lists the new test source |
| `tests/rt-safety-scope.txt` | the VeSTige note and render paths are declared audio-thread paths (5451) |

## Gates

```
$ python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>       # $L/feature-check-4.log
EXIT=0
  PASS  fork-sources / all-sources-reproduce / no-upstream-regression /
        check-namespace / no-tautology / doc-figures / complexity / file-length
  RESULT: PASS

$ python3 ~/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> 5451   # $L/submit-check-2.log
EXIT=0
  RESULT: PASS - submit
```

(The first gate run failed on the complexity gate: the new test case was over the target at CCN 16.
The case was split into helpers, the assertion was not weakened, and the gate passes. The second
gate run failed because `tests/all-sources.txt` had not been re-derived for the new test source;
the manifest is the recipe's output again and both gates pass.)

## Self-check

**1. Does the regression test FAIL with the product change reverted and PASS with it?**
Yes, both clusters have their own proof, quoted above with the exit codes:
new-p14-358: `ctest -R '^VestigeMidiHandoffTest$'` -> **EXIT=8** with the product hunk reverted
($L/test-15-red.log, "channel 1 was not cut") and **EXIT=0** with it restored
($L/test-16-green.log). 5451: `ctest -R '^RtSafetySweep$'` -> **EXIT=8** with the blocking lock
put back into `handleMidiEvent()` ($L/test-8-sweep-red.log, "NEW rt-safety hit ...
lock-call") and **EXIT=0** on the tree as it is ($L/test-9-sweep-green.log).

**2. Is the test deterministic - no wall-clock timing, no sleeps, no races?**
Yes. `VestigeMidiHandoffTest` runs on ONE thread: it injects the MIDI events itself
(`MidiEvent(MidiNoteOn, 3, 60, 100)`), clicks the button, and reads the queue's own state
(`drainMidi()` over `MidiHandoffQueue<256>`, the same helper `processImpl()` uses). No clock, no
sleep, no timeout, no second thread, no race. The sweep is a static source measurement, so it
cannot race either. The values are injected, not sampled.

**3. Is the fix at the cause and complete - every caller and sibling checked?**
- new-p14-358: the cause is the channel-0-only loop in `VestigeInstrumentView::noteOffAll()`;
  that is the only place that ends the plug-in's notes (`plugins/Vestige/Vestige.cpp:842`, called
  from the view's button at `plugins/Vestige/Vestige.cpp:628`). The fix is inside that function,
  and the events go through the same `pushOrRecord()` path as every other event the instrument
  sends, so the queue-full case keeps its channel cut (`plugins/Vestige/Vestige.cpp:414-418`).
  The other hosts have no such button, so there is no sibling to keep consistent; the *shared*
  helper the fix uses (`pushOrRecord()`, `include/MidiHandoffQueue.h:517`) is unchanged.
- 5451: the note path is the only place of the two that the branch could fix, and it is fixed;
  the remaining `processLock()` is the shared contract with three sibling hosts and is left
  consistent with them on purpose (the check above).

**4. No collateral damage - do old projects and saved data still load as before?**
Nothing about the saved format changed: no settings, no serialised state, no project XML is
touched by either change. The button now sends 16 more MIDI messages (144 events instead of 128);
the hand-off queue holds 256, so nothing is refused and no other event is delayed. The note-offs
of channel 0 are still sent exactly as before, and the all-notes-off is an addition. No limit,
capacity or check was added that another caller could hit.

**5. If the verdict is ALREADY-FIXED: the code (file:line) or the intended contract that shows it.**
The code: `plugins/Vestige/Vestige.cpp:449-463` - `handleMidiEvent()` publishes into
`m_midiHandoff` / `m_midiOverflow` (`plugins/Vestige/Vestige.h:123-128`) and never touches
`m_pluginMutex`; the declared scope (`tests/rt-safety-scope.txt`) makes the sweep measure that
body, and it measures zero hits. The intended contract for the part that remains:
`include/AudioPlugin.h:110-124` (the `ProcessMutex` contract) with the three sibling hosts'
identical `tryLock(isExporting() ? -1 : 0)` (`plugins/LadspaEffect/LadspaEffect.cpp:253`,
`plugins/ZynAddSubFx/ZynAddSubFx.cpp:390`, `plugins/VstEffect/VstEffect.cpp:128`). The finding's
own wording ("waits indefinitely during export") describes that contract, not a VeSTige defect.

**Realtime, undo and commands.** The audio thread is untouched by the new-p14-358 fix: the button
runs on the GUI thread and only publishes into the bounded lock-free queue; no allocation, no
lock, no waiting, no logging was added to any audio-thread path (the sweep measures the two
declared VeSTige bodies and finds nothing). Undo is untouched - no undoable step was added - and
no control command was added for this bug fix.
