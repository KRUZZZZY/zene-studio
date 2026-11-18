# Cluster new-p10-216-port

Verdict: **FIXED**

Both range overloads now detect signed endpoint pairs whose subtraction is not representable in the endpoint type. Those cases calculate the random offset in `long double`, while representable ranges retain the original path. The checks and fallback paths are in [lmms_math.h](../../include/lmms_math.h#L119) (overflow helper), [the exclusive overload](../../include/lmms_math.h#L129), and [the inclusive overload](../../include/lmms_math.h#L216). Both overloads now use `decltype(from + (to - from))` as their return type so short endpoints keep the promoted `int` result; the overflow fallback and normal path explicitly return that type.

The new tests are [FastRandExtremeExclusiveRangeTest](../../tests/src/core/MathTest.cpp#L35), [FastRandExtremeInclusiveRangeTest](../../tests/src/core/MathTest.cpp#L42) and [FastRandNarrowRangeReturnTypeTest](../../tests/src/core/MathTest.cpp#L50). The first two assert that the deterministic first samples for `INT_MIN` to `INT_MAX` do not collapse to `INT_MIN`. The inherited-file reasons are recorded in [upstream-modifications.txt](../../tests/upstream-modifications.txt).

## Evidence

Commands ran from the worktree unless stated. Build and test output is in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-216-port.r1/logs`.

| Stage | Exact command | Exit |
| --- | --- | ---: |
| Build regression target with fix | `cmake --build build --target MathTest` | 0 |
| GREEN with fix, from `build/tests` | `ctest -R '^MathTest$' --output-on-failure` | 0 |
| Remove fix for sensitivity check | `git diff -- include/lmms_math.h > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-216-port.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-216-port.r1/logs/fix.patch` | 0 |
| Rebuild without fix | `cmake --build build --target MathTest` | 0 |
| RED without fix, from `build/tests` | `ctest -R '^MathTest$' --output-on-failure` | 8 |
| Reapply saved fix | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-216-port.r1/logs/fix.patch` | 0 |
| Rebuild with fix restored (zene + MathTest) | `cmake --build build --target zene MathTest` | 0 |
| GREEN after reapplying fix, from `build/tests` | `ctest -R '^MathTest$' --output-on-failure` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check` | 1 |
| File-length gate | `bash tests/file-length-gate.sh --check` | 1 |
| Whitespace check | `git diff --check` | 0 |

The complexity and file-length gates exit 1 because of pre-existing ratchet debt on this branch (over-target functions and over-500-line files that are not baselined); my change introduced no new regressions in either gate. Build of dependents `waveshaper` and `peakcontrollereffect` succeeded (exit 0).

Log files: `build-1.log` (baseline build), `test-1.log` (baseline test), `build-2.log` (without fix), `test-2.log` (RED), `build-3.log` (reapplied build), `test-3.log` (GREEN), `build-5.log`/`build-6.log` (dependents), `gate-1.log` (complexity), `gate-2.log` (file-length).

## Files committed

- `include/lmms_math.h`
- `tests/src/core/MathTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-216-port.md`
