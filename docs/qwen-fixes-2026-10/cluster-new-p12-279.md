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

## R2 rework (Luna QA r1: REWORK)

The r1 fix and regression were accepted (RED with `bool`, GREEN with `std::atomic<bool>`); the only
rework point was that `tests/src/core/EffectControlsVisibilityTest.cpp` was never registered as a
fork-new source. Registration is now added:

- `tests/fork-sources.txt` - declares the fork-new regression source.
- `tests/all-sources.txt` - the whole-tree manifest's own recipe derives the source, so its entry
  list is updated in sorted place (`DummyPacingTest.cpp` / `EngineRenderBenchmarkTest.cpp`).

Gate 9 (`tests/fork-sources-gate.sh`, which runs `all-sources-reproduce.sh`) is now EXIT 0.

### R2 evidence (same commands, re-run after the bookkeeping)

| Step | Command | Exit | Notes |
|------|---------|------|-------|
| RED build | `cmake --build build --target zene EffectControlsVisibilityTest` | 0 | header member reverted to `bool` (seam kept) |
| RED test | `ctest -R '^EffectControlsVisibilityTest$' --output-on-failure` (build/tests) | 8 | 0%: `viewVisibleStorageIsAtomic()` FALSE |
| GREEN restore | `git apply -R <logs>/fix.patch` then rebuild both targets | 0 | back to `std::atomic<bool>` |
| GREEN test | `ctest -R '^EffectControlsVisibilityTest$' --output-on-failure` (build/tests) | 0 | 1/1 passed |
| Family | `ctest -R 'EffectControlsVisibilityTest\|VstEffectControlsTest\|EqCurveTest\|ReceiverEffectTest\|EffectRackAddTest'` | 0 | 5/5 passed |
| Gate 9 | `bash tests/fork-sources-gate.sh` | 0 | registration + derivation |
| Complexity gate | `bash tests/complexity-gate.sh --check` | 1 | pre-existing, unrelated (0 mentions of EffectControls) |
| File-length gate | `bash tests/file-length-gate.sh --check` | 1 | pre-existing, unrelated (0 mentions of EffectControls) |

## Files committed

- `include/EffectControls.h` - changed `bool m_viewVisible` to `std::atomic<bool>`, added test seam
- `tests/src/core/EffectControlsVisibilityTest.cpp` - new regression test (fork-new)
- `tests/CMakeLists.txt` - registered the new test
- `tests/upstream-modifications.txt` - registered EffectControls.h and CMakeLists.txt changes
- `tests/fork-sources.txt` - registered the fork-new regression source (R2)
- `tests/all-sources.txt` - whole-tree manifest entry for the new source (R2)
- `docs/qwen-fixes-2026-10/cluster-new-p12-279.md` - this report

## R2 build/test logs

All r2 logs in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-279.r2/logs/`:
- `fix.patch` - product-file diff (member `std::atomic<bool>` -> `bool`)
- `build-red.log`, `test-red-1.log` (EXIT 8)
- `build-green-2.log`, `test-green-2.log` (EXIT 0)
- `build-family.log`, `test-family-3.log` (EXIT 0, 5/5)
- `allsources-1.log` (EXIT 0), `forkgate-1.log` / `gate-forksources-final.log` (EXIT 0)
- `gate-complexity-final.log` (EXIT 1), `gate-filelength-final.log` (EXIT 1)

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
