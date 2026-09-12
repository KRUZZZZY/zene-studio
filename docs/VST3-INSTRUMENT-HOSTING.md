# VST3 instrument hosting — what was built, what was measured, what a user cannot do

**Branch:** `post-alpha/instrument-hosting-impl` · **Worktree:**
`projects/lmms-fl-research/zene-pa-instrhost` · **Base:**
`post-alpha/vst3-instrument-fixture` (`f32dc7cd1`)

**Spec this implements:** `docs/INSTRUMENT-HOSTING-SPEC.md` §1 (the effect-host inventory), §3 (the
smallest honest slice) and §4 (the open questions, each naming the file that changes).

**Verdict:** one VST3 instrument loads onto one track, receives MIDI from a real `MidiClip` through
the track's existing MIDI path with sample-accurate timing, renders audio into the track, and its
state survives save/reload of the project. The plug-in's own editor (`IPlugView`) is **not**
implemented; §7 says what that means for a user, and §9 names the links in the chain that are not
solid.

---

## 1. What the spec said, and what this work found

The spec's two conclusions held up, and both deltas were real:

| Spec claim | Outcome |
| --- | --- |
| The VST3 **effect** host is real and well factored; most instrument machinery is reusable (§0, §1.3, §1.8) | Confirmed. No second host was forked: `plugins/Vst3Instrument` reuses `Vst3Host`, `Vst3BusMap`, `Vst3Parameter`, `Vst3SubPluginFeatures` unchanged in behaviour. |
| Delta 1: no event bus, `ProcessData::inputEvents` never assigned | Confirmed (`grep -n inputEvents plugins/Vst3Effect/` returned nothing before this change). Fixed — §3. |
| Delta 2: no `IPlugView` anywhere in the tree | Confirmed. Still true. Deferred with a design — §6. |
| `AudioPlugin<Instrument, …>` already exists, the browser already filters instruments (§1.8, §1.10) | Confirmed, and both were used as-is. `Vst3SubPluginFeatures(Plugin::Type::Instrument)` needed **no** change. |
| Q14: the three orphaned VST3 test files were never compiled | Confirmed. They are still not wired (out of this slice); the two new tests below are wired under the fixture option instead. |

**Things the spec could not know, found while implementing** (all in §8): the ring buffer's
per-slot sequence numbers, the missing `lmms_plugin_main` entry point, `Clip`'s self-registration
with its track, `MidiClip::addNote`'s piano-roll dependency, `Key::saveXML`'s shape,
`InstrumentTrack::processOutEvent`'s early return, and `InstrumentView::setModel`'s crash in a
harness. Each was found by a failing test, not by inspection.

---

## 2. The shape of the slice

New plug-in module (one shared library exposes exactly one descriptor —
`src/core/PluginFactory.cpp:177-185` — so an instrument cannot share `vst3effect.so`):

```
plugins/Vst3Instrument/Vst3Instrument.{h,cpp}       the instrument: MIDI in, audio out, state
plugins/Vst3Instrument/Vst3InstrumentView.{h,cpp}   the parameter grid a user gets (no IPlugView)
plugins/Vst3Instrument/CMakeLists.txt               module + its share of the host sources
plugins/Vst3Instrument/logo.png                     browser icon
```

Touched: `plugins/Vst3Effect/Vst3Host.{h,cpp}` (the event path — shared by both hosts),
`cmake/modules/PluginList.cmake` (register the plug-in), `cmake/modules/Vst3Sdk.cmake` (make the
target idempotent, now that two directories include it), `tests/CMakeLists.txt`,
`tests/fork-sources.txt`, `tests/all-sources.txt`.

