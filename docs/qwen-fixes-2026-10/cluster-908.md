# Cluster 908 (ZynAddSubFx): the plugin mutex, the MIDI hand-off and the hosting getters

Verdict: **FIXED**

908: FIXED
new-p9-545: FIXED
new-p16-276: FIXED

All three defects live in `plugins/ZynAddSubFx/ZynAddSubFx.cpp` and are fixed
there, each with its own regression case in the new white-box test
`tests/src/plugins/ZynAddSubFxInstrumentTest.cpp` (which compiles the plug-in's
own `ZynAddSubFx.cpp`, the way the VST and CLAP tests compile theirs).

| Cluster | Verdict | Where the defect was | Fix |
|---|---|---|---|
| 908 | FIXED | `plugins/ZynAddSubFx/ZynAddSubFx.cpp:409-421` (`processLock()`) | the audio path's lock attempt is a try, never a wait |
| new-p9-545 | FIXED | `plugins/ZynAddSubFx/ZynAddSubFx.cpp:314-336` (`loadFile()`), `:464-499` (`hostingState()`, `hostingProcessId()`) | the choice of a plugin and its use, and the getters' read of the pointer, happen under the plugin mutex |
| new-p16-276 | FIXED | `plugins/ZynAddSubFx/ZynAddSubFx.cpp:455` (`handleMidiEvent()`), `:393` (`forwardQueuedMidi()`) | the event's frame offset travels through the hand-off and is handed to the remote client |

## 908 — the audio path never waits for the plugin mutex

**Defect.** `processLock()` was `m_pluginMutex.tryLock(Engine::getSong()->isExporting() ? -1 : 0)`:
while the song was exporting, the audio thread **waited** for the plugin mutex.
The main thread holds that mutex for as long as a plugin operation takes - the
client handshake in `initPlugin()` is seconds, and a preset load waits for the
client to answer - so a render could stall for the whole reload, and for as long
as the main thread needed the lock, which was unbounded.

**Fix** (`plugins/ZynAddSubFx/ZynAddSubFx.cpp:409-421`): the lock attempt is now
`m_pluginMutex.tryLock(0)` on the audio path as well. A period that cannot take
the lock is skipped - the way the live path has always skipped it - and the
render keeps its pace instead of stalling.

**Trade-off, stated.** An export that overlaps a reload now skips the affected
periods instead of stalling on them: the audio is missing rather than late.
That is the point of the fix (the wait was unbounded), and it is the only
behaviour change outside the defect.

**Evidence.** `plugins/ZynAddSubFx/ZynAddSubFx.cpp:421` (the `tryLock(0)`),
`tests/src/plugins/ZynAddSubFxInstrumentTest.cpp:115-165`
(`theAudioPathNeverWaitsForThePluginMutex`).

## new-p9-545 — the plugin choice and its use happen under one lock

**Defect.** `loadFile()` read `m_remotePlugin` **without** the mutex and took it
only for the local branch; `hostingState()` and `hostingProcessId()` read the
pointer with no lock at all. A hosting-mode switch - the instrument view's
checkbox and the control surface's `oop.set_mode` both end in `reloadPlugin()` -
deletes the running instance and creates another one under that very mutex, so a
caller that chose an instance without the lock could use it after it was gone:
the remote branch would talk to a freed client, and the local branch would load
the preset into a null pointer.

**Fix** (`plugins/ZynAddSubFx/ZynAddSubFx.cpp:314-336`, `:464-499`): the choice
and the use are under the plugin mutex in `loadFile()`, and both hosting getters
read the plugin pointer under the mutex.

**Siblings checked.** Every other user of the plugin pointers holds the same
mutex for the whole use: `saveSettings()` (`:201-213`), `loadSettings()`
(`:272-284`), `updatePitchRange()` (`:555-565`), `initPlugin()` (`:590-647`) and
the destructor (`:160-165`). `handleMidiEvent()` does not touch the pointers at
all - it only queues the event, and `forwardQueuedMidi()` runs under
`processLock()`, i.e. under the mutex.

**Trade-off, stated.** The hosting getters now wait for a reload that is in
progress (the client handshake) instead of answering from a pointer that is
being replaced. They answer correctly now; before, they could read a freed
pointer.

## new-p16-276 — a MIDI event keeps its frame offset through the hand-off

**Defect.** `handleMidiEvent()` received the frame offset the event was
scheduled at and stored `queued.offset = 0`, and the drain handed the remote
client offset 0 as well. Every event was therefore applied at the start of the
period instead of at its scheduled position - the note and controller timing of
the Zyn instrument was off by up to a whole period.

