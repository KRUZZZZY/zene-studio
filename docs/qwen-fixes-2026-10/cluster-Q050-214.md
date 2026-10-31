IMPLEMENTED

# Q050-214 - Plug-in editor gesture undo

Details and design: `docs/050-features/Q050-214.md`. Limits: `docs/KNOWN-LIMITATIONS.md`, "Plug-in editor gesture undo".

## Round 2: the review's blocking point

Two complete gestures on one parameter between GUI polls were one step, because aggregate counters and one sampled value were all the poll saw.
Now each mark crosses to the GUI on a bounded, preallocated, lock-free per-parameter queue (`include/PluginParamGesture.h`) with the value it was made at, and
`PluginGestureHistory::pull` (`include/PluginGestureHistory.h`) replays the marks in order, one journal step per nonempty gesture.

## Acceptance

Command: `cd build/tests && ctest -R '^PluginGestureUndoTest$' --output-on-failure`. Four subjects: CLAP effect, CLAP instrument,
VST3 effect, VST3 instrument, on the real main window and the product's hosting modules. All logs are in `runs/Q050-214.r2/logs/`.

| Criterion | Proof | Green | Red |
| --- | --- | --- | --- |
| Change a plug-in value with begin/end gesture events; one undo restores it and redo reapplies it | `checkOneGestureOneStep`, `checkWhatMakesAStep` | exit 0 | exit 8, all product hunks reversed: 4 of 4 subjects fail (`test-red-b2.log`) |
| Two complete gestures before one poll are two steps; two undos restore the intermediate value then the initial one, two redos reapply them in order (review round 1) | `checkTwoGesturesBeforeOnePoll`, `checkMarksAroundAPoll`, `checkMoreGesturesThanTheQueueHolds` | exit 0 | exit 8, product files back at round 1 (`8acc9aeb25`), so the gestures merge: 4 of 4 subjects fail (`test-red-a.log`) |
| GUI and agent history against the same journal checkpoint | `checkOneGestureOneStep` (Edit menu action, `control.undo`/`redo`, panel buttons clicked, on one step), `checkTheAgentsEdits`, `checkAnUndoInTheMiddleOfADrag`, `checkTwoGesturesBeforeOnePoll` (menu and agent) | exit 0 | exit 8, all product hunks reversed (`test-red-b2.log`). Only the `control.undo` hunk reversed: round 1's `RED2_EXIT=8`, not repeated in round 2, and that hunk did not change |
| The handoff keeps order and values, and says when it dropped | `PluginParamGestureTest`: single-thread order, full queue, position wrap, one and two threads reporting while a reader drains | exit 0; 30 repeats exit 0 (`test-repeat.log`) | exit 8, reports mutated to always drop: 5 of 5 cases fail (`test-red-c.log`). This test cannot compile without the header, so a plain reversal is not a red for it |

The full reversal kept the two new headers on disk, unused: the build globs headers for autogen and stops when they are missing (`build-red-b.log`, whose run did not rebuild and is discarded).

`test-final.log`: 24 of 24 passed, FINAL_EXIT=0: both tests above and the review's list of neighbours (gesture, shared-history and journal-bound tests among them). The whole suite was not run (the brief forbids it).

## Self-check

SELF_CHECK_PLACEHOLDER
