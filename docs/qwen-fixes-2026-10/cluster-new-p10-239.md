# Cluster new-p10-239: ALSA Raw-MIDI cleanup after worker exit

**Verdict: FIXED**

## Evidence

- [`src/core/midi/MidiAlsaRaw.cpp:36`](../../src/core/midi/MidiAlsaRaw.cpp#L36) initializes ALSA handles, the descriptor count, and the poll descriptor pointer, so cleanup is safe even if opening the device fails partway through.
- [`src/core/midi/MidiAlsaRaw.cpp:65`](../../src/core/midi/MidiAlsaRaw.cpp#L65) now stops and joins a running worker, then closes each initialized ALSA handle and deletes the poll descriptor array regardless of worker state.
- [`tests/src/core/MidiOutQueueTest.cpp:155`](../../tests/src/core/MidiOutQueueTest.cpp#L155) supplies fake ALSA handles and a ready pipe. The poll-descriptor callback returns an error, the worker exits, and the regression checks that both handles are closed by destruction.
- RED evidence: `test-1.log` and `test-3.log` show `g_closedHandles` was 0 instead of 2. GREEN evidence: `test-2.log` and `test-4.log` show the registered test target passed.

## Commands and results

The test commands ran from `build/tests`; build and gate logs are under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-239.r1/logs`.

| Run | Exact command | Exit | Result |
| --- | --- | ---: | --- |
| RED, original production code | `ctest -R MidiOutQueueTest --output-on-failure` | 8 | Failed as expected: 0 handle closes, expected 2. |
| GREEN, fixed production code | `ctest -R MidiOutQueueTest --output-on-failure` | 0 | 1/1 registered CTest target passed; the target contains 4 QtTest slots. |
| Mutation proof, revert production patch | `git diff -- src/core/midi/MidiAlsaRaw.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-239.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-239.r1/logs/fix.patch` | 0 | Reverted only the production file patch. |
| Mutation proof, rebuild without fix | `cmake --build build --target zene MidiOutQueueTest` | 0 | Both targets rebuilt. |
| Mutation proof, RED without fix | `ctest -R MidiOutQueueTest --output-on-failure` | 8 | Failed again with 0 handle closes, expected 2. |
| Mutation proof, restore production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-239.r1/logs/fix.patch` | 0 | Restored the exact saved product patch. |
| Mutation proof, rebuild with fix | `cmake --build build --target zene MidiOutQueueTest` | 0 | Both targets rebuilt. |
| Mutation proof, GREEN with fix | `ctest -R MidiOutQueueTest --output-on-failure` | 0 | 1/1 registered CTest target passed. |
| Complexity gate | `bash tests/complexity-gate.sh --check` | 0 | Passed. |
| File length gate | `bash tests/file-length-gate.sh --check` | 0 | Passed. |

Build logs: `build-1.log` through `build-4.log`. Test logs: `test-1.log` through `test-4.log`. Gate logs: `gate-complexity-1.log` and `gate-file-length-1.log`.

The requested `fff grep` tool was not available in the enabled tools for this run; log matches were searched with `rg`.

## Files committed

- `src/core/midi/MidiAlsaRaw.cpp`
- `tests/src/core/MidiOutQueueTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-239.md`
