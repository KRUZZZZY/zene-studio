# Cluster new-p9-108

**Verdict: NEEDS-CONTEXT**

## Finding evidence

The ownership hazard is visible in the source: `CombFeedback` allocates `m_buffer` with `new[]` at `include/Delay.h:76` and releases it at `:81`; its raw pointer member is at `:126`. `AllpassDelay` does the same at `:299`, `:304`, and `:350`. Neither class declares copy construction or copy assignment, so their compiler-generated copy operations would alias the owned buffer.

The focused regression could not reach its assertions. Including `include/Delay.h` in `DeviceDspTest` exposed malformed constructor initializers: the compiler reports missing `m_feedBack` at `include/Delay.h:72`, `:143`, `:214`, and `:295`, and missing `m_delay` / `m_fraction` at `:215-216`. The actual member in the affected public classes is `m_gain` (`:129`, `:353`). The header has no other C++ source includers in this tree, so these errors were not being compiled by the existing build.

Fixing those constructor errors would be an additional production change, with initialization behavior to decide for the dual tap class. I did not combine that separate work with the copy-ownership fix. The attempted regression edit was removed; no product or test source was changed.

## Verification

| Check | Exact command | Result |
|---|---|---|
| RED attempt (build regression target) | `cmake --build build --target zene DeviceDspTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-108.r1/logs/build-1.log 2>&1; echo EXIT=$?` | `EXIT=2`; compilation stopped on the initializer errors above, before a test assertion ran. |
| RED CTest assertion | `cd build/tests && ctest -R '^DeviceDspTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-108.r1/logs/test-1.log 2>&1; echo EXIT=$?` | Not run because the affected target did not build. Affected-family count: **0 tests executed**. |
| GREEN | Not run; the prerequisite compile issue remains unresolved. | N/A |
| 4b rollback proof | Not run; no production fix patch was made. | N/A |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-108.r1/logs/gate-5.log 2>&1; echo EXIT=$?` | `EXIT=0` |
| File length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-108.r1/logs/gate-6.log 2>&1; echo EXIT=$?` | `EXIT=0` |

The `fff` grep tool was unavailable (`command -v fff` returned 1), so logs were searched with `rg`. `rg -n 'error' /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-108.r1/logs/build-1.log` returned 0 and showed the compile errors. Searches for `FAILED` and `Assertion` in that build log returned 1 (no matches). Searches for `error`, `FAILED`, and `Assertion` in the final gate logs `gate-5.log` and `gate-6.log` each returned 1 (no matches).

**Files committed:** `docs/qwen-fixes-2026-10/cluster-new-p9-108.md` only.