**Documented policy decisions** (spec §4's open questions):

- **Q1 (which event input).** The host drives the **first** input event bus that is active by
  default, activates it explicitly, and deactivates the others. Recorded in the code at the point
  of the decision (`Vst3Host.cpp`, `load()`).
- **Q2 (who owns the event queue).** A bounded, lock-free multi-producer/multi-consumer ring with a
  per-slot sequence number (`kMidiQueueCapacity = 1024`). MIDI reaches an instrument from the audio
  thread (a note handle built during the period) *and* from the MIDI/GUI thread, so a single-producer
  ring would be wrong and a lock is not allowed. A full queue **refuses and counts** the event
  (`droppedMidiEvents()`), so nothing grows.
- **Q3 (frame of reference).** LMMS' `f_cnt_t offset` and VST3's `Event::sampleOffset` share one
  origin — proved, not assumed: §4.
- **Q4 (flags).** `IsSingleStreamed | IsMidiBased`, the LV2 precedent
  (`plugins/Lv2Instrument/Lv2Instrument.cpp:78`).
- **Q5 (audio input).** The instrument asks the track for **no** audio input (a VST3 instrument
  generates its audio). An instrument that declares audio input buses is fed silence by the host,
  which is what `AudioPlugin`'s single-bus instrument transport can express.
- **Q7 (node name).** `Vst3Instrument::nodeName()` returns `vst3instrument`; the state blobs are
  `<componentstate>` / `<controllerstate>` inside it, next to the `<key>`. Frozen: changing either
  would break saved projects.
- **Q8 (shared host code).** Option (a): the new module compiles `Vst3Effect/Vst3*.cpp` directly,
  the same way `ClapEffectIntegrationTest` compiles the CLAP host's sources
  (`tests/CMakeLists.txt:526-535`). No new static library, no move to `src/`.
- **Q12 (class identity).** Unchanged: the key still stores the class **name**, not the CID. Filed
  as a known defect (pre-existing, affects effects too), not fixed here.
- **Q11 (multi-out)** and **Q10 (instrument latency into PDC)**: out of the smallest slice, as the
  spec requires. Not attempted.

---

## 3. The event-bus wiring

`HostedPlugin` gained exactly what the spec predicted, in the files it predicted:

1. `load()` enumerates `kEvent`/`kInput` buses, records the bus index, and activates the chosen bus.
   **Only for instruments**: an effect's event buses and `ProcessData::inputEvents` are left exactly
   as they were, which is why the behaviour proof in §5 can be a plain render comparison.
2. `prepare()` sizes the VST3 `EventList` (256 events), the ordering scratch, and wires
   `processData.inputEvents = &inputEvents` — for instruments with an event bus only.
3. `pushMidiEvent(MidiEventIn)` — lock-free, allocation-free, callable from any thread.
4. `process()` drains the ring into the event list **in sample order** (stable insertion sort, no
   allocation) and clamps each offset into the block.
5. `Vst3Instrument::handleMidiEvent()` translates LMMS' `MidiEvent` + offset into `MidiEventIn`:
   note-on, note-off, note-on-with-velocity-0 (the MIDI idiom for note-off), key pressure, and
   control change. SysEx, program change, channel pressure and pitch bend are **not** carried and
   are named as such in §7.

The ordering guarantee is structural rather than hopeful:
`InstrumentPlayHandle::play()` processes the track's `NotePlayHandle`s **before** it calls the
instrument (`src/core/InstrumentPlayHandle.cpp:43-69`), so the MIDI for a block is already queued
when `processImpl()` drains it in the same block.

---

## 4. The sample-accurate timing proof

The fixture's probe established the contract against the SDK directly: a note-on at frame 0 and a
note-off at frame 256 in a 512-frame block give `sample[255] = 0.5`, `sample[256] = 0.0`. Both
halves of that are reproduced **through the host**, and then through the whole product:

**Host level** (`tests/src/plugins/Vst3InstrumentTest.cpp`, `testSampleAccurateNoteOff`):

```
through the host: sample[0]=0.500000 sample[255]=0.500000 sample[256]=0.000000 sample[511]=0.000000
```

**Product level** (`tests/src/plugins/Vst3InstrumentIntegrationTest.cpp`,
`testAMidiClipDrivesAudioWithSampleAccuracy`) — a `MidiClip`'s own `Note`, played through
`InstrumentTrack::processOutEvent` → `Instrument` → the host, with the note started 128 frames in:

```
instrument: midiBased=1 singleStreamed=1 portsModel=... parameters=1
clip -> track -> instrument: RMS=0.500000, sample[127]=0.000000, sample[128]=0.500000
```

The comparators, so "non-silent" cannot be an artefact: a block with no events is **exactly silent**;
the same note at a different offset renders **differently**; a note released after two blocks renders
differently from a note held throughout; a note started 128 frames late renders differently from the
same note started at 0; and the level parameter changing 0.5 → 0.25 produces a different render
(asserted as exact sample values, not a threshold).

Real-time behaviour is measured, not asserted: `testProcessAllocatesNothing` wraps eight
MIDI-carrying blocks in `tests/src/core/AllocationProbe.h` and reports

```
allocation probe: 8 MIDI-carrying blocks, 0 allocations on the process thread
```

and `testTheQueueIsBoundedAndCountsWhatItDrops` floods the queue with 4096 events and asserts
exactly `4096 − 1024` are refused and counted — bounded, no growth, no blocking.

---

## 5. The no-instrument behaviour proof (measured spread, not sha256)

Renders in this tree are **not** bit-reproducible, so the measure is the largest sample difference
against a same-build run-to-run floor, plus a sensitivity control that must differ. Reproduce with:

```
bash tests/vst3-instrument-render-proof.sh build/lmms build/lmms-baseline-featureabsent
```

Subject: `data/projects/tutorials/editing_note_volumes.mmp` — one built-in instrument
(`tripleoscillator`), no VST3 plug-in anywhere, i.e. a project whose render cannot touch the new
code path. Baseline binary: the **same build directory, configured without VST3**, saved before the
feature was configured in.

```
same-build run-to-run floor : max|delta| = 0 LSB, -inf dB
baseline vs new (subject)   : max|delta| = 0 LSB, -inf dB
sensitivity control         : max|delta| = 13105 LSB, -7.964 dB
```

The control is the same project with `mastervol` 100 → 60 (and nothing else, so the render length is
identical): the measurement sees it at 13,105 LSB / −7.96 dB. An earlier run of the identical
command measured the floor at **1 LSB (−130.043 dB)** with the subject at **1 LSB (−127.033 dB)** —
so the honest statement is that the floor is **at most 1 LSB** and the baseline-vs-new difference is
**within it** in both runs. The new code's effect on a project with no VST3 instrument is therefore
unmeasurable, and it is structurally unreachable (§3, item 1).

---

## 6. State round-trip

Written under `<instrument name="vst3instrument">`, inside the element the track serializer creates,
next to the plug-in key:

```xml
<instrument name="vst3instrument">
  <vst3instrument>
    <componentstate>…base64…</componentstate>
    <controllerstate>…base64…</controllerstate>
    <key><attribute name="file" value="…/vst3-test-instrument.vst3"/>
         <attribute name="class" value="Zene VST3 Test Instrument"/></key>
  </vst3instrument>
</instrument>
```

Two independent round-trips are asserted:

- **Host level** (`testStateRoundTripsIntoAFreshInstance`): set state, save it, load it into a
  plug-in that has never seen it, and assert the **restored state is audible** (`0.75` where the
  fresh instance defaulted to `0.5`), with the saved and restored renders identical.
- **Product level** (`testStateSurvivesTheProjectFile`): set the state through the parameter model,
  save through `InstrumentTrack::saveState()`, then `restoreState()` into a **fresh track with no
  instrument**, which re-instantiates the plug-in from the project's own key and hands it the saved
  state. Measured:

```
project round trip: saved 0.750000, reloaded 0.750000 (identical render)
```

The parameter model reaches the plug-in through `Vst3Instrument::poll()` — the plug-in window's
100 ms GUI-thread timer, which mirrors every LMMS model into the plug-in's edit controller, exactly
as the effect host does (`Vst3EffectControls.cpp:58-97`).

**One honest gap in that link.** The host also puts model changes into
`ProcessData::inputParameterChanges` (pre-existing effect-host plumbing, unchanged), but the fixture
voice does not read parameter changes, so **audio-level parameter delivery is not witnessed by this
fixture** — only the model→controller mirror is. A real plug-in that reads
`inputParameterChanges` (all of them do) is not covered here. See §9.

---

## 7. The editor: design note, and why it is deferred

`IPlugView` is **not implemented**. This is the spec's one high-risk area (§0, §1.6) and it is
deliberately out of this slice: a 300–600 line foreign-window implementation with no in-tree
analogue is exactly the work that should be scoped rather than rushed onto a release.

**What a user gets today:** opening a VST3 instrument's window shows LMMS' own generated parameter
view — one LMMS knob per visible VST3 parameter, rendered with the plug-in's own value formatting
(`plugins/Vst3Instrument/Vst3InstrumentView.cpp`, mirroring
`plugins/Vst3Effect/Vst3EffectControlDialog.cpp:45-80`), and a line stating that the instrument's own
editor is not shown. A plug-in with no user interface at all — the in-tree fixture is one — is
handled: it gets the same grid, or a "no visible parameters" line, and nothing in the host asks for
a view the plug-in does not have. **No host code path calls `createView`/`IPlugView` during audio,
MIDI, load or save**, so a GUI-less instrument is fully usable.

**Proposed wording for `docs/KNOWN-LIMITATIONS.md`** (that file is owned by another lane; not
edited here):

> **VST3 instruments: the plug-in's own window does not open.** A VST3 instrument loads on a track,
> plays MIDI with sample-accurate timing, and its state saves and reloads with the project. What it
> does not give you is the instrument's own editor: `IPlugView` is not implemented, so opening the
> instrument window shows LMMS' generated grid of its parameters (the same idiom the VST3 effect
> host uses) instead of the plug-in's own GUI. Instruments that ship no user interface at all are
> unaffected by this.

