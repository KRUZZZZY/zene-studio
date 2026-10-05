# Space Bunny fixer lane `midi` — batch 06

Branch `bugs/bunny-midi-2026-10-05`. Three findings were real and fixed, each in
its own commit with a regression test; one was already fixed on this branch; two
are left alone (one untestable headlessly, one out of the small-fix scope). All
three fixes are in `MidiController`, and the red/green evidence was produced by
reverting the product code with the tests in place.

### Cluster 4235 — Soft takeover can capture on the first hardware event without a crossing

Verdict: FIXED

Evidence: `src/core/midi/MidiController.cpp:99` — the crossing test compared the
incoming value with `m_lastValue`, the last value the *model* accepted, which is
stale (or still its initial `0.0`) before capture. With a target of 0.5 and
`m_lastValue` at 0, the first hardware value of 0.8 satisfied the crossing test
and captured immediately, even though the knob was already sitting at 0.8.
`MidiController` now keeps the previous *raw* hardware sample and requires an
observed crossing of the stored target; the first sample only records itself.
The within-one-MIDI-step "at target" tolerance is deliberately kept, because a
knob cannot be expected to land on an arbitrary stored value, and
`setSoftTakeoverEnabled()`, `setSoftTakeoverTarget()` and `resetSoftTakeover()`
all re-arm the detector.

Commit: `b984d366d`

Test: `ControllerSurfaceTest::SoftTakeoverNeedsAnObservedCrossingNotTheFirstSample`
(plus the now-correct assertions in `ControllerSurfaceTest::TemplatesSaveListReadAndApply`,
which previously encoded the bug itself: it asserted that a single first CC at
value 100 moves the model while takeover is armed at 0.25).

Red/green:

```
cmake --build build --target zene ControllerSurfaceTest            # EXIT=0
cd build/tests && ctest -R '^ControllerSurfaceTest$' --output-on-failure   # EXIT=8
    -> "Totals: 8 passed, 3 failed"; the new case failed with
       "Actual (moved.count(): 1) != Expected (0)" at ControllerSurfaceTest.cpp(196)
       (tests in place, product code unchanged)
cd build/tests && ctest -R '^ControllerSurfaceTest$' --output-on-failure   # EXIT=0 (after the fix)
cd build/tests && ctest -R '^(ControllerSurfaceTest|MidiControllerMenuTest|ControllerTemplateMenuTest|MidiLearnThreadTest|MidiLearnTest)$' --output-on-failure  # EXIT=0 (5/5)
bash tests/complexity-gate.sh --check                               # EXIT=0
bash tests/file-length-gate.sh --check                              # EXIT=0
```

### Cluster 4239 — Disabling LED feedback leaves the MIDI port in duplex mode

Verdict: FIXED

Evidence: `src/core/midi/MidiController.cpp:171` — enabling feedback moves an
input-only port to `Duplex` and points its output channel at this control's
channel (necessary, because `MidiPort::processOutEvent` gates on the output
channel), but `setFeedbackEnabled(false)` only cleared the flag and returned. The
port stayed exposed as an output on a channel the user never chose, and
`MidiPort::saveSettings` persists both `mode` and `outputchannel`
(`src/core/midi/MidiPort.cpp:218-230`), so those settings could be written into
the project. `MidiController` now records what feedback actually borrowed —
guarded separately for mode and channel — and gives it back when feedback is
disabled, so a control the user had already made `Duplex` on a channel of its
own is left alone and `ControllerSurface::applyTemplate` keeps working.

Commit: `2775fd0de`

Test: `ControllerSurfaceTest::DisablingFeedbackGivesThePortBack`

Red/green:

```
cmake --build build --target zene ControllerSurfaceTest            # EXIT=0
cd build/tests && ctest -R '^ControllerSurfaceTest$' --output-on-failure   # EXIT=8
    -> "Totals: 8 passed, 3 failed"; the port was still in Duplex
       (actual mode 3) where Input (1) was expected, at ControllerSurfaceTest.cpp(240)
       (tests in place, product code unchanged)
cd build/tests && ctest -R '^ControllerSurfaceTest$' --output-on-failure   # EXIT=0 (after the fix)
cd build/tests && ctest -R '^(ControllerSurfaceTest|MidiControllerMenuTest|ControllerTemplateMenuTest|MidiLearnThreadTest|MidiLearnTest)$' --output-on-failure  # EXIT=0 (5/5)
bash tests/complexity-gate.sh --check                               # EXIT=0
bash tests/file-length-gate.sh --check                              # EXIT=0
```

### Cluster 4240 — MIDI controller settings race with incoming events

Verdict: REAL-UNTESTED

