FIXED

# Cluster 5010: bounded drain of an oversized request

The product fix on `bugs/qwen-5010` is branch commit `876b219d5a` (with style follow-up `70f70f34f0`). This run re-verified it against the current code. Every log cited below is in the r2 log folder (`/home/kruzzzzy/.config/kads/zene/state/runs/5010.r2/logs`); older logs named by earlier drafts are not evidence for this report.

## a) The unbounded loop, its contradicting comment, and the new loop

- Old code: `drainPeer(int fd)` looped `while (true)` and discarded every readable byte until EOF, so one activation could read without limit.
- The old header comment said the drain was "bounded, per chunk" while the loop was not.
- New code, `src/core/ControlServer.cpp:259-273`: `drainPeer(int fd, int budgetBytes)`. The loop is `while (consumed < budgetBytes)` (line 263). Each read is capped to the remaining room (lines 265-267). When the budget is spent it returns `false` (line 272). The socket stays readable, so the level-triggered notifier fires again on the next loop pass.
- Constant: `include/ControlServer.h:95`, `DrainBytesPerActivation = 64 * 1024` (65536). The single call site is `src/core/ControlServer.cpp:359`: `drainPeer(fd, ControlServer::DrainBytesPerActivation)`.

## b) Sibling paths

- `readChunk` was already bounded by the 1 MiB `MaxRequestLineBytes` cap and is not changed by this fix.
- Windows: `src/core/ControlServerWin32.cpp:513-534` drains the over-cap line in its per-client pipe thread, not in the Qt event loop. It is a separate code path; this fix does not change it.
- Idle deadline: unchanged.

## c) Test matrix

The test file `tests/src/core/ControlServerDrainTest.cpp` has one test function, `drainIsBoundedPerActivation` (declared at line 131, defined at line 134).

| Path | Covered |
|---|---|
| normal path | yes: four bounded rounds of 65536 bytes (`logs/114-test-green2-verbose.log:21-24`) |
| failure path (read error) | not a separate test; the branch is `ControlServer.cpp:270` |
| exhaustion path | yes: 1019584-byte fill drained over several rounds (`logs/112-test-red.log:8`, `logs/114-test-green2-verbose.log:21-24`) |
| reconfiguration | not a separate test |
| audio thread | none (no audio code involved) |
| platform | POSIX |

## d) RED and GREEN numbers (this run)

