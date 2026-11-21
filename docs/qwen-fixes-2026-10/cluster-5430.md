FIXED

## Decision and evidence

Decision: REAL. `track.version_switch` checkpoints the Track at
`src/core/ControlCommandsTrackVersions.cpp:113-114`. Undo and redo reach
`ProjectJournal::restoreStep()` / `restoreState()` at
`src/core/ProjectJournal.cpp:75-106`, which invokes `jo->restoreState()` without
the project-load lock. Normal `Track::loadSettings()` calls
`Track::loadTrack(..., false)` at `src/core/Track.cpp:892-895`. Before this fix,
that method released its change guard immediately after `deleteClips()`; its
later `claimClipOrPreserveChild()` call at `src/core/Track.cpp:823` creates and
restores clips in the helper at lines 182-193, and `Track::addClip()` pushes
them into `m_clips` at lines 905-907. Audio traversal holds the same mutex in
`AudioEngine::renderNextPeriod()` at `src/core/AudioEngine.cpp:919-923`.

`Track::loadTrack()` now holds the change guard from clip deletion through
reconstruction, restoration, and post-load clip adjustments
(`src/core/Track.cpp:773-777` onward). Project loading already holds the outer
change lock from `src/core/Song.cpp:1892-2094`; nesting is safe because the
engine mutex is recursive (`include/AudioEngine.h:758`). Preset loads return
before the guarded clip path. Serialization and saved project data are
unchanged.

The regression `ArrangeToolsTest::undoAndRedoOfTrackVersionSwitchKeepModelGuardThroughClipRestore`
(`tests/src/core/ArrangeToolsTest.cpp:233-263`) performs a real version switch,
then undo and redo. Its custom SampleTrack/Clip probes the engine change mutex
inside `createClip()` and `restoreState()`. The test stops device processing,
checks the mutex with a synchronous `try_lock` probe, and uses no sleeps or
timing thresholds.

## Build and regression evidence

The selected CTest family contains one registered test, `ArrangeToolsTest`
(CTest #264). The red runs each executed 1/1 CTest test and failed at the undo
clip-creation assertion; the green runs each executed 1/1 and passed.

| Phase | Exact command | Exit | Evidence log |
| --- | --- | ---: | --- |
| Initial build | `cmake --build build --target zene ArrangeToolsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5430.r2/logs/build-1.log 2>&1` | 0 | Both targets built. |
| Initial red | `ctest -R '^ArrangeToolsTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5430.r2/logs/test-1.log 2>&1` (run from `build/tests`) | 8 | New undo probe failed; 19 passed, 1 failed. |
| Fixed build | `cmake --build build --target zene ArrangeToolsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5430.r2/logs/build-2.log 2>&1` | 0 | Both targets built. |
| Green | `ctest -R '^ArrangeToolsTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5430.r2/logs/test-2.log 2>&1` (run from `build/tests`) | 0 | 1/1 CTest test passed. |
| Reverse product patch | `git diff -- src/core/Track.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5430.r2/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5430.r2/logs/fix.patch` | 0 | Product guard reverted; test sources stayed intact. |
| Revert-proof build | `cmake --build build --target zene ArrangeToolsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5430.r2/logs/build-3.log 2>&1` | 0 | Both targets built without the guard. |
| Revert-proof red | `ctest -R '^ArrangeToolsTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5430.r2/logs/test-3.log 2>&1` (run from `build/tests`) | 8 | Same undo clip-creation assertion failed. |
| Restore product patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5430.r2/logs/fix.patch` | 0 | Guard restored. |
| Final build | `cmake --build build --target zene ArrangeToolsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5430.r2/logs/build-4.log 2>&1` | 0 | Both targets built. |
| Final green | `ctest -R '^ArrangeToolsTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5430.r2/logs/test-4.log 2>&1` (run from `build/tests`) | 0 | 1/1 CTest test passed. |
| Feature gate | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-5430 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5430.r2/logs/gate-2.log 2>&1` | 0 | PASS; all gates passed, with no new complexity or file-length failures. |
| Completion check | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-5430 5430 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5430.r2/logs/submit-1.log 2>&1` | 0 | PASS; commits, clean tree, report, changed test, and gates all passed. |

## Self-check

1. **Regression fails reverted and passes restored:** `test-1.log` and the
   required product-patch reversal run `test-3.log` both show the undo
   clip-creation probe failing (CTest exit 8). With the guard restored,
   `test-2.log` and `test-4.log` show 1/1 passing (CTest exit 0).
2. **Determinism:** the test stops audio-device processing, then synchronously
   checks the same change mutex with a `try_lock` worker and promise/future at
   clip creation and restoration. It has no sleeps, timers, or injected time.
3. **Cause and sibling paths:** switch checkpoints the Track; journal undo/redo
   call the Track restore path directly. Both operations now share
   `Track::loadTrack()`'s guard. The normal `loadSettings()` caller uses that
   path, while preset loads return before it; project loads retain their outer
   lock and safely nest the recursive mutex. The existing direct version
   replacement guard remains covered by
   `replacingTrackVersionKeepsModelGuardThroughClipRestore`.
4. **Collateral behavior:** the change only extends the control-side guard
   around the normal track-load walk. It does not change XML or saved data,
   project-load lock ownership, or the preset path, so old projects keep the
   same serialized loading behavior. No new limit or check was added.
5. **ALREADY-FIXED / FALSE-POSITIVE evidence:** not applicable; the verdict is
   FIXED. The real unguarded journal path and the fix are identified above.

## Files committed

- `src/core/Track.cpp`
- `tests/src/core/ArrangeToolsTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-5430.md`
