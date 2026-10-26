# Cluster new-p9-162

Verdict: **FIXED**

`graphModel::convolve()` wrote `m_samples[( i + centerOffset ) % graphLength]`. C++ `%` keeps the sign of its left operand, so any negative `centerOffset` produced a negative index and a write before the sample array. It now wraps the index into `[0, graphLength)` (`src/gui/widgets/Graph.cpp:663-666`):

```cpp
// '%' keeps the sign of its left operand, so a negative centerOffset
// needs a second modulo to land back in [0, graphLength)
const int target = ( ( i + centerOffset ) % graphLength + graphLength ) % graphLength;
m_samples[target] = sum;
```

**Reachability (stated plainly).** The defect is real in the function's contract but latent in the product. The header documents `centerOffset` only as "Offset for resulting values" (`include/Graph.h:179`), so a negative offset is a valid cyclic shift and the old code was wrong for it. The only production caller is `Xpressive::smooth`, which passes `guass_center = guass_size/2` (`tests/reference/Xpressive/Xpressive.cpp:250-251, 267`). `guass_size = (int)(smoothness*5) | 1` is non-negative because the smoothness models are clamped to 0..70 (`tests/reference/Xpressive/Xpressive.cpp:94-96`). So no shipped path reached the negative branch. The fix is at the cause and does not change any non-negative case.

## Sibling check

- Read side, `Graph.cpp:661` `temp[( i + j ) % graphLength]`: `i >= 0` and `j >= 0` by loop bounds, so it cannot go negative. Unchanged.
- `Graph.cpp:663` was the only write into `m_samples` in `convolve()`. `smooth()` and `smoothNonCyclic()` (`Graph.cpp:617-647`) index with loop bounds only and have no modulo, so they are not affected.
- Only other caller: `Xpressive.cpp:267` (non-negative offset). For non-negative `i + centerOffset`, `((x % N) + N) % N == x % N`, so every existing caller gets the same index as before.
- Header `include/Graph.h:180` is unchanged, so no rebuild of dependents was needed beyond the targets listed below.
- Not checked: `(i + centerOffset)` overflows for a huge positive `centerOffset`. That is pre-existing, no caller comes near it, and it is out of scope for this finding.

## Regression test

Added five slots to the existing `tests/src/gui/EnvelopeGraphTest.cpp` (`convolveZeroOffsetKeepsSamples`, `convolvePositiveOffsetRotatesRight`, `convolveNegativeOffsetRotatesLeft`, `convolveNegativeOffsetEqualToLengthIsZeroShift`, `convolveNegativeOffsetBeyondLengthWraps`). The helper `expectConvolve` runs `graphModel::convolve` with kernel `{2, 0}` over samples `{1, 2, 3, 4}`, so every output slot is `2*input` rotated by the offset. Expected values are hand-computed. Positive and zero offsets pass before and after the fix; the three negative offsets fail before the fix.

Why the kernel is `{2, 0}` and not identity: with the identity kernel, `-length` (`-4`) passed on the unfixed code by coincidence. The out-of-bounds writes left the in-bounds slots equal to the identity result, so the test could not see them. The doubled kernel makes every slot differ from the input, so a missed write shows up as a value mismatch. This was observed on the first RED run (see Evidence).

Deterministic: no clock, sleep, thread, or race. Inputs are fixed.

Why `EnvelopeGraphTest.cpp` and not a new file: a new test file needs `tests/CMakeLists.txt` plus a CMake reconfigure. The lane build tree is already configured and a guard blocks configure (`configure-1.log`, EXIT=97; see Evidence), so a new test target cannot be generated here. The brief's suggested test names do not exist as ctest entries: `cd build/tests && ctest -N -R 'MultitapEcho|DynamicsProcessor|WaveShaper'` lists only `MultitapEchoBoundsTest`, which is unrelated. Those sources appear only in the `PART_C_REFERENCE_MODULES` list (`tests/CMakeLists.txt:3224-3231`), so they are not test executables. The graph-model test was added to the already-built graph test file instead.

## Realtime, undo, commands

