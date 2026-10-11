# Cluster new-p9-469: endpoint descriptions can expose credentials

**Verdict: FIXED**

`TelemetryNetworkTransport::describe()` originally returned the configured URL
verbatim for accepted HTTPS endpoints. Round 1 fixed that description, but the
QA review found `telemetry.status` still returned the raw configured endpoint.
Round 2 adds a shared formatter that keeps only the URL scheme, host, and port;
both `describe()` and `telemetry.status` now use it.

## File evidence

| File and lines | Evidence |
| --- | --- |
| `src/core/TelemetryNetworkTransport.cpp:170-185` | `describe()` validates the configured URL and uses the shared redacted formatter. |
| `tests/src/core/TelemetryTransportTest.cpp:168-175` | Regression configures user/password, secret path, token query, and fragment; expects only `https://ingest.example.org:8443`. |
| `include/TelemetryNetworkTransport.h:87-88`, `src/core/TelemetryNetworkTransport.cpp:113-119` | Shared `redactedEndpoint()` formatter removes user info, path, query, and fragment. |
| `src/core/ControlCommandsTelemetry.cpp:177-182` | `telemetry.status` still evaluates policy against the configured endpoint, but reports the redacted origin. |
| `tests/src/core/ControlRegistryTest.cpp:491-515` | Command-level regression sets a credential-bearing configured endpoint, invokes `telemetry.status`, and verifies serialized response secrets are absent. |

## Verification evidence

CTest commands below ran from
`/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-469/build/tests`.
Build and gate commands ran from the worktree root.

| Evidence | Exact command | Exit |
| --- | --- | ---: |
| Initial RED: new regression against original product code | `ctest -R '^TelemetryTransportTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-469.r1/logs/test-1.log 2>&1; result=$?; echo EXIT=$result` | 8 |
| Initial build | `cmake --build build --target TelemetryTransportTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-469.r1/logs/build-1.log 2>&1; result=$?; echo EXIT=$result` | 0 |
| Initial GREEN build, including `zene` | `cmake --build build --target TelemetryTransportTest zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-469.r1/logs/build-2.log 2>&1; result=$?; echo EXIT=$result` | 0 |
| Initial GREEN test | `ctest -R '^TelemetryTransportTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-469.r1/logs/test-2.log 2>&1; result=$?; echo EXIT=$result` | 0 |
| Save the production fix and reverse it for sensitivity proof | `git diff -- src/core/TelemetryNetworkTransport.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-469.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-469.r1/logs/fix.patch; result=$?; echo EXIT=$result` | 0 |
| Reversed-fix rebuild, including `zene` | `cmake --build build --target TelemetryTransportTest zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-469.r1/logs/build-3.log 2>&1; result=$?; echo EXIT=$result` | 0 |
| Sensitivity RED: same regression with production fix reversed | `ctest -R '^TelemetryTransportTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-469.r1/logs/test-3.log 2>&1; result=$?; echo EXIT=$result` | 8 |
| Reapply production fix | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-469.r1/logs/fix.patch; result=$?; echo EXIT=$result` | 0 |
| Final rebuild, including `zene` | `cmake --build build --target TelemetryTransportTest zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-469.r1/logs/build-4.log 2>&1; result=$?; echo EXIT=$result` | 0 |
| Final GREEN test | `ctest -R '^TelemetryTransportTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-469.r1/logs/test-4.log 2>&1; result=$?; echo EXIT=$result` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-469.r1/logs/gate-1.log 2>&1; result=$?; echo EXIT=$result` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-469.r1/logs/gate-2.log 2>&1; result=$?; echo EXIT=$result` | 0 |

The affected CTest family selected and executed **1 test** (`TelemetryTransportTest`);
the final run passed 1/1. Both RED runs failed in
`endpointDescriptionRedactsCredentials()` with the complete configured URL in
the actual value. The reversed-fix run confirms this test catches the bug.

## Round 1 files committed

- `src/core/TelemetryNetworkTransport.cpp`
- `tests/src/core/TelemetryTransportTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p9-469.md`

## Round 2: status response leak

The initial status-command regression failed against the unchanged handler. The
same regression also failed after reversing the round-2 product patch and
rebuilding; after reapplying the patch and rebuilding, both affected CTest
targets passed. The ctest family contains **2 tests** (`ControlRegistryTest`
and `TelemetryTransportTest`), both passing in the final run.

| Evidence | Exact command | Exit |
| --- | --- | ---: |
| Regression build before fix | `cmake --build build --target ControlRegistryTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-469.r2/logs/build-1.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |
| Initial RED: status command with configured credentials | `ctest -R '^ControlRegistryTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-469.r2/test-1.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 8 |
| Build fixed app and affected tests | `cmake --build build --target zene ControlRegistryTest TelemetryTransportTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-469.r2/logs/build-2.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |
| GREEN: affected test family | `ctest -R '^(ControlRegistryTest|TelemetryTransportTest)$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-469.r2/logs/test-2.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |
| Save and reverse only the round-2 product patch | `git diff -- include/TelemetryNetworkTransport.h src/core/TelemetryNetworkTransport.cpp src/core/ControlCommandsTelemetry.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-469.r2/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-469.r2/logs/fix.patch; result=$?; echo EXIT=$result; exit $result` | 0 |
| Rebuild with product patch reversed | `cmake --build build --target zene ControlRegistryTest TelemetryTransportTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-469.r2/logs/build-3.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |
| Sensitivity RED: same status regression with fix reversed | `ctest -R '^ControlRegistryTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-469.r2/logs/test-3.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 8 |
| Reapply the round-2 product patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-469.r2/logs/fix.patch; result=$?; echo EXIT=$result; exit $result` | 0 |
| Final affected-target rebuild | `cmake --build build --target zene ControlRegistryTest TelemetryTransportTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-469.r2/logs/build-4.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |
| Final GREEN: affected test family, 2/2 passed | `ctest -R '^(ControlRegistryTest|TelemetryTransportTest)$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-469.r2/logs/test-4.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-469.r2/logs/gate-3.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-469.r2/logs/gate-4.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |

The initial RED output was moved from `new-p9-469.r2/test-1.log` to the
designated log folder as `new-p9-469.r2/logs/test-1.log` before inspection.

## Round 2 files committed

- `include/TelemetryNetworkTransport.h`
- `src/core/ControlCommandsTelemetry.cpp`
- `src/core/TelemetryNetworkTransport.cpp`
- `tests/src/core/ControlRegistryTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-469.md`
