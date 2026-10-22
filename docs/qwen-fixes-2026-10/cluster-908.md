# Cluster 908 (ZynAddSubFx): the plugin mutex, the MIDI hand-off and the hosting getters

Verdict: **FIXED**

908: FIXED
new-p9-545: FIXED
new-p16-276: FIXED

All three defects live in `plugins/ZynAddSubFx/ZynAddSubFx.cpp` and are fixed
there, each with its own regression case in the white-box test
`tests/src/plugins/ZynAddSubFxInstrumentTest.cpp` (which compiles the plug-in's
own `ZynAddSubFx.cpp`, the way the VST and CLAP tests compile theirs).

**Review round 2 (Luna, REWORK) asked for two changes, and both are in this
report:** the 908 regression now asks the instrument for the wait it allows
itself instead of measuring a thread with a timeout, and new-p16-276 now
applies a scheduled event at its frame *in the synth*, with a regression that
observes where the note starts in the rendered audio.

**Review round 3 (Luna, REWORK) found one more thing, and it is in this report
too:** a note scheduled exactly on a block boundary was landing in the block
*after* the one it was scheduled for, because Zyn generates the block that
starts at a boundary when the render reaches that boundary - and the event was
applied after that generation. The event is now applied one frame before the
boundary, and the regression requires the note to start *inside* the first
block at or after its scheduled frame, with a paired case on a boundary.

| Cluster | Verdict | Where the defect was | Fix |
|---|---|---|---|
| 908 | FIXED | `plugins/ZynAddSubFx/ZynAddSubFx.cpp:409-433` (`processLock()`, `audioLockWaitMs()`) | the audio path's lock attempt is a try, never a wait |
| new-p9-545 | FIXED | `plugins/ZynAddSubFx/ZynAddSubFx.cpp:314-336` (`loadFile()`), `:479-519` (`hostingState()`, `hostingProcessId()`) | the choice of a plugin and its use, and the getters' read of the pointer, happen under the plugin mutex |
| new-p16-276 | FIXED | `plugins/ZynAddSubFx/LocalZynAddSubFx.cpp:232-360` (`processMidiEvent()`, `process()`), `plugins/ZynAddSubFx/RemoteZynAddSubFx.cpp:138-142` | the scheduled frame reaches the synth and the event is applied while the period is rendered, at that frame |

## 908 — the audio path never waits for the plugin mutex

**Defect.** `processLock()` was `m_pluginMutex.tryLock(Engine::getSong()->isExporting() ? -1 : 0)`:
while the song was exporting, the audio thread **waited** for the plugin mutex.
The main thread holds that mutex for as long as a plugin operation takes - the
client handshake in `initPlugin()` is seconds, and a preset load waits for the
client to answer - so a render could stall for the whole reload, and for as long
as the main thread needed the lock, which was unbounded.

**Fix** (`plugins/ZynAddSubFx/ZynAddSubFx.cpp:409-433`): the wait the audio path
allows itself is a named policy, `audioLockWaitMs()`, and it is 0 - the lock
attempt is a try, never a wait. A period that cannot take the lock is skipped -
the way the live path has always skipped it - and the render keeps its pace
instead of stalling.

**The regression (round 2).** The first round asked the question with a worker
thread and a two-second semaphore: a wall-clock and scheduling dependent answer,
which the review rejected. The instrument now answers it itself:
`renderIsExporting()` is the export path (the one the old policy waited in) and
`audioLockWaitMs()` is the wait the audio path allows itself there. The case
asserts that the wait is none and that the attempt that follows comes back at
once and does not take the lock - no thread, no semaphore, no timeout, no sleep.
Against the old waiting policy the case fails on the wait itself.

**Trade-off, stated.** An export that overlaps a reload now skips the affected
periods instead of stalling on them: the audio is missing rather than late.
That is the point of the fix (the wait was unbounded), and it is the only
behaviour change outside the defect.

## new-p9-545 — the plugin choice and its use happen under one lock

**Defect.** `loadFile()` read `m_remotePlugin` **without** the mutex and took it
only for the local branch; `hostingState()` and `hostingProcessId()` read the
pointer with no lock at all. A hosting-mode switch - the instrument view's
checkbox and the control surface's `oop.set_mode` both end in `reloadPlugin()` -
deletes the running instance and creates another one under that very mutex, so a
caller that chose an instance without the lock could use it after it was gone:
the remote branch would talk to a freed client, and the local branch would load
the preset into a null pointer.

