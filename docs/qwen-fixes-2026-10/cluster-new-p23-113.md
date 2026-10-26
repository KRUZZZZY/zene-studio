FIXED

## Decision and code evidence

The finding is REAL. In `plugins/SfzPlayer/sfizz/src/sfizz/simd/HelpersSSE.cpp:130`, the scalar prelude must align every pointer used by the following aligned loads. The old condition omitted `divisor`, while line 134 calls `_mm_load_ps(divisor)`. With aligned input/output and a divisor at a four-byte offset, the old code entered that load and faulted on the tested x86_64 SSE2 build.

The caller in `plugins/SfzPlayer/sfizz/src/sfizz/SIMDHelpers.cpp:237-240` forwards `divide<float>` through the SIMD dispatcher; the x86 dispatcher can select `divideSSE` when the operation is enabled and SSE is available. The regression calls the helper directly to force this exact implementation.

The sibling scan found the same missing-array condition in `multiplyAddSSE` and `multiplyMulSSE`: both use aligned loads of `gain` at lines 154 and 197. Their guards now include `gain`. `gainSSE` already included its gain array; the scalar-gain variants load only input and output arrays. Interleaved helpers also check every pointer they load/store in their aligned loop.

## Regression matrix

| Case | Deterministic input and expected result |
|---|---|
| Normal vector path | 16-byte aligned input, divisor and output; eight exact quotients. |
| Finding edge case | Aligned input/output and `divisorStorage.data() + 1`; seven values cross an SSE-sized boundary plus scalar tail, with the untouched sentinel checked. |
| Sibling gain paths | Aligned input/output and gain at `data() + 1` for `multiplyAddSSE` and `multiplyMulSSE`; expected values and untouched sentinels checked. |
| Existing alignment fallbacks | Unaligned input or output with aligned remaining arrays; scalar results checked. |
| Scalar boundary cases | Misaligned divisor with size three and size zero; written range and sentinel checked. |
| Failure, exhaustion, reconfiguration | Not applicable: these stateless arithmetic helpers have no resource lifecycle or configuration state. Zero-length and partial-vector boundaries are covered. |
| Audio-thread constraints | Production changes only add pointer-alignment checks before scalar work; they allocate/free nothing, lock, wait, log, or perform I/O. The test's child-process synchronization runs only in the test executable. |
| Platform build | Test links the existing `sfzplayer_sfizz` target when available. The tested configuration is x86_64 SSE2; platforms without SSE2 compile the helper's scalar implementation. No cross-platform build was run. |

The test uses fixed `alignas(16)` arrays and `data() + 1` offsets. Each unsafe helper call runs in a child process, and the parent asserts normal child exit; this turns an aligned-load fault into an explicit Qt test failure without sleeps or timed assertions.

## Verification evidence

The logs below are new numbered files in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-113.r1/logs`.

| Check | Exact command and result |
|---|---|
| Reverse fix for proof | `git diff -- plugins/SfzPlayer/sfizz/src/sfizz/simd/HelpersSSE.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-113.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-113.r1/logs/fix.patch; code=$?; echo EXIT=$code` — `EXIT=0`. |
| RED with all product hunks reversed | From `build/tests`: `ctest -R SfzSimdHelperAlignmentTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-113.r1/logs/test-10.log 2>&1; code=$?; echo EXIT=$code` — CTest `EXIT=8`; its one registered test reports three failed assertions, each child receiving SIGSEGV. |
| Restore fix | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-113.r1/logs/fix.patch; code=$?; echo EXIT=$code` — `EXIT=0`. |
| Build restored code | From the worktree root: `cmake --build build --target zene SfzPlayerTest SfzPlayerRealtimeTest SfzSimdHelperAlignmentTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-113.r1/logs/build-12.log 2>&1; code=$?; echo EXIT=$code` — `EXIT=0`. |
| GREEN after restore | From `build/tests`: `ctest -R SfzSimdHelperAlignmentTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-113.r1/logs/test-11.log 2>&1; code=$?; echo EXIT=$code` — CTest `EXIT=0`, 1/1 test passed. |
| Affected test family | From `build/tests`: `ctest -R 'SfzPlayer|SfzSimdHelperAlignmentTest' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-113.r1/logs/test-12.log 2>&1; code=$?; echo EXIT=$code` — CTest `EXIT=0`, 3/3 tests passed. |
| Feature gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p23-113 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-113.r1/logs/gate-3.log 2>&1; code=$?; echo EXIT=$code` — `EXIT=0`; all eight rows pass. The stale whole-tree manifest was rederived from its own recipe, removing 1,148 excluded sfizz entries and adding this test.
| Completion check | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p23-113 new-p23-113 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-113.r1/logs/completion-1.log 2>&1; code=$?; echo EXIT=$code` — `EXIT=0`; all mechanical checks pass and the five ASK items are answered below. |

The regression source is registered in `tests/CMakeLists.txt`, `tests/fork-sources.txt`, and the recipe-derived `tests/all-sources.txt`. The feature gate reports PASS for fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity, and file-length.

## Files committed

- `plugins/SfzPlayer/sfizz/src/sfizz/simd/HelpersSSE.cpp`
- `tests/src/plugins/SfzSimdHelperAlignmentTest.cpp`
- `tests/CMakeLists.txt`
- `tests/fork-sources.txt`
- `tests/all-sources.txt`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p23-113.md`

## Self-check

1. **Reverted test fails and restored test passes:** `test-10.log` records CTest exit 8 with all three unaligned-operand probes faulting; after reapplying the captured three-hunk product patch, `test-11.log` records CTest exit 0. The exact commands are listed above.
2. **Deterministic test:** fixed stack arrays establish alignment and four-byte offsets directly. Child process exit synchronizes fault detection; there are no sleeps, time measurements, races, or timeouts.
3. **Cause and siblings:** `divideSSE`, `multiplyAddSSE`, and `multiplyMulSSE` now check all array operands before aligned loads. `gainSSE`, scalar-gain siblings, interleaved helpers, and the `divide<float>` dispatch path were inspected as described above.
4. **Collateral behavior:** no project format, saved data, command, limit, or serialization behavior changes. Unaligned operands use the existing scalar arithmetic and preserve output values; aligned operands keep the existing SSE path.
5. **Verdict:** FIXED, supported by the code locations and RED/GREEN evidence above.
