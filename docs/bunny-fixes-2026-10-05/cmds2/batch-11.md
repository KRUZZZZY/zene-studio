# cmds2 batch-11 — revisions restore, retrospective capture, safe-start clear

Lane: `ControlCommands[N-Z]*.cpp`. Six findings. Four fixed and committed one defect per commit,
two skipped as out of scope, each needing engine-side live-capture state that does not exist.

Summary: 4 FIXED, 2 SKIPPED-SCOPE, 0 FALSE-POSITIVE.

---

### Cluster 3978 — Recovery discard can remove a live capture's journal

Verdict: SKIPPED-SCOPE

Evidence: `src/core/ControlCommandsRecordingRecovery.cpp:136-148` — `recoveryDiscard` removes any
journal it can read and never calls `isRecoverable`, unlike its sibling
`recoveryRestore` at `:96-101`. The asymmetry is real. But the suggested guard cannot be the
one already in the file: a live capture's journal is written by `TrackRecorder::startRecording`
(`src/core/audio/TrackRecorder.cpp:184-192`) with `state = "in_progress"` and a take path that
exists, and it is refreshed about once a second of audio (`:322-326`). That is precisely what
`isRecoverable` tests (`src/core/RecordingJournal.cpp:198-203`), so adding the check would refuse
a live capture no more than it refuses a dead one — the predicate does not distinguish them.

Distinguishing them needs to know whether a `TrackRecorder` is currently writing that take, and
no such state is reachable from the command layer: `TakeRecorder` exposes only `of(effect)`
(`include/TakeRecorder.h:84`), keyed by `Effect*`, and the recorder that journals takes,
`TrackRecorder`, has no registry at all (bounded `rg -n -l 'recorders\(|allRecorders|recorderFor|takeRecorderFor'
src include tests` returns no match). Adding a live-capture registry is an engine change in
`src/core/audio/TrackRecorder.cpp`, not a control-command fix, and it touches a file the recorder
writer thread owns — outside this lane and outside the minimal-change rule.

Commit: none

Test: none

Red/green: none

---

### Cluster 4901 — Retrospective capture reports an overwrite count measured after the take snapshot

Verdict: FIXED

Evidence: `src/core/ControlCommandsRecordingRetro.cpp` — `retroToTake` copied the take out of
`window` and then read `capture->ring().overwrittenCount()` from the LIVE ring when building the
result, so a capture still running during the file write contributed overwrites that never
touched the bytes in that window. `RetroAudioCapture::takeWindow()`
(`src/core/RetroAudioCapture.cpp:78-108`) now reads `ring.overwrittenCount()` inside the existing
`beginSnapshot()` / `endSnapshot()` handshake and stores it in `RetroAudioCapture::Window`
(`include/RetroAudioCapture.h`), which `retroToTake` reports. No allocation, no locking and no
unbounded growth was added to the producer path — the read moved inside a handshake that was
already there.

Commit: bf755a768

Test: RetroAudioCaptureTest::theWindowCarriesTheOverwriteCountItWasCopiedWith

Red/green:

    cmake --build build --target zene RetroAudioCaptureTest            EXIT=0
    cd build/tests && ctest -R '^RetroAudioCaptureTest$' --output-on-failure   EXIT=8
      window.overwritten 0, expected 1024
    cmake --build build --target zene RetroAudioCaptureTest            EXIT=0
    cd build/tests && ctest -R '^RetroAudioCaptureTest$' --output-on-failure   EXIT=0  (9 passed)
    bash tests/complexity-gate.sh --check                             EXIT=0
    bash tests/file-length-gate.sh --check                             EXIT=0

---

### Cluster 4877 — A refused revision restore can leave a destructive undo step

Verdict: FIXED

Evidence: `src/core/ControlCommandsRevisions.cpp` — `registerRevisionsRestore` called
`control::addUndoStep` BEFORE `restoreTimelineRevision()`, which can refuse
(`src/core/RevisionTimeline.cpp:384`, live file over `ProjectRevisionPolicy::MaxRevisionBytes`,
`include/ProjectRevisions.h`). A refused restore therefore still pushed an undo step pointing at
a revision id, and a later `control.undo` would replay it. The restore now runs first and the
undo step is recorded only after it succeeds.

Commit: c7c156e5a

Test: RevisionTimelineTest::aRefusedRestoreRecordsNoUndoStep

Red/green:

    cmake --build build --target zene RevisionTimelineTest            EXIT=0
    cd build/tests && ctest -R '^RevisionTimelineTest$' --output-on-failure   EXIT=8
      undo depth 3, expected 2
    cmake --build build --target zene RevisionTimelineTest            EXIT=0
    cd build/tests && ctest -R '^RevisionTimelineTest$' --output-on-failure   EXIT=0
    bash tests/complexity-gate.sh --check                             EXIT=0
    bash tests/file-length-gate.sh --check                             EXIT=0

---

### Cluster 4878 — Revision restore redo may restore a different rotation entry

Verdict: FIXED