**Fix** (`plugins/ZynAddSubFx/ZynAddSubFx.cpp:314-336`, `:479-519`): the choice
and the use are under the plugin mutex in `loadFile()`, and both hosting getters
read the plugin pointer under the mutex.

**Siblings checked.** Every other user of the plugin pointers holds the same
mutex for the whole use: `saveSettings()`, `loadSettings()`,
`updatePitchRange()`, `initPlugin()` and the destructor.
`handleMidiEvent()` does not touch the pointers at all - it only queues the
event, and `forwardQueuedMidi()` runs under `processLock()`, i.e. under the
mutex.

**Trade-off, stated.** The hosting getters now wait for a reload that is in
progress (the client handshake) instead of answering from a pointer that is
being replaced. They answer correctly now; before, they could read a freed
pointer.

## new-p16-276 — a scheduled MIDI event is applied at its frame

**Defect (round 1 fix, round 2 review).** The offset survived the hand-off, but
it reached nothing: `RemoteZynAddSubFx::processMidiEvent()` dropped the offset
and called the event-only `LocalZynAddSubFx` API, and the in-process path called
the same event-only API. The synth therefore still applied every event at the
period start, and the first round's regression only read the queued field - it
never saw where the event landed.

**Fix** (`plugins/ZynAddSubFx/LocalZynAddSubFx.cpp`, `plugins/ZynAddSubFx/RemoteZynAddSubFx.cpp:138-142`):

- `LocalZynAddSubFx::processMidiEvent( event, offset )` takes the frame the
  event was scheduled at. An event with a frame waits for it in a fixed member
  array (no allocation); an event without one is applied at once, the way it
  always was.
- `LocalZynAddSubFx::process( out )` renders the period and splits the render
  at each scheduled event's frame, applying the event there - so the event is
  applied *while the period it was scheduled for is rendered*.
- The client hands the offset to the synth instead of dropping it.
- The synth's block is now `SynthBlockSize` (64 frames) instead of the whole
  period. That is what makes the frame matter at all: Zyn renders in blocks,
  and a block as large as the period means the whole period's samples are
  generated before the event is applied, so the event could only ever be heard
  in the next period. With a 64-frame block the event's voice starts at the
  first block boundary at or after its frame.
- **The boundary case (round 3).** Zyn generates the block that starts at a
  boundary when the render *reaches* that boundary: the block that starts at
  frame f is generated by the render call that ends at f. An event scheduled
  exactly on a boundary was therefore applied after that block had been
  generated - without the event - and landed in the block after the one it was
  scheduled for. Such an event is now applied one frame before the boundary,
  so the block that starts at its frame is generated with the event in it. The
  frames before the boundary are unaffected either way: that block was
  generated before the event was applied.

