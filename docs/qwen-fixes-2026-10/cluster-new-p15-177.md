FIXED

# Cluster new-p15-177 — clip destruction raced record-handle claiming

Verdict: **FIXED** (`src/core/SampleClip.cpp:158-168`, `:265-278`).

## Finding

`~SampleClip` deleted `m_preparedRecord` and `m_spareRecord` directly, while the audio thread
takes those same raw pointers through `claimRecordHandle()` during a render period.

## Decision: REAL

The slots are the clip's record takes. Two readers exist and they did not agree on a lock:

- `src/core/SampleClip.cpp:191-198` `SampleClip::claimRecordHandle()` — the audio-thread take:
  reads `m_preparedRecord`, moves `m_spareRecord` into it, nulls the spare and calls
  `markClaimed()`. Reached from `src/tracks/SampleTrack.cpp:211` inside `SampleTrack::play()`,
  which runs in the render period, and the render period owns the engine change lock
  (`std::lock_guard{m_changeMutex}` in `AudioEngine::renderNextPeriod()`; the lock is
  `include/AudioEngine.h:994`).
- `src/core/SampleClip.cpp:200-225` `prepareRecordTake()` and `:265-278` `releaseRecordTakes()` —
  the writing side: both read/exchange the slots under
  `Engine::audioEngine()->requestChangesGuard()`.
- `src/core/SampleClip.cpp:158-168` `~SampleClip()` — before this fix it freed the two pointers
  with **no** lock at all. That is the one path that touched the slots outside the lock the claim
  holds, so a deletion could free a handle a period was concurrently claiming: use-after-free, and
  the same pointer owned by both sides (double delete).

Not a defect of the *claim* or the *prepare* path, and not one of the "by design" behaviours in
AGENTS.md: it is a missing lock acquisition on the destructor only.

## Fix

`src/core/SampleClip.cpp:163` — the destructor now calls `releaseRecordTakes()`, which detaches
both slots under the change guard (`:272-273`) and deletes them after the guard is released
(`:276-277`), exactly the shape the release path already used. Nothing allocates, frees, locks or
logs while a period is being blocked for longer than the two `std::exchange` calls.

`src/core/SampleClip.cpp:270-271` — `releaseRecordTakes()` now tolerates a null engine. The
destructor is the one caller that can run without one: `Engine::destroy()` deletes the audio engine
(`src/core/Engine.cpp:121`) **before** the song (`:131`), so a clip destroyed at song teardown finds
`Engine::audioEngine() == nullptr`, where the old unconditional dereference would have crashed. With
an engine present the guard is taken exactly as before; the other caller
(`prepareRecordTake()`, `:206`) always has one.

Completeness of the change, checked caller by caller:

- `releaseRecordTakes()` callers: `prepareRecordTake()` (`src/core/SampleClip.cpp:206`, the disarm
  path, engine always alive) and the destructor (`:163`). Both stay consistent with the guard.
- A **claimed** handle is already detached by `claimRecordHandle()` (`:193-194` moves the spare into
  the prepared slot and nulls the spare), so the destructor can only free unclaimed handles the
  engine never owned; it cannot free a handle the engine's list holds.
- Siblings: the two slots are default-initialised to `nullptr` (`include/SampleClip.h:328`, `:331`)
  and the copy constructor does not copy them (`src/core/SampleClip.cpp:102-112`), so a copy owns
  its own takes and the destructor can never free another clip's handle.
- The destructor already took the change lock on its existing path
  (`sampletrack->updateClips()` → `AudioEngine::removePlayHandlesOfTypes()` →
  `requestChangeInModel()`), and `m_changeMutex` is a `std::recursive_mutex`
  (`include/AudioEngine.h:994`), so the fix adds no new lock ordering: it only moves the two frees
  from *before* that acquisition to *after* it.
- No new allocation, signal, command or undo step: the release path is unchanged, and the
  destructor's work is main-thread work that was already there.

## Regression test

`tests/src/core/SampleClipRecordTeardownTest.cpp` (new; registered in `tests/CMakeLists.txt:767`,
`:3905`, `tests/fork-sources.txt`, `tests/all-sources.txt`, and
`tests/upstream-modifications.txt` for `src/core/SampleClip.cpp` and the CMake file).

- `deletingAnArmedClipQueuesBothPreparedTakes()` (`:99`) — an armed clip carries two takes
  (`setRecord(true)` → `armRecordTake()` → `prepareRecordTake()`), and destroying it hands **both**
  to the main-thread reaper: `SampleRecordHandle::takesReaped()` grows by exactly 2. This passes
  with and without the fix; it pins that the teardown neither leaks nor drops a take.
