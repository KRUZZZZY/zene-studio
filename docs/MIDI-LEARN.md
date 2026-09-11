# MIDI learn — the smallest honest version

Branch `post-alpha/midi-learn`, base `0c23587d2` (the `zene-pa-midi` lane worktree).

Roadmap gap this closes: *"MIDI learn / controller surfaces — any producer with a
keyboard/controller expects to map a knob; all four rivals have it. Smallest honest
version: a single global Learn mode — focus a control → move a hardware CC → binding
saved in the project XML; no preset library, no surface support."*

Scope actually built: exactly that sentence. Nothing else.

---

## 1. What already existed (verified against this worktree, file:line)

The MIDI subsystem and the controller-binding subsystem were already there; the
missing piece was only the *learn* flow. Everything below was read, not assumed.

| Piece | Where |
| --- | --- |
| MIDI client base class | `include/MidiClient.h:45`; raw parser client `include/MidiClientRaw` at `include/MidiClient.h:124` |
| Raw-client parse → per-port dispatch | `src/core/midi/MidiClient.cpp:240` `MidiClientRaw::processParsedEvent()` → `:248` `midiPort->processInEvent(...)` |
| ALSA-sequencer input loop | `src/core/midi/MidiAlsaSeq.cpp:457` `MidiAlsaSeq::run()` (`class MidiAlsaSeq : public QThread, public MidiClient`, `include/MidiAlsaSeq.h:48`) |
| Port-level masking before the event processor | `src/core/midi/MidiPort.cpp:130` `MidiPort::processInEvent()` → `:152` `m_midiEventProcessor->processInEvent(...)` |
| CC → controller value | `src/core/midi/MidiController.cpp:74` `MidiController::processInEvent()`, `case MidiControlChange` at `:78`; matches `inputController()` and `inputChannel()`, stores `value/127.0f`, emits `valueChanged` |
| Controller ⇄ model link object | `include/ControllerConnection.h:52` `ControllerConnection : QObject, JournallingObject`; `src/core/ControllerConnection.cpp:99` `setController()` (sets `m_ownsController` for MIDI controllers), `:160` `finalizeConnections()`, `:181` `saveSettings()`, `:205` `loadSettings()` |
| Model side of the link | `src/core/AutomatableModel.cpp:488` `setControllerConnection()`, `:493-495` the two `QObject::connect`s (`valueChanged`→`dataChanged`, `destroyed`→`unlinkControllerConnection`), `:624` `unlinkControllerConnection()`, `:506` `controllerValue()` |
| Serialisation convention | `src/core/AutomatableModel.cpp:169` writes the connection into `<connection><name>` by calling `m_controllerConnection->saveSettings(...)`; reload at `:220` `setControllerConnection(new ControllerConnection(nullptr))` + `loadSettings(...)`. `MidiController::saveSettings` (`src/core/midi/MidiController.cpp:113`) embeds its `MidiPort` (`inputchannel`, `inputcontroller`, `readable`, `inports`) |
| The pre-existing partial learn flow | `src/gui/modals/ControllerConnectionDialog.cpp:51` `AutoDetectMidiController` (a `MidiController` subclass that sniffs a CC), `:94` `useDetected()`, `:302` `copyToMidiController()` |

### The gap, precisely

A learn flow *did* exist, but only inside the modal "Connection Settings" dialog:
the user opens the dialog **on one specific control** (right-click → *Connect to
controller…*, `src/gui/AutomatableModelView.cpp:200` `execConnectionDialog()`),
ticks **Auto Detect**, moves a knob, and clicks OK — at which point
`ControllerConnectionDialog::selectController()` (`:295`) copies the detected
channel/controller number into a real `MidiController` and the *caller*
(`src/gui/AutomatableModelView.cpp:207` opens the dialog, `:223`
`m->setControllerConnection(cc)`) installs the `ControllerConnection`.

What was missing is the mode the roadmap asks for: there is no global learn state
at all. You cannot arm learn once, touch a control, and move a knob — you must
re-enter a modal dialog per control, and the dialog's `AutoDetectMidiController`
lives and dies with it.

---

## 2. What this lane added

One global, one-shot learn mode. Arm it, touch a control, move a hardware CC: the
binding appears on that control as an ordinary `MidiController` +
`ControllerConnection` pair — the same objects the dialog builds — so it rides the
existing serialisation and the existing value path with no new format and no new
runtime plumbing.

**New core:** `include/MidiLearn.h` / `src/core/MidiLearn.cpp`.

- `MidiLearn::instance()` — a plain singleton (no QObject, no mutex, no hidden
  allocation).
