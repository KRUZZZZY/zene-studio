FIXED

# Cluster new-p23-104: graphModel::setLength regrow resurfaces stale samples

All logs cited here are in `/home/kruzzzzy/.config/kads/zene/state/runs/new-p23-104.r2/logs/` (abbreviated `logs/` below). Earlier attempt logs are not cited.

## Evidence (REAL)

- Pre-fix (`0772c88785^`): `graphModel::setLength` on a shrink only updates `m_length`. The resize runs only when `m_samples.size() < m_length`, so a regrow within the old capacity does not touch the slots it re-exposes. Those slots keep the stale values left by the shrink.
- Fixed code (HEAD): `src/gui/widgets/Graph.cpp:521` captures `previousLength`; the growth-only `std::fill` over `[previousLength, m_length)` is at `Graph.cpp:527-532`.
- Real callers that shrink and regrow:
  - `plugins/BitInvader/BitInvader.cpp:142-143` connects the length model's `dataChanged` to `lengthChanged()`; `BitInvader.cpp:229-231` calls `m_graph.setLength((int) m_sampleLength.value())`.
  - `plugins/MultitapEcho/MultitapEchoControls.cpp:160-162` calls `m_ampGraph.setLength(len)` and `m_lpGraph.setLength(len)`.
- Red reproduction, model level: `logs/p2-test-red-1.log` lines 21-22, `EnvelopeGraphTest` slot `regrowAfterShrinkZeroesStaleSamples` fails with `graph.samples()[2]` Actual 0.75, Expected 0.

## Why the fix zeroes on growth instead of shrinking m_samples

`BitInvader.cpp:166-167` base64-encodes `wavetableSize * sizeof(float)` bytes from `m_graph.samples()` on every save, regardless of the current length. `wavetableSize` is 200 (`plugins/BitInvader/BitInvader.h:51`). If `setLength` truncated `m_samples` on a shrink, `m_samples.size()` could fall below 200 while the save path still reads 200 floats, which over-reads the vector. The fix keeps the storage as it is and zeroes only the slots a regrow re-exposes.

## BitInvader GUI acceptance

- Test: `tests/src/gui/BitInvaderViewLengthTest.cpp`, slot `sampleLengthKnobShrinksAndRegrowsTheGraphKeepingTheKeptSamplesAndZeroingTheTail`.
- It drives the real `BitInvaderView`: `instrument->createView(&host)` inside a `GuiApplication` started by `guitest::startGui`. It finds the single `Knob` whose model display name is "Sample length" (`BitInvader.cpp:333-335` creates it) and the single `Graph` child. It sets the knob's model value, which is the signal path the knob writes.
- The minimum of the Sample length model is 4: `plugins/BitInvader/BitInvader.cpp:134` is `m_sampleLength(wavetableSize, 4, wavetableSize, 1, ...)`. So a 2 -> 4 change cannot be made through the view, and the regrow is exercised as **8 -> 4 -> 8**:
  1. set the knob to 8 (length 8), seed samples `0.1 ... 0.8`;
  2. set the knob to 4 (length 4);
  3. set the knob to 8 (length 8);
  4. assert samples 0-3 equal the seeds and samples 4-7 are 0.
