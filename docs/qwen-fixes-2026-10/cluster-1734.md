FIXED

1734: FIXED

Decision: REAL. `m_tempoMapAppliedTempo` is atomic in `include/Song.h:832`, but `Song::setTempo()` previously released the audio model guard before invalidating it. A render could therefore observe the newly updated scalar tempo while `followTempoMap()` still memoized the old mapped tempo. The fix resets the memo and updates frames-per-tick before releasing the guard in `src/core/Song.cpp:402-423`.

Regression: `TempoMapTest::globalTempoChangeInvalidatesMemoBeforeRenderResumes` injects `processNextBuffer()` at the boundary after `updateFramesPerTick()` and before the legacy invalidation point. It failed with the old ordering and passed with the reset under the guard.

RED commands and results:

```text
git diff -- src/core/Song.cpp src/core/AutomationClip.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/1734.r2/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/1734.r2/logs/fix.patch
EXIT=0
cmake --build build --target zene TempoMapTest -- -j8 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/1734.r2/logs/build-16.log 2>&1; echo EXIT=$?
EXIT=0
ctest -R '^TempoMapTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/1734.r2/logs/test-17.log 2>&1; echo EXIT=$?
EXIT=8 — globalTempoChangeInvalidatesMemoBeforeRenderResumes failed; 1/1 ctest target failed.
```

GREEN commands and results:

```text
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/1734.r2/logs/fix.patch
EXIT=0
cmake --build build --target zene TempoMapTest -- -j8 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/1734.r2/logs/build-17.log 2>&1; echo EXIT=$?
EXIT=0
ctest -R '^TempoMapTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/1734.r2/logs/test-18.log 2>&1; echo EXIT=$?
EXIT=0 — 1/1 ctest target passed.
```

Committed files: `src/core/Song.cpp`, `tests/src/core/TempoMapTest.cpp`, and the cluster 1734 row in `tests/upstream-modifications.txt`. Commit: `2b38047bb` (`fix(core): close tempo-map memo race (cluster 1734)`).

3562: FIXED

Decision: REAL. The render path walks and rewrites `Song::m_oldAutomatedValues` in `src/core/Song.cpp:733-870`; model destruction called `Song::forgetAutomatedModel()` and erased from that vector without the render-period guard. `QPointer` protects the model lifetime but does not serialize vector access. The fix takes `AudioEngine::requestChangesGuard()` before touching the cache at `src/core/Song.cpp:377-389`; `AudioEngine::renderNextPeriod()` holds the same mutex across the render period (`src/core/AudioEngine.cpp:919-922`).

Regression: `AutomationModesTest::automatedCacheRemovalUsesTheRenderGuard` reads the focused function body and verifies that `requestChangesGuard` precedes access to `m_oldAutomatedValues`. This is a deterministic source invariant; it introduces no concurrent test race.

RED commands and results:

```text
git diff -- src/core/Song.cpp src/core/AutomationClip.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/1734.r2/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/1734.r2/logs/fix.patch
EXIT=0
cmake --build build --target zene AutomationModesTest -- -j8 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/1734.r2/logs/build-18.log 2>&1; echo EXIT=$?
EXIT=0
ctest -R '^AutomationModesTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/1734.r2/logs/test-19.log 2>&1; echo EXIT=$?
EXIT=8 — automatedCacheRemovalUsesTheRenderGuard failed; 1/1 ctest target failed.
```

GREEN commands and results:

```text
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/1734.r2/logs/fix.patch
EXIT=0
cmake --build build --target zene AutomationModesTest -- -j8 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/1734.r2/logs/build-19.log 2>&1; echo EXIT=$?
EXIT=0
ctest -R '^AutomationModesTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/1734.r2/logs/test-20.log 2>&1; echo EXIT=$?
EXIT=0 — 1/1 ctest target passed.
```

Committed files: `src/core/Song.cpp`, `tests/CMakeLists.txt`, `tests/src/core/AutomationModesTest.cpp`, and the cluster 3562 rows in `tests/upstream-modifications.txt`. Commit: `fa45778f4` (`fix(core): guard automation cache removal (cluster 3562)`).

