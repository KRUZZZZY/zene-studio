# Cluster new-p12-1 — FIXED

`ColorChooser::defaultPalette()` saves Qt's original standard colors before the first Track or Mixer write and returns that saved palette for Default. The regression now reads the actual `QColorDialog::standardColor(i)` values after Track then Default. It restores the original global colors before its first assertion, then compares the observed values against the snapshot.

## File evidence

- `src/gui/modals/ColorChooser.cpp:32-39` captures the default palette before writing custom standard colors.
- `src/gui/modals/ColorChooser.cpp:73-84` stores the original 48 Qt colors once and returns that snapshot.
- `tests/src/gui/MixerModesTest.cpp:272-293` captures, exercises Track then Default, reads the actual Qt standard colors, restores the originals, and compares.
- `tests/upstream-modifications.txt:1054` tracks the production change and its regression.

## Verification

Build commands ran from the worktree root. CTest commands ran from `build/tests`. All build, test, and gate output is in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-1.r2/logs`.

| Phase | Exact command | Exit | Result |
|---|---|---:|---|
| Build updated test with fix | `set +e; cmake --build build --target zene MixerModesTest > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-1.r2/logs/build-1.log' 2>&1; rc=$?; printf 'EXIT=%d\n' "$rc"; exit "$rc"` | 0 | App and test target built |
| First GREEN | `set +e; ctest -R '^MixerModesTest$' --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-1.r2/logs/test-1.log' 2>&1; rc=$?; printf 'EXIT=%d\n' "$rc"; exit "$rc"` | 0 | 1/1 affected CTest target passed |
| Extract production-only patch | `git diff HEAD^ HEAD -- src/gui/modals/ColorChooser.cpp > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-1.r2/logs/fix.patch' && wc -l '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-1.r2/logs/fix.patch'` | 0 | 36-line production patch |
| Reverse production patch | `git apply -R '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-1.r2/logs/fix.patch'` | 0 | Only `ColorChooser.cpp` was reverted |
| Build without fix | `set +e; cmake --build build --target zene MixerModesTest > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-1.r2/logs/build-2.log' 2>&1; rc=$?; printf 'EXIT=%d\n' "$rc"; exit "$rc"` | 0 | App and test target rebuilt |
| Reverse RED | `set +e; ctest -R '^MixerModesTest$' --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-1.r2/logs/test-2.log' 2>&1; rc=$?; printf 'EXIT=%d\n' "$rc"; exit "$rc"` | 8 | 13 QtTest cases passed; regression failed at index 0 (`#ffd45858` vs original `#ff000000`) |
| Reapply production patch | `git apply '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-1.r2/logs/fix.patch'` | 0 | Original fix restored |
| Rebuild with fix | `set +e; cmake --build build --target zene MixerModesTest > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-1.r2/logs/build-3.log' 2>&1; rc=$?; printf 'EXIT=%d\n' "$rc"; exit "$rc"` | 0 | App and test target rebuilt |
| Final GREEN | `set +e; ctest -R '^MixerModesTest$' --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-1.r2/logs/test-3.log' 2>&1; rc=$?; printf 'EXIT=%d\n' "$rc"; exit "$rc"` | 0 | 1/1 affected CTest target passed |
| Complexity gate | `set +e; bash tests/complexity-gate.sh --check > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-1.r2/logs/gate-3.log' 2>&1; rc=$?; printf 'EXIT=%d\n' "$rc"; exit "$rc"` | 0 | Passed |
| File-length gate | `set +e; bash tests/file-length-gate.sh --check > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-1.r2/logs/gate-4.log' 2>&1; rc=$?; printf 'EXIT=%d\n' "$rc"; exit "$rc"` | 0 | Passed |

The affected test family contains one CTest target (`MixerModesTest`). The updated regression failed with the production-only patch removed and passed after it was restored.

## Files committed in the round-two QA correction

- `tests/src/gui/MixerModesTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p12-1.md`

The production fix and its upstream-modification registration remain in the preceding commit `f68e2d324`.
