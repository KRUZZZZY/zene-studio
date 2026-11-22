Verdict: FIXED

new-p9-487: TimePos::quantize() divides by a snap interval that can be zero

Finding: `quantize(bars)` converted `s_ticksPerBar * bars` to an `int` interval and divided by it with no check. A grid
under one tick (zero bars, negative bars, NaN, or a count such as 0.001 bars whose 0.192 ticks truncate to 0) made the
interval 0, and the division faulted.

## Evidence

Before the fix (branch tip 639e1d23e), `src/core/TimePos.cpp:64` was `int interval = s_ticksPerBar * bars;` and the
divisions at lines 66 (`m_ticks / interval`) and 68 (`m_ticks % interval`) ran with no check. Reproduced: the RED
backtrace ends in `idiv` inside `lmms::TimePos::quantize(float, bool) const` (logs test-1-red.log, frame #5).

The fix (`src/core/TimePos.cpp:61-72`):
- `:66` computes the grid as a float, `const float gridTicks = s_ticksPerBar * bars;`
- `:67-70` returns `*this` when `!( gridTicks >= 1.f )`. Testing the float before the int conversion catches a
  truncation to zero and NaN.
- `:72` `int interval = gridTicks;` gives the same int as the old `int interval = s_ticksPerBar * bars;` for every grid
  of one tick or more, so every valid snap is unchanged.

The no-op contract matches `Note::quantized`, which returns its input for a non-positive grid (`src/core/Note.cpp:485-487`).

Reachability, checked by reading callers:
- The shipped snap tables never go below one tick. `src/gui/editors/SongEditor.cpp:85-86` has the smallest snap at
  1/16 bar (12 ticks), or 1/64 bar (3 ticks) for the proportional table.
- `src/gui/editors/PianoRoll.cpp:5199-5208` takes the note length as `192 / denominator`. The combo denominators are
  1 to 192 (`PianoRoll.cpp:364-374`), so the length is at least one tick. The quantize grids
  (`include/Editor.h:36-38`, up to 192) also give at least one tick.
- A one-tick float grid is exact: `192 * float(1/192)` rounds to exactly 1.0 (checked in Python with float32 rounding).
- So the shipped UI does not reach a sub-tick grid. The crash is reachable through the public API: `quantize(0)`, or any
  bar count below 1/192. The fix covers that path; I found no UI path that reaches it.

## Behaviour change (deliberate)

A negative bar count used to snap on a negative grid. It now returns its input, the same as a zero grid. No caller
passes a negative count: every caller in the grep above passes a snap size from the tables or a PianoRoll grid of
at least one tick.

## Not fixed (outside this finding)

- The float-to-int conversion is still undefined when `s_ticksPerBar * bars` is 2^31 or more, because there is no
  upper bound check. Also, `(lowPos + snapUp) * interval` is int arithmetic that can overflow for large positions.
  These are siblings, not this finding, and need their own cluster.

## Regression test

`tests/src/core/NoteTransformTest.cpp:393-404`, `subTickTimePosGridIsANoop()`, placed beside the existing
`nonPositiveNoteQuantizeGridIsANoop()` (`:373`). It is deterministic (no timing, no sleeps) and asserts:
- `quantize(0.f)`, `quantize(0.f, true)`, `quantize(-1.f)` and `quantize(0.001f)` on 123 return 123
- `quantize(1.f / DefaultTicksPerBar)` on 123 returns 123 (a one-tick grid)
- `quantize(1.f / 16)` on 123 returns 120 (a real 12-tick grid still snaps; the normal path is not changed)

RED is a SIGFPE, not a failed assertion: the call faults before QCOMPARE can report. That is the failure the bug
produces on x86, so it is the honest RED here.

## Commands and exit codes

Environment: `QT_QPA_PLATFORM=offscreen` (preset). ccache is set to a temp directory this lane cannot write, so every
build below runs with `CCACHE_DISABLE=1`.

| Step | Command (run from the worktree unless noted) | Exit | Log (run folder logs/) |
| --- | --- | --- | --- |
| Build, first try | `cmake --build build --target zene NoteTransformTest -j4` | 2 | build-1-red.log (ccache: Permission denied) |
| Build, RED | `CCACHE_DISABLE=1 cmake --build build --target zene NoteTransformTest -j4` | 0 | build-2-red.log |
| RED run | `cd build/tests && ctest -R '^NoteTransformTest$' --output-on-failure` | 8 | test-1-red.log (SIGFPE, FPE_INTDIV) |
| Build, GREEN | `CCACHE_DISABLE=1 cmake --build build --target zene NoteTransformTest -j4` (with fix) | 0 | build-3-green.log |
| GREEN run | same ctest command | 0 | test-2-green.log (1/1 passed) |
| 4b save fix | `git diff -- src/core/TimePos.cpp > .../logs/fix.patch` | 0 | fix.patch |
| 4b revert | `git apply -R .../logs/fix.patch` | 0 | (none) |
| 4b build | `CCACHE_DISABLE=1 cmake --build build --target zene NoteTransformTest -j4` | 0 | build-4-revert.log |
| 4b RED | same ctest command | 8 | test-3-revert-red.log (SIGFPE) |
| 4b re-apply | `git apply .../logs/fix.patch` | 0 | (none) |
| 4b build | `CCACHE_DISABLE=1 cmake --build build --target zene NoteTransformTest -j4` | 0 | build-5-reapply.log |
| 4b GREEN | same ctest command | 0 | test-4-reapply-green.log (1/1 passed) |
| Family build | `CCACHE_DISABLE=1 cmake --build build --target AutomationThinTest SampleClipWarpModeMenuTest VestigeMidiHandoffTest -j4` | 0 | build-6-family.log |
| Family run | `cd build/tests && ctest -R '^(AutomationThinTest\|SampleClipWarpModeMenuTest\|VestigeMidiHandoffTest\|NoteTransformTest)$' --output-on-failure` | 0 | test-5-family.log (4/4 passed) |
| Gates (official script) | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 1 | (not written) |
| Gates (driver) | `python3 -I .../runs/new-p9-487.r1/gate_driver.py <worktree> .../runs/new-p9-487.r1/feature-gates` | 0 | feature-check-2.log |

Gate note: the official `feature_check.py` aborted before running any gate with
`PermissionError: [Errno 13]` creating `<worktree-parent>/zene-qwen-new-p9-487-feature-check`. That folder is outside
this lane's write-jail (the script writes its logs beside the worktree). I ran the same code path from
`gate_driver.py` in the run folder. It imports `feature_check.py`, uses its SIMPLE and RATCHET lists, its `run`,
`base_listings`, `key` and `item_file` functions, and the same base (`FIX-BASE` 050/m0-train-17, merge base 639e1d23e),
and only moves the log folder. Result below.

Gate table (driver, exit 0):

```
  PASS  fork-sources            exit 0
  PASS  all-sources-reproduce   exit 0
  PASS  no-upstream-regression  exit 0
  PASS  check-namespace         exit 0
  PASS  no-tautology            exit 0
  PASS  doc-figures             exit 0
  PASS  complexity              no new item in your files
  PASS  file-length             no new item in your files
RESULT: PASS
```

## Files committed

- `src/core/TimePos.cpp`: the guard in `quantize()`.
- `tests/src/core/NoteTransformTest.cpp`: `subTickTimePosGridIsANoop()`.
- `tests/upstream-modifications.txt`: registration lines for both files above (`new-p9-487`).
- `docs/qwen-fixes-2026-10/cluster-new-p9-487.md`: this report.

The test binary is the existing `NoteTransformTest` target. No new test file, so no CMake or `fork-sources` change.

## Self-check

- **Regression fails without the change and passes with it.** With the product change reverted (`git apply -R`), the
  rebuilt test exits 8 with SIGFPE (test-3-revert-red.log); with it applied, exit 0 (test-4-reapply-green.log). The
  first RED (before any fix, build-2-red.log then test-1-red.log) also exits 8. The family (four tests) exits 0.
- **Deterministic.** The test calls the pure function `TimePos::quantize` on fixed literal inputs (123 ticks, fixed bar
  counts). It uses no clock, sleep, thread or timing. No seam is needed.
- **Fix at the cause, callers checked.** The only divisor in `quantize()` is `interval`. The guard at `src/core/TimePos.cpp:66-72`
  covers both divisions (`:74` and `:76`) and the snap division at `:83`. Every caller in `src/` that passes a count was read: `TrackContentWidget.cpp`,
  `SampleTrackView.cpp`, `TimeLineWidget.cpp`, `PianoRoll.cpp:3136-3137`, `ArrangerFileDrop.cpp:82`, `ClipView.cpp`.
  All pass a snap from the tables or a PianoRoll grid of at least one tick, so the guard never fires on the shipped
  paths. The sibling `Note::quantized` (`src/core/Note.cpp:485-487`) already had the same no-op for a non-positive grid.
  Not checked: other `s_ticksPerBar` divisors such as `TimePos::getBar()` (`include/TimePos.h:90`). They are outside this
  finding.
- **No collateral damage.** `quantize()` is a runtime snap helper. The grep in `src/`, `include/`, `plugins/` and
  `tests/` shows no save or load caller, so saved projects are unaffected. Behaviour changes only for a grid under one
  tick, and for negative bar counts (see "Behaviour change"). The guard is a return of the input, not a limit that
  rejects a value. Grids of one tick or more compute the same int interval as before.
- **Gates.** The official `feature_check.py` could not write its logs from this lane (PermissionError, see the gate
  note). The same checks run through `gate_driver.py` are all PASS, exit 0. `submit_check.py`'s gate row runs the
  official script, so it stays FAIL from this lane for that reason only.
