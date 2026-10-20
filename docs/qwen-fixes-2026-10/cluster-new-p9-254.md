new-p9-254: FIXED
new-p14-134: FIXED

# Cluster new-p9-254 / new-p14-134

**Verdict: FIXED** (both reports name one defect, so one fix answers both)

## The shared cause

Both findings point at the same line of the same function: the **LB302 note
enqueue failsafe**.

- `new-p9-254` names `plugins/Lb302/Lb302.cpp:511` and says: "`playNoteImpl()`
  calls `qDebug()` when bounded enqueue retries are exhausted. Logging can
  allocate or lock, which violates the real-time rule for this play-handle
  path."
- `new-p14-134` names `plugins/Lb302/Lb302.cpp:~526` and says the same thing
  about "the bounded note-enqueue path". The word "LADSPA" in its title is
  the patrol's mislabel: `plugins/Lb302/` contains no LADSPA code at all
  (`rg -i ladspa plugins/Lb302/` returns nothing), and the file holds exactly
  one logging call - the `qDebug()` on the note path.

There is one defect, one line, one function. It was fixed once, at the cause.

Each finding has its **own** regression (round 1 was sent back because the
second finding shared the first one's test):

| finding | its own regression | where |
| --- | --- | --- |
| `new-p9-254` (the note path must not log or allocate on the render thread) | `PluginPortsMigrationTest::lb302NoteEnqueueFailsafeStaysRealtimeSafe` | `tests/src/plugins/PluginPortsMigrationTest.cpp:2306` |
| `new-p14-134` (the retry exhaustion of the note path must not log) | `Lb302NotePathTest::lb302RetryExhaustionDoesNotLog` (and its sibling `lb302RetryExhaustionIsCountedAndReportedOffTheAudioThread`) | `tests/src/plugins/Lb302NotePathTest.cpp` |

`Lb302NotePathTest` is its own CTest target: the plugin's own sources are
compiled into that test binary (the `SlicerTZoomTest` /
`Vst3EffectIntegrationTest` pattern, `tests/CMakeLists.txt`), so the code under
test is the shipped one, the test loads no plugin module and needs **no
platform skip** - it runs everywhere - and it can be run on its own:
`ctest -R '^Lb302NotePathTest$'` is EXIT 1 with the product change reversed
and EXIT 0 with the fix.

`Lb302Synth::playNoteImpl` is the instrument's per-note callback. It is
reached from the render thread: `AudioEngine::renderStageInstruments()`
(`src/core/AudioEngine.cpp:836`) runs every play handle on the audio thread,
`NotePlayHandle::play()` (`src/core/NotePlayHandle.cpp:225`) calls
`m_instrumentTrack->playNote(this, buffer)` (`:307`), and
`Instrument::playNote()` (`include/Instrument.h:108`) calls `playNoteImpl()`.
So the `qDebug()` ran on the audio thread, where logging formats a message
(it allocates) and takes Qt's logging lock - both forbidden by AGENTS.md
rule 4.

## The fix

The failsafe no longer logs on the audio thread. It counts the drop in a
lock-free counter, and the control thread reports it.

- `plugins/Lb302/Lb302.cpp:524` - the failsafe now does
  `m_droppedNoteCount.fetch_add(1, std::memory_order_relaxed);` and returns.
  No allocation, no lock, no logging on the render thread.
- `plugins/Lb302/Lb302.h:331` - the counter: `std::atomic_size_t
  m_droppedNoteCount {0};`.
- `plugins/Lb302/Lb302.cpp:558` - `Lb302Synth::droppedNoteCount()`, a relaxed
  read of the counter (control thread).
- `plugins/Lb302/Lb302.cpp:564` - `Lb302Synth::reportDroppedNotes()`: the
  control-thread report. It refuses to run on the render thread
  (`Engine::audioEngine()->isRenderThread()`), drains the counter with one
  `exchange(0)` and logs the count with `qWarning()` there.
- `plugins/Lb302/Lb302.cpp:250` - the constructor starts a 1 s `QTimer` that
  is a child of the instrument, so it fires in the instrument's own
  (control) thread and calls `reportDroppedNotes()`.
- `plugins/Lb302/Lb302.h:167` and `:174` - the two accessors are public slots,
  so the control side (and the test) can reach them through the meta-object.

The audio output is unchanged: the counter is not read by the DSP, and the
failsafe still drops the note exactly as before.

## The guard (the same defect cannot come back unnoticed)

The rt-safety whole-tree sweep now measures this path, and it knows what
"logging on an audio thread" looks like:

- `tests/rt_safety_source.py` - a new rule `qt-log` (category `blocking-io`):
  `qDebug`/`qInfo`/`qWarning`/`qCritical` on a declared audio-thread path.
  `qFatal` is deliberately not covered - it aborts the process instead of
  logging and continuing, which is another hazard. The rule bites nothing
  else in the tree: the sweep measured 0 fresh hits with the rule added.
- `tests/rt-safety-scope.txt:96` - `plugins/Lb302/Lb302.cpp` /
  `Lb302Synth::playNoteImpl` is now a declared audio-thread path, with the
  reason that names the path.
- `tests/rt_safety_selftest.py` - a positive control for the new rule
  (`a deliberate qDebug on an audio-thread path FAILS`), so a rule that
  stops biting is caught by the sweep's own controls.
- `docs/RT-SAFETY-SWEEP.md` - the rule and its bound are documented.

## Evidence

- `plugins/Lb302/Lb302.cpp:512` (before the fix) - the offending call:
  `qDebug() << "Lb302: Note dropped due to catastrophically poor performance!
  This should never happen!";` inside `Lb302Synth::playNoteImpl`, the
  per-note callback of the audio render thread.
- `plugins/Lb302/Lb302.cpp:524` (after the fix) - the same branch counts the
  drop instead of logging.
- `plugins/Lb302/Lb302.cpp:564` - the control-thread report.
- `tests/src/plugins/PluginPortsMigrationTest.cpp:2306` - the regression
  `PluginPortsMigrationTest::lb302NoteEnqueueFailsafeStaysRealtimeSafe()`.
  It drives the failsafe deterministically (no timing, no sleeps): the
  instrument's note ring buffer is filled with 512 enqueues - far more than
  the capacity the instrument declares - so the enqueues past the capacity
  find the buffer full, burn their retry budget and take the failsafe. It
  then proves (a) the note path logged nothing, (b) the note path allocated
  and freed nothing (`partc::RealtimeAllocationWatch`, the harness' own
  allocation seam), (c) the drop was recorded in the counter, and (d) the
  report happens on the control thread, names the count and drains it.
- `tests/src/plugins/PluginPortsMigrationTest.cpp:167` - `LogCapture`, the
  message seam: it counts the messages of the calling thread only (thread
  local, so the render thread's own messages are neither recorded nor raced
  on) and keeps them out of the test output.
- `tests/src/plugins/Lb302NotePathTest.cpp` - the **new-p14-134** regression
  (round 2). It is a CTest target of its own (`Lb302NotePathTest`): the
  plugin's sources are compiled into it, so it loads no plugin module and
  needs no platform skip. Its two slots are
  `lb302RetryExhaustionDoesNotLog()` (the finding's own claim: the retry
  exhaustion of the note path logs nothing, and allocates nothing) and
  `lb302RetryExhaustionIsCountedAndReportedOffTheAudioThread()` (the drop is
  counted, and the report names the count and drains it). The failsafe is
  reached by structure: 512 enqueues against a ring buffer that holds far
  fewer, so 384 of them take the failsafe on every run.
- `tests/CMakeLists.txt` - registers the `Lb302NotePathTest` target
  (the `SlicerTZoomTest` / `Vst3EffectIntegrationTest` pattern: the plugin's
  own sources are compiled into the test binary).
- `tests/all-sources.txt:2928` and `tests/fork-sources.txt` - the new test
  source is registered in both scope manifests; `bash
  tests/all-sources-reproduce.sh` prints `REPRODUCES` again.
- `tests/upstream-modifications.txt:2410-2411` - the two inherited product
  files are declared in the divergence ledger with their reasons.

The suggested `ctest -R '^Lb302$'` selector has no registered test in this
configured tree (`ctest -N -R '^Lb302$'` reports `Total Tests: 0`); the
`Lb302` slice is covered by `PluginPortsMigrationTest` and by the new
`Lb302NotePathTest`, the two registered tests used here.

### One pre-existing failure, not caused by this change

`PluginPortsMigrationTest::nonRealtimeLadspaEffectsAreInvalid()` fails in
this environment with "SWH Decimator test fixture was not loaded" - the
external LADSPA plugin `decimator_1202.so` is not installed here. It fails
identically on the untouched tree: the baseline run below (pristine test
file, pristine product code) reports `44 passed, 1 failed, 2 skipped`.

## RED/GREEN and revert proof

All commands ran in the worktree; ctest ran from `build/tests`. Build and
test output is in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-254.r1/logs`.

| Phase | Exact command | Exit | Result |
| --- | --- | ---: | --- |
| Selector check (from `build/tests`) | `ctest -N -R '^Lb302$' > .../logs/test-lb302-selector.log 2>&1; echo EXIT=$?` | 0 | `Total Tests: 0` - there is no `Lb302` ctest in this tree, so the registered `PluginPortsMigrationTest` is used. |
| RED build (regression added, product code untouched) | `cmake --build build --target PluginPortsMigrationTest -- -j12 > .../logs/build-red-2.log 2>&1; echo EXIT=$?` | 0 | Test target rebuilt. |
| **RED** | `QT_QPA_PLATFORM=offscreen ctest -R '^PluginPortsMigrationTest$' --output-on-failure > .../logs/test-red-2.log 2>&1; echo EXIT=$?` | 8 | `FAIL! ... 'logged == 0' returned FALSE. (the LB302 note path logged 384 message(s) from the audio-thread callback, first: Lb302: Note dropped due to catastrophically poor performance! This should never happen!)`; QtTest totals: 44 passed, 2 failed, 2 skipped (the second failure is the pre-existing LADSPA fixture). |
| Baseline check (pristine test file, pristine product code) | `git show HEAD:tests/src/plugins/PluginPortsMigrationTest.cpp > tests/src/plugins/PluginPortsMigrationTest.cpp && cmake --build build --target PluginPortsMigrationTest -- -j12` then `ctest -R '^PluginPortsMigrationTest$'` (`.../logs/test-baseline-check.log`) | 8 | 44 passed, **1** failed, 2 skipped - the LADSPA fixture failure alone. Evidence that the second failure is pre-existing. |
| GREEN build | `cmake --build build --target lb302 PluginPortsMigrationTest -- -j12 > .../logs/build-green-1.log 2>&1; echo EXIT=$?` | 0 | Plugin and test rebuilt with the fix. |
| **GREEN** | `QT_QPA_PLATFORM=offscreen ctest -R '^PluginPortsMigrationTest$' --output-on-failure > .../logs/test-green-1.log 2>&1; echo EXIT=$?` | 8 | `PASS : PluginPortsMigrationTest::lb302NoteEnqueueFailsafeStaysRealtimeSafe()`; totals: 45 passed, **1** failed (the pre-existing LADSPA fixture), 2 skipped. |
| Save the product-only patch | `git diff -- plugins/Lb302/Lb302.cpp plugins/Lb302/Lb302.h > .../logs/fix.patch` | 0 | The patch holds the product change only (the test file is not in it). |
| Reverse the product fix | `git apply -R .../logs/fix.patch` | 0 | The product code is back to the `qDebug()` version. |
| Revert-proof build | `cmake --build build --target lb302 PluginPortsMigrationTest -- -j12 > .../logs/build-final-proof-red.log 2>&1; echo EXIT=$?` | 0 | Rebuilt without the fix. |
| **Revert-proof RED** | `QT_QPA_PLATFORM=offscreen ctest -R '^PluginPortsMigrationTest$' --output-on-failure > .../logs/test-final-proof-red.log 2>&1; echo EXIT=$?` | 8 | The same assertion fails again: "the LB302 note path logged 384 message(s) ..."; totals: 44 passed, 2 failed, 2 skipped. |
| Restore the product fix | `git apply .../logs/fix.patch` | 0 | The fix is back. |
| Revert-proof build | `cmake --build build --target zene lb302 PluginPortsMigrationTest -- -j12 > .../logs/build-final-proof-green.log 2>&1; echo EXIT=$?` | 0 | Rebuilt with the fix. |
| **Revert-proof GREEN** | `QT_QPA_PLATFORM=offscreen ctest -R '^PluginPortsMigrationTest$|^RtSafety(Sweep\|SelfTest)$' --output-on-failure > .../logs/test-final-proof-green.log 2>&1; echo EXIT=$?` | 8 | `PASS : ...lb302NoteEnqueueFailsafeStaysRealtimeSafe()`; totals: 45 passed, 1 failed (pre-existing), 2 skipped; the two rt-safety tests pass (2/3 targets, the third being the pre-existing LADSPA failure). |
| Final sweep | `python3 tests/rt-safety-sweep.py > .../logs/rt-sweep-final.log 2>&1; echo EXIT=$?` | 0 | 72 declared pairs, 4 hits in 3 allowlisted keys, 0 fresh keys. |

The affected CTest family (`PluginPortsMigrationTest`) runs **1** test
executable with 48 test functions (45 passed, 1 pre-existing failure, 2
skipped) on every fixed-code run.

### Round 2: the new-p14-134 regression on its own

Round 1 was sent back because both findings shared one test. The second
finding now has its own test, in its own CTest target, proved the same way
(product change reversed -> the test fails; product change restored -> it
passes). The product patch is the same `.../logs/fix.patch`: it holds the two
product files only, built from the base commit
`git diff $(git merge-base HEAD $(cat .../FIX-BASE))..HEAD -- plugins/Lb302/Lb302.cpp plugins/Lb302/Lb302.h`.

| Phase | Exact command | Exit | Result |
| --- | --- | ---: | --- |
| Register the new target | `cmake --build build --target lb302 -- -j12 > .../logs/build-reconfigure-1.log 2>&1; echo EXIT=$?` | 0 | The build system re-ran CMake, so the new `Lb302NotePathTest` target exists. |
| Build the new test | `cmake --build build --target Lb302NotePathTest -- -j12 > .../logs/build-note-path-2.log 2>&1; echo EXIT=$?` | 0 | The plugin's sources compile into the test binary. |
| **GREEN (new-p14-134)** | `QT_QPA_PLATFORM=offscreen ctest -R '^Lb302NotePathTest$' --output-on-failure > .../logs/test-note-path-green-2.log 2>&1; echo EXIT=$?` | 0 | 1/1 passed. |
| Reverse the product fix | `git apply -R .../logs/fix.patch` | 0 | The product code is back to the `qDebug()` version. |
| Rebuild without the fix | `cmake --build build --target Lb302NotePathTest -- -j12 > .../logs/build-note-path-red-3.log 2>&1; echo EXIT=$?` | 0 | Rebuilt with the base product code. |
| **RED (new-p14-134)** | `QT_QPA_PLATFORM=offscreen ctest -R '^Lb302NotePathTest$' --output-on-failure > .../logs/test-note-path-red-3.log 2>&1; echo EXIT=$?` | 8 | `FAIL! ... 'logged == 0' returned FALSE. (the LB302 note path logged 384 message(s) when its enqueue retries ran out)` and `FAIL! ... the LB302 instrument does not expose the dropped-note counter`; QtTest totals: 2 passed, 2 failed, 0 skipped. |
| **RED, slot by slot** | `QT_QPA_PLATFORM=offscreen ./Lb302NotePathTest lb302RetryExhaustionDoesNotLog > .../logs/test-note-path-slot-red.log 2>&1; echo EXIT=$?` | 1 | The finding's own slot fails on its own. |
| **RED, slot by slot** | `QT_QPA_PLATFORM=offscreen ./Lb302NotePathTest lb302RetryExhaustionIsCountedAndReportedOffTheAudioThread > .../logs/test-note-path-slot2-red.log 2>&1; echo EXIT=$?` | 1 | The counter/report slot fails on its own. |
| **RED, the new-p9-254 slot** | `QT_QPA_PLATFORM=offscreen ./PluginPortsMigrationTest lb302NoteEnqueueFailsafeStaysRealtimeSafe > .../logs/test-pluginports-slot-red.log 2>&1; echo EXIT=$?` | 1 | The first finding's slot fails on its own. |
| Restore the product fix | `git apply .../logs/fix.patch` | 0 | The fix is back. |
| Rebuild with the fix | `cmake --build build --target zene lb302 Lb302NotePathTest PluginPortsMigrationTest -- -j12 > .../logs/build-final-green-2.log 2>&1; echo EXIT=$?` | 0 | Rebuilt with the fix. |
| **GREEN, slot by slot** | `QT_QPA_PLATFORM=offscreen ./Lb302NotePathTest lb302RetryExhaustionDoesNotLog > .../logs/test-note-path-slot-green.log 2>&1; echo EXIT=$?` | 0 | The finding's own slot passes on its own. |
| **GREEN, slot by slot** | `QT_QPA_PLATFORM=offscreen ./Lb302NotePathTest lb302RetryExhaustionIsCountedAndReportedOffTheAudioThread > .../logs/test-note-path-slot2-green.log 2>&1; echo EXIT=$?` | 0 | The counter/report slot passes on its own. |
| **GREEN, the new-p9-254 slot** | `QT_QPA_PLATFORM=offscreen ./PluginPortsMigrationTest lb302NoteEnqueueFailsafeStaysRealtimeSafe > .../logs/test-pluginports-slot-green.log 2>&1; echo EXIT=$?` | 0 | The first finding's slot passes on its own. |
| **GREEN (family)** | `QT_QPA_PLATFORM=offscreen ctest -R '^Lb302NotePathTest$\|^PluginPortsMigrationTest$\|^RtSafety(Sweep\|SelfTest)$' --output-on-failure > .../logs/test-family-final.log 2>&1; echo EXIT=$?` | 8 | 3/4 targets pass: `Lb302NotePathTest` **Passed**, `RtSafetySweep` Passed, `RtSafetySelfTest` Passed; `PluginPortsMigrationTest` fails only on the pre-existing `nonRealtimeLadspaEffectsAreInvalid` (the missing SWH Decimator fixture) - its own new-p9-254 slot passes. |

The affected CTest family now runs **2** test executables for this cluster
(`Lb302NotePathTest` with 2 slots, `PluginPortsMigrationTest` with 48) plus
the two rt-safety ctests.

### The static guard, proved the same way

| Phase | Exact command | Exit | Result |
| --- | --- | ---: | --- |
| Sweep with the fix | `python3 tests/rt-safety-sweep.py > .../logs/rt-sweep-2.log 2>&1; echo EXIT=$?` | 0 | 72 declared pairs, 4 hits in 3 keys, 0 fresh keys. |
| Sweep with the product fix reversed | `git apply -R .../logs/fix.patch && python3 tests/rt-safety-sweep.py > .../logs/rt-sweep-proof-red.log 2>&1` | 1 | `FAIL: NEW rt-safety hit: plugins/Lb302/Lb302.cpp:Lb302Synth::playNoteImpl:qt-log [blocking-io] - 1 occurrence(s), first at plugins/Lb302/Lb302.cpp:512: qDebug()`. |
| Sweep with the fix restored | `git apply .../logs/fix.patch && python3 tests/rt-safety-sweep.py > .../logs/rt-sweep-proof-green.log 2>&1` | 0 | Green again. |
| Sweep ctests | `QT_QPA_PLATFORM=offscreen ctest -R '^RtSafety(Sweep\|SelfTest)$' --output-on-failure > .../logs/test-rt-safety-1.log 2>&1; echo EXIT=$?` | 0 | 2/2 passed (`RtSafetySweep`, `RtSafetySelfTest`). |
| Sweep's own controls | `python3 tests/rt_safety_selftest.py > .../logs/rt-selftest-1.log 2>&1; echo EXIT=$?` | 0 | 19 controls pass, including the new `a deliberate qDebug on an audio-thread path FAILS`. |

## Gates

| Gate | Exact command | Exit | Result |
| --- | --- | ---: | --- |
| All gates (the enforced table) | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-254 > .../logs/feature-check-3.log 2>&1; echo EXIT=$?` | 0 | **PASS** - fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity ("no new item in your files"), file-length ("no new item in your files"). |
| Complexity ratchet (raw, fork scope) | `bash tests/complexity-gate.sh --check > .../logs/gate-complexity-2.log 2>&1; echo EXIT=$?` | 1 | Red **on the train**: 6 pre-existing regressions, none in a file this change touches (`XyPadPairTest.cpp`, `DawProjectReadTracks.cpp`, `ControlCommandsMixerTools.cpp`, `ControlCommandsMixerStrip.cpp`, `ProjectRevisions.cpp`, `ControlCommandsShortcuts.cpp`). The new functions are under the target: `Lb302Synth::droppedNoteCount` CCN 1, `Lb302Synth::reportDroppedNotes` CCN 4, `PluginPortsMigrationTest::lb302NoteEnqueueFailsafeStaysRealtimeSafe` CCN 6, every function of `Lb302NotePathTest` CCN <= 3 (lizard 1.24.0). |
| File-length ratchet (raw, fork scope) | `bash tests/file-length-gate.sh --check > .../logs/gate-file-length-2.log 2>&1; echo EXIT=$?` | 1 | Red **on the train**: pre-existing new files over 500 lines (`tests/src/core/DeviceDspTest.cpp`, `tests/src/core/NotePlaybackTest.cpp`, ...), none of them touched here. `plugins/Lb302/Lb302.cpp` is an inherited file outside the enforced fork scope; the new `tests/src/plugins/Lb302NotePathTest.cpp` is 199 code lines. |
| Duplication ratchet (raw, fork scope) | `bash tests/duplication-gate.sh > .../logs/gate-duplication-1.log 2>&1; echo EXIT=$?` | 0 | "PASS: duplicated lines 0.33% (budget 5%)" over 1794 fork sources - the new test file adds no clone. |
| Inherited-file ledger | `bash tests/no-upstream-regression-gate.sh > .../logs/gate-upstream-1.log 2>&1; echo EXIT=$?` | 0 | "PASS: every change to upstream-inherited code ... is declared (1495 changed path(s) declared; the ledger holds 2124 entries)". |
| Fork-source manifest | `bash tests/fork-sources-gate.sh > .../logs/gate-fork-sources-2.log 2>&1; echo EXIT=$?` | 0 | PASS - "every tracked source in scope is registered (1791 fork-NEW, 1212 inherited, 48 tooling)". |
| Whole-tree manifest | `bash tests/all-sources-reproduce.sh > .../logs/all-sources-reproduce-1.log 2>&1; echo EXIT=$?` | 0 | "REPRODUCES: the entry list in all-sources.txt is the recipe's own output." |
| Whole-tree file-length scope (not enforced, red on the train) | `bash tests/file-length-gate.sh --scope all --check > .../logs/gate-file-length-all.log 2>&1; echo EXIT=$?` | 1 | Red on the train for 20+ files (`AudioEngine.cpp`, `GigPlayer.cpp`, ...). This change adds one line to that list (`plugins/Lb302/Lb302.cpp grew 526 -> 545`); the scope is not part of the default run and its baseline may not be re-anchored by a bug fix. |

The build was rebuilt from scratch for the changed targets before every
trustworthy run: `cmake --build build --target zene lb302 PluginPortsMigrationTest
PluginPortsMigrationReference -- -j12` (`.../logs/build-final-1.log`, EXIT=0).

## Self-check

1. **The regression test FAILS with the product change reverted and PASSES
   with it.** Each finding has its own test, and each was proved on its own:
   - `new-p9-254`: `ctest -R '^PluginPortsMigrationTest$'` -> EXIT=8, "the
     LB302 note path logged 384 message(s) from the audio-thread callback";
     the slot alone (`./PluginPortsMigrationTest
     lb302NoteEnqueueFailsafeStaysRealtimeSafe`) -> EXIT=1 with the product
     change reversed, EXIT=0 with the fix.
   - `new-p14-134`: `ctest -R '^Lb302NotePathTest$'` -> EXIT=8 with the
     product change reversed ("the LB302 note path logged 384 message(s) when
     its enqueue retries ran out") and EXIT=0 with the fix; the slot alone
     (`./Lb302NotePathTest lb302RetryExhaustionDoesNotLog`) -> EXIT=1
     reversed, EXIT=0 restored.
   The static guard behaves the same way: the sweep exits 1 on the reverted
   code and 0 on the fixed code.
2. **The test is deterministic.** No wall-clock timing, no sleeps, no races:
   the failsafe is reached by *structure* (512 enqueues against a 128-slot
   ring buffer), the allocation count comes from the harness' own
   `partc::RealtimeAllocationWatch` seam (and from the equivalent
   `AllocationWatch` of the new test), the message count from the
   thread-local `LogCapture` seam, and the counter/report are read through
   the meta-object (`QMetaObject::invokeMethod`). The same input produces
   the same 384 drops on every run.
3. **The fix is at the cause and complete.** The cause is the single logging
   call in the note path; there is no other `qDebug`/`qWarning`/`qInfo`/
   `qCritical` in `plugins/Lb302/` (`rg -n "q(Debug|Warning|Info|Critical)"
   plugins/Lb302/` now reports only the control-thread report at
   `Lb302.cpp:572`). Every sibling of the change was checked: the counter is
   written only by the failsafe and read only by the control thread; the
   report is the only writer of the log; the timer is a child of the
   instrument, so it dies with it; the report refuses to run on the render
   thread and survives the engine's teardown (`Engine::destroy()` nulls
   `s_audioEngine` before deleting it, and the report checks for null).
   The only other consumer of the class is the frozen pre-migration
   reference module `tests/reference/Lb302/` (its own header, its own
   `playNote`), which is left untouched on purpose.
4. **No collateral damage.** The DSP is untouched: the counter is not read
   by the audio path, and `PluginPortsMigrationTest::migratedPluginsPreserveBehaviour`
   (which compares the migrated plugin sample-by-sample against the frozen
   pre-migration reference) still passes, as does the whole family
   (45 passed / 1 pre-existing failure / 2 skipped). `saveSettings`/
   `loadSettings` are unchanged, so old projects load exactly as before and
   nothing new is written to the project file. The new limit is no limit:
   the failsafe still drops the note after the same
   `s_maxNoteEnqueueRetries` (128) retries, and the 1 s report timer is the
   only new periodic work - it does nothing while the counter is zero.
5. **Realtime, undo and commands.** The audio thread does strictly less than
   before: the failsafe now does one relaxed `fetch_add` on an atomic
   (`plugins/Lb302/Lb302.cpp:524`) - no allocation, no free, no lock, no
   wait, no logging, no signal. The report (`Lb302.cpp:564`) and the timer
   (`Lb302.cpp:250`) are control-thread work. The change touches no undo
   step and adds **no** control command (the two new public slots are
   plugin-internal accessors, not commands; the ControlCommands snapshot is
   unchanged). Portability: the fix uses only `QTimer`, `QWarning` and
   `<atomic>`, all present in the supported Qt 5.15/6 and C++20 baseline;
   no Qt-6-only API, no `windows.h` macro, no `std::filesystem`.
6. **The ALREADY-FIXED / FALSE-POSITIVE question does not apply** - both
   verdicts are FIXED. For the record, the finding was REAL and the code
   said so before the fix: `plugins/Lb302/Lb302.cpp:512` held
   `qDebug() << "Lb302: Note dropped due to catastrophically poor
   performance! ...";` inside `Lb302Synth::playNoteImpl`, and
   `src/core/AudioEngine.cpp:836` -> `src/core/NotePlayHandle.cpp:225,307`
   -> `include/Instrument.h:108` is the path that puts that call on the
   render thread. The regression reproduced it: 384 log messages on the
   note path in one test run.

## Files in the commit

- `plugins/Lb302/Lb302.cpp` - the failsafe counts instead of logging; the
  control-thread report; the report timer.
- `plugins/Lb302/Lb302.h` - the counter and the two accessors.
- `tests/src/plugins/PluginPortsMigrationTest.cpp` - the **new-p9-254**
  regression and its message seam.
- `tests/src/plugins/Lb302NotePathTest.cpp` - the **new-p14-134** regression,
  in its own CTest target (no plugin module, no platform skip).
- `tests/CMakeLists.txt` - registers the `Lb302NotePathTest` target.
- `tests/all-sources.txt`, `tests/fork-sources.txt` - the new test source is
  registered in both scope manifests.
- `tests/rt_safety_source.py` - the `qt-log` rule.
- `tests/rt_safety_selftest.py` - the rule's positive control.
- `tests/rt-safety-scope.txt` - the LB302 note path is declared.
- `tests/upstream-modifications.txt` - the two inherited product files.
- `docs/RT-SAFETY-SWEEP.md` - the new rule and its bound.
- `docs/qwen-fixes-2026-10/cluster-new-p9-254.md` - this report.
