# Cluster new-p10-446-port: empty ValueBuffer reads

**Verdict: ALREADY-FIXED**

`ValueBuffer::value()` in `src/core/ValueBuffer.cpp:34-40` already returns `0.0f` when the buffer is empty before evaluating `offset % length()`:

```cpp
const int bufferLength = length();
if (bufferLength == 0) { return 0.0f; }
const int index = offset % bufferLength;
return at(index < 0 ? index + bufferLength : index);
```

This guard was introduced by the already-merged `bugs/qwen-new-p9-497` fix (`272883688`, "fix: guard ValueBuffer empty and negative offsets"), which is an ancestor of this branch (`cdf495ceb`). The accepted `bugs/qwen-new-p10-446` change (`9dc7f9789`) added the same empty-buffer guard to the older, unguarded implementation; on this newer line that guard is already present and is strictly stronger because it also normalizes negative offsets. A cleared buffer has `length() == 0`, so `ValueBuffer(1).clear()` is covered as well.

## Evidence

- `src/core/ValueBuffer.cpp:36-39` contains the empty-buffer guard.
- `tests/src/core/ModulationRateTest.cpp:159-163` (`emptyBufferValueIsZero`) exercises the default-constructed empty case.
- `git blame` attributes the guard to commit `272883688`, already merged into this branch.

## Evidence table

| Check | Exact command and result |
|---|---|
| Build | `cmake --build build --target ModulationRateTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-446-port.r1/logs/build-1.log 2>&1; rc=$?; echo EXIT=$rc` from worktree root; **EXIT=0** |
| Test | `ctest -R '^ModulationRateTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-446-port.r1/logs/test-1.log 2>&1; rc=$?; echo EXIT=$rc` from `build/tests`; **EXIT=0**, 1/1 target passed |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-446-port.r1/logs/gate-1.log 2>&1; rc=$?; echo EXIT=$rc` from worktree root; **EXIT=1** (pre-existing ratchet regressions in unrelated files; no change made here) |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-446-port.r1/logs/gate-2.log 2>&1; rc=$?; echo EXIT=$rc` from worktree root; **EXIT=1** (pre-existing ratchet regressions in unrelated files; no change made here) |

## Files committed

- `docs/qwen-fixes-2026-10/cluster-new-p10-446-port.md`