5388: FIXED

Decision: REAL, and REWORKED after the round-2 review. The render path may not edit a clip's node map - that edit allocates - so the write is queued and the control thread applies it. Round 2 was sent back because the queue it introduced was a FIXED global ring (16 x 16,384 entries) that the render path filled once per qualifying clip per tick, with no cap on the clip count: when the ring filled, `push()` incremented an overflow counter and returned, so the recorded automation point was silently lost.

The rework replaces the global ring with a per-clip store:

- Every `AutomationClip` owns the storage for its own pending writes (`include/AutomationClip.h:146-167` for the interface, `include/AutomationClip.h:394-410` for the members; `src/core/AutomationClip.cpp:544-574`). The room a write needs therefore grows with the project instead of with a constant, and one clip's backlog can never take another clip's place - an uncapped clip count cannot overflow a shared buffer.
- A pending write is ONE packed 64-bit word: the tick in the high half, the value's bit pattern in the low half (a tick is a `tick_t`, 32 bits, and a value is a float, 32 bits, so both fit exactly - `src/core/AutomationClip.cpp:62-84`). It is stored with a single atomic store, so the render thread allocates nothing, locks nothing, waits for nothing and logs nothing, and the writer and the reader can never see half of one write and half of another.
- The saturation behaviour is DEFINED and is not a silent drop (`src/core/AutomationClip.cpp:556-571`): when a clip's store is full, the write that arrives takes the place of the clip's newest pending write - the value the control is at when the store fills is the one that survives - and the write it replaced is COUNTED (`AutomationClip::pendingWriteMerges()`, `include/AutomationClip.h:161-165`). The queue never returns "dropped".
- The control thread applies the queued writes in the order they were queued (`AutomationClip::applyPendingWrites()`, `src/core/AutomationClip.cpp:576-608`). `Song::applyPendingAutomationWrites()` (`src/core/Song.cpp:280-307`) finds the clips that have queued writes through the project journal - which knows every clip of the session, including the inline automation clip of a note, which is on no track at all (`include/ProjectJournal.h:369-376`) - and applies them under the render guard. The drain runs on the control-thread timer and also before Stop and before a save (`src/core/Song.cpp:1276`, `src/core/Song.cpp:2349`); a project that is being cleared drops its queued writes instead of carrying them into the project that replaces it (`src/core/Song.cpp:266-278`, `src/core/Song.cpp:1612`).

Both realtime callers are covered: `Song::processAutomations()` queues the write in the clip it writes to (`src/core/Song.cpp:806-808`), and `AutomationClip::recordValue()` queues every call made inside a `RealtimeScope` (`src/core/AutomationClip.cpp:508-517`), which is the path `NotePlayHandle::processTimePos()` takes (`src/core/NotePlayHandle.cpp:707`).

Regression: `AutomationModesTest::testSaturatedPendingWritesKeepTheNewestValue` queues 300,000 writes through the product's own path - `AutomationClip::recordValue()` inside a realtime scope, which is exactly what the render thread does - and then invokes the drain. The clip must hold the NEWEST value: the write that arrived when the store was full must not be lost. The count is larger than any fixed queue the product has ever had (the round-2 ring held 262,144 entries), so the store is saturated in every build and the assertion is about the behaviour, not about a capacity. No wall-clock timing, no sleeps, no races: the writes are injected, and the drain is invoked directly.

RED commands and results (round 3, with the finished rework reverted):

```text
git diff -- include/AutomationClip.h include/ProjectJournal.h include/Song.h src/core/AutomationClip.cpp src/core/Song.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/1734.r3/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/1734.r3/logs/fix.patch
EXIT=0
cmake --build build --target zene AutomationModesTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/1734.r3/logs/build-red-2.log 2>&1; echo EXIT=$?
EXIT=0
ctest -R '^AutomationModesTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/1734.r3/logs/test-red-2.log 2>&1; echo EXIT=$?
EXIT=8 — testSaturatedPendingWritesKeepTheNewestValue failed (20 passed, 1 failed); the newest write was dropped by the full global queue.
```