Evidence: `include/MidiController.h:149-151` and `:166` —
`m_softTakeoverEnabled`, `m_softTakeoverTarget`, `m_softTakeoverCaptured` and
`m_feedbackEnabled` are plain `bool`/`float` members. They are written from GUI
and control-command paths (`setSoftTakeoverEnabled`, `setSoftTakeoverTarget`,
`setFeedbackEnabled`, and the command layer above them) and read or written by
the MIDI input thread in `MidiController::processInEvent`, with no
synchronisation anywhere on the path. The race is genuine by inspection. It
cannot be shown deterministically headlessly: it needs two real threads with a
torn-read window, and a test that merely spins both writers would be
non-deterministic and, if it ever passed, would prove nothing about the fix.
Per procedure, no product code was changed.

Commit: none

Test: none

Red/green: none

### Cluster 5133 — Feedback does not follow control changes outside MIDI input

Verdict: SKIPPED-SCOPE

Evidence: `src/core/midi/MidiController.cpp:213-215` — `feedbackByte()` derives
the byte from `m_lastValue`, which is assigned in exactly one place,
`MidiController.cpp:99` inside `processInEvent`. `sendFeedback()` itself is
called from exactly two places in this class: `MidiController.cpp:104` (after an
incoming CC) and `MidiController.cpp:210` (when feedback is switched on). There
is therefore no path from a UI edit or an automation pass to a feedback write,
which is exactly the reported symptom. A correct fix is not a small defect fix
here: `MidiController` has no pointer back to the model it drives, and
`ControllerConnection` holds the controller (`src/core/ControllerConnection.cpp:43`)
but no model back-pointer, while `Controller::hasModel`
(`src/core/Controller.cpp:233`) only walks children. It needs a new back-pointer,
a lifecycle-safe subscription so the hook is not left dangling when a controller
or connection is destroyed, and a decision about whether an automation-driven
value change should write to hardware — which touches audio-thread behaviour.
That exceeds the scope of this batch and would not be safe to land unreviewed.

Commit: none

Test: none

Red/green: none

### Cluster 5136 — Reloaded soft takeover has a zero target

Verdict: FIXED

Evidence: `src/core/midi/MidiController.cpp:255` — `saveSettings` wrote only the
`softtakeover` and `feedback` attributes and `loadSettings`
(`src/core/midi/MidiController.cpp:269`) restored only those flags, so
`m_softTakeoverTarget` stayed at its default `0.0`. A reopened project with
takeover enabled therefore armed at zero and captured on the very next
control-change — the one thing soft takeover exists to prevent. The target is now
persisted as a new project attribute and restored, clamped to 0..1; a project
saved before this change carries no attribute and keeps the old default. Loading
also re-arms the crossing detector. The target deliberately cannot be re-derived
from the controlled model at load time: `Controller::loadSettings` runs before
`ControllerConnection::finalizeConnections` binds controllers to models, so
`MidiController` has no model to ask.

Commit: `c0e005e98`

Test: `ControllerSurfaceTest::TheSoftTakeoverTargetSurvivesTheProjectRoundTrip`

Red/green:

```
cmake --build build --target zene ControllerSurfaceTest            # EXIT=0
cd build/tests && ctest -R '^ControllerSurfaceTest$' --output-on-failure   # EXIT=8
    -> "Totals: 8 passed, 3 failed"; the restored target was 0 where 0.25
       was expected, at ControllerSurfaceTest.cpp(281)
       (tests in place, product code unchanged)
cd build/tests && ctest -R '^ControllerSurfaceTest$' --output-on-failure   # EXIT=0 (after the fix)
cd build/tests && ctest -R '^(ControllerSurfaceTest|MidiControllerMenuTest|ControllerTemplateMenuTest|MidiLearnThreadTest|MidiLearnTest)$' --output-on-failure  # EXIT=0 (5/5)
bash tests/complexity-gate.sh --check                               # EXIT=0
bash tests/file-length-gate.sh --check                              # EXIT=0
```

### Cluster 4222 — JACK MIDI output discards event timing

Verdict: ALREADY-FIXED

Evidence: `src/core/midi/MidiJack.cpp:225-236` — `processOutEventAt` stores
`message.frame` from the event's offset, and `MidiJack.cpp:245-264` drains the
queue by frame, passing `midijack::cycleFrame(message.frame, nframes)` to
`jack_midi_event_write`. Output events are no longer written at frame 0. This
arrived on this branch with commit `009f591b2` ("0.5.0 catch-up wave 2 ...
frame-exact arranging"). The behaviour is pinned by three cases in
`tests/src/core/MidiJackOutScheduleTest.cpp`
(`messagesAreOrderedByFrameStably`, `aFullCycleLeavesTheRestQueued`,
`aMessageIsWrittenAtItsFrame`), which all pass.

Commit: none (`009f591b2`, already on this branch)

Test: `MidiJackOutScheduleTest` (all three cases pass)

Red/green: none

Leftovers: cluster 4240 (soft-takeover/feedback fields are unsynchronised
between the GUI/command paths and the MIDI input thread) and cluster 5133
(feedback never follows a non-MIDI value change; a correct fix needs a new
`MidiController` -> model back-pointer plus a lifecycle-safe hook) are both left
open in the code. Neither can be closed by a small fix in this lane, and 4240 has
no deterministic headless test.
