# LANE STATE — `030/freeze-journal-fix` (the 0.3.0-alpha release gate)

**This file replaces the base's `LANE-STATE.md`.** Every lane writes its own at its worktree
root (that is what the sibling lanes' files are), so this one is this lane's. Nothing is lost:
the version at this branch's base `598d4f5c1` is the `030/folder-tracks` lane's own handoff and
is recoverable verbatim with `git show 598d4f5c1:LANE-STATE.md` (711 lines).

**Worktree** `/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/zene-030/wjrnl`
**Branch** `030/freeze-journal-fix`, created at `598d4f5c1` (`release/0.3.0`; base tip unchanged,
nothing rebased, nothing merged, nothing pushed)
**Logs / raw evidence** `/home/kruzzzzy/zene-030-wjrnl-598d4f5c1/`
(`depth_probe.py`, `BEFORE-*.log`, `AFTER-*.log`, `depth-*.log`, `redcheck-*.log`,
`local-ci-baseline.log`, `local-ci-final.log`, `gate-*.log`, `ctest-*.log`)

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
echo EXIT=$?
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

**`src/core/midi/MidiPort.cpp:87-88`** (constructor; the banner comment is `:70-86`):
`m_readableModel.setJournalling(false)` / `m_writableModel.setJournalling(false)`. **Why this and
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

`src/core/midi/MidiPort.cpp` was **byte-identical to the fork point `4e677cb6c6ab`** before this
change (`git diff --stat 4e677cb6c6ab -- src/core/midi/MidiPort.cpp` was empty), so this is
declared divergence: the ledger entry in `tests/upstream-modifications.txt` (after
`src/core/midi/MidiJack.cpp`) is part of the same commit, and Gate 6 passes with it.

## The regression test in this change

`tests/src/core/UndoBoundsTest.cpp:414-493` — `aDeviceAssignmentIsNotAnUndoStep` (function at
`:435`): a `MidiPort` is built over the live client, the flags are flipped in both directions and
read back (so the guarded "value unchanged" early return at `AutomatableModel.cpp:310` cannot be
what makes it pass), the undo depth is asserted NOT to move at every step, and the flags are then
proved to still round-trip through `saveState`/`loadSettings` (the `readable`/`writable`
attributes on the saved `<midiport>` element, and a second port in `Mode::Disabled` reading them
back as `true`).

**Seen red, not claimed red** (`redcheck-*.log`): with the two `setJournalling(false)` calls
temporarily forced back to `true`, the rebuilt binary fails —
`FAIL! aDeviceAssignmentIsNotAnUndoStep: Actual (depth()): 2 / Expected (before): 1`,
`UndoBoundsTest.cpp(448)`, exit 1 (the constructor's `setValue` pushed exactly one step: the
`+1` per track start). The file was then restored (`sha256sum -c` against the pre-red-check
digest printed `OK`), the target rebuilt, and the test passes again.

## Bars and their exit codes (each measured unpiped, `cmd > log 2>&1; echo EXIT=$?`)

| bar | command | exit |
|---|---|---|
| 4 | `bash tools/local-ci.sh --build-dir build --jobs 2` | **0** — configure 0, build 0, ctest 0: `100% tests passed, 0 tests failed out of 147` (147 = the tree's own count; this change adds no ctest entry) |
| 4 | `bash tests/run-all-gates.sh` | **3** = PASS-WITH-SKIPS (gates 1,3,4,5,6,7,8,9,10,11 all PASS; gate 2 coverage skipped because `--with-coverage` was not passed — the documented skip, and an accepted exit for this bar) |
| — | `bash tests/release-honesty-gate.sh --header build/lmmsversion.h --artifacts build` | **0** (`RESULT: PASS — all 6 documented feature(s) match this build on linux`) |
| — | `bash tests/complexity-gate.sh --check` | **0** |
| — | `bash tests/file-length-gate.sh --check` | **0** |
| — | `bash tests/duplication-gate.sh` | **0** (duplicated lines 2.15%, budget 5%) |
| — | `bash tests/fork-sources-gate.sh` | **0** |
| — | `bash tests/no-upstream-regression-gate.sh` | **0** (the new ledger entry is honoured) |
| — | `bash tests/unregistered-tests-gate.sh` | **0** |
| — | `bash tests/evidence-gate.sh` | **0** |
| 3 | `ctest -R 'ControlFreezeCommandsTranscript\|ControlUndo\|ControlSocketIntegration\|MidiClockTest\|ReversibilityContractTest\|ReversibilityUndo\|ControlPausedAndResumed' --output-on-failure` | **0** (5/5, `ControlFreezeCommandsTranscript` among them) |
| 3 | `ctest -R Midi --output-on-failure` | **0** (8/8) |
| 3 | `ctest -R UndoBoundsTest --output-on-failure` + `./UndoBoundsTest aDeviceAssignmentIsNotAnUndoStep` | **0** (the new case PASSes; exit 1 in the red check above) |

`git status` in this worktree is clean after every gate run (Gate 5's mutation sweep restores its
mutant; verified again after `run-all-gates.sh`). `zene-030` was only ever read: its tracked
files are untouched (`git -C …/zene-030 status --porcelain` shows no ` M ` entry).

## What is red, and what could not be verified

* **Nothing is red in this lane's own bar list.** The one non-zero bar is `run-all-gates.sh`
  **exit 3**, which its own header defines as "every gate that ran passed, but one was SKIPPED".
* **Gate 2 (coverage) was NOT run** — it needs `--with-coverage` and its own instrumented build
  (this box has one build directory by rule). The new code in `MidiPort.cpp` is two calls; the
  new test case executes both branches of the flag.
* **The whole-tree scope** (`gates 4/7/8 --scope all`) was not measured: it is not part of a
  default run and not part of the asked-for bar. `tests/src/core/UndoBoundsTest.cpp` is now
  **495 lines against Gate 7's 500-line limit** — 5 lines of headroom, so the next edit to it
  must move code rather than grow it.
* **Only the Linux path was measured.** The fix is unconditional (set in the constructor, no
  `#ifdef`, no client check), but macOS `MidiApple`/Windows `MidiWinMM` jobs cannot be reproduced
  on this x86_64 host, and their clients are not raw — for them the fix is a no-op on a path
  that never fired. The CI matrix must confirm; nothing was pushed (per instruction).
* **The CI runs themselves were not read back.** The job ids and the failing assertion come from
  the read-only investigation (`/home/kruzzzzy/zene-freeze-investigation/FINDINGS.md`); what is
  measured HERE is the same assertion reproduced verbatim, and 0-of-41 trace equality between
  the local `MIDIDEV=/dev/null` run (before) and the CI job's trace.
* **A behaviour trade, stated rather than hidden:** the "ENABLE MIDI INPUT" / "ENABLE MIDI
  OUTPUT" LED in the instrument window (wired to these models at
  `src/gui/instrument/InstrumentMidiIOView.cpp:169,173`) is no longer undoable — it is still
  saved with the project and still restored on load, and no test in the tree asserted its
  undoability (`grep -rn 'isReadable\|setReadable' tests/` finds none). Every other writer of
  these flags is the engine's own auto-assignment, which is exactly what must not be an undo
  step.
* The narrower alternative was **not** implemented, so its predicted `+3/+3` offsets were not
  re-measured; they were not needed, because the chosen fix removed them (measured: startup
  depth 4 → 1 under `MIDIDEV=/dev/null`).

## The single next action

**The parent merges `030/freeze-journal-fix` into `release/0.3.0`** (base `598d4f5c1` is
unchanged, so no rebase is needed and nothing has to be resolved) and watches the two Linux jobs
report `ControlFreezeCommandsTranscript` green — the 40-second local pre-check for that is
exactly the reproduction command above with `MIDIDEV=/dev/null`.
