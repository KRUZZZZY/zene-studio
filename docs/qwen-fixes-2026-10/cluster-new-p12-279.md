# cluster new-p12-279: Vectorscope checks widget visibility from audio processing callback

## Verdict: FIXED

## Root cause

`EffectControls::m_viewVisible` was a plain `bool` (`include/EffectControls.h:78`). It is written
by the GUI thread via `setViewVisible()` (called when an effect control dialog opens/closes) and
read by the audio thread via `isViewVisible()` in `Vectorscope::processImpl()` (and similarly in
`EqEffect` and `SpectrumAnalyzer`). This is a data race (undefined behavior in C++).

## Fix

Changed `bool m_viewVisible` to `std::atomic<bool> m_viewVisible` in `include/EffectControls.h`.
Added `#include <atomic>` and `#include <type_traits>`. The implicit atomic load/store from
`operator bool()` and `operator=(bool)` keeps `setViewVisible()` / `isViewVisible()` behavior
unchanged. Added a `LMMS_TESTING`-guarded seam `viewVisibleStorageIsAtomic()` that returns whether
the underlying storage is `std::atomic<bool>`.

## Evidence table

| Step | Command | Exit | Notes |
|------|---------|------|-------|
| RED (original code) | `ctest -R '^EffectControlsVisibilityTest$' --output-on-failure` (build/tests) | 8 | `viewVisibleStorageMustBeAtomic` FAILS; 3 passed, 1 failed |
| GREEN (with fix) | `ctest -R '^EffectControlsVisibilityTest$' --output-on-failure` (build/tests) | 0 | 100% tests passed, 1 test |
| Revert proof | Reverted `std::atomic<bool>` to `bool`, rebuilt, ran test | 8 | FAILS again: `viewVisibleStorageIsAtomic()` returns false |
| Re-fix proof | Re-applied `std::atomic<bool>`, rebuilt, ran test | 0 | PASSES again |
| Complexity gate | `bash tests/complexity-gate.sh --check` | 1 | Pre-existing regressions only (none from this change) |
| File-length gate | `bash tests/file-length-gate.sh --check` | 1 | Pre-existing regressions only (none from this change) |

## Files committed

- `include/EffectControls.h` - changed `bool m_viewVisible` to `std::atomic<bool>`, added test seam
- `tests/src/core/EffectControlsVisibilityTest.cpp` - new regression test (fork-new)
- `tests/CMakeLists.txt` - registered the new test
- `tests/upstream-modifications.txt` - registered EffectControls.h and CMakeLists.txt changes
- `docs/qwen-fixes-2026-10/cluster-new-p12-279.md` - this report

## Build/test logs

All logs in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-279.r1/logs/`:
- `build-2.log` - first successful test build (RED prep)
- `test-1-red.log` - RED: test fails on original code (EXIT=8)
- `build-3-green.log` - build after fix
- `test-2-green.log` - GREEN: test passes with fix (EXIT=0)
- `build-5-revert2.log` - build with fix reverted
- `test-3-revert.log` - revert proof: test fails again (EXIT=8)
- `build-6-refix.log` - build with fix re-applied
- `test-4-refix.log` - re-fix proof: test passes again (EXIT=0)
- `gate-complexity.log` - complexity gate output
- `gate-filelength.log` - file-length gate output