- `clipDestructionWaitsForTheChangeLockBeforeFreeingHandles()` (`:124`) — the regression. The test
  holds the change guard (what `renderNextPeriod()` holds for the whole period, `:143`), deletes an
  armed clip on a second thread (`:145`), and decides on **states, not time**:
  `AudioEngine::modelChangeWaiters()` (`:155`, the seam `src/core/AudioEngine.cpp:1990-1992`
  increments around `m_changeMutex.lock()`) says the destructor has parked on the lock, and
  `SampleRecordHandle::takesReaped()` says how many handles were freed before that point (`:159`).
  The 10 s deadline only turns a missing lock into a failure instead of a hang; no sleep, no
  wall-clock assertion, no timing-dependent expectation. With the fix the count is 0 and both takes
  are reaped afterwards (`:172`); without it the destructor freed both handles first and the
  assertion at `:166` fails with "delete clip freed 2 record handle(s) before it waited for the
  change lock held by the render period".

Local TSan does not run on this box (AGENTS.md), so the exclusion argument above is by design and
by the lock the claim and the teardown now both take, not by a sanitizer run.

## Commands and exit codes (all logs in
`/home/kruzzzzy/.config/kads/zene/state/runs/new-p15-177.r2/logs`)

| Step | Command | EXIT | Log |
|---|---|---|---|
| build (first attempt, before regenerating the build tree) | `cmake --build build --target SampleClipRecordTeardownTest -j12` | 2 (`No rule to make target`) | `build-4.log` |
| regenerate build tree | `cmake --build build --target cmake_check_build_system` | 0 | `build-5.log` (`CHECK_EXIT=0`) |
| build RED | `cmake --build build --target SampleClipRecordTeardownTest -j12` | 0 | `build-6.log` |
| **RED** | `cd build/tests && ctest -R SampleClipRecordTeardownTest --output-on-failure` | **8** (1 failed of 1; `Loc: [../../tests/src/core/SampleClipRecordTeardownTest.cpp(166)]`) | `test-2-red.log` |
| apply the fix | `git apply --include=src/core/SampleClip.cpp <r1 leftover diff.patch>` | 0 | — |
| build GREEN | `cmake --build build --target SampleClipRecordTeardownTest -j12` | 0 | `build-7.log` |
| **GREEN** | `cd build/tests && ctest -R SampleClipRecordTeardownTest --output-on-failure` | **0** (1 of 1 passed) | `test-3-green.log` |
| save product diff (step 4b) | `git diff -- src/core/SampleClip.cpp > logs/fix.patch` | 0 | `fix.patch` |
| revert product fix (step 4b) | `git apply -R logs/fix.patch` | 0 | — |
| rebuild without fix | `cmake --build build --target SampleClipRecordTeardownTest -j12` | 0 | `build-8.log` (`BUILD_EXIT=0`) |
| **4b: must FAIL** | `cd build/tests && ctest -R SampleClipRecordTeardownTest --output-on-failure` | **8** | `test-4-red-after-revert.log` (`CTEST_EXIT=8`) |
| restore fix | `git apply logs/fix.patch` | 0 | — |
| rebuild with fix | `cmake --build build --target SampleClipRecordTeardownTest -j12` | 0 | `build-9.log` (`BUILD_EXIT=0`) |
| **4b: must PASS** | `cd build/tests && ctest -R SampleClipRecordTeardownTest --output-on-failure` | **0** | `test-5-green-after-reapply.log` (`CTEST_EXIT=0`) |
| family build | `cmake --build build --target RecordingRealtimeTest RecordingLatencyTest SampleClipStretchTest -j6` | 0 | `build-10-family.log` |
| family tests | `cd build/tests && ctest -R 'RecordingRealtimeTest\|RecordingLatencyTest\|SampleClipStretchTest' --output-on-failure` | 0 (3 of 3 passed: 11.71 s, 4.58 s, 0.70 s) | `test-6-family.log` |
| gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 0, `RESULT: PASS`, every row PASS | `gates-1.log` |
| completion check | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> new-p15-177` | see `## Self-check` | `submit-1.log` |

The RED run reproduces the same failure the earlier round recorded
(`new-p15-177.r2` pre-existing `test-1-red.log`: same message, same `Loc` line), so the rewritten
test is the same test.

