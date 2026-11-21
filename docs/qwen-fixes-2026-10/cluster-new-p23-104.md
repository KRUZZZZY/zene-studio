FIXED

# Cluster new-p23-104: graphModel::setLength regrow resurfaces stale samples

## Evidence (REAL)

- `src/gui/widgets/Graph.cpp:520-523` (pre-fix, `0772c88785^`): `setLength` on a shrink only updates `m_length`. The resize runs only when `m_samples.size() < m_length`, so a regrow within the old capacity does not touch the slots re-exposed by the regrow. Those slots still hold stale values left by the earlier shrink.
- Fixed code: `src/gui/widgets/Graph.cpp:521-531` (HEAD). `previousLength` is captured at 521, and the growth-only `std::fill` over `[previousLength, m_length)` is at 527-531.
- Earlier attempt (not this session), red run, `logs/test-red-1.log` lines 20-23: `EnvelopeGraphTest::regrowAfterShrinkZeroesStaleSamples()` FAILS at `tests/src/gui/EnvelopeGraphTest.cpp:191` with Actual 0.75, Expected 0. That log has no EXIT line; the formal red exit is the reverse run in the table below.

Real callers that shrink and re-grow:

- `plugins/BitInvader/BitInvader.cpp:229-234`: `lengthChanged()` calls `m_graph.setLength((int) m_sampleLength.value())`, then `normalize()`.
- `plugins/MultitapEcho/MultitapEchoControls.cpp:157-164`: `lengthChanged()` calls `m_ampGraph.setLength(len)` and `m_lpGraph.setLength(len)`, each followed by a samples-changed notification for `0..len-1`.

## Why the fix zeroes on growth instead of shrinking m_samples

`BitInvader.cpp:164-168` base64-encodes `wavetableSize * sizeof(float)` bytes from `m_graph.samples()` regardless of the current length. `wavetableSize` is 200 (`plugins/BitInvader/BitInvader.h:51`). If `setLength` truncated `m_samples` on a shrink, `m_samples.size()` could fall below 200 while the save path still reads 200 floats, which would over-read the vector. The fix keeps the storage as it is and zeroes only the slots that a regrow re-exposes.

## The fix

`src/gui/widgets/Graph.cpp`:

- adds `#include <algorithm>`;
- records `previousLength` before `m_length` changes;
- on growth (`m_length > previousLength`), fills `[previousLength, m_length)` with `0.0f` (lines 527-532).

Test: `tests/src/gui/EnvelopeGraphTest.cpp:178-197`, slot `regrowAfterShrinkZeroesStaleSamples`.

## Test matrix

| Case | Covered by the slot | Result |
|---|---|---|
| Shrink then regrow within capacity (2 -> 4) | lines 190-192 | RED at line 191 before the fix; PASS after the fix (`logs/p4-test-green-direct-1.log`) |
| Shrink then regrow beyond capacity (2 -> 6) | lines 194-196 | Not reached in the red run (aborted at 191); PASS after the fix |
| Plain grow (4 -> 6) | the 4 -> 6 step | PASS after the fix |
| `setLength` below `MinGraphLength` (Graph.cpp:518 early return) | no | Not tested (out of packet scope) |
| Audio-thread path | no | Not tested; callers read are GUI slots, verified by reading only |
| Non-Linux build with `std::fill` | no | Unverified; only the Linux build was run |

## Results and commands

### Earlier attempt (not this session)

These logs are from an earlier attempt, not from this session. They are kept for history and are not cited as this session's evidence.

