FALSE-POSITIVE

# Cluster new-p12-351: "Linked note mirroring may allocate and journal on the audio thread"

The finding is conditional: *"If MIDI recording mutates a linked `MidiClip` through these note
entry points on the audio thread..."* (`src/core/ClipLinks.cpp:133`, text in the finding). The
condition does not hold in this tree. Recording is deliberately split into a realtime half that
only enqueues raw MIDI events and a main/UI-thread half that turns them into notes. Every note
entry point that reaches `ClipLinks::mirrorContent()` runs on the main/UI thread. No product
change and no regression test were made; there is no defect whose fix a test could prove.

## Evidence: what the finding names

`ClipLinks::contentFingerprint()` (`src/core/ClipLinks.cpp:125-166`) does build a `QDomDocument`,
serialises the note list and returns a `QString`. `ClipLinks::mirrorContent()`
(`src/core/ClipLinks.cpp:204-266`) builds `QVector`s and calls
`ProjectJournal::addJournalCheckPoint()` at `:252-256`. All of that allocates and journals — but
only on the thread that calls `mirrorContent()`. The finding's premise is therefore entirely about
who calls it, and the answer is in the recording paths.

## Evidence: the recording paths

### 1. Arrangement MIDI take recording: realtime half is allocation-free, commit is main thread

- The audio thread only reaches `MidiTakeCapture::capture()`:
  `src/tracks/InstrumentTrack.cpp:498-500`
  (`if (event.source() == MidiEvent::Source::External) { m_midiTakeCapture.capture(event); }`).
- `MidiTakeCapture::capture()` (`src/core/MidiTakeCapture.cpp:72-86`) is `noexcept`, reads atomics,
  fills one POD `MidiTakeEvent` and calls `push()`; `push()` (`:89-116`) is a bounded Vyukov ring,
  also `noexcept`, with no allocation, no lock and no wait. The header states the contract:
  *"capture() takes no lock, never allocates and never waits ... The single consumer is the main
  thread (MidiTakeRecorder)"* (`include/MidiTakeCapture.h:55-62`). No `MidiClip` is touched there.
- `MidiTakeRecorder` is created as a child of `Song` on the main thread
  (`src/core/Song.cpp:260`), drains from a `QTimer` (`src/core/MidiTakeRecorder.cpp:105-108`,
  `:163-168`) and calls `clip->addNote(...)` only in `commitPlain()` / `loopTakeClip()`
  (`src/core/MidiTakeRecorder.cpp:322-342`, `:363-374`) after `Song::stopped`, which is emitted
  from `Song::stop()` (`src/core/Song.cpp:1328`). `MidiTakeRecorder` is a child of `Song` created
  on the main thread (`src/core/Song.cpp:260`), so the default `AutoConnection` delivers
  `onStopped()` in the main thread even when `Song::stop()` itself is called from another
  non-audio thread (for example the export worker); a normal UI/control stop runs it directly on
  that thread. It is never the audio thread. The existing
  test `MidiTakeRecordTest::theCaptureQueueIsBoundedAndAllocationFree`
  (`tests/src/core/MidiTakeRecordTest.cpp:210-231`) asserts the producer half allocates and frees
  nothing, using the allocation counter, not wall-clock timing.

### 2. Piano-roll live recording: the note is added in a GUI-thread slot

- `NotePlayHandle` emits `midiNoteOn` / `midiNoteOff` for `Origin::MidiInput`
  (`src/core/NotePlayHandle.cpp:119-125`, `:511-521`). Those signals may be emitted from the audio
  thread, but the receivers are `PianoRoll` slots connected with the default `AutoConnection`
  (`src/gui/editors/PianoRoll.cpp:923-924`: `connect(..., SIGNAL(midiNoteOn(...)), this,
  SLOT(startRecordNote(...)))`, no `Qt::DirectConnection`). A signal emitted from a thread other
  than the receiver's thread is queued by Qt, so both slots run in the GUI thread.
- `startRecordNote()` (`src/gui/editors/PianoRoll.cpp:4516-4555`) only appends to
  `m_recordingNotes`; the note is created later by `finishRecordNote()`
  (`:4557-4624`) with `m_midiClip->addNote(n1, false)` at `:4612` — GUI thread.
- Step recording is also GUI-driven: `StepRecorder::notePressed` / `noteReleased` come from the
  piano roll's key handlers, and `StepRecorder::applyStep()` calls `addNote` at
  `src/core/StepRecorder.cpp:247-253`; the recorder runs on a GUI timer
  (`src/core/StepRecorder.cpp:113-130`).

### 3. Control-surface note edits are dispatched on the UI thread

Every `ControlCommands*` caller of `mirrorContent()` (`ControlCommandsNotes.cpp:285,370,438`,
`ControlCommandsNoteEdit.cpp:461`, `ControlCommandsNoteShared.h:75`, etc.) goes through
`ControlRegistry::dispatch()`, which runs the handler through
`ControlRegistry::runOnUiThread()` (`src/core/ControlRegistry.cpp:274-300`, helper at
`:205-219`). `mirrorContent()` and its journal checkpoint therefore run on the UI thread for that
path too.

### 4. The mirror's own design says so

