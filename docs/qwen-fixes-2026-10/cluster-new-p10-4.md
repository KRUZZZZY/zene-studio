# Cluster new-p10-4

**Verdict: NEEDS-CONTEXT — baseline mismatch**

The round 1 `FALSE-POSITIVE` verdict was incorrect: it treated the absence of the
feature from this checkout as evidence that the bug did not exist. Luna's finding
is real in `a2f93d3` and the `050/feat-audio-clips-2026-10-07` line. This worktree
is `bugs/qwen-new-p10-4`, with `HEAD` descending from `6d1a114`, before that
feature. The checked-out tree has neither `src/core/ClipAudioLoop.cpp` nor
`tests/src/core/AudioClipLoopTest.cpp`.

On the feature branch, `src/core/Clip.cpp:642-646` loads `loopframes` and
`loopfrom` as signed 64-bit values while only clamping them to nonnegative values;
it also loads `loopoffset` without constraining it. `src/core/ClipAudioLoop.cpp:73-78`
adds the loop start and length in `engaged()`. The addition is repeated in
`fold()` at lines 87-91. `SamplePlayHandle::snapshotAudioLoop()` at lines 184-196
then narrows the sample size, loop bounds, and initial frame to `int`. Two
`LLONG_MAX` bounds therefore reach unsafe signed arithmetic and narrowing.
The existing `AudioClipLoopTest.cpp` on that feature line has no regression for
malformed serialized bounds.

This lane remains scoped to `6d1a114` as requested by its branch/worktree
constraints. No production change or regression test can be made or validated
here without importing the audio-clips feature. The finding should be retargeted
to `050/feat-audio-clips-2026-10-07` for the requested test and fix; it is not a
false positive.

## Evidence

| Check | Exact command | Exit | Result |
|---|---|---:|---|
| Baseline ancestry | `git merge-base --is-ancestor 6d1a114 HEAD` | 0 | The checked-out lane descends from the reviewed baseline. |
| Finding commit on feature line | `git merge-base --is-ancestor a2f93d3 050/feat-audio-clips-2026-10-07` | 0 | The finding commit is present on the audio-clips line. |
| Missing implementation and regression in this checkout | `git ls-files src/core/ClipAudioLoop.cpp tests/src/core/AudioClipLoopTest.cpp` | 0 | No output; neither path is tracked here. |
| Missing loop fields in this checkout | `git grep -n -e audioLoopStart -e audioLoopFrames -e loopfrom -e loopframes -- src/core tests/src/core` | 1 | No matches. |
| Rebuild application | `cmake --build build --target zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-4.r2/logs/build-1.log 2>&1; rc=$?; printf 'EXIT=%d\n' "$rc"; exit "$rc"` | 0 | `zene` target rebuilt successfully. |
| Build affected test target | `cmake --build build --target AudioClipLoopTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-4.r2/logs/build-2.log 2>&1; rc=$?; printf 'EXIT=%d\n' "$rc"; exit "$rc"` | 2 | CMake reports no rule for the absent test target. |
| Enumerate affected tests | `cd build/tests && ctest -N -R AudioClipLoopTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-4.r2/logs/test-list-1.log 2>&1; rc=$?; printf 'EXIT=%d\n' "$rc"; exit "$rc"` | 0 | `Total Tests: 0`; no ctest run was attempted because this lane has no matching test. |
| RED regression | Not run | N/A | The regression source and behavior are absent from this baseline. |
| GREEN regression | Not run | N/A | No fix was applied in this baseline. |
| Fix-reversal RED | Not run | N/A | No production fix exists in this baseline to reverse. |
| Affected-family test count | Not run | N/A | Zero tests are registered for this family in this build. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-4.r2/logs/gate-5.log 2>&1; rc=$?; printf 'EXIT=%d\n' "$rc"; exit "$rc"` | 0 | Passed after the final report edit. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-4.r2/logs/gate-6.log 2>&1; rc=$?; printf 'EXIT=%d\n' "$rc"; exit "$rc"` | 0 | Passed after the final report edit. |

The `fff grep` tool is not exposed in this session and no `fff` executable is on
`PATH`; the numbered logs were searched with `rg` for `error`, `FAILED`, and
`Assertion` instead.

## Files committed

- `docs/qwen-fixes-2026-10/cluster-new-p10-4.md`
