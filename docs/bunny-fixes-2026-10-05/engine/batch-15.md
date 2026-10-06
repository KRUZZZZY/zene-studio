# engine batch-15 — 2026-10-05

Clusters 4411, 5308, 3510, 5307, 3461, 5354.

Two fixed, one duplicate, three skipped as out of scope (all three need a
design change that rule 6/rule 7 exclude, and one of them also rule 8).

### Cluster 4411 — Recording allocates and starts a thread from the render path
Verdict: SKIPPED-SCOPE
Evidence: `src/core/SampleRecordHandle.cpp:45-66` — the constructor builds
`std::make_unique<SampleRecordAccumulator>(...)`, whose own constructor
allocates a 128 KiB ring, a 4096-frame scratch vector and a shared take and
then starts a `std::thread` (`include/SampleRecordAccumulator.h:91-105`). It is
constructed from `SampleTrack::play()` at `src/tracks/SampleTrack.cpp:205`,
reached from `Song::processNextBuffer()` inside `renderStageNoteSetup()`
(`src/core/AudioEngine.cpp:428`), which `renderNextPeriod()` runs under a
`RealtimeScope` while holding `m_changeMutex` (`src/core/AudioEngine.cpp:523-525`).
That is a real RT violation: a `clone()` syscall and a heap allocation on the
device thread. The source file named in the report, `src/core/SampleRecordHandle.cpp`,
is correct; the sibling report 5307 names `src/core/SampleTrack.cpp`, which is
`src/tracks/SampleTrack.cpp` here.
Committed: none. Test: none.
Red/green: none.
Why skipped: the fix ("create and start recording accumulators from a control
or worker thread before the audio thread begins recording") needs a pre-arm
hook that knows which clips will record, plus accumulator ownership and lifetime
across the take — a scheduling design change well past a behaviour-preserving
one-line fix, and it would have to allocate or block somewhere new.

### Cluster 5307 — Starting a recording allocates and launches a thread on the audio thread
Verdict: DUPLICATE
Evidence: `src/tracks/SampleTrack.cpp:205` is the same construction site as
cluster 4411 (`src/core/SampleRecordHandle.cpp:45-66`), reached from the same
`renderStageNoteSetup()` path; the two reports describe one defect from the
callee's and the caller's side. Filed path `src/core/SampleTrack.cpp` does not
exist; the file is `src/tracks/SampleTrack.cpp`.
Commit: none. Test: none. Red/green: none.
Carried under 4411's verdict (SKIPPED-SCOPE).

### Cluster 5308 — Take completion mutates Qt and clip state from the audio path
Verdict: SKIPPED-SCOPE
Evidence: `src/core/SampleRecordHandle.cpp:71-81` — the destructor calls
`m_clip->setSampleBuffer(std::move(buffer))` and `m_clip->setRecord(false)`, and
it runs on the render thread from the handle-removal loop at
`src/core/AudioEngine.cpp:404` (and the equivalent at `:461`), inside the same
`renderNextPeriod()` scope as 4411. `setSampleBuffer()`
(`src/core/SampleClip.cpp:153-166`) does take `requestChangesGuard()`, calls
`updateLength()`, `emit sampleChanged()` and `Song::setModified()` — genuinely
model/Qt work on the audio thread, so the defect is real.
The report's "stall or deadlock" claim is wrong, though: `m_changeMutex` is a
`std::recursive_mutex` (`include/AudioEngine.h:616`) and `renderNextPeriod()`
already holds it on that same thread, so the re-entry cannot deadlock. The
finding also overstates the work: the expensive half was already moved off the
audio thread — the take-sized `SampleBuffer` construction happens on the
accumulator's drain thread and `finish()` only hands the finished buffer back
(`src/core/SampleRecordHandle.cpp:73-78`, the "D9c" note).
Commit: none. Test: none. Red/green: none.
Why skipped: the prescribed fix — "publish the completed buffer to the control
thread and update the clip there" — is a new cross-thread hand-off with its own
ownership and lifetime contract, changing when a finished take becomes visible
on the clip. Design change, not a defect-sized fix.

### Cluster 3510 — Ring overflow silently removes recorded audio
Verdict: SKIPPED-SCOPE
Evidence: `src/core/SampleRecordHandle.cpp:185-192` forwards to
`SampleRecordAccumulator::append()`, which drops and counts but whose
`framesDropped()` (`include/SampleRecordAccumulator.h:126-129`) nothing in the
clip-record path ever reads — confirmed by a bounded `rg -n 'framesDropped'
src include tests`, whose only other hits are the per-route recorders. The clip
path is the outlier: every other ring-based recorder does surface its loss
(`src/core/ControlRecordingSupport.cpp:195` `overflow_frames`,
`src/core/audio/MultiTrackRecorder.cpp:140`,
`src/core/ControlCommandsRecorder.cpp:71` `dropped`). The take is therefore
shortened and misaligned with no report.
Commit: none. Test: none. Red/green: none.
Why skipped: neither half of the proposed fix is available here. "Preserve or
retry the rejected tail" needs unbounded buffering or a blocking write on the
audio thread (rule 6), and "surface an explicit recording failure and dropped
frame count" needs a new control command carrying a new field, which rule 8
forbids. Reproducing it deterministically would also need the drain thread
starved for ~0.34 s, which is not a reliable headless test. Left as-is with
this evidence, so the gap is recorded rather than silently dropped.

### Cluster 3461 — Malformed pattern IDs can silently refer to pattern 0
Verdict: FIXED
Evidence: `src/core/SessionClip.cpp:149` read the `pattern` attribute with
`QString::toInt()` and ignored the conversion result. `toInt()` returns 0 both
for text that is not an integer and for an out-of-range value, and 0 is a valid
`PatternStore` id — while the member's own sentinel is -1
(`include/SessionModel.h:193`). A cell saved as `pattern="999999999999"`
therefore loaded as a reference to the first pattern in the store, and
`publishSessionContent` handed that id to the render path
(`src/core/SessionPlayback.cpp:171`). Now a failed conversion restores as -1,
which is what a missing attribute already produced.
Commit: 8233fc524
Test: SessionModelTest::anUnreadablePatternIdIsNotPatternZero
Red/green: `ctest -R '^SessionModelTest$'` → EXIT=8 (Actual `slot.patternId()`
0, Expected -1 at SessionModelTest.cpp(330); Totals: 7 passed, 1 failed) → same
command EXIT=0 after the fix. Neighbours rebuilt and rerun:
SessionPerformTest, ScenesUpconvertTest, SessionSchedulerTest,
SessionSceneFollowTest, ProjectVersionTest — all EXIT=0.
Gates: complexity-gate --check EXIT=0, file-length-gate --check EXIT=0.

### Cluster 5354 — Disarmed follow plans permanently consume plan slots
Verdict: FIXED
Evidence: `src/core/SessionFollow.cpp:93-105` — disarming is an install with
`enabled` false, and the replace-in-place loop overwrote `installed.plan` while
leaving the entry's `track` and `scene` set. The free-entry loop below only
claims entries whose `track` is negative, so a disarmed entry could never be
reclaimed: `MaxFollowPlans` is 16 (`include/SessionFollow.h:84`), so 16 cells
armed and disarmed in turn exhausted the table and every later plan was refused
and counted by `droppedCommands()` until `reset()`. A disarmed plan now clears
its entry (`InstalledFollowPlan{}`) instead of staying addressed to the cell.
Commit: 1c2a5652d
Test: SessionFollowTest::aDisarmedPlanGivesItsSlotBack
Red/green: `ctest -R '^SessionFollowTest$'` → EXIT=8 (Actual
`droppedCommands()` 1, Expected 0 at SessionFollowTest.cpp(409); Totals: 11
passed, 1 failed) → same command EXIT=0 after the fix. Neighbours rebuilt and
rerun: SessionSceneFollowTest, SessionSchedulerTest — both EXIT=0.
Gates: complexity-gate --check EXIT=0, file-length-gate --check EXIT=0.

Leftovers: cluster 3510 is a real, still-present gap — a starved drain thread
silently shortens a clip take — but every available remedy is ruled out of this
lane (rule 6 for the retry, rule 8 for a new reporting command); it needs a
decision from the parent. Clusters 4411/5307 and 5308 are real RT-path defects
(a `std::thread` plus a 128 KiB allocation, and clip/Qt mutation, on the device
thread) that need a scheduling and ownership design change, not a patch here.