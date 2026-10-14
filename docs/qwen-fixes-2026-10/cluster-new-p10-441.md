# Cluster new-p10-441: failed VST3 output bus activation

**Verdict: FIXED**

## Evidence

In [Vst3HostReports.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-441/plugins/Vst3Effect/Vst3HostReports.cpp:73), `activateOutputBuses()` now rejects an upper bound beyond the available output buses before calling the component. If a component refuses a bus after earlier buses activated, the method rebuilds `d.layout` from the descriptors that reflect the successful activations before returning `false` (lines 77–94).

The VST3 chunk-probe fixture declares two inactive auxiliary outputs and refuses activation of output bus 2 ([fixture](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-441/tests/data/vst3-chunk-probe/vst3-chunk-probe.cpp:181), refusal at line 228). The regression cases in [Vst3ChunkProbeTest.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-441/tests/src/plugins/Vst3ChunkProbeTest.cpp:159) verify that a successful prefix is reflected in the layout after refusal, and that an out-of-range request leaves inactive buses unchanged.

On the original implementation both cases failed: after refusal the layout remained at 2 output channels instead of 3, and the out-of-range request marked bus 1 active. Removing the production patch reproduced the same two failures.

## Command evidence

Commands below are copied as run. Build commands ran from the worktree root; `ctest` commands ran from `build/tests`. Build and test output is in the numbered log named in each command, under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-441.r1/logs`.

### RED on the original implementation

| Step | Command | EXIT |
|---|---|---:|
| Build regression target | `cmake --build build --target Vst3ChunkProbeTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-441.r1/logs/build-1.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |
| Run regression | `ctest -R '^Vst3ChunkProbeTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-441.r1/logs/test-1.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 8 |

### GREEN with the fix

| Step | Command | EXIT |
|---|---|---:|
| Build regression target | `cmake --build build --target Vst3ChunkProbeTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-441.r1/logs/build-2.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |
| Run regression | `ctest -R '^Vst3ChunkProbeTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-441.r1/logs/test-2.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |

### Patch-removal proof

| Step | Command | EXIT |
|---|---|---:|
| Save and reverse only the production fix | `git diff -- plugins/Vst3Effect/Vst3HostReports.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-441.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-441.r1/logs/fix.patch; result=$?; echo EXIT=$result; exit $result` | 0 |
| Rebuild `zene` without the fix | `cmake --build build --target zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-441.r1/logs/build-3.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |
| Rebuild regression target without the fix | `cmake --build build --target Vst3ChunkProbeTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-441.r1/logs/build-4.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |
| Rerun regression without the fix | `ctest -R '^Vst3ChunkProbeTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-441.r1/logs/test-3.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 8 |
| Reapply the saved fix | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-441.r1/logs/fix.patch; result=$?; echo EXIT=$result; exit $result` | 0 |
| Rebuild `zene` with the fix restored | `cmake --build build --target zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-441.r1/logs/build-5.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |
| Rebuild regression target with the fix restored | `cmake --build build --target Vst3ChunkProbeTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-441.r1/logs/build-6.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |
| Rerun regression with the fix restored | `ctest -R '^Vst3ChunkProbeTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-441.r1/logs/test-4.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |

### VST3 family and gates

The affected family ran **7 tests; all 7 passed**. The final family run was after the test-fixture cleanup.

| Step | Command | EXIT |
|---|---|---:|
| Build VST3 family targets | `cmake --build build --target Vst3BusMapTest Vst3HostTest Vst3ChunkProbeTest Vst3EffectIntegrationTest Vst3InstrumentFixtureProbe Vst3InstrumentTest Vst3InstrumentIntegrationTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-441.r1/logs/build-8.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |
| Run VST3 family | `ctest -R '^Vst3' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-441.r1/logs/test-6.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |
| Rebuild final regression target | `cmake --build build --target Vst3ChunkProbeTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-441.r1/logs/build-9.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |
| Final VST3 family run | `ctest -R '^Vst3' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-441.r1/logs/test-7.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-441.r1/logs/gate-3.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-441.r1/logs/gate-4.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |

## Files committed

- `plugins/Vst3Effect/Vst3HostReports.cpp`
- `tests/data/vst3-chunk-probe/vst3-chunk-probe.cpp`
- `tests/src/plugins/Vst3ChunkProbeTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p10-441.md`