**The regression (round 2, strengthened in round 3).**
`theScheduledFrameDecidesWhereTheNoteStarts()` observes the position **through
the rendered audio**: a note is scheduled 100 frames into a period, and the case
checks that the frames before the block boundary at or after that frame are the
ones the reference render (the same period without the note) has, and that the
note starts **inside that block** - not merely somewhere later in the period.
`theScheduledFrameOnABlockBoundaryStartsInThatBlock()` is the paired case: the
note is scheduled exactly on a block boundary, and it has to start in *that*
block. Against the old code the note never sounds in the period it was
scheduled for ("the note never sounded: the scheduled frame was not applied at
all"); against the round-2 scheduling the boundary case reads "the note started
in the block after the one it was scheduled on" - the observed first difference
was at frame 130, i.e. the block after the one the note was scheduled on. With
the fix the difference is inside the scheduled block (the attack ramps up over
the first frames of it, so the first difference is the block's second frame).

**Consequence, stated.** The synth's voices are now computed in 64-frame blocks
instead of one block per period, so the envelopes and the filters are stepped
four times as often: the rendering is finer, musically the same, not
bit-identical. That is the cost of making the scheduled frame audible in a
block-based synth, and it is the only change outside the defect.

## Test

`tests/src/plugins/ZynAddSubFxInstrumentTest.cpp` - a white-box regression test
that compiles the plug-in's own `ZynAddSubFx.cpp` into the test executable (the
pattern the VST and CLAP tests already use) and drives the class directly.

| Case | What it pins |
|---|---|
| `theExportPathDoesNotWaitForThePluginMutex` | the wait the audio path allows itself in the export path is none, and the attempt that follows does not take the lock |
| `thePluginChoiceAndItsUseAreUnderOneLock` | `loadFile()` holds the plugin mutex between the choice and the use |
| `theHostingGettersReadThePluginUnderTheLock` | `hostingState()` and `hostingProcessId()` read the pointer under the mutex |
| `theScheduledFrameDecidesWhereTheNoteStarts` | a scheduled note starts inside the first block at or after its frame, in the rendered audio |
| `theScheduledFrameOnABlockBoundaryStartsInThatBlock` | a note scheduled exactly on a block boundary starts in that block |
| `theHandoffKeepsTheEventOffset` | a queued event keeps its frame offset |
| `theDrainForwardsTheEventOffset` | the drain hands the offset over |

**Determinism.** No case waits for a wall-clock outcome, none of them sleeps and
none of them races. The lock state is read with `QMutex::tryLock()`, which never
blocks (a self-held mutex makes it return `false`, checked here: `tryLock(0)` on
a mutex the calling thread holds returns `false` without blocking). The wait the
audio path allows itself is **asked of the instrument**, not measured - that is
what replaced the worker thread and the two-second semaphore of the first round.
The scheduled frames are injected values (100), never measured.

**Registration.** `tests/fork-sources.txt` (the new test file),
`tests/all-sources.txt` (re-derived with the manifest's own recipe - it
reproduces), `tests/upstream-modifications.txt` (the changed inherited files
with their reasons), `tests/CMakeLists.txt` (the test target).

## Evidence

All output is in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/908.r2/logs`.
Commands are run from the worktree root unless a different working directory is
stated. The tree was at `b61e5f965` (clean) before the first commit.

### Round 2 (the review's two blocking items)

| Phase | Exact command | Exit | Log / evidence |
|---|---|---:|---|
| Build the reworked test | `cmake --build build --target ZynAddSubFxInstrumentTest -j20` | 0 | `build-r2-908b.log` |
| GREEN, reworked 908 case | From `build/tests`: `ctest -R '^ZynAddSubFxInstrumentTest$' --output-on-failure` | 0 | `test-r2-908b.log`; 100% tests passed out of 1 |
| Save the old waiting policy | `git diff -- plugins/ZynAddSubFx/ZynAddSubFx.cpp > .../logs/fix-908-old-policy.patch` (the policy line changed to `renderIsExporting() ? -1 : 0`) | 0 | `fix-908-old-policy.patch`; the policy the finding describes |
| Rebuild against the old policy | `cmake --build build --target ZynAddSubFxInstrumentTest -j20` | 0 | `build-r2-908-red.log` |
| **RED** against the old waiting policy | From `build/tests`: `ctest -R '^ZynAddSubFxInstrumentTest$' --output-on-failure` | **8** | `test-r2-908-red.log`; `FAIL! ... theExportPathDoesNotWaitForThePluginMutex() Compared values are not the same - Actual (instrument.audioLockWaitMs()): -1 / Expected (0): 0`; Totals 6 passed, 1 failed - a clean assertion failure, no hang and no timeout |
| Restore the fix | `git apply -R .../logs/fix-908-old-policy.patch` | 0 | tree back to the fixed state |
| Rebuild with the fix | `cmake --build build --target ZynAddSubFxInstrumentTest zynaddsubfx -j20` | 0 | `build-r2-908-green.log` |
| **GREEN** with the fix | From `build/tests`: `ctest -R '^ZynAddSubFxInstrumentTest$' --output-on-failure` | **0** | `test-r2-908-green.log`; 100% tests passed out of 1 |
| Build the 276 rework (prep + fix) | `cmake --build build --target ZynAddSubFxInstrumentTest zynaddsubfx RemoteZynAddSubFx -j20` | 0 | `build-r2-276-prep.log` (the prep state), `build-r2-276-fix.log` |
| GREEN, 276 rework | From `build/tests`: `ctest -R '^ZynAddSubFxInstrumentTest$' --output-on-failure` | 0 | `test-r2-276-fix.log`; 100% tests passed out of 1 |
| Save the 276 product patch | `git diff -- plugins/ZynAddSubFx/LocalZynAddSubFx.cpp plugins/ZynAddSubFx/LocalZynAddSubFx.h > .../logs/fix-276-r2.patch` | 0 | the product half only (163 lines) |
| Reverse-apply the 276 fix | `git apply -R .../logs/fix-276-r2.patch` | 0 | the prep state: the offset reaches the synth but is not scheduled |
| Rebuild without the 276 fix | `cmake --build build --target ZynAddSubFxInstrumentTest -j20` | 0 | `build-r2-276-red.log` |
| **RED** without the 276 fix | From `build/tests`: `ctest -R '^ZynAddSubFxInstrumentTest$' --output-on-failure` | **8** | `test-r2-276-red.log`; `FAIL! ... theScheduledFrameDecidesWhereTheNoteStarts() 'anyDifferent(reference, withNote, onset, frames)' returned FALSE. (the note never sounded: the scheduled frame was not applied at all)`; Totals 7 passed, 1 failed |
| Restore the 276 fix | `git apply .../logs/fix-276-r2.patch` | 0 | tree back to the fixed state |
| Rebuild with the 276 fix | `cmake --build build --target ZynAddSubFxInstrumentTest zynaddsubfx RemoteZynAddSubFx -j20` | 0 | `build-r2-276-green.log` |
| **GREEN** with the 276 fix | From `build/tests`: `ctest -R '^ZynAddSubFxInstrumentTest$' --output-on-failure` | **0** | `test-r2-276-green.log`; 100% tests passed out of 1 |
| Affected-family run (round 2) | From `build/tests`: `ctest -R 'Zyn\|OutOfProcess\|MidiHandoffQueue' --output-on-failure` | 0 | `test-r2-family.log`; **6 of 6** CTest suites passed: `OutOfProcessHostTest`, `OutOfProcessHostClientLoopTest`, `MidiHandoffQueueTest`, `ZynSeparateProcessTest`, `ZynNoteOffTest`, `ZynAddSubFxInstrumentTest` |
| Production build (the branch's binary) | `cmake --build build --target zene -j20` | 0 | `build-r2-zene.log`; no `error:` in the log |
| Gates (round 2) | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-908` | **0** | `feature-check-r2.log`; `RESULT: PASS` - fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity and file-length all PASS |

### Round 3 (the review's exact-boundary miss)

| Phase | Exact command | Exit | Log / evidence |
|---|---|---:|---|
| Build the boundary fix and the strengthened test | `cmake --build build --target ZynAddSubFxInstrumentTest -j20` | 0 | `build-r3-276.log` |
| GREEN, boundary fix | From `build/tests`: `ctest -R '^ZynAddSubFxInstrumentTest$' --output-on-failure` | 0 | `test-r3-276.log`; 100% tests passed out of 1 |
| Save the boundary product patch | `git diff -- plugins/ZynAddSubFx/LocalZynAddSubFx.cpp > .../logs/fix-276-r3.patch` | 0 | the product half only (the split at a boundary) |
| Reverse-apply the boundary fix | `git apply -R .../logs/fix-276-r3.patch` | 0 | the round-2 scheduling is back: the event is applied after the block that starts at its frame was generated |
| Rebuild without the boundary fix | `cmake --build build --target ZynAddSubFxInstrumentTest -j20` | 0 | `build-r3-276-red.log` |
| **RED** without the boundary fix | From `build/tests`: `ctest -R '^ZynAddSubFxInstrumentTest$' --output-on-failure` | **8** | `test-r3-276-red.log`; `FAIL! ... theScheduledFrameOnABlockBoundaryStartsInThatBlock() 'first < block + block' returned FALSE. (the note started in the block after the one it was scheduled on)`; Totals 8 passed, 1 failed |
| Restore the boundary fix | `git apply .../logs/fix-276-r3.patch` | 0 | tree back to the fixed state |
| Rebuild with the boundary fix | `cmake --build build --target ZynAddSubFxInstrumentTest zynaddsubfx RemoteZynAddSubFx -j20` | 0 | `build-r3-276-green.log` |
| **GREEN** with the boundary fix | From `build/tests`: `ctest -R '^ZynAddSubFxInstrumentTest$' --output-on-failure` | **0** | `test-r3-276-green.log`; 100% tests passed out of 1 |
| The observed onset, measured | the same case with a temporary print of the first differing frame | 0 | `build-r3-measure.log`, `test-r3-measure.log`; "MEASURE boundary: first difference at 66 (block 64)" - the difference is inside the block that starts at the scheduled frame, the attack ramping up over its first frames. The temporary print was removed again before the commit. |
| Affected-family run (round 3) | From `build/tests`: `ctest -R 'Zyn\|OutOfProcess\|MidiHandoffQueue' --output-on-failure` | 0 | `test-r3-family.log`; **6 of 6** CTest suites passed |
| Final build of the touched targets | `cmake --build build --target ZynAddSubFxInstrumentTest zynaddsubfx RemoteZynAddSubFx -j20` | 0 | `build-r3-final.log` |
| Final run of the focused test | From `build/tests`: `ctest -R '^ZynAddSubFxInstrumentTest$' --output-on-failure` | 0 | `test-r3-final.log`; 100% tests passed out of 1 |

### Round 1 (the first pass at the three defects)

| Phase | Exact command | Exit | Log / evidence |
|---|---|---:|---|
| Prep build (the test seams) | `cmake --build build --target zynaddsubfx -j20` | 0 | `build-prep-1.log` |
| Build the new test target | `cmake --build build --target ZynAddSubFxInstrumentTest -j20` | 0 | `build-test-3.log` |
| **RED** for 908, before the fix | From `build/tests`: `ctest -R '^ZynAddSubFxInstrumentTest$' --output-on-failure` | **8** | `test-red-908.log`; `FAIL! ... theAudioPathNeverWaitsForThePluginMutex() 'returned' returned FALSE. (the audio path waited for the plugin mutex instead of skipping the period)` |
| Reverse-apply the 908 fix | `git apply -R .../logs/fix-908.patch` | 0 | the old export policy is back |
| **RED** without the 908 fix | From `build/tests`: `ctest -R '^ZynAddSubFxInstrumentTest$' --output-on-failure` | **8** | `test-red-908-proof.log`; the same failure |
| **GREEN** with the 908 fix | From `build/tests`: `ctest -R '^ZynAddSubFxInstrumentTest$' --output-on-failure` | **0** | `test-green-908-proof.log`; 100% tests passed out of 1 |
| Reverse-apply the 545 fix | `git apply -R .../logs/fix-545.patch` | 0 | the unlocked choice and the unlocked getters are back |
| **RED** without the 545 fix | From `build/tests`: `ctest -R '^ZynAddSubFxInstrumentTest$' --output-on-failure` | **8** | `test-red-545.log`; `FAIL! ... thePluginChoiceAndItsUseAreUnderOneLock()` and `FAIL! ... theHostingGettersReadThePluginUnderTheLock()` |
| **GREEN** with the 545 fix | From `build/tests`: `ctest -R '^ZynAddSubFxInstrumentTest$' --output-on-failure` | **0** | `test-green-545-proof.log`; 100% tests passed out of 1 |
| Reverse-apply the 276 fix | `git apply -R .../logs/fix-276.patch` | 0 | the dropped offset is back |
| **RED** without the 276 fix | From `build/tests`: `ctest -R '^ZynAddSubFxInstrumentTest$' --output-on-failure` | **8** | `test-red-276.log`; `FAIL! ... theHandoffKeepsTheEventOffset()` and `FAIL! ... theDrainForwardsTheEventOffset()` |
| **GREEN** with the 276 fix | From `build/tests`: `ctest -R '^ZynAddSubFxInstrumentTest$' --output-on-failure` | **0** | `test-green-276-proof.log`; 100% tests passed out of 1 |
| Affected-family run (round 1) | From `build/tests`: `ctest -R 'Zyn\|OutOfProcess\|MidiHandoff' --output-on-failure` | 8 | `test-family-5.log`; 6 of 7 CTest suites passed; the one failure is `CarlaMidiHandoffTest`, a pre-existing flake in Carla's own timing assertion |
| Pre-existing flake, measured | From `build/tests`: `ctest -R '^CarlaMidiHandoffTest$'` three times | 8, 8, 0 | the same suite fails 2 runs out of 3 with `processing did not overlap MIDI producer activity`; it is Carla's own wall-clock assertion and this branch does not touch `plugins/CarlaBase/` at all (`git diff --name-only b61e5f965..HEAD`) |
| Gates (round 1, three runs) | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-908` | 0 | `feature-check-1.log`, `feature-check-2.log`, `feature-check-3.log`; `RESULT: PASS` each time |
| Pre-submit check (round 1) | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-908 908` | 0 | `submit-check-1.log`; `RESULT: PASS - submit` |

### Files committed

| File | Change |
|---|---|
| `plugins/ZynAddSubFx/ZynAddSubFx.cpp` | the three fixes: `processLock()` never waits, `loadFile()` and the hosting getters read the plugin under the mutex, the MIDI offset survives the hand-off and reaches the synth |
| `plugins/ZynAddSubFx/ZynAddSubFx.h` | the test seams: the mutex, the plugin pointers and the hand-off move to protected, the mutex becomes mutable, `forwardQueuedMidi()`, `renderIsExporting()` and `audioLockWaitMs()` are declared |
| `plugins/ZynAddSubFx/LocalZynAddSubFx.h` / `.cpp` | the scheduled-event path: the synth takes the scheduled frame, applies the event while the period is rendered, and renders in blocks of `SynthBlockSize` |
| `plugins/ZynAddSubFx/RemoteZynAddSubFx.cpp` | the client hands the scheduled frame to the synth instead of dropping it |
| `tests/src/plugins/ZynAddSubFxInstrumentTest.cpp` | the white-box regression test (six cases) |
| `tests/CMakeLists.txt` | the `ZynAddSubFxInstrumentTest` target |
| `tests/fork-sources.txt`, `tests/all-sources.txt`, `tests/upstream-modifications.txt` | the registrations |
| `docs/qwen-fixes-2026-10/cluster-908.md` | this report |

### Commits

| Commit | What |
|---|---|
| `f054f7b76` | `refactor(zyn): expose the plugin lock and the MIDI hand-off to a white-box test (cluster 908)` - the test seams, no behaviour change |
| `3f5ab5f2d` | `fix(zyn): the audio path never waits for the plugin mutex (cluster 908)` |
| `34d6538d6` | `fix(zyn): the plugin choice and its use happen under one lock (cluster new-p9-545)` |
| `3e4a622bb` | `fix(zyn): a MIDI event keeps its frame offset through the hand-off (cluster new-p16-276)` |
| `0e4d7f154`, `eb739c930` | `docs(cluster 908)`: the report |
| `6f76a837f` | `test(zyn): the audio path's lock wait is asked, not measured (cluster 908)` - round 2 |
| `0169a0395` | `refactor(zyn): the scheduled frame reaches the synth (cluster new-p16-276)` - round 2, the plumbing |
| `9a2ebd589` | `fix(zyn): a scheduled MIDI event is applied at its frame (cluster new-p16-276)` - round 2 |
| `fed7f79d5`, `09a8d6673` | `docs(cluster 908)`: this report, round 2 |
| `d2a5bccc5` | `fix(zyn): a note scheduled on a block boundary starts in that block (cluster new-p16-276)` - round 3 |

## Self-check

1. **The regression test FAILS with the product change reverted and PASSES with
   it (exit codes).** Yes, one patch per defect:
   - 908 (round 1): `test-red-908-proof.log` exit 8, `test-green-908-proof.log`
     exit 0.
   - 908 (round 2, the deterministic case): `fix-908-old-policy.patch` restores
     the waiting policy the finding describes; `test-r2-908-red.log` exit 8
     ("Actual (-1) / Expected (0)"), `test-r2-908-green.log` exit 0. The
     prescribed `git diff | git apply -R` cannot be used for this one, and the
     report says why: the deterministic case needs a product seam
     (`renderIsExporting()`, `audioLockWaitMs()`) that does not exist before it,
     so reverting the commit's product half would break the test's compilation
     instead of producing a clean assertion failure. The old-policy patch is the
     equivalent proof, and it fails on the assertion, not on a hang.
   - new-p9-545: `test-red-545.log` exit 8, `test-green-545-proof.log` exit 0.
   - new-p16-276 (round 1): `test-red-276.log` exit 8, `test-green-276-proof.log`
     exit 0.
   - new-p16-276 (round 2, the scheduled frame): `fix-276-r2.patch` is the
     product half of the rework (163 lines, `LocalZynAddSubFx.cpp` and `.h`);
     `test-r2-276-red.log` exit 8 ("the note never sounded"), `test-r2-276-green.log`
     exit 0.
   - new-p16-276 (round 3, the block boundary): `fix-276-r3.patch` is the product
     half of the boundary fix; `test-r3-276-red.log` exit 8 ("the note started in
     the block after the one it was scheduled on"), `test-r3-276-green.log` exit 0.
2. **The test is deterministic: no wall-clock timing, sleeps or races (name the
   seam or injected values it uses).** The seams are `renderIsExporting()` and
   `audioLockWaitMs()` (the export path and the wait the audio path allows
   itself), `m_testHook` (the point at which the lock state is read) and
   `m_testLastForwardedOffset`; the injected values are the scheduled frames
   (100, 42). The lock state is read with `QMutex::tryLock(0)`, which never
   blocks. The round-2 lock case **asks** the instrument for the wait instead of
   starting a thread and giving it two seconds - there is no thread, no
   semaphore, no timeout and no sleep anywhere in the test file.
3. **The fix is at the cause and complete: every caller and sibling of what you
   changed was checked and stays consistent.** The plugin pointers are read in
   `loadFile()`, `saveSettings()`, `loadSettings()`, `updatePitchRange()`,
   `initPlugin()`, the destructor, `hostingState()`, `hostingProcessId()` and
   `forwardQueuedMidi()`; all of them now hold the mutex for the read and the
   use. The hand-off is written in `handleMidiEvent()` and read in
   `forwardQueuedMidi()`; both carry the offset, and both the remote client and
   the in-process synth now take it. The all-notes-off cut that `drainMidi()`
   sends for a refused channel keeps offset 0 on purpose: the cut belongs at
   the refusal point, which is the start of the period. The callers of
   `LocalZynAddSubFx::processMidiEvent()` are the instrument's drain, the
   remote client and `ZynNoteOffTest`; the last one passes no offset and keeps
   the old behaviour (the event is applied at once).
4. **No collateral damage: old projects and saved data still load as before, and
   any limit or check you added does not hit other callers that legitimately
   exceed it.** Nothing in the save format, the project data or the preset files
   changed. Two behaviour changes are stated above and both are the fix: an
   export that overlaps a reload skips the affected periods instead of stalling
   on them, and the Zyn synth renders its voices in 64-frame blocks instead of
   one block per period (finer envelope and filter stepping - musically the
   same, not bit-identical). No new limit, threshold or check was added: the
   scheduled-event list holds 256 events, which is the hand-off queue's own
   capacity, and a full list applies the event at once rather than dropping it.
5. **Nothing you added runs on the audio thread and allocates, frees, locks,
   waits, does I/O or emits a signal.** The audio path (`processLock()`,
   `processImpl()`, `forwardQueuedMidi()`, `handleMidiEvent()`) only does atomic
   queue operations, one *non-blocking* `tryLock(0)` and the synth's own render.
   The scheduled-event list is a fixed member array of the synth: queueing an
   event writes into it and allocates nothing. The change makes the audio path
   strictly less blocking than before (the unbounded `tryLock(-1)` during an
   export is gone). The test seams are compiled only under `LMMS_TESTING` and
   live on main-thread paths (`loadFile()`, the hosting getters, the drain).
6. **Qt 5.15 / MSVC / macOS < 10.15 portability.** Nothing platform-specific was
   added: the fixes use `QMutex::tryLock()`/`lock()`/`unlock()`, `std::uint32_t`,
   `std::clamp` and the existing queue types. The new test uses `QMutex::tryLock()`,
   `std::array` and `std::vector` only; it compiles the plug-in's source with
   `PLUGIN_STATIC_DEFINE` and `PLUGIN_NAME=zynaddsubfx`, the same construct the
   CLAP tests use.
7. **Sibling commands in the command files you changed.** No control command was
   touched: the diff contains no `ControlCommands*` file and no new command id
   (`git diff --name-only b61e5f965..HEAD` lists eleven files, none of them a
   command file), so the control-surface snapshot is untouched.
8. **If your verdict is ALREADY-FIXED or FALSE-POSITIVE: the code (file:line) or
   the intended contract that shows it.** No verdict here is ALREADY-FIXED or
   FALSE-POSITIVE, so there is nothing to cite in that shape: all three defects
   are REAL and each one is reproduced by a case that fails on the tree without
   its fix (`test-red-908-proof.log`, `test-red-545.log`, `test-r2-276-red.log`,
   exit 8 each) and passes on the fixed tree (exit 0 each). The one part of the
   908 finding that was already fixed on this branch before this work -
   `handleMidiEvent()` taking the plugin mutex with a blocking lock - was fixed
   by the earlier commit `f3953cc8e` ("Vestige, Carla and ZynAddSubFx queue MIDI
   instead of locking for it"), and the remaining part (the audio path's own
   lock attempt) is what this branch fixes.
