# Cluster new-p8-294: public invite reachability

**Verdict: FIXED**

`mappedInviteAddress()` in `src/core/CollabSession.cpp:80-91` returned a router address rejected by `isPublicIPv4()` when scope was `public`. It now refuses that address and reports that the host can use scope `lan` or resolve the upstream NAT. Auto scope still falls back to the LAN address. The deterministic regression is `tests/src/core/CollabPolicyTest.cpp:277-301`.

## Evidence

| Check | Exact command | Result |
| --- | --- | --- |
| Regression against original code (RED) | `ctest -R CollabPolicyTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-294.r1/logs/test-1.log 2>&1; result=$?; echo EXIT=$result; exit $result` | Exit 8; `publicInviteRefusesUnreachableMapping()` failed at `!result.ok` (test line 298). |
| Regression after fix (GREEN) | `ctest -R CollabPolicyTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-294.r1/logs/test-2.log 2>&1; result=$?; echo EXIT=$result; exit $result` | Exit 0. |
| Required patch removal proof (RED) | After removing only `src/core/CollabSession.cpp`, the same regression command wrote `test-6.log`. | Exit 8; the same `!result.ok` assertion failed. |
| Required patch restoration proof (GREEN) | After reapplying only the production patch, the same regression command wrote `test-7.log`. | Exit 0. |
| Affected collaboration family | `ctest -R 'Collab|PortMappingCodecTest' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-294.r1/logs/test-8.log 2>&1; result=$?; echo EXIT=$result; exit $result` | Exit 0; 5/5 tests passed: CollabPolicyTest, CollabTransportTest, PortMappingCodecTest, CollabPanelTest, ControlCollab. |
| Complexity gate | `bash tests/complexity-gate.sh --check` (output in `gate-3.log`) | Exit 0. |
| File-length gate | `bash tests/file-length-gate.sh --check` (output in `gate-4.log`) | Exit 0. |

The regression was also rerun after the final source edit with the patch removed and restored. Patch capture and reverse application exited 0; reapplication exited 0. Both gates passed after the source change. CTest was run from `build/tests`; no test was skipped in the recorded collaboration family run.

## Build and test command record

All commands below exited with the recorded status. Build and test output is in the named log under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-294.r1/logs`.

| Log | Command | Exit |
| --- | --- | --- |
| `build-1.log` | `cmake --build build --target CollabPolicyTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-294.r1/logs/build-1.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |
| `build-2.log` | `cmake --build build --target zene CollabPolicyTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-294.r1/logs/build-2.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |
| `test-1.log` | `ctest -R CollabPolicyTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-294.r1/logs/test-1.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 8 |
| `test-2.log` | `ctest -R CollabPolicyTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-294.r1/logs/test-2.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |
| `build-3.log` | `cmake --build build --target zene CollabPolicyTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-294.r1/logs/build-3.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |
| `test-3.log` | `ctest -R CollabPolicyTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-294.r1/logs/test-3.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 8 |
| `build-4.log` | `cmake --build build --target zene CollabPolicyTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-294.r1/logs/build-4.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |
| `test-4.log` | `ctest -R CollabPolicyTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-294.r1/logs/test-4.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |
| `build-5.log` | `cmake --build build --target zene CollabPolicyTest CollabTransportTest PortMappingCodecTest CollabPanelTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-294.r1/logs/build-5.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |
| `test-5.log` | `ctest -R 'Collab|PortMappingCodecTest' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-294.r1/logs/test-5.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0; 5/5 |
| `build-6.log` | `cmake --build build --target zene CollabPolicyTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-294.r1/logs/build-6.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |
| `test-6.log` | `ctest -R CollabPolicyTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-294.r1/logs/test-6.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 8 |
| `build-7.log` | `cmake --build build --target zene CollabPolicyTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-294.r1/logs/build-7.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |
| `test-7.log` | `ctest -R CollabPolicyTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-294.r1/logs/test-7.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |
| `build-8.log` | `cmake --build build --target zene CollabPolicyTest CollabTransportTest PortMappingCodecTest CollabPanelTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-294.r1/logs/build-8.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |
| `test-8.log` | `ctest -R 'Collab|PortMappingCodecTest' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-294.r1/logs/test-8.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0; 5/5 |

Final regression reversal proof commands:

```sh
git diff -- src/core/CollabSession.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-294.r1/logs/fix.patch; diff_result=$?; git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-294.r1/logs/fix.patch; reverse_result=$?; echo PATCH_CAPTURE_EXIT=$diff_result REVERSE_APPLY_EXIT=$reverse_result; test $diff_result -eq 0 -a $reverse_result -eq 0
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-294.r1/logs/fix.patch; result=$?; echo PATCH_REAPPLY_EXIT=$result; exit $result
```

The first command exited 0 (`PATCH_CAPTURE_EXIT=0 REVERSE_APPLY_EXIT=0`); the second exited 0 (`PATCH_REAPPLY_EXIT=0`).

Final gate command and exits:

```sh
bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-294.r1/logs/gate-3.log 2>&1; complexity_result=$?; echo COMPLEXITY_EXIT=$complexity_result; bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-294.r1/logs/gate-4.log 2>&1; length_result=$?; echo FILE_LENGTH_EXIT=$length_result; test $complexity_result -eq 0 -a $length_result -eq 0
```

Exit 0; both individual gate exits were 0.

## Files committed

- `src/core/CollabSession.cpp`
- `tests/src/core/CollabPolicyTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p8-294.md`

Both C++ paths were already registered in `tests/fork-sources.txt`.
