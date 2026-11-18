# Cluster new-p9-339: FIXED

## Evidence

- `include/Piano.h:53-56`: `Piano::isKeyPressed()` checks `isValidKey(key)` before reading `m_pressedKeys` and returns false for invalid keys.
- `src/core/Piano.cpp:126-139`: `isBlackKey()` rejects keys outside `[0, NumKeys)`. `isWhiteKey()` also rejects them, so invalid keys are classified as neither black nor white.
- `tests/src/gui/PianoRollBenchmarkTest.cpp:118-127`: `invalidKeysAreSafe()` checks `-1` and `NumKeys` for both public key classifiers and the pressed-state query. Its first assertion checks `isBlackKey(NumKeys)`, which deterministically fails before the fix because `128 % 12 == 8` maps to a black key.
- The inherited production changes are registered in `tests/upstream-modifications.txt` with the regression reference.

## Regression and verification

The final regression was run against the original production code by reversing only the product patch. It failed at `PianoRollBenchmarkTest.cpp:121` with `!Piano::isBlackKey(NumKeys)` returned false. Reapplying the same patch made the test pass.

The affected CTest family contains one registered test. The final run executed `PianoRollBenchmarkTest` (1/1 passed; zero tests were not run).

All command exit codes below were captured without a pipeline. Build and CTest output is in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-339.r1/logs`.

| Run | Exact command | Exit |
|---|---|---:|
| Initial build | `cmake --build build --target zene PianoRollBenchmarkTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-339.r1/logs/build-1.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit "$result"` | 0 |
| Initial RED | `ctest -R '^PianoRollBenchmarkTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-339.r1/logs/test-1.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit "$result"` | 8 |
| First fixed build | `cmake --build build --target zene PianoRollBenchmarkTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-339.r1/logs/build-2.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit "$result"` | 0 |
| First GREEN | `ctest -R '^PianoRollBenchmarkTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-339.r1/logs/test-2.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit "$result"` | 0 |
| Earlier reverse-proof build | `cmake --build build --target zene PianoRollBenchmarkTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-339.r1/logs/build-3.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit "$result"` | 0 |
| Earlier reverse-proof RED | `ctest -R '^PianoRollBenchmarkTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-339.r1/logs/test-3.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit "$result"` | 8 |
| Earlier restored build | `cmake --build build --target zene PianoRollBenchmarkTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-339.r1/logs/build-4.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit "$result"` | 0 |
| Earlier restored GREEN | `ctest -R '^PianoRollBenchmarkTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-339.r1/logs/test-4.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit "$result"` | 0 |
| Final deterministic RED build | `cmake --build build --target zene PianoRollBenchmarkTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-339.r1/logs/build-5.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit "$result"` | 0 |
| Final deterministic RED | `ctest -R '^PianoRollBenchmarkTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-339.r1/logs/test-5.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit "$result"` | 8 |
| Restored fix build | `cmake --build build --target zene PianoRollBenchmarkTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-339.r1/logs/build-6.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit "$result"` | 0 |
| Final GREEN | `ctest -R '^PianoRollBenchmarkTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-339.r1/logs/test-6.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit "$result"` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-339.r1/logs/gate-3.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit "$result"` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-339.r1/logs/gate-4.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit "$result"` | 0 |

The final reverse-patch and restore commands both exited 0:

```sh
git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-339.r1/logs/fix.patch; result=$?; printf 'EXIT=%s\n' "$result"; exit "$result"
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-339.r1/logs/fix.patch; result=$?; printf 'EXIT=%s\n' "$result"; exit "$result"
```

The patch file was created from only the two product files:

```sh
git diff -- include/Piano.h src/core/Piano.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-339.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-339.r1/logs/fix.patch; result=$?; printf 'EXIT=%s\n' "$result"; exit "$result"
```

## Files committed

- `include/Piano.h`
- `src/core/Piano.cpp`
- `tests/src/gui/PianoRollBenchmarkTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-339.md`
