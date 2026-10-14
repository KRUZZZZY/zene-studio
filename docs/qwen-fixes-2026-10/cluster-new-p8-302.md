# Cluster new-p8-302

Verdict: **FIXED**

The finding is real. In `src/core/CollabClient.cpp:241-253`, a rejected streamed command previously advanced `m_seq` before the command sink returned and kept `m_haveSnapshot` true while asking the host to resync. A following command could therefore be applied to the diverged project. The client now invalidates the snapshot before notifying/requesting resync and commits the command sequence only after the sink succeeds.

The regression in `tests/src/core/CollabTransportTest.cpp:159-195` makes the rejecting sink queue a second command before returning failure. It checks that the failed command did not advance the sequence, that the queued command was ignored, and that the replacement snapshot restores the host sequence.

## Verification evidence

All build and test output was captured under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-302.r1/logs`.

| Check | Exact command | Exit | Result |
| --- | --- | ---: | --- |
| RED build | `cmake --build build --target CollabTransportTest` (build-2.log) | 0 | Regression compiled against the original implementation. |
| RED test | From `build/tests`: `ctest -R '^CollabTransportTest$' --output-on-failure` (test-2.log) | 8 | Failed as expected: sequence was 1 instead of 0 and `track.wait` was also attempted. |
| GREEN build | `cmake --build build --target zene CollabTransportTest` (build-3.log) | 0 | Rebuilt the application and affected test target. |
| GREEN test | From `build/tests`: `ctest -R '^CollabTransportTest$' --output-on-failure` (test-3.log) | 0 | 1/1 registered CTest target passed. The family contains 12 Qt test cases. |
| Revert proof | `git diff -- src/core/CollabClient.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-302.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-302.r1/logs/fix.patch` (proof-apply-1.log) | 0 | Reversed only the production fix. |
| Revert build | `cmake --build build --target zene CollabTransportTest` (build-4.log) | 0 | Built with the regression still present and production fix reverted. |
| Revert test | From `build/tests`: `ctest -R '^CollabTransportTest$' --output-on-failure` (test-4.log) | 8 | Failed with the same sequence and stale-command evidence. |
| Restore fix | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-302.r1/logs/fix.patch` (proof-apply-2.log) | 0 | Restored the production patch. |
| Final build | `cmake --build build --target zene CollabTransportTest` (build-5.log) | 0 | Rebuilt both targets after restoring the fix. |
| Final test | From `build/tests`: `ctest -R '^CollabTransportTest$' --output-on-failure` (test-5.log) | 0 | 1/1 registered CTest target passed. |
| Complexity gate | `bash tests/complexity-gate.sh --check` (gate-1.log) | 0 | Passed. |
| File length gate | `bash tests/file-length-gate.sh --check` (gate-2.log) | 0 | Passed. |

## Files committed

- `src/core/CollabClient.cpp`
- `tests/src/core/CollabTransportTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p8-302.md`
