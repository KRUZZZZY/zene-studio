# Cluster new-p12-180-port

Verdict: **FIXED** (port of `bugs/qwen-new-p12-180` onto `050/m0-train-5`, written by hand; no merge, cherry-pick or rebase).

The newest line still had the bug. `src/core/PortMappingPmp.cpp:160` checked only the sender address of a datagram before handing it to the mapper, so a reply from the gateway's IP but a different UDP port was accepted as the gateway's answer. The fix adds `datagram.senderPort() == m_serverPort` to that condition (`src/core/PortMappingPmp.cpp:161-162`).

The regression is `PortMappingCodecTest::wrongPortGatewayRepliesAreIgnored()` (`tests/src/core/PortMappingCodecTest.cpp:244-251`). The fake gateway gets a `WrongPort` mode: the PCP request is refused from the configured port, and the NAT-PMP address and map requests are answered from a second socket bound to another port on the same host (`tests/src/core/PortMappingCodecTest.cpp:70`, `:103`, `:117-118`, `:172-173`). Without the fix the mapper accepts those NAT-PMP answers and reports success. With the fix it sends one PCP try and two unanswered NAT-PMP address tries (3 requests), then fails with "did not answer".

## Verification

`LOGS` = `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-180-port.r1/logs`. Each command is run from the worktree root unless noted. `EXIT` is the unpiped shell exit code.

| Phase | Command | EXIT | Result |
| --- | --- | ---: | --- |
| RED build, attempt 1 | `cmake --build build --target zene PortMappingCodecTest -j2 > $LOGS/build-1.log 2>&1; echo EXIT=$?` | 2 | Environment only: ccache could not write to `/run/user/1000/ccache-tmp` (Permission denied). Not a compile error. |
| RED build, attempt 2 | `CCACHE_TEMPDIR=/tmp/ccache-tmp-p12-180 cmake --build build --target zene PortMappingCodecTest -j2 > $LOGS/build-2.log 2>&1; echo EXIT=$?` | 0 | Built with the new test and the unfixed production code. |
| **RED test** | `cd build/tests && ctest -R PortMappingCodecTest --output-on-failure > $LOGS/test-1.log 2>&1; echo EXIT=$?` | **8** | `FAIL! : PortMappingCodecTest::wrongPortGatewayRepliesAreIgnored() '!result.ok' returned FALSE.` 14 passed, 1 failed. |
| Fix applied, build | `CCACHE_TEMPDIR=/tmp/ccache-tmp-p12-180 cmake --build build --target zene PortMappingCodecTest -j2 > $LOGS/build-3.log 2>&1; echo EXIT=$?` | 0 | |
| GREEN test | `cd build/tests && ctest -R PortMappingCodecTest --output-on-failure > $LOGS/test-2.log 2>&1; echo EXIT=$?` | 0 | 100% of 1 registered test. |
| Proof: save and reverse fix | `git diff -- src/core/PortMappingPmp.cpp > $LOGS/fix.patch && git apply -R $LOGS/fix.patch; echo EXIT=$?` | 0 | |
| Proof: build with fix reversed | `CCACHE_TEMPDIR=/tmp/ccache-tmp-p12-180 cmake --build build --target zene PortMappingCodecTest -j2 > $LOGS/build-4.log 2>&1; echo EXIT=$?` | 0 | |
| **Proof RED** | `cd build/tests && ctest -R PortMappingCodecTest --output-on-failure > $LOGS/test-3.log 2>&1; echo EXIT=$?` | **8** | Same assertion: `'!result.ok' returned FALSE`. |
| Proof: re-apply fix | `git apply $LOGS/fix.patch; echo EXIT=$?` | 0 | |
| Proof: build with fix | `CCACHE_TEMPDIR=/tmp/ccache-tmp-p12-180 cmake --build build --target zene PortMappingCodecTest -j2 > $LOGS/build-5.log 2>&1; echo EXIT=$?` | 0 | |
| **Proof GREEN** | `cd build/tests && ctest -R PortMappingCodecTest --output-on-failure > $LOGS/test-4.log 2>&1; echo EXIT=$?` | **0** | Passed. |
| Family build | `CCACHE_TEMPDIR=/tmp/ccache-tmp-p12-180 cmake --build build --target PortMappingOrchestratorTest -j2 > $LOGS/build-6.log 2>&1; echo EXIT=$?` | 0 | |
| Family test | `cd build/tests && ctest -R PortMapping -V > $LOGS/test-6.log 2>&1; echo EXIT=$?` | 0 | **2/2 ctest entries; 25 test functions executed:** PortMappingCodecTest 15 passed, 0 failed; PortMappingOrchestratorTest 10 passed, 0 failed. |
| Complexity gate (with fix) | `bash tests/complexity-gate.sh --check > $LOGS/gate-1.log 2>&1; echo EXIT=$?` | **1** | 6 regressions, none in the files changed here (see below). |
| File-length gate (with fix) | `bash tests/file-length-gate.sh --check > $LOGS/gate-2.log 2>&1; echo EXIT=$?` | **1** | 14 regressions, none in the files changed here (see below). |
| Complexity gate (base, fix reversed) | `git apply -R $LOGS/full-fix.patch; bash tests/complexity-gate.sh --check > $LOGS/gate-3.log 2>&1; echo EXIT=$?` | **1** | Same 6 regressions as `gate-1.log`. |
| File-length gate (base, fix reversed) | `bash tests/file-length-gate.sh --check > $LOGS/gate-4.log 2>&1; echo EXIT=$?` | **1** | Same 14 regressions as `gate-2.log`. |

Earlier logs `test-5.log` (ctest -V before the orchestrator target was built) and `build-1.log` are superseded: `test-5.log` shows the orchestrator as "Not Run", which was a build gap on my side, not a result. The `test-6.log` run is the one reported above.

## The ratchet gates are red on the base commit too

Both gates exit 1 with the fix and exit 1 with the fix reversed, and the regression sets are identical (`gate-1.log` = `gate-3.log`, `gate-2.log` = `gate-4.log`). The flagged files are not the files in this change: the complexity gate flags `ControlCommandsMixerTools.cpp`, `ProjectRevisions.cpp`, `DawProjectReadTracks.cpp` and others; the file-length gate flags `SmfInterchangeReader.cpp`, `TakeLane.cpp`, `CollabTransportTest.cpp` and others. Neither gate names `PortMappingPmp.cpp` or `PortMappingCodecTest.cpp`.

I did not fix those regressions. They are outside this bug, and clearing them means refactoring about 20 unrelated files across other lanes, which is a separate task. Gate limits and baselines were not touched. The gates need an owner decision or a separate cleanup pass before this line can go green.

## Files committed

- `src/core/PortMappingPmp.cpp`
- `tests/src/core/PortMappingCodecTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p12-180-port.md`

No new source files were added, so `tests/fork-sources.txt` and `tests/upstream-modifications.txt` need no change: `src/core/PortMappingPmp.cpp` and `tests/src/core/PortMappingCodecTest.cpp` are already registered as fork sources (`tests/fork-sources.txt:1737` and `:2338`).
