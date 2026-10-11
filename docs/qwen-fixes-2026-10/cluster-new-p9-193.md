# Cluster new-p9-193: cubic interpolation changes a constant signal’s level

**Verdict: FIXED**

`include/interpolation.h:65-71` used an amplitude-dependent value as part of the interpolation coordinate. The replacement evaluates the four-point Catmull–Rom cubic in Horner form. `plugins/Vibed/VibratingString.cpp:101-106` is the production caller. The regression in `tests/src/core/AudioResamplerRatioTest.cpp:133-143` checks a constant input at five positions from 0 through 1.

On the original implementation, the regression failed at `x=0.25`: output `1.28092` for constant input `1`. After the fix it passed. The same assertion failed again when the product patch was reversed, then passed after it was reapplied.

## Evidence

All build and test output was redirected to the named log under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-193.r1/logs`. CTest commands ran from `build/tests`.

| Phase | Exact command | Exit | Evidence |
|---|---|---:|---|
| Test build before fix | `cmake --build build --target AudioResamplerRatioTest` | 0 | `build-1.log` |
| RED before fix | `ctest -R '^AudioResamplerRatioTest$' --output-on-failure` | 8 | `test-red-1.log`; 8 passed, 1 failed with output 1.28092 at x=0.25 |
| Named-target build after fix | `cmake --build build --target zene AudioResamplerRatioTest vibedstrings slicert granularpitchshifter monstro frequencyshifter partc_ref_vibedstrings partc_ref_monstro partc_ref_frequencyshifter partc_ref_granularpitchshifter` | 0 | `build-3.log` |
| GREEN after fix | `ctest -R '^AudioResamplerRatioTest$' --output-on-failure` | 0 | `test-green-1.log`; affected family ran 1 CTest entry, 1 passed |
| Negative control: reverse header patch | `git diff -- include/interpolation.h > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-193.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-193.r1/logs/fix.patch` | 0 | Patch reversed |
| Rebuild without fix | `cmake --build build --target AudioResamplerRatioTest` | 0 | `build-reverse-1.log` |
| Negative-control RED | `ctest -R '^AudioResamplerRatioTest$' --output-on-failure` | 8 | `test-reverse-1.log`; same assertion and 1.28092 at x=0.25 |
| Reapply header patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-193.r1/logs/fix.patch` | 0 | Patch reapplied |
| Rebuild with fix restored | `cmake --build build --target zene AudioResamplerRatioTest vibedstrings slicert granularpitchshifter monstro frequencyshifter partc_ref_vibedstrings partc_ref_monstro partc_ref_frequencyshifter partc_ref_granularpitchshifter` | 0 | `build-reapply-1.log` |
| Negative-control GREEN | `ctest -R '^AudioResamplerRatioTest$' --output-on-failure` | 0 | `test-reapply-1.log`; 1/1 CTest passed |
| Complexity gate | `bash tests/complexity-gate.sh --check` | 0 | `complexity-gate-1.log` |
| File-length gate | `bash tests/file-length-gate.sh --check` | 0 | `file-length-gate-1.log` |

An unqualified `cmake --build build` returned 97 because the local build guard requires named targets; the successful named-target builds above rebuilt `zene`, the test, affected plugins, and reference targets.

## Files committed

- `include/interpolation.h`
- `tests/src/core/AudioResamplerRatioTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-193.md`
