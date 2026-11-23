FIXED

# Cluster new-p15-204

`Published session audio can outlive its sample buffer` - a clip an engine-rate change REPLACED was put on the
clip cache's free-at-next-close list, so the project close that immediately followed it freed the buffer while the
scheduler's content table could still name it. The ordinary publish/queue path was already safe (the borrowed
pointer is the documented model-thread ownership contract); the defect was in the deferred-reclamation schedule of
that one path. Fixed at the cause: a replaced clip is parked and retired at the next close, exactly like a
project's own clips, so it is freed only after a close whose scheduler reset has cleared the table.

## Why the item is real (evidence)

The API's borrowed pointer is by design, not the defect:

- `include/SessionPlayback.h:67-69` - `SessionSlotContent::clip` is "Owned on the model thread by
  `include/SessionAudioCells.h`, which keeps it alive while the scheduler can name it".
- `include/SessionAudioCells.h:96-109` - the same contract on `prepare()`/`retire()`: the pointer stays valid
  until the project is closed twice over, and `retire()` frees the clips retired at the PREVIOUS close, "by then
  the scheduler's reset has dropped every reference the audio thread had to them".
- `src/core/SessionScheduler.cpp:440-453` - `processAudio()` consumes a pending reset FIRST (line 443), before
  draining content commands or advancing slots.
- `src/core/SessionFollow.cpp:487-490` - that reset clears `m_slotContent`, the only table that stores the clip.
- `src/core/SessionAudioCells.cpp:109-140` - the only dereference of the published clip is
  `SessionCellPlayHandle::play()`; `src/core/SampleTrack.cpp:73-77` and `src/core/InstrumentTrack.cpp:216-233`
  remove that handle under the engine change lock, before `Song::clearProject` calls `retire()`.

So a queued/stored raw pointer is safe while the cache obeys "free only after a close that a reset has cleared".
The rate-change path broke that invariant:

- Before the fix, `src/core/SessionAudioCells.cpp:182-188` (`load()`) moved every held clip straight into
  `retired` on an engine-rate change.
- `src/core/SessionAudioCells.cpp:264-271` (`retire()`) freed `retired` unconditionally at the NEXT close.
- `src/core/Song.cpp:1818-1832` shows that close calls `sessionaudio::retire()` at 1820 but only REQUESTS
  `m_sessionScheduler.reset()` at 1832, after `doneChangeInModel()` at 1822.

The close therefore freed the replaced buffer before the reset that drops the table's reference to it. That is
the reported "published session audio outlives its sample buffer" case (the replacement half of the finding's
trigger). A project's own clips never took that path: they were retired at the previous close, whose reset had a
full project lifetime to run.

## The fix

- `src/core/SessionAudioCells.cpp:148-159` - `Clips::stale`, the parking list for clips an engine-rate change
  replaced.
- `src/core/SessionAudioCells.cpp:184-196` - `load()` parks the replaced source clips and variants in `stale`
  instead of `retired`.
- `src/core/SessionAudioCells.cpp:273-287` - `retire()` frees the previous close's `retired` set, promotes
  `stale` into `retired` (they were retired DURING the closing project, so its reset is only requested below),
  then retires the closing project's held clips. Every freed buffer has now survived a close whose reset cleared
  the content table - the same two-close cadence documented in `docs/SESSION-AUDIO-RECORD.md` "Lifetime".
- `include/SessionAudioCells.h:103-109` and `docs/SESSION-AUDIO-RECORD.md:74-77` - the contract text says so.
- Commit `97bc493878` added the test seam `sessionaudio::holdsClipForTest()` (under `LMMS_TESTING`,
  `include/SessionAudioCells.h:113-120`, `src/core/SessionAudioCells.cpp:295-317`) so the regression can observe
  ownership without dereferencing a freed buffer. It is a test seam, not a behaviour change; the shipping build
  does not compile it.

