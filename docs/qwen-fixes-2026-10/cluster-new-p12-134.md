# Cluster new-p12-134: controller lanes on MIDI clip edits

## Verdict: BLOCKED

The controller-lane fix is implemented and the regression passes. The required complexity and file-length gates both exit 1 on diagnostics that name files outside this change set, so the repository gate requirement remains unmet.

## Finding and code evidence

- `src/gui/clips/MidiClipView.cpp:80` and `:93`: `controllerPrefix` and `controllerSegment` retain lane points in a tick range and use `ControllerLane::valueAt` to preserve the interpolated value at each edit boundary.
- `src/gui/clips/MidiClipView.cpp:449` and `:485`: `clearNotesOutOfBounds` crops the clone's lanes to the visible source range and rebases them to tick zero.
- `src/gui/clips/MidiClipView.cpp:979` and `:1040`: `destructiveSplitClip` ends the left lane at the split value and starts the right lane there, rebasing the remaining points.
- `tests/src/gui/MidiClipLinkedEditTest.cpp:247` and `:290`: regressions cover the tick-48 split and an offset trim with interpolated boundary values.

Before the fix, both regressions failed at their controller-point assertions: the split retained its point at tick 96 instead of adding tick 48, and the trim retained value 0 instead of interpolating value 32. The family passed after the fix, and failed with the same assertions when the production patch was reversed.

## Evidence

Build and gate commands ran from the worktree root. CTest commands ran from `build/tests`.

| Check | Exact command | Exit and result |
| --- | --- | --- |
| Initial test build | `cmake --build build --target MidiClipLinkedEditTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-134.r1/logs/build-1.log 2>&1; rc=$?; echo EXIT=$rc` | `0` |
| Rebuild `zene` before RED | `cmake --build build --target zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-134.r1/logs/build-2.log 2>&1; rc=$?; echo EXIT=$rc` | `0` |
| RED | `ctest -R '^MidiClipLinkedEditTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-134.r1/logs/test-1.log 2>&1; rc=$?; echo EXIT=$rc` | `8`; 12 passed, 2 regression assertions failed |
| GREEN build | `cmake --build build --target zene MidiClipLinkedEditTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-134.r1/logs/build-3.log 2>&1; rc=$?; echo EXIT=$rc` | `0` |
| GREEN | `ctest -R '^MidiClipLinkedEditTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-134.r1/logs/test-2.log 2>&1; rc=$?; echo EXIT=$rc` | `0`; 1/1 registered CTest test passed |
| Save and reverse production patch | `git diff -- src/gui/clips/MidiClipView.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-134.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-134.r1/logs/fix.patch; rc=$?; echo EXIT=$rc` | `0` |
| Rebuild without fix | `cmake --build build --target zene MidiClipLinkedEditTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-134.r1/logs/build-4.log 2>&1; rc=$?; echo EXIT=$rc` | `0` |
| RED with production patch reversed | `ctest -R '^MidiClipLinkedEditTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-134.r1/logs/test-3.log 2>&1; rc=$?; echo EXIT=$rc` | `8`; same 2 assertions failed, 12 passed |
| Reapply production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-134.r1/logs/fix.patch; rc=$?; echo EXIT=$rc` | `0` |
| Rebuild after reapplying | `cmake --build build --target zene MidiClipLinkedEditTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-134.r1/logs/build-5.log 2>&1; rc=$?; echo EXIT=$rc` | `0` |
| GREEN after reapplying | `ctest -R '^MidiClipLinkedEditTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-134.r1/logs/test-4.log 2>&1; rc=$?; echo EXIT=$rc` | `0` |
| Affected-family count | `ctest -R '^MidiClipLinkedEditTest$' --output-on-failure -V > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-134.r1/logs/test-5.log 2>&1; rc=$?; echo EXIT=$rc` | `0`; 14 QtTest cases passed, 0 failed; CTest ran 1 registered test |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-134.r1/logs/gate-1.log 2>&1; rc=$?; echo EXIT=$rc` | `1`; six reported over-target functions are in other files |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-134.r1/logs/gate-2.log 2>&1; rc=$?; echo EXIT=$rc` | `1`; fourteen reported new over-500-line files are outside this change set |

The requested `fff grep` tool was not exposed in this session and no `fff` executable was available. Build and test logs were searched with `rg` for `error`, `FAILED`, and `Assertion`, plus the test totals and regression names.

## Files committed

- `src/gui/clips/MidiClipView.cpp`
- `tests/src/gui/MidiClipLinkedEditTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p12-134.md`