Evidence: `src/core/ControlCommandsRevisions.cpp` — the redo callback captured the revision id
string. With `ProjectRevisionPolicy::Keep = 3` (`include/ProjectRevisions.h`), restoring and then
undoing rotates the `.rev`/`.rev.2` files, so `rev1`/`rev2` name different bytes than they did at
restore time and redo restored the wrong revision. The handler now captures
`sha256OfFile(target)` after a successful restore and the redo resolves rotation ids by content
(`revisionHoldingBytes()` / `isRotationId()`, file-local helpers in the same file). Stable ids
(`backup`, `autosave`, `git:<sha>`) still resolve directly, and a rotation id whose content is
gone is a no-op rather than a wrong restore. `recordRestoreUndoStep()` was extracted so the
complexity gate stayed satisfied — the change pushed `registerRevisionsRestore` to CCN 12 against
a limit of 10, and the helper split fixed that without touching the gate.

Commit: 955e050a5

Test: RevisionTimelineTest::theTimelineRedoRestoresTheBytesTheRestoreSelected

Red/green:

    cmake --build build --target zene RevisionTimelineTest            EXIT=0
    cd build/tests && ctest -R '^RevisionTimelineTest$' --output-on-failure   EXIT=8
      live file held documentWith(3), expected documentWith(1)
    cmake --build build --target zene RevisionTimelineTest            EXIT=0
    cd build/tests && ctest -R '^RevisionTimelineTest$' --output-on-failure   EXIT=0
    bash tests/complexity-gate.sh --check                             EXIT=0
    bash tests/file-length-gate.sh --check                             EXIT=0

---

### Cluster 1223 — The revision restore transaction advertises the restore itself as its inverse

Verdict: FIXED

Evidence: `src/core/ControlCommandsRevisions.cpp` — the `__transaction` block advertised
`revisions.restore` with `{path, id}` as the inverse. That command applies the CHOSEN revision, so
replaying the advertised inverse reapplies the restore instead of putting back the file it
replaced; the advertised args also used `path`, which is not a member of the command's schema
(the schema takes `project`). The transaction now advertises `control.undo` with an empty
`QJsonObject()`, the actual recorded inverse of the restore's checkpoint. No result key, param or
id changed.

Commit: 930bf62f7

Test: RevisionTimelineTest::theTimelineRestoreAdvertisesAReplayableInverse

Red/green:

    cmake --build build --target zene RevisionTimelineTest            EXIT=0
    cd build/tests && ctest -R '^RevisionTimelineTest$' --output-on-failure   EXIT=8
      the advertised inverse is refused when replayed
    cmake --build build --target zene RevisionTimelineTest            EXIT=0
    cd build/tests && ctest -R '^RevisionTimelineTest$' --output-on-failure   EXIT=0
    bash tests/complexity-gate.sh --check                             EXIT=0
    bash tests/file-length-gate.sh --check                             EXIT=0

---

### Cluster 4879 — `safestart.clear` reports success without checking whether files were removed

Verdict: FIXED

Evidence: `src/core/ControlCommandsSafeStartEdit.cpp:128-160` — `safeStartHandleClear` called the
void engine operation and then wrote `cleared: true` unconditionally, while `SafeStart::clear()`
(`src/core/SafeStart.cpp:365-378`) ignores both `QFile::remove()` results (`:369-370`) and flips
the in-memory state regardless. A marker or acknowledgement that could not be unlinked stayed on
disk while the reply said the next launch would be a normal one. The handler now reads both paths
back after the call — the same read-back `safeStartHandleAcknowledge` already does at
`src/core/ControlCommandsSafeStartEdit.cpp:93` — and returns a typed `Busy` refusal naming what is
still on disk. No result key, param or id changed.

Commit: 6b9528506

Test: SafeStartTest::clearRefusesWhenTheMarkerCannotActuallyBeRemoved

Red/green:

    cmake --build build --target zene SafeStartTest                    EXIT=0
    cd build/tests && ctest -R '^SafeStartTest$' --output-on-failure   EXIT=8
      9 passed, 1 failed — '!reply.ok' returned FALSE (safestart.clear reported
      success although neither file was removed)
    cmake --build build --target zene SafeStartTest                    EXIT=0
    cd build/tests && ctest -R '^SafeStartTest$' --output-on-failure   EXIT=0
    cd build/tests && ctest -R '^(SafeStartLoadPathTest|ControlSurfaceReferenceTest|CommandFormTest|CommandPaletteTest)$' --output-on-failure   EXIT=0
    bash tests/complexity-gate.sh --check                             EXIT=0
    bash tests/file-length-gate.sh --check                             EXIT=0

The test makes the working directory read-only so unlink cannot succeed for a non-privileged uid,
and QSKIPs where the machine (root) ignores directory write permissions — the condition cannot be
produced there.

---

Leftovers: cluster 3978 is left unfixed on purpose — closing it needs a live-capture registry in
`src/core/audio/TrackRecorder.cpp`, which no command-layer code can reach today. The
`isRecoverable` asymmetry between `recovery_discard` and `recovery_restore` stays as it is.
Nothing else from this batch is half-done.