**Fix** (`plugins/ZynAddSubFx/ZynAddSubFx.cpp:455`, `:393`): the offset travels
with the event through the queue, and the drain hands it to the remote client,
which places the event inside the period it renders. This is the same convention
the sibling instruments use (`plugins/Vestige/Vestige.cpp:459` keeps the
offset and hands it to the plugin, `plugins/CarlaBase/CarlaMidiHandoff.h:110`
keeps it in the native event's time field).

**Limit, stated.** The in-process synth still applies the event at the start of
the period it renders: `LocalZynAddSubFx::processMidiEvent()`
(`plugins/ZynAddSubFx/LocalZynAddSubFx.h:63`) takes an event, not a position, so
the synth has no sub-period placement to offer. The hand-off no longer loses the
information; what the in-process synth can do with it did not change.

## Test

`tests/src/plugins/ZynAddSubFxInstrumentTest.cpp` - a new white-box regression
test that compiles the plug-in's own `ZynAddSubFx.cpp` into the test executable
(the pattern the VST and CLAP tests already use) and drives the class directly.
Six cases, one per defect and its proof:

| Case | What it pins |
|---|---|
| `theAudioPathNeverWaitsForThePluginMutex` | the audio path's lock attempt returns without waiting and does not take the lock |
| `thePluginChoiceAndItsUseAreUnderOneLock` | `loadFile()` holds the plugin mutex between the choice and the use |
| `theHostingGettersReadThePluginUnderTheLock` | `hostingState()` and `hostingProcessId()` read the pointer under the mutex |
| `theHandoffKeepsTheEventOffset` | a queued event keeps its frame offset |
| `theDrainForwardsTheEventOffset` | the drain hands the offset over |

**Determinism.** No case waits for a wall-clock outcome and none of them races.
The lock state is read with `QMutex::tryLock()`, which never blocks (a
self-held mutex makes it return `false`, checked here: `tryLock(0)` on a mutex
the calling thread holds returns `false` without blocking). The one place where
a thread has to be waited for - the audio thread's lock attempt - is waited for
with a bounded `QSemaphore::tryAcquire(1, 2000)`, whose only job is to turn a
hang into a failure; the assertion itself is the value the audio path returns.
The offsets are injected values (42), never measured.

**Registration.** `tests/fork-sources.txt` (the new test file),
`tests/all-sources.txt` (re-derived with the manifest's own recipe - it
reproduces), `tests/upstream-modifications.txt` (the changed inherited files
with their reasons), `tests/CMakeLists.txt` (the test target).

## Evidence

All output is in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/908.r2/logs`.
Commands are run from the worktree root unless a different working directory is
stated. The tree was at `b61e5f965` (clean) before the first commit.

| Phase | Exact command | Exit | Log / evidence |
|---|---|---:|---|
| Prep build (the test seams) | `cmake --build build --target zynaddsubfx -j20` | 0 | `build-prep-1.log` |
| Build the new test target | `cmake --build build --target ZynAddSubFxInstrumentTest -j20` | 0 | `build-test-3.log` |
| **RED** for 908, before the fix | From `build/tests`: `ctest -R '^ZynAddSubFxInstrumentTest$' --output-on-failure` | **8** | `test-red-908.log`; `FAIL! ... theAudioPathNeverWaitsForThePluginMutex() 'returned' returned FALSE. (the audio path waited for the plugin mutex instead of skipping the period)`; Totals 2 passed, 1 failed |
| GREEN for 908 | From `build/tests`: `ctest -R '^ZynAddSubFxInstrumentTest$' --output-on-failure` | 0 | `test-green-908.log`; 100% tests passed out of 1 |
| Save the 908 product patch | `git diff -- plugins/ZynAddSubFx/ZynAddSubFx.cpp > .../logs/fix-908.patch` | 0 | the product half only (28 lines) |
| Reverse-apply the 908 fix | `git apply -R .../logs/fix-908.patch` | 0 | the old export policy is back |
| Rebuild without the 908 fix | `cmake --build build --target ZynAddSubFxInstrumentTest -j20` | 0 | `build-red-908.log` |
| **RED** without the 908 fix | From `build/tests`: `ctest -R '^ZynAddSubFxInstrumentTest$' --output-on-failure` | **8** | `test-red-908-proof.log`; the same failure; Totals 2 passed, 1 failed |
| Restore the 908 fix | `git apply .../logs/fix-908.patch` | 0 | tree back to the fixed state |
| Rebuild with the 908 fix | `cmake --build build --target ZynAddSubFxInstrumentTest zynaddsubfx -j20` | 0 | `build-green-908-proof.log` |
| **GREEN** with the 908 fix | From `build/tests`: `ctest -R '^ZynAddSubFxInstrumentTest$' --output-on-failure` | **0** | `test-green-908-proof.log`; 100% tests passed out of 1 |
| Save the 545 product patch | `git diff -- plugins/ZynAddSubFx/ZynAddSubFx.cpp > .../logs/fix-545.patch` | 0 | the product half only (74 lines) |
| Reverse-apply the 545 fix | `git apply -R .../logs/fix-545.patch` | 0 | the unlocked choice and the unlocked getters are back |
| Rebuild without the 545 fix | `cmake --build build --target ZynAddSubFxInstrumentTest -j20` | 0 | `build-red-545.log` |
| **RED** without the 545 fix | From `build/tests`: `ctest -R '^ZynAddSubFxInstrumentTest$' --output-on-failure` | **8** | `test-red-545.log`; `FAIL! ... thePluginChoiceAndItsUseAreUnderOneLock() 'lockHeldAtTheUse' returned FALSE` and `FAIL! ... theHostingGettersReadThePluginUnderTheLock() 'lockHeldAtTheRead' returned FALSE`; Totals 3 passed, 2 failed |
| Restore the 545 fix | `git apply .../logs/fix-545.patch` | 0 | tree back to the fixed state |
| Rebuild with the 545 fix | `cmake --build build --target ZynAddSubFxInstrumentTest zynaddsubfx -j20` | 0 | `build-green-545-proof.log` |
| **GREEN** with the 545 fix | From `build/tests`: `ctest -R '^ZynAddSubFxInstrumentTest$' --output-on-failure` | **0** | `test-green-545-proof.log`; 100% tests passed out of 1 |
| Save the 276 product patch | `git diff -- plugins/ZynAddSubFx/ZynAddSubFx.cpp > .../logs/fix-276.patch` | 0 | the product half only (27 lines) |
| Reverse-apply the 276 fix | `git apply -R .../logs/fix-276.patch` | 0 | the dropped offset is back |
| Rebuild without the 276 fix | `cmake --build build --target ZynAddSubFxInstrumentTest -j20` | 0 | `build-red-276.log` |
| **RED** without the 276 fix | From `build/tests`: `ctest -R '^ZynAddSubFxInstrumentTest$' --output-on-failure` | **8** | `test-red-276.log`; `FAIL! ... theHandoffKeepsTheEventOffset() Compared values are not the same` and `FAIL! ... theDrainForwardsTheEventOffset() Compared values are not the same`; Totals 5 passed, 2 failed |
| Restore the 276 fix | `git apply .../logs/fix-276.patch` | 0 | tree back to the fixed state |
| Rebuild with the 276 fix | `cmake --build build --target ZynAddSubFxInstrumentTest zynaddsubfx -j20` | 0 | `build-green-276-proof.log` |
| **GREEN** with the 276 fix | From `build/tests`: `ctest -R '^ZynAddSubFxInstrumentTest$' --output-on-failure` | **0** | `test-green-276-proof.log`; 100% tests passed out of 1 |
| Affected-family run (after all three fixes) | From `build/tests`: `ctest -R 'Zyn\|OutOfProcess\|MidiHandoff' --output-on-failure` | 8 | `test-family-5.log`; **6 of 7** CTest suites passed: `MidiHandoffQueueTest`, `ZynSeparateProcessTest`, `ZynNoteOffTest`, `ZynAddSubFxInstrumentTest` and two out-of-process suites passed; the one failure is `CarlaMidiHandoffTest`, a pre-existing flake in Carla's own timing assertion (see the note below) |
| Pre-existing flake, measured | From `build/tests`: `ctest -R '^CarlaMidiHandoffTest$'` three times | 8, 8, 0 | the same suite fails 2 runs out of 3 with `processing did not overlap MIDI producer activity`; it is Carla's own wall-clock assertion and this branch does not touch `plugins/CarlaBase/` at all (`git diff --name-only b61e5f965..HEAD`) |
| Gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-908` | **0** | `feature-check-1.log`; `RESULT: PASS` - fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity and file-length all PASS |
| Gates (second run, after the report) | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-908` | 0 | `feature-check-2.log` |

### Files committed

| File | Change |
|---|---|
| `plugins/ZynAddSubFx/ZynAddSubFx.cpp` | the three fixes: `processLock()` never waits, `loadFile()` and the hosting getters read the plugin under the mutex, the MIDI offset survives the hand-off |
| `plugins/ZynAddSubFx/ZynAddSubFx.h` | the test seams: the mutex, the plugin pointers and the hand-off move to protected, the mutex becomes mutable, `forwardQueuedMidi()` is declared |
| `tests/src/plugins/ZynAddSubFxInstrumentTest.cpp` | the new white-box regression test (six cases) |
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

## Self-check

1. **The regression test FAILS with the product change reverted and PASSES with
   it (exit codes).** Yes, one patch per defect, each reverted on its own:
   908 -> `test-red-908-proof.log` exit 8, `test-green-908-proof.log` exit 0;
   new-p9-545 -> `test-red-545.log` exit 8, `test-green-545-proof.log` exit 0;
   new-p16-276 -> `test-red-276.log` exit 8, `test-green-276-proof.log` exit 0.
   The patches are the product half only (`fix-908.patch` 28 lines,
   `fix-545.patch` 74 lines, `fix-276.patch` 27 lines - all
   `plugins/ZynAddSubFx/ZynAddSubFx.cpp`), the test file and the CMake
   registration stay in place across the revert, so the reverted tree still
   builds and the failure is the assertion, not a build error.
2. **The test is deterministic: no wall-clock timing, sleeps or races (name the
   seam or injected values it uses).** The seams are `m_testForceExport` (the
   export policy of the lock), `m_testHook` (the point at which the lock state
   is read) and `m_testLastForwardedOffset` (the offset the drain handed over);
   the injected value is the frame offset 42. The lock state is read with
   `QMutex::tryLock(0)`, which never blocks; the only wait is the bounded
   `QSemaphore::tryAcquire(1, 2000)` that turns a hang into a failure, and the
   assertion itself is the returned value, not a duration.
3. **The fix is at the cause and complete: every caller and sibling of what you
   changed was checked and stays consistent.** The plugin pointers are read in
   `loadFile()`, `saveSettings()`, `loadSettings()`, `updatePitchRange()`,
   `initPlugin()`, the destructor, `hostingState()`, `hostingProcessId()` and
   `forwardQueuedMidi()`; all of them now hold the mutex for the read and the
   use. The hand-off is written in `handleMidiEvent()` and read in
   `forwardQueuedMidi()`; both carry the offset. The all-notes-off cut that
   `drainMidi()` sends for a refused channel keeps offset 0 on purpose: the cut
   belongs at the refusal point, which is the start of the period.
4. **No collateral damage: old projects and saved data still load as before, and
   any limit or check you added does not hit other callers that legitimately
   exceed it.** Nothing in the save format, the project data or the preset
   files changed; the fixes touch the runtime locking and the MIDI timing only.
   The one behaviour change is stated above: an export that overlaps a reload
   skips the affected periods instead of stalling on them, and the hosting
   getters wait for a reload that is in progress instead of reading a pointer
   that is being replaced. No new limit, threshold or check was added.
5. **Nothing you added runs on the audio thread and allocates, frees, locks,
   waits, does I/O or emits a signal.** The audio path (`processLock()`,
   `processImpl()`, `forwardQueuedMidi()`, `handleMidiEvent()`) only does atomic
   queue operations and one *non-blocking* `tryLock(0)`. The change makes the
   audio path strictly less blocking than before (the unbounded `tryLock(-1)`
   during an export is gone). The test seams are compiled only under
   `LMMS_TESTING` and live on main-thread paths (`loadFile()`, the hosting
   getters, the drain). No allocation, no free, no I/O and no signal was added
   to the audio path.
6. **Qt 5.15 / MSVC / macOS < 10.15 portability.** Nothing platform-specific was
   added: the fixes use `QMutex::tryLock()`/`lock()`/`unlock()` (Qt 5 and Qt 6
   alike), `std::uint32_t` and the existing queue types. The new test uses
   `QSemaphore`, `QThread`-free `std::thread` and `QMutex::tryLock()`, all
   portable; it compiles the plug-in's source with `PLUGIN_STATIC_DEFINE` and
   `PLUGIN_NAME=zynaddsubfx`, the same construct the CLAP tests use.
7. **Sibling commands in the command files you changed.** No control command was
   touched: the diff contains no `ControlCommands*` file and no new command id
   (`git diff --name-only b61e5f965..HEAD` lists seven files, none of them a
   command file), so the control-surface snapshot is untouched.