`src/core/ClipLinks.cpp:54-57` documents the re-entrancy guard as *"UI-thread only, like every
other write path here."* The write paths above are the complete set: `rg -n
"addNote\(|removeNote\(" src` finds them in `src/gui/` (`PianoRoll.cpp`, `MidiClipView.cpp`),
the UI-thread `MidiTakeRecorder`, `StepRecorder`, `RetroMidiClipWriter`, the script/control
commands, and `src/tracks/MidiClip.cpp`'s own note methods. No
`AudioEngine*`, `NotePlayHandle`, `Mixer`, `PlayHandle` or `InstrumentTrack::play()` file calls
`addNote`, `removeNote`, `notesChanged` or `mirrorContent`.

## Why no defensive fix was added

The finding's own fix, *"keep recording mutations off the audio thread, or queue linked
propagation"*, is already the implemented design: the audio thread records into a bounded,
allocation-free event queue and the mutation is committed on the main thread. Adding a second
deferral inside `mirrorContent()` would have no caller to exercise it and would introduce a new
consistency policy (when the queued mirror is flushed) for behaviour nobody reaches. That would be
a guessed fix, not the cause. Per the procedure, no product change and no test are made for a
false positive.

## Commands run (this session, worktree `kads-new-p12-351`)

Logs are under `/home/kruzzzzy/.config/kads/zene/state/runs/new-p12-351.r1/logs/`.

| Step | Command | EXIT | Result |
| --- | --- | --- | --- |
| Rebuild affected targets | `cmake --build build --target MidiTakeRecordTest ClipLinkTest LinkedClipGroupEditsTest MidiClipLinkedEditTest zene -j4` (worktree root) | 0 | `build-1.log`; all targets built |
| Recording + linked families | `ctest -R 'MidiTakeRecordTest\|ClipLinkTest\|LinkedClipGroupEditsTest\|MidiClipLinkedEditTest' --output-on-failure` (from `build/tests`) | 0 | `test-1.log`; 4/4 passed (ClipLinkTest passed, LinkedClipGroupEditsTest passed, MidiClipLinkedEditTest passed, MidiTakeRecordTest passed) |
| Feature gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p12-351` | 0 | `feature-check-1.log`; all eight gates PASS (`fork-sources`, `all-sources-reproduce`, `no-upstream-regression`, `check-namespace`, `no-tautology`, `doc-figures`, `complexity`, `file-length`) |
| Completion check, before the report | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> new-p12-351` | 1 | `submit-check-1.log`; only the missing report and Self-check failed |
| Completion check, final | same command after this report | 0 | `submit-check-2.log`; `RESULT: PASS - submit` |

Not run: the step 4 RED/GREEN reversal. It is not applicable because the verdict is
FALSE-POSITIVE and there is no product hunk to reverse and no new test.

The completion check's `tests added or changed` row passes on test files that are already in the
branch's merge-train diff (`base..HEAD`), not on a test from this session; this session adds only
this report.

## Self-check

- **Regression test FAILS with the product change reverted and PASSES with it:** not applicable.
  The verdict is FALSE-POSITIVE; no product code changed, so there is no product hunk to reverse
  and no new regression test. The existing recording and linked-clip tests were run instead:
  `ctest -R 'MidiTakeRecordTest|ClipLinkTest|LinkedClipGroupEditsTest|MidiClipLinkedEditTest'`
  exited 0 with 4/4 passed (`test-1.log`).
- **Deterministic test:** no test was added. The existing `MidiTakeRecordTest` producer check uses
  `lmms::test::resetAllocationCount()` and the queue's claim-gate test seam, with no sleeps,
  wall-clock timing or races (`tests/src/core/MidiTakeRecordTest.cpp:210-231`, `:266-310`).
- **Cause and every caller/sibling:** no product edit. I enumerated every caller of
  `mirrorContent()` (`rg -n "mirrorContent" src include`) and every `addNote` / `removeNote` /
  `notesChanged` caller (`rg -n "addNote\(|removeNote\(|notesChanged\(\)" src include`), and traced
  both recording families (arrangement take recording and piano-roll/step recording) as above.
  None of them is an audio-path file; the complete recording path is `InstrumentTrack.cpp:500 ->
  MidiTakeCapture.cpp:72-116` on the realtime side and `MidiTakeRecorder.cpp:322-374` on the
  main-thread side.
- **No collateral damage:** no product file, project format or serialization code changed. The
  four affected test families pass, and the gates pass.
- **ALREADY-FIXED / FALSE-POSITIVE evidence:** `src/core/ClipLinks.cpp:125-166` (the allocating
  fingerprint) and `:204-266` (vectors + journal checkpoint) are only reached from UI-thread
  callers: arrangement recording commits in `src/core/MidiTakeRecorder.cpp:322-374` (main-thread
  `QTimer`, `:105-108`, `:163-168`), piano-roll recording in
  `src/gui/editors/PianoRoll.cpp:923-924` (AutoConnection) and `:4612` (GUI slot), and control
  commands through `src/core/ControlRegistry.cpp:274-300` (`runOnUiThread`). The realtime half is
  allocation-free by contract and by test: `include/MidiTakeCapture.h:55-62`,
  `src/core/MidiTakeCapture.cpp:72-116`,
  `tests/src/core/MidiTakeRecordTest.cpp:210-231`. `src/core/ClipLinks.cpp:54-57` records the
  intended contract: every mirror write path is UI-thread only.
- **Sibling commands in changed command files:** this session changed no command file and added
  no command (the finding is a false positive). The sibling list in the completion check
  (`app.*`, `arrangement.*`, ...) is produced from the branch's merge-train diff
  (`base..HEAD`), not from this cluster; those files are unchanged by this session and are not
  affected.

## Other findings

- `ClipLinks::mirrorContent()` has no runtime thread guard. That is not a defect today because
  every caller is on the UI thread (above), but if a future audio-thread caller is added, the
  guard would have to be a deferral with an explicit flush/consistency policy rather than a
  silent no-op. Not fixed here; it is outside this finding's reachable path.