- `setEnabled(bool)` / `isEnabled()`, `setFocusTarget(AutomatableModel*)` /
  `focusTarget()`, `bindingCount()`.
- **`MidiLearn::handleMidiEvent(const MidiEvent&)` — the learn entry point**
  (`src/core/MidiLearn.cpp:90`). Returns `true` when the event completed a learn.
  This is the single function the real MIDI clients call and the single function
  the tests drive.
- `MidiController::midiPort()` accessors added (`include/MidiController.h:74,78`) so
  a learned binding can be configured and read back.

**New GUI:** `include/MidiLearnGui.h` / `src/gui/MidiLearnGui.cpp`.

- `MidiLearnGui::setArmed(bool)` installs/removes itself as an application event
  filter on `qApp` **only while armed**.
- While armed, a `MouseButtonPress`/`FocusIn` on a widget that cross-casts to
  `gui::ModelView` sets that widget's model as the learn target. Every model-backed
  control (`Knob`, `Fader`, `LcdSpinBox`, …) inherits `QWidget` + `ModelView`
  together, so one cross-cast covers all of them; no per-widget code.
- `MainWindow` Edit menu gets a checkable **MIDI Learn** action
  (`src/gui/MainWindow.cpp:354`), slot `toggleMidiLearn()` (`:1291`), and
  `updateMidiLearnAction()` which re-reads the real state on `edit_menu::aboutToShow`.

**Wiring into the two real MIDI input paths** (both are MIDI-input threads, not the
audio thread):

- `src/core/midi/MidiClient.cpp:244` — `MidiClientRaw::processParsedEvent()`, before
  the event is fanned out to the ports (covers ALSA-raw, OSS, sndio, WinMM, JACK).
- `src/core/midi/MidiAlsaSeq.cpp:570` — the `SND_SEQ_EVENT_CONTROLLER` case of
  `MidiAlsaSeq::run()`, before the port mask is applied (covers the default Linux
  ALSA-sequencer client).

### Deliberately NOT built

No preset library. No controller-surface/template support. No multi-control batch
learn. No override of `AutomatableModelView`'s per-control context menu. No
removal UI (the existing controller dialog / controller rack still owns unbinding).
No change to `ControllerConnectionDialog` — the old modal flow keeps working
untouched.

---

## 3. Threads and real-time safety

| Step | Thread |
| --- | --- |
| Arming/disarming learn, setting the focused control | GUI thread (`MainWindow::toggleMidiLearn`, `MidiLearnGui::eventFilter`) |
| `MidiLearn::handleMidiEvent()` | MIDI **input** thread — `MidiAlsaSeq::run()` (an `QThread`) or the `QThread`-owned raw clients' read loop, replayed in tests on the main thread. It only *records* the control-change there; the binding itself is built on the GUI thread (`post-alpha/midi-race`, see `MIDI-LEARN-RACE.md`) |
| Reading the bound value while playing | audio render thread, unchanged: `AutomatableModel::value()` → `ControllerConnection::currentValue()` → `MidiController` (`src/core/AutomatableModel.cpp:506`) |

- The **audio callback has no call site into `MidiLearn`**. `AudioEngine::renderNextPeriod()`
  (`src/core/AudioEngine.cpp`, `Engine::getSong()->processNextBuffer()` at `:241`) and
  everything below it never mention it; the only two callers of
  `MidiLearn::handleMidiEvent` are the two MIDI input loops named above.
- No mutex is added to any path. `MidiLearn`'s state is `std::atomic<bool>` /
  `std::atomic<AutomatableModel*>` / `std::atomic<unsigned int>`; with learn off,
  `handleMidiEvent()` is one atomic load and an early return.
- Allocation only happens when a learn actually completes (one `MidiController` +
  one `ControllerConnection`), and it happens on the **GUI thread** — never in
  the audio callback, and no longer on a MIDI input thread (`post-alpha/midi-race`:
  the input thread leaves the channel and controller number in a lock-free slot).
  The learned controller is deliberately **parentless** so that
  no thread splices a new `QObject` into the `Song`'s child graph; lifetime is
  covered by `ControllerConnection` owning MIDI controllers
  (`src/core/ControllerConnection.cpp:129-130`) and by the target model deleting the
  connection (`src/core/AutomatableModel.cpp:77-80`).

---

## 4. Persistence

The binding rides the tree's existing format — no second format was invented.