- Earlier red run, initial: `test-red-1.log`. `regrowAfterShrinkZeroesStaleSamples` FAIL at `EnvelopeGraphTest.cpp:191` (Actual 0.75, Expected 0). No EXIT line in this log.
- Earlier green build: `build-green-1.log` EXIT=0 (line 215).
- Earlier green ctest: `test-green-1.log` EXIT=0 (`#515 EnvelopeGraphTest ... Passed`, 100% of 1).
- Earlier green direct run: `test-green-1-direct.log` EXIT=0. All slots PASS, including `regrowAfterShrinkZeroesStaleSamples`.
- Earlier family run: `test-family-1.log` EXIT=0 (WaveformDataTest, GraphWaveformLoadTest and EnvelopeGraphTest, 3/3 passed).
- Earlier reverse run, fix removed: `apply-reverse-1.log` EXIT=0; `build-reverse-1.log` EXIT=0; `test-reverse-1.log` EXIT=8. `regrowAfterShrinkZeroesStaleSamples` FAILED (line 20), `EXIT=8` at line 91.
- Earlier reapply, fix restored: `apply-forward-1.log` EXIT=0; `build-reapply-1.log` EXIT=0; `test-reapply-1.log` EXIT=0.
- Earlier pre-commit feature check: `feature-check-precommit-1.log`. Every row PASS, `RESULT: PASS`, `EXIT=0`.
- Earlier commit and post-commit checks: `git-commit-1.log`, `feature-check-1.log` (8/8 PASS, `EXIT=0`) and `git-status-1.log`. Superseded for gate evidence by `p5-feature-check-2.log` below.

### This session (p1-p5)

Logs are under `runs/new-p23-104.r1/logs/`. Each EXIT value is the line written by the step itself. The Self-check and gate table cite these logs.

| Step | Result | EXIT | Log |
|---|---|---|---|
| p1 status, show, log | git read-only checks | 0 | `p1-status-1.log`, `p1-show-1.log`, `p1-log-1.log` |
| p2 build (green) | Linux build | 0 | `p2-build-1.log` |
| p2 ctest (green) | `#515 EnvelopeGraphTest` Passed | 0 | `p2-test-1.log` |
| p2 direct run (green) | all slots PASS | 0 | `p2-test-direct-1.log` |
| p3 remove fix (apply reverse) | fix removed | 0 | `p3-apply-reverse-1.log` |
| p3 build (red) | Linux build | 0 | `p3-build-red-1.log` |
| p3 ctest (RED) | `regrowAfterShrinkZeroesStaleSamples` FAILED, `EXIT=8` | **8** | `p3-test-red-1.log` (line 20-22: Actual 0.75, Expected 0) |
| p3 direct run (red) | Totals 14 passed, 1 failed (line 22); FAIL at line 17 | 1 | `p3-test-red-direct-1.log` |
| p4 restore fix (apply forward) | fix restored | 0 | `p4-apply-forward-1.log` |
| p4 build (green) | Linux build | 0 | `p4-build-green-1.log` |
| p4 ctest (green) | `100% tests passed, 1 of 1` | 0 | `p4-test-green-1.log` |
| p4 direct run (green) | all slots PASS | 0 | `p4-test-green-direct-1.log` |
| p4 family run | 3/3 passed | 0 | `p4-test-family-1.log` |
| p5 screenshot sha256 | `d081016751862a3850fb00043e1c76e81d8c1979f3fd7f77abe147ad20bd7045` | 0 | `p5-sha-1.log` |
| p5 feature_check, first attempt | superseded by the re-run below (its log reports `EXIT=0`) | 0 | `p5-feature-check-1.log` (earlier p5 attempt, not used as gate evidence) |
| p5 feature_check, re-run (gate) | RESULT: PASS | 0 | `p5-feature-check-2.log` |

The formal red is p3 ctest, EXIT=8. The p3 direct run also exits 1 and its totals line reports `1 failed` for the same slot (line 17 FAIL). I did not separately trace why the direct binary returns 1 beyond that totals line; the ctest EXIT=8 is the one used as red evidence.

## Other findings

- The shrink path still leaves stale values beyond `m_length`. `BitInvader.cpp:166-167` saves all 200 floats, so those stale tail values are still written to the project file. Not tested and not changed here.
- The `MinGraphLength` early return (Graph.cpp:518) has no test.
- The `std::fill` change has only been built on Linux.
- This session used `logs/fix-p3.patch` (32 lines). It has no `EXIT=` line and is verified clean. The older `logs/fix.patch` (earlier attempt) ends with a stray `EXIT=0` line (line 33); it is not used here.

## Self-check

