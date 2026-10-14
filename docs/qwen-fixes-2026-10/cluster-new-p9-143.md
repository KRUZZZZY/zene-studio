# Cluster new-p9-143 — FIXED

`RandomVectorSeedFunction` now checks each finite float against its destination integer range before converting it. Indexes above `UINT_MAX` return `0`; seeds above `INT_MAX` use seed `0`, matching the existing invalid-input fallbacks. The comparisons use `double` so the integer limits are compared without rounding them to `float` first.

## Evidence

| Location | Evidence |
| --- | --- |
| `plugins/Xpressive/ExprSynth.cpp:322-327` | The index range check precedes the `unsigned int` conversion. |
| `plugins/Xpressive/ExprSynth.cpp:337-341` | The seed range check precedes the `int` conversion. |
| `tests/src/plugins/PluginPortsMigrationTest.cpp:222-279` | Regression renders Xpressive expressions with computed out-of-range index and seed values, then checks the zero-index and seed-zero fallbacks. |
| `tests/upstream-modifications.txt:1054` | Records the inherited production source change and reason. |

The exact test expressions are `randsv(65536*65536,0)` and `randsv(0,32768*65536)`. In the reverse-patch RED run, both fallback checks failed; the zero-expression control stayed zero.

## Validation record

All output is in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-143.r1/logs`. The initial test iterations used direct large decimal tokens and were superseded after the rendered values showed those tokens did not exercise the intended overflow. The final proof below uses the computed values above.

| Check | Exact command | Exit |
| --- | --- | ---: |
| Reverse production patch | `git diff -- plugins/Xpressive/ExprSynth.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-143.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-143.r1/logs/fix.patch > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-143.r1/logs/proof-1.log 2>&1; proof_status=$?; printf 'EXIT=%s\n' "$proof_status" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-143.r1/logs/proof-1.log; exit "$proof_status"` | 0 |
| Build without fix | `cmake --build build --target zene xpressive PluginPortsMigrationTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-143.r1/logs/build-7.log 2>&1; build_status=$?; printf 'EXIT=%s\n' "$build_status" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-143.r1/logs/build-7.log; exit "$build_status"` | 0 |
| RED, no fix | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-143.r1/logs/test-6.log 2>&1; test_status=$?; printf 'EXIT=%s\n' "$test_status" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-143.r1/logs/test-6.log; exit "$test_status"` (run from `build/tests`) | 8 |
| Restore production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-143.r1/logs/fix.patch > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-143.r1/logs/proof-2.log 2>&1; proof_status=$?; printf 'EXIT=%s\n' "$proof_status" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-143.r1/logs/proof-2.log; exit "$proof_status"` | 0 |
| Final build | `cmake --build build --target zene xpressive PluginPortsMigrationTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-143.r1/logs/build-8.log 2>&1; build_status=$?; printf 'EXIT=%s\n' "$build_status" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-143.r1/logs/build-8.log; exit "$build_status"` | 0 |
| GREEN, restored fix | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-143.r1/logs/test-7.log 2>&1; test_status=$?; printf 'EXIT=%s\n' "$test_status" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-143.r1/logs/test-7.log; exit "$test_status"` (run from `build/tests`) | 0 |
| Affected-family count | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure --verbose > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-143.r1/logs/test-8.log 2>&1; test_status=$?; printf 'EXIT=%s\n' "$test_status" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-143.r1/logs/test-8.log; exit "$test_status"` (run from `build/tests`) | 0; 1/1 ctest entry, 9 QtTest cases passed |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-143.r1/logs/gate-1.log 2>&1; gate_status=$?; printf 'EXIT=%s\n' "$gate_status" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-143.r1/logs/gate-1.log; exit "$gate_status"` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-143.r1/logs/gate-2.log 2>&1; gate_status=$?; printf 'EXIT=%s\n' "$gate_status" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-143.r1/logs/gate-2.log; exit "$gate_status"` | 0 |

The ctest family contains one registered test executable. Its verbose QtTest run passed all 9 cases; the no-fix run reported 8 passed and the regression failed.

## Files committed

- `plugins/Xpressive/ExprSynth.cpp`
- `tests/src/plugins/PluginPortsMigrationTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-143.md`
