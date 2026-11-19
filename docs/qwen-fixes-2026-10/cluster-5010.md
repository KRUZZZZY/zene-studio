FIXED

# Cluster 5010: bounded drain of an oversized request

Fix commits on `bugs/qwen-5010`: `876b219d5a` (fix) and `70f70f34f0` (style). This report adds the gate registration fix and this document.

## a) The unbounded loop, its contradicting comment, and the new loop

- Old code, `git show fa4a5f2e78:src/core/ControlServer.cpp`: `drainPeer(int fd)` is lines 257-267. The loop at lines 260-266 is `while (true)` with no budget. `if (got > 0) { continue; }` (line 263) discards bytes until EOF, so one activation can read without limit.
- The comment that contradicts it is `include/ControlServer.h:159`: "peer sends is read and DISCARDED (bounded, per chunk) until EOF". `bool draining` is at `:163`. The header said "bounded"; the loop was not.
- New code, `src/core/ControlServer.cpp:259-273`: `drainPeer(int fd, int budgetBytes)`. The loop is `while (consumed < budgetBytes)` (line 263). Each read is capped to the remaining room (lines 265-267). When the budget is spent it returns `false` (line 272). The socket is still readable, so the level-triggered notifier fires again on the next loop pass (comment at lines 257-258).
- Constant: `include/ControlServer.h:95`, `DrainBytesPerActivation = 64 * 1024`. Call site: `src/core/ControlServer.cpp:359`.

## b) Sibling paths