- Nothing new on any audio thread. `convolve()` is called from `Xpressive.cpp:184-186` (restoring saved sample graphs) and `Xpressive.cpp:635` (writing into the GUI graph model). Neither is in a render or `process()` path. The change adds one `int` addition and one `int` modulo per sample. It does not allocate, free, lock, wait, log or emit anything new (`emit samplesChanged` is unchanged).
- Undo: not touched. No command, no `ControlRegistry` change, no new control command.

## Files committed

- `src/gui/widgets/Graph.cpp` (the fix, lines 663-666)
- `tests/src/gui/EnvelopeGraphTest.cpp` (helper and five slots)
- `tests/upstream-modifications.txt` (one line for `Graph.cpp`, same format as the existing `new-p9-160` entry)
- `docs/qwen-fixes-2026-10/cluster-new-p9-162.md` (this report)

No new file, so `tests/CMakeLists.txt` and `tests/fork-sources.txt` are unchanged.

## Evidence

Logs: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-162.r1/logs/`. Each command was run from the worktree root unless marked `build/tests`. `cmake --build build ...` used the configured build tree.

| Step | Command | EXIT | Log | Result |
|---|---|---|---|---|
| RED attempt 1 | `cmake --build build --target GraphModelConvolveTest -j4` | 2 | `build-1-red.log` | `No rule to make target`: a new test file cannot be built in a configured tree. |
| Configure attempt | `cmake -S . -B build` | 97 | `configure-1.log` | Blocked by the lane guard: `configuring is the parent's job; the lane build tree is already configured`. Not bypassed. |
| Build attempt | `cmake --build build --target EnvelopeGraphTest -j4` | 2 | `build-3-red.log` | `ccache: failed to create temporary file ... ccache-tmp-r6 ... Permission denied` (ccache temp dir is outside the write-jail). |
| Build (workaround) | `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/zene-tmp/ccache-tmp-new-p9-162 cmake --build build --target EnvelopeGraphTest -j4` | 0 | `build-4-red.log` | Built. Only the ccache temp dir was overridden, through the environment; `ccache.conf` is unchanged. |
| RED run 1 (kernel `{1,0}`, unfixed) | `cd build/tests && ctest -R '^EnvelopeGraphTest$' --output-on-failure` | 8 | `test-1-red.log` | 2 failures: `convolveNegativeOffsetRotatesLeft` (4 vs 1), `convolveNegativeOffsetBeyondLengthWraps` (2 vs 3). `-length` passed by coincidence, which motivated the `{2,0}` kernel. |
| Build (RED, unfixed, final test) | `CCACHE_TEMPDIR=... cmake --build build --target EnvelopeGraphTest -j4` | 0 | `build-5-red.log` | Built. |
| **RED** (unfixed, final test) | `cd build/tests && ctest -R '^EnvelopeGraphTest$' --output-on-failure` | **8** | `test-2-red.log` | `Totals: 5 passed, 3 failed`. `convolveNegativeOffsetRotatesLeft`: Actual 4, Expected 2. `convolveNegativeOffsetEqualToLengthIsZeroShift`: Actual 2, Expected 4. `convolveNegativeOffsetBeyondLengthWraps`: Actual 2, Expected 6. |
| Fix | `Graph.cpp:663-666` edit | - | - | - |
| **GREEN** (fixed) | `CCACHE_TEMPDIR=... cmake --build build --target zene EnvelopeGraphTest -j4` then `cd build/tests && ctest -R '^EnvelopeGraphTest$' --output-on-failure` | build 0 / **test 0** | `build-6-green.log`, `test-3-green.log` | `100% tests passed out of 1`. |
| Proof: save fix | `git diff -- src/gui/widgets/Graph.cpp > .../logs/fix.patch` | 0 | `fix.patch` | Product-file hunk only. |
| Proof: revert fix | `git apply -R .../logs/fix.patch` | 0 | - | `Graph.cpp` back to HEAD (`git diff --stat` empty). |
| Proof: RED | build `zene EnvelopeGraphTest` then ctest | build 0 / **test 8** | `build-7-proof-unfixed.log`, `test-4-proof-unfixed.log` | Same three negative-offset functions fail. |
| Proof: re-apply fix | `git apply .../logs/fix.patch` | 0 | - | Fix restored. |
| Proof: GREEN | build `zene EnvelopeGraphTest` then ctest | build 0 / **test 0** | `build-8-proof-fixed.log`, `test-5-proof-fixed.log` | `100% tests passed out of 1`. |
| Affected family | build `LfoGraphTest ExprSynthTest` (`-j4`) | 0 | `build-9-family.log` | Built. |
| Affected family run | `cd build/tests && ctest -R '^(EnvelopeGraphTest\|LfoGraphTest\|ExprSynthTest)$' --output-on-failure` | 0 | `test-6-family.log` | 3/3 passed: `LfoGraphTest`, `EnvelopeGraphTest`, `ExprSynthTest` (Xpressive, the only `convolve()` caller). |
| Family listing | `cd build/tests && ctest -N -R 'Graph\|Xpressive\|ExprSynth\|Envelope\|Lfo\|Convol\|Waveshap\|WaveShap'` | 0 | `list-1.log` | 14 entries in the family. 3 built and run above. The other 11 have no built executable in this tree and were not run. |
| Gate (as shipped) | `python3 .../qwen/feature_check.py <worktree>` | 1 | `feature-check-1.log` | Crashed before any gate ran: `PermissionError` creating `<worktree>-feature-check` (outside the write-jail). |
| Gate (log folder only redirected) | copy of `feature_check.py` with `logs` set to the LOG folder, run with the same `ROOT` and baseline cache | **0** | `feature-check-2.log` | All rows PASS: fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity ("no new item in your files"), file-length ("no new item in your files"). `RESULT: PASS`. |

