# LANE STATE — `030/freeze-journal-fix` (the 0.3.0-alpha release gate)

**This file replaces the base's `LANE-STATE.md`.** Every lane writes its own at its worktree
root (that is what the sibling lanes' files are), so this one is this lane's. Nothing is lost:
the version at this branch's base `598d4f5c1` is the `030/folder-tracks` lane's own handoff and
is recoverable verbatim with `git show 598d4f5c1:LANE-STATE.md` (711 lines).

**Worktree** `/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/zene-030/wjrnl`
**Branch** `030/freeze-journal-fix`, created at `598d4f5c1` (`release/0.3.0`; base tip unchanged,
nothing rebased, nothing merged, nothing pushed)
**Logs / raw evidence** `/home/kruzzzzy/zene-030-wjrnl-598d4f5c1/`
(`depth_probe.py`, `BEFORE-*.log`, `AFTER-*.log`, `depth-*.log`, `local-ci-baseline.log`,
`gate-*.log`)

## The defect (chain read here, not taken on trust)

`MidiPort::m_readableModel` / `m_writableModel` are journalled `BoolModel`s
(`include/MidiPort.h:170-171`, built at `src/core/midi/MidiPort.cpp:65-66`).
`InstrumentTrack::autoAssignMidiDevice()` writes them on the RAW-client branch
(`src/tracks/InstrumentTrack.cpp:1234` `if (midiClient()->isRaw() && device != "none")` →
`:1236 m_midiPort.setReadable(assign)`), and `saveTrackSpecificSettings()` deliberately clears
and restores the auto-assigned device around every save (`:1017`, `:1025`). Each write reaches
`AutomatableModel::setValue` → `addJournalCheckPoint()` (`src/core/AutomatableModel.cpp:317`)
= ONE undo step. On the two Linux CI runners there is no ALSA sequencer, so the MIDI chain
falls through to `MidiDummy` (`src/core/AudioEngine.cpp:1036` `return new MidiDummy;`), which IS
a `MidiClientRaw` (`include/MidiClient.h:145-148`, `isRaw() == true`) and the branch fires.
macOS (`include/MidiApple.h:113`) and Windows (`include/MidiWinMM.h:113`) clients are not raw —
which is why exactly the two Linux jobs are red.

## Reproduction: exact command, both exit codes, before and after

```
cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/zene-030/wjrnl
MIDIDEV=/dev/null QT_QPA_PLATFORM=offscreen python3 tests/control-freeze-commands-transcript.py "$PWD/build/zene"
```

| state | binary | `MIDIDEV=/dev/null` | no knob |
|---|---|---|---|
| BEFORE the fix | `zene-030/build/zene` @ `598d4f5c1` (the integration build at this branch's base) | **EXIT=1**, the committed assertion verbatim: `AssertionError: the fixture's clip clip-0 carries 0 notes, not the one it was built with: clip-0=[]@0/trk-5 clip-1=['note-0']@192/trk-5 \| song_clips=['clip-0','clip-1'] depth=14` | EXIT=0 (PASS) |
| AFTER the fix | this worktree's `build/zene` | **EXIT=0** (PASS) | EXIT=0 (PASS) |

`MIDIDEV` is read by `MidiAlsaSeq::probeDevice()` (`src/core/midi/MidiAlsaSeq.cpp:199-211`), so
`/dev/null` makes the local instance take CI's MIDI path with **zero code change**. The 39-line
note trace of the AFTER `MIDIDEV=/dev/null` run is **byte-identical** (`diff` clean) to the
BEFORE no-knob trace — i.e. the CI condition now behaves like the healthy local one — while the
BEFORE `MIDIDEV=/dev/null` trace had 41 lines (the two extra being `render.render` and
`bounce.in_place`, whose depths moved).

## Bar 2 — every `not_mutating` command leaves the undo depth alone, in BOTH configurations

Measured with this lane's own probe (`/home/kruzzzzy/zene-030-wjrnl-598d4f5c1/depth_probe.py`,
which only READS `control.undo_depth` around each command inside a real instance):

| config | command | BEFORE | AFTER |
|---|---|---|---|
| `MIDIDEV=/dev/null` | `render.render` | +1 | **+0** |
| `MIDIDEV=/dev/null` | `bounce.in_place` | +1 | **+0** |
| `MIDIDEV=/dev/null` | `project.save` | +1 | **+0** |
| no knob | `render.render` | +0 | +0 |
| no knob | `bounce.in_place` | +0 | +0 |
| no knob | `project.save` | +0 | +0 |

Startup depth with `MIDIDEV=/dev/null`: 4 before, **1 after** — the investigation's predicted
`+3` startup offset is gone too, because the same two models were also written by
`autoAssignMidiDevice` on every track construction (`InstrumentTrack.cpp:114`) and every
destruction (`:213`).

## The fix, at file:line

`src/core/midi/MidiPort.cpp:70-88` (constructor): `m_readableModel.setJournalling(false)` and
`m_writableModel.setJournalling(false)`, with the banner comment that states why. **Why this and
not the narrow one:** the flags are DEVICE-ASSIGNMENT state, not user work — they are flipped by
track construction/destruction (`InstrumentTrack.cpp:114`, `:213`), by every save (`:1017`,
`:1025`), by a piano-roll or piano-view click (`src/gui/editors/PianoRoll.cpp:4299`,
`src/gui/instrument/PianoView.cpp:686`) and by `MidiPort::subscribeReadablePort()`
(`MidiPort.cpp:302`) whenever it forces input on, so journalling them makes a device assignment
an undo step. Precedents the tree already ships for a model that is deliberately not journalled:
`src/gui/editors/SongEditor.cpp:250`, `src/gui/editors/AutomationEditor.cpp:115`,
`src/gui/widgets/AutomatableButton.cpp:220`. Nothing is dropped to get the zero:
`MidiPort::saveSettings` (`:197-198`) still writes both flags into the port element,
`loadSettings` (`:251-252`) still restores them, and a checkpoint taken on the port itself still
carries them as attributes. The narrower alternative (suspend journalling around
`InstrumentTrack.cpp:1015-1025`, the `src/core/ImportFilter.cpp:66-80` pattern) was **rejected**:
it removes the save-time pushes but leaves the ctor/dtor ones, i.e. the `+3`/`+3` offsets
measured above — it treats the symptom.

## The regression test in this change

`tests/src/core/UndoBoundsTest.cpp` gains `aDeviceAssignmentIsNotAnUndoStep`:
a `MidiPort` is built over the live client, the flags are flipped in both directions and read
back (so the guarded "value unchanged" early return cannot be what makes it pass), the undo
depth is asserted NOT to move at each step, and the flags are then proved to still round-trip
through `saveState`/`loadSettings` (the `readable`/`writable` attributes on the saved
`<midiport>` element are checked, and a second port in `Mode::Disabled` reads them back as
`true`). It is **red before the fix and green after** — see the "red check" note at the bottom.

## Bars and their exit codes

(measured unpiped, each `<command> > log 2>&1; echo EXIT=$?`)

| bar | command | exit |
|---|---|---|
| 4 (build) | `bash tools/local-ci.sh --build-dir build --jobs 2` | configure EXIT=0, build EXIT=0, ctest — see the summary line below |
| 4 (gates) | `bash tests/run-all-gates.sh` | see below |
| — | `bash tests/release-honesty-gate.sh --header build/lmmsversion.h --artifacts build` | see below |
| — | `bash tests/complexity-gate.sh --check` | 0 |
| — | `bash tests/file-length-gate.sh --check` | 0 |
| — | `bash tests/duplication-gate.sh` | 0 (duplicated lines 2.15%, budget 5%) |
| — | `bash tests/fork-sources-gate.sh` | 0 |
| — | `bash tests/no-upstream-regression-gate.sh` | 0 (the new ledger entry is honoured) |
| — | `bash tests/unregistered-tests-gate.sh` | 0 |
| — | `bash tests/evidence-gate.sh` | 0 |
| 3 (neighbouring suites) | `ctest -R 'ControlFreezeCommandsTranscript\|ControlUndo\|ControlSocketIntegration\|MidiClockTest\|ReversibilityContractTest\|ReversibilityUndo\|ControlPausedAndResumed'` and `ctest -R Midi` | see below |

## What is red / what could not be verified

(filled at the end of the lane)

## The single next action

(filled at the end of the lane)