No audio-thread code changed. `stale` is model-thread storage touched only by `load()`/`retire()` under the engine
change lock; no allocation, lock, free or log was added to any render path.

## Regression test

`tests/src/core/SessionAudioCellPlaybackTest.cpp` -
`aRateReplacedClipSurvivesTheCloseThatStillNamesIt()`:

1. prepare an audio clip and publish it, then render a period so the content table names it;
2. change the engine rate with `setSampleRateForTesting()` and prepare again (the old-rate buffer is replaced);
3. `song.clearProject()` - the close that used to free it; the test asserts the cache still owns it;
4. render a period so the scheduler consumes the reset and clears the table;
5. `song.clearProject()` again - the test asserts the clip is now reclaimed.

Deterministic: no sleeps, wall clock or threads; the test drives the periods and the closes itself, and reads the
cache's ownership through the seam. Normal path (two closes reclaim a project clip), failure path (missing/unreadable
file is untouched) and exhaustion path (the 256-variant budget in `variant()`) are unchanged and covered by the
existing cases in the same file. No platform-specific code is involved.

## Commands and EXIT codes

Run from the worktree root unless the working directory says otherwise. Logs are in
`/home/kruzzzzy/.config/kads/zene/state/runs/new-p15-204.r1/logs/`.

| # | Command | EXIT | Log |
| --- | --- | ---: | --- |
| 0 | `cmake --build build --target SessionAudioCellPlaybackTest -j4` (baseline, before any change) | 0 | `build-1-baseline.log` |
| 0 | `ctest -R '^SessionAudioCellPlaybackTest$' --output-on-failure` (in `build/tests`; baseline green, 13 cases) | 0 | `test-1-baseline.log` |
| 1 | `cmake --build build --target SessionAudioCellPlaybackTest -j4` (seam commit, test added, fix NOT yet applied) | 0 | `build-2-red-seam.log` |
| 2 | `ctest -R '^SessionAudioCellPlaybackTest$' --output-on-failure` (in `build/tests`) - RED, without the fix | 8 | `test-2-red-seam.log` |
| 3 | `cmake --build build --target SessionAudioCellPlaybackTest -j4` (with the fix) | 0 | `build-3-green.log` |
| 4 | `ctest -R '^SessionAudioCellPlaybackTest$' --output-on-failure` (in `build/tests`) - GREEN | 0 | `test-3-green.log` |
| 5 | `cmake --build build --target SessionAudioCellPlaybackTest SessionAudioRecorderTest SessionAudioRecordTest SessionPlaybackTest SessionPlaybackRenderTest SessionTransitionsTest SessionSchedulerTest SessionModelTest -j4` | 0 | `build-4-family.log` |
| 6 | `ctest -R '^(SessionAudioCellPlaybackTest\|SessionAudioRecorderTest\|SessionAudioRecordTest\|SessionPlaybackTest\|SessionPlaybackRenderTest\|SessionTransitionsTest\|SessionSchedulerTest\|SessionModelTest)$' --output-on-failure` (in `build/tests`) - 8/8 pass | 0 | `test-4-family.log` |
| 7 | `git diff -- src/core/SessionAudioCells.cpp include/SessionAudioCells.h > .../logs/fix.patch` | 0 | `fix.patch` |
| 8 | `git apply -R .../logs/fix.patch` | 0 | - |
| 9 | `cmake --build build --target SessionAudioCellPlaybackTest -j4` (fix reversed) | 0 | `build-5-red-reversed.log` |
| 10 | `ctest -R '^SessionAudioCellPlaybackTest$' --output-on-failure` (in `build/tests`) - RED again | 8 | `test-5-red-reversed.log` |
| 11 | `git apply .../logs/fix.patch` | 0 | - |
| 12 | `cmake --build build --target SessionAudioCellPlaybackTest -j4` (fix restored) | 0 | `build-6-green-restored.log` |
| 13 | `ctest -R '^SessionAudioCellPlaybackTest$' --output-on-failure` (in `build/tests`) - GREEN again | 0 | `test-6-green-restored.log` |
| 14 | `cmake --build build --target zene -j4` (header changed) | 0 | `build-7-zene.log` |
| 15 | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 0 | `gates-1.log` |

