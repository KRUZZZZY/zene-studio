FIXED

# Cluster new-p14-88: finite beat values whose 48-tick product overflows

Status: REAL. Fixed by refusing (not clamping) finite beat values whose 48-ticks-per-beat product is not finite.

## Defect (pre-fix evidence)

File: `src/core/DawProjectReadShared.h` at the pre-fix commit (`git show HEAD^:src/core/DawProjectReadShared.h`), lines 249-254:

- `const double exact = beats * DawProjectTicksPerQuarterNote;` (line 250)
- `const double held = std::isfinite(exact) ? std::min(exact, TickLimit) : 0.0;` (line 251). When `exact` is non-finite, `held` becomes 0.
- `if (!(std::fabs(exact - rounded) <= 1e-6)) { state.report->loss.roundedTimes++; }` (line 253). The non-finite value is counted as a rounded time.
- `return static_cast<qint64>(std::min(rounded, TickLimit));` (line 254). Returns 0, so a finite beat such as `1e308` silently lands at tick zero with no error.

`doubleAttribute` at `src/core/DawProjectReadShared.h:188-205` accepts `1e308` as a finite double, so the bad value reaches `ticksFromBeats` with no earlier refusal.

Unguarded call sites that feed `ticksFromBeats` from attributes (`src/core/DawProjectReadTracks.cpp`):

- `:137` note start (`time`, through `startTicks`)
- `:140` note duration (`ticksFromBeats(state, duration)`)
- `:192` clip start (`time`)
- `:194` clip duration (`duration`)
- `:199` clip playStart (`playStart`)

Already-guarded path: the automation point path, `src/core/DawProjectRead.cpp:115-122`, calls `dawProjectBeatRepresentable(raw)` before `ticksFromBeats` and sets a state error when it fails. The packet cites `:116`, which is the guard line; I checked lines 115-122 this session.

## Fix

`src/core/DawProjectReadShared.h:249-261` (the change in commit c7e7daa63):

- When `exact` is not finite, `ticksFromBeats` now sets `state.error` to a message containing `48-tick-per-beat` (`"a timeline value %1 does not have a finite 48-tick-per-beat count"`) and returns 0. The error is set only if none is already present.
- The finite path is unchanged: `held = std::min(exact, TickLimit)`, the rounding and `roundedTimes` counting are as before.
- The doc comment above the function gained one line describing the refusal.

Why clamp-and-count is preserved: a finite beat past the 32-bit tick grid is still clamped into `TickLimit` and counted in `roundedTimes`. The test's control `time="50000000"` (see below) relies on that. Only the non-finite product is refused.

Why `dawProjectBeatRepresentable` was not used at the parse sites: it rejects any beat whose tick, rounded, falls outside `tick_t` (int32). `src/core/DawProjectModel.cpp:84-93` computes `floor(beats*48+0.5)` and requires it to be in `[lowest, max]` of `tick_t`. For `50000000`, `50000000*48 = 2.4e9`, which is above `INT32_MAX` (2147483647), so the helper returns false. The test `hugeBeatPositionsCannotEnterTheModelOutOfTickRange` at `tests/src/core/DawProjectInterchangeRoundTripTest.cpp:1918-1948` requires `50000000` to be accepted and clamped. Using the helper at parse sites would therefore break that existing test and change the documented clamp-and-count behaviour. The fix refuses only the non-finite case, which is the case that was silently wrong.

## Test

Added `finiteBeatValuesThatOverflowTheTickProductAreRefusedByReader()` at `tests/src/core/DawProjectInterchangeRoundTripTest.cpp:1950-2026`. It is already registered in the existing test class; no registration edit was needed.

The test feeds five overflowing fixtures (`1e308` at F1-F5) through `dawProjectModelFromXml` and requires each to be refused with a `48-tick-per-beat` error. It also has two controls: `50000000` clamped and counted (`roundedTimes >= 1`), and `time=1` accepted exactly (`1.0`).

### Test matrix

| Case | Input | Pre-fix (red) | Post-fix (green) |
|---|---|---|---|
| Normal path | `time="1" duration="4"` note at 0 | accepted, `time == 1.0` | accepted, `time == 1.0` (control, passes) |
| F1 note time | `<Note time="1e308">` | accepted (test-1-red) | refused with `48-tick-per-beat` |
| F2 note duration | `<Note duration="1e308">` | accepted (test-1-red) | refused with `48-tick-per-beat` |
| F3 clip time | `<Clip time="1e308">` | accepted (test-1-red) | refused with `48-tick-per-beat` |
| F4 clip duration | `<Clip duration="1e308">` | accepted (test-1-red) | refused with `48-tick-per-beat` |
| F5 clip playStart | `<Clip playStart="1e308">` | accepted (test-1-red) | refused with `48-tick-per-beat` |
| 50000000 clamp control | `time="50000000"` | clamped, counted (unchanged) | clamped, counted (control, passes) |
| Audio-thread | n/a | n/a | n/a (not exercised by this test; not checked) |
| Undo | n/a | n/a | n/a (not exercised by this test; not checked) |
| Platform | n/a | n/a | n/a (tests were run on Linux only) |