GREEN commands and results:

```text
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/1734.r3/logs/fix.patch
EXIT=0
cmake --build build --target zene AutomationModesTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/1734.r3/logs/build-green-3.log 2>&1; echo EXIT=$?
EXIT=0
ctest -R '^AutomationModesTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/1734.r3/logs/test-green-3.log 2>&1; echo EXIT=$?
EXIT=0 — 1/1 ctest target passed.
```

Committed files: `include/AutomationClip.h`, `include/ProjectJournal.h`, `include/Song.h`, `src/core/AutomationClip.cpp`, `src/core/Song.cpp`, `tests/src/core/AutomationModesTest.cpp`, `tests/upstream-modifications.txt`, and this report.

## Evidence table

| Cluster | RED | GREEN | Final affected-family run | Feature gate | Completion gate | Files committed |
|---|---|---|---|---|---|---|
| 1734 | `1734.r2/logs/test-17.log`, ctest exit 8 | `1734.r2/logs/test-18.log`, ctest exit 0 | `1734.r3/logs/test-family-4.log`, 20/20 targets passed | `feature_check.py`, exit 0 | `submit_check.py`, exit 0 | Song.cpp, TempoMapTest.cpp, upstream-modifications.txt |
| 3562 | `1734.r2/logs/test-19.log`, ctest exit 8 | `1734.r2/logs/test-20.log`, ctest exit 0 | `1734.r3/logs/test-family-4.log`, 20/20 targets passed | `feature_check.py`, exit 0 | `submit_check.py`, exit 0 | Song.cpp, CMakeLists.txt, AutomationModesTest.cpp, upstream-modifications.txt |
| 5388 | `1734.r3/logs/test-red-2.log`, ctest exit 8 | `1734.r3/logs/test-green-3.log`, ctest exit 0 | `1734.r3/logs/test-family-4.log`, 20/20 targets passed | `feature_check.py`, exit 0 | `submit_check.py`, exit 0 | AutomationClip.h, ProjectJournal.h, Song.h, AutomationClip.cpp, Song.cpp, AutomationModesTest.cpp, upstream-modifications.txt, this report |

Final combined commands (round 3):

```text
cmake --build build --target zene AutomationModesTest TempoMapTest SampleAccurateAutomationTest ControlAutomationScriptTest ControlAutomationModesTest AutomationRenderAllocationTest AutomationPrepassProofTest AutomationLanesTest AutomationWriteToEndTest AutomationThinTest AudioAutomationToolsTest TempoMapPersistenceTest AutomationModeMenuTest AutomationEditorEditingTest AutomationClipDisconnectTest TempoMapPanelTest AutomationTrackTest DelayLfoAutomationTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/1734.r3/logs/build-final-1.log 2>&1; echo EXIT=$?
EXIT=0
ctest -R 'Automation|TempoMap' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/1734.r3/logs/test-family-4.log 2>&1; echo EXIT=$?
EXIT=0 — 20/20 ctest targets passed (the automation and tempo-map family: AutomationModesTest, SampleAccurateAutomationTest, ControlAutomationScriptTest, ControlAutomationModesTest, ControlTempoMapCommandsTest, AutomationRenderAllocationTest, AutomationPrepassProofTest, AutomationLanesTest, AutomationWriteToEndTest, AutomationThinTest, AudioAutomationToolsTest, TempoMapPersistenceTest, TempoMapTest, AutomationModeMenuTest, AutomationEditorEditingTest, AutomationClipDisconnectTest, TempoMapPanelTest, AutomationTrackTest, DelayLfoAutomationTest, ControlTempoMapPersistence).
python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-1734 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/1734.r3/logs/gate-1.log 2>&1; echo EXIT=$?
EXIT=0 — RESULT: PASS; complexity and file-length report "no new item in your files".
```

