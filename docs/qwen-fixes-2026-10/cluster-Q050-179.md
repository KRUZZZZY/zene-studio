IMPLEMENTED

# Cluster Q050-179 — Show remaining record time in the UI

Every acceptance criterion met and proven by a test that was run on this tree (see the table and
the release log tails); branch `feat/050-Q050-179-oc` from train 5 `de5a85a96`.

## What was built

The shell header's transport readouts (the recording controls/status strip next to Rec mode) gained
a **Rec left** readout that displays the existing `record.remaining` command through the same
registered registry command the agent surface answers, and refreshes with the readouts' tick:

- `src/gui/shell/TransportReadouts.cpp:361,365,412-414` - the field, bound to
  `ControlRegistry::invoke("record.remaining")`;
- `src/gui/shell/TransportReadouts.cpp:279-293` / `include/TransportReadouts.h:114` -
  `recordRemainingLabel()`: "0m" (zero estimate), "2h 3m" (finite, minutes floor),
  "-" (unavailable: the command refused - it can never render as a time);
- `src/gui/shell/TransportReadouts.cpp:458-467` - the tick refreshes it once per second
  (`RecordRemainingEvery = 60` at the 60 Hz header update), so an armed route or the consumed free
  space moves the readout;
- `include/TransportReadouts.h:128,140` - the accessor the test uses;
- `include/TransportReadouts.h:126-130` / `src/gui/shell/TransportReadouts.cpp:62-64,412-414,442-445` -
  the review-rework-1 test seam: `TransportReadouts::setRemainingTestSource()` lets the tick test present
  a deterministic value that CHANGED since the bind instead of asking for the disk's real free space
  (the estimate follows the disk, which no test can move); a null restores the live command, the default;
- `tests/src/gui/RecordRemainingTest.cpp:104-127` - the tick test, reworked to the seam: the bind takes
  "2h 3m", then the answer becomes "1m", 59 ticks keep the old words, tick 60 (the once-a-second refresh)
  shows the change - and with the periodic refresh removed from `tick()` the test FAILS (logs `test-2-reversal.log`, EXIT 8);
- `data/manual/index.html` - the recording section now names the header readout, not just
  `record.remaining` for scripts and agents;
- no new command ids, no reversibility-table rows, no project-format change; the
  `record.remaining` handler is untouched, so the socket reply shape cannot have moved.

## Acceptance

| Criterion | Test run and result | Command | EXIT |
|---|---|---|---|
| The recording controls/status area display record.remaining and update as the budget changes | `RecordRemainingTest::theReadoutShowsTheCommandTheAgentsAreShown` (the bound readout equals a fresh `record.remaining` answer from the real engine) and `::theTickKeepsTheEstimateRunningWithTheBudget` (review rework r1: through the test seam the estimate deterministically CHANGES between the bind and the due refresh; ticks 1-59 keep "2h 3m", tick 60 shows "1m"; with the periodic refresh removed in `tick()` the same test fails, `logs/test-2-reversal.log` EXIT 8) | `ctest -R RecordRemainingTest --output-on-failure` (from `build/tests`) | 0 |
| A Qt GUI test covers zero, finite, and unavailable estimates | `aZeroEstimateSaysTheBudgetIsSpent` ("0m"), `aFiniteEstimateIsMinutesAndHours` ("1m", "2h 3m"), `anUnavailableEstimateSaysSoRatherThanZero` ("-") | same ctest run | 0 |
| The command's existing socket response remains unchanged | `tests/control-record-inputs.py` against the real binary over the socket: the reply keeps exactly the keys `seconds, armed_routes, free_bytes, directory` with their original types | `python3 tests/control-record-inputs.py build/zene` | 0 |

Full run tails (`logs/test-3.log`, `logs/socket-3.log`; the reversal of the rework-1 round is
`logs/test-2-reversal.log`, EXIT 8):

```
1/1 Test #421: RecordRemainingTest ..............   Passed    1.18 sec
100% tests passed out of 1
...
  record.remaining replies with the keys it has always shown       ok
  the reply holds a seconds estimate and the route count           ok
PASS: recording engine surface control-surface transcript (every check held)
```

Regression-adjacent tests (shell tests that own the readouts this unit extends) rebuilt and rerun:
`ctest -R "TaskIndicatorTest|ShellHomesTest|DevicePanelTest|WorkflowFeedbackTest"` exit 0, 4/4
(`build-4-neighbours.log`, `test-4-neighbours.log`).

## Registration

- `tests/fork-sources.txt` - the new fork test `tests/src/gui/RecordRemainingTest.cpp`, named by
  hand like its `tests/src/gui` neighbours (the recipe's keep-list does not cover `tests/src/gui`);
- `tests/all-sources.txt` - the same file, in C-sorted place after `RackMenuTest.cpp`;
- `tests/CMakeLists.txt` - the `RecordRemainingTest` target with the offscreen environment of its
  neighbours;
- `tests/upstream-modifications.txt` needs no entry: `TransportReadouts.h/.cpp` are already
  fork-registered (`tests/fork-sources.txt:1099,2049`) and no inherited file was touched.

## Gates

```
bash tests/complexity-gate.sh --check     EXIT=1   (red on train 5 by design; REGRESSION list md5
                                                    9b0e24bd4f3b5ea4a3b9bbf6bf581ef6, byte-identical
                                                    to ../zene-cachyos - no file of this unit new)
bash tests/file-length-gate.sh  --check   EXIT=1   (same reading; list md5 3023dc24ed33296a1290a0d76861b341
                                                    identical to ../zene-cachyos)
bash tests/fork-sources-gate.sh           EXIT=0
bash tests/all-sources-reproduce.sh       EXIT=0
bash tests/no-upstream-regression-gate.sh EXIT=0
tests/scripted/check-namespace            EXIT=0
bash tests/no-tautology-gate.sh           EXIT=0
```

## Known limits

- The Rec left words are `record.remaining`'s estimate verbatim (24-bit mono per route, one route
  when none is armed, the volume the armed routes write to). It does not invent a different model.
- "-" means the command refused (a volume it cannot measure); it is never shown as a time.
- The header refreshes once a second - the free space does not move at 60 Hz.
