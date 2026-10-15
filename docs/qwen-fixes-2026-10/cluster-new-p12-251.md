# Cluster new-p12-251: SlicerT fade timing with playback speed

**Verdict: FIXED**

`SlicerT::playNoteImpl` computed the remaining fade frames as `framesLeft * speedRatio`, but `framesLeft` is a source-sample distance while `fadeOutFrames` and the loop index are in output frames, so the remaining distance must be divided by the playback speed. The fade now uses `framesLeft / speedRatio - i`, so the fade tail is no longer shifted when BPM sync or pitch changes the speed.

Evidence:

- `plugins/SlicerT/SlicerT.cpp:158` uses `framesLeft / speedRatio - static_cast<int>(i)`.
- `tests/src/core/SlicerTPlayheadTest.cpp:300` adds `fadeTimingStaysCorrectWhenSpeedDiffers`, which renders a pitch-shifted (speed 0.5) slice and checks that a frame about 1000 output frames before the tail is still (near) full amplitude because the corrected fade window has not started.

## Evidence table

| Check | Exact command and result |
| --- | --- |
| RED after introducing regression test on original buggy code | `git apply -R .../fix.patch` then `cmake --build build --target SlicerTPlayheadTest` and `ctest -R '^SlicerTPlayheadTest$' --output-on-failure` from `build/tests`; test log `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-251.r1/logs/test-red.log`; **EXIT=8** (`amplitude > 0.3f` failed, actual `0.284713`). |
| GREEN after fix | `cmake --build build --target SlicerTPlayheadTest` then ctest from `build/tests`; log `test-green.log`; **EXIT=0**, 1/1 test target passed. |
| Reversed-fix proof | `git apply -R .../fix.patch` (**EXIT=0**); rebuild (**EXIT=0**); ctest output `test-red.log` (**EXIT=8**). |
| Restored-fix proof | `git apply .../fix.patch` (**EXIT=0**); rebuild (**EXIT=0**); ctest output `test-green.log` (**EXIT=0**). |
| Affected family | `SlicerTPlayheadTest`: 1 CTest target executed; 1/1 passed on GREEN runs. |
| Complexity gate | `bash tests/complexity-gate.sh --check`; log `complexity.log` (run with `--check` on this branch); **EXIT=1** (pre-existing branch ratchet entries unrelated to this change: `dotCentreAlong`, `parseNotes`, `adjust`, `setStrip`, `rotateProjectRevision`, `importedOverrides`). |
| File-length gate | `bash tests/file-length-gate.sh --check`; log `filelen.log` (run with `--check` on this branch); **EXIT=1** (pre-existing branch ratchet entries unrelated to this change: `RackMacrosTest`, `RevisionTimelineTest`, `VideoTrackTest`, `ArrangerTimelineTest`). |

## Build and test commands

```text
cmake --build build --target zene slicert SlicerTPlayheadTest > .../build-green.log 2>&1; rc=$?; echo EXIT=$rc  # EXIT=0
ctest -R '^SlicerTPlayheadTest$' --output-on-failure > .../test-green.log 2>&1; rc=$?; echo EXIT=$rc  # EXIT=0
```

Commits: fix + regression test `tests/src/core/SlicerTPlayheadTest.cpp`, product `plugins/SlicerT/SlicerT.cpp`, note `tests/upstream-modifications.txt`.
