FIXED

Swapped multiply-add gives the wrong result for aliased buffers. The aliased
right channel used the already-updated left sample, so one frame `(1, 2)` at
coefficient `0.5` produced `3` rather than `2.5`.

## Evidence

- `src/core/MixHelpers.cpp:133-137` before the fix:
  `dst[0] += src[1] * m_coeff; dst[1] += src[0] * m_coeff;`
  With `dst == src` (aliased, `run()` passes both by reference at
  `src/core/MixHelpers.cpp:42-47`), `dst[1]` reads the updated `dst[0]`.
- Callers are `src/core/RingBuffer.cpp:365,372,374`
  (`RingBuffer::writeSwappedAddingMultiplied`); caching both source channels
  is behaviour-preserving for disjoint buffers and correct for aliased ones.

## Fix

Cache both source channels in a swapped temporary before writing either
destination channel (`src/core/MixHelpers.cpp:135-136`):

```cpp
const SampleFrame swapped = { src[1], src[0] };
dst += swapped * m_coeff;
```

No allocation on the audio thread (one small value temporary), no locks,
waits, or logging. Zero-frame remains a no-op through `run()`.

## Regression test

`tests/src/core/AudioBufferTest.cpp` (`AudioBufferTest` target):

- `AddSwappedMultiplied_AliasedBufferUsesOriginalLeftChannel` — aliased
  `(1, 2)` at `0.5` must give `(2, 2.5)` (RED: actual right `3`).
- `AddSwappedMultiplied_DisjointBuffersUseScaledOppositeChannel` — disjoint
  multi-frame source/destination with distinct initial values.
- `AddSwappedMultiplied_ZeroFramesIsNoOp` — zero-frame no-op.

## Evidence table

| Step | Command (workdir `build/tests` for ctest) | Exit |
|---|---|---|
| Build (RED) | `cmake --build build --target AudioBufferTest > <LOGS>/build-01-red.log 2>&1` | 0 |
| Test RED | `ctest -R '^AudioBufferTest$' --output-on-failure > <LOGS>/test-01-red.log 2>&1` | 8 |
| Build (GREEN) | `cmake --build build --target AudioBufferTest > <LOGS>/build-02-green.log 2>&1` | 0 |
| Test GREEN | `ctest -R '^AudioBufferTest$' --output-on-failure > <LOGS>/test-02-green.log 2>&1` | 0 |
| Save patch | `git diff -- src/core/MixHelpers.cpp > <LOGS>/fix.patch` | 0 |
| Reverse patch | `git apply -R <LOGS>/fix.patch` | 0 |
| Build (reversed) | `cmake --build build --target AudioBufferTest > <LOGS>/build-03-reverse.log 2>&1` | 0 |
| Test (reversed, RED) | `ctest -R '^AudioBufferTest$' --output-on-failure > <LOGS>/test-03-reverse.log 2>&1` | 8 |
| Re-apply patch | `git apply <LOGS>/fix.patch` | 0 |
| Build (forward) | `cmake --build build --target AudioBufferTest > <LOGS>/build-04-forward.log 2>&1` | 0 |
| Test (forward, GREEN) | `ctest -R '^AudioBufferTest$' --output-on-failure > <LOGS>/test-04-forward.log 2>&1` | 0 |
| Gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > <LOGS>/gates-01.log 2>&1` | 0 |

RED assertion (test-01-red.log):
`FAIL! AddSwappedMultiplied_AliasedBufferUsesOriginalLeftChannel() Actual (buffer[0].right()): 3 Expected (2.5f): 2.5`

GREEN: 1/1 ctest passed; `./AudioBufferTest` shows all three new tests PASS.
The same 1 test fails on the reversed (original) helper and passes after
re-applying the patch.

Affected family: `AudioBufferTest` only (1 ctest entry, 2 passed/failed
counted as 1 test). No other ctest family covers MixHelpers.

Logs: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-190.r3/logs/`

Files committed:

- `src/core/MixHelpers.cpp` (product fix)
- `tests/src/core/AudioBufferTest.cpp` (regression tests)
- `tests/upstream-modifications.txt` (registration, inherited files)
- `docs/qwen-fixes-2026-10/cluster-new-p14-190.md` (this report)

## Self-check

- Realtime: the fix is two float values into a stack temporary and one
  `operator+=`/`operator*`; no heap, locks, or logging on the audio path.
  Allocation probe: N/A (no probe harness; the op is allocation-free by
  construction).
- Undo/commands: not touched; no new control commands.
- Saved data: no format change; disjoint-buffer results are bit-identical
  to before (verified by the disjoint test values).
