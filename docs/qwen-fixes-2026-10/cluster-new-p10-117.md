# Cluster new-p10-117: FIXED

`fillLfoLevel` previously returned a zeroed block when the block began at or before the LFO pre-delay. It now zeros only samples before the delay boundary, then fills the remaining samples using their original block offsets. The boundary sample is processed, including when pre-delay is zero. The change is in [EnvelopeAndLfoParameters.cpp:260](../../src/core/EnvelopeAndLfoParameters.cpp#L260).

Regression coverage uses the public `fillLevel` path with a zero envelope contribution: [ModulationRateTest.cpp:191](../../tests/src/core/ModulationRateTest.cpp#L191) checks zero pre-delay in the first block, and [ModulationRateTest.cpp:203](../../tests/src/core/ModulationRateTest.cpp#L203) checks a block crossing a nonzero delay. The test source is registered in [fork-sources.txt:1970](../../tests/fork-sources.txt#L1970); the inherited production change is recorded in [upstream-modifications.txt:1054](../../tests/upstream-modifications.txt#L1054).

## Evidence

The common build command was `cmake --build build --target zene ModulationRateTest` from the worktree root. The common test command was `ctest -R ModulationRateTest --output-on-failure` from `build/tests`. Every command's output and exit code was saved in the run log folder.

| Phase | Log | Exit | Result |
|---|---|---:|---|
| Initial test harness build | `build-1.log` | 2 | The test attempted to link the protected inline `fillLfoLevel`; the test was changed to call public `fillLevel`. |
| Original production build | `build-2.log` | 0 | Build succeeded before the first regression run. |
| Original production regression (RED) | `test-1.log` | 8 | The zero-delay assertion failed. The crossing fixture also failed because loading its partial settings reset the LFO amount; that fixture was corrected before the final proof. |
| First fixed-code build | `build-3.log` | 0 | Build succeeded. |
| First fixed-code regression attempt | `test-2.log` | 8 | The zero-delay case passed; the crossing case exposed the fixture issue described above. |
| Corrected fixture build | `build-4.log` | 0 | Build succeeded. |
| Corrected regression (GREEN) | `test-3.log` | 0 | The full `ModulationRateTest` ctest passed. |
| Reverted-fix build | `build-5.log` | 0 | Rebuilt `zene` and `ModulationRateTest` with the production patch reversed. |
| Reverted-fix regression (RED proof) | `test-4.log` | 8 | Both new regression cases failed without the fix. |
| Restored-fix build | `build-6.log` | 0 | Rebuilt `zene` and `ModulationRateTest` after restoring the production patch. |
| Restored-fix regression (GREEN proof) | `test-5.log` | 0 | The full `ModulationRateTest` ctest passed. |
| `bash tests/complexity-gate.sh --check` | `gate-3.log` | 0 | Passed after the final source and manifest edits. |
| `bash tests/file-length-gate.sh --check` | `gate-4.log` | 0 | Passed after the final source and manifest edits. |

The affected ctest family contains one registered test, `ModulationRateTest`; it ran and passed in each GREEN run. The exact revert-proof commands were `git diff -- src/core/EnvelopeAndLfoParameters.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-117.r1/logs/fix.patch` (EXIT=0), `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-117.r1/logs/fix.patch` (EXIT=0), and `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-117.r1/logs/fix.patch` (EXIT=0).

The `fff grep` utility was not exposed in this environment (`command -v fff` found no executable), so matching log lines were inspected with `rg`.

## Files in the commit

- `src/core/EnvelopeAndLfoParameters.cpp`
- `tests/src/core/ModulationRateTest.cpp`
- `tests/fork-sources.txt`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-117.md`
