IMPLEMENTED

Q050-176 - Zoom history with back and forward (L13, WF-094 / B-WF-094). The arranger's zoom walk is real back /
forward now: three prior extents visited, back and forward restore each exactly in order, both directions refuse
at their ends, a new zoom cuts the forward walk off, and `arrangement.zoom_toggle` swaps the current extent with
the one the zoom last left (twice returns you). `arrangement.zoom_back` keeps its id and its refusal; a GUI
(Navigate > Zoom Back / Zoom Forward / Zoom Toggle) and the agent surface run the same registered handlers.

Acceptance (commands as run from `build/tests`; exit codes are the ctest exits):

| Criterion | Command | EXIT |
| --- | --- | --- |
| Visit at least three distinct arrangement zoom levels, exercise back and forward, and verify each prior extent is restored | `ctest -R ArrangerTimelineTest --output-on-failure` (QtTest `backAndForwardWalkSeveralPriorZooms`, `tests/src/gui/ArrangerTimelineTest.cpp:180`; 30 slots passed) | 0 |
| The same walk seen over the agent surface | `ctest -R ControlCustomisation --output-on-failure` (`tests/control-customisation.py:334`: three `arrangement.zoom {pixels_per_bar}` levels, then back / forward / back / back restoring each, refusal checks, drain) | 0 |
| Use zoom toggle to swap between the current and prior extent | `ctest -R ArrangerTimelineTest --output-on-failure` (`zoomToggleSwapsTheCurrentAndPriorExtent`) and the same transcript's `zoom_toggle` rows in `ControlCustomisation` | 0 |
| The walk's contract rows / histogram follow | `ctest -R ReversibilityContractTest --output-on-failure` (published figure re-taken: 839 rows, 358 not_mutating) | 0 |
| Snapshot fresh against the binary | `ctest -R ControlCommandsSnapshot --output-on-failure` (830 ids / 76 groups, regenerated live) | 0 |
| Registry contract rows match | `ctest -R ControlRegistryTest --output-on-failure` | 0 |

Gates (from the repo root): `tests/scripted/check-namespace` 0; `bash tests/no-tautology-gate.sh` 0;
`bash tests/fork-sources-gate.sh` 0; `bash tests/all-sources-reproduce.sh` 0;
`bash tests/no-upstream-regression-gate.sh de5a85a96` 0; `python3 tests/doc-figures-gate.py` 0.
Gate 4 (complexity) and gate 7 (file length) exit 1 as train 5 leaves them; against `../zene-cachyos` the
regression list is unchanged for complexity and differs only in the ArrangerTimelineTest line count (689 -> 776,
a file already listed - no file of this unit's is newly listed).

Round 2 (r1 rework). The review's report - toggle left stale back/forward entries, so after zoom A->B and a
toggle to A, Back succeeded without changing the zoom - is fixed and proven: `takeToggle` now reconciles the
walk, taking the extent swapped to off the stack that holds it and queueing the extent left behind in its place
(`src/gui/ArrangerZoom.cpp:219-242`). Regression cases are in both surfaces
(`ArrangerTimelineTest::backAndForwardAfterTheToggleNeverNoOp`, `tests/src/gui/ArrangerTimelineTest.cpp:253`;
the "after the toggle, Back is refused..." check, `tests/control-customisation.py:357-365`), asserting not a
no-op but a real change or a typed refusal. Both drain loops are bounded and fail explicitly
(`drainZoomWalk` bound 64 = twice the walk's cap; the python counters at lines 353 and 367):

| Rework check | Command | EXIT |
| --- | --- | --- |
| Fix present | `cmake --build build --target zene ArrangerTimelineTest ReversibilityContractTest -- -j4` | 0 |
| GUI suites after the fix | `ctest --timeout 300 -R '^ArrangerTimelineTest$'`, `^ControlCommandsSnapshot$`, `^ReversibilityContractTest$`, `^DocFiguresGate$` (from `build/tests`) | 0 each |
| Agent surface after the fix | `python3 tests/control-customisation.py build/zene` | 0 |
| Negative control, GUI: with the round-1 `takeToggle` restored | `ctest --timeout 60 -R '^ArrangerTimelineTest$'` - fails at `backAndForwardAfterTheToggleNeverNoOp` with "Back after the toggle succeeded without changing the zoom", no timeout | 8 |
| Negative control, agent: same reversal | `python3 tests/control-customisation.py build/zene` - the "after the toggle..." check fails, the drain checks still end | 1 |


New reversibility rows: `arrangement.zoom_forward` and `arrangement.zoom_toggle` (`not_mutating`), and
`arrangement.zoom_back` re-worded. Snapshot + README canary 828 -> 830 ids / 76 groups. A16 block re-taken in
`docs/RELEASE-NOTES-v0.3.0-alpha.md`. `tests/upstream-modifications.txt` carries a reason line for
`tests/src/gui/ArrangerTimelineTest.cpp`.

Known limits (docs/050-features/Q050-176.md): the walk holds zoom extents only, not scroll positions; it is per
process and not saved with the project; forward after a new zoom is refused by design.
