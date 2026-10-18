# Cluster new-p10-330-port: remote message argument count

**Verdict: FIXED.** Re-applied by hand on 050/m0-train-5 (socket transport). The finding is real:
`receiveMessage()` (socket branch) and `readFrameLocked()` (shared-memory branch) used the
wire-provided argument count as an unbounded loop limit. Added `MaxRemoteMessageArguments`
(=(512*1024-8)/4) and reject negative or over-limit counts, invalidating the transport and
returning an undefined message. Also applied the same bound to `receiveStartedFrame()`.

## Evidence

Commands ran in the worktree; ctest commands ran from `build/tests`.

| Stage | Exact command | Exit |
| --- | --- | ---: |
| Initial build | `cmake --build build --target zene RemotePluginAudioPortsTest > .../logs/build-1.log 2>&1` | 0 |
| RED, original code | `ctest -R '^RemotePluginAudioPortsTest$' --output-on-failure > .../logs/test-red.log 2>&1` | 8 |
| Build fixed code | `cmake --build build --target zene RemotePluginAudioPortsTest > .../logs/build-final.log 2>&1` | 0 |
| GREEN, fixed code | `ctest -R '^RemotePluginAudioPortsTest$' --output-on-failure > .../logs/test-green2.log 2>&1` | 0 |
| Reverse production patch | `git diff -- src/common/RemotePluginBase.cpp > .../logs/fix.patch && git apply -R .../logs/fix.patch` | 0 |
| Rebuild without fix | `cmake --build build --target zene RemotePluginAudioPortsTest > .../logs/build-rev.log 2>&1` | 0 |
| RED, reversed fix | `ctest -R '^RemotePluginAudioPortsTest$' --output-on-failure > .../logs/test-red.log 2>&1` | 8 |
| Restore production patch | `git apply .../logs/fix.patch` | 0 |
| Rebuild restored fix | `cmake --build build --target zene RemotePluginAudioPortsTest > .../logs/build-restored.log 2>&1` | 0 |
| GREEN, restored fix | `ctest -R '^RemotePluginAudioPortsTest$' --output-on-failure > .../logs/test-green2.log 2>&1` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > .../logs/gate-cur.log 2>&1` | 1 (pre-existing regressions; none introduced by this change) |
| File length gate | `bash tests/file-length-gate.sh --check > .../logs/gate-fl2.log 2>&1` | 1 (pre-existing regressions; none introduced by this change) |

Red run: `RemotePluginAudioPortsTest::oversizedArgumentCountInvalidatesRemoteMessage()` failed
with actual message id 25 where `IdUndefined` (0) was expected. Green run: 17 QtTest cases passed.

Files in the change:
- `src/common/RemotePluginBase.cpp`
- `tests/src/core/RemotePluginAudioPortsTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-330-port.md`
