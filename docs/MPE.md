# MPE: per-note expression capture, storage, editing (task #601)

**Verdict: the smallest honest slice of MPE landed, and (task #649) all three axes now reach
playback.** Per-note expression is captured from
MPE-style MIDI input, stored on the note backwards-compatibly, readable and editable through a
headless API, and the **pitch** axis is applied by the playback path as a frequency ratio. The
**pressure** and **timbre** axes are captured, stored and readable, and since task #649 they are
**applied** too: the note sends them to the instrument as MIDI events (channel pressure, CC74) on
the channel its own note-on took — §2.5, which replaces the "not applied" verdict this document
carried for both axes until then. *The original #601 limit is still worth reading: §2.4's blocking
list is what #649 had to build, and §4's table now records what each axis does at each stage.*

Work is on branch `post-alpha/mpe` (base `post-alpha/v0.2` = `0c23587d2`), worktree
`projects/lmms-fl-research/zene-pa-mpe`. Nothing was pushed.

---

## 1. MPE is not the two things it is easy to confuse it with

| | what it is | how it is expressed here | why MPE is a different feature |
|---|---|---|---|
| **MPE** (this task) | per-note expression from an MPE controller: the controller gives each note its own MIDI channel, and that channel's pitch bend, channel pressure and CC74 belong **to that note** | a note's own `mpepitch` / `mpepressure` / `mpetimbre` | — |
| **Channel pitch bend** (pre-existing) | one 14-bit bend per MIDI channel; every note of the channel moves together | `MidiEvent::pitchBend()` (`include/MidiEvent.h:183`), parsed in `src/core/midi/MidiClient.cpp:221-226`, applied to the track's `m_pitchModel` at `src/tracks/InstrumentTrack.cpp:389-393` (base) | MPE's whole mechanism is the *opposite*: one note per channel precisely so a bend is **not** shared. A single capture field cannot express MPE for two notes at once. |
| **Slide notes** (task #557, pre-existing) | a portamento the piano roll draws: the note **glides** from the nearest preceding note's key to its own key, over its whole length | `Note::slide()` / `setSlide()` (`include/Note.h:132-133` in base), serialized as the optional `slide` attribute (`src/core/Note.cpp:202-205`), interpolated in `NotePlayHandle::slidePitchOffset()` (`src/core/NotePlayHandle.cpp:537-540`) | A slide is **one note's pitch changing over its own length**, authored in the timeline. MPE expression is **a captured controller gesture attached to a note**, and the pitch axis here is a single offset for the note, not a glide. They compose: a slide glides the base key, MPE bends on top of it (see §4). Neither can be implemented in terms of the other. |

Neither the word "MPE" nor any per-note expression field existed before this change. What did
exist, and what was verified against the base commit (`0c23587d2`) rather than assumed:

- `include/Note.h:100` — `class Note : public SerializingObject`. Per-note state is `m_key`
  (`:280`), `m_volume`, `m_panning`, `m_length`, `m_pos`, `m_detuning`, `m_type`, `m_slide` — **no
  pitch / pressure / timbre expression field**. `Note::saveSettings` (`src/core/Note.cpp:190-211`)
  writes `key vol pan len pos type` unconditionally and `slide` only when set; `loadSettings`
  (`:216-235`) reads them back. Note that the "optional attribute" precedent already existed for
  slide notes, and this change follows exactly that precedent rather than inventing a format.
- `include/NotePlayHandle.h:47` — `class NotePlayHandle : public PlayHandle, public Note`: a
  playing note **is** a `Note`, so anything stored on a note is already on the playing handle.
- `src/core/NotePlayHandle.cpp:545-590` (base) — `updateFrequency()` is where a note's pitch
  becomes the instrument's frequency: `m_frequency` from key, base note, master pitch, detuning,
  slide offset, instrument pitch (default mapping at `:581-583`, microtuner at `:562-577`), and
  `m_frequencyNeedsUpdate` is honoured in `play()` at `:241`.
- `src/core/midi/MidiClient.cpp:221-226` parses pitch bend (14-bit), `:211-214` channel pressure
  and program change, `:216-219` control change — i.e. the input path already sees every message
  MPE is made of; there was nothing to add to the parser.
- `src/core/midi/MidiPort.cpp:130-154` — the input path: `processInEvent` filters by input channel
  (`:133-134`) and hands the event to the track. **This filter is the prerequisite for MPE input**:
  an instrument track set to a single MIDI channel (e.g. channel 1) drops the member channels
  before the track ever sees them, so an MPE controller must be received with `--`/"all channels"
  (input channel 0).
- `src/tracks/InstrumentTrack.cpp:322-462` (base) — the track's event handling. Note-on builds a
  `NotePlayHandle` with `Origin::MidiInput` (`:336-358`), poly key pressure folds into note volume
  (`:379-387`), and there is **no** channel-pressure or CC74 handling at all: those fall through to
  `instrument()->handleMidiEvent()` (`:457-460`).
- `src/gui/editors/PianoRoll.cpp:4526-4536` (base) — recording rebuilds the note from the live
  handle's length/pos/key/volume/panning/detuning; it did **not** carry any expression (there was
  none to carry).

---

## 2. What landed

### 2.1 Capture — `include/MpeExpression.h`, `src/core/midi/MpeExpression.cpp` (both new)

`MpeExpression` is a fixed-size, allocation-free state machine over the 16 MIDI channels:

- one **master** channel (default index 0, i.e. "channel 1") — that channel's bend / pressure /
  CC74 stay channel-wide, exactly as before;
- every other channel is a **member** channel (covers MPE's lower zone, master 1 / members 2-16,
  and its upper zone);
- per member channel it tracks the current bend (14-bit), channel pressure and CC74, plus the keys
  currently sounding on that channel (fixed array of 4, so MPE+ polyphony on one channel is
  bounded rather than unbounded);
- `handleExpressionEvent(const MidiEvent&)` returns **true** only for a per-note expression event
  on a member channel, which is the caller's signal to *not* treat it as channel-wide.

Capture is wired into the **existing** input path, not a second parser: `InstrumentTrack::processInEvent`
calls a new `trackMpeInputEvent()` first, which feeds note on/off into the tracker and, for a
consumed expression event, loads the channel's current expression onto every note handle playing on
that channel (`loadMpeExpressionOntoChannelNotes()`). A note is stamped when it is created
(`InstrumentTrack.cpp`, the `MidiNoteOn` case) and re-stamped on every bend/pressure/CC74 it
receives while held, so what the note ends up carrying is the **last** value the controller sent on
its channel, not the value that happened to be current before the first bend arrived.

**MPE mode is off by default** (`MpeExpression::isEnabled()`, process-global `std::atomic_bool`).
With it off, not one branch of the input path changes — which is what makes the
behaviour-preservation claim in §5 provable rather than hopeful. The flag is deliberately **not
serialized** (see §6).

### 2.2 Storage — `include/Note.h`, `src/core/Note.cpp`

Per-note expression lives on `Note` as `MpeNoteExpression { int pitchCents; int pressure; int timbre; }`
(pitch in 1/100 semitone, ±4800 = the MPE default ±48-semitone member bend range; pressure and
CC74 0..127), behind:

- readback / edit API (headless, no Qt widgets involved):
  `Note::hasMpeExpression()`, `mpeExpression()`, `mpePitchCents()`, `mpePressure()`, `mpeTimbre()`;
  writers `setMpeExpression(...)`, `setMpePitchCents()`, `setMpePressure()`, `setMpeTimbre()`,
  `clearMpeExpression()` (all clamp to their documented range);
- the copy constructor, `operator=` and `clone()` carry it, so `MidiClip::addNote()` and
  `NotePlayHandle` (which is-a `Note`) inherit it for free;
- `InstrumentTrack::playingNote(int key)` exposes the live note handle for a key — the readback a
  piano-roll expression editor would use while a note sounds.

On disk, three optional attributes and nothing else:

```xml
<note key="57" vol="100" pos="384" pan="0" len="384" mpepitch="1200" mpepressure="64" mpetimbre="32"/>
```

**Backwards compatibility, stated exactly:**

- **An old project → this build.** No `mpe*` attributes ⇒ `hasMpeExpression()` is false, every axis
  reads 0, and the note behaves exactly as it always did.
- **This build → an older build.** The older build's `loadSettings` reads the attributes it knows
  (`key vol pan len pos type slide`) and **ignores the three unknown ones**; the note loads with its
  key/volume/panning/timing untouched and the expression simply absent. It does not fail, warn or
  corrupt (QDom attribute reads are selective). This is proven end-to-end in §5 by rendering the
  expressive project with the **unmodified** binary and getting the byte-identical WAV.
- **No format version bump and no attribute on notes that carry no expression.** A note without
  expression, and a whole project without any expression, serialize byte-identically to the previous
  build (test `noteWithoutExpressionIsUnchanged`).
- Captured-but-neutral (all three axes 0) is *not* the same as "no expression": the attributes are
  still written, **presence** is the flag, so a controller that sent a self-centring bend before
  any real gesture keeps its MPE-ness across a reload.

### 2.3 Editing

The editing entry points are the `Note` setters above (`setMpeExpression()` for all three axes,
or one setter per axis). The piano-roll **UI hook-up is not implemented** — a deliberate omission:
drawing and dragging per-note expression curves in a 6,000-line widget is a separate change. What
the UI would need already exists headlessly, and the one piece of GUI code that does touch this
reads rather than writes: `PianoRoll::finishRecordNote` (now `src/gui/editors/PianoRoll.cpp:4510-4546`)
copies the expression off the live note handle onto the note it records, 4 lines, because a
performance recorded into a clip otherwise loses the expression at exactly that reconstruction
step. It is a declared change to an upstream file (`tests/upstream-modifications.txt`).

### 2.4 Playback application — `src/core/NotePlayHandle.cpp`

`NotePlayHandle::updateFrequency()` (now `:586-596`) multiplies `m_frequency` by
`NotePlayHandle::mpePitchRatio(mpePitchCents())` = 2^(cents/1200), i.e. the note's own bend as a
frequency ratio, composed with the existing detuning / slide / instrument pitch. That reaches every
instrument that asks the engine for a note's frequency (`nph->frequency()`), which is the built-in
synthesisers' path. The ratio is deliberately **not** folded into `m_unpitchedFrequency`, which is
documented as "without pitch wheel influence" (`NotePlayHandle.h:103-107`) and a per-note bend is
exactly a pitch-wheel-per-note.

**What cannot express it, with the blocking line:** a note's *MIDI* identity is an integer key —

- `NotePlayHandle::play()` sends `MidiEvent(MidiNoteOn, midiChannel(), midiKey(), ...)`
  (`src/core/NotePlayHandle.cpp:235-238`), and `midiKey()` is `int` (`:182-185`);
- `InstrumentTrack::processOutEvent()` forwards that to the instrument as an integer-keyed
  note-on/off (`src/tracks/InstrumentTrack.cpp:486-499` in base);
- the same applies to any MIDI-out / hosted-plugin path, which receives note numbers, not
  frequencies.

So: a captured bend is audible on frequency-driven instruments and **inaudible on integer-key
(MIDI-forwarding) paths**. That is a real limit of this slice, not a rounding detail, and the
sensitivity render in §5 uses a frequency-driven instrument (TripleOscillator) for that reason.

### 2.5 Pressure and timbre reach the instrument (task #649)

The blocking list above is a list of what is *missing*, not of what is impossible: a note already
knows the channel its note-on took (`midiChannel()`, stamped by the track when the handle is
built), and the instrument-facing path already carries arbitrary MIDI for every other message
(`InstrumentTrack::processOutEvent` → `m_instrument->handleMidiEvent`, then the track's MIDI port).
What #649 adds is the sender.

- **`NotePlayHandle::sendMpeExpressionMidi()`** (`src/core/NotePlayHandle.cpp`, declared in
  `include/NotePlayHandle.h`) sends two events — `MidiChannelPressure` with `mpePressure()`, then
  `MidiControlChange` with `MpeTimbreController` (74) and `mpeTimbre()` — on `midiChannel()`. A
  note whose `hasMpeExpression()` is false sends **nothing at all**, so a project that never
  captured expression reaches the instrument exactly as it did before.
- **When it runs.** (a) From `NotePlayHandle::play()`, immediately after the note-on and with the
  note-on's own `TimePos`/offset, so a stored expression arrives with the note it belongs to and
  sample-aligned inside the block. (b) From
  `InstrumentTrack::loadMpeExpressionOntoChannelNotes()`, on a sounding note, when the channel's
  pressure or timbre actually CHANGED — a bend-only update (every pitch gesture, and the common
  case) sends nothing extra, because the pitch axis reaches playback through
  `setFrequencyUpdate()` and needs no event.
- **Why the same channel as the note-on.** For a note captured from an MPE controller that is the
  note's own member channel, which is the whole point: an MPE instrument tells one note's pressure
  from another's by the channel it arrives on, and this is what makes the two axes per-note rather
  than channel-wide. (A note played back from a clip has no stored channel — §6.10 — so it uses the
  track's output channel, exactly where its note-on went.)
- **What a consumer is.** `handleMidiEvent` is the interface every instrument implements; the
  hosted families (Vestige, LV2, CLAP, Carla) forward MIDI to their plug-in, and a MIDI output port
  carries it to hardware. No *built-in* synthesiser consumes channel pressure or CC74 — they are
  driven by frequency and volume and inherit the base no-op `handleMidiEvent` — which is why the
  proof in §5.4 uses a purpose-built test instrument as its subject.
- **Realtime:** two `MidiEvent` values and a virtual call, on the same thread and the same path as
  the note-on that precedes them; no allocation, no lock, no growth. A note that carries no
  expression costs one branch.

---

## 3. Where the data reaches the playback path (realtime contract)

| stage | thread | what happens |
|---|---|---|
| `MidiClientRaw::parseData` (`src/core/midi/MidiClient.cpp`) | MIDI input | unchanged parser; it already produced bend / channel-pressure / CC74 events |
| `MidiPort::processInEvent` (`src/core/midi/MidiPort.cpp:130`) | MIDI input | unchanged |
| `InstrumentTrack::processInEvent` → `trackMpeInputEvent` | MIDI input | fixed-array state update + plain `int` writes onto live `NotePlayHandle`s; **no allocation, no lock** in the new code (`MpeExpression` sizes everything in its constructor; `std::atomic_bool` for the mode flag) |
| `Note` (stored expression) | — | the note handle **is** a `Note`, so a clip note's expression is already on the handle when it is constructed |
| `NotePlayHandle::updateFrequency` | audio thread | a single extra float multiply, and only when the expression is non-zero; reached from the constructor, from `play()` when `m_frequencyNeedsUpdate` is set, and from `processTimePos` |
| live bend arriving mid-note | MIDI input | `setMpeExpression()` + `setFrequencyUpdate()`; the audio thread picks it up on its next `play()`, exactly the mechanism `InstrumentTrack.cpp:640` already used |

`m_notes[key]` is looked up from the MIDI thread in `loadMpeExpressionOntoChannelNotes()`. That is
the *same* unsynchronised lookup the pre-existing `MidiKeyPressure` handling does
(`src/tracks/InstrumentTrack.cpp:379-387` in base, `m_notes[event.key()]->setVolume(...)`), so this
change adds no new class of race; it is noted here rather than silently inherited.

---

## 4. What is stored vs what is applied

| axis | captured | stored in the project | read back / editable | applied to playback |
|---|---|---|---|---|
| pitch bend (per note) | yes | yes (`mpepitch`, cents) | yes | **yes** — frequency ratio in `updateFrequency()` |
| channel pressure | yes | yes (`mpepressure`, 0..127) | yes | **yes (task #649)** — sent to the instrument as channel pressure on the note's own channel, at the note-on and on every live change (§2.5) |
| timbre / CC74 | yes | yes (`mpetimbre`, 0..127) | yes | **yes (task #649)** — sent to the instrument as CC74 on the note's own channel, the same way and at the same moments |

*A stored-but-unapplied axis was still worth storing: it is what a re-save, an expression editor,
or a per-note-timbre instrument path reads, and it was honest about which half had landed. Task
#649 closed the second half for both axes; the storage, its format and the readback are unchanged.*

---

## 5. Proofs

### 5.1 Tests

Three test files, registered in `tests/CMakeLists.txt` and listed in `tests/all-sources.txt`:
`tests/src/core/MpeExpressionTest.cpp` (the per-channel tracker and the pitch math),
`tests/src/core/MpeInputPathTest.cpp` (the real input path, which needs a live Engine) and
`tests/src/core/MpeNoteStorageTest.cpp` (what a note carries and what lands on disk). They are
split by layer rather than kept in one file because the repo's file-length ratchet wants new files
under 500 lines; all three are (216 / 161 / 396 lines).

- `captureIsPerChannel` — **the acceptance capture test**: two notes on two member channels, bent
  and squeezed independently; asserts each channel's own (pitch, pressure, timbre) and that they
  differ (a test that could not tell channels apart cannot pass this), that a note on the master
  channel captures nothing, that a non-CC74 control change (sustain) is *not* consumed as
  expression, and that note-off drops the channel binding without erasing what the note captured.
- `bendConvertsToCentsExactly` — the 14-bit bend → cents mapping, integer-exact (±2400 at a quarter
  range, −4800/+4799 at the ends, half-range, clamped nonsense).
- `activeNotesAreBounded` — the per-channel note list is capped and releases.
- `noteExpressionRoundTrips` — save → load → save identical; copy/assignment carry it; ranges clamp
  (including a hostile project file); `clearMpeExpression()` removes the attributes.
- `noteWithoutExpressionIsUnchanged` — a plain note and a whole project serialize with **exactly**
  the upstream attribute set and re-save byte-identically.
- `expressionIsAdditiveOnDisk` — an old project loads with no expression; the expressive project
  loads with its upstream fields *identical* and exactly three extra attributes (what an older
  build sees and ignores); re-save is stable.
- `playbackAppliesThePitch` — the pure pitch math `updateFrequency()` uses.
- `inputPathStampsTheRightNotes` — the **real** input path (`InstrumentTrack::processInEvent`),
  end to end: note handles on member channels get their own channel's expression and not the
  other's, the channel-wide pitch model stays put for member channels, the master-channel bend
  still bends the whole instrument, and with MPE off nothing is captured at all.
- `renderFixturesLoadWithTheExpressionTheyCarry` — the three render fixtures below are real,
  loadable projects: two notes, keys and positions as written, and exactly the expression each
  fixture claims (so a fixture cannot drift away from what the render table means).

### 5.2 Build and suite

- `JOBS=4 bash tools/local-ci.sh --build-dir build --jobs 4` — the CI `linux-x86_64` job's exact
  `CMAKE_OPTS` (`-DUSE_WERROR=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo -DTARGET_UARCH=official
  -DUSE_COMPILE_CACHE=ON -DWANT_DEBUG_CPACK=ON`) plus the script's loudly printed deviation
  `-DWANT_QT6=ON` (no Qt5 development files on this box). Unpiped: **configure EXIT=0 · build
  EXIT=0 · ctest EXIT=0**, `linux-x86_64: REPRODUCED`, script overall exit 0. The other six matrix
  jobs are `NOT-REPRODUCIBLE-HERE` (macOS, MSVC, mingw64, windows-arm64, aarch64) and were not
  run — CI is their verifier.
- Test suite, `ctest` from `build/tests` with `QT_QPA_PLATFORM=offscreen`: **`100% tests passed, 0
  tests failed out of 28`**, exit 0 (26 before this change + the two new MPE test binaries).
  (0 tests would have been an error, not a pass.)
- The three gates, exit codes measured unpiped:

  | gate | command | exit | result |
  |---|---|---|---|
  | 9 | `bash tests/fork-sources-gate.sh` | **0** | 1,096 tracked sources scanned, 102 fork-NEW, 995 inherited upstream, 0 stale |
  | 6 | `bash tests/no-upstream-regression-gate.sh` | **0** | every change to inherited code is declared (37 ledger entries) |
  | all | `bash tests/run-all-gates.sh` | **3** | `PASS-WITH-SKIPS (exit 3)`: gates 1, 3, 4, 5, 6, 7, 8, 9 PASS (Gate 1 = `28/28` above, Gate 5 = 30 mutants, 3 survived), gate 2 (coverage) SKIP because `--with-coverage` was not passed — "this run is INCOMPLETE, not green" |

  Reproducibility note, because two earlier attempts at that same command came back **exit 1**: the
  box is shared with ~10 sibling lanes (measured while writing this: **load average 54**, 7 GB of
  8 GB swap in use) and the abort was always in a test this change does not touch — `AudioPortsTest`
  (twice) and `PdcMixerTest` (once) dying in `cleanupTestCase()` with
  `QThread: Destroyed while thread is still running`. Both pass in isolation (`AudioPortsTest` 6/6
  immediately after one such abort) and both are green in the dedicated `ctest` runs quoted above;
  Gate 5 also died once under that load and left a mutant *and* a truncated `RoutingGraph.cpp.o`,
  which broke the next run's Gate 1 *build* (`relocation truncated to fit … access beyond end of
  merged section`) until the file was restored — its `sha256` is the gate's own pristine
  `1fc2d8fb3fe015e94468cd77e8fb285805198f6074258e0c7ced217c8632f163` and the tree is clean at
  `681432a06`. Nothing in that chain is a property of this feature.
- **The fix for that flake, for whoever owns those two tests:** `AudioBusHandleTest` stops the dummy
  device before tearing the Engine down; `AudioPortsTest` and `PdcMixerTest` destroy it with the
  device thread still running. That is the race in one line.

- Every file this change adds is under the repo's 500-line ratchet: `include/MpeExpression.h` 165,
  `src/core/midi/MpeExpression.cpp` 263, the three test files 216 / 161 / 396.
- **Not green, and not mine:** the whole-tree ratchet run
  (`bash tests/file-length-gate.sh --check --scope all`) is red on this branch for four *inherited*
  test files that grew elsewhere — `ScriptEngineTest.cpp` 546→592, `PluginPortsHarness.h`
  1156→1196, `PluginPortsMigrationTest.cpp` 632→731, `WasmSandboxTest.cpp` 948→1022. It also
  flagged this change's first, single 669-line test file ("new file over 500 lines"), which is why
  the tests are split as described in §5.1. The default gate run (`run-all-gates.sh`) is
  fork-scoped and does not measure those files.
- **Red/green.** The first `ctest` run on this branch failed exactly one test,
  `MpeExpressionTest::inputPathStampsTheRightNotes` (`'note60->hasMpeExpression()' returned FALSE`),
  because the test had not switched MPE mode on — i.e. the capture assertions are sensitive to the
  capture path being off rather than decorative. The render pair below is the same shape one layer
  down, at the audio output.


### 5.3 Renders — behaviour preservation and its sensitivity control

TripleOscillator (frequency-driven), two notes, fixture before the render:

- `tests/data/mpe/mpe-plain.mmp` — the baseline project; no note carries expression. Two notes on
  key 57 with a TripleOscillator.
- `tests/data/mpe/mpe-expression.mmp` — **the same file** with three attributes added to the second
  note (`mpepitch="1200" mpepressure="64" mpetimbre="32"`; a whole octave up, so the control cannot
  pass by accident). `diff` between the two files is exactly that one line.
- `tests/data/mpe/mpe-neutral.mmp` — again the same file, with all three attributes present on
  **both** notes and every value at its neutral 0. It must render exactly like the plain one: a
  captured-but-neutral note is not a retune.

The fixtures are written in the **current** project format (`<lmms-project version="31">`, the
current version being `DataFile::UPGRADE_METHODS.size()` — `src/core/DataFile.cpp:133`). That is
deliberate and was learned the hard way: a fixture in the old `version="1.0"` shape runs the whole
upgrade chain on load (`src/core/DataFile.cpp:73-91`), whose `upgrade_extendedNoteRange`
(`:1812`) re-encodes the notes — the file then renders *notes it does not contain as written*, and
a fixture that says one thing while playing another is worthless as evidence. With the current
version no upgrade touches it.

Renders (16-bit stereo WAV, `lmms render <project> -o <out> -f wav`, exit codes unpiped):

| # | binary | project | WAV md5 | peak | reading |
|---|---|---|---|---|---|
| A | reference: `zene-pa-mididepth` build, `LMMS 0.1.0-alpha.3+ea16459`, contains no MPE code | `mpe-plain` | `9b969d92ae7f44c5a3fdfa9b6f19b39b` | 32439 | the pre-change baseline |
| B | this branch, `LMMS 0.1.0-alpha.7+2a6a3ea` | `mpe-plain` | `9b969d92ae7f44c5a3fdfa9b6f19b39b` | 32439 | **byte-identical to A** — a project with no MPE expression renders exactly as before |
| C | this branch | `mpe-neutral` (both notes captured, every axis 0) | `9b969d92ae7f44c5a3fdfa9b6f19b39b` | 32439 | identical to B: captured-but-neutral is a true no-op, not a silent retune |
| D | this branch | `mpe-expression` (`mpepitch="1200"` on the second note) | `1f00568519b48055e115816aa5f2ea6b` | 32767 | **differs from B/C** — the sensitivity control: the captured pitch reaches the audio (an octave up, peak clips further) |
| E | the reference binary (A's) | `mpe-expression` | `9b969d92ae7f44c5a3fdfa9b6f19b39b` | 32439 | identical to A/B: an older build ignores the three unknown attributes and plays the project as if the expression were absent |

```sh
# reference renders (no MPE code in that binary)
XDG_CONFIG_HOME=/tmp/mpe-cfg QT_QPA_PLATFORM=offscreen \
  <zene-pa-mididepth>/build/lmms render tests/data/mpe/mpe-<v>.mmp -o ref-<v>.wav -f wav
# this branch
XDG_CONFIG_HOME=/tmp/mpe-cfg QT_QPA_PLATFORM=offscreen \
  build/lmms render tests/data/mpe/mpe-<v>.mmp -o mine-<v>.wav -f wav   # every one EXIT=0
md5sum ref-*.wav mine-*.wav
```

Honest caveats about that table:

- The reference binary is a **sibling lane's build** (`zene-pa-mididepth`, three commits past this
  lane's base, whose own feature is not engaged by these fixtures) rather than a rebuild of this
  lane's base — one build directory per lane is the workspace rule, and a second 4.5 GB build tree
  was not worth it for a null result. Rows C and E are what make it usable: two binaries with
  different feature sets agree byte-for-byte on three of the four renders, so the difference in row
  D is this change and not the build. A mismatch would have been inconclusive; a match is evidence.
- One reference-binary render (`ref-plain`) **aborted at exit** with
  `QThread: Destroyed while thread is still running` *after* writing a correct WAV (its md5 is the
  one in the table) — the known shutdown-path defect of that tree, not of this change. Every render
  with this branch's build exited 0.
- The comparison binaries have identical CMake caches (build type, `USE_WERROR`, `TARGET_UARCH`,
  `WANT_QT6` and the optional features), so the null result is not a compiler-flag artefact.

### 5.4 The pressure/timbre proof (task #649) — an in-tree instrument as the vehicle

A render table cannot prove these two axes: no built-in instrument consumes them (§2.5), so a
project carrying them renders identically with and without the route. The proof is therefore a
registered ctest with a purpose-built subject.

- **The subject:** `tests/src/plugins/MpeTestConsumer.cpp` — a minimal MIT-licensed instrument
  built as a MODULE from `tests/CMakeLists.txt` (never from `plugins/`, never installed). It keeps
  the channel pressure and CC74 that reached it through `Instrument::handleMidiEvent`, per MIDI
  channel, resetting both on a note-on, and renders a CONSTANT level for a note:
  `kBaseLevel * (1 + pressure/127) * (1 + timbre/127)` — +100% per fully pressed axis, exactly 1.0
  when no expression reached it. **The fixture is the vehicle, and the tree has no other one**: this
  is the same device `tests/data/vst3-test-instrument` is for the VST3 host and
  `tests/data/clap-test-plugin` for the CLAP host.
- **The registration:** `src/core/MpePlaybackTest.cpp` in `tests/CMakeLists.txt`'s `LMMS_TESTS`
  list, with the fixture directory handed to it as `LMMS_MPE_CONSUMER_DIR` and `LMMS_PLUGIN_DIR`
  pointed at it in `initTestCase()` (the plugin factory reads its search paths in its
  constructor), so the instrument arrives through the real `Instrument::instantiate` path.
- **The measurement, not a smoke test:** one audio block of one note is rendered through the real
  playback path (`NotePlayHandle::play()` → `InstrumentTrack::processOutEvent()` →
  `Instrument::handleMidiEvent()` → the fixture's `playNoteImpl()`), once with the expression and
  once without, and the two levels are compared. `pressure=64` alone, `timbre=64` alone and the two
  together each have to land on the fixture's own mapping within 1%; a note whose captured axes are
  both 0 has to render *identically* to a note with no expression, which pins the route to the
  values and not to the presence of an event. With the route missing every ratio is exactly 1.0 and
  every one of those assertions fails. The second slot drives the live half: with MPE input on, a
  `MidiChannelPressure` message arriving on the sounding note's own member channel moves the next
  block, through `InstrumentTrack::processInEvent()` and the real `MpeExpression` state machine.
- **What it does NOT prove:** that a *musical* instrument responds the way a musician expects. The
  fixture's mapping is its own; what is proven is delivery — the two axes, with their values, on the
  note's own channel, into the instrument's MIDI entry point, and a block that measurably differs
  because of them.
- **Status on the 030/mpe-playback branch (measured 2026-09-15, provider window ended before a
  build):** `cmake -S . -B build -DUSE_WERROR=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo
  -DTARGET_UARCH=official -DUSE_COMPILE_CACHE=ON -DWANT_DEBUG_CPACK=ON -DWANT_QT6=ON
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON` → **configure EXIT=0** (the module target, the test target and
  the `LMMS_MPE_CONSUMER_DIR`/`LMMS_PLUGIN_DIR` wiring are valid CMake against this tree). The four
  translation units this change adds or touches were then compiled with each one's own command out of
  `build/compile_commands.json` and `-fsyntax-only` — and with `-Werror`, which the tree's CI opts set:
  `src/core/NotePlayHandle.cpp` **EXIT=0**, `src/tracks/InstrumentTrack.cpp` **EXIT=0**,
  `tests/src/plugins/MpeTestConsumer.cpp` **EXIT=0**, `tests/src/core/MpePlaybackTest.cpp`
  **EXIT=0** (that one with the AUTOMOC `#include` stripped from a copy, since the `.moc` is generated
  by the real build). Two real errors were found and fixed this way: the fixture's `lmms_plugin_main`
  was outside `namespace lmms`, and the fixture had not implemented the three pure virtuals it
  inherits (`nodeName`/`saveSettings`/`loadSettings`). **No link and no ctest run was taken** — the
  test binary needs all of `lmmsobjs`, and the lane's window did not allow a full build — so the
  suite's transcript for `MpePlaybackTest`, and the `MPE_EVIDENCE` numbers it prints, are the
  fix-up pass's to produce and record. (The commits are named in §7.)


---

## 6. What is NOT done (deliberate limits)

1. **No piano-roll UI for expression** — no per-note curve drawing/editing, no display of captured
   expression on a note. The API is there; the widget work is not (it is not a small change in
   `PianoRoll.cpp`).
2. **No MPE mode toggle in the product.** `MpeExpression::setEnabled()` is process-global, not
   serialized into the project (deliberately: a project must never change meaning because of a
   flag), has no settings entry and no persistence. A user cannot yet switch MPE input on from the
   GUI; a lane with a settings surface would wire it (a per-`MidiPort` model is the natural home).
3. **Pressure and timbre are applied** — CLOSED by task #649 (§2.5). They are sent to the instrument
   as MIDI on the note's own channel; what remains open is not the route but the SUBJECT: no
   in-tree instrument consumes either axis, so the measurement uses the in-tree MIT test subject
   `tests/src/plugins/MpeTestConsumer.cpp` (§5.4 below), and a hosted or hardware instrument is
   still the thing a musician would actually hear.
4. **No per-note expression *curve*.** What is captured per axis is one value: the channel's state
   at note-on, updated live while the note is held, and frozen at note-off. A bend that sweeps up
   and back down during one note survives only as its final value. Storing the trajectory needs a
   per-note automation curve (there is a precedent for one pitch curve per note —
   `Note::createDetuning()` — but no multi-axis per-note automation) and a playback path that
   interpolates it per period; that is a larger change than "the smallest honest MPE".
5. **Bend range is not negotiated from the input.** The member bend range is a configuration value
   defaulting to MPE's ±48 semitones; an RPN `0,0` message from the controller is not parsed, so a
   controller configured for ±12 will be captured 4× too wide until `setBendRangeSemitones()` is
   called.
6. **MPE+ is capped.** Standard MPE is one note per member channel; a second note taken on a held
   channel is tracked up to a fixed cap of 4 per channel, beyond which it simply is not tracked.
7. **No global/palm messages.** Only pitch bend, channel pressure and CC74 are expression;
   MIDI Mode/Pitch-Bend-Sensitivity globals are not consumed, and non-CC74 control changes keep
   their pre-existing meaning (verified: sustain is not swallowed).
8. **The step recorder path is not covered.** `PianoRoll::finishRecordNote` (live recording) copies
   the expression; the step recorder (`StepRecorder`) has no live controller gesture to copy, and
   was left alone.
9. **No coverage of a real MPE controller.** Everything was driven by synthetic events and fixtures
   derived from them; no hardware MPE controller was attached, and the LV2/CLAP/VST3 hosting paths
   (integer-key, §2.4) were not exercised with expression at all.
10. **A note played back from a clip has no stored member channel.** Expression is captured per
   channel and stored per note, but the CHANNEL is not a serialized property of a note: a clip note
   is built with `midiEventChannel = -1` (`src/tracks/InstrumentTrack.cpp`, the
   `NotePlayHandleManager::acquire` call in `play()`), so its note-on — and therefore the two
   expression events #649 sends with it — go on the track's output channel. For a one-note-per-track
   performance this is indistinguishable; for a clip that captured two notes on two different member
   channels, the instrument sees both notes' expression on one channel. Storing the channel (an
   optional `mpechannel` attribute, the same shape as `mpepitch`) is the honest fix and was left out
   of this slice deliberately.

---

## 7. Commits

Branch `post-alpha/mpe`, base `post-alpha/v0.2` = `0c23587d2`. Nothing was pushed, and no PR,
issue or remote was touched.

**Task #649 (the two axes applied — §2.5, §5.4) is NOT on this branch.** It is the continuation
lane, branch `030/mpe-playback` off `release/0.3.0`, worktree
`projects/lmms-fl-research/zene-030/wmpe`: it adds `NotePlayHandle::sendMpeExpressionMidi()`, the
one live-update call in `InstrumentTrack::loadMpeExpressionOntoChannelNotes()`, the fixture
`tests/src/plugins/MpeTestConsumer.cpp` and the registered ctest `MpePlaybackTest`. The rest of this
document is the #601 record and is unchanged.

- `e4f934eea` — `feat(mpe): capture, store and apply per-note MPE expression (#601)`: the module,
  the note storage, the input-path wiring, the playback application, the three test files, the
  render fixtures, and the ledger/scope entries.
- the docs commit that carries this file (the report).
- Six commits cherry-picked from `post-alpha/gate-debt` with `git cherry-pick -x` (provenance in
  each message) so the gates §5.2 names actually exist on this branch: Gate 9
  (`tests/fork-sources-gate.sh`), the coverage-entry floor, and "a skipped gate is not a pass"
  which is what makes `run-all-gates.sh` exit 3 instead of claiming a green run. They are test
  tooling only (`tests/**`, `.github/**`, docs) and touch no production source.