- `readChunk` (`src/core/ControlServer.cpp:288`) returns `false` once `buffer.size() > MaxRequestLineBytes` (1 MiB, `include/ControlServer.h:83`). It was already bounded and is not changed.
- Windows: the drain runs in a per-pipe thread in `src/core/ControlServerWin32.cpp` (the plan's range :472-531; the `draining` flag is at :513 and the drain branch at :524-534). It is a separate code path, so this fix does not change it.
- Idle deadline: unchanged. The `deadline` grep over the two fix commits' diff of `src/core/ControlServer.cpp` returns no lines.

## c) Test matrix

The test file `tests/src/core/ControlServerDrainTest.cpp` has one test function, `drainIsBoundedPerActivation` (line 131).

| Path | Covered |
|---|---|
| normal path | yes: bounded rounds of 65536 bytes (`logs/003-test-3-green.log:22-25`) |
| failure path (read error) | not a separate test; the branch is `ControlServer.cpp:270` |
| exhaustion path | yes: 1019584-byte fill drained over several rounds (`logs/003-test-3-green.log:21`) |
| reconfiguration | not a separate test |
| audio thread | none (no audio code involved) |
| platform | POSIX |

## d) RED and GREEN numbers

- RED, old unbounded code: fill 1019584 bytes (`logs/test-2-red.log:7`). One activation consumed 8228544 bytes (`logs/test-2-red.log:8-9`). The limit is `DrainBytesPerActivation` (65536) + 8192 slack. `logs/test-4-red.log:8-10` shows the same numbers.
- GREEN, bounded code: fill 1019584 bytes (`logs/003-test-3-green.log:21`). Rounds 0-3 each consumed 65536 bytes (`logs/003-test-3-green.log:22-25`).
- `logs/test-4-green.log` is 8 lines and has no count lines, so the GREEN count in that log is not verified here.

## e) Commands and EXIT codes (copied from the logs)

- build-1: `logs/build-1.log`. No EXIT line in the log.
- test-1: `logs/test-1.log`. No EXIT line in the log.
- build-2: `logs/build-2.log` EXIT=0. An earlier attempt, `logs/build-2-attempt1.log`, has EXIT=2.
- test-2-red: `logs/test-2-red.log` EXIT=8 (expected failure on the unfixed code).
- build-3: `logs/001-build-3.log`, `logs/002-build-3.log`. No EXIT line in either log.
- test-3 green: `logs/003-test-3-green.log`. No EXIT line in the log. `logs/004-test-3-green-short.log` EXIT=0.
- test-3 family: `logs/005-test-3-family.log` EXIT=0.
- test-3 control-all: `logs/006-test-3-control-all.log` EXIT=8 (see Other findings).
- build-3b: `logs/build-3b.log`. No EXIT line in the log.
- p4 (fix.patch, reverse, RED, reapply, GREEN): `logs/test-4-baseline.log`, `logs/test-4-red.log`, `logs/test-4-withfix.log`, `logs/test-4-green.log`. None has an EXIT line. No separate reverse or reapply log was found by name. `logs/fix.patch` is present.
- gates: `logs/gates-first-run-FAIL.log` EXIT=1 and `logs/gates.log` EXIT=0. These codes were echoed by the command in the session; the logs themselves contain `RESULT: FAIL` and `RESULT: PASS`.

## f) Control command canary

No control command was added. The files changed since the base `fa4a5f2e78` are `include/ControlServer.h`, `src/core/ControlServer.cpp`, `tests/CMakeLists.txt`, `tests/fork-sources.txt`, and `tests/src/core/ControlServerDrainTest.cpp`. No command registration file is in that list. The feature gate table has no `ControlCommandsSnapshot` row, and I did not run that test. The canary's status is therefore not shown by any gate run here.

## g) Limits

- The full `cmake --build build` is guard-blocked (per the plan), so only the named targets were built. A grep of `logs/build-*.log` for "guard" found no matches, so the block is not recorded in those logs.
- No local TSan run. The race reasoning is by design: the Qt loop is single-threaded and the notifier is level-triggered.
- The test's counts depend on kernel AF_UNIX accounting. The 8192 slack is calibrated to exactly 65536 (the RED message in `logs/test-2-red.log:9`).
- Gate registration: `tests/all-sources.txt` gained `tests/src/core/ControlServerDrainTest.cpp`, which is what the whole-tree recipe derives. `tests/fork-sources.txt` already listed the path.

## Gate table

From `logs/gates.log` (after the fix; the first run failed on two rows):

| Gate | Result |
|---|---|
| fork-sources | PASS |
| all-sources-reproduce | PASS |
| no-upstream-regression | PASS |
| check-namespace | PASS |
| no-tautology | PASS |
| doc-figures | PASS |
| complexity | PASS (no new item) |
| file-length | PASS (no new item) |

The first run (`logs/gates-first-run-FAIL.log`) failed `fork-sources` and `all-sources-reproduce`. The cause was the missing `tests/src/core/ControlServerDrainTest.cpp` line in `tests/all-sources.txt`. `fork-sources` failed only because `tests/fork-sources-gate.sh` (lines 198-203) runs the all-sources reproduce check.

## Other findings

- `drainPeer`'s read-error branch returns `false` (`src/core/ControlServer.cpp:270`) without dropping the client. On Linux, ECONNRESET reports once and then EOF, so this costs at most one extra pass. Not changed.
- 31 Control tests fail identically with and without the fix. The list is `logs/fail-without-fix.txt` (31 lines). The environment has no ALSA seq, no JACK, and no audible instrument, per the plan; I did not re-check this.
- `ControlShutdownHookTest` has 3 cases that fail under the harness TMPDIR, because the socket path is longer than 107 bytes: `logs/test-1-longtmp.log:14`, `:19`, `:22` (`server.listen` returned FALSE).
- `tests/src/core/ControlShutdownHookTest.cpp` is 522 lines, over 500. This predates the fix.
- The Regenerate-with block in `tests/fork-sources.txt` (starting at line 49) puts `; \` before each `| awk` line. That is a syntax error in bash. No gate runs that recipe; `all-sources-reproduce.sh` checks only `tests/all-sources.txt`. This is not changed here.

## Self-check

(to be filled after the completion check in p6)
