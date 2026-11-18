# Cluster new-p8-97

Verdict: **FIXED**

## Finding and change

`src/core/DawProjectWrite.cpp:140` now checks clip and note endpoints using the tick values obtained by formatting each field to six decimals and converting it through `dawProjectTicksFromBeats()`. Since each operand is independently validated as nonnegative and in range, `startTicks <= max(tick_t) - durationTicks` rejects an overflowing endpoint without performing the overflowing addition. The checks are applied to clips at line 171 and notes at line 186.

The existing 50,000,000-beat and serialization-rounding checks remain in `tests/src/core/DawProjectInterchangeRoundTripTest.cpp:1342`. New independent clip and note endpoint regressions are at lines 1370 and 1385; each uses a start that rounds to `tick_t` max and a one-tick duration.

## Evidence

| Check | Exact command | Exit and result |
|---|---|---|
| Initial RED build | `cmake --build build --target DawProjectInterchangeRoundTripTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-97.r2/logs/build-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | 0 |
| Initial RED CTest, from `build/tests` | `ctest -R DawProjectInterchangeRoundTripTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-97.r2/logs/test-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | 8; the new clip endpoint assertion failed before the product change |
| Remove only product patch | `git diff -- src/core/DawProjectWrite.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-97.r2/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-97.r2/logs/fix.patch; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | 0 |
| Proof RED build | `cmake --build build --target DawProjectInterchangeRoundTripTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-97.r2/logs/build-10.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | 0 |
| Proof RED CTest, from `build/tests` | `ctest -R DawProjectInterchangeRoundTripTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-97.r2/logs/test-9.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | 8; both independent endpoint slots failed, proving each regression catches the bug |
| Restore product patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-97.r2/logs/fix.patch; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | 0 |
| Final GREEN build | `cmake --build build --target DawProjectInterchangeRoundTripTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-97.r2/logs/build-11.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | 0 |
| Final GREEN CTest, from `build/tests` | `ctest -R DawProjectInterchangeRoundTripTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-97.r2/logs/test-10.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | 0; **1/1 CTest target executed and passed** |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-97.r2/logs/complexity-3.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-97.r2/logs/file-length-4.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | 0 |

## Files committed

- `src/core/DawProjectWrite.cpp`
- `tests/src/core/DawProjectInterchangeRoundTripTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p8-97.md`
