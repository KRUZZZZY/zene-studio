# Cluster new-p10-86

**Verdict: BLOCKED**

## Finding evidence

The finding is real in the current source. `include/ControlServer.h:238-241` stores one `std::thread` per accepted Windows connection. `src/core/ControlServerWin32.cpp:349-351` appends each worker and says it remains until shutdown; `src/core/ControlServerWin32.cpp:477-488` joins and clears the vector only in `closeWin32()`. Repeated short connections therefore retain completed, joinable thread handles for the server lifetime.

## Why the fix is blocked

The affected implementation is Windows-only (`src/core/ControlServerWin32.cpp:58`), but the existing configured build is Linux (`CMAKE_CXX_COMPILER=/usr/bin/c++`, `CMAKE_GENERATOR=Unix Makefiles`) and has no MinGW Windows headers. The suggested `ControlShutdownHookTest` covers the AF_UNIX transport; its non-Unix branch skips those socket cases (`tests/src/core/ControlShutdownHookTest.cpp:197-198`). There is no Windows named-pipe regression in this test, and a Linux run cannot demonstrate that the Windows worker list is reaped. I did not add a proxy test or alter the Windows lifecycle without a regression that can be run RED and GREEN.

The two required ratchet checks also fail on this clean starting tree, with reported regressions in unrelated files. I did not edit those files or re-anchor either gate because that would exceed this one-bug change.

## Evidence

| Check | Exact command | Result |
|---|---|---|
| RED regression | Not run: no Windows-capable configured test path; no regression test was added. | N/A |
| GREEN regression | Not run for the same platform limitation. | N/A |
| Affected family | `ctest -R '^ControlShutdownHookTest$' --output-on-failure` from `build/tests` was not invoked. | 0 executed; no ctest result claimed |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-86.r1/logs/gate-1.log 2>&1; gate_rc=$?; printf 'EXIT=%s\n' "$gate_rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-86.r1/logs/gate-1.log; exit "$gate_rc"` | EXIT=1; six new over-target functions were reported in unrelated files. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-86.r1/logs/gate-2.log 2>&1; gate_rc=$?; printf 'EXIT=%s\n' "$gate_rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-86.r1/logs/gate-2.log; exit "$gate_rc"` | EXIT=1; fourteen new over-500-line files were reported, unrelated to this finding. |

The fff grep tool was not available in this session. I searched both gate logs with `rg` for `error`, `FAILED`, and `Assertion` (each search exited 1 with no matches), then searched the gate-specific regression diagnostics; the gate failures above are present in those logs.

**Files committed:** `docs/qwen-fixes-2026-10/cluster-new-p10-86.md` only. No product or test file was changed.
