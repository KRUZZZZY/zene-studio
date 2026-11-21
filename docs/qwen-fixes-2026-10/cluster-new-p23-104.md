FIXED

# Cluster new-p23-104: graphModel::setLength regrow resurfaces stale samples

## Evidence (REAL)

- `src/gui/widgets/Graph.cpp:520-523` (HEAD, before the fix): `setLength` on a shrink only updates `m_length`. The resize runs only when `m_samples.size() < m_length`, so a regrow within the old capacity does not touch the slots re-exposed by the regrow. Those slots still hold stale values left by the earlier shrink.
- Red run, `logs/test-red-1.log` lines 20-23: `EnvelopeGraphTest::regrowAfterShrinkZeroesStaleSamples()` FAILS at `tests/src/gui/EnvelopeGraphTest.cpp:191` with Actual 0.75, Expected 0. That log has no EXIT line; the formal red exit is the reverse run in the table below.

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
| Shrink then regrow within capacity (2 -> 4) | lines 190-192 | RED at line 191 before the fix; PASS after the fix (`logs/test-green-1-direct.log`) |
| Shrink then regrow beyond capacity (2 -> 6) | lines 194-196 | Not reached in the red run (aborted at 191); PASS after the fix |
| Plain grow (4 -> 6) | the 4 -> 6 step | PASS after the fix |
| `setLength` below `MinGraphLength` (Graph.cpp:518 early return) | no | Not tested (out of packet scope) |
| Audio-thread path | no | Not tested; callers read are GUI slots, verified by reading only |
| Non-Linux build with `std::fill` | no | Unverified; only the Linux build was run |

## Results and commands (all from saved logs under `runs/new-p23-104.r1/logs/`)

- p1 red, initial run: `test-red-1.log`. `regrowAfterShrinkZeroesStaleSamples` FAIL at `EnvelopeGraphTest.cpp:191` (Actual 0.75, Expected 0). Other slots PASS. No EXIT line in this log.
- p2 green build: `build-green-1.log` EXIT=0 (line 215).
- p2 green ctest: `test-green-1.log` EXIT=0 (`#515 EnvelopeGraphTest ... Passed`, 100% of 1).
- p2 green direct run: `test-green-1-direct.log` EXIT=0. All slots PASS, including `regrowAfterShrinkZeroesStaleSamples`.
- p2 family run: `test-family-1.log` EXIT=0 (WaveformDataTest, GraphWaveformLoadTest and EnvelopeGraphTest, 3/3 passed).
- p3 reverse, fix removed: `apply-reverse-1.log` EXIT=0; `build-reverse-1.log` EXIT=0; `test-reverse-1.log` EXIT=8. `regrowAfterShrinkZeroesStaleSamples` FAILED (line 20), "The following tests FAILED" at line 88, `EXIT=8` at line 91. This is the formal red_exit=8.
- p3 reapply, fix restored: `apply-forward-1.log` EXIT=0; `build-reapply-1.log` EXIT=0; `test-reapply-1.log` EXIT=0. This is the green_exit=0.
- Pre-commit feature check: `feature-check-precommit-1.log`. Every row PASS (fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity, file-length), `RESULT: PASS`, `EXIT=0`.

The commit, the post-commit feature check and the git status check are recorded in `git-commit-1.log`, `feature-check-1.log` and `git-status-1.log`. They happen after this report is written, so they are not reproduced here. The post-commit feature check is the authoritative gate evidence.

## Other findings

- The shrink path still leaves stale values beyond `m_length`. `BitInvader.cpp:166-167` saves all 200 floats, so those stale tail values are still written to the project file. Not tested and not changed here.
- The `MinGraphLength` early return (Graph.cpp:518) has no test.
- The `std::fill` change has only been built on Linux.
- `logs/fix.patch` ends with a stray `EXIT=0` line (line 33). It is a log artifact in the patch file and is not part of the `Graph.cpp` diff.

## Self-check

1. **Red/green reverse run.** With the fix removed, `logs/test-reverse-1.log` shows `regrowAfterShrinkZeroesStaleSamples` FAILED and `EXIT=8` (line 91). With the fix reapplied, `logs/test-reapply-1.log` shows `100% tests passed, 1 of 1` and `EXIT=0` (line 8). Reverse exit 8 is the red exit and reapply exit 0 is the green exit.
2. **Deterministic model-only slot.** The slot (`tests/src/gui/EnvelopeGraphTest.cpp:178-197`) uses a fixed `constexpr std::array<float,4>` input and calls only `graphModel` methods (`setSamples`, `setLength`, `samples`, `length`). It has no timers, `QTest::qWait`, sleeps, threads, or GUI widgets, so the result does not depend on timing or event ordering.
3. **Callers and constructor sibling.** Both shrink-and-regrow callers are checked by reading (report lines 10-13): `BitInvader.cpp:229-234` and `MultitapEchoControls.cpp:157-164`. The `graphModel` constructor (`src/gui/widgets/Graph.cpp:488-496`) sizes `m_samples` and `m_length` to the same `max(length, MinGraphLength)`, so the invariant `m_samples.size() >= m_length` already holds at construction, and the fix only touches the growth path. Why not truncate: `BitInvader.cpp:164-168` encodes `wavetableSize * sizeof(float)` bytes, i.e. 200 floats (`BitInvader.h:51`), from `m_graph.samples()` on every save regardless of length. Truncating `m_samples` on shrink would let the vector fall below 200 entries, so the save path would over-read. The fix keeps storage unchanged. (Evidence: the 200-float read at `BitInvader.cpp:166-167` and the `wavetableSize = 200` constant. No runtime over-read was reproduced here.)
4. **No new limit; related tests green.** The diff in `0772c88785` adds no clamp, minimum, or maximum on length. The only logic change is the `std::fill` over `[previousLength, m_length)` on growth (`Graph.cpp:527-532`). `logs/test-family-1.log` shows `GraphWaveformLoadTest` (#143) Passed, `WaveformDataTest` and `EnvelopeGraphTest` (#515) Passed, `100% tests passed out of 3`, `EXIT=0`.
5. **Verdict.** N/A for ALREADY-FIXED or FALSE-POSITIVE. The defect was reproduced red (`test-red-1.log`, Actual 0.75, Expected 0) and the fix is in the code, so the verdict is FIXED.

**Post-commit gate table** (`logs/feature-check-1.log`, `RESULT: PASS`, `EXIT=0`): fork-sources PASS, all-sources-reproduce PASS, no-upstream-regression PASS, check-namespace PASS, no-tautology PASS, doc-figures PASS, complexity PASS, file-length PASS. That is 8/8.

**Screenshot digest.** `logs/screenshot/shell-arrange.png` has sha256 `d081016751862a3850fb00043e1c76e81d8c1979f3fd7f77abe147ad20bd7045`. This matches `logs/screenshot-1.sha256` and was checked with `sha256sum` in this session.