**The design, when it is built** (the spec's §1.6 and Q9, unchanged by this work):

1. `IEditController::createView(kEditorView)` on the GUI thread, then
   `IPlugView::isPlatformTypeSupported("X11EmbedWindowID")` (Wayland is a second, later leg).
2. `IPlugView::attached(windowId, type)` with the Qt host widget's `winId()`; `setFrame()` with an
   `IPlugFrame` implementation that answers `resizeView()` by calling back `onSize()` and resizing the
   Qt widget — the plug-in→host→plug-in handshake documented at `iplugview.h:109-123`.
3. An `IRunLoop` on Qt's event loop (`registerEventHandler` + `registerTimer`); plug-ins that use
   timers or file descriptors need it, and the SDK's own sample is the only reference implementation
   (`samples/vst-hosting/editorhost/source/platform/linux/runloop.h:70`).
4. `detached()`/`removed()` on close, and DPI/content-scale handling.
5. The in-tree prior art for the *embed* half is VST2/Vestige
   (`ConfigManager::vstEmbedMethod()`, `src/gui/SubWindow.cpp:175-180`, the `qt5-x11embed`
   sub-module) — useful, but the `I…` plumbing and the run loop still have to be written.

---

## 8. Bugs this slice found in the tree (all fixed, all with a test)

1. **The MIDI ring's per-slot sequence numbers were not initialised to their index.** Every slot
   after the first reported "full" until the ring wrapped once, so exactly one event per queue
   lifetime got through. Found by the queue-flood test reporting 4096 drops instead of 3072.
2. **`lmms_plugin_main` was missing** from the new module. `PluginFactory` resolves that symbol by
   name (`src/core/PluginFactory.cpp:177`), so the plug-in was unloadable — found by the
   integration test skipping with "no lmms_plugin_main symbol".
3. **`cmake/modules/Vst3Sdk.cmake` was not idempotent**: two plug-in directories each
   `INCLUDE(Vst3Sdk)`, and the second `ADD_LIBRARY(lmms_vst3_sdk)` failed the configure outright.
   Guarded with `IF(NOT TARGET lmms_vst3_sdk)`.
4. `Clip`'s constructor already registers the clip with its track (`src/core/Clip.cpp:49-52`), so the
   double `addClip()` in the first draft made `Track::~Track` delete the same clip twice.
5. `MidiClip::addNote()` consults the piano-roll window; with quantization off it does not, which is
   how a headless test can add a note.
6. `Key::saveXML()` writes its attributes as `<attribute name=… value=…/>` child elements, not XML
   attributes (`src/core/Plugin.cpp:293-308`).
7. `InstrumentTrack::processOutEvent()` returns immediately when the track has no instrument
   (`src/tracks/InstrumentTrack.cpp:467-473`), which is why the test loads the plug-in through
   `InstrumentTrack::loadInstrument()` rather than constructing the class.

---

## 9. What is weak, and what a user still cannot do

Named plainly, not softened. In descending order of importance:

1. **No third-party VST3 instrument has been run.** The witness is the in-tree MIT fixture. Every
   claim above is about that subject; real-world instruments will exercise code paths it does not
   (multi-event-bus layouts, plug-ins that read `inputParameterChanges`, unusual bus arrangements).
   **This is the single biggest gap between this evidence and a release claim.**
2. **The plug-in's editor does not open** (§7), and — narrowly — the parameter view's *construction*
   was not verified in the harness: `InstrumentView::setModel` crashes inside Qt's
   `QWidget::setWindowIcon` on both the offscreen platform and under `Xvfb`, with the plug-in's logo
   pixmap failing to resolve in a test binary ("Error loading pixmap `vst3instrument/logo`"). That is
   a harness limitation and needs one live GUI session to settle; the *surface* the grid is built
   from (parameter count and names) **is** asserted.
3. **Audio-level parameter delivery is not witnessed** (§6): the host fills
   `ProcessData::inputParameterChanges`, but the fixture's voice ignores it, so nothing here proves a
   plug-in acts on a host parameter change. The model→controller mirror is proved.
4. **Multi-out, instrument latency in PDC, preset browsers, out-of-process hosting and the scan
   cache** are all out of scope by construction (the spec's own list).
5. **Pitch bend, channel pressure, program change and SysEx are not forwarded** to the instrument
   (note-on/off, key pressure and CC are).
6. **The class-identity defect (Q12) stands**: the saved key identifies the plug-in by class *name*,
   so an instrument that renames its class stops resolving in old projects.
7. **Gate 9 (`tests/fork-sources-gate.sh`) does not exist on this branch** — it exists only on lanes
   descended from `post-alpha/gate-debt`. No result is claimed for it. The gates that do exist are
   run in §10.

**What a user can do today:** put a VST3 instrument on an instrument track, play a MIDI clip into
it and hear audio with sample-accurate timing, automate and edit its parameters through the generated
grid, and save and reload the project with the instrument and its state intact.

**What a user cannot do:** open the instrument's own GUI; use an instrument with several audio outputs
or an instrument that needs a sidechain input; see instrument latency compensated on the track.

---

## 10. Build and test evidence (every exit code unpiped)

Checkouts: the VST3 SDK is cloned into this worktree's own build directory with the documented recipe
(`cmake/modules/Vst3Sdk.cmake`), tag `v3.8.1_build_84`, submodules `base`, `cmake`, `pluginterfaces`,
`public.sdk` at the pinned commits, licensed MIT (the module's licence gate passed).

**Baseline — CI flags, VST3 absent (`JOBS=4 bash tools/local-ci.sh --build-dir build --jobs 4`):**

```
configure EXIT=0   (log: build/configure.log)
build EXIT=0   (log: build/build.log)
ctest EXIT=0   (log: build/ctest.log)
ctest totals: 100% tests passed, 0 tests failed out of 25
local-ci: overall exit=0 (0 = every executed step passed)
  deviation: Qt5 development files not found: added -DWANT_QT6=ON (CI's runner installs qtbase5-dev; this box has Qt6 only)
```

**Feature configuration — the SAME build directory, reconfigured:**

```
cmake -S . -B build -DLMMS_VST3_SDK_PATH="$PWD/build/vst3sdk" -DWANT_VST3=ON \
      -DWANT_VST3_TEST_INSTRUMENT=ON -DUSE_WERROR=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DTARGET_UARCH=official -DUSE_COMPILE_CACHE=ON -DWANT_DEBUG_CPACK=ON -DWANT_QT6=ON
CONFIGURE_EXIT=0   -- Found VST3 SDK 3.8 (MIT) at …/build/vst3sdk
cmake --build build -j 4
BUILD_EXIT=0
cd build/tests && ctest
CTEST_EXIT=0
100% tests passed, 0 tests failed out of 28
```

**Which of those ran with the fixture option ON:** the second set only. 25 → 28 is exactly
`Vst3InstrumentFixtureProbe`, `Vst3InstrumentTest` and `Vst3InstrumentIntegrationTest`; the baseline
25 are unchanged and all pass. `-DUSE_WERROR=ON` is on in both, so every new file is `-Werror` clean.

**Render proof:** `RENDER_PROOF_EXIT=0` — the numbers are in §5.

**New test coverage:** `Vst3InstrumentTest` (host level, 11 assertions across 9 cases, compiles the
host sources itself) and `Vst3InstrumentIntegrationTest` (product level, 8 cases, loads the real
`.so` through the plug-in factory). Both are opt-in behind `WANT_VST3_TEST_INSTRUMENT`, like the
fixture they need.
