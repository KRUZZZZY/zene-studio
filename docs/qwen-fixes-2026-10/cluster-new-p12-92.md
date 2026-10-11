# Cluster new-p12-92 — Full-scale meter fill bounds

**Verdict: FIXED**

The finding names `src/gui/InputLevelMeter.cpp`, which is absent from this branch. The active Large Meters dialog in `src/gui/MixerWindows.cpp:97-99` had the same inclusive-boundary error: at full scale, its fill began one row above the bar and stopped before the bar's bottom row. The fix computes the filled height and derives the top from `bar.bottom()` inclusively, keeping the fill inside the bar.

The regression in `tests/src/gui/MixerModesTest.cpp:238-293` opens the real Large Meters dialog, sets the master RMS to full scale immediately before repaint, and checks that the pixel above the bar stays clear while the bottom row is filled.

## Evidence

| Check | Exact command and working directory | Exit | Result |
|---|---|---:|---|
| Reversed production patch build (worktree) | `cmake --build build --target zene MixerModesTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-92.r1/logs/build-5.log 2>&1; status=$?; echo EXIT=$status` | 0 | Rebuilt `zene` and `MixerModesTest` without the fix. |
| RED regression (`build/tests`) | `ctest -R MixerModesTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-92.r1/logs/test-5.log 2>&1; status=$?; echo EXIT=$status` | 8 | 1/1 CTest target failed as expected; diagnostic showed the above-bar pixel was meter color `#ff5468` instead of panel `#17191d`, and the bottom was control `#292d33` instead of meter color. QtTest summary: 13 passed, 1 failed. |
| Restored production patch build (worktree) | `cmake --build build --target zene MixerModesTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-92.r1/logs/build-6.log 2>&1; status=$?; echo EXIT=$status` | 0 | Rebuilt `zene` and the affected test target with the fix. |
| GREEN regression (`build/tests`) | `ctest -R MixerModesTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-92.r1/logs/test-6.log 2>&1; status=$?; echo EXIT=$status` | 0 | 1/1 CTest target passed. The affected family has 14 QtTest cases. |
| Complexity gate (worktree) | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-92.r1/logs/gate-1.log 2>&1; status=$?; echo EXIT=$status` | 0 | Passed. |
| File-length gate (worktree) | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-92.r1/logs/gate-2.log 2>&1; status=$?; echo EXIT=$status` | 0 | Passed. |

The reverse-patch proof used `git diff -- src/gui/MixerWindows.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-92.r1/logs/fix.patch` (exit 0), `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-92.r1/logs/fix.patch` (exit 0), then restored it with `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-92.r1/logs/fix.patch` (exit 0). The RED and GREEN commands above are that same regression with the production patch removed and restored.

## Files committed

- `src/gui/MixerWindows.cpp`
- `tests/src/gui/MixerModesTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p12-92.md`