The family run needs the instrument modules to be present (the six targets that failed on a bare tree failed with "the song exposes no automatable device parameter ... the binary needs the instrument modules (LMMS_TEST_PLUGIN_DIR)"; they pass once `tripleoscillator` and `amplifier` are built - `1734.r3/logs/build-plugins-1.log`, EXIT=0). `AutomationRenderAllocationTest`, the realtime allocation sweep over the render path, passes in the family run (`1734.r3/logs/test-family-4.log`, 177.12 sec), which is the evidence that the new queue allocates nothing on the audio thread.

`AutomationRenderAllocationTest::theBookkeepingCostsMicrosecondsPerEdit()` is a WALL-CLOCK test (it builds 10,000 clips and asserts a per-edit cost), and two later runs of the same binaries timed out in it (`1734.r3/logs/test-family-5.log`, `1734.r3/logs/test-family-6.log`: "Test function timed out", 10 passed / 1 failed). The machine was at a load average of ~90 with 120 unrelated runaway `zene` processes on it (started before this session, 12 to 46 hours old) when those two runs ran; the same target and the same code passed in 196.80 sec (`test-family-3.log`) and 177.12 sec (`test-family-4.log`), and passed again in 154.31 sec when the load had dropped (`1734.r3/logs/test-family-7.log`, ctest exit 0). The timeout is the loaded machine, not the change: the failing assertion is QtTest's per-function time bound, and no assertion of that test failed.

## Self-check

- **The regression test fails with the product change reverted and passes with it (exit codes).** Yes. Round 3: `git apply -R .../fix.patch` -> ctest exit 8 with `testSaturatedPendingWritesKeepTheNewestValue` failing (20 passed, 1 failed, `test-red-2.log`); `git apply .../fix.patch` -> ctest exit 0 (`test-green-3.log`). The 1734 and 3562 RED/GREEN proofs are the round-2 runs quoted above and are unchanged by this rework.
- **The test is deterministic: no wall-clock timing, sleeps or races (name the seam or injected values it uses).** Yes. The saturation test injects 300,000 writes through the product's own queueing path (`AutomationClip::recordValue()` inside a `RealtimeScope`) and invokes the control thread's drain directly (`QTimer::timeout` on the drain timer, as the existing 5388 regression does). No clock, no sleep, no thread race.
- **The fix is at the cause and complete: every caller and sibling of what you changed was checked and stays consistent.** Yes. The queue now lives in the clip, so both realtime entry points (`Song::processAutomations()` and `AutomationClip::recordValue()` under `RealtimeScope`, which `NotePlayHandle::processTimePos()` uses) reach the same store; the control thread's drain reaches every clip of the session through the project journal (song tracks, pattern-store tracks and the inline note clip); Stop, save and project-clear all have an explicit path.
- **No collateral damage: old projects and saved data still load as before, and any limit or check added does not hit other callers that legitimately exceed it.** Yes. The pending store is runtime-only: no project format, no saved state and no load path changes. The store is per clip, so a project with many automation clips needs more room but no clip can overflow a shared buffer; a clip whose store fills keeps its newest write and counts the merge. Saves and Stop drain the queued writes before persisting or replacing the project, and a cleared project drops its queued writes.
- **Realtime: nothing new on the audio thread allocates, frees, locks, waits or logs.** Yes. `AutomationClip::queuePendingWrite()` is a bounded number of atomic loads and stores on preallocated members; the write is one packed 64-bit word. `AutomationRenderAllocationTest` (the render-path allocation sweep) passes in the family run.
- **Undo and commands:** unchanged. The queued write is applied by the same `AutomationClip::recordValue()` the render path used before the deferral, on the control thread, with the same clip semantics; no new control command was added.
- **If your verdict is ALREADY-FIXED or FALSE-POSITIVE: the code or intended contract that shows it.** Not applicable: all three verdicts are FIXED, on the RED/GREEN evidence above.
- **Completion check.** `submit_check.py` returned exit 0 after the fix/report commit (`1734.r3/logs/submit-1.log`: "RESULT: PASS - submit", working tree clean), and `feature_check.py` returned exit 0 after the same commit (`1734.r3/logs/gate-2.log`: "RESULT: PASS", complexity and file-length "no new item in your files").
