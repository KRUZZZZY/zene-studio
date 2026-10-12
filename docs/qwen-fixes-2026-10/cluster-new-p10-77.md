# Cluster new-p10-77: NaN render metrics

**Verdict: BLOCKED**

## Evidence

At `src/core/ControlCommandsWasmRender.cpp:107-114`, `renderJson` passes each
sample through `std::max(peak, fabs(sample))` and accumulates its square. A NaN
does not replace the current peak, while its square makes RMS NaN. Lines
`123-124` insert those metrics without reporting how many output samples were
non-finite. The finding is supported by the source, but no production change
was made because this worktree's configured build cannot build or run the WASM
path.

`build/CMakeCache.txt:767,770` has `Wasmtime_INCLUDE_DIR` and
`Wasmtime_LIBRARY` set to `NOTFOUND`. The generated
`build/src/CMakeFiles/lmmsobjs.dir/build.make` has no
`ControlCommandsWasmRender.cpp` rule, and `build/tests/CTestTestfile.cmake` has
no `WasmSandboxTest` registration. The attempted target build confirms the
target is absent. The regression test could not be run RED, so there is no
verified fix or GREEN result to report.

## Verification record

| Check | Command | Result |
|---|---|---|
| RED setup / affected target build | `cmake --build build --target WasmSandboxTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-77.r1/logs/build-1.log 2>&1; result=$?; echo EXIT=$result` | `EXIT=2` (`No rule to make target 'WasmSandboxTest'`) |
| RED regression test | Not run; the target could not be built | No result |
| GREEN regression test | Not run; no production fix was applied | No result |
| Affected-family tests executed | No ctest command was run because the target build failed | `0` |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-77.r1/logs/gate-complexity-2.log 2>&1; result=$?; echo EXIT=$result` | `EXIT=0` |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-77.r1/logs/gate-filelen-2.log 2>&1; result=$?; echo EXIT=$result` | `EXIT=0` |

The requested `fff grep` log tool was unavailable in this session. The logs
were searched with `rg`: `error`, `FAILED`, and `Assertion` returned no
matches; searching for `No rule` in `build-1.log` found the missing-target
message above.

## Files committed

- `docs/qwen-fixes-2026-10/cluster-new-p10-77.md` (this BLOCKED report only)