`MidiLearn::handleMidiEvent()` ends with `target->setControllerConnection(connection)`
(`src/core/MidiLearn.cpp`), which is exactly what the dialog's caller does
(`src/gui/AutomatableModelView.cpp:222`). From there the existing machinery takes
over: `AutomatableModel::saveSettings` (`:169`) writes a `<connection>` element
containing the serialised `MidiController` (`Controller::saveState` →
`MidiController::saveSettings` → `MidiPort::saveSettings`), and on load
`AutomatableModel::loadSettings` (`:220`) rebuilds a `ControllerConnection` and
`ControllerConnection::loadSettings` (`:205`) recreates the `MidiController` from
the embedded element, with `setController()` setting `m_ownsController = true` so the
connection owns it. Round-trip is asserted by test, below.

`Song::setModified()` is called so the project is dirty and the binding is actually
written when the user saves.

---

## 5. Proofs

All numbers below are unpiped exit codes (`cmd > log 2>&1; echo EXIT=$?`).

### 5.1 Build (CI linux-x86_64 flags)

```
JOBS=4 bash tools/local-ci.sh --build-dir build --jobs 4
```
(printed deviation: `-DWANT_QT6=ON` added because this box has no Qt5 development
files; the CI runner uses qtbase5-dev.)

```
machine     : Linux x86_64, Ubuntu 24.04.4 LTS
compiler    : g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0
cmake       : cmake version 3.28.3
build dir   : build (jobs=4, ctest -j2)
CI CMAKE_OPTS: -DUSE_WERROR=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo -DTARGET_UARCH=official -DUSE_COMPILE_CACHE=ON -DWANT_DEBUG_CPACK=ON
qt flags    : -DWANT_QT6=ON

--- [1/3] configure (cmake -S . -B build ...) ---
configure EXIT=0   (log: build/configure.log)
--- [2/3] build (cmake --build build -j4) ---
build EXIT=0   (log: build/build.log)
--- [3/3] ctest (from build/tests, -j2) ---
ctest EXIT=0   (log: build/ctest.log)
ctest totals: 100% tests passed, 0 tests failed out of 26
  deviation: Qt5 development files not found: added -DWANT_QT6=ON (CI's runner installs qtbase5-dev; this box has Qt6 only)

local-ci: overall exit=0 (0 = every executed step passed)
```

(First attempt: `configure EXIT=0`, `build EXIT=1` — `src/gui/MidiLearnGui.cpp:107`
did not compile because `QWidget` was not included; the missing `#include <QWidget>`
was added and the run repeated, giving the numbers above.)

### 5.2 Tests

`tests/src/core/MidiLearnTest.cpp`, registered in `tests/CMakeLists.txt` and its
sources in `tests/fork-sources.txt`.

`MidiLearnTest` entered the suite: `10/26 Test #9: MidiLearnTest ... Passed 1.67 sec`
(`build/ctest.log`). Run directly for its per-case verdicts, unpiped:

```
$ cd build/tests && ./MidiLearnTest > /tmp/midilearn_pass.log 2>&1; echo DIRECT_EXIT=$?
DIRECT_EXIT=0
PASS   : MidiLearnTest::initTestCase()
PASS   : MidiLearnTest::SyntheticCcBindsFocusedControl()
PASS   : MidiLearnTest::BindingSurvivesProjectRoundTrip()
PASS   : MidiLearnTest::LearnOffIgnoresTheSameCc()
PASS   : MidiLearnTest::ArmedWithoutFocusTargetDoesNotBind()
PASS   : MidiLearnTest::NonControlChangeDoesNotBind()
PASS   : MidiLearnTest::AudioPeriodDoesNotLearn()
PASS   : MidiLearnTest::cleanupTestCase()
Totals: 8 passed, 0 failed, 0 skipped, 0 blacklisted, 1598ms
```

`26` = the `25` pre-existing tests in `tests/CMakeLists.txt` plus this one; 0 tests
was never reached (26 ran).

Test-by-test:

| Test | What it pins |
| --- | --- |
| `SyntheticCcBindsFocusedControl` | arm + focus a `FloatModel`, feed the synthetic CC into `MidiLearn::handleMidiEvent`, assert a `MidiController` binding exists with the transmitted channel (2 → port channel 3) and controller number (74); assert learn auto-disarmed and the focus target was consumed; assert a CC on the bound port reaches the model's `dataChanged` while a CC on a different controller number or channel does **not** |
| `BindingSurvivesProjectRoundTrip` | create the binding via learn, `saveSettings` → XML, assert the XML carries the existing `<connection>`/`Midicontroller`/`inputcontroller` shape, `loadSettings` into a fresh model, assert channel/controller are identical and actually drive the reloaded model (and not the original) |
| `LearnOffIgnoresTheSameCc` | **negative control**: with learn off the same synthetic CC creates no binding, consumes nothing, and 512 CCs leave the path inert |
| `ArmedWithoutFocusTargetDoesNotBind` | armed but nothing focused → no binding, still armed |
| `NonControlChangeDoesNotBind` | note-on and pitch-bend never map |
| `AudioPeriodDoesNotLearn` | with learn armed and a control focused, ~10 real audio periods (the `AudioDummy` thread calls `AudioEngine::renderNextPeriod()` for the whole test) produce no binding and leave learn armed |

