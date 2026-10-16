IMPLEMENTED

Q050-176 - Zoom history with back and forward (L13, WF-094 / B-WF-094). The arranger's zoom walk is real back /
forward now: three prior extents visited, back and forward restore each exactly in order, both directions refuse
at their ends, a new zoom cuts the forward walk off, and `arrangement.zoom_toggle` swaps the current extent with
the one the zoom last left (twice returns you). `arrangement.zoom_back` keeps its id and its refusal; a GUI
(Navigate > Zoom Back / Zoom Forward / Zoom Toggle) and the agent surface run the same registered handlers.

Acceptance (commands as run from `build/tests`; exit codes are the ctest exits):

| Criterion | Command | EXIT |
| --- | --- | --- |
| Visit at least three distinct arrangement zoom levels, exercise back and forward, and verify each prior extent is restored | `ctest -R ArrangerTimelineTest --output-on-failure` (QtTest `backAndForwardWalkSeveralPriorZooms`, `tests/src/gui/ArrangerTimelineTest.cpp:163`; 29 slots passed) | 0 |
| The same walk seen over the agent surface | `ctest -R ControlCustomisation --output-on-failure` (`tests/control-customisation.py:334`: three `arrangement.zoom {pixels_per_bar}` levels, then back / forward / back / back restoring each, refusal checks, drain) | 0 |
| Use zoom toggle to swap between the current and prior extent | `ctest -R ArrangerTimelineTest --output-on-failure` (`zoomToggleSwapsTheCurrentAndPriorExtent`) and the same transcript's `zoom_toggle` rows in `ControlCustomisation` | 0 |
| The walk's contract rows / histogram follow | `ctest -R ReversibilityContractTest --output-on-failure` (published figure re-taken: 839 rows, 358 not_mutating) | 0 |
| Snapshot fresh against the binary | `ctest -R ControlCommandsSnapshot --output-on-failure` (830 ids / 76 groups, regenerated live) | 0 |
| Registry contract rows match | `ctest -R ControlRegistryTest --output-on-failure` | 0 |

Gates (from the repo root): `tests/scripted/check-namespace` 0; `bash tests/no-tautology-gate.sh` 0;
`bash tests/fork-sources-gate.sh` 0; `bash tests/all-sources-reproduce.sh` 0;
`bash tests/no-upstream-regression-gate.sh de5a85a96` 0; `python3 tests/doc-figures-gate.py` 0.
Gate 4 (complexity) and gate 7 (file length) exit 1 as train 5 leaves them; against `../zene-cachyos` the
regression list is unchanged for complexity and differs only in the ArrangerTimelineTest line count (689 -> 742,
a file already listed - no file of this unit's is newly listed).

New reversibility rows: `arrangement.zoom_forward` and `arrangement.zoom_toggle` (`not_mutating`), and
`arrangement.zoom_back` re-worded. Snapshot + README canary 828 -> 830 ids / 76 groups. A16 block re-taken in
`docs/RELEASE-NOTES-v0.3.0-alpha.md`. `tests/upstream-modifications.txt` carries a reason line for
`tests/src/gui/ArrangerTimelineTest.cpp`.

Known limits (docs/050-features/Q050-176.md): the walk holds zoom extents only, not scroll positions; it is per
process and not saved with the project; forward after a new zoom is refused by design.
