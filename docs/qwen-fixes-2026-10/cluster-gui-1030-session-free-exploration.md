FIXED

# cluster gui-1030-session-free-exploration — a device with no host is labelled "Quarantined after repeated crashes"

## Verdict: REAL

- `oop.get_state` sets `hosting.refused = true` for **two** different states (`src/core/ControlCommandsOutOfProcessSupport.cpp:259-262,274`):
  `refused-crash-loop` (the family's client died `maxCrashesPerClient()` times) **and** `refused-no-client`
  (the family ships no client executable, `hostedDeviceState` at `ControlCommandsOutOfProcessSupport.cpp:241`). `familyFor()` answers
  any module not in the family table, TripleOscillator included, as `NoClientInBuild` (`src/core/OutOfProcessHosting.cpp:185-191`).
- The Device card read `refused` alone as "quarantined" (`src/gui/shell/DeviceCardState.cpp`, old `problemOf`, line 51), so every in-process
  instrument showed "Quarantined after repeated crashes" with no crash ever having happened.
- Fix: a `refused-no-client` state reports no problem (the device runs in this process by design). Everything else is unchanged:
  `refused-crash-loop`, and `refused` with any other state, still say "quarantined"; `client-exited` still says "crashed".
- Seam: `problemOf` moved out of the anonymous namespace and renamed `devicestate::hostingProblemOf(const QJsonObject&)`, declared in
  `include/DeviceCardState.h`, so the mapping is testable without a crashing client. `oop.get_state`'s own `refused_count` (which counts
  `refused-*`) is untouched, as is the control surface.

## Test

`DevicePanelTest::aDeviceWithoutAHostIsNotCalledQuarantined` (tests/src/gui/DevicePanelTest.cpp), deterministic, no timing:
1. the mapping for each state/`refused` combination (no-client → none; crash-loop → quarantined, with `refused` true or false;
   separate-process + refused → quarantined, kept; client-exited → crashed; in-process / separate-process → none);
2. real path: a track with a real TripleOscillator; `oop.get_state` reports `refused-no-client`; `hostingProblems()` has no entry for it;
   the Device card's state has an empty `hosting` and its text does not contain "Quarantined".

## Evidence (all run from the worktree; `CCACHE_DISABLE=1` because ccache's temp dir is outside the write-jail)

| step | command | exit |
|---|---|---|
| RED (unfixed logic, seam in place) | `ctest -R '^DevicePanelTest$' --output-on-failure` in build/tests → `logs/test-1-red.log`: `Actual: "quarantined"` / `Expected: ""` for `refused-no-client` | 8 |
| GREEN | rebuild `cmake --build build --target DevicePanelTest -j4` (`build-4.log`) then ctest (`test-3-green.log`) | 0 / 0 |
| 4b revert the one-line fix only | `git apply -R`-equivalent (`logs/fix.patch` is the single logic hunk; the seam stays so the test compiles), rebuild (`build-5-revert.log`), ctest (`test-4-revert-red.log`): same RED assertion | 0 / 8 |
| 4b re-apply | `git apply logs/fix.patch` (exit 0, file byte-identical to the fixed copy), rebuild, ctest (`test-5-green.log`) | 0 / 0 |
| family | `DevicePanelTest` (the full test, 32 cases: 31 before + 1 new) and `DeviceAbUndoTest` are the only tests built from these files; DevicePanelTest ran 1/1 ctest test, 100% passed | 0 |
| gates | `feature_check.py` (local copy, only the log dir redirected; the stock script mkdirs a sibling of the worktree, refused by the jail): fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity, file-length all PASS (`logs/gates-1.log`) | 0 |

The RED/GREEN counts: before the new case 31 passed; with the unfixed logic `Totals: 31 passed, 1 failed`.

## Files committed
`src/gui/shell/DeviceCardState.cpp`, `include/DeviceCardState.h`, `tests/src/gui/DevicePanelTest.cpp`, `tests/upstream-modifications.txt`
(DevicePanelTest reason line), this report. No new files, no new control commands, nothing on the audio thread.

## Not covered
No real crashing client was driven (no deterministic way to crash one here); the crash-loop path is covered through the JSON seam only.
The "JO-ID ... already in use by midiclip!" log line in the finding is a separate matter and not addressed by this cluster.
