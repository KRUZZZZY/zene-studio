# Cluster new-p10-445-port: VST3 prepare request validation

**Verdict: FIXED**

`HostedPlugin::prepare()` now rejects non-finite or non-positive sample rates and block sizes above 1,048,576 frames. If host buffer preparation throws a standard allocation/size exception after activation, it disables processing, deactivates the component, releases partial buffers, and returns an error.

## File evidence

- `plugins/Vst3Effect/Vst3Host.cpp:65-73` defines the block cap and request predicate.
- `plugins/Vst3Effect/Vst3Host.cpp:471-476` validates the request before calling the plug-in.
- `plugins/Vst3Effect/Vst3Host.cpp:502-555` catches buffer preparation exceptions and unwinds processing and activation.
- `tests/src/plugins/Vst3HostTest.cpp:433-446` covers an oversized block, NaN, and infinity.

## Regression and verification evidence

Commands below ran in the stated working directory. Build and test output was written to new files under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-445-port.r1/logs`.

| Evidence | Exact command | Exit / result |
|---|---|---|
| Final rebuild | `cmake --build build --target zene Vst3HostTest Vst3ChunkProbeTest Vst3InstrumentTest > .../logs/build-3.log 2>&1; echo EXIT=$?` | `EXIT=0` |
| Final GREEN (`build/tests`) | `ctest -R '^Vst3(Host|ChunkProbe|Instrument)Test$' --output-on-failure > .../logs/test-3.log 2>&1; echo EXIT=$?` | `EXIT=0`; 3/3 affected-family tests passed. |
| Reverse-patch application | `git diff -- plugins/Vst3Effect/Vst3Host.cpp > .../logs/fix.patch && git apply -R .../logs/fix.patch; echo EXIT=$?` | `EXIT=0`. |
| Reverse-patch rebuild | `cmake --build build --target zene Vst3HostTest > .../logs/build-2.log 2>&1; echo EXIT=$?` | `EXIT=0`. |
| Reverse-patch RED (`build/tests`) | `ctest -R '^Vst3HostTest$' --output-on-failure > .../logs/test-2.log 2>&1; echo EXIT=$?` | `EXIT=8`; oversized-block assertion failed. |
| Restore production patch | `git apply .../logs/fix.patch; echo EXIT=$?` | `EXIT=0`. |
| Complexity gate | `bash tests/complexity-gate.sh --check > .../logs/gate-1.log 2>&1; echo EXIT=$?` | `EXIT=1`; 6 pre-existing regressions unrelated to this change (none in Vst3Host.cpp). |
| File-length gate | `bash tests/file-length-gate.sh --check > .../logs/gate-2.log 2>&1; echo EXIT=$?` | `EXIT=1`; 4 pre-existing regressions unrelated to this change (none in Vst3Host.cpp). |

## Files committed

- `plugins/Vst3Effect/Vst3Host.cpp`
- `tests/src/plugins/Vst3HostTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-445-port.md`