- Baseline before touching the fix: build `logs/101-build-green.log`; test `logs/102-test-green.log`, `100% tests passed out of 1`.
- RED, fix reverted with `logs/110-fix-cpp.patch`: build `logs/111-build-red.log`, test `logs/112-test-red.log` EXIT=8. Fill 1019584 bytes; round 0 consumed 8228544 bytes in one activation against a limit of `DrainBytesPerActivation` (65536) + 8192 slack = 73728 (`logs/112-test-red.log:8-10`); `0% tests passed, 1 tests failed out of 1`.
- GREEN, fix reapplied: build `logs/113-build-green2.log`, test `logs/114-test-green2.log` EXIT=0, `100% tests passed out of 1`. The verbose run `logs/114-test-green2-verbose.log` shows rounds 0, 1, 2 and 3 each consuming exactly 65536 bytes (`:21-24`).
- Wrong-fix mutant that stays safe: `logs/115b-mutant.patch` (clamps in a way that still respects the activation bound), build `logs/116b-build-mutant.log`, test `logs/117b-test-mutant-red.log` EXIT=8: round 0 consumed 1048576 bytes and the test fails at `tests/src/core/ControlServerDrainTest.cpp:212`. Restored: build `logs/118b-build-restored.log`, test `logs/119b-test-restored-green.log` EXIT=0, `100% tests passed out of 1`.
- The first mutant (`logs/115-mutant.patch`, `logs/116-build-mutant.log`, `logs/117-test-mutant-red.log`) was unsafe and is discarded: it pushed `consumed` past the budget, made `room` negative, and failed at the write helper (`tests/src/core/ControlServerDrainTest.cpp:209`), not at the bound assertion. It is not used as proof.
- Family: `logs/120-family.log` EXIT=0, `100% tests passed out of 2` (`ControlShutdownHookTest`, `ControlServerDrainTest`).
- Canary: `logs/121-canary.log` EXIT=0, `100% tests passed out of 1` (`ControlCommandsSnapshot`, ctest #934).
- Gates: `logs/130-gates.log` EXIT=0, `RESULT: PASS`.

## e) Commands and EXIT codes (copied from the saved logs)

- `logs/101-build-green.log`: `cmake --build build --target zene ControlServerDrainTest`; no EXIT line in the log, build output clean.
- `logs/102-test-green.log`: `ctest -R ^ControlServerDrainTest$ --output-on-failure`; `100% tests passed out of 1`.
- `logs/111-build-red.log`: same build target; build output clean, no EXIT line in the log.
- `logs/112-test-red.log`: same ctest command; EXIT=8, expected failure on unfixed code.
- `logs/113-build-green2.log`: same build target; build output clean, no EXIT line in the log.
- `logs/114-test-green2.log`: same ctest command; EXIT=0.
- `logs/114-test-green2-verbose.log`: verbose form of the same ctest command; rounds 0-3 at 65536 bytes, PASS.
- `logs/116b-build-mutant.log`: mutant build; build output clean, no EXIT line in the log.
- `logs/117b-test-mutant-red.log`: same ctest command; EXIT=8, FAIL at test line 212.
- `logs/118b-build-restored.log`: restored build; build output clean, no EXIT line in the log.
- `logs/119b-test-restored-green.log`: same ctest command; EXIT=0.
- `logs/120-family.log`: `ControlShutdownHookTest` and `ControlServerDrainTest`; EXIT=0, 2/2 passed.
- `logs/121-canary.log`: `ControlCommandsSnapshot`; EXIT=0, 1/1 passed.
- `logs/130-gates.log`: feature-check gates; EXIT=0, `RESULT: PASS` with no FAIL row.

## f) Control command canary

No control command was added. `ControlCommandsSnapshot` (ctest #934) passed in `logs/121-canary.log` (1/1), so the command table is unchanged by this fix.

## g) Limits

- The full `cmake --build build` remains guard-blocked per the plan; only the named targets were built. This was not re-checked in r2.
- No local TSan run. The race reasoning is by design: the Qt loop is single-threaded and the notifier is level-triggered.
- The test's counts depend on kernel AF_UNIX accounting. The 8192 slack is calibrated to exactly one 65536-byte budget (`logs/112-test-red.log:10`).
- Gate registration is verified by `all-sources-reproduce` and `fork-sources` both passing in `logs/130-gates.log`.

## Gate table

From `logs/130-gates.log` (this run):

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

## Other findings

These were reported by earlier drafts and were **not re-checked in r2**; they are not evidence for this run.

- `drainPeer`'s read-error branch returns `false` (`src/core/ControlServer.cpp:270`) without dropping the client. Not changed. Not re-checked in r2.
- A set of Control tests unrelated to this drain fail in this environment (no ALSA seq, no JACK, no audible instrument). Not re-checked in r2.
- `ControlShutdownHookTest` cases can fail under a long harness `TMPDIR` because the AF_UNIX path exceeds 107 bytes. Not re-checked in r2.
- `tests/src/core/ControlShutdownHookTest.cpp` is over 500 lines; this predates the fix. Not re-checked in r2.
- The Regenerate-with block in `tests/fork-sources.txt` has a shell syntax problem. No gate runs that recipe, and it is not changed here. Not re-checked in r2.

## Self-check

1. **Fails with the change reverted, passes with it (exit codes).** Reverted with `logs/110-fix-cpp.patch`: build `logs/111-build-red.log`, test `logs/112-test-red.log` EXIT=8, `0% tests passed, 1 tests failed out of 1`; round 0 consumed 8228544 bytes in ONE activation against the 73728 limit. Reapplied: build `logs/113-build-green2.log`, `logs/114-test-green2.log` EXIT=0, `100% tests passed out of 1`; `logs/114-test-green2-verbose.log` shows four rounds of 65536 bytes.
2. **Deterministic: no wall-clock timing, sleeps or races.** The seams are: the socket send buffer, set with `SO_SNDBUF` (`tests/src/core/ControlServerDrainTest.cpp:164`); a fill of at least `2 * DrainBytesPerActivation` (`:191-197`); the per-round count taken by `writeUntilWouldBlock` (`:98`, called at `:208`); and the tolerance `ConsumedSlackBytes = 8192` (`:75`). The exact counts depend on kernel AF_UNIX buffer accounting (see g).
3. **Fix at the cause; callers and siblings checked.** The cause is the unbounded loop in the old `drainPeer`. The single call site is `src/core/ControlServer.cpp:359`. The sibling `readChunk` is already bounded by the 1 MiB `MaxRequestLineBytes` cap and is unchanged. The Windows drain in `src/core/ControlServerWin32.cpp:513-534` is a separate path; it runs in its per-client pipe thread, not the Qt event loop, and was not changed or built here.
4. **No collateral damage.** The bound applies only on the draining branch of `onClientReadable` (`src/core/ControlServer.cpp:356-359`), after a request line is over the 1 MiB cap. No saved data or file format is involved, and no request is refused by the budget; the level-triggered notifier resumes the drain (`:257-258`). Evidence: `logs/120-family.log` EXIT=0, 2/2 passed; `logs/121-canary.log` EXIT=0, 1/1 passed.
5. **Verdict clause.** Not applicable: the verdict is FIXED (line 1 of this report), so the ALREADY-FIXED and FALSE-POSITIVE clause does not apply.