Inverted-check demonstration: `MIDILEARN_NEGATIVE_CONTROL_INVERTED=1` flips the
negative control's expectation, so the run must FAIL — that is the evidence the
negative control is a real check:

```
$ cd build/tests && MIDILEARN_NEGATIVE_CONTROL_INVERTED=1 ./MidiLearnTest > /tmp/midilearn_inverted.log 2>&1; echo INVERTED_EXIT=$?
INVERTED_EXIT=1
FAIL!  : MidiLearnTest::LearnOffIgnoresTheSameCc() 'MidiLearn::instance()->handleMidiEvent(makeCc(2, 74, 127))' returned FALSE. ()
   Loc: [.../tests/src/core/MidiLearnTest.cpp(208)]
Totals: 7 passed, 1 failed, 0 skipped, 0 blacklisted, 1601ms
```

### 5.3 Call-site inventory (the "nothing in the audio callback" evidence)

```
$ grep -rn "handleMidiEvent" src include tests | grep -v build | grep MidiLearn
src/core/midi/MidiClient.cpp:244:	MidiLearn::instance()->handleMidiEvent(m_midiParseData.m_midiEvent);
src/core/midi/MidiAlsaSeq.cpp:570:					MidiLearn::instance()->handleMidiEvent( ccEvent );
src/core/MidiLearn.cpp:90:bool MidiLearn::handleMidiEvent(const MidiEvent& event)
include/MidiLearn.h:68:	bool handleMidiEvent(const MidiEvent& event);

$ grep -rn "MidiLearn" src/core/AudioEngine.cpp src/core/Song.cpp src/core/AudioBuffer.cpp | wc -l
0
```

Two production call sites, both in MIDI **input** loops; the audio engine, the song
per-period work and the buffer code never mention `MidiLearn`.

---

## 6. What is NOT proven

- **No hardware MIDI controller was used.** There is none on this box. Every proof
  feeds a synthetic `MidiEvent` into the same function the real MIDI client calls
  (`MidiLearn::handleMidiEvent`, `src/core/MidiLearn.cpp:90`), but nothing here
  demonstrates a real keyboard's CC arriving through ALSA.
- **The GUI interaction was not exercised.** There is no X display here, so
  `MidiLearnGui`'s event filter, the Edit-menu action, and the "touch a control"
  gesture are compiled but untested. The headless tests set the focus target
  programmatically via the same `MidiLearn::setFocusTarget()` the GUI calls.
- **ALSA-sequencer wiring is untested.** The `MidiAlsaSeq.cpp` call site is compiled
  (`LMMS_HAVE_ALSA` on this box) but not exercised; the tests use `Engine::init(true)`,
  which forces `MidiDummy`. The raw-client call site is likewise compiled, not driven.
- **Learn on a control that is already bound** replaces the old connection's
  controller; the old `MidiController` is deleted. Not covered by a test.
- **Superseded by `post-alpha/midi-race`** (see `MIDI-LEARN-RACE.md`): the binding
  is no longer built on the MIDI input thread, so `Song::setModified()` and the
  model's `dataChanged` emission now happen on the GUI thread, and the Edit-menu
  tick is reconciled by `MidiLearnGui`'s bind timer instead of waiting for
  `aboutToShow`. Both are asserted in `MidiLearnThreadTest`.

---

## 7. Files

New:

```
include/MidiLearn.h
include/MidiLearnGui.h
src/core/MidiLearn.cpp
src/gui/MidiLearnGui.cpp
tests/src/core/MidiLearnTest.cpp
tests/src/core/MidiLearnThreadTest.cpp
docs/MIDI-LEARN.md
docs/MIDI-LEARN-RACE.md
```

Changed:

```
include/MidiController.h        midiPort() accessors
include/MainWindow.h            m_midiLearnAction + two slots
src/core/CMakeLists.txt         core/MidiLearn.cpp
src/core/midi/MidiClient.cpp    learn hook in processParsedEvent()
src/core/midi/MidiAlsaSeq.cpp   learn hook in the CC case of run()
src/gui/CMakeLists.txt          gui/MidiLearnGui.cpp
src/gui/MainWindow.cpp          Edit-menu MIDI Learn action + slots
tests/CMakeLists.txt            MidiLearnTest
tests/fork-sources.txt          the four new src/include files
tests/QA-GATES.md               fork-source count 99 -> 103
```