## Test matrix

| Path | Covered by |
|---|---|
| normal path: an armed clip with both slots filled is destroyed while no period runs | `deletingAnArmedClipQueuesBothPreparedTakes` (2 takes reaped) |
| failure/race path: a period owns the change lock while the clip is destroyed | `clipDestructionWaitsForTheChangeLockBeforeFreeingHandles` (0 frees before the lock, 2 after) |
| exhaustion/reconfiguration: a clip with **no** armed takes (`== nullptr` slots) | unchanged code path (`std::exchange` of null), exercised by every `SampleClip` destruction in `SampleClipStretchTest` (3 of 3 family suites pass) |
| engine absent (teardown, `Engine::destroy`) | `:270-271` null guard; a null guard assignment is a no-op, no lock taken because there is no audio thread left |
| audio-thread constraints | the fix adds nothing to `claimRecordHandle()` or `SampleTrack::play()`; the destructor is main-thread (it calls `updateClips()` there already) |
| supported platforms | no Qt-6-only API, no POSIX-only path, no new header; `std::atomic`, `std::thread`, `std::chrono` are already used by neighbouring tests (`tests/src/tracks/TakeLaneCompPlaybackTest.cpp:219-260`) |

## Self-check

1. **The regression test FAILS with the product change reverted and PASSES with it.**
   Reverted (`git apply -R logs/fix.patch`, rebuild exit 0): `CTEST_EXIT=8`
   (`test-4-red-after-revert.log`). Restored (`git apply logs/fix.patch`, rebuild exit 0):
   `CTEST_EXIT=0` (`test-5-green-after-reapply.log`). The first RED, before any fix existed, is
   `test-2-red.log` (exit 8, same assertion and line).
2. **The test is deterministic.** No sleep and no elapsed-time assertion: it synchronises on
   `AudioEngine::modelChangeWaiters()`, which `requestChangeInModel()` increments around the
   `m_changeMutex.lock()` (`src/core/AudioEngine.cpp:1990-1992`), and counts frees through
   `SampleRecordHandle::takesReaped()`. The same seam and idiom is already used by
   `tests/src/tracks/TakeLaneCompPlaybackTest.cpp:219-260`. The 10 s deadline exists only so a
   missing lock fails instead of hanging.
3. **The fix is at the cause and complete.** Callers and siblings listed above: the destructor
   (`:163`), `prepareRecordTake()`'s release branch (`:206`), the audio-thread claim (`:191`) left
   as it was, the slots' default initialisation and the copy constructor, the engine-teardown
   order (`src/core/Engine.cpp:121` before `:131`), and the destructor's pre-existing lock
   acquisition through `updateClips()`. No other file holds the slots.
4. **No collateral damage.** The release path is byte-for-byte the same behaviour under a live
   engine; only the destructor's ordering changed. No new limit or check rejects any caller (the
   null-engine branch is only taken when there is no engine). Saved data is untouched: neither
   `saveSettings()` nor the clip's XML changed, and no new member, id or file format was added.
   The record family suites (`RecordingRealtimeTest`, `RecordingLatencyTest`) and the clip suite
   (`SampleClipStretchTest`) pass unchanged (`test-6-family.log`).
5. **No audio-thread work added.** The diff adds no allocation, lock, wait, log or signal inside a
   render function: `~SampleClip` and `releaseRecordTakes()` are main-thread code (the destructor
   calls `updateClips()`, which already takes the same lock), and `claimRecordHandle()` /
   `SampleTrack::play()` are not touched. The guard taken is the same recursive mutex the
   surrounding code already takes; `std::exchange` and `delete` of an unclaimed handle (which only
   queues the take, `src/core/SampleRecordHandle.cpp:140-160`) happen after it is released.
6. **Verdict evidence** (asked only for not-real verdicts): not applicable, the verdict is FIXED;
   the code showing the defect is `src/core/SampleClip.cpp:158-168` before the fix, and the
   unguarded claim it raced is `:191-198` reached from `src/tracks/SampleTrack.cpp:211`.

## Other findings

- `tests/upstream-modifications.txt` had moved since the earlier round, so the two registration
  lines were appended by hand instead of by patch; both manifest gates pass (`gates-1.log`).
- The build tree's Makefiles do not know a newly registered test target until
  `cmake_check_build_system` runs; `cmake --build build --target <new test>` fails with
  "No rule to make target" until then (`build-4.log`). Worth knowing for other lanes; not a defect
  in this cluster.
