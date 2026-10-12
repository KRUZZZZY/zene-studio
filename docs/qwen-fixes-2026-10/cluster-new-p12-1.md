# Cluster new-p12-1 — FIXED

`ColorChooser::defaultPalette()` previously read Qt's mutable standard colours each time Default was selected. Applying Track or Mixer first replaced those colours, so Default then returned the custom palette.

The array setter now captures the standard colours before its first write. `defaultPalette()` retains and returns that snapshot. The regression sets Track, selects Default, and compares all 48 restored colours with the original palette.

## File evidence

- `src/gui/modals/ColorChooser.cpp:32-39` primes the saved palette before writing any standard colour.
- `src/gui/modals/ColorChooser.cpp:72-84` captures the original Qt standard palette once and returns it on later calls.
- `tests/src/gui/MixerModesTest.cpp:272-284` covers Track followed by Default.

## Verification

Build and gate commands ran from the worktree root. CTest commands ran from `build/tests`. Build, test, and gate output is in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-1.r1/logs`.

| Phase | Exact command | Exit | Log / evidence |
|---|---|---:|---|
| Original build | `cmake --build build --target MixerModesTest` | 0 | `build-2.log` |
| Original behavior RED | `ctest -R '^MixerModesTest$' --output-on-failure` | 8 | `test-1.log`; regression failed at index 0; 13 QtTest cases passed and the new case failed |
| Fixed build | `cmake --build build --target zene MixerModesTest` | 0 | `build-3.log` |
| Fixed behavior GREEN | `ctest -R '^MixerModesTest$' --output-on-failure` | 0 | `test-2.log`; 1/1 affected CTest target passed |
| Apply reverse proof | `git diff -- src/gui/modals/ColorChooser.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-1.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-1.r1/logs/fix.patch` | 0 | Product patch removed |
| Reverted build | `cmake --build build --target zene MixerModesTest` | 0 | `build-4.log` |
| Reverted behavior RED | `ctest -R '^MixerModesTest$' --output-on-failure` | 8 | `test-3.log`; the same assertion failed at index 0 |
| Reapply product patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-1.r1/logs/fix.patch` | 0 | Product patch restored |
| Restored build | `cmake --build build --target zene MixerModesTest` | 0 | `build-5.log` |
| Fixed behavior GREEN | `ctest -R '^MixerModesTest$' --output-on-failure` | 0 | `test-4.log`; 1/1 affected CTest target passed |
| Complexity gate | `bash tests/complexity-gate.sh --check` | 0 | `gate-1.log` |
| File-length gate | `bash tests/file-length-gate.sh --check` | 0 | `gate-2.log` |

The affected test family contains one CTest target (`MixerModesTest`). The RED run executed 14 QtTest cases: 13 passed and the regression failed. It failed with the product patch removed and passed after the patch was reapplied.

## Files committed

- `src/gui/modals/ColorChooser.cpp`
- `tests/src/gui/MixerModesTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p12-1.md`