- RED (p2, fix removed): `logs/p2-build-red-1.log` EXIT=0; `logs/p2-test-red-1.log` ctest reports `EnvelopeGraphTest` (#515) and `BitInvaderViewLengthTest` (#523) failed, `EXIT=8` (last line, 124). In the GUI test, `Actual (samples[i]): 0.5` vs `Expected 0` at `BitInvaderViewLengthTest.cpp(101)` (`logs/p2-test-red-1.log` lines 108-109). Direct run `logs/p2-test-red-direct-1.log`: `Totals: 2 passed, 1 failed`, `EXIT=1`.
- GREEN (p3, fix restored): `logs/p3-build-green-1.log` EXIT=0; `logs/p3-test-green-1.log` `100% tests passed out of 4` (lines 10-11), `EXIT=0`; `logs/p3-test-green-direct-1.log` slot PASS, `Totals: 3 passed, 0 failed`, `EXIT=0`.
- Screenshot: `/home/kruzzzzy/.config/kads/zene/state/runs/new-p23-104.r2/logs/bitinvader-shortened-regrown.png`, sha256 `f2979cafa661b45fa8e2b69be573e6c546b3cf7719503daab69fa12884d96f0e` (`logs/p3-png-1.log`: `PNG image data, 250 x 250`, sha256 line, `EXIT=0`). It is an offscreen render of the real `BitInvaderView` (`view->grab()`, written only when `BITINVADER_PROOF_PNG` is set; the test runs with `QT_QPA_PLATFORM=offscreen`). It is **not** `docs/ui-logs/050-ui-shell/shell-arrange.png`. The PNG pixels are not asserted; the zero-tail model assertion is the pass/fail result.

## Changes

- `src/gui/widgets/Graph.cpp`: `#include <algorithm>`, `previousLength` capture, growth-only zero fill (commit `0772c88785`).
- `tests/src/gui/EnvelopeGraphTest.cpp`: slot `regrowAfterShrinkZeroesStaleSamples` at lines 180-199 (commit `0772c88785`).
- `tests/upstream-modifications.txt`: one line added (commit `0772c88785`).
- `tests/src/gui/BitInvaderViewLengthTest.cpp`: new GUI test (this cluster, staged in this session).
- `tests/CMakeLists.txt`: registers `src/gui/BitInvaderViewLengthTest.cpp` in `LMMS_TESTS` (lines 1444-1445); adds `BitInvaderViewLengthTest` to the BitInvader plugin `target_sources` branch (line 2630); sets `QT_QPA_PLATFORM=offscreen` (near line 4072).
- `tests/all-sources.txt`: adds `tests/src/gui/BitInvaderViewLengthTest.cpp`.
- `tests/fork-sources.txt`: adds a `cluster new-p23-104` comment and `tests/src/gui/BitInvaderViewLengthTest.cpp`.
- `docs/qwen-fixes-2026-10/cluster-new-p23-104.md`: this report.

## EnvelopeGraphTest matrix (model level)

| Case | Result |
|---|---|
| Slot `regrowAfterShrinkZeroesStaleSamples` (lines 180-199), shrink then regrow within capacity | RED `logs/p2-test-red-1.log` lines 21-22 (Actual 0.75, Expected 0); GREEN `logs/p3-test-green-1.log` (4/4) |
| Shrink then regrow beyond capacity, plain grow | Same slot; not reported per step in r2 logs. The slot passes as one unit. |
| `setLength` below `MinGraphLength` (`Graph.cpp:518` early return) | Not tested (out of packet scope) |
| Audio-thread path | Not tested; callers checked by reading only |
| Non-Linux build with `std::fill` | Unverified; only the Linux build was run |

## Build and run log index (r2)

| Step | Result | EXIT | Log |
|---|---|---|---|
| p1 model test (baseline, fix present) | EnvelopeGraphTest #515 Passed | 0 | `p1-model-1.log` (line 8) |
| p1 GUI test (baseline) | BitInvaderViewLengthTest #523 Passed | 0 | `p1-test-1.log` (line 8) |
| p1 GUI build | target build; no EXIT line in log | — | `p1-build-1.log`, `p1-build-2.log` (not used as gate evidence) |
| p1 proof run | #523 Passed | 0 | `p1-proof-1.log` (line 8) |
| p1 status | git read-only check | 0 | `p1-status-1.log` |
| p2 patch capture | patch file captured | 0 | `p2-patch-1.log` |
| p2 remove fix (apply reverse) | `Graph.cpp` back to pre-fix | 0 | `p2-apply-reverse-1.log` |
| p2 build (RED) | Linux build | 0 | `p2-build-red-1.log` |
| p2 ctest (RED) | #515 and #523 failed | **8** | `p2-test-red-1.log` |
| p2 direct run (RED, GUI) | `samples[i]` 0.5 vs 0 | 1 | `p2-test-red-direct-1.log` |
| p3 restore fix (apply forward) | fix restored | 0 | `p3-apply-forward-1.log` |
| p3 build (GREEN) | Linux build | 0 | `p3-build-green-1.log` |
| p3 ctest (GREEN) | 4/4 passed | 0 | `p3-test-green-1.log` |
| p3 direct run (GREEN, GUI) | slot PASS | 0 | `p3-test-green-direct-1.log` |
| p3 screenshot check | PNG 250x250, sha256 f2979cafa6... | 0 | `p3-png-1.log` |
| p4 commit stat of `0772c88785` | files listed in Changes above | 0 | `p4-show-stat-1.log` |
| p4 stage report, test, manifests | explicit `git add` | 0 | `p4-stage-1.log` |
| p4 all-sources-reproduce | prints `REPRODUCES` | 0 | `p4-all-sources-1.log` |
| p4 feature_check | `RESULT: PASS` 8/8 | 0 | `p4-feature-check-1.log` |
| p4 git status | five staged paths | 0 | `p4-status-1.log` |

## Gate table (p4 re-gate after report update)

| Gate | Result | Log |
|---|---|---|
| fork-sources | PASS | `p4-feature-check-2.log` |
| all-sources-reproduce | PASS | `p4-feature-check-2.log` |
| no-upstream-regression | PASS | `p4-feature-check-2.log` |
| check-namespace | PASS | `p4-feature-check-2.log` |
| no-tautology | PASS | `p4-feature-check-2.log` |
| doc-figures | PASS | `p4-feature-check-2.log` |
| complexity | PASS | `p4-feature-check-2.log` |
| file-length | PASS | `p4-feature-check-2.log` |

Gate run: `p4-feature-check-2.log`, `RESULT: PASS`, `EXIT=0`.

## Other findings

- The shrink path still leaves stale values beyond `m_length` in storage. `BitInvader.cpp:166-167` saves all 200 floats, so those tail values are still written to the project file. Not tested and not changed here.
- The `MinGraphLength` early return (`Graph.cpp:518`) has no test.
- The `std::fill` change has only been built on Linux.
- The GUI test's `QWARN` about the `bitinvader/smooth_inactive` icon pixmap is from missing icon resources in the test environment; it does not affect the assertions.

## Self-check

1. **GUI surface.** The test runs the real `BitInvaderView` (`createView` on a real `QWidget` host, shown offscreen). It uses the view's actual Sample length `Knob` and `Graph` widgets and sets the knob's model value, which is what the knob writes. It asserts on the graph model's samples and length, not on painted pixels. The PNG is an offscreen render for reference only.
2. **Determinism.** Seeds are fixed (`0.1` ... `0.8`), the knob sequence is fixed (8, 4, 8), and the assertions read model values. The only waits are `QTest::qWaitForWindowExposed` and `processEvents`, used for window exposure. The PNG is written only when `BITINVADER_PROOF_PNG` is set. The slot in `EnvelopeGraphTest` has no timers, waits, or threads.
3. **Caller audit (by reading).** `BitInvader.cpp:142-143` and `229-231` (knob-driven shrink and regrow); `MultitapEchoControls.cpp:160-162` (both graphs shrink and regrow through `setLength`). The fix changes only the growth path, so both callers get zeroed re-exposed slots and unchanged kept samples. The `graphModel` constructor (`src/gui/widgets/Graph.cpp:488-497`) sizes both `m_samples` and `m_length` to `std::max(length, MinGraphLength)`, so `m_samples.size() >= m_length` holds at construction; the fix touches only the growth path.
4. **No collateral damage.** This session changes only the new test, `tests/CMakeLists.txt`, `tests/all-sources.txt`, `tests/fork-sources.txt`, and this report. No product code changes in this session; `git status` (`logs/p4-status-1.log`) shows only these paths. The product change `0772c88785` (`logs/p4-show-stat-1.log`) touches only `Graph.cpp`, `EnvelopeGraphTest.cpp`, `tests/upstream-modifications.txt`, and this report. The fix adds no clamp or new limit on length. No save/load code changed (`BitInvader.cpp:164-211` untouched, identical to the fix commit's parent), so existing saved wavetables load as before, and no new check was added.
5. **Verdict.** FIXED. The defect reproduces RED with the fix removed (`logs/p2-test-red-1.log`, `EXIT=8`; GUI direct `EXIT=1`), and is GREEN with the fix restored (`logs/p3-test-green-1.log`, 4/4, `EXIT=0`; `logs/p3-test-green-direct-1.log`, `EXIT=0`). The fix is in the code.
6. **Sibling commands.** `graphModel::setLength` is a model method, not an agent command. The commit `0772c88785` changes no `ControlCommands*` or `ControlReversibilityTable` file (`logs/p4-show-stat-1.log`). The listed app and arrangement commands are not affected by this fix.
