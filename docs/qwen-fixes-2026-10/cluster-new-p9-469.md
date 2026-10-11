# Cluster new-p9-469: endpoint descriptions can expose credentials

**Verdict: FIXED**

`TelemetryNetworkTransport::describe()` returned the configured URL verbatim for
accepted HTTPS endpoints. A URL's user info, path, query, and fragment can all
contain secrets. The description now formats only the HTTPS origin and port.

## File evidence

| File and lines | Evidence |
| --- | --- |
| `src/core/TelemetryNetworkTransport.cpp:161-178` | `describe()` validates the configured URL and formats it with `RemoveUserInfo`, `RemovePath`, `RemoveQuery`, and `RemoveFragment`. |
| `tests/src/core/TelemetryTransportTest.cpp:168-175` | Regression configures user/password, secret path, token query, and fragment; expects only `https://ingest.example.org:8443`. |

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

## Files committed

- `src/core/TelemetryNetworkTransport.cpp`
- `tests/src/core/TelemetryTransportTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p9-469.md`