The gate copy differs from the shipped script only in where its per-gate logs are written (`ROOT` and `logs` lines). It reads the baseline cache that already exists for the merge-base `cf3618e61`. Its checks and limits are unchanged.

## Self-check

Completion check: `python3 .../qwen/submit_check.py <worktree> new-p9-162` → EXIT 1, log `submit-check-1.log`. PASS: commits on the branch, working tree clean, report present, tests changed. **FAIL: `gates (feature_check.py)`.** Cause: the shipped `feature_check.py` creates `<worktree>-feature-check` (`feature_check.py:70-71`), which is outside this lane's write-jail, so it dies with `PermissionError` before any gate runs (`feature-check-1.log`, EXIT 1). I did not work around this inside the tool. The gate's checks were run with only the log folder redirected and all eight rows PASS, EXIT 0 (`feature-check-2.log`). The shipped gate still needs running where that folder is writable.

1. **RED then GREEN with the fix reverted.** Reverted: `test-4-proof-unfixed.log`, EXIT 8, failing `convolveNegativeOffsetRotatesLeft`, `convolveNegativeOffsetEqualToLengthIsZeroShift`, `convolveNegativeOffsetBeyondLengthWraps`. Restored: `test-5-proof-fixed.log`, EXIT 0, `100% tests passed out of 1`. The zero and positive slots pass in both states; they guard the unchanged behaviour.
2. **Deterministic.** The helper `expectConvolve` (`tests/src/gui/EnvelopeGraphTest.cpp:43-56`) builds a fresh `graphModel` with fixed input `{1,2,3,4}`, kernel `{2,0}`, and fixed offsets. It uses no clock, sleep, thread, or shared state.
3. **Fix at the cause; callers and siblings checked.** The write at `Graph.cpp:663-666` is the only write into `m_samples` in `convolve()`. The read at `Graph.cpp:661` cannot go negative (loop indices are non-negative). `smooth()` and `smoothNonCyclic()` have no modulo. The one production caller, `Xpressive.cpp:267`, passes a non-negative offset, and for `x >= 0` the new expression `((x % N) + N) % N` equals the old `x % N`. No control command is touched, so there are no sibling commands to align.
4. **No collateral damage.** No serialized format or limit changed. Saved projects reach `convolve()` only through `Xpressive::smooth` with a non-negative offset (`Xpressive.cpp:184-186, 635`), so their output is unchanged. The fix adds no check that other callers could hit.
5. **Verdict is FIXED, not ALREADY-FIXED or FALSE-POSITIVE**, so this item is not applicable. For the record, the defect is evidenced by `Graph.cpp:663` before the fix and by the header contract `include/Graph.h:179`. Reachability is evidenced by `Xpressive.cpp:250-251, 267` and the 0..70 clamp at `Xpressive.cpp:94-96`.