Evidence for the F1-F5 "accepted" column: `test-1-red.log` and `test-4-red-proof.log` both print `accepted: [F1 note time=1e308, F2 note duration=1e308, F3 clip time=1e308, F4 clip duration=1e308, F5 clip playStart=1e308]`.

## Commands and exit codes

Log folder: `/home/kruzzzzy/.config/kads/zene/state/runs/new-p14-88.r1/logs/`

Exit codes are as recorded for this run. The build logs contain no literal `EXIT=` line; their exit 0 is supported by `Built target DawProjectInterchangeRoundTripTest` as the last line. The test exit codes are supported by the `Totals:` line in each test log.

| Step | Log | Exit | Evidence in log |
|---|---|---|---|
| build-1-red | build-1-red.log | 0 | `Built target DawProjectInterchangeRoundTripTest` |
| test-1-red | test-1-red.log | 1 | `Totals: 2 passed, 1 failed`; five accepted |
| build-2-green | build-2-green.log | 0 | `Built target DawProjectInterchangeRoundTripTest` |
| test-2-green | test-2-green.log | 0 | `Totals: 3 passed, 0 failed` |
| test-3-family | test-3-family.log | 0 | `100% tests passed out of 2` (DawProjectInterchangeRoundTripTest, DawProjectDeflateImportTest) |
| final-stat | final-stat.log | 0 | `git show --stat HEAD` on c7e7daa63: `3 files changed, 207 insertions(+), 1 deletion(-)` (docs, header, test) |
| patch-check | patch-check.log | see log | `git apply -R --check fix.patch` run after the commit; the exit is recorded in `patch-check.log`. The p4 revert/apply exits were observed in the earlier run; this post-commit check is the logged one |
| build-4-red-proof | build-4-red-proof.log | 0 | `Built target DawProjectInterchangeRoundTripTest` |
| test-4-red-proof | test-4-red-proof.log | 1 | `Totals: 2 passed, 1 failed`; five accepted |
| build-5-green-proof | build-5-green-proof.log | 0 | `Built target DawProjectInterchangeRoundTripTest` |
| test-5-green-proof | test-5-green-proof.log | 0 | `Totals: 3 passed, 0 failed` |
| feature_check | gate-1.log | 0 | `RESULT: PASS` (all eight items PASS) |
| git diff --check | (shell, this session) | 0 | no output |

Not cited: `build-1b` and `test-1b` do not exist and are not used here.

Commands (run in the work tree):

```
python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > .../logs/gate-1.log 2>&1; echo EXIT=$?   # EXIT=0
git diff --check; echo EXIT=$?                                                                                                  # EXIT=0
```

Note on the feature_check base: `gate-1.log` reports `base fa4a5f2e7, 188 changed files` relative to that base; the working-tree diff this session is the two modified files listed in the commit.

## Self-check

Gate table (from `gate-1.log`):

| Gate item | Result | Short answer |
|---|---|---|
| fork-sources | PASS (exit 0) | Passed; no registration edit was needed, so none was made. Gate internals not inspected. |
| all-sources-reproduce | PASS (exit 0) | Passed as reported by the gate. Gate internals not inspected. |
| no-upstream-regression | PASS (exit 0) | Passed as reported by the gate. Gate internals not inspected. |
| check-namespace | PASS (exit 0) | Passed as reported by the gate. Gate internals not inspected. |
| no-tautology | PASS (exit 0) | Passed as reported by the gate. Gate internals not inspected. |
| doc-figures | PASS (exit 0) | Passed as reported by the gate. Gate internals not inspected. |
| complexity | PASS | `no new item in your files` (from gate-1.log). |
| file-length | PASS | `no new item in your files` (from gate-1.log). |

No gate item is red, so no pre-existing train ratchet needed naming.

Packet questions:

- Did the fix refuse rather than clamp the non-finite case? Yes: `DawProjectReadShared.h:252-260` returns 0 with an error set.
- Did the finite clamp path change? No: `held`, rounding and `roundedTimes` are unchanged.
- Does the new test fail before the fix? Yes: `test-1-red` and `test-4-red-proof` each show `FAIL!` with five accepted fixtures.
- Does it pass after? Yes: `test-2-green` and `test-5-green-proof` show `3 passed, 0 failed`.
- Did the neighbouring tests stay green? Yes: `test-3-family` shows 2/2 passed.
- Is the diff whitespace-clean? Yes: `git diff --check` exit 0.