The RED runs' output is the behavioural failure, not a build failure:

```
FAIL!  : SessionAudioCellPlaybackTest::aRateReplacedClipSurvivesTheCloseThatStillNamesIt()
         'sessionaudio::holdsClipForTest(p1.clip)' returned FALSE.
         (the close freed a clip the scheduler's content table can still name)
Totals: 14 passed, 1 failed, 0 skipped, 0 blacklisted
```

The GREEN runs print `100% tests passed out of 1` (`SessionAudioCellPlaybackTest` now has 15 cases including
init/cleanup). The 8-test family run prints `100% tests passed out of 8`.

Gate table (`gates-1.log`, exit 0):

```
PASS  fork-sources / all-sources-reproduce / no-upstream-regression / check-namespace / no-tautology /
      doc-figures / complexity (no new item in your files) / file-length (no new item in your files)
RESULT: PASS
```

## Files changed

- `include/SessionAudioCells.h` (fork-new: contract text, test seam from the earlier commit)
- `src/core/SessionAudioCells.cpp` (fork-new: `stale` parking, `load()`, `retire()`, seam)
- `tests/src/core/SessionAudioCellPlaybackTest.cpp` (regression test)
- `docs/SESSION-AUDIO-RECORD.md` (lifetime contract sentence)
- `docs/qwen-fixes-2026-10/cluster-new-p15-204.md` (this report)

## Self-check

- **Regression test FAILS with the product change reverted and PASSES with it:** yes. Without the fix (step 10)
  `ctest` exited 8 with the assertion above; with it (step 13) exit 0. The pre-fix RED (step 2) is the same
  behavioural failure. The fix-reversal patch is `logs/fix.patch` (steps 7-11), and the test seam lives in the
  earlier commit so the test still compiles when the fix is reversed.
- **Deterministic:** yes. No timing, sleeps or threads; the test injects the engine rate with
  `AudioDevice::setSampleRateForTesting()` and drives the periods and `Song::clearProject()` calls itself, then
  reads the cache's ownership through `sessionaudio::holdsClipForTest()`.
- **Fix at the cause and complete:** yes. `load()` (rate-change retirement) and `retire()` (close retirement) are
  the only two places a clip leaves the cache; both are updated. `load()` has one caller (`prepare()`,
  `src/core/SessionAudioCells.cpp:256`), `retire()` has one product caller (`src/core/Song.cpp:1820`), and the
  target-family `variant()`/`heldClips()` budget and reporting are untouched. `SessionAudioCells.cpp` is a
  fork-new file (`tests/fork-sources.txt:2227`), so no upstream-modifications entry is required.
- **No collateral damage:** the normal path (no rate change) is byte-for-byte the same schedule: held clips are
  moved to `retired` at a close and freed at the next one, as before. No limit or check was added; `heldClips()`
  and the 256-variant budget are unchanged. Old projects load through the same `prepare()` path (the new `stale`
  list is only filled when the output-sample-rate changes). The 8-test session family passed (step 6).
- **Realtime/undo/commands:** no audio-thread code changed; `stale` is model-thread state touched only under the
  engine change lock, and nothing new allocates, frees, locks, waits or logs in a render path. No control command
  was added and no undo step changed.
- **Sibling commands in the command files the diff appears to touch:** this lane changed no file matching
  `ControlCommands*`, `ControlReversibilityTable*` or `ControlRegistry*` (the changed-file list the check sees is
  the train's `base..HEAD` merge diff). The `app.*` / `arrangement.*` ids the ASK lists therefore have no sibling
  relationship to this fix and are unaffected; the fix adds no command and moves no `ControlCommandsSnapshot`
  canary.