1. **Red/green run (this session).** With the fix removed (p3), `logs/p3-test-red-1.log` lists `regrowAfterShrinkZeroesStaleSamples` under "The following tests FAILED" (line 88), shows Actual 0.75 and Expected 0 (lines 21-22), and ends with `EXIT=8` (line 91). With the fix restored (p4), `logs/p4-test-green-1.log` shows `100% tests passed out of 1` and `EXIT=0` (line 8). Exit 8 is the red exit and exit 0 is the green exit.
2. **Deterministic model-only slot.** The slot (`tests/src/gui/EnvelopeGraphTest.cpp:178-197`) uses a fixed `constexpr std::array<float,4>` input and calls only `graphModel` methods (`setSamples`, `setLength`, `samples`, `length`). It has no timers, `QTest::qWait`, sleeps, threads, or GUI widgets, so the result does not depend on timing or event ordering.
3. **Callers and constructor sibling.** Both shrink-and-regrow callers are checked by reading (report lines 10-13): `BitInvader.cpp:229-234` and `MultitapEchoControls.cpp:157-164`. The `graphModel` constructor (`src/gui/widgets/Graph.cpp:488-497`) sizes `m_samples` and `m_length` to the same `max(length, MinGraphLength)`, so the invariant `m_samples.size() >= m_length` already holds at construction, and the fix only touches the growth path. Why not truncate: `BitInvader.cpp:164-168` encodes `wavetableSize * sizeof(float)` bytes, i.e. 200 floats (`BitInvader.h:51`), from `m_graph.samples()` on every save regardless of length. Truncating `m_samples` on shrink would let the vector fall below 200 entries, so the save path would over-read. The fix keeps storage unchanged. (Evidence: the 200-float read at `BitInvader.cpp:166-167` and the `wavetableSize = 200` constant. No runtime over-read was reproduced here.)
4. **No new limit; related tests green.** The diff in `0772c88785` adds no clamp, minimum, or maximum on length. The only logic change is the `std::fill` over `[previousLength, m_length)` on growth (`Graph.cpp:527-532`). `logs/p4-test-family-1.log` shows `WaveformDataTest` (#142) Passed, `GraphWaveformLoadTest` (#143) Passed, `EnvelopeGraphTest` (#515) Passed (line 7), `100% tests passed out of 3` (line 9), `EXIT=0` (line 12).
5. **Verdict.** N/A for ALREADY-FIXED or FALSE-POSITIVE. The defect was reproduced red this session (`logs/p3-test-red-1.log`, lines 21-22, Actual 0.75, Expected 0, `EXIT=8`), the fix is green (`logs/p4-test-green-1.log`, `EXIT=0`), and the fix is in the code, so the verdict is FIXED.
6. **Sibling commands (submit_check ASK).** The product diff in `0772c88785` changes no `ControlCommands*` or `ControlReversibilityTable` file. `git show --stat 0772c8878` (`logs/p1-show-1.log`, lines 9-13, `EXIT=0` at line 14) lists only `src/gui/widgets/Graph.cpp`, `tests/src/gui/EnvelopeGraphTest.cpp`, `tests/upstream-modifications.txt` and this report. `graphModel::setLength` is a model method, not an agent command, so the listed `app.*` and `arrangement.*` commands are unaffected by the fix. The generic ASK list is in `logs/submit-check-1.log` (lines 8-14), which is the earlier attempt's copy, not this session's run; this session's submit_check is `logs/p6-submit-check-1.log`.

**Gate table** (this session's re-run `logs/p5-feature-check-2.log`, `RESULT: PASS`, `EXIT=0` at the last line):

| Gate | Result |
|---|---|
| fork-sources | PASS (exit 0) |
| all-sources-reproduce | PASS (exit 0) |
| no-upstream-regression | PASS (exit 0) |
| check-namespace | PASS (exit 0) |
| no-tautology | PASS (exit 0) |
| doc-figures | PASS (exit 0) |
| complexity | PASS (no new item in your files) |
| file-length | PASS (no new item in your files) |

That is 8/8. The earlier post-commit table (`logs/feature-check-1.log`, 8/8, earlier attempt) is superseded by this one.

**Screenshot digest.** `logs/screenshot/shell-arrange.png` has sha256 `d081016751862a3850fb00043e1c76e81d8c1979f3fd7f77abe147ad20bd7045`. This session's check is `logs/p5-sha-1.log` (sha256sum output, `EXIT=0` at line 2).
